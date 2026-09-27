#include "PluginEditor.h"
#include "BinaryData.h"
#include "SampleLibrary.h"
#include <i18n/Translator.h>

namespace thf::grain
{
    using namespace thf::palette;

    namespace
    {
        // Design-space geometry (see designWidth/Height). Mirrors the controller, left to right.
        const juce::Rectangle<int> headerArea    { 0, 0, 1040, 56 };
        const juce::Rectangle<int> waveArea      { 16, 60, 1008, 330 };
        const juce::Rectangle<int> plateArea     { 12, 400, 1016, 318 };
        const juce::Rectangle<int> stripsArea    { 22, 410, 60, 226 };
        const juce::Rectangle<int> displayArea   { 94, 406, 216, 232 };
        const juce::Rectangle<int> encodersArea  { 322, 404, 392, 236 };
        const juce::Rectangle<int> fadersArea    { 722, 404, 300, 236 };
        const juce::Rectangle<int> padsArea      { 94, 648, 928, 62 };
        const juce::Rectangle<int> bankArea      { 22, 648, 60, 62 };
        const juce::Rectangle<int> keysArea      { 22, 722, 1000, 84 };   // the controller's 25 keys
        const juce::Rectangle<int> footerArea    { 16, 810, 1008, 56 };

        const juce::Colour padColours[] = { padBlue, padTeal, padGreen, padYellow, padSalmon, padGrey, padPink, padCream };
        const juce::Colour faderColours[] = { accentTeal, accentGreen, accentYellow, accentOrange };

        juce::Colour pageColour (int page) { return page == 0 ? knobArc : accentOrange; }

        juce::String str (std::string_view s) { return juce::String (std::string (s)); }

        void loadTranslations()
        {
            static bool loaded = false;
            if (! loaded)
            {
                Translator::get().addTable (Translator::Language::russian, BinaryData::ru_grain_txt, (size_t) BinaryData::ru_grain_txtSize);
                loaded = true;
            }
        }
    }

    GrainEditor::GrainEditor (GrainProcessor& p)
        : AudioProcessorEditor (p),
          processor (p),
          lookAndFeel (BinaryData::GolosTextRegular_ttf, (size_t) BinaryData::GolosTextRegular_ttfSize,
                       BinaryData::GolosTextSemiBold_ttf, (size_t) BinaryData::GolosTextSemiBold_ttfSize),
          ctx { p, lookAndFeel, {}, {} },
          waveform (ctx),
          screen (lookAndFeel),
          mainKnob (ctx),
          keyboard (p.getKeyboardState()),
          settings (ctx),
          help (lookAndFeel)
    {
        loadTranslations();
        setLookAndFeel (&lookAndFeel);
        ctx.onFocus = [this] (const juce::String& id) { focusParam (id); };
        ctx.isLearnMode = [this] { return learnMode; };

        addAndMakeVisible (content);

        // Header.
        for (auto* b : { &prevPreset, &nextPreset, &presetName, &undoButton, &redoButton, &langButton, &learnButton, &helpButton })
            content.addAndMakeVisible (b);
        prevPreset.onClick = [this] { processor.getPresets().step (-1); };
        nextPreset.onClick = [this] { processor.getPresets().step (1); };
        presetName.onClick = [this] { showPresetMenu(); };
        undoButton.onClick = [this] { processor.getUndoManager().undo(); };
        redoButton.onClick = [this] { processor.getUndoManager().redo(); };
        undoButton.setButtonText (juce::String::fromUTF8 ("\xe2\x86\xb6"));
        redoButton.setButtonText (juce::String::fromUTF8 ("\xe2\x86\xb7"));
        langButton.onClick = [] {
            auto& t = Translator::get();
            t.setLanguage (t.getLanguage() == Translator::Language::russian ? Translator::Language::english
                                                                             : Translator::Language::russian);
        };
        learnButton.setClickingTogglesState (true);
        learnButton.onClick = [this]
        {
            learnMode = learnButton.getToggleState();
            if (! learnMode) processor.cancelLearn();
            repaint();
        };
        helpButton.setButtonText ("?");
        helpButton.onClick = [this] { showHelp (! help.isVisible()); };

        // Glass screen and its overlays.
        content.addAndMakeVisible (waveform);
        waveform.onLoadRequest = [this] { chooseSample(); };
        moreButton.setClickingTogglesState (true);
        moreButton.onClick = [this] { showSettings (moreButton.getToggleState()); };
        content.addChildComponent (settings);
        content.addAndMakeVisible (moreButton);
        content.addChildComponent (help);

        // Strips.
        content.addAndMakeVisible (pitchStrip);
        content.addAndMakeVisible (modStrip);

        // Display + main encoder.
        content.addAndMakeVisible (screen);
        screen.onPageClick = [this] { processor.setPage ((processor.getPage() + 1) % layout::numPages); };
        mainKnob.paramId = str (layout::mainEncoderParam);
        mainKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        mainKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        mainKnob.setRotaryParameters (juce::MathConstants<float>::pi * (7.0f / 6.0f), juce::MathConstants<float>::pi * (17.0f / 6.0f), true);
        mainKnob.setMouseDragSensitivity (600);
        mainKnob.setColour (juce::Slider::rotarySliderFillColourId, glassText.withAlpha (0.0f));
        mainKnob.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::transparentBlack);
        mainKnob.onValueChange = [this] { focusParam (mainKnob.paramId); };
        mainKnobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getState(), mainKnob.paramId, mainKnob);
        content.addAndMakeVisible (mainKnob);

        // Encoders, faders, pads: all from the layout table.
        for (int i = 0; i < layout::numEncoders; ++i)
        {
            encoders[(size_t) i] = std::make_unique<ControlKnob> (ctx, i + 1, 62.0f);
            content.addAndMakeVisible (*encoders[(size_t) i]);
        }
        for (int i = 0; i < layout::numFaders; ++i)
        {
            faders[(size_t) i] = std::make_unique<ControlFader> (ctx, i + 1, str (layout::faderParams[(size_t) i]), faderColours[i]);
            content.addAndMakeVisible (*faders[(size_t) i]);
        }
        for (int i = 0; i < layout::numPads; ++i)
        {
            pads[(size_t) i] = std::make_unique<PadButton> (padColours[i]);
            pads[(size_t) i]->onClick = [this, i] (const juce::ModifierKeys& m) { padClicked (i, m); };
            content.addAndMakeVisible (*pads[(size_t) i]);
        }
        for (auto* b : { &bankA, &bankB })
        {
            b->setRadioGroupId (1);
            b->setClickingTogglesState (true);
            content.addAndMakeVisible (b);
        }
        bankA.setToggleState (true, juce::dontSendNotification);
        bankA.onClick = [this] { setPadBank (0); };
        bankB.onClick = [this] { setPadBank (1); };

        // Keys: on screen, and from the computer keyboard.
        content.addAndMakeVisible (keyboard);
        keyboard.onSettingsChanged = [this] { content.repaint (footerArea); };

        // Footer.
        content.addAndMakeVisible (meter);
        hqButton.setClickingTogglesState (true);
        clipButton.setClickingTogglesState (true);
        hqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getState(), pid::hq, hqButton);
        clipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getState(), pid::safeClip, clipButton);
        midiButton.onClick = [this] { showMidiMenu(); };
        for (auto* b : { &hqButton, &clipButton, &midiButton })
            content.addAndMakeVisible (b);

        pitchStrip.onUserChange = [] (float) {};
        modStrip.onUserChange = [] (float) {};

        Translator::get().addChangeListener (this);
        refreshTexts();
        applyPage();
        layoutContent();

        setResizable (true, true);
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio ((double) designWidth / designHeight);
        setResizeLimits (designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 16 / 10, designHeight * 16 / 10);
        // Start at full size if it fits the screen, otherwise scaled down (laptops).
        auto scale = 1.0;
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            scale = juce::jlimit (0.6, 1.0, (display->userBounds.getHeight() - 90) / (double) designHeight);
        setSize (juce::roundToInt (designWidth * scale), juce::roundToInt (designHeight * scale));
        keepKeyboardFocus();
        startTimerHz (30);
    }

    // Only the keyboard takes keyboard focus, so clicking knobs, pads or buttons never stops
    // the computer keys from playing. Text fields (preset name) still get focus when open.
    void GrainEditor::keepKeyboardFocus()
    {
        std::function<void (juce::Component&)> noFocus = [&noFocus, this] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (child != &keyboard)
                {
                    child->setWantsKeyboardFocus (false);
                    child->setMouseClickGrabsKeyboardFocus (false);
                }
                noFocus (*child);
            }
        };
        noFocus (content);
        setWantsKeyboardFocus (false);
        content.addMouseListener (this, true);
    }

    void GrainEditor::mouseDown (const juce::MouseEvent&)
    {
        // A click anywhere in the plug-in hands the computer keyboard to the on-screen keys,
        // unless a text field (preset name) is being edited.
        if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) == nullptr)
            keyboard.grabKeyboardFocus();
    }

    GrainEditor::~GrainEditor()
    {
        Translator::get().removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    //==============================================================================
    void GrainEditor::paint (juce::Graphics& g)
    {
        g.fillAll (plate);
    }

    void GrainEditor::resized()
    {
        const auto scale = (float) getWidth() / (float) designWidth;
        content.setBounds (0, 0, designWidth, designHeight);
        content.setTransform (juce::AffineTransform::scale (scale));
    }

    void GrainEditor::layoutContent()
    {
        // Header.
        auto h = headerArea.reduced (14, 10);
        helpButton.setBounds (h.removeFromRight (36));
        h.removeFromRight (8);
        learnButton.setBounds (h.removeFromRight (84));
        h.removeFromRight (8);
        langButton.setBounds (h.removeFromRight (48));
        int x = 290;
        const auto y = h.getY(), hh = h.getHeight();
        prevPreset.setBounds (x, y, 36, hh);  x += 40;
        presetName.setBounds (x, y, 300, hh); x += 304;
        nextPreset.setBounds (x, y, 36, hh);  x += 50;
        undoButton.setBounds (x, y, 36, hh);  x += 42;
        redoButton.setBounds (x, y, 36, hh);

        waveform.setBounds (waveArea);
        moreButton.setBounds (waveArea.getRight() - 120, waveArea.getY() + 8, 108, 26);
        settings.setBounds (waveArea.reduced (8).withTrimmedTop (34));
        help.setBounds (waveArea.reduced (8).withTrimmedTop (34));

        // Strips (the controller has them at the far left).
        auto strips = stripsArea;
        pitchStrip.setBounds (strips.removeFromLeft (26).withTrimmedBottom (22));
        strips.removeFromLeft (8);
        modStrip.setBounds (strips.removeFromLeft (26).withTrimmedBottom (22));

        // Display + main encoder.
        auto d = displayArea.reduced (10);
        screen.setBounds (d.removeFromTop (118));
        mainKnob.setBounds (d.withSizeKeepingCentre (90, 90).translated (0, 2));

        // Encoders: row 1 = 1-4, row 2 = 5-8.
        const auto cellW = encodersArea.getWidth() / 4;
        const auto cellH = encodersArea.getHeight() / 2;
        for (int i = 0; i < layout::numEncoders; ++i)
            encoders[(size_t) i]->setBounds (encodersArea.getX() + layout::encoderColumn (i) * cellW,
                                             encodersArea.getY() + layout::encoderRow (i) * cellH,
                                             cellW, cellH - 2);

        const auto faderW = fadersArea.getWidth() / layout::numFaders;
        for (int i = 0; i < layout::numFaders; ++i)
            faders[(size_t) i]->setBounds (fadersArea.getX() + i * faderW, fadersArea.getY(), faderW, fadersArea.getHeight());

        const auto gap = 10;
        const auto padW = (padsArea.getWidth() - gap * (layout::numPads - 1)) / layout::numPads;
        for (int i = 0; i < layout::numPads; ++i)
            pads[(size_t) i]->setBounds (padsArea.getX() + i * (padW + gap), padsArea.getY(), padW, padsArea.getHeight());

        keyboard.setBounds (keysArea);
        keyboard.setKeyWidth ((float) keysArea.getWidth() / 15.0f);   // 15 white keys in 25

        auto bank = bankArea;
        bankA.setBounds (bank.removeFromTop (29));
        bank.removeFromTop (4);
        bankB.setBounds (bank.removeFromTop (29));

        auto f = footerArea.reduced (6, 12);
        midiButton.setBounds (f.removeFromRight (84));
        f.removeFromRight (8);
        clipButton.setBounds (f.removeFromRight (104));
        f.removeFromRight (8);
        hqButton.setBounds (f.removeFromRight (64));
        meter.setBounds (footerArea.getX() + 80, f.getCentreY() - 7, 220, 14);
    }

    void GrainEditor::paintContent (juce::Graphics& g)
    {
        g.fillAll (plate);

        // Brand.
        g.setColour (ink);
        g.setFont (lookAndFeel.font (22.0f));
        g.drawText ("thf", 18, 12, 40, 32, juce::Justification::centredLeft);
        g.setFont (lookAndFeel.font (22.0f, true));
        g.drawText ("Grain", 55, 12, 100, 32, juce::Justification::centredLeft);

        // Plate panels.
        g.setColour (plateEdge);
        g.drawHorizontalLine (footerArea.getY() - 4, 16.0f, (float) footerArea.getRight());

        const auto displayPanel = displayArea.toFloat();
        g.setColour (plateInset);
        g.fillRoundedRectangle (displayPanel, 10.0f);
        g.setColour (plateEdge);
        g.drawRoundedRectangle (displayPanel.reduced (0.5f), 10.0f, 1.0f);

        g.setFont (lookAndFeel.font (12.0f));
        g.setColour (inkDim);
        g.drawText (tr ("Pitch"), pitchStrip.getX() - 8, pitchStrip.getBottom() + 4, pitchStrip.getWidth() + 16, 16, juce::Justification::centred);
        g.drawText (tr ("Mod"), modStrip.getX() - 8, modStrip.getBottom() + 4, modStrip.getWidth() + 16, 16, juce::Justification::centred);

        // Output readout next to the meter.
        g.setColour (inkDim);
        g.drawText (tr ("Output"), footerArea.getX() + 6, meter.getY() - 2, 70, 18, juce::Justification::centredLeft);

        // Computer keyboard state: range and velocity.
        const auto lowest = keyboard.getLowestNote();
        g.drawText (tr ("Keyboard") + " " + noteName (lowest) + "-" + noteName (lowest + PlayKeyboard::numKeys - 1)
                        + sep() + tr ("velocity") + " " + juce::String (keyboard.getVelocity()),
                    meter.getRight() + 24, meter.getY() - 2, 300, 18, juce::Justification::centredLeft);

        // Main encoder is endless on the controller: draw the cap only, with a position tick ring.
        const auto knob = mainKnob.getBounds().toFloat();
        g.setColour (plateEdge);
        for (int i = 0; i < 24; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
            const auto c = knob.getCentre();
            const auto r0 = knob.getWidth() * 0.5f + 3.0f, r1 = r0 + (i % 6 == 0 ? 6.0f : 3.0f);
            g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, 1.0f);
        }

        if (learnMode)
        {
            g.setColour (knobArc);
            g.drawRoundedRectangle (plateArea.toFloat(), 10.0f, 2.0f);
        }
    }

    //==============================================================================
    void GrainEditor::refreshTexts()
    {
        const bool russian = Translator::get().getLanguage() == Translator::Language::russian;
        langButton.setButtonText (russian ? "RU" : "EN");
        learnButton.setButtonText (tr ("Learn"));
        moreButton.setButtonText (tr ("More"));
        clipButton.setButtonText (tr ("Safe Clip"));
        presetName.setButtonText (processor.getPresets().getCurrentName());
        undoButton.setTooltip (tr ("Undo"));
        redoButton.setTooltip (tr ("Redo"));
        settings.refreshTexts();
        refreshPads();
        repaint();
        content.repaint();
    }

    void GrainEditor::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        refreshTexts();
    }

    void GrainEditor::applyPage()
    {
        const auto page = processor.getPage();
        if (page == shownPage)
            return;
        shownPage = page;
        for (int i = 0; i < layout::numEncoders; ++i)
            encoders[(size_t) i]->attach (str (layout::encoderParams[(size_t) page][(size_t) i]), pageColour (page));
        screen.setPage (tr (str (layout::pageNames[(size_t) page])), page, layout::numPages);
    }

    void GrainEditor::setPadBank (int bank)
    {
        padBank = bank;
        bankA.setToggleState (bank == 0, juce::dontSendNotification);
        bankB.setToggleState (bank == 1, juce::dontSendNotification);
        refreshPads();
    }

    void GrainEditor::refreshPads()
    {
        auto value = [this] (const char* id) { return processor.param (id)->getValue(); };
        auto text = [this] (const char* id) { return processor.param (id)->getCurrentValueAsText(); };

        for (int i = 0; i < layout::numPads; ++i)
        {
            auto& pad = *pads[(size_t) i];
            if (padBank == 1)
            {
                const auto cue = processor.getCue (i);
                pad.setColour (padYellow);
                pad.setLabel (tr ("Cue") + " " + juce::String (i + 1));
                pad.setLit (cue >= 0.0f);
                pad.setSubLabel (cue >= 0.0f ? juce::String (cue * 100.0f, 1) + " %" : juce::String());
                continue;
            }

            pad.setColour (padColours[i]);
            pad.setLabel (tr (str (layout::padLabels[(size_t) i])));
            switch (layout::padsBankA[(size_t) i])
            {
                case layout::PadAction::freeze:  pad.setLit (value (pid::freeze) > 0.5f); pad.setSubLabel ({}); break;
                case layout::PadAction::hold:    pad.setLit (value (pid::hold) > 0.5f); pad.setSubLabel ({}); break;
                case layout::PadAction::reverse: pad.setLit (value (pid::reverse) > 0.5f); pad.setSubLabel (text (pid::reverse)); break;
                case layout::PadAction::windowCycle:
                {
                    const auto w = value (pid::window);
                    pad.setLit (w > 0.01f);
                    pad.setSubLabel (w < 0.25f ? "Hann" : (w < 0.75f ? "Tukey" : "Flat"));
                    break;
                }
                case layout::PadAction::sync:    pad.setLit (value (pid::sync) > 0.5f); pad.setSubLabel (text (pid::syncRate)); break;
                case layout::PadAction::filterCycle:
                    pad.setLit (value (pid::filterType) > 0.01f); pad.setSubLabel (text (pid::filterType)); break;
                case layout::PadAction::voiceModeCycle:
                    pad.setLit (value (pid::voiceMode) > 0.01f); pad.setSubLabel (text (pid::voiceMode)); break;
                case layout::PadAction::abToggle:
                    pad.setLabel (processor.getABSlot() == 0 ? "A" : "B");
                    pad.setLit (processor.getABSlot() == 1);
                    pad.setSubLabel ({});
                    break;
            }
        }
    }

    void GrainEditor::padClicked (int index, const juce::ModifierKeys& mods)
    {
        if (padBank == 1)
        {
            if (mods.isAltDown())
                processor.setCue (index, -1.0f);
            else if (mods.isShiftDown() || mods.isPopupMenu() || processor.getCue (index) < 0.0f)
                processor.setCue (index, processor.getEngine().getPlayhead());
            else
                processor.jumpToCue (index);
            refreshPads();
            return;
        }
        processor.getUndoManager().beginNewTransaction();
        if (layout::padsBankA[(size_t) index] == layout::PadAction::abToggle)
            processor.toggleAB();
        else
            processor.performPadAction (index);
        refreshPads();
    }

    void GrainEditor::focusParam (const juce::String& id)
    {
        focusedParam = id;
        focusTime = juce::Time::getMillisecondCounter();
        updateStatus();
    }

    void GrainEditor::updateStatus()
    {
        if (processor.isLearning())
        {
            auto* p = processor.param (processor.getLearningParam());
            screen.setStatus (tr ("MIDI Learn"), p != nullptr ? p->getName (32) : juce::String(), tr ("move a control on the controller"));
            return;
        }
        auto* p = processor.param (focusedParam);
        if (p == nullptr)
        {
            screen.setStatus ({}, {}, {});
            return;
        }
        const auto cc = processor.getCcFor (focusedParam);
        juce::String ccText = cc >= 0 ? "CC " + juce::String (cc) : tr ("no CC");
        if (processor.isLearned (focusedParam))
            ccText << sep() << tr ("learned");
        screen.setStatus (p->getName (32), p->getCurrentValueAsText(), ccText);
    }

    void GrainEditor::timerCallback()
    {
        applyPage();

        if (const auto serial = processor.getLastTouchedSerial(); serial != lastTouchSerial)
        {
            lastTouchSerial = serial;
            if (const auto index = processor.getLastTouchedParam(); index >= 0)
                if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters()[index]))
                    focusParam (p->getParameterID());
        }
        if (const auto serial = processor.getLastPadSerial(); serial != lastPadSerial)
        {
            if (lastPadSerial >= 0)
            {
                const auto pad = processor.getLastPad();
                if (pad >= layout::numPads && padBank != 1) setPadBank (1);
                if (pad >= 0 && pad < layout::numPads && padBank != 0) setPadBank (0);
                if (pad >= 0) pads[(size_t) (pad % layout::numPads)]->flash();
            }
            lastPadSerial = serial;
        }
        updateStatus();
        refreshPads();
        keyboard.setPadChannel (processor.getPadsAsControls() ? processor.getPadChannel() : 17);

        // Standalone: the window is ours, so the computer keys play as soon as it opens.
        // In a host the keyboard only takes focus when the plug-in window is clicked
        // (mouseDown below): grabbing it from a timer would steal the host's keyboard.
        if (juce::JUCEApplicationBase::isStandaloneApp() && ! standaloneFocusTaken && isShowing())
        {
            keyboard.grabKeyboardFocus();
            standaloneFocusTaken = true;
        }

        pitchStrip.setValue (processor.getPitchStrip());
        modStrip.setValue (processor.getModStrip());
        const auto [l, r] = processor.getOutputPeaks();
        meter.push (l, r);
        waveform.tick();

        undoButton.setEnabled (processor.getUndoManager().canUndo());
        redoButton.setEnabled (processor.getUndoManager().canRedo());
        if (presetName.getButtonText() != processor.getPresets().getCurrentName())
            presetName.setButtonText (processor.getPresets().getCurrentName());
        if (const auto learning = processor.getLearningParam(); learning != lastLearning)
        {
            lastLearning = learning;
            for (auto& e : encoders) e->repaint();
            for (auto& f : faders) f->repaint();
        }
    }

    //==============================================================================
    void GrainEditor::showSettings (bool show)
    {
        if (show) help.setVisible (false);
        settings.setVisible (show);
        moreButton.setToggleState (show, juce::dontSendNotification);
    }

    void GrainEditor::showHelp (bool show)
    {
        if (show) showSettings (false);
        help.setVisible (show);
    }

    void GrainEditor::showPresetMenu()
    {
        auto& presets = processor.getPresets();
        const auto entries = presets.list();
        juce::PopupMenu menu;
        std::map<juce::String, juce::PopupMenu> categories;
        juce::StringArray order;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            const auto cat = tr (e.category);
            if (! order.contains (cat)) order.add (cat);
            categories[cat].addItem ((int) i + 1, e.name, true, e.name == presets.getCurrentName());
        }
        for (auto& cat : order)
            menu.addSubMenu (cat, categories[cat]);
        menu.addSeparator();
        menu.addItem (10001, tr ("Save preset..."));
        menu.addItem (10002, tr ("Show preset folder"));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName),
                            [this, entries] (int r)
                            {
                                auto& manager = processor.getPresets();
                                if (r > 0 && r <= (int) entries.size())
                                    manager.load (entries[(size_t) r - 1]);
                                else if (r == 10002)
                                {
                                    PresetManager::userFolder().createDirectory();
                                    PresetManager::userFolder().revealToUser();
                                }
                                else if (r == 10001)
                                {
                                    auto* w = new juce::AlertWindow (tr ("Save preset"), {}, juce::MessageBoxIconType::NoIcon, this);
                                    w->addTextEditor ("name", manager.getCurrentName());
                                    w->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
                                    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
                                    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
                                    {
                                        if (result == 1)
                                            processor.getPresets().saveUser (w->getTextEditorContents ("name"));
                                    }), true);
                                }
                            });
    }

    void GrainEditor::showMidiMenu()
    {
        juce::PopupMenu menu, encoderModes, channels;
        menu.addSectionHeader (tr (juce::String::fromUTF8 ("Works with Arturia\xc2\xae MiniLab 3")));
        const auto mode = processor.getEncoderMode();
        encoderModes.addItem (1, tr ("Relative #1 (64 +/- n)"), true, mode == midi::EncoderMode::binaryOffset);
        encoderModes.addItem (2, tr ("Relative, two's complement"), true, mode == midi::EncoderMode::twosComplement);
        encoderModes.addItem (3, tr ("Relative, sign bit"), true, mode == midi::EncoderMode::signMagnitude);
        encoderModes.addItem (4, tr ("Absolute (with pickup)"), true, mode == midi::EncoderMode::absolute);
        menu.addSubMenu (tr ("Encoders"), encoderModes);
        menu.addItem (10, tr ("Pads control the instrument"), true, processor.getPadsAsControls());
        for (int ch = 1; ch <= 16; ++ch)
            channels.addItem (100 + ch, juce::String (ch), true, processor.getPadChannel() == ch);
        menu.addSubMenu (tr ("Pad MIDI channel"), channels);
        menu.addSeparator();
        menu.addItem (20, tr ("Forget all learned CCs"));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton), [this] (int r)
        {
            if (r >= 1 && r <= 4)
                processor.setEncoderMode ((midi::EncoderMode) (r - 1));
            else if (r == 10)
                processor.setPadsAsControls (! processor.getPadsAsControls());
            else if (r > 100)
                processor.setPadChannel (r - 100);
            else if (r == 20)
                processor.clearAllLearned();
        });
    }

    void GrainEditor::chooseSample()
    {
        // Starts where the last sample came from; offers every format the platform reads.
        const auto recentFiles = library::recent();
        const auto start = recentFiles.isEmpty() ? library::userFolder() : recentFiles.getFirst().getParentDirectory();
        chooser = std::make_unique<juce::FileChooser> (tr ("Load sample"), start,
                                                       "*." + sources::audioExtensions().replace (";", ";*."));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      processor.loadSampleAsync (fc.getResult());
                              });
    }

    //==============================================================================
    bool GrainEditor::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (auto& f : files)
            if (juce::File (f).hasFileExtension (sources::audioExtensions()))
                return true;
        return false;
    }

    void GrainEditor::fileDragEnter (const juce::StringArray&, int, int) { waveform.setDropHighlight (true); }
    void GrainEditor::fileDragExit (const juce::StringArray&)            { waveform.setDropHighlight (false); }

    void GrainEditor::filesDropped (const juce::StringArray& files, int, int)
    {
        waveform.setDropHighlight (false);
        for (auto& f : files)
            if (juce::File (f).hasFileExtension (sources::audioExtensions()))
            {
                processor.loadSampleAsync (juce::File (f));
                return;
            }
    }
}
