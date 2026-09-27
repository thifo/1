#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Small DSP building blocks of the grain engine. Everything here is allocation-free after
// construction and safe for the audio thread.
namespace thf::grain::dsp
{
    inline constexpr float pi = 3.14159265358979323846f;

    //==============================================================================
    // xoshiro128+: fast, seedable, reproducible across platforms.
    class Random
    {
    public:
        explicit Random (uint32_t seed = 0x7f4a7c15u) { setSeed (seed); }

        void setSeed (uint32_t seed) noexcept
        {
            uint64_t x = seed;
            for (auto& s : state)
            {
                x += 0x9e3779b97f4a7c15ull;           // splitmix64
                uint64_t z = x;
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
                s = (uint32_t) ((z ^ (z >> 31)) >> 16);
            }
        }

        uint32_t next() noexcept
        {
            const auto result = state[0] + state[3];
            const auto t = state[1] << 9;
            state[2] ^= state[0]; state[3] ^= state[1];
            state[1] ^= state[2]; state[0] ^= state[3];
            state[2] ^= t;
            state[3] = (state[3] << 11) | (state[3] >> 21);
            return result;
        }

        // Uniform in [0, 1).
        float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
        // Uniform in [-1, 1).
        float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }
        // Exponential with mean 1 (inter-onset times of a Poisson process).
        float exponential() noexcept { return -std::log (1.0f - uniform() * 0.9999f); }

    private:
        std::array<uint32_t, 4> state {};
    };

    //==============================================================================
    // Grain window: a Tukey (tapered cosine) window. fade = fraction of the grain used by
    // each raised-cosine side, 0.5 = Hann, smaller = flatter top.
    class WindowTable
    {
    public:
        static constexpr int size = 2048;

        WindowTable()
        {
            for (int i = 0; i <= size; ++i)
                rise[(size_t) i] = 0.5f - 0.5f * std::cos (pi * (float) i / (float) size);
            rise[size + 1] = 1.0f;
        }

        // Raised cosine rising edge, x in [0, 1].
        float riseAt (float x) const noexcept
        {
            const auto p = x * (float) size;
            const auto i = (int) p;
            if (i >= size) return 1.0f;
            const auto f = p - (float) i;
            return rise[(size_t) i] + f * (rise[(size_t) i + 1] - rise[(size_t) i]);
        }

        // Window value at phase in [0, 1).
        float value (float phase, float fade) const noexcept
        {
            if (phase < fade)          return riseAt (phase / fade);
            if (phase > 1.0f - fade)   return riseAt ((1.0f - phase) / fade);
            return 1.0f;
        }

    private:
        std::array<float, size + 2> rise {};
    };

    // Window shape 0..1 (Hann .. nearly rectangular) to the side fade fraction. Each side
    // keeps at least minFadeSeconds so even "rectangular" grains never click.
    inline float windowFade (float shape, float grainSamples, float sampleRate) noexcept
    {
        constexpr float minFadeSeconds = 0.002f;
        const auto fade = 0.5f * (1.0f - 0.92f * shape);
        const auto minFade = minFadeSeconds * sampleRate / std::fmax (grainSamples, 1.0f);
        return std::fmin (0.5f, std::fmax (fade, minFade));
    }

    // Mean of w^2 over the Tukey window: 1 - 2f + 2f * 3/8.
    inline float windowEnergy (float fade) noexcept { return 1.0f - 1.25f * fade; }

    //==============================================================================
    // 4-point, 3rd-order Hermite interpolation. d points at x[0]; frac in [0, 1).
    inline float hermite (const float* d, float frac) noexcept
    {
        const auto xm1 = d[-1], x0 = d[0], x1 = d[1], x2 = d[2];
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + x0;
    }

    // Kaiser-windowed sinc kernel for band-limited reads (HQ mode). Cutoff at 0.9 of Nyquist:
    // -0.8 dB at 19.2 kHz, about -80 dB from 27 kHz up (at 48 kHz). The kernel is stretched by
    // `stretch` >= 1 when reading faster than 1:1, which moves the cutoff down with the ratio.
    class SincTable
    {
    public:
        static constexpr int halfTaps = 12;
        static constexpr int resolution = 512;   // table points per unit of x
        static constexpr double cutoff = 0.9;

        SincTable()
        {
            constexpr double beta = 8.0;
            const auto i0Beta = besselI0 (beta);
            for (int i = 0; i <= halfTaps * resolution; ++i)
            {
                const auto x = (double) i / resolution;
                const auto arg = 3.14159265358979323846 * cutoff * x;
                const auto sinc = x < 1.0e-12 ? 1.0 : std::sin (arg) / arg;
                const auto r = x / halfTaps;
                const auto w = besselI0 (beta * std::sqrt (std::fmax (0.0, 1.0 - r * r))) / i0Beta;
                table[(size_t) i] = (float) (cutoff * sinc * w);
            }
            table[(size_t) halfTaps * resolution + 1] = 0.0f;
        }

        float kernel (float x) const noexcept
        {
            x = std::fabs (x) * (float) resolution;
            const auto i = (int) x;
            if (i >= halfTaps * resolution) return 0.0f;
            const auto f = x - (float) i;
            return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
        }

        static constexpr int maxWeights = 4 * halfTaps + 2;   // stretch up to 2

        // Kernel weights for reading at x[floor(pos) + first + i], i < count. Scaled by
        // 1/stretch. Compute once, apply to every channel with dot().
        int weights (float frac, float stretch, float invStretch, float* w, int& first) const noexcept
        {
            // Never reach further than the padding allows (stretch is clamped to 2 by callers).
            const auto reach = juce_ceil (halfTaps * std::fmin (stretch, 2.0f));
            first = -reach + 1;
            const auto count = 2 * reach;
            const auto scale = invStretch * (float) resolution;
            for (int i = 0; i < count; ++i)
            {
                auto x = std::fabs ((float) (first + i) - frac) * scale;
                const auto j = (int) x;
                if (j >= halfTaps * resolution) { w[i] = 0.0f; continue; }
                const auto f = x - (float) j;
                w[i] = (table[(size_t) j] + f * (table[(size_t) j + 1] - table[(size_t) j])) * invStretch;
            }
            return count;
        }

        static float dot (const float* d, const float* w, int count) noexcept
        {
            float sum = 0.0f;
            for (int i = 0; i < count; ++i)
                sum += d[i] * w[i];
            return sum;
        }

        // d points at x[floor(pos)], frac = pos - floor(pos). Needs halfTaps * stretch + 1
        // valid samples on each side.
        float read (const float* d, float frac, float stretch) const noexcept
        {
            float w[maxWeights];
            int first = 0;
            const auto count = weights (frac, stretch, 1.0f / stretch, w, first);
            return dot (d + first, w, count);
        }

    private:
        static int juce_ceil (float x) noexcept { const auto i = (int) x; return (float) i < x ? i + 1 : i; }

    public:

    private:
        static double besselI0 (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        }

        std::array<float, halfTaps * resolution + 2> table {};
    };

    //==============================================================================
    // Exponential ADSR (analog-style curves, after Nigel Redmon). The coefficients are
    // shared by all voices and recomputed once per control block.
    struct AdsrCoefs
    {
        float attackCoef = 0, attackBase = 1, decayCoef = 0, decayBase = 0;
        float releaseCoef = 0, releaseBase = 0, sustain = 1, stealCoef = 0, stealBase = 0;

        void set (float sampleRate, float attackMs, float decayMs, float sustainLevel, float releaseMs) noexcept
        {
            constexpr float ratioA = 0.3f, ratioDR = 0.0001f;
            attackCoef  = coef (attackMs * 0.001f * sampleRate, ratioA);
            attackBase  = (1.0f + ratioA) * (1.0f - attackCoef);
            decayCoef   = coef (decayMs * 0.001f * sampleRate, ratioDR);
            decayBase   = (sustainLevel - ratioDR) * (1.0f - decayCoef);
            releaseCoef = coef (releaseMs * 0.001f * sampleRate, ratioDR);
            releaseBase = -ratioDR * (1.0f - releaseCoef);
            stealCoef   = coef (0.003f * sampleRate, ratioDR);
            stealBase   = -ratioDR * (1.0f - stealCoef);
            sustain     = sustainLevel;
        }

        static float coef (float samples, float ratio) noexcept
        {
            return samples <= 1.0f ? 0.0f : std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
        }
    };

    class Adsr
    {
    public:
        enum class Stage { idle, attack, decay, sustain, release, steal };

        void gateOn() noexcept   { stage = Stage::attack; }
        void gateOff() noexcept  { if (stage != Stage::idle && stage != Stage::steal) stage = Stage::release; }
        void steal() noexcept    { if (stage != Stage::idle) stage = Stage::steal; }
        void reset() noexcept    { stage = Stage::idle; out = 0.0f; }

        bool isActive() const noexcept   { return stage != Stage::idle; }
        bool isReleasing() const noexcept{ return stage == Stage::release || stage == Stage::steal; }
        Stage getStage() const noexcept  { return stage; }
        float getLevel() const noexcept  { return out; }

        float process (const AdsrCoefs& c) noexcept
        {
            switch (stage)
            {
                case Stage::idle: break;
                case Stage::attack:
                    out = c.attackBase + out * c.attackCoef;
                    if (out >= 1.0f || c.attackCoef <= 0.0f) { out = 1.0f; stage = Stage::decay; }
                    break;
                case Stage::decay:
                    out = c.decayBase + out * c.decayCoef;
                    if (out <= c.sustain || c.decayCoef <= 0.0f) { out = c.sustain; stage = Stage::sustain; }
                    break;
                case Stage::sustain:
                    out += (c.sustain - out) * 0.002f;   // follow sustain changes without steps
                    break;
                case Stage::release:
                    out = c.releaseBase + out * c.releaseCoef;
                    if (out <= 0.0f || c.releaseCoef <= 0.0f) reset();
                    break;
                case Stage::steal:
                    out = c.stealBase + out * c.stealCoef;
                    if (out <= 0.0f) reset();
                    break;
            }
            return out;
        }

    private:
        Stage stage = Stage::idle;
        float out = 0.0f;
    };

    // Attack-decay envelope for the filter: 1 ms linear attack, exponential decay to -60 dB
    // over decayMs.
    class FilterEnvelope
    {
    public:
        void trigger() noexcept { attacking = true; }
        void reset() noexcept   { attacking = false; out = 0.0f; }

        void set (float sampleRate, float decayMs) noexcept
        {
            attackStep = 1.0f / (0.001f * sampleRate);
            decayCoef = std::exp (-6.9077553f / std::fmax (1.0f, decayMs * 0.001f * sampleRate));
        }

        // Advances n samples, returns the value at the end (control rate).
        float advance (int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                if (attacking)
                {
                    out += attackStep;
                    if (out >= 1.0f) { out = 1.0f; attacking = false; }
                }
                else
                {
                    out *= decayCoef;
                }
            }
            return out;
        }

    private:
        bool attacking = false;
        float out = 0.0f, attackStep = 0.02f, decayCoef = 0.999f;
    };

    //==============================================================================
    // State-variable filter, trapezoidal integration (topology-preserving transform).
    struct SvfCoefs
    {
        float a1 = 1, a2 = 0, a3 = 0, k = 1.414f;

        void set (float cutoff, float resonance, float sampleRate) noexcept
        {
            cutoff = std::fmin (std::fmax (cutoff, 10.0f), 0.49f * sampleRate);
            const auto g = std::tan (pi * cutoff / sampleRate);
            k = 1.4142135f - 1.37f * std::fmin (std::fmax (resonance, 0.0f), 1.0f);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
    };

    class Svf
    {
    public:
        enum class Type { lowPass, bandPass, highPass };

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        float process (float v0, const SvfCoefs& c, Type type) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = c.a1 * ic1 + c.a2 * v3;
            const auto v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            switch (type)
            {
                case Type::lowPass:  return v2;
                case Type::bandPass: return v1 * c.k;     // unity gain at the centre
                case Type::highPass: return v0 - c.k * v1 - v2;
            }
            return v2;
        }

    private:
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    //==============================================================================
    // tanh saturation with first-order antiderivative anti-aliasing.
    class Drive
    {
    public:
        void reset() noexcept { prev = { 0.0f, 0.0f }; }

        float process (int channel, float x) noexcept
        {
            auto& x1 = prev[(size_t) channel];
            const auto d = x - x1;
            float y;
            if (std::fabs (d) > 1.0e-4f)
                y = (logCosh (x) - logCosh (x1)) / d;
            else
                y = std::tanh (0.5f * (x + x1));
            x1 = x;
            return y;
        }

    private:
        static float logCosh (float x) noexcept
        {
            const auto a = std::fabs (x);
            return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f;
        }

        std::array<float, 2> prev {};
    };

    //==============================================================================
    // Free-running LFO, bipolar output, evaluated at control rate.
    class Lfo
    {
    public:
        enum class Shape { sine, triangle, saw, square, random };

        void reset (Random& rng) noexcept { phase = 0.0; held = rng.bipolar(); }

        // Tempo sync: puts the phase where the host transport says it is.
        void setPhase (double p) noexcept { phase = p - std::floor (p); }

        float advance (int samples, float rateHz, float sampleRate, Shape shape, Random& rng) noexcept
        {
            phase += (double) rateHz * samples / sampleRate;
            if (phase >= 1.0)
            {
                phase -= std::floor (phase);
                held = rng.bipolar();
            }
            const auto p = (float) phase;
            switch (shape)
            {
                case Shape::sine:     return std::sin (2.0f * pi * p);
                case Shape::triangle: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
                case Shape::saw:      return 2.0f * p - 1.0f;
                case Shape::square:   return p < 0.5f ? 1.0f : -1.0f;
                case Shape::random:   return held;
            }
            return 0.0f;
        }

    private:
        double phase = 0.0;
        float held = 0.0f;
    };
}
