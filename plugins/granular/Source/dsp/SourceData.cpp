#include "SourceData.h"
#include "DspCore.h"

namespace thf::grain
{
    namespace
    {
        // Linear-phase low-pass for 2:1 decimation. Passband to 0.21 fs keeps the top of each
        // octave copy clean; stopband starts below the new Nyquist.
        struct DecimationFilter
        {
            static constexpr int taps = 63;
            static constexpr int centre = taps / 2;
            std::array<float, taps> h {};

            DecimationFilter()
            {
                constexpr double fc = 0.21, beta = 8.0;
                auto i0 = [] (double x)
                {
                    double sum = 1.0, term = 1.0;
                    for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
                    return sum;
                };
                double total = 0.0;
                for (int n = 0; n < taps; ++n)
                {
                    const double m = n - centre;
                    const double x = 2.0 * fc * m;
                    const double sinc = n == centre ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                    const double r = m / centre;
                    const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
                    h[(size_t) n] = (float) (2.0 * fc * sinc * w);
                    total += h[(size_t) n];
                }
                for (auto& v : h) v = (float) (v / total);
            }
        };

        juce::AudioBuffer<float> padded (int channels, int length)
        {
            juce::AudioBuffer<float> b (channels, length + 2 * SourceData::padding);
            b.clear();
            return b;
        }
    }

    SourceData::Ptr SourceData::fromBuffer (const juce::AudioBuffer<float>& audio, double rate, const juce::String& sourceName)
    {
        Ptr s (new SourceData());
        s->numChannels = juce::jlimit (1, 2, audio.getNumChannels());
        s->length = juce::jmax (1, audio.getNumSamples());
        s->sampleRate = rate > 0.0 ? rate : 48000.0;
        s->name = sourceName;

        // Level 0: sanitised copy.
        auto level0 = padded (s->numChannels, s->length);
        for (int ch = 0; ch < s->numChannels; ++ch)
        {
            auto* dst = level0.getWritePointer (ch) + padding;
            if (audio.getNumSamples() == 0)
                break;
            const auto* src = audio.getReadPointer (ch);
            for (int i = 0; i < s->length; ++i)
                dst[i] = std::isfinite (src[i]) ? juce::jlimit (-4.0f, 4.0f, src[i]) : 0.0f;
        }
        s->levels.push_back (std::move (level0));
        s->levelLengths.push_back (s->length);

        // Octave copies.
        static const DecimationFilter filter;
        for (int level = 1; level < maxLevels; ++level)
        {
            const auto prevLength = s->levelLengths.back();
            const auto newLength = (prevLength + 1) / 2;
            if (newLength < 64)
                break;
            auto next = padded (s->numChannels, newLength);
            const auto& prev = s->levels.back();
            for (int ch = 0; ch < s->numChannels; ++ch)
            {
                const auto* src = prev.getReadPointer (ch) + padding;
                auto* dst = next.getWritePointer (ch) + padding;
                for (int m = 0; m < newLength; ++m)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < DecimationFilter::taps; ++k)
                    {
                        const int idx = 2 * m + DecimationFilter::centre - k;
                        if (idx >= 0 && idx < prevLength)
                            sum += filter.h[(size_t) k] * src[idx];
                    }
                    dst[m] = sum;
                }
            }
            s->levels.push_back (std::move (next));
            s->levelLengths.push_back (newLength);
        }

        // Peak overview.
        s->peakMin.assign (overviewSize, 0.0f);
        s->peakMax.assign (overviewSize, 0.0f);
        for (int b = 0; b < overviewSize; ++b)
        {
            const auto start = (int) ((int64_t) b * s->length / overviewSize);
            const auto end = juce::jmax (start + 1, (int) ((int64_t) (b + 1) * s->length / overviewSize));
            float lo = 0.0f, hi = 0.0f;
            for (int ch = 0; ch < s->numChannels; ++ch)
            {
                const auto* d = s->channel (0, ch);
                for (int i = start; i < juce::jmin (end, s->length); ++i)
                {
                    lo = juce::jmin (lo, d[i]);
                    hi = juce::jmax (hi, d[i]);
                }
            }
            s->peakMin[(size_t) b] = lo;
            s->peakMax[(size_t) b] = hi;
        }
        return s;
    }

    juce::AudioBuffer<float> SourceData::copyOriginal() const
    {
        juce::AudioBuffer<float> out (numChannels, length);
        for (int ch = 0; ch < numChannels; ++ch)
            out.copyFrom (ch, 0, channel (0, ch), length);
        return out;
    }

    namespace sources
    {
        namespace
        {
            juce::AudioFormatManager& formats()
            {
                static const auto manager = []
                {
                    auto m = std::make_unique<juce::AudioFormatManager>();
                    m->registerBasicFormats();
                    return m;
                }();
                return *manager;
            }

            SourceData::Ptr fromReader (juce::AudioFormatReader* rawReader, const juce::String& name, juce::String& error)
            {
                std::unique_ptr<juce::AudioFormatReader> reader (rawReader);
                if (reader == nullptr)
                {
                    error = "Unsupported or damaged audio file";
                    return nullptr;
                }
                if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
                {
                    error = "The file contains no audio";
                    return nullptr;
                }
                if ((double) reader->lengthInSamples / reader->sampleRate > maxFileSeconds)
                {
                    error = "The file is longer than 10 minutes";
                    return nullptr;
                }
                const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);
                juce::AudioBuffer<float> audio (channels, (int) reader->lengthInSamples);
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, channels > 1);
                auto source = SourceData::fromBuffer (audio, reader->sampleRate, name);
                source->contentHash = hashOf (audio);
                return source;
            }

            constexpr double genRate = 48000.0;
            constexpr double genSeconds = 4.0;
            constexpr double rootHz = 261.6255653;   // MIDI note 60

            // Naive saw corrected with polyBLEP at the discontinuity.
            float polyBlepSaw (double& phase, double inc)
            {
                auto t = phase;
                auto v = 2.0 * t - 1.0;
                if (t < inc)             { t /= inc;               v -= t + t - t * t - 1.0; }
                else if (t > 1.0 - inc)  { t = (t - 1.0) / inc;    v -= t * t + t + t + 1.0; }
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
                return (float) v;
            }

            void normalise (juce::AudioBuffer<float>& b, float peak)
            {
                const auto m = b.getMagnitude (0, b.getNumSamples());
                if (m > 0.0f) b.applyGain (peak / m);
            }

            juce::AudioBuffer<float> sawPad()
            {
                const int n = (int) (genRate * genSeconds);
                juce::AudioBuffer<float> b (2, n);
                constexpr double cents[] = { -11.0, -4.0, 0.0, 4.5, 10.0 };
                for (int ch = 0; ch < 2; ++ch)
                {
                    double phases[5];
                    for (int v = 0; v < 5; ++v) phases[v] = std::fmod (0.137 * (v + 1) + 0.31 * ch, 1.0);
                    dsp::Svf lp1, lp2;
                    dsp::SvfCoefs c;
                    auto* d = b.getWritePointer (ch);
                    for (int i = 0; i < n; ++i)
                    {
                        const auto t = (double) i / n;
                        if (i % 32 == 0)
                        {
                            const auto bright = 700.0 * std::pow (9.0, std::sin (juce::MathConstants<double>::pi * t));
                            c.set ((float) bright, 0.15f, (float) genRate);
                        }
                        float sum = 0.0f;
                        for (int v = 0; v < 5; ++v)
                        {
                            const auto detune = cents[v] * (ch == 0 ? 1.0 : -1.0);
                            sum += polyBlepSaw (phases[v], rootHz * std::pow (2.0, detune / 1200.0) / genRate);
                        }
                        const auto swell = 0.6f + 0.4f * (float) std::sin (juce::MathConstants<double>::pi * t);
                        d[i] = lp2.process (lp1.process (sum * 0.2f, c, dsp::Svf::Type::lowPass), c, dsp::Svf::Type::lowPass) * swell;
                    }
                }
                normalise (b, 0.7f);
                return b;
            }

            juce::AudioBuffer<float> voice()
            {
                const int n = (int) (genRate * genSeconds);
                juce::AudioBuffer<float> b (1, n);
                struct Vowel { float f1, f2, f3; };
                constexpr Vowel vowels[] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                             { 570, 840, 2410 },  { 300, 870, 2240 } };
                dsp::Svf f[3];
                dsp::SvfCoefs c[3];
                double phase = 0.0;
                auto* d = b.getWritePointer (0);
                for (int i = 0; i < n; ++i)
                {
                    const auto t = (double) i / n;
                    if (i % 32 == 0)
                    {
                        const auto pos = t * 4.0;
                        const auto k = juce::jmin (3, (int) pos);
                        const auto frac = (float) (pos - k);
                        const auto& a = vowels[k];
                        const auto& z = vowels[k + 1];
                        c[0].set (a.f1 + frac * (z.f1 - a.f1), 0.85f, (float) genRate);
                        c[1].set (a.f2 + frac * (z.f2 - a.f2), 0.88f, (float) genRate);
                        c[2].set (a.f3 + frac * (z.f3 - a.f3), 0.9f, (float) genRate);
                    }
                    const auto vibrato = std::pow (2.0, 0.15 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.2 * i / genRate));
                    const auto src = polyBlepSaw (phase, rootHz * vibrato / genRate);
                    d[i] = f[0].process (src, c[0], dsp::Svf::Type::bandPass)
                         + 0.5f * f[1].process (src, c[1], dsp::Svf::Type::bandPass)
                         + 0.25f * f[2].process (src, c[2], dsp::Svf::Type::bandPass);
                }
                normalise (b, 0.7f);
                return b;
            }

            juce::AudioBuffer<float> bell()
            {
                const int n = (int) (genRate * genSeconds);
                juce::AudioBuffer<float> b (2, n);
                b.clear();
                constexpr double ratios[] = { 1.4, 2.0, 3.5, 1.17 };
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    for (int strike = 0; strike < 4; ++strike)
                    {
                        const int start = (int) (strike * genRate);
                        const auto ratio = ratios[strike] * (ch == 0 ? 1.0 : 1.003);
                        for (int i = start; i < n; ++i)
                        {
                            const auto t = (i - start) / genRate;
                            const auto index = 4.0 * std::exp (-t / 0.5);
                            const auto amp = std::exp (-t / 1.1);
                            const auto w = 2.0 * juce::MathConstants<double>::pi * rootHz * t;
                            d[i] += (float) (amp * std::sin (w + index * std::sin (w * ratio)));
                        }
                    }
                }
                normalise (b, 0.7f);
                return b;
            }

            juce::AudioBuffer<float> noise()
            {
                const int n = (int) (genRate * genSeconds);
                juce::AudioBuffer<float> b (2, n);
                dsp::Random rng (1234);
                for (int ch = 0; ch < 2; ++ch)
                {
                    float b0 = 0, b1 = 0, b2 = 0;    // pink noise, Paul Kellet's economy filter
                    dsp::Svf bp;
                    dsp::SvfCoefs c;
                    auto* d = b.getWritePointer (ch);
                    for (int i = 0; i < n; ++i)
                    {
                        if (i % 32 == 0)
                            c.set ((float) (150.0 * std::pow (60.0, (double) i / n)), 0.35f, (float) genRate);
                        const auto white = rng.bipolar();
                        b0 = 0.99765f * b0 + white * 0.0990460f;
                        b1 = 0.96300f * b1 + white * 0.2965164f;
                        b2 = 0.57000f * b2 + white * 1.0526913f;
                        const auto pink = b0 + b1 + b2 + white * 0.1848f;
                        d[i] = bp.process (pink, c, dsp::Svf::Type::bandPass);
                    }
                }
                normalise (b, 0.7f);
                return b;
            }

            juce::AudioBuffer<float> glass()
            {
                const int n = (int) (genRate * genSeconds);
                juce::AudioBuffer<float> b (2, n);
                constexpr double partials[] = { 1.0, 2.32, 4.25, 6.63, 9.38, 12.1 };
                constexpr double amps[] = { 1.0, 0.6, 0.42, 0.28, 0.17, 0.1 };
                constexpr double shimmer[] = { 0.23, 0.41, 0.67, 0.93, 1.3, 1.7 };
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    for (int i = 0; i < n; ++i)
                    {
                        const auto t = i / genRate;
                        double sum = 0.0;
                        for (int p = 0; p < 6; ++p)
                        {
                            const auto am = 0.55 + 0.45 * std::sin (2.0 * juce::MathConstants<double>::pi * shimmer[p] * t + p + ch * 1.7);
                            sum += amps[p] * am * std::sin (2.0 * juce::MathConstants<double>::pi * rootHz * partials[p] * t + p * 0.7 + ch * 0.3);
                        }
                        d[i] = (float) sum;
                    }
                }
                normalise (b, 0.7f);
                return b;
            }
        }

        SourceData::Ptr loadFile (const juce::File& file, juce::String& error)
        {
            auto source = fromReader (formats().createReaderFor (file), file.getFileName(), error);
            if (source != nullptr)
                source->file = file;
            return source;
        }

        SourceData::Ptr loadFromMemory (const void* data, size_t size, const juce::String& name, juce::String& error)
        {
            auto stream = std::make_unique<juce::MemoryInputStream> (data, size, false);
            return fromReader (formats().createReaderFor (std::move (stream)), name, error);
        }

        juce::MemoryBlock encodeFlac (const juce::AudioBuffer<float>& audio, double sampleRate)
        {
            juce::MemoryBlock block;
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
            const auto options = juce::AudioFormatWriterOptions()
                                     .withSampleRate (sampleRate)
                                     .withNumChannels (audio.getNumChannels())
                                     .withBitsPerSample (24);
            if (auto writer = flac.createWriterFor (stream, options))
            {
                writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
                writer.reset();   // flushes into block
                return block;
            }
            return {};
        }

        SourceData::Ptr generate (int choice)
        {
            juce::AudioBuffer<float> audio;
            switch (choice)
            {
                case 1: audio = sawPad(); break;
                case 2: audio = voice();  break;
                case 3: audio = bell();   break;
                case 4: audio = noise();  break;
                case 5: audio = glass();  break;
                default: return nullptr;
            }
            return SourceData::fromBuffer (audio, genRate, "");
        }

        juce::String hashOf (const juce::AudioBuffer<float>& audio)
        {
            // FNV-1a over the raw samples: identifies a sample, not a security feature.
            uint64_t h = 0xcbf29ce484222325ull;
            auto mix = [&h] (const void* data, size_t bytes)
            {
                auto* p = static_cast<const unsigned char*> (data);
                for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
            };
            const int dims[] = { audio.getNumChannels(), audio.getNumSamples() };
            mix (dims, sizeof (dims));
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                mix (audio.getReadPointer (ch), sizeof (float) * (size_t) audio.getNumSamples());
            return juce::String::toHexString ((juce::int64) h);
        }
    }
}
