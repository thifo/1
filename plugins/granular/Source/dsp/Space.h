#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

namespace thf::grain::dsp
{
    // "Space": a feedback delay network reverb. Eight modulated delay lines mixed by a
    // Hadamard matrix, damped inside the loop, fed through a pre-delay and four allpass
    // diffusers. Modulation keeps plucks and bells from ringing metallically. The dry signal
    // is never touched: the reverb is only added, so switching it on changes nothing but the
    // tail. Allocates in prepare() only.
    class SpaceReverb
    {
    public:
        static constexpr int lines = 8;

        // Decay time (-60 dB) for Space Size 0..1: 0.6 s .. 7 s.
        static double decaySeconds (float size) noexcept { return 0.6 * std::pow (11.7, (double) juce::jlimit (0.0f, 1.0f, size)); }

        void prepare (double sampleRate)
        {
            rate = sampleRate;
            const auto scale = rate / 48000.0;
            static constexpr double lengthsMs[lines] = { 31.3, 37.9, 41.1, 47.3, 53.9, 59.1, 66.7, 73.3 };
            for (int i = 0; i < lines; ++i)
            {
                auto& line = delays[(size_t) i];
                line.length = lengthsMs[i] * 0.001 * rate;
                line.buffer.assign ((size_t) juce::nextPowerOfTwo ((int) line.length + 64), 0.0f);
                line.mask = (int) line.buffer.size() - 1;
                line.lfoRate = (0.11 + 0.07 * i) / rate;          // 0.11 .. 0.6 Hz
                line.lfoPhase = 0.137 * i;
            }
            static constexpr int diffuserSamples[4] = { 142, 107, 379, 277 };
            for (int i = 0; i < 4; ++i)
            {
                auto& d = diffusers[(size_t) i];
                d.length = juce::jmax (1, (int) std::lround (diffuserSamples[i] * scale));
                d.buffer.assign ((size_t) d.length, 0.0f);
            }
            preDelay.assign ((size_t) juce::nextPowerOfTwo ((int) (0.05 * rate) + 2), 0.0f);
            preMask = (int) preDelay.size() - 1;
            modDepth = 4.0 * scale;
            reset();
        }

        void reset() noexcept
        {
            for (auto& line : delays) { std::fill (line.buffer.begin(), line.buffer.end(), 0.0f); line.write = 0; line.damp = 0.0f; }
            for (auto& d : diffusers) { std::fill (d.buffer.begin(), d.buffer.end(), 0.0f); d.index = 0; }
            std::fill (preDelay.begin(), preDelay.end(), 0.0f);
            preWrite = 0;
            energy = 0.0f;
            amountSmoothed = -1.0f;
        }

        // Adds the reverb to left/right. amount 0..1 (send level), size 0..1 (decay).
        void process (float* left, float* right, int n, float amount, float size) noexcept
        {
            const auto t60 = decaySeconds (size);
            std::array<float, lines> feedback {};
            for (int i = 0; i < lines; ++i)
                feedback[(size_t) i] = (float) std::pow (10.0, -3.0 * delays[(size_t) i].length / (t60 * rate));
            // Bigger spaces are darker: damping cutoff 9 kHz .. 4.5 kHz.
            const auto cutoff = 9000.0 * std::pow (0.5, (double) size);
            const auto dampCoef = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / rate));
            const auto preSamples = (int) ((0.012 + 0.02 * size) * rate);

            if (amountSmoothed < 0.0f) amountSmoothed = amount;
            const auto amountStart = amountSmoothed;
            amountSmoothed += (amount - amountSmoothed) * (1.0f - std::exp (-(float) n / (0.02f * (float) rate)));
            const auto amountStep = (amountSmoothed - amountStart) / (float) n;

            float blockEnergy = 0.0f;
            for (int s = 0; s < n; ++s)
            {
                // Send (so turning Space down lets the tail ring out), pre-delay, diffusion.
                const auto send = amountStart + amountStep * (float) (s + 1);
                preDelay[(size_t) preWrite] = 0.5f * (left[s] + right[s]) * send;
                auto x = preDelay[(size_t) ((preWrite - preSamples) & preMask)];
                preWrite = (preWrite + 1) & preMask;
                for (auto& d : diffusers)
                {
                    const auto delayed = d.buffer[(size_t) d.index];
                    const auto v = x + 0.62f * delayed;
                    d.buffer[(size_t) d.index] = v;
                    x = delayed - 0.62f * v;
                    if (++d.index >= d.length) d.index = 0;
                }

                // Read the lines (modulated), damp.
                std::array<float, lines> y {};
                for (int i = 0; i < lines; ++i)
                {
                    auto& line = delays[(size_t) i];
                    line.lfoPhase += line.lfoRate;
                    if (line.lfoPhase >= 1.0) line.lfoPhase -= 1.0;
                    const auto delay = line.length + modDepth * std::sin (2.0 * juce::MathConstants<double>::pi * line.lfoPhase);
                    const auto readPos = (double) line.write - delay;
                    const auto base = (int) std::floor (readPos);
                    const auto frac = (float) (readPos - base);
                    const auto a = line.buffer[(size_t) (base & line.mask)], b = line.buffer[(size_t) ((base + 1) & line.mask)];
                    line.damp += dampCoef * ((a + frac * (b - a)) - line.damp);
                    y[(size_t) i] = line.damp;
                }

                // Hadamard 8x8 (fast, orthonormal) and write back with the decay gains.
                auto h = y;
                for (int len = 1; len < lines; len <<= 1)
                    for (int i = 0; i < lines; i += len << 1)
                        for (int j = i; j < i + len; ++j)
                        {
                            const auto u = h[(size_t) j], v = h[(size_t) (j + len)];
                            h[(size_t) j] = u + v;
                            h[(size_t) (j + len)] = u - v;
                        }
                constexpr float norm = 0.35355339f;   // 1 / sqrt(8)
                for (int i = 0; i < lines; ++i)
                {
                    auto& line = delays[(size_t) i];
                    line.buffer[(size_t) line.write] = (h[(size_t) i] * norm + (i % 2 == 0 ? x : -x)) * feedback[(size_t) i];
                    line.write = (line.write + 1) & line.mask;
                }

                // Two decorrelated outputs.
                const auto outL = 0.5f * (y[0] - y[2] + y[4] - y[6] + y[1]);
                const auto outR = 0.5f * (y[1] - y[3] + y[5] - y[7] + y[2]);
                left[s] += outL * 0.6f;
                right[s] += outR * 0.6f;
                blockEnergy += outL * outL + outR * outR;
            }
            energy = blockEnergy / (float) n;
        }

        // Still audible (for keeping the reverb running after Space went to 0).
        bool isRinging() const noexcept { return energy > 1.0e-10f; }

    private:
        struct Line
        {
            std::vector<float> buffer;
            int mask = 0, write = 0;
            double length = 1.0, lfoRate = 0.0, lfoPhase = 0.0;
            float damp = 0.0f;
        };
        struct Diffuser
        {
            std::vector<float> buffer;
            int length = 1, index = 0;
        };

        double rate = 48000.0, modDepth = 4.0;
        std::array<Line, lines> delays;
        std::array<Diffuser, 4> diffusers;
        std::vector<float> preDelay;
        int preMask = 0, preWrite = 0;
        float energy = 0.0f, amountSmoothed = -1.0f;
    };
}
