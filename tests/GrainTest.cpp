// DSP and processor regression tests for thf Grain.

#include <PluginProcessor.h>
#include <PluginEditor.h>
#include <SampleLibrary.h>
#include <i18n/Translator.h>
#include <juce_dsp/juce_dsp.h>

namespace thf::test
{
    extern std::atomic<bool> countAllocations;
    extern std::atomic<long> allocations;
}

using namespace thf::grain;

namespace
{
    constexpr double rate = 48000.0;

    SourceData::Ptr sineSource (double freq, double seconds, double sourceRate = rate, float amp = 0.5f)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * sourceRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sourceRate));
        return SourceData::fromBuffer (b, sourceRate, "sine");
    }

    SourceData::Ptr noiseSource (double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        juce::Random r (42);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, r.nextFloat() - 0.5f);
        return SourceData::fromBuffer (b, rate, "noise");
    }

    EngineParams plain()
    {
        EngineParams p;
        p.chaos = 0.0f;
        p.spray = 0.0f;
        p.jitter = 0.0f;
        p.stereo = 0.0f;
        p.attackMs = 1.0f;
        p.decayMs = 10.0f;
        p.sustain = 1.0f;
        p.releaseMs = 20.0f;
        p.velocitySens = 0.0f;
        p.lfoDepth = 0.0f;
        p.safeClip = false;
        return p;
    }

    // Renders `samples` of the left channel.
    std::vector<float> render (GrainEngine& e, const EngineParams& p, int samples, std::vector<float>* rightOut = nullptr)
    {
        std::vector<float> l ((size_t) samples), r ((size_t) samples);
        for (int done = 0; done < samples; done += 512)
        {
            const auto n = std::min (512, samples - done);
            e.render (l.data() + done, r.data() + done, n, p);
        }
        if (rightOut != nullptr) *rightOut = r;
        return l;
    }

    double rms (const std::vector<float>& x, size_t from = 0)
    {
        double s = 0.0;
        for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) std::max<size_t> (1, x.size() - from));
    }

    double dominantFrequency (const std::vector<float>& x, double sr)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data (2 * size, 0.0f);
        const auto offset = x.size() > (size_t) size ? x.size() - (size_t) size : 0;
        for (int i = 0; i < size && offset + (size_t) i < x.size(); ++i)
            data[(size_t) i] = x[offset + (size_t) i] * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / size));
        fft.performFrequencyOnlyForwardTransform (data.data());
        int best = 1;
        for (int i = 1; i < size / 2 - 1; ++i)
            if (data[(size_t) i] > data[(size_t) best]) best = i;
        const auto a = data[(size_t) best - 1], b = data[(size_t) best], c = data[(size_t) best + 1];
        const auto shift = 0.5 * (a - c) / (a - 2.0 * b + c);
        return (best + shift) * sr / size;
    }

    float maxStep (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (size_t i = 1; i < x.size(); ++i) m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    float peak (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (auto v : x) m = std::max (m, std::abs (v));
        return m;
    }

    void waitForFactory (GrainProcessor& p)
    {
        for (int i = 0; i < 4000 && p.getFactorySource (sourceChoices.size() - 1) == nullptr; ++i)
            juce::Thread::sleep (5);
    }

    void process (GrainProcessor& p, juce::MidiBuffer& midi, int samples = 512)
    {
        juce::AudioBuffer<float> buffer (2, samples);
        buffer.clear();
        p.processBlock (buffer, midi);
        p.flushHardwareChanges();   // what the timer does ~30 times a second
    }
}

//==============================================================================
class GrainDspTest : public juce::UnitTest
{
public:
    GrainDspTest() : juce::UnitTest ("Grain DSP", "thf") {}

    void runTest() override
    {
        beginTest ("Window table: Hann halves sum to one, energy formula matches");
        {
            thf::grain::dsp::WindowTable w;
            float worst = 0.0f;
            for (int i = 0; i < 1000; ++i)
            {
                const auto ph = (float) i / 2000.0f;
                worst = std::max (worst, std::abs (w.value (ph, 0.5f) + w.value (ph + 0.5f, 0.5f) - 1.0f));
            }
            expectLessThan (worst, 1.0e-4f);

            for (float fade : { 0.5f, 0.25f, 0.1f, 0.04f })
            {
                double sum = 0.0;
                constexpr int n = 20000;
                for (int i = 0; i < n; ++i) { const auto v = w.value ((float) i / n, fade); sum += v * v; }
                expectWithinAbsoluteError ((float) (sum / n), thf::grain::dsp::windowEnergy (fade), 2.0e-3f);
            }
        }

        beginTest ("Interpolation accuracy on a 1 kHz sine");
        {
            std::vector<float> s (4096);
            for (size_t i = 0; i < s.size(); ++i) s[i] = std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / 48000.0f);
            thf::grain::dsp::SincTable sinc;
            float errH = 0.0f, errS = 0.0f;
            for (int i = 100; i < 4000; ++i)
                for (float f : { 0.25f, 0.5f, 0.75f })
                {
                    const auto truth = std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * ((float) i + f) / 48000.0f);
                    errH = std::max (errH, std::abs (thf::grain::dsp::hermite (s.data() + i, f) - truth));
                    errS = std::max (errS, std::abs (sinc.read (s.data() + i, f, 1.0f) - truth));
                }
            logMessage ("  Hermite max error " + juce::String (juce::Decibels::gainToDecibels (errH), 1) + " dB, sinc "
                        + juce::String (juce::Decibels::gainToDecibels (errS), 1) + " dB");
            expectLessThan (errH, 2.0e-4f);   // < -74 dB
            expectLessThan (errS, 2.0e-3f);   // Kaiser window ripple, < -54 dB at 1 kHz
        }

        beginTest ("Pitch: note - root sets the grain pitch (same and different sample rates)");
        {
            for (double hostRate : { 48000.0, 44100.0 })
            {
                auto src = sineSource (440.0, 5.0, 48000.0);
                GrainEngine e;
                e.prepare (hostRate);
                e.setSource (src.get());
                auto p = plain();
                p.sizeMs = 100.0f;
                p.density = 40.0f;
                p.position = 0.2f;
                e.noteOn (72, 1.0f, p);
                const auto out = render (e, p, (int) hostRate * 2);
                const auto f = dominantFrequency (out, hostRate);
                logMessage ("  host " + juce::String (hostRate) + " Hz: " + juce::String (f, 2) + " Hz");
                expectWithinAbsoluteError (f, 880.0, 880.0 * 0.004);
            }
        }

        beginTest ("Loudness stays constant across density and window shape (noise, +/- 1 dB)");
        {
            auto src = noiseSource (10.0);
            std::vector<double> levels;
            const std::pair<float, float> settings[] = { { 40.0f, 0.0f }, { 80.0f, 0.0f }, { 160.0f, 0.0f }, { 80.0f, 1.0f }, { 160.0f, 0.5f } };
            for (auto [density, window] : settings)
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (src.get());
                auto p = plain();
                p.spray = 1.0f;
                p.chaos = 1.0f;
                p.sizeMs = 100.0f;
                p.density = density;
                p.window = window;
                e.noteOn (60, 1.0f, p);
                const auto out = render (e, p, (int) rate * 4);
                levels.push_back (rms (out, (size_t) rate));
            }
            const auto ref = levels.front();
            for (size_t i = 0; i < levels.size(); ++i)
            {
                const auto db = juce::Decibels::gainToDecibels (levels[i] / ref);
                logMessage ("  setting " + juce::String ((int) i) + ": " + juce::String (db, 2) + " dB");
                expectWithinAbsoluteError (db, 0.0, 1.0);
            }
        }

        beginTest ("Aliasing: content pushed above Nyquist is removed (normal and HQ)");
        {
            for (bool hq : { false, true })
            {
                auto run = [hq] (double freq, int note)
                {
                    auto src = sineSource (freq, 3.0);
                    GrainEngine e;
                    e.prepare (rate);
                    e.setSource (src.get());
                    auto p = plain();
                    p.hq = hq;
                    p.sizeMs = 150.0f;
                    p.density = 20.0f;
                    e.noteOn (note, 1.0f, p);
                    return rms (render (e, p, (int) rate), 4800);
                };
                const auto reference = run (10000.0, 60);              // 10 kHz, unshifted
                const auto octaveUp = run (15000.0, 72);              // would be 30 kHz
                const auto fifthUp = run (18000.0, 67);               // would be 27 kHz
                const auto octaveDb = juce::Decibels::gainToDecibels (octaveUp / reference, -200.0);
                const auto fifthDb = juce::Decibels::gainToDecibels (fifthUp / reference, -200.0);
                logMessage (juce::String (hq ? "  HQ" : "  normal") + ": 15 kHz +12 st -> " + juce::String (octaveDb, 1)
                            + " dB, 18 kHz +7 st -> " + juce::String (fifthDb, 1) + " dB");
                expectLessThan (octaveDb, -50.0);
                expectLessThan (fifthDb, hq ? -40.0 : -30.0);
            }
        }

        beginTest ("Grain boundaries do not click (Hann and flat windows)");
        {
            auto src = sineSource (220.0, 4.0);
            for (float window : { 0.0f, 1.0f })
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (src.get());
                auto p = plain();
                p.sizeMs = 60.0f;
                p.density = 30.0f;
                p.spray = 0.5f;
                p.chaos = 0.5f;
                p.window = window;
                e.noteOn (60, 1.0f, p);
                const auto out = render (e, p, (int) rate * 2);
                const std::vector<float> steady (out.begin() + 4800, out.end());
                const auto ratio = maxStep (steady) / std::max (1.0e-6f, peak (steady));
                logMessage ("  window " + juce::String (window) + ": max step / peak = " + juce::String (ratio, 3));
                expectLessThan (ratio, 0.12f);
            }
        }

        beginTest ("Stress: random parameters, no NaN, bounded output");
        {
            auto src = noiseSource (2.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            juce::Random r (7);
            bool finite = true;
            float worst = 0.0f;
            std::vector<float> l (256), rr (256);
            for (int block = 0; block < 600; ++block)
            {
                EngineParams p;
                p.position = r.nextFloat(); p.scan = r.nextFloat() * 4.0f - 2.0f; p.spray = r.nextFloat();
                p.sizeMs = 5.0f + r.nextFloat() * 1995.0f; p.density = 0.5f + r.nextFloat() * 199.5f;
                p.chaos = r.nextFloat(); p.window = r.nextFloat(); p.pitch = r.nextFloat() * 48.0f - 24.0f;
                p.jitter = r.nextFloat() * 24.0f; p.reverse = r.nextFloat(); p.stereo = r.nextFloat();
                p.voices = 1 + r.nextInt (16); p.voiceMode = r.nextInt (3); p.glideMs = r.nextFloat() * 500.0f;
                p.cutoff = 20.0f + r.nextFloat() * 19980.0f; p.resonance = r.nextFloat(); p.filterType = r.nextInt (3);
                p.filterEnv = r.nextFloat() * 2.0f - 1.0f; p.drive = r.nextFloat(); p.lfoDepth = r.nextFloat();
                p.lfoRate = r.nextFloat() * 20.0f; p.lfoTarget = r.nextInt (6); p.lfoShape = r.nextInt (5);
                p.hq = r.nextBool(); p.safeClip = true; p.attackMs = 1.0f + r.nextFloat() * 100.0f;
                if (r.nextInt (4) == 0) e.noteOn (24 + r.nextInt (80), r.nextFloat(), p);
                if (r.nextInt (5) == 0) e.noteOff (24 + r.nextInt (80), p);
                e.setPitchBend (r.nextFloat() * 2.0f - 1.0f);
                e.setModWheel (r.nextFloat());
                e.render (l.data(), rr.data(), 256, p);
                for (int i = 0; i < 256; ++i)
                {
                    finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (rr[(size_t) i]);
                    worst = std::max ({ worst, std::abs (l[(size_t) i]), std::abs (rr[(size_t) i]) });
                }
            }
            expect (finite, "non-finite output");
            expectLessOrEqual (worst, 1.0f);
        }

        beginTest ("Pitch quantize snaps random offsets to the chosen intervals");
        {
            expectEquals (GrainEngine::quantizeInterval (5.0f, 0), 5.0f);
            expectEquals (GrainEngine::quantizeInterval (5.0f, 1), 0.0f);
            expectEquals (GrainEngine::quantizeInterval (7.0f, 1), 12.0f);
            expectEquals (GrainEngine::quantizeInterval (-8.0f, 1), -12.0f);
            expectEquals (GrainEngine::quantizeInterval (8.0f, 2), 7.0f);
            expectEquals (GrainEngine::quantizeInterval (-4.0f, 2), -5.0f);
            expectEquals (GrainEngine::quantizeInterval (3.4f, 3), 4.0f);
            expectEquals (GrainEngine::quantizeInterval (3.4f, 4), 3.0f);
            expectEquals (GrainEngine::quantizeInterval (-1.4f, 3), -1.0f);
        }

        beginTest ("LFO on Level ducks the output (pump)");
        {
            auto src = noiseSource (4.0);
            auto run = [&src] (float depth)
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (src.get());
                auto p = plain();
                p.spray = 1.0f; p.chaos = 1.0f; p.density = 80.0f;
                p.lfoTarget = 6; p.lfoShape = 3; p.lfoRate = 4.0f; p.lfoDepth = depth;
                e.noteOn (60, 1.0f, p);
                return render (e, p, (int) rate * 2);
            };
            const auto dry = run (0.0f), pumped = run (1.0f);
            // Square LFO at full depth: half of every cycle is silent.
            expectLessThan (rms (pumped, 4800) / rms (dry, 4800), 0.8);
            float quietest = 1.0f;
            for (size_t i = 4800; i + 480 < pumped.size(); i += 480)
                quietest = std::min (quietest, (float) rms (std::vector<float> (pumped.begin() + (long) i, pumped.begin() + (long) i + 480)));
            expectLessThan (quietest, 1.0e-3f);
        }

        beginTest ("Region: grains only come from between Sample Start and Sample End");
        {
            auto src = noiseSource (4.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.regionStart = 0.5f; p.regionEnd = 0.6f;
            p.spray = 1.0f; p.chaos = 1.0f; p.sizeMs = 20.0f; p.density = 100.0f; p.scan = 1.0f;
            e.noteOn (60, 1.0f, p);
            std::array<GrainEvent, 512> events {};
            int seen = 0;
            bool inside = true;
            for (int i = 0; i < 40; ++i)
            {
                render (e, p, 2400);
                const auto n = e.popGrainEvents (events.data(), (int) events.size());
                for (int k = 0; k < n; ++k)
                {
                    inside = inside && events[(size_t) k].position >= 0.5f - 1.0e-4f
                                    && events[(size_t) k].position + events[(size_t) k].span <= 0.6f + 1.0e-4f;
                    ++seen;
                }
            }
            expect (seen > 100);
            expect (inside, "a grain left the region");
        }

        beginTest ("Rate conversion on load: a 44.1 kHz sine keeps its pitch and stays clean");
        {
            juce::AudioBuffer<float> b (1, 44100);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 44100.0));
            auto s = SourceData::fromBuffer (b, 44100.0, "sine", 48000.0);
            expectEquals (s->getSampleRate(), 48000.0);
            std::vector<float> x ((size_t) s->getLength());
            std::copy (s->channel (0, 0), s->channel (0, 0) + s->getLength(), x.begin());
            // Compare against an ideal 1 kHz sine at 48 kHz in the middle (edges excluded).
            double err = 0.0, sig = 0.0;
            for (size_t i = 4800; i + 4800 < x.size(); ++i)
            {
                const auto ideal = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) i / 48000.0);
                err += (x[i] - ideal) * (x[i] - ideal);
                sig += ideal * ideal;
            }
            const auto snr = 10.0 * std::log10 (sig / juce::jmax (1.0e-20, err));
            logMessage ("  44.1 -> 48 kHz: signal to error " + juce::String (snr, 1) + " dB");
            expectGreaterThan (snr, 80.0);
        }

        beginTest ("DC is removed on load");
        {
            juce::AudioBuffer<float> b (1, 48000);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.3f + 0.2f * (float) std::sin ((float) i * 0.05f));
            auto s = SourceData::fromBuffer (b, 48000.0, "dc");
            double mean = 0.0;
            for (int i = 4800; i < 43200; ++i) mean += s->channel (0, 0)[i];
            mean /= 38400.0;
            expectLessThan (std::abs (mean), 1.0e-3);
        }

        beginTest ("Pitch detection finds the note of a harmonic sample");
        {
            for (double hz : { 110.0, 220.0, 329.63, 523.25 })
            {
                juce::AudioBuffer<float> b (1, 96000);
                double phase = 0.0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    b.setSample (0, i, (float) (0.4 * (2.0 * phase - 1.0) + 0.2 * std::sin (4.0 * juce::MathConstants<double>::pi * phase)));
                    phase += hz / rate;
                    if (phase >= 1.0) phase -= 1.0;
                }
                auto s = SourceData::fromBuffer (b, rate, "saw");
                sources::detectPitch (*s);
                const auto expected = 69.0 + 12.0 * std::log2 (hz / 440.0);
                logMessage ("  " + juce::String (hz) + " Hz -> " + juce::String (s->detectedNote, 2)
                            + " (confidence " + juce::String (s->pitchConfidence, 2) + ")");
                expectWithinAbsoluteError ((double) s->detectedNote, expected, 0.1);
            }
            auto noise = noiseSource (2.0);
            sources::detectPitch (*const_cast<SourceData*> (noise.get()));
            expect (noise->detectedNote < 0.0f || noise->pitchConfidence < 0.5f, "noise reported as pitched");
        }

        beginTest ("Tiny sources in HQ, pitched far up: every octave copy exists, reads stay inside");
        {
            for (int length : { 10, 100, 200, 600, 1000 })
            {
                juce::AudioBuffer<float> b (2, length);
                for (int i = 0; i < length; ++i)
                {
                    b.setSample (0, i, std::sin ((float) i * 0.3f));
                    b.setSample (1, i, std::cos ((float) i * 0.3f));
                }
                auto src = SourceData::fromBuffer (b, 96000.0, "tiny");
                expectEquals (src->getNumLevels(), SourceData::maxLevels);
                GrainEngine e;
                e.prepare (44100.0);
                e.setSource (src.get());
                auto p = plain();
                p.hq = true; p.pitch = 24.0f; p.jitter = 24.0f; p.spray = 1.0f; p.chaos = 1.0f;
                p.bendRange = 24.0f; p.density = 80.0f;
                e.setPitchBend (1.0f);
                e.noteOn (108, 1.0f, p);
                std::vector<float> r;
                const auto out = render (e, p, 22050, &r);
                bool finite = true;
                for (size_t i = 0; i < out.size(); ++i) finite = finite && std::isfinite (out[i]) && std::isfinite (r[i]);
                expect (finite, "length " + juce::String (length));
            }
        }

        beginTest ("Switching the source while notes sound does not click");
        {
            auto a1 = sineSource (220.0, 3.0), b1 = sineSource (330.0, 3.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (a1.get());
            auto p = plain();
            p.sizeMs = 80.0f; p.density = 40.0f; p.spray = 0.3f; p.chaos = 0.5f;
            for (int n : { 48, 52, 55, 60, 64, 67, 71, 72 })
                e.noteOn (n, 0.8f, p);
            const auto before = render (e, p, 24000);
            e.setSource (b1.get());
            const auto after = render (e, p, 4800);
            std::vector<float> joined (before.end() - 4800, before.end());
            joined.insert (joined.end(), after.begin(), after.end());
            const auto normalStep = maxStep (std::vector<float> (before.begin() + 4800, before.end() - 4800));
            const auto switchStep = maxStep (joined);
            logMessage ("  max step: steady " + juce::String (normalStep, 4) + ", around the switch " + juce::String (switchStep, 4));
            expectLessThan (switchStep, normalStep * 1.5f);
        }

        beginTest ("After a NaN the engine recovers, reverb included");
        {
            auto src = sineSource (220.0, 2.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.space = 0.6f;
            e.noteOn (60, 1.0f, p);
            render (e, p, 4800);
            auto broken = p;
            broken.cutoff = std::numeric_limits<float>::quiet_NaN();
            render (e, broken, 512);
            const auto out = render (e, p, 9600);
            bool finite = true;
            for (auto v : out) finite = finite && std::isfinite (v);
            expect (finite);
            expectGreaterThan (rms (out, 4800), 1.0e-3);
        }

        beginTest ("Sixteen dense voices all get their grains");
        {
            auto src = noiseSource (4.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.voices = 16; p.density = 200.0f; p.sizeMs = 400.0f; p.spray = 1.0f; p.chaos = 1.0f;
            for (int n = 0; n < 16; ++n)
                e.noteOn (40 + n * 2, 0.8f, p);
            render (e, p, 48000);
            expectGreaterThan (e.getActiveGrains(), 16 * 40);
        }

        beginTest ("A grain longer than the region is shortened to the material");
        {
            auto src = noiseSource (1.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.regionStart = 0.4f; p.regionEnd = 0.45f;   // 50 ms of material
            p.sizeMs = 1000.0f; p.density = 20.0f; p.pitch = 12.0f;
            e.noteOn (60, 1.0f, p);
            std::array<GrainEvent, 512> events {};
            render (e, p, 24000);
            const auto n = e.popGrainEvents (events.data(), (int) events.size());
            expect (n > 0);
            for (int k = 0; k < n; ++k)
            {
                expect (events[(size_t) k].position >= 0.4f - 1.0e-4f);
                expect (events[(size_t) k].position + events[(size_t) k].span <= 0.45f + 1.0e-4f);
            }
        }

        beginTest ("No source: silence, no crash");
        {
            GrainEngine e;
            e.prepare (rate);
            auto p = plain();
            e.noteOn (60, 1.0f, p);
            expectEquals (peak (render (e, p, 4800)), 0.0f);
        }

        beginTest ("Voices: stealing respects the limit, mono uses one voice, hold latches");
        {
            auto src = sineSource (220.0, 2.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.voices = 2;
            e.noteOn (60, 1.0f, p);
            e.noteOn (64, 1.0f, p);
            e.noteOn (67, 1.0f, p);
            render (e, p, 4800);
            expectLessOrEqual (e.getActiveVoices(), 2);
            render (e, p, 4800);
            expectEquals (e.getActiveVoices(), 2);

            e.allNotesOff (true);
            p.voiceMode = 1;
            e.noteOn (60, 1.0f, p);
            e.noteOn (64, 1.0f, p);
            e.noteOn (67, 1.0f, p);
            render (e, p, 4800);
            expectEquals (e.getActiveVoices(), 1);

            e.allNotesOff (true);
            render (e, p, 480);
            p.voiceMode = 0;
            e.setHold (true);
            e.noteOn (60, 1.0f, p);
            e.noteOff (60, p);
            render (e, p, 24000);
            expectEquals (e.getActiveVoices(), 1, "held note released");
            e.setHold (false);
            render (e, p, 24000);
            expectEquals (e.getActiveVoices(), 0, "note not released after hold off");
        }

        beginTest ("Renders are reproducible");
        {
            auto src = noiseSource (2.0);
            auto run = [&src]
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (src.get());
                EngineParams p;
                p.spray = 0.7f; p.chaos = 1.0f; p.jitter = 3.0f; p.reverse = 0.5f;
                e.noteOn (60, 0.8f, p);
                e.noteOn (67, 0.8f, p);
                return render (e, p, 24000);
            };
            expect (run() == run());
        }
    }
};

//==============================================================================
class GrainProcessorTest : public juce::UnitTest
{
public:
    GrainProcessorTest() : juce::UnitTest ("Grain processor", "thf") {}

    void runTest() override
    {
        beginTest ("processBlock does not allocate (notes, encoders, faders, pads, bend)");
        {
            GrainProcessor p;
            waitForFactory (p);
            p.prepareToPlay (rate, 512);
            juce::MidiBuffer midi;
            midi.ensureSize (4096);
            juce::AudioBuffer<float> buffer (2, 512);

            auto fill = [&midi]
            {
                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 76, 65), 10);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 82, 20), 20);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 28, 66), 30);
                midi.addEvent (juce::MidiMessage::noteOn (10, 36, 0.9f), 40);
                midi.addEvent (juce::MidiMessage::noteOff (10, 36), 50);
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 9000), 60);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 90), 70);
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 400);
            };
            p.param (pid::space)->setValueNotifyingHost (0.5f);        // reverb running
            p.param (pid::quantize)->setValueNotifyingHost (1.0f);     // minor
            p.param (pid::lfoTarget)->setValueNotifyingHost (1.0f);    // Level
            p.param (pid::lfoDepth)->setValueNotifyingHost (0.8f);
            p.param (pid::jitter)->setValueNotifyingHost (0.5f);
            for (int i = 0; i < 4; ++i) { fill(); buffer.clear(); p.processBlock (buffer, midi); }

            fill();
            buffer.clear();
            thf::test::allocations.store (0);
            thf::test::countAllocations.store (true);
            p.processBlock (buffer, midi);
            thf::test::countAllocations.store (false);
            expectEquals ((int) thf::test::allocations.load(), 0);
        }

        beginTest ("Encoders follow the page; faders pick up; main encoder and click");
        {
            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            juce::MidiBuffer midi;

            auto* size = p.param (pid::size);
            const auto before = size->getValue();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 76, 65), 0);   // encoder 3, +1
            process (p, midi);
            expectWithinAbsoluteError (size->getValue() - before, 0.005f, 1.0e-4f);

            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 118, 127), 0); // main click
            process (p, midi);
            expectEquals (p.getPage(), 1);

            auto* filterEnv = p.param (pid::filterEnv);
            const auto envBefore = filterEnv->getValue();
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 76, 62), 0);   // encoder 3, -2
            process (p, midi);
            expectWithinAbsoluteError (filterEnv->getValue() - envBefore, -0.01f, 1.0e-4f);
            expectWithinAbsoluteError (size->getValue() - before, 0.005f, 1.0e-4f);

            auto* attack = p.param (pid::attack);
            const auto attackBefore = attack->getValue();
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 82, 127), 0);  // far away: ignored
            process (p, midi);
            expectEquals (attack->getValue(), attackBefore);
            midi.clear();
            for (int v = 0; v <= 127; v += 8)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 82, v), v);
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 82, 127), 200);
            process (p, midi);
            expectWithinAbsoluteError (attack->getValue(), 1.0f, 1.0e-4f);

            auto* position = p.param (pid::position);
            const auto posBefore = position->getValue();
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 28, 67), 0);   // main encoder +3
            process (p, midi);
            expectWithinAbsoluteError (position->getValue() - posBefore, 0.006f, 1.0e-4f);
        }

        beginTest ("Pads: bank A toggles, bank B stores and recalls cues");
        {
            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, 36, 1.0f), 0);          // pad 1: Freeze
            midi.addEvent (juce::MidiMessage::noteOn (10, 39, 1.0f), 1);          // pad 4: Window
            process (p, midi);
            expect (p.param (pid::freeze)->getValue() > 0.5f);
            expectWithinAbsoluteError (p.param (pid::window)->getValue(), 0.5f, 1.0e-6f);

            // Notes on the pad channel outside the pad range still play.
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (10, 60, 1.0f), 0);
            process (p, midi);
            process (p, midi = {});
            expectEquals (p.getEngine().getActiveVoices(), 1);

            p.param (pid::position)->setValueNotifyingHost (0.3f);
            process (p, midi = {});
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (10, 44, 1.0f), 0);          // bank B pad 1, empty
            midi.addEvent (juce::MidiMessage::noteOff (10, 44), 100);
            process (p, midi);
            expectWithinAbsoluteError (p.getCue (0), 0.3f, 1.0e-3f);

            p.param (pid::position)->setValueNotifyingHost (0.8f);
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (10, 44, 1.0f), 0);          // tap: jump
            midi.addEvent (juce::MidiMessage::noteOff (10, 44), 100);
            process (p, midi);
            expectWithinAbsoluteError (p.param (pid::position)->getValue(), 0.3f, 1.0e-3f);
        }

        beginTest ("On-screen / computer keyboard notes reach the engine");
        {
            GrainProcessor p;
            waitForFactory (p);
            p.prepareToPlay (rate, 512);
            juce::MidiBuffer midi;
            p.getKeyboardState().noteOn (16, 60, 0.8f);
            process (p, midi);
            process (p, midi);
            expectEquals (p.getEngine().getActiveVoices(), 1);
            p.getKeyboardState().noteOff (16, 60, 0.0f);
            p.param (pid::release)->setValueNotifyingHost (0.0f);
            for (int i = 0; i < 20; ++i) process (p, midi);
            expectEquals (p.getEngine().getActiveVoices(), 0);
        }

        beginTest ("MIDI learn assigns a CC and takes priority over the template");
        {
            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            p.startLearn (pid::chaos);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 0), 0);
            process (p, midi);
            expect (! p.isLearning());
            expectEquals (p.getCcFor (pid::chaos), 20);
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 30), 0);   // picks up near 0.3? no: jump in steps
            for (int v = 30; v <= 100; v += 5) midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, v), v);
            process (p, midi);
            expectWithinAbsoluteError (p.param (pid::chaos)->getValue(), 100.0f / 127.0f, 1.0e-3f);
        }

        beginTest ("State round trip: parameters, cues, MIDI setup, embedded sample");
        {
            auto file = juce::File::createTempFile (".wav");
            {
                juce::AudioBuffer<float> b (2, 48000);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    b.setSample (0, i, 0.4f * std::sin ((float) i * 0.05f));
                    b.setSample (1, i, 0.3f * std::sin ((float) i * 0.031f));
                }
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                               .withNumChannels (2).withBitsPerSample (24));
                expect (writer != nullptr);
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }

            juce::MemoryBlock state;
            juce::String hash;
            {
                GrainProcessor a;
                juce::String error;
                expect (a.loadSampleSync (file, error), error);
                hash = a.getUserSample()->contentHash;
                a.param (pid::size)->setValueNotifyingHost (0.7f);
                a.param (pid::cutoff)->setValueNotifyingHost (0.4f);
                a.setCue (2, 0.42f);
                a.setEncoderMode (thf::midi::EncoderMode::twosComplement);
                a.startLearn (pid::chaos);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 0), 0);
                a.prepareToPlay (rate, 512);
                process (a, midi);
                a.getStateInformation (state);
            }
            file.deleteFile();   // the session must not depend on the file any more

            GrainProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            expectWithinAbsoluteError (b.param (pid::size)->getValue(), 0.7f, 1.0e-6f);
            expectWithinAbsoluteError (b.param (pid::cutoff)->getValue(), 0.4f, 1.0e-6f);
            expectWithinAbsoluteError (b.getCue (2), 0.42f, 1.0e-5f);
            expectEquals (b.getCue (0), -1.0f);
            expect (b.getEncoderMode() == thf::midi::EncoderMode::twosComplement);
            expectEquals (b.getCcFor (pid::chaos), 21);
            expect (b.getUserSample() != nullptr, "embedded sample not restored");
            if (auto s = b.getUserSample())
            {
                // Converted to the host rate (48 kHz by default), the original rate is kept.
                expectEquals (s->getSampleRate(), 48000.0);
                expectEquals (s->originalRate, 44100.0);
                expectEquals (s->getLength(), (int) std::llround (48000.0 * 48000.0 / 44100.0));
                expectEquals (s->getNumChannels(), 2);
                expectEquals (s->contentHash, hash);
            }
            expect ((int) b.param (pid::source)->convertFrom0to1 (b.param (pid::source)->getValue()) == 0);
        }

        beginTest ("Detected pitch becomes Root + Fine");
        {
            GrainProcessor p;
            juce::AudioBuffer<float> b (1, 96000);
            for (int i = 0; i < b.getNumSamples(); ++i)                 // A3 a little sharp: 222 Hz
                b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 222.0 * i / rate)
                                 + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 444.0 * i / rate));
            auto s = SourceData::fromBuffer (b, rate, "a3");
            sources::detectPitch (*s);
            p.setUserSample (s);
            expect (p.applyDetectedRoot());
            expectEquals ((int) p.param (pid::root)->convertFrom0to1 (p.param (pid::root)->getValue()), 57);
            const auto fine = p.param (pid::fine)->convertFrom0to1 (p.param (pid::fine)->getValue());
            expectWithinAbsoluteError (fine, -15.6f, 1.5f);   // 222 Hz is +15.6 ct above A3
        }

        beginTest ("Locked own sample survives preset changes; a new sample resets region and cues");
        {
            GrainProcessor p;
            juce::AudioBuffer<float> b (1, 48000);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.3f * std::sin ((float) i * 0.03f));
            auto s = SourceData::fromBuffer (b, rate, "one");
            s->contentHash = "one";
            p.setUserSample (s);
            p.param (pid::regionStart)->setValueNotifyingHost (0.2f);
            p.param (pid::regionEnd)->setValueNotifyingHost (0.7f);
            p.setCue (3, 0.5f);
            p.getPresets().loadFactory (5);
            expectEquals ((int) p.param (pid::source)->convertFrom0to1 (p.param (pid::source)->getValue()), 0);
            expectWithinAbsoluteError (p.param (pid::regionStart)->getValue(), 0.2f, 1.0e-6f);
            expectWithinAbsoluteError (p.getCue (3), 0.5f, 1.0e-6f);

            p.setKeepSample (false);
            p.getPresets().loadFactory (5);
            expect ((int) p.param (pid::source)->convertFrom0to1 (p.param (pid::source)->getValue()) != 0);

            auto other = SourceData::fromBuffer (b, rate, "two");
            other->contentHash = "two";
            p.setCue (3, 0.5f);
            p.param (pid::regionEnd)->setValueNotifyingHost (0.4f);
            p.setUserSample (other);
            expectEquals (p.getCue (3), -1.0f);
            expectEquals (p.param (pid::regionEnd)->getValue(), 1.0f);
        }

        beginTest ("Undo: user gestures and A/B are steps; the history survives A/B");
        {
            GrainProcessor p;
            auto* size = p.param (pid::size);
            const auto original = size->getValue();
            size->beginChangeGesture();
            size->setValueNotifyingHost (0.9f);
            size->endChangeGesture();
            expect (p.getUndoManager().canUndo());
            p.toggleAB();                                   // B = copy of A
            size->beginChangeGesture();
            size->setValueNotifyingHost (0.2f);
            size->endChangeGesture();
            p.toggleAB();                                   // back to A
            expectWithinAbsoluteError (size->getValue(), 0.9f, 1.0e-6f);
            expect (p.getUndoManager().canUndo(), "A/B cleared the history");
            p.getUndoManager().undo();                      // undo the A/B switch
            expectWithinAbsoluteError (size->getValue(), 0.2f, 1.0e-6f);
            p.getUndoManager().undo();
            p.getUndoManager().undo();
            p.getUndoManager().undo();
            expectWithinAbsoluteError (size->getValue(), original, 1.0e-6f);
        }

        beginTest ("Undo of a sample load brings back the sample, its region and cues");
        {
            GrainProcessor p;
            juce::AudioBuffer<float> b (1, 48000);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.3f * std::sin ((float) i * 0.03f));
            auto first = SourceData::fromBuffer (b, rate, "first");
            first->contentHash = "first";
            auto second = SourceData::fromBuffer (b, rate, "second");
            second->contentHash = "second";
            p.setUserSample (first);
            p.param (pid::regionStart)->setValueNotifyingHost (0.25f);
            p.setCue (2, 0.6f);
            p.setUserSample (second);
            expectEquals (p.getCue (2), -1.0f);
            p.getUndoManager().undo();
            expect (p.getUserSample() == first);
            expectWithinAbsoluteError (p.param (pid::regionStart)->getValue(), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (p.getCue (2), 0.6f, 1.0e-6f);
            // Coming back to a sample later restores its cues and region too.
            p.setUserSample (second);
            p.setUserSample (first);
            expectWithinAbsoluteError (p.getCue (2), 0.6f, 1.0e-6f);
        }

        beginTest ("Old sessions: parameters they do not know start at their defaults");
        {
            juce::MemoryBlock state;
            {
                GrainProcessor a;
                a.getStateInformation (state);
            }
            juce::MemoryInputStream in (state, false);
            in.readString();
            auto root = juce::ValueTree::readFromStream (in);
            auto paramsTree = root.getChildWithName ("PARAMS");
            paramsTree.removeChild (paramsTree.getChildWithProperty ("id", pid::regionStart), nullptr);
            juce::MemoryBlock old;
            {
                juce::MemoryOutputStream out (old, false);
                out.writeString ("THFG");
                root.writeToStream (out);
            }
            GrainProcessor b;
            b.param (pid::regionStart)->setValueNotifyingHost (0.3f);
            b.setStateInformation (old.getData(), (int) old.getSize());
            expectEquals (b.param (pid::regionStart)->getValue(), 0.0f);
        }

        beginTest ("Hardware moves reach the host inside begin/end gestures");
        {
            struct Gestures : juce::AudioProcessorListener
            {
                int begins = 0, ends = 0;
                void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
                void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
                void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
                void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
            } gestures;
            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            p.addListener (&gestures);
            juce::MidiBuffer midi;
            for (int k = 0; k < 5; ++k) midi.addEvent (juce::MidiMessage::controllerEvent (1, 76, 65), k);
            process (p, midi);
            expectEquals (gestures.begins, 1);
            expectEquals (gestures.ends, 0);
            juce::Thread::sleep (350);
            p.flushHardwareChanges();
            expectEquals (gestures.ends, 1);
            expect (p.getUndoManager().canUndo(), "a hardware move should be one undo step");
            p.removeListener (&gestures);
        }

        beginTest ("No allocations in processBlock with the editor open");
        {
            GrainProcessor p;
            waitForFactory (p);
            p.prepareToPlay (rate, 512);
            GrainEditor editor (p);
            juce::MidiBuffer midi;
            midi.ensureSize (4096);
            juce::AudioBuffer<float> buffer (2, 512);
            auto fill = [&midi]
            {
                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 65), 10);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 82, 20), 20);
                midi.addEvent (juce::MidiMessage::noteOn (10, 37, 0.9f), 30);
            };
            for (int i = 0; i < 4; ++i) { fill(); buffer.clear(); p.processBlock (buffer, midi); }
            fill();
            buffer.clear();
            thf::test::allocations.store (0);
            thf::test::countAllocations.store (true);
            p.processBlock (buffer, midi);
            thf::test::countAllocations.store (false);
            expectEquals ((int) thf::test::allocations.load(), 0);
            p.flushHardwareChanges();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        }

        beginTest ("A late asynchronous load does not overwrite a restored session");
        {
            auto makeWav = [] (const char* fileName, float freq)
            {
                auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (fileName);
                juce::AudioBuffer<float> b (1, 24000);
                for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.4f * std::sin ((float) i * freq));
                juce::WavAudioFormat wav;
                file.deleteFile();
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24)))
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                return file;
            };
            const auto fileA = makeWav ("thf-grain-test-a.wav", 0.02f);
            const auto fileB = makeWav ("thf-grain-test-b.wav", 0.05f);
            juce::MemoryBlock stateB;
            juce::String hashB;
            {
                GrainProcessor b;
                juce::String error;
                expect (b.loadSampleSync (fileB, error));
                hashB = b.getUserSample()->contentHash;
                b.getStateInformation (stateB);
            }
            GrainProcessor p;
            p.loadSampleAsync (fileA);
            p.setStateInformation (stateB.getData(), (int) stateB.getSize());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
            expect (p.getUserSample() != nullptr);
            if (p.getUserSample() != nullptr)
                expectEquals (p.getUserSample()->contentHash, hashB);
            fileA.deleteFile();
            fileB.deleteFile();
        }

        beginTest ("Long samples are copied to the library and relinked only when the audio matches");
        {
            auto writeWav = [] (const juce::File& file, int length, double rate, float freq)
            {
                juce::AudioBuffer<float> b (1, length);
                for (int i = 0; i < length; ++i) b.setSample (0, i, 0.4f * std::sin ((float) i * freq));
                juce::WavAudioFormat wav;
                file.deleteFile();
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (16)))
                    w->writeFromAudioSampleBuffer (b, 0, length);
            };
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
            const auto original = temp.getChildFile ("thf-grain-test-long.wav");
            constexpr double rate = 8000.0;
            const auto length = (int) (rate * (sources::maxEmbedSeconds + 1.0));
            writeWav (original, length, rate, 0.07f);

            juce::MemoryBlock state;
            juce::File copy;
            juce::String hash;
            {
                GrainProcessor p;
                juce::String error;
                expect (p.loadSampleSync (original, error), error);
                auto s = p.getUserSample();
                expect (s != nullptr && s->embeddedFlac.getSize() == 0);   // too long to embed
                if (s != nullptr)
                {
                    copy = s->file;
                    hash = s->contentHash;
                }
                expect (copy.isAChildOf (library::userFolder()), copy.getFullPathName());
                expect (copy.existsAsFile());
                p.getStateInformation (state);
            }
            original.deleteFile();

            // The project still opens after the original is gone.
            {
                GrainProcessor p;
                p.setStateInformation (state.getData(), (int) state.getSize());
                expect (p.getUserSample() != nullptr && p.getUserSample()->contentHash == hash);
            }

            // The library copy disappears; a different file with the same name must not be used.
            const auto tempCopy = temp.getChildFile ("thf-grain-test-long-copy.wav");
            expect (copy.moveFileTo (tempCopy));
            const auto impostor = library::userFolder().getChildFile (copy.getFileName());
            writeWav (impostor, 4000, rate, 0.2f);
            {
                GrainProcessor p;
                p.setStateInformation (state.getData(), (int) state.getSize());
                expect (p.getUserSample() == nullptr);
                expect (p.getLastLoadError().isNotEmpty());
                expect (p.getMissingSamplePath().isNotEmpty());
            }

            // The same audio under that name is relinked.
            impostor.deleteFile();
            expect (tempCopy.moveFileTo (impostor));
            {
                GrainProcessor p;
                p.setStateInformation (state.getData(), (int) state.getSize());
                expect (p.getUserSample() != nullptr && p.getUserSample()->contentHash == hash);
                expect (p.getLastLoadError().isEmpty());
                expect (p.getMissingSamplePath().isEmpty());
            }
            impostor.deleteFile();
            copy.deleteFile();
            tempCopy.deleteFile();
        }

        beginTest ("Files over the length and memory limits are refused, not loaded");
        {
            // 10 min + 1 s of silence at a low rate keeps the file small.
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
            const auto tooLong = temp.getChildFile ("thf-grain-test-too-long.wav");
            {
                constexpr double rate = 1000.0;
                juce::AudioBuffer<float> b (1, (int) (rate * (sources::maxFileSeconds + 1.0)));
                b.clear();
                juce::WavAudioFormat wav;
                tooLong.deleteFile();
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (tooLong);
                if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (16)))
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            juce::String error;
            expect (sources::loadFile (tooLong, error) == nullptr);
            expect (error.containsIgnoreCase ("10 minutes"), error);

            // The memory limit, scaled down so the test does not need a 256 MB file.
            sources::LoadOptions small;
            small.maxSamples = 1000;
            error.clear();
            const auto shortFile = temp.getChildFile ("thf-grain-test-over-budget.wav");
            {
                juce::AudioBuffer<float> b (2, 1024);
                b.clear();
                juce::WavAudioFormat wav;
                shortFile.deleteFile();
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (shortFile);
                if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16)))
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            expect (sources::loadFile (shortFile, error, small) == nullptr);
            expect (error.containsIgnoreCase ("too large"), error);
            error.clear();
            expect (sources::loadFile (shortFile, error) != nullptr, error);

            GrainProcessor p;
            expect (! p.loadSampleSync (tooLong, error));
            expect (p.getUserSample() == nullptr);
            tooLong.deleteFile();
            shortFile.deleteFile();
        }

        beginTest ("Factory presets load and only use known parameters");
        {
            GrainProcessor p;
            for (size_t i = 0; i < factoryPresets().size(); ++i)
            {
                for (auto& [id, v] : factoryPresets()[i].values)
                {
                    auto* param = p.param (id);
                    expect (param != nullptr, juce::String ("unknown parameter ") + id);
                    if (param != nullptr)
                    {
                        const auto range = param->getNormalisableRange();
                        expect (v >= range.start && v <= range.end,
                                juce::String (factoryPresets()[i].name) + ": " + id + " out of range");
                    }
                }
                p.getPresets().loadFactory ((int) i);
                expectEquals (p.getPresets().getCurrentName(), juce::String (factoryPresets()[i].name));
            }
        }

        beginTest ("Controller helpers: relative decoding and pickup");
        {
            using namespace thf::midi;
            expectEquals (decodeRelative (65, EncoderMode::binaryOffset), 1);
            expectEquals (decodeRelative (61, EncoderMode::binaryOffset), -3);
            expectEquals (decodeRelative (127, EncoderMode::twosComplement), -1);
            expectEquals (decodeRelative (2, EncoderMode::twosComplement), 2);
            expectEquals (decodeRelative (65, EncoderMode::signMagnitude), -1);
            expectEquals (decodeRelative (3, EncoderMode::signMagnitude), 3);

            Pickup pk;
            expect (! pk.process (1.0f, 0.2f));
            expect (! pk.process (0.5f, 0.2f));
            expect (pk.process (0.1f, 0.2f));        // crossed
            expect (pk.process (0.15f, 0.1f));       // follows
            expect (! pk.process (0.9f, 0.5f));      // moved elsewhere: must pick up again
        }

        beginTest ("Translation table parsing");
        {
            const auto t = thf::Translator::parse (juce::String::fromUTF8 ("# comment\n\"Freeze\" = \"Заморозка\"\n\"A \\\"q\\\"\" = \"B\"\n"));
            expectEquals ((int) t.size(), 2);
            expectEquals (t.at ("Freeze"), juce::String::fromUTF8 ("Заморозка"));
            expectEquals (t.at ("A \"q\""), juce::String ("B"));
        }
    }
};

static GrainDspTest grainDspTest;
static GrainProcessorTest grainProcessorTest;
