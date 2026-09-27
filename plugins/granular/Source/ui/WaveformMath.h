#pragma once

#include <algorithm>
#include <cmath>

// Geometry of the zoomable waveform, kept apart from drawing so tests can check it.
// Positions are fractions 0..1 of the whole sample.
namespace thf::grain::wave
{
    struct View
    {
        double start = 0.0, end = 1.0;
        double span() const noexcept { return end - start; }
        bool isWhole() const noexcept { return start <= 0.0 && end >= 1.0; }
    };

    // The shortest stretch of a sample shown across the screen: 64 samples.
    inline double minimumViewSpan (double lengthSamples) noexcept { return std::min (1.0, 64.0 / std::max (1.0, lengthSamples)); }

    // The smallest region the handles allow: 10 ms (or 16 samples), whatever the file's length.
    inline double minimumRegion (double durationSeconds, double rate) noexcept
    {
        const auto seconds = std::max (0.010, 16.0 / std::max (1.0, rate));
        return std::min (0.5, seconds / std::max (1.0e-6, durationSeconds));
    }

    inline View clampView (View v, double minSpan) noexcept
    {
        auto span = std::clamp (v.span(), minSpan, 1.0);
        auto start = std::clamp (v.start, 0.0, 1.0 - span);
        return { start, start + span };
    }

    // Zooms by `factor` (< 1 = in) keeping `anchor` (0..1 of the file) under the pointer.
    inline View zoomAround (View v, double anchor, double factor, double minSpan) noexcept
    {
        const auto span = std::clamp (v.span() * factor, minSpan, 1.0);
        const auto t = v.span() > 0.0 ? (anchor - v.start) / v.span() : 0.5;
        return clampView ({ anchor - t * span, anchor - t * span + span }, minSpan);
    }

    inline View pan (View v, double delta, double minSpan) noexcept
    {
        return clampView ({ v.start + delta, v.end + delta }, minSpan);
    }

    // A region handle dragged to x never crosses the other handle.
    inline double clampHandle (double x, double other, bool isStart, double minGap) noexcept
    {
        x = std::clamp (x, 0.0, 1.0);
        return isStart ? std::min (x, other - minGap) : std::max (x, other + minGap);
    }
}
