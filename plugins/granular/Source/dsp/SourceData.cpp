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
            // Every level is built, even for tiny sources: the grain reader relies on the
            // top level bringing any ratio <= 32 down to an effective ratio <= 1.
            const auto newLength = juce::jmax (1, (prevLength + 1) / 2);
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

        // Level normalisation: peak to 0.7 (-3 dBFS), never more than +30 dB.
        {
            float peak = 0.0f;
            for (int ch = 0; ch < s->numChannels; ++ch)
            {
                const auto* d = s->channel (0, ch);
                for (int i = 0; i < s->length; ++i)
                    peak = juce::jmax (peak, std::abs (d[i]));
            }
            s->normalGain = peak > 1.0e-6f ? juce::jmin (31.6f, 0.7f / peak) : 1.0f;
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
                detectPitch (*source);
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

            // A sung phrase: pitched vowels gliding through a minor-pentatonic line, so
            // scanning or spraying across it gives pitched vocal chops.
            juce::AudioBuffer<float> vocalPhrase()
            {
                constexpr double seconds = 6.0;
                const int n = (int) (genRate * seconds);
                juce::AudioBuffer<float> b (1, n);
                struct Vowel { float f1, f2, f3; };
                constexpr Vowel vowels[] = { { 730, 1090, 2440 }, { 570, 840, 2410 }, { 530, 1840, 2480 },
                                             { 300, 870, 2240 }, { 270, 2290, 3010 } };
                constexpr int melody[] = { 0, 3, 7, 5, 10, 7, 12, 3 };
                constexpr int notes = (int) std::size (melody);
                const auto noteLength = n / notes;
                dsp::Svf f[3];
                dsp::SvfCoefs c[3];
                dsp::Random rng (77);
                double phase = 0.0, currentSemis = melody[0];
                auto* d = b.getWritePointer (0);
                for (int i = 0; i < n; ++i)
                {
                    const auto k = juce::jmin (notes - 1, i / noteLength);
                    const auto inNote = (double) (i - k * noteLength) / genRate;
                    currentSemis += (melody[k] - currentSemis) * (1.0 - std::exp (-1.0 / (0.03 * genRate)));   // portamento
                    if (i % 32 == 0)
                    {
                        const auto& a = vowels[k % 5];
                        const auto& z = vowels[(k + 1) % 5];
                        const auto frac = (float) juce::jlimit (0.0, 1.0, inNote / ((double) noteLength / genRate));
                        c[0].set (a.f1 + frac * frac * (z.f1 - a.f1), 0.86f, (float) genRate);
                        c[1].set (a.f2 + frac * frac * (z.f2 - a.f2), 0.88f, (float) genRate);
                        c[2].set (a.f3 + frac * frac * (z.f3 - a.f3), 0.9f, (float) genRate);
                    }
                    const auto vibratoDepth = juce::jlimit (0.0, 1.0, (inNote - 0.2) * 3.0) * 0.25;
                    const auto vibrato = vibratoDepth * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * i / genRate);
                    const auto hz = rootHz * std::pow (2.0, (currentSemis + vibrato) / 12.0);
                    const auto glottal = polyBlepSaw (phase, hz / genRate);
                    const auto breath = rng.bipolar() * 0.08f;
                    const auto src = glottal + breath;
                    const auto env = (float) (juce::jlimit (0.0, 1.0, inNote / 0.04)
                                              * (0.75 + 0.25 * std::cos (juce::MathConstants<double>::pi * inNote / ((double) noteLength / genRate))));
                    d[i] = env * (f[0].process (src, c[0], dsp::Svf::Type::bandPass)
                                  + 0.55f * f[1].process (src, c[1], dsp::Svf::Type::bandPass)
                                  + 0.3f * f[2].process (src, c[2], dsp::Svf::Type::bandPass));
                }
                normalise (b, 0.7f);
                return b;
            }

            // Wide detuned-saw chords, one per second: I maj9, vi m9, IV maj9, V sus.
            juce::AudioBuffer<float> chordStack()
            {
                const int n = (int) (genRate * 4.0);
                juce::AudioBuffer<float> b (2, n);
                b.clear();
                constexpr int chords[4][5] = { { 0, 4, 7, 11, 14 }, { -3, 0, 4, 7, 11 },
                                               { -7, -3, 0, 4, 7 }, { -5, 0, 2, 7, 12 } };
                constexpr double detune[] = { -14.0, 0.0, 13.0 };
                const int chordLength = n / 4;
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = b.getWritePointer (ch);
                    dsp::Svf lp1, lp2;
                    dsp::SvfCoefs c;
                    double phases[5][3];
                    for (int v = 0; v < 5; ++v)
                        for (int k = 0; k < 3; ++k)
                            phases[v][k] = std::fmod (0.113 * (v + 1) * (k + 2) + 0.29 * ch, 1.0);
                    for (int i = 0; i < n; ++i)
                    {
                        const auto chord = juce::jmin (3, i / chordLength);
                        const auto inChord = (double) (i - chord * chordLength) / chordLength;
                        if (i % 32 == 0)
                            c.set ((float) (1800.0 + 4200.0 * std::exp (-inChord * 3.0)), 0.2f, (float) genRate);
                        float sum = 0.0f;
                        for (int v = 0; v < 5; ++v)
                            for (int k = 0; k < 3; ++k)
                            {
                                const auto cents = detune[k] * (ch == 0 ? 1.0 : -1.0) + (k == 1 ? 0.0 : 3.0 * v);
                                const auto hz = rootHz * std::pow (2.0, chords[chord][v] / 12.0 + cents / 1200.0);
                                sum += polyBlepSaw (phases[v][k], hz / genRate);
                            }
                        const auto env = (float) (juce::jlimit (0.0, 1.0, inChord * 40.0) * (1.0 - 0.3 * inChord));
                        d[i] = lp2.process (lp1.process (sum * 0.07f * env, c, dsp::Svf::Type::lowPass), c, dsp::Svf::Type::lowPass);
                    }
                }
                normalise (b, 0.7f);
                return b;
            }

            // Plucked strings (Karplus-Strong) playing a pentatonic line, 0.5 s per note.
            juce::AudioBuffer<float> pluck()
            {
                const int n = (int) (genRate * 4.0);
                juce::AudioBuffer<float> b (2, n);
                b.clear();
                constexpr int line[] = { 0, 2, 4, 7, 9, 12, 7, 4 };
                constexpr int mask = 4095;
                const int noteLength = n / 8;
                dsp::Random rng (5);
                std::vector<float> ring (mask + 1);
                for (int note = 0; note < 8; ++note)
                {
                    const auto period = genRate / (rootHz * std::pow (2.0, line[note] / 12.0));
                    const auto delay = period - 0.5;   // the two-point average adds half a sample
                    std::fill (ring.begin(), ring.end(), 0.0f);
                    const int start = note * noteLength;
                    const int end = juce::jmin (n, start + (int) (2.0 * genRate));
                    for (int i = start, w = 0; i < end; ++i, w = (w + 1) & mask)
                    {
                        auto at = [&] (double back)
                        {
                            const auto pos = (double) w - back;
                            const auto i0 = (int) std::floor (pos);
                            const auto f = (float) (pos - i0);
                            const auto x0 = ring[(size_t) (i0 & mask)], x1 = ring[(size_t) ((i0 + 1) & mask)];
                            return x0 + f * (x1 - x0);
                        };
                        const auto excite = (i - start) < (int) period ? rng.bipolar() : 0.0f;
                        const auto y = excite + 0.996f * 0.5f * (at (delay) + at (delay + 1.0));
                        ring[(size_t) w] = y;
                        b.addSample (0, i, y);
                        if (i + 23 < n) b.addSample (1, i + 23, y);   // a little width
                    }
                }
                normalise (b, 0.7f);
                return b;
            }

            // Soft electric-piano tones (FM tine + body) as a broken chord, with tremolo and a
            // little vinyl-like crackle.
            juce::AudioBuffer<float> keys()
            {
                const int n = (int) (genRate * 4.0);
                juce::AudioBuffer<float> b (2, n);
                b.clear();
                constexpr int line[] = { 0, 4, 7, 11, 12, 7, 4, 2 };
                const int noteLength = n / 8;
                for (int note = 0; note < 8; ++note)
                {
                    const auto hz = rootHz * std::pow (2.0, line[note] / 12.0);
                    const int start = note * noteLength;
                    for (int i = start; i < n; ++i)
                    {
                        const auto t = (i - start) / genRate;
                        if (t > 2.5) break;
                        const auto w = 2.0 * juce::MathConstants<double>::pi * hz * t;
                        const auto tine = std::exp (-t * 9.0) * 1.2;
                        const auto body = std::exp (-t * 1.4);
                        const auto v = body * std::sin (w + tine * std::sin (w * 14.0) + 0.6 * body * std::sin (w));
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            const auto trem = 1.0 + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 4.8 * i / genRate + ch * juce::MathConstants<double>::pi);
                            b.addSample (ch, i, (float) (v * trem));
                        }
                    }
                }
                dsp::Random rng (9);
                for (int i = 0; i < n; ++i)
                    if (rng.uniform() < 0.0006f)
                    {
                        const auto click = rng.bipolar() * 0.08f;
                        b.addSample (0, i, click);
                        b.addSample (1, i, click);
                    }
                normalise (b, 0.7f);
                return b;
            }
        }

        juce::String audioExtensions()
        {
            // Everything the format manager reads (CoreAudio adds mp3/m4a/caf on macOS).
            juce::StringArray extensions;
            for (auto* format : formats())
                for (auto ext : format->getFileExtensions())
                    extensions.addIfNotAlreadyThere (ext.trimCharactersAtStart ("."));
            return extensions.joinIntoString (";");
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
                case 6: audio = vocalPhrase(); break;
                case 7: audio = chordStack(); break;
                case 8: audio = pluck(); break;
                case 9: audio = keys(); break;
                default: return nullptr;
            }
            return SourceData::fromBuffer (audio, genRate, "");
        }

        void detectPitch (SourceData& s)
        {
            // YIN (de Cheveigné & Kawahara) on up to 8 of the loudest 46 ms windows of the
            // mono mix; the median of confident estimates wins.
            const auto rate = s.getSampleRate();
            const int window = juce::nextPowerOfTwo ((int) (0.046 * rate));
            const int maxLag = juce::jmin (window - 2, (int) (rate / 40.0));    // down to 40 Hz
            const int minLag = juce::jmax (2, (int) (rate / 2000.0));           // up to 2 kHz
            const auto length = s.getLength();
            if (length < 2 * window)
                return;

            std::vector<float> mono ((size_t) length);
            for (int ch = 0; ch < s.getNumChannels(); ++ch)
            {
                const auto* d = s.channel (0, ch);
                for (int i = 0; i < length; ++i) mono[(size_t) i] += d[i];
            }

            std::vector<std::pair<float, int>> energies;
            for (int start = 0; start + 2 * window < length; start += window / 2)
            {
                float e = 0.0f;
                for (int i = 0; i < window; ++i) e += mono[(size_t) (start + i)] * mono[(size_t) (start + i)];
                energies.push_back ({ e, start });
            }
            std::sort (energies.begin(), energies.end(), [] (auto& a, auto& b) { return a.first > b.first; });

            std::vector<float> diff ((size_t) maxLag + 2), notes;
            float confidenceSum = 0.0f;
            for (size_t w = 0; w < juce::jmin ((size_t) 8, energies.size()); ++w)
            {
                const auto* x = mono.data() + energies[w].second;
                for (int tau = 1; tau <= maxLag; ++tau)
                {
                    float sum = 0.0f;
                    for (int i = 0; i < window; ++i) { const auto d = x[i] - x[i + tau]; sum += d * d; }
                    diff[(size_t) tau] = sum;
                }
                // Cumulative mean normalised difference.
                float running = 0.0f;
                diff[0] = 1.0f;
                for (int tau = 1; tau <= maxLag; ++tau)
                {
                    running += diff[(size_t) tau];
                    diff[(size_t) tau] = running > 0.0f ? diff[(size_t) tau] * (float) tau / running : 1.0f;
                }
                int best = -1;
                for (int tau = minLag; tau < maxLag; ++tau)
                    if (diff[(size_t) tau] < 0.15f)
                    {
                        while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                        best = tau;
                        break;
                    }
                if (best < 0)
                    continue;
                const auto a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
                const auto denom = a - 2.0f * b + c;
                const auto shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
                const auto hz = rate / ((double) best + shift);
                notes.push_back ((float) (69.0 + 12.0 * std::log2 (hz / 440.0)));
                confidenceSum += 1.0f - b;
            }
            if (notes.size() < 3)
                return;
            std::sort (notes.begin(), notes.end());
            const auto median = notes[notes.size() / 2];
            int agreeing = 0;
            for (auto note : notes) if (std::abs (note - median) < 0.5f) ++agreeing;
            s.detectedNote = median;
            s.pitchConfidence = juce::jlimit (0.0f, 1.0f, (confidenceSum / (float) notes.size()) * (float) agreeing / (float) notes.size());
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
