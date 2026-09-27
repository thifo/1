#include "WaveformView.h"
#include "../SampleLibrary.h"
#include <i18n/Translator.h>

namespace thf::grain
{
    using namespace thf::palette;

    namespace
    {
        const juce::Colour voiceColours[] = { accentBlue, accentTeal, accentGreen, accentYellow, accentOrange, accentRose };

        juce::String formatSeconds (double s)
        {
            return s < 10.0 ? juce::String (s, 2) + " s" : juce::String (s, 1) + " s";
        }
    }

    WaveformView::WaveformView (UiContext& c) : ctx (c)
    {
        live.reserve (512);
        for (auto* b : { &prevSample, &sampleButton, &nextSample })
        {
            b->setColour (juce::TextButton::buttonColourId, glassRaised);
            b->setColour (juce::TextButton::textColourOffId, glassText);
            addAndMakeVisible (b);
        }
        prevSample.onClick = [this] { stepSample (-1); };
        nextSample.onClick = [this] { stepSample (1); };
        sampleButton.onClick = [this] { showSampleMenu(); };
    }

    void WaveformView::resized()
    {
        // Sample bar sits left of the editor's "More" button (right 120 px of the header).
        auto bar = juce::Rectangle<int> (getWidth() - 128 - 236, 8, 236, 26);
        prevSample.setBounds (bar.removeFromLeft (30));
        nextSample.setBounds (bar.removeFromRight (30));
        sampleButton.setBounds (bar.reduced (4, 0));
    }

    float WaveformView::regionStart() const
    {
        const auto& s = ctx.processor.getState();
        return juce::jmin (s.getRawParameterValue (pid::regionStart)->load(), s.getRawParameterValue (pid::regionEnd)->load());
    }

    float WaveformView::regionEnd() const
    {
        const auto& s = ctx.processor.getState();
        return juce::jmax (s.getRawParameterValue (pid::regionStart)->load(), s.getRawParameterValue (pid::regionEnd)->load());
    }

    float WaveformView::toAbsolute (float relative) const
    {
        return regionStart() + relative * juce::jmax (1.0e-3f, regionEnd() - regionStart());
    }

    float WaveformView::toRelative (float absolute) const
    {
        return juce::jlimit (0.0f, 1.0f, (absolute - regionStart()) / juce::jmax (1.0e-3f, regionEnd() - regionStart()));
    }

    juce::Rectangle<float> WaveformView::waveArea() const
    {
        return getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (40.0f).withTrimmedBottom (34.0f);
    }

    float WaveformView::xToPosition (float x) const
    {
        const auto a = waveArea();
        return juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    }

    float WaveformView::positionToX (float p) const
    {
        const auto a = waveArea();
        return a.getX() + p * a.getWidth();
    }

    void WaveformView::tick()
    {
        auto& proc = ctx.processor;
        const auto serial = proc.getSourceSerial();
        const auto choice = (int) proc.param (pid::source)->convertFrom0to1 (proc.param (pid::source)->getValue());
        if (serial != sourceSerial || choice != sourceChoice || (source == nullptr && choice != 0))
        {
            sourceSerial = serial;
            sourceChoice = choice;
            source = proc.getCurrentSourceForDisplay();
            live.clear();
            const auto label = choice == 0 ? (source != nullptr ? tr ("Sample") : tr ("Load sample..."))
                                           : tr (sourceChoices[choice]);
            sampleButton.setButtonText (label + juce::String::fromUTF8 (" \xe2\x96\xbe"));
        }

        const auto now = juce::Time::getMillisecondCounter();
        const auto n = proc.getEngine().popGrainEvents (incoming.data(), (int) incoming.size());
        for (int i = 0; i < n; ++i)
            if (live.size() < 400)
                live.push_back ({ incoming[(size_t) i], now });

        live.erase (std::remove_if (live.begin(), live.end(), [now] (const LiveGrain& g)
                    {
                        const auto lifeMs = juce::jmax (90.0f, g.event.seconds * 1000.0f);
                        return (float) (now - g.born) > lifeMs;
                    }), live.end());

        // Idle (no voices, or no audio running): the next note starts at Position.
        playhead = proc.getEngine().getActiveVoices() > 0 ? proc.getEngine().getPlayhead()
                                                          : proc.param (pid::position)->getValue();
        repaint();
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        auto& laf = ctx.lookAndFeel;
        auto& proc = ctx.processor;
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (glass);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (dropHighlight ? knobArc : glassEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, dropHighlight ? 2.0f : 1.0f);

        const auto area = waveArea();

        // Header: source description.
        auto header = getLocalBounds().reduced (16, 0).removeFromTop (38);
        g.setFont (laf.font (14.0f, true));
        g.setColour (glassText);
        juce::String title;
        if (sourceChoice == 0)
            title = source != nullptr ? source->getName() : tr ("No sample");
        else
            title = tr (sourceChoices[sourceChoice]) + sep() + tr ("built-in");
        if (source != nullptr)
            title << sep() << formatSeconds (source->getDurationSeconds())
                  << sep() << juce::String (source->getSampleRate() / 1000.0, 1) << " kHz";
        title << sep() << tr ("Root") << " " << noteName ((int) proc.getState().getRawParameterValue (pid::root)->load());
        if (sourceChoice == 0 && source != nullptr)
        {
            if (source->detectedNote >= 0.0f)
                title << sep() << tr ("pitch") << " " << noteName (juce::roundToInt (source->detectedNote));
            if (source->originalPeak > 0.0f)
                title << sep() << tr ("peak") << " " << juce::String (juce::Decibels::gainToDecibels (source->originalPeak), 1) << " dB";
            title << sep() << (source->embeddedFlac.getSize() > 0 ? tr ("in the project") : tr ("linked"));
        }
        g.drawFittedText (title, header.withTrimmedRight (372), juce::Justification::centredLeft, 1);

        // Grid.
        g.setColour (glassGrid);
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), area.getBottom());
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        if (source == nullptr)
        {
            g.setColour (glassDim);
            g.setFont (laf.font (17.0f));
            juce::String message;
            if (proc.isLoading())                              message = tr ("Loading...");
            else if (proc.getLastLoadError().isNotEmpty())    message = tr (proc.getLastLoadError());
            else if (proc.getMissingSamplePath().isNotEmpty()) message = tr ("File not found:") + " " + proc.getMissingSamplePath() + "\n" + tr ("Drop the file here or right-click to find it");
            else if (sourceChoice == 0)                       message = tr ("Drop a wav, aiff or flac file here");
            else                                               message = tr ("Preparing built-in sources...");
            g.drawFittedText (message, area.toNearestInt().reduced (20), juce::Justification::centred, 3);
            return;
        }

        // Waveform: min/max per pixel column from the overview.
        const auto& mins = source->getPeakMin();
        const auto& maxs = source->getPeakMax();
        const auto buckets = (int) mins.size();
        const auto width = juce::jmax (1, (int) area.getWidth());
        const auto halfH = area.getHeight() * 0.46f;
        float peak = 1.0e-3f;
        for (int b = 0; b < buckets; ++b)
            peak = juce::jmax (peak, maxs[(size_t) b], -mins[(size_t) b]);
        const auto scale = 1.0f / peak;

        juce::Path wave;
        for (int x = 0; x < width; ++x)
        {
            const auto b0 = x * buckets / width;
            const auto b1 = juce::jmax (b0 + 1, (x + 1) * buckets / width);
            float lo = 0.0f, hi = 0.0f;
            for (int b = b0; b < b1 && b < buckets; ++b)
            {
                lo = juce::jmin (lo, mins[(size_t) b]);
                hi = juce::jmax (hi, maxs[(size_t) b]);
            }
            const auto px = area.getX() + (float) x;
            wave.addRectangle (px, area.getCentreY() - hi * scale * halfH, 1.0f,
                               juce::jmax (1.0f, (hi - lo) * scale * halfH));
        }
        g.setColour (glassText.withAlpha (0.55f));
        g.fillPath (wave);

        // Region: everything outside is dimmed, the edges are draggable handles.
        const auto xStart = positionToX (regionStart()), xEnd = positionToX (regionEnd());
        g.setColour (glass.withAlpha (0.62f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), xStart - area.getX(), area.getHeight()));
        g.fillRect (juce::Rectangle<float> (xEnd, area.getY(), area.getRight() - xEnd, area.getHeight()));
        for (auto hx : { xStart, xEnd })
        {
            g.setColour (accentYellow.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (hx - 1.0f, area.getY(), 2.0f, area.getHeight()));
            g.fillRoundedRectangle (juce::Rectangle<float> (hx - 5.0f, area.getY(), 10.0f, 14.0f), 2.0f);
        }

        // Spray range around the playhead (both relative to the region).
        const auto regionWidth = xEnd - xStart;
        const auto spray = proc.getState().getRawParameterValue (pid::spray)->load();
        const auto px = positionToX (toAbsolute (playhead));
        const auto sprayW = spray * regionWidth;
        g.setColour (knobArc.withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (px - sprayW * 0.5f, area.getY(), sprayW, area.getHeight()));
        if (px - sprayW * 0.5f < area.getX())   // wraps around
            g.fillRect (juce::Rectangle<float> (area.getRight() - (area.getX() - (px - sprayW * 0.5f)), area.getY(),
                                                area.getX() - (px - sprayW * 0.5f), area.getHeight()));
        if (px + sprayW * 0.5f > area.getRight())
            g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), px + sprayW * 0.5f - area.getRight(), area.getHeight()));

        // Live grains: x = material, y = pan, fading with age.
        const auto now = juce::Time::getMillisecondCounter();
        for (const auto& lg : live)
        {
            const auto& e = lg.event;
            const auto lifeMs = juce::jmax (90.0f, e.seconds * 1000.0f);
            const auto age = (float) (now - lg.born) / lifeMs;
            const auto alpha = juce::jlimit (0.0f, 1.0f, 1.0f - age) * 0.85f;
            const auto x = positionToX (e.position);
            const auto w = juce::jmax (3.0f, e.span * area.getWidth());
            const auto y = area.getCentreY() + e.pan * area.getHeight() * 0.36f;
            g.setColour (voiceColours[(size_t) e.voice % std::size (voiceColours)].withAlpha (alpha));
            g.fillRoundedRectangle (juce::Rectangle<float> (x, y - 3.0f, juce::jmin (w, area.getRight() - x + 2.0f), 6.0f), 3.0f);
        }

        // Playhead.
        g.setColour (glassTrace);
        g.fillRect (juce::Rectangle<float> (px - 1.0f, area.getY() - 6.0f, 2.0f, area.getHeight() + 12.0f));
        juce::Path head;
        head.addTriangle (px - 6.0f, area.getY() - 10.0f, px + 6.0f, area.getY() - 10.0f, px, area.getY() - 3.0f);
        g.fillPath (head);

        // Cue markers.
        g.setFont (laf.font (12.0f, true));
        for (int i = 0; i < 8; ++i)
        {
            const auto cue = proc.getCue (i);
            if (cue < 0.0f)
                continue;
            const auto cx = positionToX (toAbsolute (cue));
            g.setColour (accentYellow.withAlpha (0.7f));
            g.drawVerticalLine ((int) cx, area.getBottom() - 10.0f, area.getBottom() + 4.0f);
            auto tag = juce::Rectangle<float> (cx - 8.0f, area.getBottom() + 6.0f, 16.0f, 16.0f);
            g.fillRoundedRectangle (tag, 3.0f);
            g.setColour (glass);
            g.drawText (juce::String (i + 1), tag, juce::Justification::centred);
        }

        // Footer: engine activity.
        auto footer = getLocalBounds().reduced (16, 0).removeFromBottom (30);
        g.setFont (laf.font (12.5f));
        g.setColour (glassDim);
        const bool frozen = proc.getState().getRawParameterValue (pid::freeze)->load() > 0.5f;
        juce::String left = tr ("Position") + " " + juce::String (playhead * 100.0f, 1) + " %";
        if (frozen) left << sep() << tr ("Frozen");
        g.drawText (left, footer, juce::Justification::centredLeft);
        g.drawText (tr ("Voices") + " " + juce::String (proc.getEngine().getActiveVoices())
                        + sep() + tr ("Grains") + " " + juce::String (proc.getEngine().getActiveGrains()),
                    footer, juce::Justification::centredRight);
    }

    void WaveformView::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            showMenu (e);
            return;
        }
        if (source == nullptr)
        {
            if (sourceChoice == 0 && onLoadRequest) onLoadRequest();
            return;
        }
        auto& proc = ctx.processor;
        proc.getUndoManager().beginNewTransaction();

        // Region handles take priority when grabbed within a few pixels.
        const auto x = (float) e.x;
        const auto dStart = std::abs (x - positionToX (regionStart())), dEnd = std::abs (x - positionToX (regionEnd()));
        if (juce::jmin (dStart, dEnd) < 7.0f && waveArea().expanded (0.0f, 4.0f).contains (e.position))
        {
            drag = dStart <= dEnd ? Drag::regionStart : Drag::regionEnd;
            auto* p = proc.param (drag == Drag::regionStart ? pid::regionStart : pid::regionEnd);
            p->beginChangeGesture();
            if (ctx.onFocus) ctx.onFocus (p->getParameterID());
            return;
        }

        drag = Drag::position;
        dragging = true;
        dragStartY = e.y;
        dragStartSpray = proc.param (pid::spray)->getValue();
        proc.param (pid::position)->beginChangeGesture();
        proc.param (pid::spray)->beginChangeGesture();
        proc.requestScanReset();
        if (! e.mods.isAltDown())
            proc.param (pid::position)->setValueNotifyingHost (toRelative (xToPosition ((float) e.x)));
        if (ctx.onFocus) ctx.onFocus (pid::position);
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        auto& proc = ctx.processor;
        if (drag == Drag::regionStart || drag == Drag::regionEnd)
        {
            // Handles never cross: at least 0.5 % of the file stays between them.
            const auto x = xToPosition ((float) e.x);
            if (drag == Drag::regionStart)
                proc.param (pid::regionStart)->setValueNotifyingHost (juce::jmin (x, regionEnd() - 0.005f));
            else
                proc.param (pid::regionEnd)->setValueNotifyingHost (juce::jmax (x, regionStart() + 0.005f));
            return;
        }
        if (! dragging)
            return;
        if (! e.mods.isAltDown())
        {
            proc.param (pid::position)->setValueNotifyingHost (toRelative (xToPosition ((float) e.x)));
            proc.requestScanReset();
        }
        const auto dy = (float) (dragStartY - e.y) / waveArea().getHeight();
        if (std::abs (dy) > 0.02f || e.mods.isAltDown())
        {
            proc.param (pid::spray)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartSpray + dy));
            if (ctx.onFocus) ctx.onFocus (pid::spray);
        }
    }

    void WaveformView::mouseUp (const juce::MouseEvent&)
    {
        if (drag == Drag::regionStart || drag == Drag::regionEnd)
            ctx.processor.param (drag == Drag::regionStart ? pid::regionStart : pid::regionEnd)->endChangeGesture();
        drag = Drag::none;
        if (! dragging)
            return;
        dragging = false;
        ctx.processor.param (pid::position)->endChangeGesture();
        ctx.processor.param (pid::spray)->endChangeGesture();
    }

    void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        auto* p = ctx.processor.param (e.mods.isShiftDown() ? pid::spray : pid::size);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.05f));
        p->endChangeGesture();
        if (ctx.onFocus) ctx.onFocus (p->getParameterID());
    }

    void WaveformView::showMenu (const juce::MouseEvent& e)
    {
        auto& proc = ctx.processor;
        const auto where = xToPosition ((float) e.x);
        juce::PopupMenu menu, store;
        for (int i = 0; i < 8; ++i)
            store.addItem (100 + i, tr ("Cue") + " " + juce::String (i + 1)
                                        + (proc.getCue (i) >= 0.0f ? "  (" + tr ("replace") + ")" : juce::String()));
        menu.addItem (1, tr ("Load sample..."));
        menu.addSubMenu (tr ("Store cue here"), store);
        menu.addItem (2, tr ("Clear all cues"));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [this, where, &proc] (int r)
                            {
                                if (r == 1 && onLoadRequest) onLoadRequest();
                                if (r == 2) for (int i = 0; i < 8; ++i) proc.setCue (i, -1.0f);
                                if (r >= 100) proc.setCue (r - 100, toRelative (where));
                            });
    }

    void WaveformView::stepSample (int delta)
    {
        auto& proc = ctx.processor;
        if (sourceChoice == 0)
        {
            // Own samples: the neighbour in the same folder, or the first of "My samples".
            const auto current = proc.getUserSample();
            juce::File next;
            if (current != nullptr && current->file.existsAsFile())
                next = library::neighbour (current->file, delta);
            else if (const auto files = library::audioFilesIn (library::userFolder()); ! files.isEmpty())
                next = delta > 0 ? files.getFirst() : files.getLast();
            if (next.existsAsFile())
                proc.loadSampleAsync (next);
            return;
        }
        // Built-in sources cycle among themselves.
        const auto builtIns = sourceChoices.size() - 1;
        const auto next = ((sourceChoice - 1 + delta) % builtIns + builtIns) % builtIns + 1;
        auto* p = proc.param (pid::source);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) next));
        p->endChangeGesture();
    }

    void WaveformView::showSampleMenu()
    {
        auto& proc = ctx.processor;
        const auto sample = proc.getUserSample();
        const auto recentFiles = library::recent();
        const auto myFiles = library::audioFilesIn (library::userFolder());

        juce::PopupMenu menu, recentMenu, myMenu, builtIns;
        menu.addItem (1, tr ("Load sample..."));
        for (int i = 0; i < recentFiles.size(); ++i)
            recentMenu.addItem (1000 + i, recentFiles[i].getFileName());
        menu.addSubMenu (tr ("Recent"), recentMenu, ! recentFiles.isEmpty());
        for (int i = 0; i < juce::jmin (200, myFiles.size()); ++i)
            myMenu.addItem (2000 + i, myFiles[i].getFileName());
        if (! myFiles.isEmpty()) myMenu.addSeparator();
        myMenu.addItem (3, tr ("Open the folder"));
        menu.addSubMenu (tr ("My samples"), myMenu);
        for (int c = 1; c < sourceChoices.size(); ++c)
            builtIns.addItem (3000 + c, tr (sourceChoices[c]), true, sourceChoice == c);
        menu.addSubMenu (tr ("Built-in sources"), builtIns);

        if (sample != nullptr)
        {
            menu.addSeparator();
            if (sample->detectedNote >= 0.0f)
            {
                const auto note = juce::roundToInt (sample->detectedNote);
                const auto cents = juce::roundToInt ((sample->detectedNote - (float) note) * 100.0f);
                menu.addItem (4, tr ("Use the sample's pitch as Root") + ": " + noteName (note)
                                     + (cents != 0 ? " " + juce::String (cents > 0 ? "+" : "") + juce::String (cents) + " ct" : juce::String()));
            }
            menu.addItem (5, tr ("Normalize"), true, proc.param (pid::normalize)->getValue() > 0.5f);
            menu.addItem (6, tr ("Reset region"), regionStart() > 0.0f || regionEnd() < 1.0f);
            menu.addItem (10, tr ("Keep this sample when switching presets"), true, proc.getKeepSample());
            if (sample->file.existsAsFile())
                menu.addItem (7, tr ("Show in Finder"));
            menu.addItem (8, tr ("Remove sample"));
        }
        if (proc.getMissingSamplePath().isNotEmpty())
            menu.addItem (9, tr ("Find the missing file..."));

        juce::Component::SafePointer<WaveformView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sampleButton),
                            [safe, recentFiles, myFiles, sample] (int r)
                            {
                                if (safe == nullptr || r == 0)
                                    return;
                                auto& p = safe->ctx.processor;
                                auto setParam = [&p] (const char* id, float normalised)
                                {
                                    auto* param = p.param (id);
                                    param->beginChangeGesture();
                                    param->setValueNotifyingHost (normalised);
                                    param->endChangeGesture();
                                };
                                if (r == 1 || r == 9)      { if (safe->onLoadRequest) safe->onLoadRequest(); }
                                else if (r == 3)           library::userFolder().revealToUser();
                                else if (r == 4)           p.applyDetectedRoot();
                                else if (r == 5)           setParam (pid::normalize, p.param (pid::normalize)->getValue() > 0.5f ? 0.0f : 1.0f);
                                else if (r == 6)           { setParam (pid::regionStart, 0.0f); setParam (pid::regionEnd, 1.0f); }
                                else if (r == 7 && sample) sample->file.revealToUser();
                                else if (r == 8)           p.clearUserSample();
                                else if (r == 10)          p.setKeepSample (! p.getKeepSample());
                                else if (r >= 3000)        setParam (pid::source, p.param (pid::source)->convertTo0to1 ((float) (r - 3000)));
                                else if (r >= 2000)        p.loadSampleAsync (myFiles[r - 2000]);
                                else if (r >= 1000)        p.loadSampleAsync (recentFiles[r - 1000]);
                            });
    }
}
