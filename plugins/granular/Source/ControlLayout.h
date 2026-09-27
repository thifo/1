#pragma once

#include "Parameters.h"
#include <array>
#include <string_view>

// The one table that ties hardware controls to parameters. The MIDI template and the
// on-screen layout are both generated from it, so "control N on screen" and "control N on
// the controller" can never disagree. tests/GrainLayoutTest.cpp checks it.
//
// Physical layout mirrored on screen (left to right, as on the controller):
//
//   [pitch][mod]  [display + main encoder]  (1)(2)(3)(4)   |F1|F2|F3|F4|
//     strips                                (5)(6)(7)(8)
//                 [P1 ][P2 ][P3 ][P4 ][P5 ][P6 ][P7 ][P8 ]
namespace thf::grain::layout
{
    inline constexpr int numPages = 2;
    inline constexpr int numEncoders = 8;
    inline constexpr int numFaders = 4;
    inline constexpr int numPads = 8;

    enum class Page { engine = 0, tone = 1 };

    inline constexpr std::array<std::string_view, numPages> pageNames { "Engine", "Tone" };

    // Encoders 1-8 per page. Row 1 = encoders 1-4, row 2 = encoders 5-8.
    inline constexpr std::array<std::array<std::string_view, numEncoders>, numPages> encoderParams {{
        { pid::scan,   pid::spray,     pid::size,      pid::density,
          pid::pitch,  pid::jitter,    pid::stereo,    pid::reverse },
        { pid::cutoff, pid::resonance, pid::filterEnv, pid::drive,
          pid::lfoRate,pid::lfoDepth,  pid::space,     pid::output },
    }};

    // Faders never change with the page: they are absolute, and paging them would make
    // pickup confusing.
    inline constexpr std::array<std::string_view, numFaders> faderParams {
        pid::attack, pid::decay, pid::sustain, pid::release
    };

    // Main encoder: turn = Position (fine, relative), click = next page.
    inline constexpr std::string_view mainEncoderParam = pid::position;

    // Pads, bank A: performance functions. Bank B: eight cue points in the source.
    enum class PadAction { freeze, hold, reverse, windowCycle, sync, filterCycle, voiceModeCycle, abToggle };

    inline constexpr std::array<PadAction, numPads> padsBankA {
        PadAction::freeze, PadAction::hold, PadAction::reverse, PadAction::windowCycle,
        PadAction::sync, PadAction::filterCycle, PadAction::voiceModeCycle, PadAction::abToggle
    };

    inline constexpr std::array<std::string_view, numPads> padLabels {
        "Freeze", "Hold", "Reverse", "Window", "Sync", "Filter", "Mode", "A/B"
    };

    // Parameter each bank A pad lights up from (empty = not a parameter, e.g. A/B).
    inline constexpr std::array<std::string_view, numPads> padParams {
        pid::freeze, pid::hold, pid::reverse, pid::window, pid::sync, pid::filterType, pid::voiceMode, ""
    };

    // Default MIDI template for the controller's factory "Arturia" program on its MIDI port.
    // Encoders are expected in Relative #1 (binary offset), faders absolute.
    namespace minilab3
    {
        inline constexpr std::array<int, numEncoders> encoderCC { 74, 71, 76, 77, 93, 18, 19, 16 };
        inline constexpr std::array<int, numFaders>   faderCC   { 82, 83, 85, 17 };
        inline constexpr int mainEncoderCC = 28;
        inline constexpr int mainClickCC   = 118;
        inline constexpr int modStripCC    = 1;
        inline constexpr int sustainCC     = 64;
        inline constexpr int padChannel    = 10;   // 1-based, as shown in the MIDI menu
        inline constexpr int padBankANote  = 36;   // pads 1-8, bank A
        inline constexpr int padBankBNote  = 44;   // pads 1-8, bank B
    }

    constexpr int encoderRow (int slot) noexcept    { return slot / 4; }
    constexpr int encoderColumn (int slot) noexcept { return slot % 4; }
}
