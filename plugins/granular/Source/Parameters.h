#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace thf::grain
{
    // Parameter IDs. Names shown to hosts are English; IDs never change once released:
    // a new behaviour gets a new ID or a bumped ParameterID version.
    namespace pid
    {
        // Source and grains
        inline constexpr const char* source     = "source";
        inline constexpr const char* root       = "root";
        inline constexpr const char* position   = "position";
        inline constexpr const char* regionStart= "regionStart";
        inline constexpr const char* regionEnd  = "regionEnd";
        inline constexpr const char* normalize  = "normalize";
        inline constexpr const char* scan       = "scan";
        inline constexpr const char* scanMode   = "scanMode";
        inline constexpr const char* freeze     = "freeze";
        inline constexpr const char* spray      = "spray";
        inline constexpr const char* size       = "size";
        inline constexpr const char* density    = "density";
        inline constexpr const char* sync       = "sync";
        inline constexpr const char* syncRate   = "syncRate";
        inline constexpr const char* chaos      = "chaos";
        inline constexpr const char* window     = "window";
        inline constexpr const char* pitch      = "pitch";
        inline constexpr const char* fine       = "fine";
        inline constexpr const char* jitter     = "jitter";
        inline constexpr const char* reverse    = "reverse";
        inline constexpr const char* stereo     = "stereo";
        inline constexpr const char* quantize   = "quantize";

        // Voices
        inline constexpr const char* voices     = "voices";
        inline constexpr const char* voiceMode  = "voiceMode";
        inline constexpr const char* glide      = "glide";
        inline constexpr const char* hold       = "hold";
        inline constexpr const char* bendRange  = "bendRange";
        inline constexpr const char* velocity   = "velocity";
        inline constexpr const char* attack     = "attack";
        inline constexpr const char* decay      = "decay";
        inline constexpr const char* sustain    = "sustain";
        inline constexpr const char* release    = "release";

        // Tone
        inline constexpr const char* filterType = "filterType";
        inline constexpr const char* cutoff     = "cutoff";
        inline constexpr const char* resonance  = "resonance";
        inline constexpr const char* filterEnv  = "filterEnv";
        inline constexpr const char* filterDecay= "filterDecay";
        inline constexpr const char* drive      = "drive";

        // Modulation
        inline constexpr const char* lfoRate    = "lfoRate";
        inline constexpr const char* lfoDepth   = "lfoDepth";
        inline constexpr const char* lfoShape   = "lfoShape";
        inline constexpr const char* lfoTarget  = "lfoTarget";
        inline constexpr const char* modTarget  = "modTarget";
        inline constexpr const char* modDepth   = "modDepth";
        inline constexpr const char* lfoMode    = "lfoMode";
        inline constexpr const char* lfoDivision= "lfoDivision";

        // Space (reverb)
        inline constexpr const char* space      = "space";
        inline constexpr const char* spaceSize  = "spaceSize";

        // Output
        inline constexpr const char* output     = "output";
        inline constexpr const char* safeClip   = "safeClip";
        inline constexpr const char* hq         = "hq";
        inline constexpr const char* linkVoices = "linkVoices";
        inline constexpr const char* scanLoop   = "scanLoop";
    }

    // Choice lists (order is part of the saved state: append only). Hosts store automation of a
    // choice as index / (count - 1), so a list that grows would shift every existing curve:
    // the parameters are created with room to grow (reservedChoices), and these lists hold
    // only the real entries.
    inline const juce::StringArray sourceChoices   { "Sample", "Saw Pad", "Voice", "Bell", "Noise", "Glass",
                                                     "Vocal Phrase", "Chord Stack", "Pluck", "Keys" };
    inline const juce::StringArray scanModeChoices { "Global", "Per Note" };
    inline const juce::StringArray scanLoopChoices { "Loop", "Ping-Pong", "Once" };
    inline const juce::StringArray syncRateChoices { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
    inline const juce::StringArray voiceModeChoices{ "Poly", "Mono", "Legato" };
    inline const juce::StringArray filterChoices   { "Low Pass", "Band Pass", "High Pass" };
    inline const juce::StringArray lfoShapeChoices { "Sine", "Triangle", "Saw", "Square", "Random" };
    inline const juce::StringArray modTargetChoices{ "Position", "Spray", "Size", "Density", "Pitch", "Cutoff", "Level" };
    inline const juce::StringArray quantizeChoices { "Off", "Octaves", "Fifths", "Major", "Minor" };
    inline const juce::StringArray lfoModeChoices  { "Free", "Sync" };
    inline const juce::StringArray lfoDivisionChoices { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16",
                                                        "1/4T", "1/8T", "1/4D" };

    inline constexpr int reservedSources = 32, reservedTargets = 16, reservedShapes = 8,
                         reservedQuantize = 12, reservedRates = 12, reservedDivisions = 16, reservedFilters = 8;
    inline constexpr const char* reservedChoiceName = "(reserved)";

    // Beats per grain for each syncRate choice.
    inline constexpr double syncRateBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };

    // Beats per LFO cycle for each lfoDivision choice.
    inline constexpr double lfoDivisionBeats[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.5 };

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Note name as shown (60 = C3) back to a MIDI note.
    int parseNote (const juce::String&);

    // "C3" style note name, Ableton convention (60 = C3).
    juce::String noteName (int midiNote);
}
