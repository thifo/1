#pragma once

#include <cmath>

// Small, allocation-free helpers for hardware controllers: relative encoder decoding and
// soft takeover ("pickup") for absolute faders. Safe to use on the audio thread.
namespace thf::midi
{
    // How an endless encoder reports movement in the CC value.
    enum class EncoderMode
    {
        binaryOffset,   // 64 = no move, 65 = +1, 63 = -1  (Arturia "Relative #1")
        twosComplement, // 1 = +1, 127 = -1
        signMagnitude,  // 1 = +1, 65 = -1 (bit 6 is the sign)
        absolute        // plain 0..127
    };

    // Returns the signed number of ticks for a relative CC value (0 for absolute mode).
    inline int decodeRelative (int value, EncoderMode mode) noexcept
    {
        switch (mode)
        {
            case EncoderMode::binaryOffset:   return value - 64;
            case EncoderMode::twosComplement: return value < 64 ? value : value - 128;
            case EncoderMode::signMagnitude:  return (value & 0x40) ? -(value & 0x3f) : (value & 0x3f);
            case EncoderMode::absolute:       break;
        }
        return 0;
    }

    // Soft takeover for an absolute control: the control only starts moving the parameter
    // once it reaches or crosses the parameter's current value. After the parameter is
    // changed from anywhere else (mouse, automation, preset) the control must pick it up again.
    class Pickup
    {
    public:
        // hw and param are normalised 0..1. Returns true if the hardware value should be applied.
        bool process (float hw, float param) noexcept
        {
            if (engaged && std::abs (param - lastWritten) > externalChangeTolerance)
            {
                engaged = false;
                lastHw = -1.0f;   // the old hardware position says nothing about the new value
            }

            if (! engaged)
            {
                const bool close = std::abs (hw - param) <= catchTolerance;
                const bool crossed = lastHw >= 0.0f && (lastHw - param) * (hw - param) <= 0.0f;
                engaged = close || crossed;
            }

            lastHw = hw;
            if (engaged)
                lastWritten = hw;
            return engaged;
        }

        bool isEngaged() const noexcept   { return engaged; }
        float getLastHardware() const noexcept { return lastHw; }
        void reset() noexcept             { engaged = false; lastHw = -1.0f; }

    private:
        static constexpr float catchTolerance = 0.025f;
        static constexpr float externalChangeTolerance = 0.004f;
        bool engaged = false;
        float lastHw = -1.0f;
        float lastWritten = 0.0f;
    };
}
