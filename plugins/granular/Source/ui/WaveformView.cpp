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

    juce::Rectangle<float> WaveformView::minimapArea() const
    {
        // In the footer, between the position readout and the activity readout.
        const auto footer = getLocalBounds().toFloat().reduced (16.0f, 0.0f).removeFromBottom (30.0f);
        return footer.reduced (180.0f, 0.0f).withSizeKeepingCentre (footer.getWidth() - 360.0f, 10.0f);
    }

    // x <-> position (0..1 of the whole sample) through the zoomed view.
    float WaveformView::xToPosition (float x) const
    {
        const auto a = waveArea();
        return juce::jlimit (0.0f, 1.0f, (float) (view.start + (double) ((x - a.getX()) / a.getWidth()) * view.span()));
    }

    float WaveformView::positionToX (float p) const
    {
        const auto a = waveArea();
        return a.getX() + (float) (((double) p - view.start) / view.span()) * a.getWidth();
    }

    float WaveformView::snapHandle (float position, bool free) const
    {
        if (source == nullptr || free)
            return position;
        // An attack within 6 px wins; otherwise the nearest zero crossing within 2 ms.
        const auto length = (float) source->getLength();
        const auto x = positionToX (position);
        float best = -1.0f, bestDistance = 6.0f;
        for (const auto& o : source->getOnsets())
        {
            const auto d = std::abs (positionToX ((float) o.position / length) - x);
            if (d < bestDistance) { bestDistance = d; best = (float) o.position / length; }
        }
        if (best >= 0.0f)
            return best;
        const auto radius = (int) (0.002 * source->getSampleRate());
        return (float) source->nearestZeroCrossing ((int) std::lround (position * length), radius) / length;
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
            view = {};
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

        // Centre line and time ticks (1, 2, 5, 10... s apart, at least 60 px).
        g.setColour (glassGrid);
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        if (source != nullptr && source->getDurationSeconds() > 0.0)
        {
            const auto seconds = source->getDurationSeconds();
            const auto pxPerSecond = area.getWidth() / (seconds * view.span());
            double step = 0.001;
            for (double candidate : { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 60.0 })
                if ((step = candidate) * pxPerSecond >= 60.0)
                    break;
            for (auto t = std::ceil (view.start * seconds / step) * step; t < view.end * seconds; t += step)
            {
                const auto x = positionToX ((float) (t / seconds));
                g.drawVerticalLine ((int) x, area.getY(), area.getY() + 6.0f);
            }
        }

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

        // Waveform in its real level (as normalised when Normalize is on), min/max per pixel
        // column of the zoomed view.
        g.saveState();
        g.reduceClipRegion (area.toNearestInt().expanded (0, 24));
        const auto width = juce::jmax (1, (int) area.getWidth());
        const auto halfH = area.getHeight() * 0.48f;
        const auto scale = proc.param (pid::normalize)->getValue() > 0.5f ? source->getNormalGain() : 1.0f;
        columnMin.resize ((size_t) width);
        columnMax.resize ((size_t) width);
        source->getPeaks (view.start, view.end, width, columnMin.data(), columnMax.data());
        juce::Path wave;
        for (int x = 0; x < width; ++x)
        {
            const auto lo = juce::jlimit (-1.0f, 1.0f, columnMin[(size_t) x] * scale);
            const auto hi = juce::jlimit (-1.0f, 1.0f, columnMax[(size_t) x] * scale);
            wave.addRectangle (area.getX() + (float) x, area.getCentreY() - hi * halfH, 1.0f, juce::jmax (1.0f, (hi - lo) * halfH));
        }
        g.setColour (glassText.withAlpha (0.55f));
        g.fillPath (wave);

        // Attacks: short marks along the bottom edge.
        g.setColour (accentTeal.withAlpha (0.7f));
        for (const auto& o : source->getOnsets())
        {
            const auto x = positionToX ((float) o.position / (float) source->getLength());
            if (x >= area.getX() && x <= area.getRight())
                g.drawVerticalLine ((int) x, area.getBottom() - 5.0f, area.getBottom());
        }

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

        g.restoreState();

        // Whole-sample strip while zoomed in: the shown part is outlined.
        if (! view.isWhole())
        {
            const auto m = minimapArea();
            g.setColour (glassRaised);
            g.fillRoundedRectangle (m, 2.0f);
            const auto buckets = (int) source->getPeakMax().size();
            juce::Path strip;
            for (int x = 0; x < (int) m.getWidth(); ++x)
            {
                const auto b = x * buckets / juce::jmax (1, (int) m.getWidth());
                const auto h = juce::jlimit (0.0f, 1.0f, juce::jmax (source->getPeakMax()[(size_t) b], -source->getPeakMin()[(size_t) b]) * scale);
                strip.addRectangle (m.getX() + (float) x, m.getCentreY() - h * m.getHeight() * 0.5f, 1.0f, juce::jmax (1.0f, h * m.getHeight()));
            }
            g.setColour (glassDim);
            g.fillPath (strip);
            g.setColour (glassText);
            g.drawRect (juce::Rectangle<float> (m.getX() + (float) view.start * m.getWidth(), m.getY() - 1.0f,
                                                juce::jmax (2.0f, (float) view.span() * m.getWidth()), m.getHeight() + 2.0f), 1.0f);
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
        if (! view.isWhole() && minimapArea().expanded (0.0f, 4.0f).contains (e.position))
        {
            drag = Drag::minimap;
            mouseDrag (e);
            return;
        }
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
        axis = e.mods.isAltDown() ? Axis::spray : Axis::undecided;
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
        if (drag == Drag::minimap)
        {
            const auto m = minimapArea();
            const auto centre = (double) ((e.position.x - m.getX()) / m.getWidth());
            view = wave::clampView ({ centre - view.span() * 0.5, centre + view.span() * 0.5 },
                                    wave::minimumViewSpan (source != nullptr ? source->getLength() : 1));
            repaint();
            return;
        }
        if (drag == Drag::regionStart || drag == Drag::regionEnd)
        {
            // Handles never cross; they snap to attacks and zero crossings (Alt: free).
            const auto minGap = source != nullptr ? wave::minimumRegion (source->getDurationSeconds(), source->getSampleRate()) : 0.005;
            const auto x = snapHandle (xToPosition ((float) e.x), e.mods.isAltDown());
            if (drag == Drag::regionStart)
                proc.param (pid::regionStart)->setValueNotifyingHost ((float) wave::clampHandle (x, regionEnd(), true, minGap));
            else
                proc.param (pid::regionEnd)->setValueNotifyingHost ((float) wave::clampHandle (x, regionStart(), false, minGap));
            return;
        }
        if (! dragging)
            return;
        // The first 6 px decide: sideways = Position, up/down = Spray (Alt: Spray at once).
        if (axis == Axis::undecided && e.getDistanceFromDragStart() >= 6)
            axis = std::abs (e.getDistanceFromDragStartX()) >= std::abs (e.getDistanceFromDragStartY()) ? Axis::position : Axis::spray;
        if (axis == Axis::position || axis == Axis::undecided)
        {
            proc.param (pid::position)->setValueNotifyingHost (toRelative (xToPosition ((float) e.x)));
            proc.requestScanReset();
        }
        else
        {
            const auto dy = (float) (dragStartY - e.y) / waveArea().getHeight();
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
        // Without a modifier the wheel zooms (sideways scroll pans): never the sound.
        if (! e.mods.isCommandDown() && ! e.mods.isShiftDown())
        {
            if (source == nullptr)
                return;
            const auto minSpan = wave::minimumViewSpan (source->getLength());
            if (std::abs (w.deltaX) > std::abs (w.deltaY))
                view = wave::pan (view, -w.deltaX * view.span(), minSpan);
            else
                view = wave::zoomAround (view, xToPosition ((float) e.x), std::exp (-w.deltaY * 2.0), minSpan);
            repaint();
            return;
        }
        auto* p = ctx.processor.param (e.mods.isShiftDown() ? pid::spray : pid::size);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.05f));
        p->endChangeGesture();
        if (ctx.onFocus) ctx.onFocus (p->getParameterID());
    }

    void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        // Whole sample; from the whole sample, the region.
        if (view.isWhole() && (regionStart() > 0.0f || regionEnd() < 1.0f))
            view = wave::clampView ({ regionStart(), regionEnd() }, wave::minimumViewSpan (source != nullptr ? source->getLength() : 1));
        else
            view = {};
        juce::ignoreUnused (e);
        repaint();
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
                                if (r >= 100) proc.setCue (r - 100, toRelative (snapHandle (where, false)));
                            });
    }

    void WaveformView::stepSample (int delta)
    {
        auto& proc = ctx.processor;
        if (sourceChoice == 0)
        {
            // Own samples: the next file of a dropped set, or the neighbour in the same folder.
            proc.stepSample (delta);
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

        const auto favourites = library::favourites();
        juce::PopupMenu menu, recentMenu, myMenu, builtIns, favouriteMenu;
        menu.addItem (1, tr ("Load sample..."));
        for (int i = 0; i < recentFiles.size(); ++i)
            recentMenu.addItem (1000 + i, recentFiles[i].getFileName());
        menu.addSubMenu (tr ("Recent"), recentMenu, ! recentFiles.isEmpty());
        for (int i = 0; i < juce::jmin (200, myFiles.size()); ++i)
            myMenu.addItem (2000 + i, myFiles[i].getFileName());
        if (! myFiles.isEmpty()) myMenu.addSeparator();
        myMenu.addItem (3, tr ("Open the folder"));
        menu.addSubMenu (tr ("My samples"), myMenu);
        for (int i = 0; i < favourites.size(); ++i)
            favouriteMenu.addItem (4000 + i, favourites[i].getFileName());
        menu.addSubMenu (tr ("Favourites"), favouriteMenu, ! favourites.isEmpty());
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
            menu.addItem (11, tr ("Cues on the attacks"), ! sample->getOnsets().empty());
            menu.addItem (12, tr ("Trim to region"), regionStart() > 0.0f || regionEnd() < 1.0f);
            if (sample->file.existsAsFile())
                menu.addItem (13, tr ("Favourite"), true, library::isFavourite (sample->file));
            menu.addItem (10, tr ("Keep this sample when switching presets"), true, proc.getKeepSample());
            if (sample->file.existsAsFile())
                menu.addItem (7, tr ("Show in Finder"));
            menu.addItem (8, tr ("Remove sample"));
        }
        if (proc.getMissingSamplePath().isNotEmpty())
            menu.addItem (9, tr ("Find the missing file..."));
        if (proc.isPreviewing())
            menu.addItem (14, tr ("Stop listening"));

        juce::Component::SafePointer<WaveformView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sampleButton),
                            [safe, recentFiles, myFiles, favourites, sample] (int r)
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
                                else if (r == 11)          p.sliceCuesFromAttacks();
                                else if (r == 12)          { juce::String error; p.trimToRegion (error); }
                                else if (r == 13 && sample) library::setFavourite (sample->file, ! library::isFavourite (sample->file));
                                else if (r == 14)          p.stopPreview();
                                else if (r >= 3000 && r < 4000) setParam (pid::source, p.param (pid::source)->convertTo0to1 ((float) (r - 3000)));
                                else
                                {
                                    // A file: Alt listens to it without loading.
                                    juce::File file;
                                    if (r >= 4000)      file = favourites[r - 4000];
                                    else if (r >= 2000) file = myFiles[r - 2000];
                                    else if (r >= 1000) file = recentFiles[r - 1000];
                                    if (! file.existsAsFile())
                                        return;
                                    if (juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown())
                                        p.previewFile (file);
                                    else
                                    {
                                        p.stopPreview();
                                        p.loadSampleAsync (file);
                                    }
                                }
                            });
    }
}
