#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace thf
{
    // Series look: faceted white knobs with an arc, dark fader slots with white caps,
    // light rounded buttons that turn dark when on, Golos Text everywhere.
    class ThifoLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        // Font data comes from the plug-in's BinaryData (fonts are embedded per plug-in).
        ThifoLookAndFeel (const void* regularData, size_t regularSize,
                          const void* semiBoldData, size_t semiBoldSize);

        juce::Font font (float height, bool semiBold = false) const;

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                                   bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                           juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getLabelFont (juce::Label&) override;

        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

        void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;

        // Octagonal knob cap used by drawRotarySlider; public so plug-ins can draw the
        // same cap for display-only knobs (e.g. an endless main encoder).
        static void drawKnobCap (juce::Graphics&, juce::Rectangle<float> area, float pointerAngle);

    private:
        juce::Typeface::Ptr regular, semiBold;
    };
}
