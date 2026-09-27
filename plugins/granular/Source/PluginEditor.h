#pragma once

#include "PluginProcessor.h"
#include "ui/GrainViews.h"
#include "ui/WaveformView.h"

namespace thf::grain
{
    // Editor. Everything is laid out at a fixed design size inside `content`, which is scaled
    // as a whole, so the window resizes without re-flowing and the picture stays a mirror of
    // the controller: strips, display + main encoder, 2x4 encoders, 4 faders, 8 pads.
    class GrainEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer,
                        private juce::ChangeListener
    {
    public:
        static constexpr int designWidth = 1040;
        static constexpr int designHeight = 790;

        explicit GrainEditor (GrainProcessor&);
        ~GrainEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;

        // For tests and snapshots.
        juce::String getEncoderParam (int slot) const { return encoders[(size_t) slot]->getParamId(); }
        juce::Rectangle<int> getEncoderBounds (int slot) const { return encoders[(size_t) slot]->getBounds(); }
        juce::Rectangle<int> getFaderBounds (int slot) const { return faders[(size_t) slot]->getBounds(); }
        juce::Rectangle<int> getPadBounds (int slot) const { return pads[(size_t) slot]->getBounds(); }
        juce::Rectangle<int> getMainKnobBounds() const { return mainKnob.getBounds(); }
        void showSettings (bool);
        void showHelp (bool);
        void setPadBank (int bank);

    private:
        struct Content : juce::Component
        {
            explicit Content (GrainEditor& e) : editor (e) {}
            void paint (juce::Graphics& g) override { editor.paintContent (g); }
            GrainEditor& editor;
        };

        void paintContent (juce::Graphics&);
        void layoutContent();
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void applyPage();
        void refreshTexts();
        void refreshPads();
        void updateStatus();
        void focusParam (const juce::String& id);
        void showPresetMenu();
        void showMidiMenu();
        void chooseSample();
        void padClicked (int index, const juce::ModifierKeys&);

        GrainProcessor& processor;
        ThifoLookAndFeel lookAndFeel;
        UiContext ctx;
        Content content { *this };

        // Header
        juce::TextButton prevPreset { "<" }, nextPreset { ">" }, presetName, undoButton, redoButton,
                         langButton, learnButton, helpButton, moreButton;

        WaveformView waveform;
        StripView pitchStrip { true }, modStrip { false };
        DisplayScreen screen;
        ParamSlider mainKnob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mainKnobAttachment;
        std::array<std::unique_ptr<ControlKnob>, layout::numEncoders> encoders;
        std::array<std::unique_ptr<ControlFader>, layout::numFaders> faders;
        std::array<std::unique_ptr<PadButton>, layout::numPads> pads;
        juce::TextButton bankA { "A" }, bankB { "B" };
        MeterView meter;
        juce::TextButton hqButton { "HQ" }, clipButton, midiButton { "MIDI" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAttachment, clipAttachment;
        SettingsPanel settings;
        HelpPanel help;
        std::unique_ptr<juce::FileChooser> chooser;

        int shownPage = -1;
        int padBank = 0;
        bool learnMode = false;
        juce::String focusedParam;
        juce::uint32 focusTime = 0;
        int lastTouchSerial = -1, lastPadSerial = -1;
        juce::String lastLearning;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainEditor)
    };
}
