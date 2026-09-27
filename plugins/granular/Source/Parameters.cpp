#include "Parameters.h"
#include "ControlLayout.h"

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

        // Text typed into a host's value field, in the units the parameter shows: "50 %",
        // "2 kHz", "1.5 s", "+3 st" all mean what they say.
        float parseValue (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            auto v = t.getFloatValue();
            if (t.contains ("%"))                                      v *= 0.01f;   // shown as value x 100
            else if (t.endsWith ("khz") || t.endsWith ("k"))           v *= 1000.0f;
            else if (t.endsWith (" s") || (t.endsWith ("s") && ! t.endsWith ("ms") && ! t.endsWith ("hz")))
                v *= 1000.0f;                                          // seconds into ms
            return v;
        }

        auto floatParam (const char* id, const char* name, Range range, float def,
                         std::function<juce::String (float)> toText,
                         std::function<float (const juce::String&)> fromText = parseValue)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID (id, version), name, range, def,
                Attributes().withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                            .withValueFromStringFunction (fromText));
        }

        auto percentParam (const char* id, const char* name, float def)
        {
            return floatParam (id, name, Range (0.0f, 1.0f), def,
                               [] (float v) { return juce::String (v * 100.0f, 1) + " %"; },
                               [] (const juce::String& s)
                               {
                                   const auto v = s.getFloatValue();
                                   return s.contains ("%") || std::abs (v) > 1.0f ? v * 0.01f : v;
                               });
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

    int parseNote (const juce::String& text)
    {
        // "C3" = 60 (as shown), "F#2", "Bb4", or a plain MIDI number.
        const auto t = text.trim().toUpperCase();
        if (t.isEmpty()) return 60;
        if (juce::CharacterFunctions::isDigit (t[0]) || t[0] == '-')
            return juce::jlimit (0, 127, t.getIntValue());
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        const auto letter = t[0];
        if (letter < 'A' || letter > 'G') return 60;
        int note = base[letter - 'A'];
        int i = 1;
        if (i < t.length() && t[i] == '#') { ++note; ++i; }
        else if (i < t.length() && (t[i] == 'B' || t[i] == 'b')) { --note; ++i; }
        const auto octave = t.substring (i).getIntValue();
        return juce::jlimit (0, 127, note + 12 * (octave + 2));
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        // Collected first, then added in the order hosts list them: the three encoder pages
        // (banks of 8 in Live), the faders, Position, then everything else.
        struct Collector
        {
            std::vector<std::unique_ptr<juce::RangedAudioParameter>> all;
            void add (std::unique_ptr<juce::RangedAudioParameter> p) { all.push_back (std::move (p)); }
        } layout;

        // Source and grains
        layout.add (choiceParam (pid::source, "Source", sourceChoices, 1, reservedSources));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID (pid::root, version), "Root", 0, 127, 60,
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                               .withValueFromStringFunction ([] (const juce::String& s) { return parseNote (s); })));
        layout.add (percentParam (pid::position, "Position", 0.25f));
        layout.add (percentParam (pid::regionStart, "Sample Start", 0.0f));
        layout.add (percentParam (pid::regionEnd, "Sample End", 1.0f));
        layout.add (boolParam (pid::normalize, "Normalize", true));
        layout.add (floatParam (pid::scan, "Scan", Range (-2.0f, 2.0f), 0.0f,
                                [] (float v) { return juce::String (v, 2) + "x"; }));
        layout.add (choiceParam (pid::scanMode, "Scan Mode", scanModeChoices, 0));
        layout.add (choiceParam (pid::scanLoop, "Scan Loop", scanLoopChoices, 0, 8));
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

        std::vector<std::string_view> order;
        for (const auto& page : layout::encoderParams)
            order.insert (order.end(), page.begin(), page.end());
        order.insert (order.end(), layout::faderParams.begin(), layout::faderParams.end());
        order.push_back (layout::mainEncoderParam);
        auto rank = [&order] (const juce::RangedAudioParameter& p)
        {
            const auto id = p.getParameterID().toStdString();
            const auto it = std::find (order.begin(), order.end(), std::string_view (id));
            return it != order.end() ? (int) (it - order.begin()) : (int) order.size();
        };
        std::stable_sort (layout.all.begin(), layout.all.end(),
                          [&rank] (const auto& a, const auto& b) { return rank (*a) < rank (*b); });

        juce::AudioProcessorValueTreeState::ParameterLayout result;
        result.add (layout.all.begin(), layout.all.end());
        return result;
    }
}
