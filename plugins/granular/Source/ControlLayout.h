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
    inline constexpr int numPages = 3;
    inline constexpr int numEncoders = 8;
    inline constexpr int numFaders = 4;
    inline constexpr int numPads = 8;

    enum class Page { engine = 0, tone = 1, sample = 2 };

    inline constexpr std::array<std::string_view, numPages> pageNames { "Engine", "Tone", "Sample" };

    // Encoders 1-8 per page. Row 1 = encoders 1-4, row 2 = encoders 5-8. The first six of
    // Engine never move (muscle memory); Output lives on screen only.
    inline constexpr std::array<std::array<std::string_view, numEncoders>, numPages> encoderParams {{
        { pid::scan,        pid::spray,     pid::size,      pid::density,
          pid::pitch,       pid::jitter,    pid::chaos,     pid::reverse },
        { pid::cutoff,      pid::resonance, pid::filterEnv, pid::drive,
          pid::lfoRate,     pid::lfoDepth,  pid::space,     pid::spaceSize },
        { pid::regionStart, pid::regionEnd, pid::root,      pid::fine,
          pid::quantize,    pid::stereo,    pid::glide,     pid::scanMode },
    }};

    // Faders never change with the page: they are absolute, and paging them would make
    // pickup confusing.
    inline constexpr std::array<std::string_view, numFaders> faderParams {
        pid::attack, pid::decay, pid::sustain, pid::release
    };

    // Main encoder: turn = Position (accelerated), click = next page, hold + turn = browse
    // samples in the current sample's folder.
    inline constexpr std::string_view mainEncoderParam = pid::position;

    // Pads, bank A ("Play"): performance functions. Bank B ("Cues"): eight cue points.
    // A tap toggles; holding a toggle pad makes it momentary (back on release).
    enum class PadAction { freeze, link, reverse, window, sync, filterCycle, voiceModeCycle, abToggle };

    struct PadSlot
    {
        PadAction action;
        std::string_view label;     // translated on screen
        std::string_view param;     // parameter it lights up from ("" = none)
        bool momentary;             // hold = momentary
    };

    inline constexpr std::array<PadSlot, numPads> padsBankA {{
        { PadAction::freeze,         "Freeze",  pid::freeze,     true  },
        { PadAction::link,           "Link",    pid::linkVoices, true  },
        { PadAction::reverse,        "Reverse", pid::reverse,    true  },
        { PadAction::window,         "Window",  pid::window,     true  },
        { PadAction::sync,           "Sync",    pid::sync,       true  },
        { PadAction::filterCycle,    "Filter",  pid::filterType, false },
        { PadAction::voiceModeCycle, "Voices",  pid::voiceMode,  false },
        { PadAction::abToggle,       "A/B",     "",              false },
    }};

    // MIDI CCs MIDI learn never takes: the template's own controls, bank select, and the
    // channel mode messages (reset all controllers, all notes off, ...).
    constexpr bool isReservedForLearn (int cc) noexcept;

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

    constexpr bool isReservedForLearn (int cc) noexcept
    {
        using namespace minilab3;
        for (auto e : encoderCC) if (e == cc) return true;
        for (auto f : faderCC)   if (f == cc) return true;
        return cc == mainEncoderCC || cc == mainClickCC || cc == modStripCC || cc == sustainCC
            || cc == 0 || cc == 32 || cc >= 120;
    }

    constexpr int encoderRow (int slot) noexcept    { return slot / 4; }
    constexpr int encoderColumn (int slot) noexcept { return slot % 4; }
}
