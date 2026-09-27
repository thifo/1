#include "Presets.h"
#include "PluginProcessor.h"

namespace thf::grain
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        // Source choices: 0 Sample, 1 Saw Pad, 2 Voice, 3 Bell, 4 Noise, 5 Glass.
        static const std::vector<FactoryPreset> list {
            { "Init", "Basic", {} },

            { "Slow Tide", "Pads", {
                { pid::source, 1 }, { pid::position, 0.2f }, { pid::scan, 0.05f }, { pid::spray, 0.08f },
                { pid::size, 350 }, { pid::density, 18 }, { pid::chaos, 0.5f }, { pid::stereo, 0.8f },
                { pid::attack, 1200 }, { pid::decay, 2000 }, { pid::sustain, 0.9f }, { pid::release, 3500 },
                { pid::cutoff, 6000 }, { pid::lfoRate, 0.08f }, { pid::lfoDepth, 0.3f }, { pid::lfoTarget, 0 } } },

            { "Glass Hall", "Pads", {
                { pid::source, 5 }, { pid::position, 0.3f }, { pid::spray, 0.3f }, { pid::size, 600 },
                { pid::density, 12 }, { pid::jitter, 0.1f }, { pid::stereo, 1.0f }, { pid::reverse, 0.3f },
                { pid::attack, 900 }, { pid::sustain, 0.85f }, { pid::release, 5000 } } },

            { "Low Drone", "Pads", {
                { pid::source, 1 }, { pid::pitch, -12 }, { pid::size, 500 }, { pid::density, 15 },
                { pid::spray, 0.15f }, { pid::cutoff, 1200 }, { pid::resonance, 0.3f }, { pid::drive, 0.3f },
                { pid::voiceMode, 1 }, { pid::glide, 300 }, { pid::attack, 800 }, { pid::release, 2500 } } },

            { "Wide Shimmer", "Pads", {
                { pid::source, 5 }, { pid::pitch, 12 }, { pid::jitter, 0.3f }, { pid::size, 300 },
                { pid::density, 30 }, { pid::spray, 0.5f }, { pid::stereo, 1.0f }, { pid::chaos, 0.7f },
                { pid::attack, 1500 }, { pid::release, 4000 }, { pid::output, -3 } } },

            { "Choir of Dust", "Textures", {
                { pid::source, 2 }, { pid::position, 0.5f }, { pid::spray, 0.5f }, { pid::size, 180 },
                { pid::density, 40 }, { pid::chaos, 0.8f }, { pid::jitter, 0.15f }, { pid::stereo, 0.9f },
                { pid::attack, 600 }, { pid::release, 2500 }, { pid::cutoff, 5000 }, { pid::resonance, 0.2f } } },

            { "Frozen Breath", "Textures", {
                { pid::source, 2 }, { pid::freeze, 1 }, { pid::position, 0.35f }, { pid::spray, 0.04f },
                { pid::size, 90 }, { pid::density, 60 }, { pid::stereo, 0.7f }, { pid::window, 0.5f },
                { pid::attack, 200 }, { pid::release, 1500 } } },

            { "Reverse Mist", "Textures", {
                { pid::source, 5 }, { pid::reverse, 1.0f }, { pid::size, 400 }, { pid::density, 20 },
                { pid::spray, 0.4f }, { pid::stereo, 0.9f }, { pid::attack, 700 }, { pid::release, 3000 },
                { pid::cutoff, 7000 } } },

            { "Bell Garden", "Keys", {
                { pid::source, 3 }, { pid::position, 0.02f }, { pid::spray, 0.01f }, { pid::size, 250 },
                { pid::density, 30 }, { pid::chaos, 0.2f }, { pid::attack, 2 }, { pid::decay, 1500 },
                { pid::sustain, 0.3f }, { pid::release, 1500 }, { pid::stereo, 0.6f } } },

            { "Lead Glide", "Keys", {
                { pid::source, 2 }, { pid::voiceMode, 2 }, { pid::glide, 120 }, { pid::size, 70 },
                { pid::density, 80 }, { pid::chaos, 0.1f }, { pid::spray, 0.02f }, { pid::attack, 5 },
                { pid::sustain, 0.9f }, { pid::release, 300 }, { pid::stereo, 0.3f } } },

            { "Tape Scrub", "Motion", {
                { pid::source, 1 }, { pid::scan, 0.5f }, { pid::spray, 0.01f }, { pid::size, 60 },
                { pid::density, 50 }, { pid::chaos, 0.1f }, { pid::stereo, 0.4f }, { pid::release, 600 } } },

            { "Crushed Motion", "Motion", {
                { pid::source, 1 }, { pid::drive, 0.7f }, { pid::density, 90 }, { pid::size, 25 },
                { pid::chaos, 0.6f }, { pid::spray, 0.1f }, { pid::scan, -0.3f }, { pid::output, -4 } } },

            { "Grain Pulse", "Rhythmic", {
                { pid::source, 1 }, { pid::sync, 1 }, { pid::syncRate, 3 }, { pid::window, 1.0f },
                { pid::size, 80 }, { pid::chaos, 0.0f }, { pid::spray, 0.02f }, { pid::filterEnv, 0.4f },
                { pid::filterDecay, 150 }, { pid::cutoff, 800 }, { pid::resonance, 0.5f } } },

            { "Stutter Voice", "Rhythmic", {
                { pid::source, 2 }, { pid::sync, 1 }, { pid::syncRate, 5 }, { pid::chaos, 0.0f },
                { pid::size, 40 }, { pid::window, 0.6f }, { pid::spray, 0.2f }, { pid::stereo, 0.6f } } },

            { "Wind Tunnel", "FX", {
                { pid::source, 4 }, { pid::scan, 0.2f }, { pid::spray, 0.2f }, { pid::size, 200 },
                { pid::density, 40 }, { pid::filterType, 1 }, { pid::cutoff, 1500 }, { pid::resonance, 0.6f },
                { pid::lfoTarget, 5 }, { pid::lfoRate, 0.15f }, { pid::lfoDepth, 0.5f }, { pid::attack, 1000 },
                { pid::release, 3000 } } },

            { "Starfield", "FX", {
                { pid::source, 3 }, { pid::spray, 1.0f }, { pid::size, 40 }, { pid::density, 25 },
                { pid::chaos, 1.0f }, { pid::jitter, 12 }, { pid::stereo, 1.0f }, { pid::reverse, 0.5f },
                { pid::release, 4000 } } },
        };
        return list;
    }

    //==============================================================================
    PresetManager::PresetManager (GrainProcessor& p) : processor (p) {}

    juce::File PresetManager::userFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Presets/thf/thf Grain");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("thf/thf Grain/Presets");
       #endif
    }

    std::vector<PresetManager::Entry> PresetManager::list() const
    {
        std::vector<Entry> entries;
        const auto& factory = factoryPresets();
        for (size_t i = 0; i < factory.size(); ++i)
            entries.push_back ({ factory[i].name, factory[i].category, (int) i, {} });

        auto files = userFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
        for (auto& f : files)
            entries.push_back ({ f.getFileNameWithoutExtension(), "User", -1, f });
        return entries;
    }

    void PresetManager::resetToDefaults()
    {
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
    }

    void PresetManager::loadFactory (int index)
    {
        const auto& factory = factoryPresets();
        if (index < 0 || index >= (int) factory.size())
            return;
        processor.getUndoManager().beginNewTransaction();
        resetToDefaults();
        for (const auto& [id, value] : factory[(size_t) index].values)
            if (auto* p = processor.param (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        for (int i = 0; i < 8; ++i)
            processor.setCue (i, -1.0f);
        processor.getUndoManager().beginNewTransaction();
        currentName = factory[(size_t) index].name;
        if (onChange) onChange();
    }

    bool PresetManager::loadFile (const juce::File& file)
    {
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("thfGrainPreset"))
            return false;

        processor.getUndoManager().beginNewTransaction();
        resetToDefaults();
        for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
            if (auto* p = processor.param (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));

        if (auto* extra = xml->getChildByName ("Extra"))
        {
            // Keep the current sample if the preset does not carry one.
            auto tree = juce::ValueTree::fromXml (*extra);
            if (! tree.getChildWithName ("Sample").isValid())
            {
                if (auto current = processor.getUserSample())
                {
                    const auto keep = processor.saveExtraState (false).getChildWithName ("Sample");
                    if (keep.isValid())
                        tree.addChild (keep.createCopy(), -1, nullptr);
                }
            }
            processor.restoreExtraState (tree, false);
        }
        processor.getUndoManager().beginNewTransaction();
        currentName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        if (onChange) onChange();
        return true;
    }

    void PresetManager::load (const Entry& e)
    {
        if (e.factoryIndex >= 0) loadFactory (e.factoryIndex);
        else                     loadFile (e.file);
    }

    bool PresetManager::saveUser (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty())
            return false;
        auto folder = userFolder();
        if (! folder.createDirectory())
            return false;

        juce::XmlElement xml ("thfGrainPreset");
        xml.setAttribute ("name", name.trim());
        xml.setAttribute ("version", GrainProcessor::stateVersion);
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", ranged->getParameterID());
                e->setAttribute ("value", (double) ranged->convertFrom0to1 (ranged->getValue()));
            }
        auto extra = processor.saveExtraState (false);
        if (auto sample = extra.getChildWithName ("Sample"); sample.isValid())
            if (const auto* block = sample.getProperty ("flac").getBinaryData())
                sample.setProperty ("flac", block->toBase64Encoding(), nullptr);
        if (auto extraXml = extra.createXml())
            xml.addChildElement (extraXml.release());

        const auto file = folder.getChildFile (clean + extension);
        if (! xml.writeTo (file))
            return false;
        currentName = name.trim();
        if (onChange) onChange();
        return true;
    }

    bool PresetManager::remove (const Entry& e)
    {
        return e.factoryIndex < 0 && e.file.existsAsFile() && e.file.deleteFile();
    }

    void PresetManager::step (int delta)
    {
        const auto entries = list();
        if (entries.empty())
            return;
        int current = -1;
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == currentName) { current = (int) i; break; }
        const auto next = ((current < 0 ? 0 : current + delta) % (int) entries.size() + (int) entries.size())
                          % (int) entries.size();
        load (entries[(size_t) next]);
    }
}
