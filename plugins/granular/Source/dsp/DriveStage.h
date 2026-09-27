#pragma once

#include <juce_dsp/juce_dsp.h>

namespace thf::grain::dsp
{
    // Drive: tanh saturation at twice the rate (polyphase IIR half-band filters) with
    // first-order antiderivative anti-aliasing. It never switches in or out: at amount 0 the
    // output is the clean path, and the saturated path is blended in continuously, so moving
    // the knob off zero gives no step. Loudness follows the input (RMS matched over ~100 ms)
    // instead of dropping as the drive rises.
    class DriveStage
    {
    public:
        static constexpr int maxBlock = 128;

        void prepare (double sampleRate)
        {
            rate = sampleRate;
            oversampling.initProcessing ((size_t) maxBlock);
            reset();
        }

        void reset() noexcept
        {
            oversampling.reset();
            for (auto& c : state) c = {};
            powerIn = powerWet = 0.0f;
            agc = -1.0f;
        }

        // amountStart/amountEnd: 0..1 at the start and end of the block (ramped inside).
        void process (float* left, float* right, int n, float amountStart, float amountEnd) noexcept
        {
            float* channels[] = { left, right };
            juce::dsp::AudioBlock<float> block (channels, 2, (size_t) n);
            auto up = oversampling.processSamplesUp (block);
            const auto m = (int) up.getNumSamples();

            const auto preStart = preGain (amountStart), preEnd = preGain (amountEnd);
            const auto mixStart = mix (amountStart), mixEnd = mix (amountEnd);
            const auto target = agcTarget (preEnd);
            if (agc < 0.0f) agc = target;
            const auto agcStart = agc;
            agc += (target - agc) * (1.0f - std::exp (-(float) n / (0.1f * (float) rate)));

            float sumIn = 0.0f, sumWet = 0.0f;
            const auto inv = 1.0f / (float) m;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                auto& st = state[ch];
                for (int i = 0; i < m; ++i)
                {
                    const auto t = (float) (i + 1) * inv;
                    const auto pre = preStart + (preEnd - preStart) * t;
                    const auto mx = mixStart + (mixEnd - mixStart) * t;
                    const auto g = agcStart + (agc - agcStart) * t;

                    // Pre-emphasis that cancels the half-sample averaging of both paths
                    // (flat within 0.2 dB up to 20 kHz), one sample of delay at the 2x rate.
                    const auto x = (1.0f + 2.0f * emphasis) * st.x1 - emphasis * (d[i] + st.x2);
                    st.x2 = st.x1;
                    st.x1 = d[i];

                    const auto clean = 0.5f * (x + st.last);            // same response as ADAA
                    const auto u = x * pre, u1 = st.last * pre;
                    const auto du = u - u1;
                    const auto wet = std::fabs (du) > 1.0e-4f ? (logCosh (u) - logCosh (u1)) / du
                                                              : std::tanh (0.5f * (u + u1));
                    st.last = x;
                    sumIn += clean * clean;
                    sumWet += wet * wet;
                    d[i] = clean + mx * (wet * g - clean);
                }
            }
            oversampling.processSamplesDown (block);

            const auto a = 1.0f - std::exp (-(float) n / (0.1f * (float) rate));
            powerIn += (sumIn * inv - powerIn) * a;
            powerWet += (sumWet * inv - powerWet) * a;
        }

    private:
        static float preGain (float amount) noexcept { return std::exp2 (juce::jlimit (0.0f, 1.0f, amount) * 30.0f / 6.0206f); }
        // Blend of the saturated path: 0 at amount 0, fully in from 0.1 up.
        static float mix (float amount) noexcept { return juce::jlimit (0.0f, 1.0f, amount * 10.0f); }

        // Gain that brings the saturated path back to the level of the clean one.
        float agcTarget (float pre) const noexcept
        {
            constexpr float eps = 1.0e-7f;
            return juce::jlimit (1.0f / 64.0f, 4.0f, std::sqrt ((powerIn + eps) / (powerWet + eps * pre * pre)));
        }

        static float logCosh (float x) noexcept
        {
            const auto ax = std::fabs (x);
            return ax + std::log1p (std::exp (-2.0f * ax)) - 0.69314718f;
        }

        static constexpr float emphasis = 0.176f;
        struct ChannelState { float x1 = 0, x2 = 0, last = 0; };
        std::array<ChannelState, 2> state {};
        double rate = 48000.0;
        float powerIn = 0.0f, powerWet = 0.0f, agc = -1.0f;
        juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    };
}
