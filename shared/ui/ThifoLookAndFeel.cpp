#include "ThifoLookAndFeel.h"
#include "Palette.h"

namespace thf
{
    ThifoLookAndFeel::ThifoLookAndFeel (const void* regularData, size_t regularSize,
                                        const void* semiBoldData, size_t semiBoldSize)
    {
        regular  = juce::Typeface::createSystemTypefaceFor (regularData, regularSize);
        semiBold = juce::Typeface::createSystemTypefaceFor (semiBoldData, semiBoldSize);

        using namespace palette;
        setColour (juce::ResizableWindow::backgroundColourId, plate);
        setColour (juce::Label::textColourId, ink);
        setColour (juce::TextButton::buttonColourId, plateRaised);
        setColour (juce::TextButton::buttonOnColourId, buttonOn);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, buttonOnText);
        setColour (juce::ComboBox::backgroundColourId, plateRaised);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::outlineColourId, plateEdge);
        setColour (juce::ComboBox::arrowColourId, inkDim);
        setColour (juce::PopupMenu::backgroundColourId, plateRaised);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::headerTextColourId, inkDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, buttonOn);
        setColour (juce::PopupMenu::highlightedTextColourId, buttonOnText);
        setColour (juce::Slider::rotarySliderFillColourId, knobArc);
        setColour (juce::Slider::rotarySliderOutlineColourId, knobTrack);
        setColour (juce::Slider::thumbColourId, accentBlue);
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, glass);
        setColour (juce::TooltipWindow::textColourId, glassText);
        setColour (juce::TextEditor::backgroundColourId, plateRaised);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::outlineColourId, plateEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, knobArc);
        setColour (juce::AlertWindow::backgroundColourId, plateRaised);
        setColour (juce::AlertWindow::textColourId, ink);
    }

    juce::Font ThifoLookAndFeel::font (float height, bool bold) const
    {
        return juce::Font (juce::FontOptions (bold ? semiBold : regular).withHeight (height));
    }

    juce::Typeface::Ptr ThifoLookAndFeel::getTypefaceForFont (const juce::Font& f)
    {
        if (f.isBold() && semiBold != nullptr)
            return semiBold;
        if (regular != nullptr)
            return regular;
        return LookAndFeel_V4::getTypefaceForFont (f);
    }

    void ThifoLookAndFeel::drawKnobCap (juce::Graphics& g, juce::Rectangle<float> area, float angle)
    {
        using namespace palette;
        const auto centre = area.getCentre();
        const auto radius = area.getWidth() * 0.5f;

        auto octagon = [&] (float r)
        {
            juce::Path p;
            for (int i = 0; i < 8; ++i)
            {
                const auto a = juce::MathConstants<float>::pi * (0.125f + 0.25f * (float) i);
                const auto pt = centre + juce::Point<float> (std::cos (a), std::sin (a)) * r;
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            p.closeSubPath();
            return p;
        };

        // Soft drop shadow under the cap.
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.fillEllipse (area.translated (0.0f, radius * 0.06f).reduced (radius * 0.02f));

        const auto outer = octagon (radius);
        g.setGradientFill (juce::ColourGradient (knobFace, centre.x, area.getY(),
                                                 knobFacet, centre.x, area.getBottom(), false));
        g.fillPath (outer);
        g.setColour (plateEdge);
        g.strokePath (outer, juce::PathStrokeType (1.0f));

        // Flat top facet.
        const auto inner = octagon (radius * 0.78f);
        g.setGradientFill (juce::ColourGradient (knobFacet.brighter (0.4f), centre.x, area.getY(),
                                                 knobFace, centre.x, area.getBottom(), false));
        g.fillPath (inner);
        g.setColour (plateEdge.withAlpha (0.7f));
        g.strokePath (inner, juce::PathStrokeType (0.8f));

        // Pointer.
        const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
        g.setColour (knobPointer);
        g.drawLine ({ centre + dir * (radius * 0.18f), centre + dir * (radius * 0.66f) },
                    juce::jmax (1.6f, radius * 0.1f));
    }

    void ThifoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                             float startAngle, float endAngle, juce::Slider& s)
    {
        using namespace palette;
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto area = bounds.withSizeKeepingCentre (side, side);
        const auto centre = area.getCentre();
        const auto radius = side * 0.5f;
        const auto ringWidth = juce::jmax (2.0f, radius * 0.09f);
        const auto ringRadius = radius - ringWidth * 0.5f;
        const auto angle = startAngle + pos * (endAngle - startAngle);
        const bool enabled = s.isEnabled();

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (s.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // The arc starts at the "arcOrigin" component property (0..1 of the travel, e.g. the
        // default of a bipolar parameter), otherwise at the start of the travel.
        const auto origin = (float) (double) s.getProperties().getWithDefault ("arcOrigin", 0.0);
        const auto from = startAngle + juce::jlimit (0.0f, 1.0f, origin) * (endAngle - startAngle);
        if (std::abs (angle - from) > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                               juce::jmin (from, angle), juce::jmax (from, angle), true);
            auto arcColour = s.findColour (juce::Slider::rotarySliderFillColourId);
            g.setColour (enabled ? arcColour : arcColour.withAlpha (0.35f));
            g.strokePath (arc, juce::PathStrokeType (ringWidth, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        const auto cap = area.reduced (ringWidth * 2.4f);
        if (! enabled)
            g.setOpacity (0.5f);
        drawKnobCap (g, cap, angle);
    }

    void ThifoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                             float, float, juce::Slider::SliderStyle style, juce::Slider& s)
    {
        using namespace palette;
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }

        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const auto capH = juce::jmin (22.0f, bounds.getWidth() * 0.5f);
        const auto capW = juce::jmin (bounds.getWidth() - 4.0f, 40.0f);
        const auto slot = juce::Rectangle<float> (7.0f, bounds.getHeight() + capH * 0.6f)
                              .withCentre (bounds.getCentre());

        g.setColour (faderSlot);
        g.fillRoundedRectangle (slot, 3.5f);

        const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ bounds.getCentreX(), pos });
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 4.0f);
        g.setGradientFill (juce::ColourGradient (knobFace, cap.getX(), cap.getY(),
                                                 knobFacet, cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (plateEdge);
        g.drawRoundedRectangle (cap, 4.0f, 1.0f);
        g.setColour (s.findColour (juce::Slider::thumbColourId));
        g.fillRoundedRectangle (cap.withSizeKeepingCentre (capW * 0.62f, 2.6f), 1.3f);
    }

    void ThifoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                 bool highlighted, bool down)
    {
        using namespace palette;
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId)
                       : b.findColour (juce::TextButton::buttonColourId);
        if (down)             fill = fill.darker (0.08f);
        else if (highlighted) fill = on ? fill.brighter (0.12f) : fill.darker (0.03f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? fill : plateEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    juce::Font ThifoLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return font (juce::jmin (15.0f, (float) buttonHeight * 0.52f), true);
    }

    void ThifoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        const auto colour = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                             : juce::TextButton::textColourOffId);
        g.setColour (b.isEnabled() ? colour : colour.withAlpha (0.4f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }

    void ThifoLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
    {
        using namespace palette;
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        juce::Path arrow;
        const auto cx = (float) w - 12.0f, cy = (float) h * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    juce::Font ThifoLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        return font (juce::jmin (15.0f, (float) box.getHeight() * 0.5f));
    }

    juce::Font ThifoLookAndFeel::getPopupMenuFont() { return font (15.0f); }

    juce::Font ThifoLookAndFeel::getLabelFont (juce::Label& l)
    {
        return font (l.getFont().getHeight(), l.getFont().isBold());
    }

    void ThifoLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (palette::plateEdge);
        g.drawRect (0, 0, w, h);
    }

    void ThifoLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
    {
        g.fillAll (palette::glass);
        g.setColour (palette::glassText);
        g.setFont (font (14.0f));
        g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 2), juce::Justification::centredLeft, 3);
    }
}
