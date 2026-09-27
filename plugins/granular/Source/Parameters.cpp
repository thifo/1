#include "Parameters.h"

namespace thf::grain
{
    namespace
    {
        using Apvts = juce::AudioProcessorValueTreeState;
        using Range = juce::NormalisableRange<float>;
        using Attributes = juce::AudioParameterFloatAttributes;

        constexpr int version = 1;

        juce::String formatTime (float ms)
        {
            if (ms < 10.0f)    return juce::String (ms, 1) + " ms";
            if (ms < 1000.0f)  return juce::String (juce::roundToInt (ms)) + " ms";
            return juce::String (ms / 1000.0f, 2) + " s";
        }

        juce::String formatHz (float hz)
        {
            if (hz < 1.0f)     return juce::String (hz, 2) + " Hz";
            if (hz < 100.0f)   return juce::String (hz, 1) + " Hz";
            if (hz < 1000.0f)  return juce::String (juce::roundToInt (hz)) + " Hz";
            return juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
        }

        juce::String signedValue (float v, int decimals)
        {
            return (v > 0.0f ? "+" : "") + juce::String (v, decimals);
        }

        Range logRange (float lo, float hi, float centre, float interval = 0.0f)
        {
            Range r (lo, hi, interval);
            r.setSkewForCentre (centre);
            return r;
        }

        auto floatParam (const char* id, const char* name, Range range, float def,
                         std::function<juce::String (float)> toText)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID (id, version), name, range, def,
                Attributes().withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                            .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); }));
        }

        auto percentParam (const char* id, const char* name, float def)
        {
            return floatParam (id, name, Range (0.0f, 1.0f), def,
                               [] (float v) { return juce::String (v * 100.0f, 1) + " %"; });
        }

        auto choiceParam (const char* id, const char* name, const juce::StringArray& choices, int def, int reserveTo = 0)
        {
            auto list = choices;
            while (list.size() < reserveTo)
                list.add (reservedChoiceName);
            return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (id, version), name, list, def);
        }

        auto boolParam (const char* id, const char* name, bool def)
        {
            return std::make_unique<juce::AudioParameterBool> (juce::ParameterID (id, version), name, def);
        }
    }

    juce::String noteName (int midiNote)
    {
        return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // Source and grains
        layout.add (choiceParam (pid::source, "Source", sourceChoices, 1, reservedSources));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID (pid::root, version), "Root", 0, 127, 60,
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return noteName (v); })));
        layout.add (percentParam (pid::position, "Position", 0.25f));
        layout.add (percentParam (pid::regionStart, "Sample Start", 0.0f));
        layout.add (percentParam (pid::regionEnd, "Sample End", 1.0f));
        layout.add (boolParam (pid::normalize, "Normalize", true));
        layout.add (floatParam (pid::scan, "Scan", Range (-2.0f, 2.0f), 0.0f,
                                [] (float v) { return juce::String (v, 2) + "x"; }));
        layout.add (choiceParam (pid::scanMode, "Scan Mode", scanModeChoices, 0));
        layout.add (boolParam (pid::freeze, "Freeze", false));
        layout.add (floatParam (pid::spray, "Spray", logRange (0.0f, 1.0f, 0.1f), 0.04f,
                                [] (float v) { return juce::String (v * 100.0f, v < 0.1f ? 2 : 1) + " %"; }));
        layout.add (floatParam (pid::size, "Size", logRange (5.0f, 2000.0f, 120.0f), 120.0f, formatTime));
        layout.add (floatParam (pid::density, "Density", logRange (0.5f, 200.0f, 20.0f), 24.0f, formatHz));
        layout.add (boolParam (pid::sync, "Sync", false));
        layout.add (choiceParam (pid::syncRate, "Sync Rate", syncRateChoices, 3, reservedRates));
        layout.add (boolParam (pid::linkVoices, "Link Voices", false));
        layout.add (percentParam (pid::chaos, "Chaos", 0.3f));
        layout.add (percentParam (pid::window, "Window", 0.0f));
        layout.add (floatParam (pid::pitch, "Pitch", Range (-24.0f, 24.0f, 1.0f), 0.0f,
                                [] (float v) { return signedValue (v, 0) + " st"; }));
        layout.add (floatParam (pid::fine, "Fine", Range (-100.0f, 100.0f), 0.0f,
                                [] (float v) { return signedValue (v, 0) + " ct"; }));
        layout.add (floatParam (pid::jitter, "Pitch Jitter", logRange (0.0f, 24.0f, 2.0f), 0.0f,
                                [] (float v) { return juce::String (v, 2) + " st"; }));
        layout.add (percentParam (pid::reverse, "Reverse", 0.0f));
        layout.add (percentParam (pid::stereo, "Stereo", 0.5f));
        layout.add (choiceParam (pid::quantize, "Pitch Quantize", quantizeChoices, 0, reservedQuantize));

        // Voices
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID (pid::voices, version), "Voices", 1, 16, 8));
        layout.add (choiceParam (pid::voiceMode, "Voice Mode", voiceModeChoices, 0));
        layout.add (floatParam (pid::glide, "Glide", logRange (0.0f, 2000.0f, 150.0f), 0.0f, formatTime));
        layout.add (boolParam (pid::hold, "Hold", false));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID (pid::bendRange, version), "Bend Range", 0, 24, 2));
        layout.add (percentParam (pid::velocity, "Velocity", 0.6f));
        layout.add (floatParam (pid::attack,  "Attack",  logRange (0.5f, 10000.0f, 300.0f), 30.0f, formatTime));
        layout.add (floatParam (pid::decay,   "Decay",   logRange (1.0f, 10000.0f, 500.0f), 500.0f, formatTime));
        layout.add (percentParam (pid::sustain, "Sustain", 0.8f));
        layout.add (floatParam (pid::release, "Release", logRange (1.0f, 20000.0f, 800.0f), 800.0f, formatTime));

        // Tone
        layout.add (choiceParam (pid::filterType, "Filter Type", filterChoices, 0, reservedFilters));
        layout.add (floatParam (pid::cutoff, "Cutoff", logRange (20.0f, 20000.0f, 1000.0f), 20000.0f, formatHz));
        layout.add (percentParam (pid::resonance, "Resonance", 0.1f));
        layout.add (floatParam (pid::filterEnv, "Filter Env", Range (-1.0f, 1.0f), 0.0f,
                                [] (float v) { return signedValue (v * 100.0f, 0) + " %"; }));
        layout.add (floatParam (pid::filterDecay, "Filter Decay", logRange (5.0f, 5000.0f, 400.0f), 400.0f, formatTime));
        layout.add (percentParam (pid::drive, "Drive", 0.0f));

        // Modulation
        layout.add (floatParam (pid::lfoRate, "LFO Rate", logRange (0.01f, 20.0f, 1.0f), 0.5f, formatHz));
        layout.add (percentParam (pid::lfoDepth, "LFO Depth", 0.0f));
        layout.add (choiceParam (pid::lfoShape, "LFO Shape", lfoShapeChoices, 0, reservedShapes));
        layout.add (choiceParam (pid::lfoTarget, "LFO Target", modTargetChoices, 0, reservedTargets));
        layout.add (choiceParam (pid::modTarget, "Mod Target", modTargetChoices, 1, reservedTargets));
        layout.add (percentParam (pid::modDepth, "Mod Depth", 0.5f));
        layout.add (choiceParam (pid::lfoMode, "LFO Mode", lfoModeChoices, 0));
        layout.add (choiceParam (pid::lfoDivision, "LFO Division", lfoDivisionChoices, 4, reservedDivisions));

        // Space
        layout.add (percentParam (pid::space, "Space", 0.0f));
        layout.add (percentParam (pid::spaceSize, "Space Size", 0.6f));

        // Output
        layout.add (floatParam (pid::output, "Output", Range (-36.0f, 12.0f, 0.0f, 1.8f), 0.0f,
                                [] (float v) { return v <= -35.95f ? juce::String ("-inf dB") : signedValue (v, 1) + " dB"; }));
        layout.add (boolParam (pid::safeClip, "Safe Clip", true));
        layout.add (boolParam (pid::hq, "HQ", false));

        return layout;
    }
}
