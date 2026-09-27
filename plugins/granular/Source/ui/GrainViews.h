#pragma once

#include "../PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <ui/ThifoLookAndFeel.h>
#include <ui/Palette.h>

namespace thf::grain
{
    // Shared context for the controls: the processor plus a callback that reports which
    // parameter the user is pointing at (for the status line).
    struct UiContext
    {
        GrainProcessor& processor;
        ThifoLookAndFeel& lookAndFeel;
        std::function<void (const juce::String& paramId)> onFocus;
        std::function<bool()> isLearnMode;
    };

    // Right-click menu shared by knobs and faders: learn / forget CC, default value.
    void showParameterMenu (UiContext&, juce::Component& target, const juce::String& paramId);

    // Slider that reports focus, opens the parameter menu on right-click and, in learn mode,
    // arms MIDI learn instead of moving.
    class ParamSlider : public juce::Slider
    {
    public:
        ParamSlider (UiContext& c) : ctx (c) {}
        juce::String paramId;

        void mouseEnter (const juce::MouseEvent& e) override
        {
            if (ctx.onFocus) ctx.onFocus (paramId);
            Slider::mouseEnter (e);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (ctx.onFocus) ctx.onFocus (paramId);
            if (e.mods.isPopupMenu()) { showParameterMenu (ctx, *this, paramId); return; }
            if (ctx.isLearnMode && ctx.isLearnMode()) { ctx.processor.startLearn (paramId); return; }
            ctx.processor.getUndoManager().beginNewTransaction();
            Slider::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || (ctx.isLearnMode && ctx.isLearnMode())) return;
            Slider::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || (ctx.isLearnMode && ctx.isLearnMode())) return;
            Slider::mouseUp (e);
        }

    private:
        UiContext& ctx;
    };

    //==============================================================================
    // Encoder slot: slot number, knob, parameter name, value. Re-attachable (pages).
    class ControlKnob : public juce::Component
    {
    public:
        ControlKnob (UiContext&, int slotNumber, float knobSize);

        void attach (const juce::String& paramId, juce::Colour arc);
        void setCompact (bool c) { compact = c; resized(); }
        const juce::String& getParamId() const noexcept { return paramId; }
        void refresh();

        void resized() override;
        void paint (juce::Graphics&) override;

        ParamSlider slider;

    private:
        UiContext& ctx;
        int slot;
        float knobSize;
        bool compact = false;
        juce::String paramId;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Label name, value;
    };

    //==============================================================================
    class ControlFader : public juce::Component
    {
    public:
        ControlFader (UiContext&, int slotNumber, const juce::String& paramId, juce::Colour capColour);
        void refresh();
        // Pickup: while the hardware fader has not reached the value yet, its position is
        // drawn as a hollow cap. Call from a timer.
        void refreshPickup();
        void resized() override;
        void paint (juce::Graphics&) override;
        ParamSlider slider;

    private:
        UiContext& ctx;
        int slot;
        juce::String paramId;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Label name, value;
        GrainProcessor::PickupState pickup;
    };

    //==============================================================================
    class PadButton : public juce::Component
    {
    public:
        PadButton (juce::Colour pastel);
        void setLabel (const juce::String& l)     { if (label != l) { label = l; repaint(); } }
        void setLit (bool lit);
        void setSubLabel (const juce::String& s)  { if (sub != s) { sub = s; repaint(); } }
        void flash();
        void setColour (juce::Colour c)           { pastel = c; repaint(); }

        // Press and release, like the hardware pad; right-click opens onMenu instead.
        std::function<void (bool down)> onPress;
        std::function<void()> onMenu;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Colour pastel;
        juce::String label, sub;
        bool lit = false, down = false;
        juce::uint32 flashTime = 0;
    };

    //==============================================================================
    // Mirror of a hardware touch strip. Pitch springs back to the centre.
    class StripView : public juce::Component
    {
    public:
        StripView (bool isPitch);
        void setValue (float v) { if (std::abs (v - value) > 1.0e-3f) { value = v; repaint(); } }
        void paint (juce::Graphics&) override;
        std::function<void (float)> onUserChange;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        bool pitch;
        float value;
    };

    //==============================================================================
    // The controller's 25 keys on screen, also playable from the computer keyboard like in
    // Ableton: A-K white keys, W E T Y U black keys, Z/X octave, C/V velocity.
    class PlayKeyboard : public juce::MidiKeyboardComponent
    {
    public:
        static constexpr int numKeys = 25;
        static constexpr int midiChannel = 16;   // keeps these notes apart from the controller's

        explicit PlayKeyboard (juce::MidiKeyboardState&);

        int getLowestNote() const noexcept     { return lowest; }
        int getVelocity() const noexcept       { return velocity; }
        void shiftOctave (int delta);
        void changeVelocity (int delta);
        void setPadChannel (int channel);
        std::function<void()> onSettingsChanged;
        std::function<void (int note)> onRootPick;      // Alt-click on a key

        bool keyPressed (const juce::KeyPress&) override;
        bool mouseDownOnKey (int midiNoteNumber, const juce::MouseEvent&) override;
        void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float>, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override;
        void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float>, bool isDown, bool isOver,
                            juce::Colour noteFillColour) override;

    private:
        void applyRange();
        int lowest = 48;       // C2..C4, as the controller starts
        int velocity = 100;
    };

    //==============================================================================
    class MeterView : public juce::Component
    {
    public:
        void push (float left, float right);
        void paint (juce::Graphics&) override;
    private:
        float levelL = 0, levelR = 0, holdL = 0, holdR = 0;
        int holdCount = 0;
    };

    //==============================================================================
    // Small glass screen above the main encoder: page and the focused parameter.
    class DisplayScreen : public juce::Component
    {
    public:
        explicit DisplayScreen (ThifoLookAndFeel& l) : lookAndFeel (l) {}
        void setPage (const juce::String& name, int index, int count, juce::Colour colour);
        void setStatus (const juce::String& param, const juce::String& value, const juce::String& cc);
        void paint (juce::Graphics&) override;
        std::function<void()> onPageClick;
        void mouseDown (const juce::MouseEvent&) override { if (onPageClick) onPageClick(); }

    private:
        ThifoLookAndFeel& lookAndFeel;
        juce::String page, statusParam, statusValue, statusCc;
        int pageIndex = 0, pageCount = 1;
        juce::Colour pageColour;
    };

    //==============================================================================
    // Overlay with the parameters that have no hardware slot (mouse only).
    class SettingsPanel : public juce::Component
    {
    public:
        explicit SettingsPanel (UiContext&);
        void resized() override;
        void paint (juce::Graphics&) override;
        void refreshTexts();

    private:
        UiContext& ctx;
        struct Choice
        {
            juce::String paramId;
            juce::Label label;
            juce::ComboBox box;
            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        };
        // Groups shown as columns: a title, choices on top, knobs below.
        struct Group
        {
            juce::String title;
            std::vector<Choice*> choices;
            std::vector<ControlKnob*> knobs;
            juce::Rectangle<int> bounds;
        };
        std::vector<std::unique_ptr<ControlKnob>> knobs;
        std::vector<std::unique_ptr<Choice>> choices;
        std::vector<Group> groups;
        void addChoice (Group&, const char* id);
        void addKnob (Group&, const char* id);
    };

    //==============================================================================
    class HelpPanel : public juce::Component
    {
    public:
        explicit HelpPanel (ThifoLookAndFeel& l) : lookAndFeel (l) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
    private:
        ThifoLookAndFeel& lookAndFeel;
    };
}
