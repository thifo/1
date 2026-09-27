#include "GrainViews.h"
#include <i18n/Translator.h>

namespace thf::grain
{
    using namespace thf::palette;

    namespace
    {
        void styleLabel (juce::Label& l, ThifoLookAndFeel& laf, float height, bool bold, juce::Colour colour)
        {
            l.setFont (laf.font (height, bold));
            l.setColour (juce::Label::textColourId, colour);
            l.setJustificationType (juce::Justification::centred);
            l.setInterceptsMouseClicks (false, false);
            l.setMinimumHorizontalScale (0.7f);
        }
    }

    void showParameterMenu (UiContext& ctx, juce::Component& target, const juce::String& paramId)
    {
        auto& proc = ctx.processor;
        auto* p = proc.param (paramId);
        if (p == nullptr)
            return;

        juce::PopupMenu menu;
        const auto cc = proc.getCcFor (paramId);
        menu.addSectionHeader (p->getName (64) + (cc >= 0 ? sep() + "CC " + juce::String (cc) : juce::String()));
        menu.addItem (1, tr ("MIDI Learn"));
        menu.addItem (2, tr ("Forget learned CC"), proc.isLearned (paramId));
        menu.addSeparator();
        menu.addItem (3, tr ("Reset to default"));

        juce::Component::SafePointer<juce::Component> safe (&target);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                            [&proc, paramId, p, safe] (int result)
                            {
                                if (result == 1) proc.startLearn (paramId);
                                if (result == 2) proc.clearLearned (paramId);
                                if (result == 3)
                                {
                                    proc.getUndoManager().beginNewTransaction();
                                    p->beginChangeGesture();
                                    p->setValueNotifyingHost (p->getDefaultValue());
                                    p->endChangeGesture();
                                }
                            });
    }

    //==============================================================================
    ControlKnob::ControlKnob (UiContext& c, int slotNumber, float size)
        : slider (c), ctx (c), slot (slotNumber), knobSize (size)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setVelocityBasedMode (false);
        slider.onValueChange = [this] { refresh(); if (ctx.onFocus) ctx.onFocus (paramId); };
        addAndMakeVisible (slider);

        styleLabel (name, c.lookAndFeel, 14.0f, true, ink);
        styleLabel (value, c.lookAndFeel, 15.0f, true, ink);
        addAndMakeVisible (name);
        addAndMakeVisible (value);
    }

    void ControlKnob::attach (const juce::String& id, juce::Colour arc)
    {
        slider.setColour (juce::Slider::rotarySliderFillColourId, arc);
        if (id == paramId)
            return;
        attachment.reset();
        paramId = id;
        slider.paramId = id;
        auto* p = ctx.processor.param (id);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.processor.getState(), id, slider);
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        const auto range = p->getNormalisableRange();
        // Parameters that go below and above zero draw their arc from the default.
        slider.getProperties().set ("arcOrigin", range.start < 0.0f && range.end > 0.0f ? p->getDefaultValue() : 0.0f);
        name.setText (p->getName (32), juce::dontSendNotification);
        refresh();
        repaint();
    }

    void ControlKnob::refresh()
    {
        if (auto* p = ctx.processor.param (paramId))
            value.setText (p->getCurrentValueAsText(), juce::dontSendNotification);
    }

    void ControlKnob::resized()
    {
        auto r = getLocalBounds();
        const auto nameH = compact ? 16 : 20;
        name.setBounds (r.removeFromTop (nameH));
        name.setFont (ctx.lookAndFeel.font (compact ? 12.5f : 14.0f, true));
        value.setFont (ctx.lookAndFeel.font (compact ? 12.5f : 15.0f, true));
        auto numberArea = compact ? juce::Rectangle<int>() : r.removeFromBottom (14);
        juce::ignoreUnused (numberArea);
        value.setBounds (r.removeFromBottom (compact ? 16 : 20));
        const auto side = juce::jmin ((int) knobSize, r.getWidth(), r.getHeight());
        slider.setBounds (r.withSizeKeepingCentre (side, side));
    }

    void ControlKnob::paint (juce::Graphics& g)
    {
        if (compact || slot <= 0)
            return;
        g.setColour (inkFaint);
        g.setFont (ctx.lookAndFeel.font (11.0f));
        g.drawText (juce::String (slot), getLocalBounds().removeFromBottom (14), juce::Justification::centred);

        const bool learning = ctx.processor.getLearningParam() == paramId && paramId.isNotEmpty();
        if (learning)
        {
            g.setColour (knobArc.withAlpha (0.18f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        }
    }

    //==============================================================================
    ControlFader::ControlFader (UiContext& c, int slotNumber, const juce::String& id, juce::Colour capColour)
        : slider (c), ctx (c), slot (slotNumber), paramId (id)
    {
        slider.paramId = id;
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::thumbColourId, capColour);
        slider.onValueChange = [this] { refresh(); if (ctx.onFocus) ctx.onFocus (paramId); };
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (c.processor.getState(), id, slider);
        auto* p = c.processor.param (id);
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

        styleLabel (name, c.lookAndFeel, 14.0f, true, ink);
        styleLabel (value, c.lookAndFeel, 15.0f, true, ink);
        name.setText (p->getName (32), juce::dontSendNotification);
        addAndMakeVisible (name);
        addAndMakeVisible (value);
        refresh();
    }

    void ControlFader::refresh()
    {
        if (auto* p = ctx.processor.param (paramId))
            value.setText (p->getCurrentValueAsText(), juce::dontSendNotification);
    }

    void ControlFader::resized()
    {
        auto r = getLocalBounds();
        r.removeFromBottom (14);
        value.setBounds (r.removeFromBottom (20));
        name.setBounds (r.removeFromBottom (20));
        slider.setBounds (r.reduced (0, 6));
    }

    void ControlFader::paint (juce::Graphics& g)
    {
        g.setColour (inkFaint);
        g.setFont (ctx.lookAndFeel.font (11.0f));
        g.drawText (juce::String (slot), getLocalBounds().removeFromBottom (14), juce::Justification::centred);
        if (ctx.processor.getLearningParam() == paramId)
        {
            g.setColour (knobArc.withAlpha (0.18f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        }
    }

    //==============================================================================
    PadButton::PadButton (juce::Colour c) : pastel (c) {}

    void PadButton::setLit (bool l)
    {
        if (l != lit) { lit = l; repaint(); }
    }

    void PadButton::flash()
    {
        flashTime = juce::Time::getMillisecondCounter();
        repaint();
    }

    void PadButton::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        const auto sinceFlash = juce::Time::getMillisecondCounter() - flashTime;
        const bool flashing = sinceFlash < 160;

        auto face = lit ? padLit (pastel) : pastel;
        if (down || flashing) face = face.darker (0.08f);

        g.setColour (juce::Colours::black.withAlpha (0.08f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 7.0f);
        g.setColour (face);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (lit ? padLit (pastel).darker (0.25f) : plateEdge);
        g.drawRoundedRectangle (r, 7.0f, lit ? 2.0f : 1.0f);

        g.setColour (ink);
        auto* laf = dynamic_cast<ThifoLookAndFeel*> (&getLookAndFeel());
        const auto textArea = r.toNearestInt();
        if (laf != nullptr) g.setFont (laf->font (16.0f, true));
        g.drawFittedText (label, textArea.withTrimmedBottom (sub.isNotEmpty() ? 14 : 0), juce::Justification::centred, 1);
        if (sub.isNotEmpty())
        {
            if (laf != nullptr) g.setFont (laf->font (12.0f));
            g.setColour (inkDim);
            g.drawFittedText (sub, textArea.withTrimmedTop (textArea.getHeight() / 2 + 6), juce::Justification::centred, 1);
        }
    }

    void PadButton::mouseDown (const juce::MouseEvent& e)
    {
        down = true;
        repaint();
        if (onClick) onClick (e.mods);
    }

    void PadButton::mouseUp (const juce::MouseEvent&)
    {
        down = false;
        repaint();
    }

    //==============================================================================
    StripView::StripView (bool isPitch) : pitch (isPitch), value (isPitch ? 0.5f : 0.0f) {}

    void StripView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (plateRaised);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (plateEdge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        auto inner = r.reduced (4.0f, 6.0f);
        if (pitch)
        {
            const auto centre = inner.getCentreY();
            const auto y = inner.getBottom() - value * inner.getHeight();
            g.setColour (plateEdge);
            g.drawHorizontalLine ((int) centre, inner.getX(), inner.getRight());
            g.setColour (accentBlue.withAlpha (0.8f));
            g.fillRoundedRectangle (juce::Rectangle<float> (inner.getX(), juce::jmin (y, centre), inner.getWidth(),
                                                            std::abs (y - centre) + 1.0f), 3.0f);
        }
        else
        {
            const auto h = value * inner.getHeight();
            g.setColour (accentTeal.withAlpha (0.8f));
            g.fillRoundedRectangle (inner.withTop (inner.getBottom() - h), 3.0f);
        }
    }

    void StripView::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }

    void StripView::mouseDrag (const juce::MouseEvent& e)
    {
        const auto inner = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
        const auto v = juce::jlimit (0.0f, 1.0f, (inner.getBottom() - (float) e.y) / inner.getHeight());
        setValue (v);
        if (onUserChange) onUserChange (v);
    }

    void StripView::mouseUp (const juce::MouseEvent&)
    {
        if (pitch)
        {
            setValue (0.5f);
            if (onUserChange) onUserChange (0.5f);
        }
    }

    //==============================================================================
    PlayKeyboard::PlayKeyboard (juce::MidiKeyboardState& s)
        : juce::MidiKeyboardComponent (s, juce::KeyboardComponentBase::horizontalKeyboard)
    {
        setMidiChannel (midiChannel);
        setScrollButtonsVisible (false);
        setOctaveForMiddleC (3);             // 60 = C3, as in Ableton
        setBlackNoteLengthProportion (0.6f);
        setWantsKeyboardFocus (true);
        setColour (keyDownOverlayColourId, accentBlue.withAlpha (0.75f));
        setColour (mouseOverKeyOverlayColourId, accentBlue.withAlpha (0.15f));
        setPadChannel (layout::minilab3::padChannel);
        applyRange();
    }

    void PlayKeyboard::applyRange()
    {
        setAvailableRange (lowest, lowest + numKeys - 1);
        setKeyPressBaseOctave (lowest / 12 + 1);   // "A" plays the C in the middle of the range
        setVelocity ((float) velocity / 127.0f, false);
    }

    void PlayKeyboard::setPadChannel (int channel)
    {
        // Show every channel except the one the pads use (their notes are controls).
        setMidiChannelsToDisplay (0xffff & ~(1 << (channel - 1)));
    }

    void PlayKeyboard::shiftOctave (int delta)
    {
        const auto next = juce::jlimit (0, 120 - numKeys, lowest + 12 * delta);
        if (next == lowest)
            return;
        focusLost (focusChangedDirectly);   // releases held keys before the mapping moves
        lowest = next;
        applyRange();
        repaint();
        if (onSettingsChanged) onSettingsChanged();
    }

    void PlayKeyboard::changeVelocity (int delta)
    {
        velocity = juce::jlimit (1, 127, velocity + delta);
        applyRange();
        if (onSettingsChanged) onSettingsChanged();
    }

    bool PlayKeyboard::keyPressed (const juce::KeyPress& key)
    {
        // Command shortcuts (undo, copy...) belong to the host, never to the notes.
        if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
            return false;
        switch (juce::CharacterFunctions::toLowerCase (key.getTextCharacter()))
        {
            case 'z': shiftOctave (-1); return true;
            case 'x': shiftOctave (1); return true;
            case 'c': changeVelocity (-20); return true;
            case 'v': changeVelocity (20); return true;
            default: break;
        }
        return MidiKeyboardComponent::keyPressed (key);
    }

    void PlayKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown,
                                      bool isOver, juce::Colour, juce::Colour)
    {
        auto key = area.reduced (1.0f, 0.0f).withTrimmedBottom (1.0f);
        g.setColour (isDown ? padLit (padBlue) : (isOver ? plateRaised.darker (0.03f) : juce::Colours::white));
        g.fillRoundedRectangle (key, 3.0f);
        g.setColour (plateEdge);
        g.drawRoundedRectangle (key, 3.0f, 1.0f);
        if (note % 12 == 0)
        {
            g.setColour (inkFaint);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (noteName (note), key.removeFromBottom (18.0f), juce::Justification::centred);
        }
    }

    void PlayKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown,
                                      bool isOver, juce::Colour)
    {
        auto key = area.reduced (1.5f, 0.0f);
        g.setColour (isDown ? accentBlue : (isOver ? ink.brighter (0.3f) : ink));
        g.fillRoundedRectangle (key, 2.5f);
    }

    //==============================================================================
    void MeterView::push (float l, float r)
    {
        auto toPos = [] (float v) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -60.0f) + 60.0f) / 60.0f); };
        levelL = juce::jmax (toPos (l), levelL * 0.85f);
        levelR = juce::jmax (toPos (r), levelR * 0.85f);
        if (levelL >= holdL || levelR >= holdR || ++holdCount > 45)
        {
            holdL = juce::jmax (levelL, holdCount > 45 ? levelL : holdL);
            holdR = juce::jmax (levelR, holdCount > 45 ? levelR : holdR);
            holdCount = 0;
        }
        repaint();
    }

    void MeterView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const auto barH = (r.getHeight() - 2.0f) * 0.5f;
        auto drawBar = [&] (juce::Rectangle<float> bar, float level, float hold)
        {
            g.setColour (faderSlot);
            g.fillRoundedRectangle (bar, 2.0f);
            const auto clipX = bar.getX() + bar.getWidth() * (54.0f / 60.0f);
            auto fill = bar.withWidth (bar.getWidth() * level);
            g.setColour (accentGreen);
            g.fillRoundedRectangle (fill.withRight (juce::jmin (fill.getRight(), clipX)), 2.0f);
            if (fill.getRight() > clipX)
            {
                g.setColour (accentOrange);
                g.fillRect (fill.withLeft (clipX));
            }
            g.setColour (glassText);
            g.fillRect (bar.getX() + bar.getWidth() * hold - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        };
        drawBar (r.removeFromTop (barH), levelL, holdL);
        r.removeFromTop (2.0f);
        drawBar (r, levelR, holdR);
    }

    //==============================================================================
    void DisplayScreen::setPage (const juce::String& name, int index, int count)
    {
        if (name != page || index != pageIndex || count != pageCount)
        {
            page = name; pageIndex = index; pageCount = count;
            repaint();
        }
    }

    void DisplayScreen::setStatus (const juce::String& p, const juce::String& v, const juce::String& cc)
    {
        if (p != statusParam || v != statusValue || cc != statusCc)
        {
            statusParam = p; statusValue = v; statusCc = cc;
            repaint();
        }
    }

    void DisplayScreen::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (glass);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (glassEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto inner = r.reduced (12.0f, 8.0f).toNearestInt();
        auto top = inner.removeFromTop (22);
        g.setFont (lookAndFeel.font (13.0f, true));
        g.setColour (pageIndex == 0 ? accentBlue : accentOrange);
        g.fillEllipse ((float) top.getX(), (float) top.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (glassText);
        g.drawText (page.toUpperCase(), top.withTrimmedLeft (14), juce::Justification::centredLeft);
        g.setColour (glassDim);
        g.drawText (juce::String (pageIndex + 1) + "/" + juce::String (pageCount), top, juce::Justification::centredRight);

        inner.removeFromTop (6);
        g.setColour (glassDim);
        g.setFont (lookAndFeel.font (13.0f));
        g.drawText (statusParam, inner.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (glassText);
        g.setFont (lookAndFeel.font (26.0f, true));
        g.drawFittedText (statusValue, inner.removeFromTop (34), juce::Justification::centredLeft, 1);
        g.setColour (glassDim);
        g.setFont (lookAndFeel.font (12.5f));
        g.drawText (statusCc, inner.removeFromTop (18), juce::Justification::centredLeft);
    }

    //==============================================================================
    SettingsPanel::SettingsPanel (UiContext& c) : ctx (c)
    {
        const char* knobIds[] = { pid::root, pid::fine, pid::chaos, pid::window, pid::voices, pid::glide,
                                  pid::bendRange, pid::velocity, pid::filterDecay, pid::modDepth, pid::spaceSize };
        for (auto* id : knobIds)
        {
            auto k = std::make_unique<ControlKnob> (ctx, 0, 46.0f);
            k->setCompact (true);
            k->attach (id, knobArc);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }

        const char* choiceIds[] = { pid::source, pid::quantize, pid::scanMode, pid::syncRate, pid::voiceMode, pid::filterType,
                                    pid::lfoShape, pid::lfoTarget, pid::lfoMode, pid::lfoDivision, pid::modTarget,
                                    pid::linkVoices };
        for (auto* id : choiceIds)
        {
            auto ch = std::make_unique<Choice>();
            ch->paramId = id;
            auto* param = ctx.processor.param (id);
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (param))
            {
                for (int i = 0; i < p->choices.size(); ++i)      // real entries only
                    if (p->choices[i] != reservedChoiceName)
                        ch->box.addItem (p->choices[i], i + 1);
            }
            else
            {
                ch->box.addItem (tr ("Off"), 1);
                ch->box.addItem (tr ("On"), 2);
            }
            ch->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ctx.processor.getState(), id, ch->box);
            ch->label.setText (param->getName (32), juce::dontSendNotification);
            styleLabel (ch->label, ctx.lookAndFeel, 12.5f, true, glassText);
            ch->label.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (ch->label);
            addAndMakeVisible (ch->box);
            choices.push_back (std::move (ch));
        }
        for (auto& k : knobs)
        {
            k->setColour (juce::Label::textColourId, glassText);
            for (auto* child : k->getChildren())
                if (auto* l = dynamic_cast<juce::Label*> (child))
                    l->setColour (juce::Label::textColourId, glassText);
        }
    }

    void SettingsPanel::refreshTexts() { repaint(); }

    void SettingsPanel::resized()
    {
        auto r = getLocalBounds().reduced (18, 14);
        r.removeFromTop (24);
        auto knobRow = r.removeFromTop (100);
        const auto knobW = knobRow.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
            k->setBounds (knobRow.removeFromLeft (knobW).reduced (4, 0));

        r.removeFromTop (12);
        const int perRow = 6;
        const auto cellW = r.getWidth() / perRow;
        for (size_t i = 0; i < choices.size(); ++i)
        {
            const auto row = (int) i / perRow, col = (int) i % perRow;
            auto cell = juce::Rectangle<int> (r.getX() + col * cellW, r.getY() + row * 62, cellW, 58).reduced (8, 2);
            choices[i]->label.setBounds (cell.removeFromTop (20));
            choices[i]->box.setBounds (cell.removeFromTop (30));
        }
    }

    void SettingsPanel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (glass.withAlpha (0.96f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (glassText);
        g.setFont (ctx.lookAndFeel.font (14.0f, true));
        g.drawText (tr ("More settings"), getLocalBounds().reduced (18, 12).removeFromTop (20), juce::Justification::centredLeft);
    }

    //==============================================================================
    void HelpPanel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (glass.withAlpha (0.97f));
        g.fillRoundedRectangle (r, 8.0f);

        // Written with "|" for the separator dot; UTF-8 stays out of C++ literals.
        const std::pair<const char*, const char*> lines[] = {
            { "Keyboard", "play grains; pitch is relative to Root" },
            { "Main encoder", "turn: Position | click: next page" },
            { "Encoders 1-8", "parameters of the current page" },
            { "Faders 1-4", "envelope; they pick up the current value" },
            { "Pads, bank A", "Freeze | Hold | Reverse | Window | Sync | Filter | Mode | A/B" },
            { "Pads, bank B", "cues: tap an empty pad to store, tap to jump, hold to overwrite" },
            { "Touch strips", "pitch bend | modulation (target in More settings)" },
            { "Computer keyboard", "A-K play | Z X octave | C V velocity" },
            { "Waveform", "drag: Position | Alt-drag or vertical drag: Spray | wheel: Size" },
            { "Right-click a control", "MIDI Learn, forget CC, reset" },
            { "Double-click a control", "default value" },
        };

        auto area = getLocalBounds().reduced (22, 12);
        g.setColour (glassText);
        g.setFont (lookAndFeel.font (15.0f, true));
        g.drawText (tr ("Gestures"), area.removeFromTop (24), juce::Justification::centredLeft);
        area.removeFromTop (2);
        for (auto& [what, how] : lines)
        {
            auto row = area.removeFromTop (21);
            g.setColour (glassText);
            g.setFont (lookAndFeel.font (13.5f, true));
            g.drawText (tr (what), row.removeFromLeft (220), juce::Justification::centredLeft);
            g.setColour (glassDim);
            g.setFont (lookAndFeel.font (13.5f));
            g.drawFittedText (tr (how).replace ("|", juce::String::fromUTF8 ("\xc2\xb7")), row, juce::Justification::centredLeft, 1);
        }
    }
}
