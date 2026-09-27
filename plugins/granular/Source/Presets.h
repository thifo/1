#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace thf::grain
{
    class GrainProcessor;

    // Factory presets live in code: parameter values in real units; anything not listed is
    // the parameter default. Names describe the sound, never a brand or an artist.
    struct FactoryPreset
    {
        const char* name;
        const char* category;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<FactoryPreset>& factoryPresets();

    // Factory + user presets, previous/next browsing. Message thread only.
    // User presets are .thfgrain files (XML) in ~/Library/Audio/Presets/thf/thf Grain.
    class PresetManager
    {
    public:
        struct Entry
        {
            juce::String name, category;
            int factoryIndex = -1;          // >= 0 for factory presets
            juce::File file;                // user presets
        };

        explicit PresetManager (GrainProcessor&);

        std::vector<Entry> list() const;
        void load (const Entry&);
        void loadFactory (int index);
        bool loadFile (const juce::File&);
        bool saveUser (const juce::String& name);
        bool remove (const Entry&);
        void step (int delta);

        juce::String getCurrentName() const { return currentName; }
        void setCurrentName (const juce::String& n) { currentName = n; }

        static juce::File userFolder();
        static constexpr const char* extension = ".thfgrain";

        std::function<void()> onChange;

    private:
        // What a locked sample keeps across preset changes.
        struct SampleKeep
        {
            bool active = false;
            std::vector<std::pair<juce::String, float>> values;   // normalised
            std::array<float, 8> cues {};
        };
        SampleKeep captureLockedSample() const;
        void restoreLockedSample (const SampleKeep&);

        void resetToDefaults();
        GrainProcessor& processor;
        juce::String currentName { "Init" };
    };
}
