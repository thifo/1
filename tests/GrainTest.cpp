// DSP and processor regression tests for thf Grain.

#include <PluginProcessor.h>
#include <PluginEditor.h>
#include <SampleLibrary.h>
#include <ui/WaveformMath.h>
#include <i18n/Translator.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <complex>
#include <set>

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

    // Energy away from the harmonics of f0 (within +/- tolerance Hz) relative to the energy on
    // them, over 2^15 samples from `from`, Blackman-Harris windowed.
    double offHarmonicDb (const std::vector<float>& x, size_t from, double f0, double tolerance = 40.0)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data (2 * size, 0.0f);
        for (int i = 0; i < size && from + (size_t) i < x.size(); ++i)
        {
            const auto t = 2.0 * juce::MathConstants<double>::pi * i / size;
            const auto w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
            data[(size_t) i] = x[from + (size_t) i] * (float) w;
        }
        fft.performFrequencyOnlyForwardTransform (data.data());
        double right = 0.0, wrong = 0.0;
        for (int bin = 1; bin < size / 2; ++bin)
        {
            const auto hz = bin * rate / size;
            const auto harmonic = std::round (hz / f0);
            const auto e2 = (double) data[(size_t) bin] * data[(size_t) bin];
            if (harmonic >= 1.0 && std::abs (hz - harmonic * f0) < tolerance) right += e2;
            else wrong += e2;
        }
        return 10.0 * std::log10 ((wrong + 1.0e-30) / (right + 1.0e-30));
    }

    // Renders a factory preset through the processor (120 BPM, transport running): a held
    // chord (poly) or one held note (mono) from `notes`, `seconds` long, stereo.
    juce::AudioBuffer<float> renderPreset (const juce::String& name, std::vector<int> notes, double seconds, double holdSeconds = 4.0,
                                           bool line = false)
    {
        struct Transport : juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setBpm (120.0);
                info.setPpqPosition (ppq);
                info.setIsPlaying (true);
                return info;
            }
        } transport;
        GrainProcessor p;
        for (int i = 0; i < 4000 && p.getFactorySource (sourceChoices.size() - 1) == nullptr; ++i)
            juce::Thread::sleep (5);
        p.setPlayHead (&transport);
        p.prepareToPlay (rate, 512);
        const auto& list = factoryPresets();
        for (size_t i = 0; i < list.size(); ++i)
            if (name == list[i].name) p.getPresets().loadFactory ((int) i);
        const int total = (int) (seconds * rate);
        juce::AudioBuffer<float> out (2, total);
        for (int pos = 0; pos < total; pos += 512)
        {
            const auto n = std::min (512, total - pos);
            juce::MidiBuffer midi;
            if (line)
            {
                // One note per second (0.95 s each), as the level tool plays mono presets.
                for (size_t k = 0; k < notes.size(); ++k)
                {
                    const auto on = (int) ((double) k * rate), off = (int) (((double) k + 0.95) * rate);
                    if (on >= pos && on < pos + n) midi.addEvent (juce::MidiMessage::noteOn (1, notes[k], 0.9f), on - pos);
                    if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, notes[k]), off - pos);
                }
            }
            else
            {
                if (pos == 0) for (auto note : notes) midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.85f), 0);
                const auto off = (int) (holdSeconds * rate);
                if (off >= pos && off < pos + n) for (auto note : notes) midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
            }
            juce::AudioBuffer<float> block (2, n);
            block.clear();
            p.processBlock (block, midi);
            transport.ppq += n / rate * 2.0;
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, block, ch, 0, n);
        }
        p.setPlayHead (nullptr);
        return out;
    }

    // Energy per pitch class (0 = C) of the mono mix between from and to seconds, only in
    // [loHz, hiHz] (fundamentals, before the harmonics blur the picture).
    std::array<double, 12> chroma (const juce::AudioBuffer<float>& b, double from, double to, double loHz, double hiHz)
    {
        constexpr int order = 15, size = 1 << order;
        juce::dsp::FFT fft (order);
        std::array<double, 12> result {};
        for (auto start = (int) (from * rate); start + size <= (int) (to * rate) || start == (int) (from * rate); start += size / 2)
        {
            std::vector<float> data (2 * size, 0.0f);
            for (int i = 0; i < size && start + i < b.getNumSamples(); ++i)
                data[(size_t) i] = 0.5f * (b.getSample (0, start + i) + b.getSample (1, start + i))
                                   * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / size));
            fft.performFrequencyOnlyForwardTransform (data.data());
            for (int bin = 1; bin < size / 2; ++bin)
            {
                const auto hz = bin * rate / size;
                if (hz < loHz || hz > hiHz) continue;
                // Only near a note (within 35 cents): detuned unison voices between two notes
                // belong to neither.
                const auto note = 69.0 + 12.0 * std::log2 (hz / 440.0);
                if (std::abs (note - std::round (note)) > 0.35) continue;
                const auto pc = ((int) std::lround (note) % 12 + 12) % 12;
                result[(size_t) pc] += (double) data[(size_t) bin] * data[(size_t) bin];
            }
            if (start + size >= (int) (to * rate)) break;
        }
        return result;
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

        beginTest ("Interpolation accuracy on a 1 kHz sine (2x oversampled copy)");
        {
            constexpr float storageRate = 96000.0f;
            std::vector<float> s (8192);
            for (size_t i = 0; i < s.size(); ++i) s[i] = std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / storageRate);
            const thf::grain::dsp::GrainKernel normal { 7.0, 0.8 };
            const thf::grain::dsp::GrainKernelHq hq { 9.0, 0.8 };
            float errN = 0.0f, errH = 0.0f;
            for (int i = 100; i < 8000; ++i)
                for (float f : { 0.1f, 0.25f, 0.5f, 0.77f, 0.999f })
                {
                    const auto truth = std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * ((float) i + f) / storageRate);
                    errN = std::max (errN, std::abs (normal.read (s.data() + i, f) - truth));
                    errH = std::max (errH, std::abs (hq.read (s.data() + i, f) - truth));
                }
            logMessage ("  8-tap max error " + juce::String (juce::Decibels::gainToDecibels (errN), 1) + " dB, 12-tap "
                        + juce::String (juce::Decibels::gainToDecibels (errH), 1) + " dB");
            expectLessThan (errN, 3.0e-4f);   // < -70 dB
            expectLessThan (errH, 1.0e-4f);   // < -80 dB
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

        beginTest ("Aliasing grid: 1 kHz saw at +1..+11 semitones, wrong energy below -60 dB");
        {
            // Band-limited saw (harmonics below 24 kHz only).
            juce::AudioBuffer<float> b (1, (int) rate * 3);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                double v = 0.0;
                for (int k = 1; k * 1000 < 24000; ++k)
                    v += std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * k * i / rate) / k;
                b.setSample (0, i, (float) (0.3 * v));
            }
            auto saw = SourceData::fromBuffer (b, rate, "saw");
            double worst = -300.0;
            for (int semis = 1; semis <= 11; ++semis)
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (saw.get());
                auto p = plain();
                p.sizeMs = 500.0f;
                p.density = 4.0f;
                p.scan = 1.0f;
                p.position = 0.1f;
                e.noteOn (60 + semis, 1.0f, p);
                const auto out = render (e, p, (int) rate + (1 << 15));
                worst = std::max (worst, offHarmonicDb (out, (size_t) rate / 2, 1000.0 * std::exp2 (semis / 12.0)));
            }
            logMessage ("  worst wrong energy " + juce::String (worst, 1) + " dB");
            expectLessThan (worst, -60.0);
        }

        beginTest ("Aliasing: a 0.2 fs sine pitched by 2^1.45 vanishes, by 1.35 stays clean");
        {
            auto src = sineSource (0.2 * rate, 3.0);
            auto run = [&src] (double ratio)
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (src.get());
                auto p = plain();
                p.sizeMs = 200.0f;
                p.density = 15.0f;
                p.pitch = (float) (12.0 * std::log2 (ratio));
                e.noteOn (60, 1.0f, p);
                return render (e, p, (int) rate / 2 + (1 << 15));
            };
            const auto reference = rms (run (1.0), (size_t) rate / 2);
            const auto aboveDb = juce::Decibels::gainToDecibels (rms (run (std::exp2 (1.45)), (size_t) rate / 2) / reference, -200.0);
            // Grain windows spread the tone by a few tens of Hz: everything further away is error.
            const auto cleanDb = offHarmonicDb (run (1.35), (size_t) rate / 2, 0.2 * rate * 1.35, 150.0);
            logMessage ("  x2^1.45 (above Nyquist): " + juce::String (aboveDb, 1) + " dB, x1.35: error " + juce::String (cleanDb, 1) + " dB");
            expectLessThan (aboveDb, -60.0);
            expectLessThan (cleanDb, -60.0);
        }

        beginTest ("Note-on attack: a Future Stab cloud is at full level within 20 ms");
        {
            auto src = sources::generate (7);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.spray = 0.01f; p.sizeMs = 350.0f; p.density = 25.0f; p.chaos = 0.2f; p.position = 0.0f;
            p.attackMs = 2.0f; p.sustain = 1.0f;
            e.noteOn (60, 1.0f, p);
            const auto out = render (e, p, (int) rate * 2);
            const std::vector<float> early (out.begin() + (int) (0.01 * rate), out.begin() + (int) (0.02 * rate));
            const std::vector<float> steady (out.begin() + (int) rate, out.end());
            const auto db = juce::Decibels::gainToDecibels (rms (early) / rms (steady));
            logMessage ("  10..20 ms vs steady: " + juce::String (db, 1) + " dB");
            expectGreaterThan (db, -3.0);
        }

        beginTest ("Drive: no step leaving zero, flat response at low drive");
        {
            // A 1 kHz + 15 kHz pair through the drive stage alone.
            auto measure = [] (float amount, double freq)
            {
                thf::grain::dsp::DriveStage d;
                d.prepare (rate);
                std::vector<float> l (32), r (32);
                double c = 0.0, s = 0.0;
                int n = 0;
                for (int block = 0; block < 3000; ++block)
                {
                    for (int i = 0; i < 32; ++i)
                        l[(size_t) i] = r[(size_t) i] = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (block * 32 + i) / rate);
                    d.process (l.data(), r.data(), 32, amount, amount);
                    if (block >= 1500)
                        for (int i = 0; i < 32; ++i, ++n)
                        {
                            const auto ph = 2.0 * juce::MathConstants<double>::pi * freq * (block * 32 + i) / rate;
                            c += l[(size_t) i] * std::cos (ph);
                            s += l[(size_t) i] * std::sin (ph);
                        }
                }
                return 2.0 * std::sqrt (c * c + s * s) / n;
            };
            const auto zero1k = measure (0.0f, 1000.0), tiny1k = measure (0.002f, 1000.0);
            const auto jump = juce::Decibels::gainToDecibels (tiny1k / zero1k);
            const auto low1k = measure (0.05f, 1000.0), low15k = measure (0.05f, 15000.0);
            const auto zero15k = measure (0.0f, 15000.0);
            const auto tilt = juce::Decibels::gainToDecibels ((low15k / low1k));
            const auto flat = juce::Decibels::gainToDecibels (zero15k / zero1k);
            logMessage ("  0 -> 0.002: " + juce::String (jump, 3) + " dB; 15 kHz vs 1 kHz at 0.05: " + juce::String (tilt, 2)
                        + " dB, at 0: " + juce::String (flat, 2) + " dB");
            expectLessThan (std::abs (jump), 0.1);
            expectGreaterThan (tilt, -0.5);
            expectLessThan (std::abs (flat), 0.5);
        }

        beginTest ("Coherent grains are as loud as random ones (+/- 1.5 dB)");
        {
            auto src = noiseSource (10.0);
            auto level = [&src] (bool coherent)
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (src.get());
                auto p = plain();
                p.sizeMs = 120.0f;
                p.density = 60.0f;
                p.chaos = coherent ? 0.0f : 1.0f;
                p.spray = coherent ? 0.0f : 1.0f;
                p.scan = coherent ? 1.0f : 0.0f;
                e.noteOn (60, 1.0f, p);
                return rms (render (e, p, (int) rate * 3), (size_t) rate);
            };
            const auto db = juce::Decibels::gainToDecibels (level (true) / level (false));
            logMessage ("  coherent vs random: " + juce::String (db, 2) + " dB");
            expectWithinAbsoluteError (db, 0.0, 1.5);
        }

        beginTest ("Density changes apply at once (0.5 -> 50 Hz: next grain within 40 ms)");
        {
            auto src = noiseSource (4.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.density = 0.5f;
            p.sizeMs = 20.0f;
            e.noteOn (60, 1.0f, p);
            render (e, p, (int) (0.1 * rate));
            GrainEvent events[64];
            e.popGrainEvents (events, 64);
            p.density = 50.0f;
            int waited = 0;
            bool found = false;
            std::vector<float> l (48), r (48);
            while (waited < (int) (0.2 * rate) && ! found)
            {
                e.render (l.data(), r.data(), 48, p);
                waited += 48;
                found = e.popGrainEvents (events, 64) > 0;
            }
            logMessage ("  first grain after " + juce::String (waited * 1000.0 / rate, 1) + " ms");
            expect (found && waited <= (int) (0.04 * rate));
        }

        beginTest ("Sync: onsets sit on the host beat grid, chord notes together");
        {
            auto src = noiseSource (4.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.sync = true;
            p.syncBeats = 0.25;
            p.bpm = 120.0;
            p.sizeMs = 50.0f;
            const double startPpq = 3.1;
            e.setTransport (true, startPpq);
            for (int note : { 60, 64, 67 })
                e.noteOn (note, 1.0f, p);
            std::vector<float> l (512), r (512);
            std::vector<GrainEvent> all;
            for (int b = 0; b < 100; ++b)
            {
                e.render (l.data(), r.data(), 512, p);
                GrainEvent ev[256];
                const auto n = e.popGrainEvents (ev, 256);
                all.insert (all.end(), ev, ev + n);
            }
            const auto samplesPerBeat = rate * 60.0 / p.bpm;
            int onGrid = 0, offGrid = 0;
            std::map<int64_t, int> perTime;
            for (const auto& ev : all)
            {
                if (ev.time == 0) continue;            // the note-on grains
                const auto beat = startPpq + (double) ev.time / samplesPerBeat;
                const auto nearest = std::round (beat / p.syncBeats) * p.syncBeats;
                if (std::abs ((beat - nearest) * samplesPerBeat) <= 1.0) ++onGrid; else ++offGrid;
                ++perTime[ev.time];
            }
            int together = 0;
            for (auto& [t, count] : perTime) together += count == 3 ? 1 : 0;
            logMessage ("  on grid " + juce::String (onGrid) + ", off " + juce::String (offGrid) + ", ticks with all 3 notes "
                        + juce::String (together) + " of " + juce::String ((int) perTime.size()));
            expect (onGrid >= 21 && offGrid == 0);
            expectEquals (together, (int) perTime.size());
        }

        beginTest ("Link Voices: a chord shares onsets and random choices");
        {
            auto src = noiseSource (4.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.linkVoices = true;
            p.chaos = 0.8f; p.spray = 0.5f; p.stereo = 1.0f; p.reverse = 0.5f;
            p.density = 30.0f;
            p.sizeMs = 60.0f;
            for (int note : { 60, 64, 67 })
                e.noteOn (note, 1.0f, p);
            std::map<int64_t, std::vector<GrainEvent>> byTime;
            std::vector<float> l (512), r (512);
            for (int b = 0; b < 60; ++b)
            {
                e.render (l.data(), r.data(), 512, p);
                GrainEvent ev[256];
                const auto n = e.popGrainEvents (ev, 256);
                for (int i = 0; i < n; ++i) byTime[ev[i].time].push_back (ev[i]);
            }
            int groups = 0, matching = 0;
            for (auto& [t, list] : byTime)
            {
                if (t == 0) continue;
                ++groups;
                bool same = list.size() == 3;
                for (auto& ev : list) same = same && std::abs (ev.pan - list[0].pan) < 1.0e-6f && ev.reversed == list[0].reversed;
                matching += same ? 1 : 0;
            }
            logMessage ("  onsets " + juce::String (groups) + ", shared by all three notes " + juce::String (matching));
            expect (groups > 20);
            expectEquals (matching, groups);
        }

        beginTest ("Regular onsets with a flat window: no level ripple");
        {
            auto src = noiseSource (6.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.window = 1.0f;
            p.sizeMs = 150.0f;
            p.density = 10.0f;            // overlap 1.5
            p.spray = 1.0f;
            e.noteOn (60, 1.0f, p);
            auto out = render (e, p, (int) rate * 4);
            // Envelope in 5 ms windows over whole grain periods: max/min.
            double lo = 1e9, hi = 0.0;
            const int win = (int) (0.005 * rate);
            for (int start = (int) rate; start + win < (int) out.size(); start += win)
            {
                double s = 0.0;
                for (int i = 0; i < win; ++i) s += out[(size_t) (start + i)] * out[(size_t) (start + i)];
                lo = std::min (lo, s); hi = std::max (hi, s);
            }
            // Noise itself fluctuates in 5 ms windows; compare with a dense random cloud.
            const auto rippleDb = 10.0 * std::log10 (hi / lo);
            GrainEngine ref;
            ref.prepare (rate);
            ref.setSource (src.get());
            auto q = p;
            q.window = 0.0f; q.density = 200.0f; q.chaos = 1.0f;
            ref.noteOn (60, 1.0f, q);
            auto refOut = render (ref, q, (int) rate * 4);
            double rlo = 1e9, rhi = 0.0;
            for (int start = (int) rate; start + win < (int) refOut.size(); start += win)
            {
                double s = 0.0;
                for (int i = 0; i < win; ++i) s += refOut[(size_t) (start + i)] * refOut[(size_t) (start + i)];
                rlo = std::min (rlo, s); rhi = std::max (rhi, s);
            }
            const auto refDb = 10.0 * std::log10 (rhi / rlo);
            logMessage ("  5 ms level range " + juce::String (rippleDb, 1) + " dB (dense random cloud: " + juce::String (refDb, 1) + " dB)");
            expectLessThan (rippleDb, refDb + 3.0);
        }

        beginTest ("Voice stealing on a bass note and filter type changes do not click");
        {
            auto src = sineSource (55.0, 4.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.voices = 1;
            p.sizeMs = 200.0f;
            p.density = 20.0f;
            p.root = 33;
            e.noteOn (33, 1.0f, p);
            auto steady = render (e, p, (int) rate);
            const auto steadyStep = maxStep (steady);
            e.noteOn (40, 1.0f, p);                     // steals the only voice
            auto around = render (e, p, (int) (0.05 * rate));
            logMessage ("  bass steal: steady step " + juce::String (steadyStep, 4) + ", around " + juce::String (maxStep (around), 4));
            expectLessThan (maxStep (around), steadyStep * 3.0f + 1.0e-3f);

            // A 110 Hz tone through LP 300 Hz (passes) switched to HP 300 Hz (mostly removed):
            // a hard switch jumps by about the tone's amplitude.
            auto tone = sineSource (110.0, 4.0);
            GrainEngine f;
            f.prepare (rate);
            f.setSource (tone.get());
            auto q = plain();
            q.cutoff = 300.0f;
            q.sizeMs = 400.0f;
            q.density = 10.0f;
            q.root = 60;
            f.noteOn (60, 1.0f, q);
            const auto before = render (f, q, (int) rate);
            q.filterType = 2;
            const auto after = render (f, q, (int) (0.02 * rate));
            logMessage ("  LP -> HP: steady step " + juce::String (maxStep (before), 4) + ", at the switch " + juce::String (maxStep (after), 4));
            expectLessThan (maxStep (after), maxStep (before) * 1.5f);

        }

        beginTest ("Attacks: 8 of 8 clicks found within 2 ms");
        {
            juce::AudioBuffer<float> b (1, (int) rate * 4);
            juce::Random r (7);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.002f * (r.nextFloat() - 0.5f));
            std::vector<int> clicks;
            for (int k = 0; k < 8; ++k)
            {
                const auto at = (int) (rate * (0.2 + 0.43 * k)) + 37 * k;
                clicks.push_back (at);
                for (int i = 0; i < 300; ++i)       // short decaying burst
                    b.setSample (0, at + i, b.getSample (0, at + i) + 0.8f * std::exp (-(float) i / 60.0f) * (i % 2 == 0 ? 1.0f : -1.0f));
            }
            auto s = SourceData::fromBuffer (b, rate, "clicks");
            int found = 0;
            for (auto c : clicks)
                for (const auto& o : s->getOnsets())
                    if (std::abs (o.position - c) <= (int) (0.002 * rate)) { ++found; break; }
            logMessage ("  onsets " + juce::String ((int) s->getOnsets().size()) + ", clicks matched " + juce::String (found));
            expectEquals (found, 8);
            expectEquals ((int) s->getOnsets().size(), 8);
        }

        beginTest ("Zero-crossing snap lands within one sample");
        {
            auto s = sineSource (100.0, 1.0);      // zero crossings every 240 samples
            for (int target : { 2400, 4800, 7200 })
            {
                const auto found = s->nearestZeroCrossing (target + 57, 200);
                expect (std::abs (found - target) <= 1, juce::String (target) + " -> " + juce::String (found));
            }
        }

        beginTest ("Zoom and region: 50 ms on a 10-minute sample");
        {
            const double seconds = 600.0, length = seconds * rate;
            const auto target = 0.05 / seconds;
            expect (wave::minimumRegion (seconds, rate) <= target);
            expect (wave::minimumViewSpan (length) <= target);
            wave::View v;
            for (int i = 0; i < 40; ++i) v = wave::zoomAround (v, 0.3, 0.7, wave::minimumViewSpan (length));
            expect (v.span() <= target && v.start <= 0.3 && v.end >= 0.3);
            const auto start = 0.3, end = wave::clampHandle (0.3 + target, start, false, wave::minimumRegion (seconds, rate));
            expectWithinAbsoluteError (end - start, target, 1.0e-9);
        }

        beginTest ("Root from the file name");
        {
            auto midi = [] (const char* file) { return sources::rootFromName (file).midiNote; };
            auto pitchClass = [] (const char* file) { return sources::rootFromName (file).pitchClass; };
            expectEquals (midi ("Vox Chop 92bpm F#3.wav"), 66);
            expectEquals (midi ("pad_F#3"), 66);
            expectEquals (midi ("Lead C4.aif"), 72);
            expectEquals (midi ("bass-Bb2"), 58);
            expectEquals (pitchClass ("Chords_Fmin.wav"), 5);
            expectEquals (midi ("Chords_Fmin.wav"), -1);
            expectEquals (pitchClass ("Bass.wav"), -1);
            expectEquals (pitchClass ("A Cappella.wav"), -1);
            expectEquals (pitchClass ("Drum Loop 120.wav"), -1);
        }

        beginTest ("Scan Loop: loop, ping-pong, once");
        {
            expectWithinAbsoluteError (GrainEngine::mapPlayhead (1.25, 0), 0.25, 1.0e-9);
            expectWithinAbsoluteError (GrainEngine::mapPlayhead (1.25, 1), 0.75, 1.0e-9);
            expectWithinAbsoluteError (GrainEngine::mapPlayhead (2.25, 1), 0.25, 1.0e-9);
            expectWithinAbsoluteError (GrainEngine::mapPlayhead (1.25, 2), 1.0, 1.0e-9);
            expectWithinAbsoluteError (GrainEngine::mapPlayhead (-0.25, 1), 0.25, 1.0e-9);
        }

        beginTest ("Cue pads: a note plays from its own position");
        {
            auto src = noiseSource (4.0);
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.sizeMs = 50.0f;
            p.position = 0.8f;                         // Position elsewhere
            e.noteOnAt (200, 60, 1.0f, 0.3f, p);
            GrainEvent ev[64];
            std::vector<float> l (256), r (256);
            e.render (l.data(), r.data(), 256, p);
            const auto n = e.popGrainEvents (ev, 64);
            expect (n > 0);
            const auto length = (double) src->getLength();
            for (int i = 0; i < n; ++i)
            {
                // Same mapping as Position: starts spread over the region minus the grain.
                const auto expected = 0.3 * (1.0 - ev[i].span) * length;
                if (ev[i].time == 0 && i == 0)
                    expect (std::abs (ev[i].position * length - expected) <= 1.0, juce::String (ev[i].position * length) + " vs " + juce::String (expected));
            }
            e.noteOff (200, p);
        }

        beginTest ("Pressure modulates like the mod strip");
        {
            auto src = noiseSource (4.0);
            auto level = [&src] (float pressure)
            {
                GrainEngine e;
                e.prepare (rate);
                e.setSource (src.get());
                auto p = plain();
                p.spray = 1.0f; p.chaos = 1.0f; p.density = 60.0f;
                p.modTarget = 6; p.modDepth = 1.0f;     // Level
                e.setPressure (pressure);
                e.noteOn (60, 1.0f, p);
                return rms (render (e, p, (int) rate), (size_t) rate / 4);
            };
            const auto db = juce::Decibels::gainToDecibels (level (0.5f) / level (0.0f));
            logMessage ("  pressure 0.5 on Level: " + juce::String (db, 1) + " dB");
            expectWithinAbsoluteError (db, -6.0, 0.5);
        }

        beginTest ("Pump: the level ducks at each beat within 5 ms and is back by 35 %");
        {
            auto src = noiseSource (4.0);             // dense noise: a steady level to watch
            GrainEngine e;
            e.prepare (rate);
            e.setSource (src.get());
            auto p = plain();
            p.spray = 1.0f; p.chaos = 1.0f; p.density = 200.0f; p.sizeMs = 100.0f;
            p.lfoTarget = 6; p.lfoShape = 5; p.lfoDepth = 1.0f; p.lfoSync = true; p.lfoBeats = 1.0; p.bpm = 120.0;
            e.noteOn (57, 1.0f, p);
            e.syncLfo (0.5, 1.0);                        // half a beat before the next one
            const auto out = render (e, p, (int) rate);
            auto rmsAt = [&out] (double from, double to)
            {
                double s2 = 0.0;
                for (auto i = (int) (from * rate); i < (int) (to * rate); ++i) s2 += out[(size_t) i] * out[(size_t) i];
                return 10.0 * std::log10 (s2 / ((to - from) * rate) + 1.0e-20);
            };
            const auto beat = 0.25, beatLength = 0.5;         // next beat at 0.25 s; 0.5 s per beat
            const auto before = rmsAt (beat - 0.08, beat - 0.005), dip = rmsAt (beat + 0.005, beat + 0.012),
                       back = rmsAt (beat + 0.37 * beatLength, beat + 0.95 * beatLength);
            logMessage ("  before " + juce::String (before, 1) + ", 5 ms after the beat " + juce::String (dip, 1)
                        + ", at 36 % " + juce::String (back, 1) + " dB");
            expectLessThan (dip - before, -12.0);
            expectWithinAbsoluteError (back, before, 1.5);
        }

        beginTest ("Safe Clip: nothing above the ceiling; the reverb rings on after Space goes to 0");
        {
            thf::grain::dsp::Limiter limiter;
            limiter.prepare (rate);
            std::vector<float> l (4800), r (4800);
            float peakOut = 0.0f;
            for (int block = 0; block < 20; ++block)
            {
                for (int i = 0; i < 4800; ++i)
                    l[(size_t) i] = r[(size_t) i] = 3.0f * std::sin (0.05f * (float) (block * 4800 + i)) * (i % 700 == 0 ? 2.0f : 1.0f);
                limiter.process (l.data(), r.data(), 4800, true);
                for (auto v : l) peakOut = std::max (peakOut, std::abs (v));
            }
            expectLessOrEqual (peakOut, thf::grain::dsp::Limiter::ceiling + 1.0e-4f);

            thf::grain::dsp::SpaceReverb reverb;
            reverb.prepare (rate);
            std::vector<float> a (512, 0.0f), b (512, 0.0f);
            a[0] = b[0] = 1.0f;
            reverb.process (a.data(), b.data(), 512, 0.5f, 0.6f);
            expectEquals (a[0], 1.0f);                                    // dry untouched, wet comes later
            double tail = 0.0;
            for (int block = 0; block < 40; ++block)                      // Space now 0: the tail keeps ringing
            {
                std::fill (a.begin(), a.end(), 0.0f);
                std::fill (b.begin(), b.end(), 0.0f);
                reverb.process (a.data(), b.data(), 512, 0.0f, 0.6f);
                if (block > 20) for (auto v : a) tail += v * v;
            }
            expect (tail > 1.0e-6 && reverb.isRinging());
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
            for (int i = 0; i < s->getLength(); ++i) x[(size_t) i] = s->sample (0, i);
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
            for (int i = 4800; i < 43200; ++i) mean += s->sample (0, i);
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
        // The built-in sources are shared by all instances; keep them alive for the whole
        // run instead of rebuilding them for every processor the tests create.
        const juce::SharedResourcePointer<FactorySources> keepFactory;

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
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 118, 0), 10);
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
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 28, 67), 0);   // main encoder +3, slow
            process (p, midi);
            expectWithinAbsoluteError (position->getValue() - posBefore, 0.003f, 1.0e-4f);
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
            expectWithinAbsoluteError (p.param (pid::window)->getValue(), 1.0f, 1.0e-6f);

            // Notes on the pad channel outside the pad range still play.
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (10, 60, 1.0f), 0);
            process (p, midi);
            process (p, midi = {});
            expectEquals (p.getEngine().getActiveVoices(), 1);

            p.setCuePadsPlay (false);                                              // cue pads move Position
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

        beginTest ("MiniLab: learn refuses template CCs, encoder mode is detected");
        {
            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            juce::MidiBuffer midi;
            p.startLearn (pid::chaos);
            for (int cc : { 74, 82, 28, 118, 1, 64, 0, 32, 121 })
            {
                midi.clear();
                midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, 10), 0);
                process (p, midi);
                expect (p.isLearning(), "learn took reserved CC " + juce::String (cc));
                expectEquals (p.getLearnRefusedCc(), cc);
            }
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 10), 0);
            process (p, midi);
            expect (! p.isLearning());
            expectEquals (p.getCcFor (pid::chaos), 20);

            // Detection: an absolute ramp, then relative 63/65 repeats.
            thf::midi::EncoderModeDetector d;
            for (int v : { 60, 61, 62, 63, 64, 65, 66 }) d.feed (v);
            expect (! d.hasDecided());                      // ambiguous so far
            for (int v : { 80, 81, 82 }) d.feed (v);
            expect (d.getMode() == thf::midi::EncoderMode::absolute);
            d.reset();
            for (int v : { 65, 65, 65, 63 }) d.feed (v);
            expect (d.getMode() == thf::midi::EncoderMode::binaryOffset);
            d.reset();
            for (int v : { 1, 1, 1 }) d.feed (v);
            expect (d.getMode() == thf::midi::EncoderMode::twosComplement);
            d.reset();
            for (int v : { 90, 110, 127, 127, 127 }) d.feed (v);   // absolute resting at the top
            expect (d.getMode() == thf::midi::EncoderMode::absolute);
        }

        beginTest ("Main encoder: accelerates when spun, hold + turn browses, click deletes a held cue");
        {
            auto spin = [] (int gapSamples)
            {
                GrainProcessor p;
                p.prepareToPlay (rate, 512);
                auto* position = p.param (pid::position);
                position->setValueNotifyingHost (0.1f);
                const auto before = position->getValue();
                for (int i = 0; i < 10; ++i)
                {
                    juce::MidiBuffer midi;
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, 28, 65), 0);
                    process (p, midi, gapSamples);
                }
                return position->getValue() - before;
            };
            const auto fast = spin (480), slow = spin (4800);   // 10 ticks in 100 ms vs 1 s
            logMessage ("  10 ticks in 100 ms: " + juce::String (fast * 100.0f, 2) + " %, in 1 s: " + juce::String (slow * 100.0f, 2) + " %");
            expect (fast > slow * 2.0f);

            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 118, 127), 0);   // hold
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 28, 66), 10);    // turn +2
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 118, 0), 20);    // release
            process (p, midi);
            expectEquals (p.peekBrowseRequest(), 2);
            expectEquals (p.getPage(), 0);                                          // no page change

            p.setCue (3, 0.4f);
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (10, 47, 1.0f), 0);            // cue pad 4 held
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 118, 127), 10);   // + click
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 118, 0), 20);
            midi.addEvent (juce::MidiMessage::noteOff (10, 47), 30);
            process (p, midi);
            expectEquals (p.getCue (3), -1.0f);
            expectEquals (p.getPage(), 0);
        }

        beginTest ("Pads: amounts come back, holding is momentary");
        {
            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            auto* reverse = p.param (pid::reverse);
            reverse->setValueNotifyingHost (0.35f);
            p.pressPad (2, true, 0.0);  p.pressPad (2, false, 0.05);          // tap: off
            expectWithinAbsoluteError (reverse->getValue(), 0.0f, 1.0e-6f);
            p.pressPad (2, true, 1.0);  p.pressPad (2, false, 1.05);          // tap: back to 35 %
            expectWithinAbsoluteError (reverse->getValue(), 0.35f, 1.0e-6f);

            auto* freeze = p.param (pid::freeze);
            p.pressPad (0, true, 2.0);
            expect (freeze->getValue() > 0.5f);
            p.pressPad (0, false, 2.8);                                        // held: momentary
            expect (freeze->getValue() < 0.5f);
        }

        beginTest ("Host text entry: units are understood");
        {
            GrainProcessor p;
            auto check = [this, &p] (const char* id, const char* text, float expected)
            {
                auto* param = p.param (id);
                expectWithinAbsoluteError (param->convertFrom0to1 (param->getValueForText (text)), expected, expected * 0.01f + 1.0e-3f,
                                           juce::String (id) + " <- " + text);
            };
            check (pid::spray, "50 %", 0.5f);
            check (pid::spray, "0.5", 0.5f);
            check (pid::cutoff, "2 kHz", 2000.0f);
            check (pid::cutoff, "440 Hz", 440.0f);
            check (pid::size, "1.5 s", 1500.0f);
            check (pid::size, "120 ms", 120.0f);
            check (pid::root, "C3", 60.0f);
            check (pid::root, "F#2", 54.0f);
            check (pid::pitch, "+7 st", 7.0f);
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
            float cue0 = 0.0f;
            {
                GrainProcessor a;
                juce::String error;
                expect (a.loadSampleSync (file, error), error);
                hash = a.getUserSample()->contentHash;
                a.param (pid::size)->setValueNotifyingHost (0.7f);
                a.param (pid::cutoff)->setValueNotifyingHost (0.4f);
                a.setCue (2, 0.42f);
                cue0 = a.getCue (0);                   // placed on an attack, or none
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
            expectEquals (b.getCue (0), cue0);
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

        beginTest ("Trim to region, undo brings the whole sample back; favourites; preview");
        {
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("thf-grain-test-trim.wav");
            {
                juce::AudioBuffer<float> b (1, 48000 * 2);
                for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.4f * std::sin ((float) i * 0.03f));
                juce::WavAudioFormat wav;
                file.deleteFile();
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24)))
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            GrainProcessor p;
            p.prepareToPlay (rate, 512);
            juce::String error;
            expect (p.loadSampleSync (file, error), error);
            const auto wholeHash = p.getUserSample()->contentHash;
            p.param (pid::regionStart)->setValueNotifyingHost (0.25f);
            p.param (pid::regionEnd)->setValueNotifyingHost (0.5f);
            p.setCue (1, 0.6f);
            expect (p.trimToRegion (error), error);
            auto trimmed = p.getUserSample();
            expect (trimmed != nullptr && std::abs (trimmed->getLength() - 24000) <= 2);
            expectWithinAbsoluteError (p.param (pid::regionStart)->getValue(), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (p.param (pid::regionEnd)->getValue(), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (p.getCue (1), 0.6f, 1.0e-6f);
            p.getUndoManager().undo();
            expect (p.getUserSample() != nullptr && p.getUserSample()->contentHash == wholeHash);
            expectWithinAbsoluteError (p.param (pid::regionStart)->getValue(), 0.25f, 1.0e-6f);
            if (trimmed != nullptr) trimmed->file.deleteFile();

            library::setFavourite (file, true);
            expect (library::isFavourite (file));
            library::setFavourite (file, false);
            expect (! library::isFavourite (file));

            // Preview: sound without a note.
            p.previewFile (file);
            for (int i = 0; i < 100 && ! p.isPreviewing(); ++i)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                juce::MidiBuffer midi;
                juce::AudioBuffer<float> buffer (2, 512);
                buffer.clear();
                p.processBlock (buffer, midi);
            }
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            p.processBlock (buffer, midi);
            expect (p.isPreviewing() && buffer.getMagnitude (0, 0, 512) > 0.01f);
            p.stopPreview();
            file.deleteFile();
        }

        beginTest ("Chord presets: a Cmaj7 chord stays Cmaj7 (other notes below -12 dB)");
        {
            const std::pair<const char*, std::vector<int>> cases[] = {
                { "Warm Chord Cloud", { 60, 64, 67, 71 } }, { "Warble Pad", { 60, 64, 67, 71 } },
                { "Pumping Chords", { 60, 64, 67, 71 } }, { "Wobble Chords", { 60, 64, 67, 71 } },
                { "Tape Chords", { 60, 64, 67, 71 } }, { "Reverse Swell", { 60, 64, 67, 71 } },
                { "Neon Stab Chords", { 60, 64, 67, 71 } }, { "Sidechain Saw Wall", { 60, 64, 67, 71 } },
                { "Future Stab", { 60 } }, { "Chord Freeze", { 60 } } };   // one key plays the stack's chord
            for (auto& [presetName, notes] : cases)
            {
                const auto b = renderPreset (presetName, notes, 3.0, 3.0);
                const auto c = chroma (b, 0.8, 2.8, 200.0, 700.0);
                const std::set<int> allowed = notes.size() > 1 ? std::set<int> { 0, 4, 7, 11 } : std::set<int> { 0, 2, 4, 7, 11 };
                double loudest = 0.0, foreign = 0.0;
                for (int pc = 0; pc < 12; ++pc)
                {
                    loudest = std::max (loudest, c[(size_t) pc]);
                    if (allowed.count (pc) == 0) foreign = std::max (foreign, c[(size_t) pc]);
                }
                const auto db = 10.0 * std::log10 (foreign / loudest + 1.0e-30);
                logMessage ("  " + juce::String (presetName) + ": other notes " + juce::String (db, 1) + " dB");
                expectLessThan (db, -12.0, presetName);
            }
        }

        beginTest ("Vocal presets on the key C sound in C");
        {
            for (auto* presetName : { "Vox Lead", "Breathy Vox Pad", "Pitched Stutter", "Hyper Vox Lead", "Syllable Stutter",
                                      "Ghost Garage Vox", "Lofi Keys", "Dusty Tape Keys" })
            {
                const auto b = renderPreset (presetName, { 60 }, 1.5, 1.5);
                const auto c = chroma (b, 0.1, 1.4, 90.0, 1100.0);
                const auto strongest = (int) (std::max_element (c.begin(), c.end()) - c.begin());
                logMessage ("  " + juce::String (presetName) + ": strongest pitch class " + juce::String (strongest));
                expectEquals (strongest, 0, presetName);
            }
        }

        beginTest ("Every preset: loudness -16 dB +/- 1.5, Output below +10, low end in mono");
        {
            for (const auto& preset : factoryPresets())
            {
                if (juce::String (preset.name) == "Init") continue;
                const bool mono = std::any_of (preset.values.begin(), preset.values.end(), [] (auto& v)
                                               { return juce::String (v.first) == pid::voiceMode && v.second > 0.5f; });
                const bool bass = juce::String (preset.category) == "Bass";
                const auto base = bass ? 48 : 60;
                const auto notes = mono ? std::vector<int> { base, base + 3, base + 7, base + 10 } : std::vector<int> { 60, 64, 67, 71 };
                const auto b = renderPreset (preset.name, notes, 4.5, 4.0, mono);
                // Loudest 400 ms.
                double loudest = 0.0;
                const int window = (int) (0.4 * rate), step = (int) (0.1 * rate);
                for (int start = 0; start + window <= b.getNumSamples(); start += step)
                {
                    double s2 = 0.0;
                    for (int i = start; i < start + window; ++i)
                        for (int ch = 0; ch < 2; ++ch) s2 += (double) b.getSample (ch, i) * b.getSample (ch, i);
                    loudest = std::max (loudest, s2 / (2.0 * window));
                }
                const auto db = 10.0 * std::log10 (loudest + 1.0e-20);
                float output = 0.0f;
                for (auto& v : preset.values) if (juce::String (v.first) == pid::output) output = v.second;
                expectWithinAbsoluteError (db, -16.0, 2.0, juce::String (preset.name) + " " + juce::String (db, 1) + " dB");
                expectLessThan (output, 10.0f, preset.name);

                // Correlation of L and R below 150 Hz (cross-spectrum), where there is a low end.
                double lr = 0.0, ll = 0.0, rr = 0.0, all = 0.0;
                {
                    constexpr int order = 14, size = 1 << order;
                    juce::dsp::FFT fft (order);
                    std::vector<std::complex<float>> inL ((size_t) size), inR ((size_t) size), outL ((size_t) size), outR ((size_t) size);
                    for (int start = (int) (0.5 * rate); start + size <= b.getNumSamples(); start += size)
                    {
                        for (int i = 0; i < size; ++i)
                        {
                            const auto w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / size);
                            inL[(size_t) i] = b.getSample (0, start + i) * w;
                            inR[(size_t) i] = b.getSample (1, start + i) * w;
                        }
                        fft.perform (inL.data(), outL.data(), false);
                        fft.perform (inR.data(), outR.data(), false);
                        for (int bin = 1; bin < size / 2; ++bin)
                        {
                            const auto hz = bin * rate / size;
                            const auto e = std::norm (outL[(size_t) bin]) + std::norm (outR[(size_t) bin]);
                            all += e;
                            if (hz < 20.0 || hz > 150.0) continue;
                            lr += (outL[(size_t) bin] * std::conj (outR[(size_t) bin])).real();
                            ll += std::norm (outL[(size_t) bin]);
                            rr += std::norm (outR[(size_t) bin]);
                        }
                    }
                }
                if ((ll + rr) > all * 1.0e-3)   // at least -30 dB of the whole below 150 Hz
                    expectGreaterThan (lr / std::sqrt (ll * rr), 0.8, juce::String (preset.name) + " low-end correlation");
            }
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

        beginTest ("Every UI string has a Russian translation");
        {
            const auto root = juce::File (__FILE__).getParentDirectory().getParentDirectory().getChildFile ("plugins/granular");
            const auto table = thf::Translator::parse (root.getChildFile ("Resources/ru_grain.txt").loadFileAsString());
            juce::StringArray keys;
            // Literals passed to tr() and the tables the UI translates.
            for (const auto& entry : juce::RangedDirectoryIterator (root.getChildFile ("Source"), true, "*.cpp"))
            {
                const auto text = entry.getFile().loadFileAsString();
                for (auto pattern : { "tr (\"", "title = \"" })
                    for (int i = text.indexOf (pattern); i >= 0; i = text.indexOf (i + 1, pattern))
                    {
                        const auto start = i + (int) std::strlen (pattern);
                        const auto end = text.indexOf (start, "\"");
                        keys.addIfNotAlreadyThere (text.substring (start, end));
                    }
            }
            for (auto page : layout::pageNames) keys.addIfNotAlreadyThere (juce::String (std::string (page)));
            for (auto& pad : layout::padsBankA) keys.addIfNotAlreadyThere (juce::String (std::string (pad.label)));
            for (auto* list : { &sourceChoices, &scanModeChoices, &voiceModeChoices, &filterChoices, &lfoShapeChoices,
                                &modTargetChoices, &quantizeChoices, &lfoModeChoices, &scanLoopChoices })
                for (auto& c : *list) keys.addIfNotAlreadyThere (c);
            for (auto& preset : factoryPresets()) keys.addIfNotAlreadyThere (preset.category);
            // Help lines: { "what", "how" } pairs.
            const auto views = root.getChildFile ("Source/ui/GrainViews.cpp").loadFileAsString();
            const auto help = views.fromFirstOccurrenceOf ("lines[] = {", false, false).upToFirstOccurrenceOf ("};", false, false);
            for (int i = help.indexOf ("\""); i >= 0;)
            {
                const auto end = help.indexOf (i + 1, "\"");
                keys.addIfNotAlreadyThere (help.substring (i + 1, end));
                i = help.indexOf (end + 1, "\"");
            }
            juce::StringArray missing;
            for (auto& k : keys)
                if (k.isNotEmpty() && table.find (k) == table.end() && k != "thf" && k != "?")
                    missing.add (k);
            expect (missing.isEmpty(), "untranslated: " + missing.joinIntoString (" | "));
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
