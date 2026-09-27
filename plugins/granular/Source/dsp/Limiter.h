#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace thf::grain::dsp
{
    // Look-ahead peak limiter (1.5 ms) under a fixed ceiling. The gain for a sample is at most
    // what every sample in the look-ahead window allows (running minimum), smoothed by a box
    // average of the same length (a gentle attack that still arrives in time) and released
    // over 60 ms. The audio is always delayed by the look-ahead, so the latency never changes
    // when the limiter is switched on or off. Allocates in prepare() only.
    class Limiter
    {
    public:
        static constexpr float ceiling = 0.944f;            // -0.5 dBFS

        void prepare (double sampleRate)
        {
            rate = sampleRate;
            lookahead = juce::jmax (1, (int) std::lround (0.0015 * sampleRate));
            const auto size = (size_t) juce::nextPowerOfTwo (lookahead + 2);
            delayL.assign (size, 0.0f);
            delayR.assign (size, 0.0f);
            minQueueValue.assign (size, 1.0f);
            minQueueIndex.assign (size, 0);
            boxHistory.assign (size, 1.0f);
            mask = (int) size - 1;
            releaseCoef = 1.0f - std::exp (-1.0f / (0.06f * (float) sampleRate));
            reset();
        }

        void reset() noexcept
        {
            std::fill (delayL.begin(), delayL.end(), 0.0f);
            std::fill (delayR.begin(), delayR.end(), 0.0f);
            std::fill (boxHistory.begin(), boxHistory.end(), 1.0f);
            head = tail = 0;
            time = 0;
            boxSum = (double) (lookahead + 1);
            gain = 1.0f;
        }

        int getLatencySamples() const noexcept { return lookahead; }

        void process (float* left, float* right, int n, bool active) noexcept
        {
            for (int s = 0; s < n; ++s)
            {
                const auto l = left[s], r = right[s];
                const auto peak = std::max (std::abs (l), std::abs (r));
                const auto required = active && peak > ceiling ? ceiling / peak : 1.0f;

                // Running minimum of `required` over the last lookahead + 1 samples.
                while (tail != head && minQueueValue[(size_t) ((tail - 1) & mask)] >= required)
                    tail = (tail - 1) & mask;
                minQueueValue[(size_t) tail] = required;
                minQueueIndex[(size_t) tail] = time;
                tail = (tail + 1) & mask;
                while (minQueueIndex[(size_t) head] <= time - (juce::int64) lookahead - 1)
                    head = (head + 1) & mask;
                const auto windowMin = minQueueValue[(size_t) head];

                // Box average of the minima over the same length.
                const auto slot = (size_t) (time % (juce::int64) (lookahead + 1));
                boxSum += windowMin - boxHistory[slot];
                boxHistory[slot] = windowMin;
                const auto smoothed = (float) (boxSum / (double) (lookahead + 1));

                gain = smoothed < gain ? smoothed : gain + (smoothed - gain) * releaseCoef;
                gain = std::min (gain, smoothed);

                // Delay the audio by the look-ahead and apply.
                const auto write = (size_t) (time & mask);
                delayL[write] = l;
                delayR[write] = r;
                const auto read = (size_t) ((time - lookahead) & mask);
                left[s] = delayL[read] * gain;
                right[s] = delayR[read] * gain;
                ++time;
            }
        }

    private:
        double rate = 48000.0;
        int lookahead = 72, mask = 127, head = 0, tail = 0;
        juce::int64 time = 0;
        std::vector<float> delayL, delayR, minQueueValue, boxHistory;
        std::vector<juce::int64> minQueueIndex;
        double boxSum = 73.0;
        float gain = 1.0f, releaseCoef = 0.0003f;
    };
}
