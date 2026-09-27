#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#if defined (__SSE__) || defined (_M_X64) || defined (_M_AMD64)
 #include <xmmintrin.h>
 #define THF_GRAIN_SSE 1
#elif defined (__ARM_NEON) || defined (__ARM_NEON__)
 #include <arm_neon.h>
 #define THF_GRAIN_NEON 1
#endif

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
        float value (float phase, float fade) const noexcept { return value (phase, fade, 1.0f / fade); }

        float value (float phase, float fade, float invFade) const noexcept
        {
            if (phase < fade)          return riseAt (phase * invFade);
            if (phase > 1.0f - fade)   return riseAt ((1.0f - phase) * invFade);
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

    // Kaiser-windowed sinc kernel for offline rate conversion. Cutoff at 0.9 of Nyquist:
    // -0.8 dB at 19.2 kHz, about -80 dB from 27 kHz up (at 48 kHz). Callers stretch it to move
    // the cutoff down.
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
    // Polyphase Kaiser-windowed sinc for reading the grain copies. Every copy is stored at
    // twice its rate, so its content ends at a quarter of the storage rate and images start
    // at three quarters: a short kernel removes them (8 taps: below -70 dB, 12 taps: below
    // -90 dB). Rows are normalised to unity DC gain so the level never depends on the phase.
    template <int Taps, int Phases, bool Interpolate>
    class PolyphaseKernel
    {
    public:
        static constexpr int taps = Taps;
        static constexpr int first = -(Taps / 2 - 1);   // offset of the first tap from floor(pos)
        static constexpr int reach = Taps / 2;          // samples needed on each side

        PolyphaseKernel (double beta, double cutoff)
        {
            const auto i0Beta = besselI0 (beta);
            const double half = Taps / 2;
            for (int p = 0; p <= Phases; ++p)
            {
                const auto frac = (double) p / Phases;
                double sum = 0.0;
                double row[(size_t) Taps];
                for (int t = 0; t < Taps; ++t)
                {
                    const auto x = (double) (first + t) - frac;
                    const auto arg = 3.14159265358979323846 * cutoff * x;
                    const auto sinc = std::fabs (arg) < 1.0e-12 ? 1.0 : std::sin (arg) / arg;
                    const auto r = x / half;
                    const auto w = std::fabs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta;
                    row[t] = sinc * w;
                    sum += row[t];
                }
                for (int t = 0; t < Taps; ++t)
                    table[(size_t) (p * Taps + t)] = (float) (row[t] / sum);
            }
        }

        // Weights for frac in [0, 1]; w must hold Taps floats (used only when interpolating).
        const float* weights (float frac, float* w) const noexcept
        {
            const auto x = frac * (float) Phases;
            if constexpr (! Interpolate)
            {
                return table.data() + (size_t) ((int) (x + 0.5f) * Taps);
            }
            else
            {
                const auto p = minInt ((int) x, Phases - 1);
                const auto f = x - (float) p;
                const auto* a = table.data() + (size_t) (p * Taps);
                const auto* b = a + Taps;
                for (int t = 0; t < Taps; ++t)
                    w[t] = a[t] + f * (b[t] - a[t]);
                return w;
            }
        }

        // d points at x[floor(pos)]. SIMD in groups of four taps (Taps is a multiple of 4).
        static float dot (const float* d, const float* w) noexcept
        {
            static_assert (Taps % 4 == 0, "groups of four taps");
            d += first;
           #if THF_GRAIN_SSE
            auto acc = _mm_mul_ps (_mm_loadu_ps (d), _mm_loadu_ps (w));
            for (int t = 4; t < Taps; t += 4)
                acc = _mm_add_ps (acc, _mm_mul_ps (_mm_loadu_ps (d + t), _mm_loadu_ps (w + t)));
            acc = _mm_add_ps (acc, _mm_movehl_ps (acc, acc));
            acc = _mm_add_ss (acc, _mm_shuffle_ps (acc, acc, 1));
            return _mm_cvtss_f32 (acc);
           #elif THF_GRAIN_NEON
            auto acc = vmulq_f32 (vld1q_f32 (d), vld1q_f32 (w));
            for (int t = 4; t < Taps; t += 4)
                acc = vmlaq_f32 (acc, vld1q_f32 (d + t), vld1q_f32 (w + t));
            return vaddvq_f32 (acc);
           #else
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
            for (int t = 0; t < Taps; t += 4)
            {
                s0 += d[t] * w[t];
                s1 += d[t + 1] * w[t + 1];
                s2 += d[t + 2] * w[t + 2];
                s3 += d[t + 3] * w[t + 3];
            }
            return (s0 + s2) + (s1 + s3);
           #endif
        }

        // Both channels in one pass over the weights.
        static void dot2 (const float* a, const float* b, const float* w, float& outA, float& outB) noexcept
        {
           #if THF_GRAIN_SSE
            a += first;
            b += first;
            auto wv = _mm_loadu_ps (w);
            auto accA = _mm_mul_ps (_mm_loadu_ps (a), wv);
            auto accB = _mm_mul_ps (_mm_loadu_ps (b), wv);
            for (int t = 4; t < Taps; t += 4)
            {
                wv = _mm_loadu_ps (w + t);
                accA = _mm_add_ps (accA, _mm_mul_ps (_mm_loadu_ps (a + t), wv));
                accB = _mm_add_ps (accB, _mm_mul_ps (_mm_loadu_ps (b + t), wv));
            }
            // Horizontal sums of both at once.
            const auto lo = _mm_unpacklo_ps (accA, accB), hi = _mm_unpackhi_ps (accA, accB);
            const auto sum = _mm_add_ps (lo, hi);                        // a0+a2 b0+b2 a1+a3 b1+b3
            const auto total = _mm_add_ps (sum, _mm_movehl_ps (sum, sum));
            outA = _mm_cvtss_f32 (total);
            outB = _mm_cvtss_f32 (_mm_shuffle_ps (total, total, 1));
           #else
            outA = dot (a, w);
            outB = dot (b, w);
           #endif
        }

        float read (const float* d, float frac) const noexcept
        {
            float w[(size_t) Taps];
            return dot (d, weights (frac, w));
        }

    private:
        static int minInt (int a, int b) noexcept { return a < b ? a : b; }
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

        std::array<float, (size_t) ((Phases + 1) * Taps)> table {};
    };

    // Normal quality: 8 taps, nearest of 2048 phases. HQ: 12 taps, phases interpolated.
    using GrainKernel = PolyphaseKernel<8, 2048, false>;
    using GrainKernelHq = PolyphaseKernel<12, 1024, true>;

    //==============================================================================
    // Exponential ADSR (analog-style curves, after Nigel Redmon). The coefficients are
    // shared by all voices and recomputed once per control block.
    struct AdsrCoefs
    {
        float attackCoef = 0, attackBase = 1, decayCoef = 0, decayBase = 0;
        float releaseCoef = 0, releaseBase = 0, sustain = 1, sustainFollow = 0.002f;

        void set (float sampleRate, float attackMs, float decayMs, float sustainLevel, float releaseMs) noexcept
        {
            constexpr float ratioA = 0.3f, ratioDR = 0.0001f;
            attackCoef  = coef (attackMs * 0.001f * sampleRate, ratioA);
            attackBase  = (1.0f + ratioA) * (1.0f - attackCoef);
            decayCoef   = coef (decayMs * 0.001f * sampleRate, ratioDR);
            decayBase   = (sustainLevel - ratioDR) * (1.0f - decayCoef);
            releaseCoef = coef (releaseMs * 0.001f * sampleRate, ratioDR);
            releaseBase = -ratioDR * (1.0f - releaseCoef);
            sustain     = sustainLevel;
            sustainFollow = 1.0f - std::exp (-1.0f / (0.01f * sampleRate));   // 10 ms
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
        // Voice stealing: a linear fade to silence over `samples` from the current level.
        void steal (float samples) noexcept
        {
            if (stage == Stage::idle) return;
            stage = Stage::steal;
            stealStep = out / std::fmax (1.0f, samples);
        }
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
                    out += (c.sustain - out) * c.sustainFollow;   // follow sustain changes without steps
                    break;
                case Stage::release:
                    out = c.releaseBase + out * c.releaseCoef;
                    if (out <= 0.0f || c.releaseCoef <= 0.0f) reset();
                    break;
                case Stage::steal:
                    out -= stealStep;
                    if (out <= 0.0f) reset();
                    break;
            }
            return out;
        }

    private:
        Stage stage = Stage::idle;
        float out = 0.0f, stealStep = 0.0f;
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
        float a1 = 1, a2 = 0, a3 = 0, k = 1.414f, gain = 1;

        // Damping k falls exponentially with resonance (Q 0.71 at 0, 1.9 at 0.3, 3.8 at 0.5,
        // 20 at 1), so the whole knob is useful. Output drops gently as Q rises (-4.7 dB at Q 20).
        static float damping (float resonance) noexcept
        {
            constexpr float kMax = 1.4142135f, kMin = 0.05f;
            return kMax * std::pow (kMin / kMax, std::fmin (std::fmax (resonance, 0.0f), 1.0f));
        }

        void set (float cutoff, float resonance, float sampleRate) noexcept
        {
            cutoff = std::fmin (std::fmax (cutoff, 10.0f), 0.49f * sampleRate);
            const auto g = std::tan (pi * cutoff / sampleRate);
            k = damping (resonance);
            gain = 1.0f / std::sqrt (1.0f + 0.1f * std::fmax (0.0f, 1.0f / k - 0.7071f));
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
    };

    class Svf
    {
    public:
        enum class Type { lowPass, bandPass, highPass };
        struct Outputs { float lp, bp, hp; };

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        Outputs processAll (float v0, const SvfCoefs& c) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = c.a1 * ic1 + c.a2 * v3;
            const auto v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2 * c.gain, v1 * c.k * c.gain, (v0 - c.k * v1 - v2) * c.gain };   // BP: unity at the centre
        }

        static float select (const Outputs& o, Type type) noexcept
        {
            switch (type)
            {
                case Type::lowPass:  return o.lp;
                case Type::bandPass: return o.bp;
                case Type::highPass: return o.hp;
            }
            return o.lp;
        }

        float process (float v0, const SvfCoefs& c, Type type) noexcept { return select (processAll (v0, c), type); }

    private:
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    //==============================================================================
    // Free-running LFO, bipolar output, evaluated at control rate.
    class Lfo
    {
    public:
        enum class Shape { sine, triangle, saw, square, random };

        void reset (Random& rng) noexcept { phase = 0.0; cycle = 0; held = rng.bipolar(); }

        // Tempo sync: puts the phase where the host transport says it is. A new cycle draws a
        // new random step even when the jump skipped the wrap inside advance().
        void setPhase (double p, Random& rng) noexcept
        {
            const auto c = (int64_t) std::floor (p);
            if (c != cycle) { cycle = c; held = rng.bipolar(); }
            phase = p - std::floor (p);
        }

        float advance (int samples, float rateHz, float sampleRate, Shape shape, Random& rng) noexcept
        {
            phase += (double) rateHz * samples / sampleRate;
            if (phase >= 1.0)
            {
                cycle += (int64_t) std::floor (phase);
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
        int64_t cycle = 0;
        float held = 0.0f;
    };
}
