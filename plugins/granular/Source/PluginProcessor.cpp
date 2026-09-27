#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SampleLibrary.h"

namespace thf::grain
{
    //==============================================================================
    FactorySources::FactorySources()
    {
        jassert (count == sourceChoices.size());
        for (auto& p : published)
            p.store (nullptr);
        worker = std::thread ([this]
        {
            for (int i = 1; i < (int) owned.size(); ++i)
            {
                owned[(size_t) i] = sources::generate (i);
                published[(size_t) i].store (owned[(size_t) i].get(), std::memory_order_release);
            }
        });
    }

    FactorySources::~FactorySources()
    {
        if (worker.joinable())
            worker.join();
    }

    const SourceData* FactorySources::get (int choice) const noexcept
    {
        if (choice <= 0 || choice >= (int) published.size())
            return nullptr;
        return published[(size_t) choice].load (std::memory_order_acquire);
    }

    void FactorySources::waitUntilReady() const
    {
        while (published.back().load (std::memory_order_acquire) == nullptr)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }

    namespace
    {
        // True while this thread is inside processBlock: parameter writes from there go
        // through the hardware hand-over instead of talking to the host directly.
        thread_local bool inProcessBlock = false;
    }

    //==============================================================================
    struct GrainProcessor::Raw
    {
        explicit Raw (juce::AudioProcessorValueTreeState& s)
        {
            auto get = [&s] (const char* id) { return s.getRawParameterValue (id); };
            source = get (pid::source); root = get (pid::root); position = get (pid::position);
            scan = get (pid::scan); scanMode = get (pid::scanMode); freeze = get (pid::freeze);
            spray = get (pid::spray); size = get (pid::size); density = get (pid::density);
            sync = get (pid::sync); syncRate = get (pid::syncRate); chaos = get (pid::chaos);
            window = get (pid::window); pitch = get (pid::pitch); fine = get (pid::fine);
            jitter = get (pid::jitter); reverse = get (pid::reverse); stereo = get (pid::stereo);
            quantize = get (pid::quantize); lfoMode = get (pid::lfoMode); lfoDivision = get (pid::lfoDivision);
            space = get (pid::space); spaceSize = get (pid::spaceSize);
            regionStart = get (pid::regionStart); regionEnd = get (pid::regionEnd); normalize = get (pid::normalize);
            voices = get (pid::voices); voiceMode = get (pid::voiceMode); glide = get (pid::glide);
            hold = get (pid::hold); bendRange = get (pid::bendRange); velocity = get (pid::velocity);
            attack = get (pid::attack); decay = get (pid::decay); sustain = get (pid::sustain);
            release = get (pid::release); filterType = get (pid::filterType); cutoff = get (pid::cutoff);
            resonance = get (pid::resonance); filterEnv = get (pid::filterEnv);
            filterDecay = get (pid::filterDecay); drive = get (pid::drive); lfoRate = get (pid::lfoRate);
            lfoDepth = get (pid::lfoDepth); lfoShape = get (pid::lfoShape); lfoTarget = get (pid::lfoTarget);
            modTarget = get (pid::modTarget); modDepth = get (pid::modDepth); output = get (pid::output);
            safeClip = get (pid::safeClip); hq = get (pid::hq); linkVoices = get (pid::linkVoices);
        }

        std::atomic<float> *source, *root, *position, *scan, *scanMode, *freeze, *spray, *size, *density,
            *sync, *syncRate, *chaos, *window, *pitch, *fine, *jitter, *reverse, *stereo, *voices,
            *voiceMode, *glide, *hold, *bendRange, *velocity, *attack, *decay, *sustain, *release,
            *filterType, *cutoff, *resonance, *filterEnv, *filterDecay, *drive, *lfoRate, *lfoDepth,
            *lfoShape, *lfoTarget, *modTarget, *modDepth, *output, *safeClip, *hq,
            *quantize, *lfoMode, *lfoDivision, *space, *spaceSize, *regionStart, *regionEnd, *normalize,
            *linkVoices;
    };

    //==============================================================================
    GrainProcessor::GrainProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "PARAMS", createParameterLayout())   // undo is our own (see recordChange)
    {
        for (auto* p : getParameters())
            params.push_back (dynamic_cast<juce::RangedAudioParameter*> (p));
        raw = std::make_unique<Raw> (state);

        auto lookup = [this] (std::string_view id) { return param (juce::String (std::string (id))); };
        for (int pg = 0; pg < layout::numPages; ++pg)
            for (int slot = 0; slot < layout::numEncoders; ++slot)
                encoderSlots[(size_t) pg][(size_t) slot] = lookup (layout::encoderParams[(size_t) pg][(size_t) slot]);
        for (int slot = 0; slot < layout::numFaders; ++slot)
            faderSlots[(size_t) slot] = lookup (layout::faderParams[(size_t) slot]);
        mainEncoderSlot = lookup (layout::mainEncoderParam);
        positionParam = param (pid::position);

        for (auto& c : cues) c.store (-1.0f);
        for (auto& l : learned) l.store (-1);

        const auto count = params.size();
        pendingHardware.reset (new std::atomic<float>[count]);
        queuedHardware.reset (new std::atomic<bool>[count]);
        for (size_t i = 0; i < count; ++i)
        {
            rawValues.push_back (state.getRawParameterValue (params[i]->getParameterID()));
            pendingHardware[i].store (-1.0f);
            queuedHardware[i].store (false);
            params[i]->addListener (this);
        }
        gestureOpen.assign (count, false);
        lastHardwareMove.assign (count, 0);
        gestureStartValue.assign (count, 0.0f);

        undoManager.setMaxNumberOfStoredUnits (400, 20);
        startTimerHz (30);
    }

    GrainProcessor::~GrainProcessor()
    {
        *alive = false;
        stopTimer();
        loader.removeAllJobs (true, 5000);
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (gestureOpen[i])
                params[i]->endChangeGesture();   // a hardware move still "held"
            params[i]->removeListener (this);
        }
    }

    int GrainProcessor::indexOf (juce::StringRef id) const
    {
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i]->getParameterID() == id)
                return (int) i;
        return -1;
    }

    bool GrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        return layouts.getMainInputChannelSet().isDisabled()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    double GrainProcessor::getTailLengthSeconds() const
    {
        return (raw->release->load() + raw->size->load()) * 0.001;
    }

    void GrainProcessor::prepareToPlay (double sampleRate, int)
    {
        const auto previousRate = hostSampleRate.exchange (sampleRate);
        if (std::abs (previousRate - sampleRate) > 0.5 && getUserSample() != nullptr)
            reloadForSampleRate();
        engine.prepare (sampleRate);
        for (auto& p : pickups) p.reset();
    }

    EngineParams GrainProcessor::makeEngineParams() const
    {
        const auto& r = *raw;
        EngineParams p;
        p.position = r.position->load();
        p.regionStart = r.regionStart->load();
        p.regionEnd = r.regionEnd->load();
        p.normalizeSource = r.normalize->load() > 0.5f;
        p.scan = r.scan->load();
        p.perNoteScan = r.scanMode->load() > 0.5f;
        p.freeze = r.freeze->load() > 0.5f;
        p.spray = r.spray->load();
        p.sizeMs = r.size->load();
        p.density = r.density->load();
        p.sync = r.sync->load() > 0.5f;
        p.linkVoices = r.linkVoices->load() > 0.5f;
        p.bpm = hostBpm;
        p.syncBeats = syncRateBeats[juce::jlimit (0, (int) std::size (syncRateBeats) - 1, (int) r.syncRate->load())];
        p.chaos = r.chaos->load();
        p.window = r.window->load();
        p.pitch = r.pitch->load() + r.fine->load() * 0.01f;
        p.jitter = r.jitter->load();
        p.reverse = r.reverse->load();
        p.stereo = r.stereo->load();
        p.quantize = (int) r.quantize->load();
        p.lfoSync = r.lfoMode->load() > 0.5f;
        p.lfoBeats = lfoDivisionBeats[juce::jlimit (0, (int) std::size (lfoDivisionBeats) - 1, (int) r.lfoDivision->load())];
        p.space = r.space->load();
        p.spaceSize = r.spaceSize->load();
        p.root = (int) r.root->load();
        p.voices = (int) r.voices->load();
        p.voiceMode = (int) r.voiceMode->load();
        p.glideMs = r.glide->load();
        p.bendRange = r.bendRange->load();
        p.velocitySens = r.velocity->load();
        p.attackMs = r.attack->load();
        p.decayMs = r.decay->load();
        p.sustain = r.sustain->load();
        p.releaseMs = r.release->load();
        p.filterType = (int) r.filterType->load();
        p.cutoff = r.cutoff->load();
        p.resonance = r.resonance->load();
        p.filterEnv = r.filterEnv->load();
        p.filterDecayMs = r.filterDecay->load();
        p.drive = r.drive->load();
        p.lfoRate = r.lfoRate->load();
        p.lfoDepth = r.lfoDepth->load();
        p.lfoShape = (int) r.lfoShape->load();
        p.lfoTarget = (int) r.lfoTarget->load();
        p.modTarget = (int) r.modTarget->load();
        p.modDepth = r.modDepth->load();
        const auto outDb = r.output->load();
        p.outputGain = outDb <= -35.95f ? 0.0f : juce::Decibels::decibelsToGain (outDb);
        p.safeClip = r.safeClip->load() > 0.5f;
        p.hq = r.hq->load() > 0.5f;
        return p;
    }

    //==============================================================================
    void GrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        juce::ScopedNoDenormals noDenormals;
        const juce::ScopedValueSetter<bool> audioScope (inProcessBlock, true);
        const auto numSamples = buffer.getNumSamples();

        // Source: user sample (try-lock, never block) or a built-in one.
        {
            const juce::SpinLock::ScopedTryLockType lock (sourceLock);
            if (lock.isLocked() && audioSource != userSample)
                audioSource = userSample;
        }
        const auto sourceChoice = (int) raw->source->load();
        engine.setSource (sourceChoice == 0 ? audioSource.get() : factory->get (sourceChoice));

        // Transport: tempo for Sync and the LFO, song position for the beat grid.
        {
            bool playing = false;
            double ppq = 0.0;
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                {
                    if (auto bpm = pos->getBpm())
                        hostBpm = juce::jlimit (20.0, 999.0, *bpm);
                    if (auto position = pos->getPpqPosition(); position && pos->getIsPlaying())
                    {
                        playing = true;
                        ppq = *position;
                    }
                }
            engine.setTransport (playing, ppq);
            if (playing && raw->lfoMode->load() > 0.5f)
                engine.syncLfo (ppq, lfoDivisionBeats[juce::jlimit (0, (int) std::size (lfoDivisionBeats) - 1, (int) raw->lfoDivision->load())]);
        }
        auto engineParams = makeEngineParams();
        engine.setHold (raw->hold->load() > 0.5f);
        if (scanResetRequested.exchange (false))
            engine.resetScan();

        // Adds notes played on the on-screen / computer keyboard and records incoming ones
        // so the keyboard lights up.
        keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        float scratch[GrainEngine::maxControlBlock];

        // Render between MIDI events so notes and controls land sample-accurately.
        int rendered = 0;
        auto renderTo = [&] (int end)
        {
            while (rendered < end)
            {
                const auto n = juce::jmin (end - rendered, GrainEngine::maxControlBlock);
                engine.render (left + rendered, right != nullptr ? right + rendered : scratch, n, engineParams);
                rendered += n;
            }
        };

        for (const auto metadata : midiMessages)
        {
            renderTo (juce::jlimit (0, numSamples, metadata.samplePosition));
            handleMidi (metadata.getMessage(), engineParams);
            engineParams = makeEngineParams();   // hardware may have moved parameters
            engine.setHold (raw->hold->load() > 0.5f);
        }
        renderTo (numSamples);
        midiMessages.clear();

        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        auto updatePeak = [] (std::atomic<float>& peak, float value)
        {
            auto current = peak.load();
            while (value > current && ! peak.compare_exchange_weak (current, value)) {}
        };
        updatePeak (peakL, buffer.getMagnitude (0, 0, numSamples));
        if (right != nullptr)
            updatePeak (peakR, buffer.getMagnitude (1, 0, numSamples));
        samplesProcessed += numSamples;
    }

    //==============================================================================
    // MIDI: controller template, learned CCs, pads, strips.
    void GrainProcessor::handleMidi (const juce::MidiMessage& m, const EngineParams& p)
    {
        if (m.isNoteOnOrOff())
        {
            const auto note = m.getNoteNumber();
            if (padsAsControls.load() && m.getChannel() == padChannel.load())
            {
                using namespace layout::minilab3;
                if (note >= padBankANote && note < padBankANote + layout::numPads)
                {
                    handlePad (note - padBankANote, m.isNoteOn(), m.getFloatVelocity());
                    return;
                }
                if (note >= padBankBNote && note < padBankBNote + layout::numPads)
                {
                    handlePad (layout::numPads + note - padBankBNote, m.isNoteOn(), m.getFloatVelocity());
                    return;
                }
            }
            if (m.isNoteOn())
                engine.noteOn (note, m.getFloatVelocity(), p);
            else
                engine.noteOff (note, p);
            return;
        }

        if (m.isPitchWheel())
        {
            const auto v = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            engine.setPitchBend (juce::jlimit (-1.0f, 1.0f, v));
            pitchStrip.store (juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * v));
            return;
        }

        if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            engine.allNotesOff (m.isAllSoundOff());
            return;
        }

        if (m.isController())
            handleController (m.getControllerNumber(), m.getControllerValue());
    }

    void GrainProcessor::touched (juce::RangedAudioParameter* p, int)
    {
        const auto it = std::find (params.begin(), params.end(), p);
        lastTouchedParam.store (it != params.end() ? (int) (it - params.begin()) : -1);
        lastTouchedSerial.fetch_add (1);
    }

    float GrainProcessor::currentValue (const juce::RangedAudioParameter* p) const noexcept
    {
        // A hardware move not yet flushed to the host is the value that counts.
        const auto pending = pendingHardware[(size_t) p->getParameterIndex()].load();
        return pending >= 0.0f ? pending : p->getValue();
    }

    void GrainProcessor::setParamFromAudio (juce::RangedAudioParameter* p, float normalised)
    {
        const auto& range = p->getNormalisableRange();
        normalised = range.convertTo0to1 (range.snapToLegalValue (range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised))));
        if (std::abs (currentValue (p) - normalised) <= 1.0e-6f)
            return;

        // Outside the audio callback (clicks on the on-screen pads) the host is told directly.
        if (! inProcessBlock)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
            return;
        }

        // Audio thread: the engine hears the new value at once (raw value), the host is told
        // from the message thread inside a gesture (flushHardwareChanges). No locks, no
        // allocations, no host calls here.
        const auto index = (size_t) p->getParameterIndex();
        pendingHardware[index].store (normalised);
        rawValues[index]->store (range.convertFrom0to1 (normalised));
        if (! queuedHardware[index].exchange (true))
        {
            const auto w = hardwareWrite.load (std::memory_order_relaxed);
            if (w - hardwareRead.load (std::memory_order_acquire) < (uint32_t) hardwareRing.size())
            {
                hardwareRing[w % hardwareRing.size()] = (int) index;
                hardwareWrite.store (w + 1, std::memory_order_release);
            }
            else
            {
                queuedHardware[index].store (false);
            }
        }
    }

    void GrainProcessor::flushHardwareChanges()
    {
        const auto now = juce::Time::getMillisecondCounter();
        auto r = hardwareRead.load (std::memory_order_relaxed);
        const auto w = hardwareWrite.load (std::memory_order_acquire);
        for (; r != w; ++r)
        {
            const auto index = (size_t) hardwareRing[r % hardwareRing.size()];
            queuedHardware[index].store (false);
            auto value = pendingHardware[index].load();
            if (value < 0.0f)
                continue;
            auto* p = params[index];
            if (! gestureOpen[index])
            {
                p->beginChangeGesture();
                gestureOpen[index] = true;
            }
            p->setValueNotifyingHost (value);
            lastHardwareMove[index] = now;
            pendingHardware[index].compare_exchange_strong (value, -1.0f);   // unless moved again meanwhile
        }
        hardwareRead.store (r, std::memory_order_release);

        // A control that has been still for 300 ms ends its gesture (one undo step, one
        // automation touch).
        for (size_t i = 0; i < gestureOpen.size(); ++i)
            if (gestureOpen[i] && now - lastHardwareMove[i] > 300 && pendingHardware[i].load() < 0.0f)
            {
                params[i]->endChangeGesture();
                gestureOpen[i] = false;
            }
    }

    void GrainProcessor::nudgeParam (juce::RangedAudioParameter* p, int ticks)
    {
        // Discrete parameters move one step per tick; continuous ones 0.5 % of the range.
        const auto& range = p->getNormalisableRange();
        int steps = 0;
        if (range.interval > 0.0f)
            steps = juce::roundToInt ((range.end - range.start) / range.interval) + 1;
        else if (p->isDiscrete())
            steps = p->getNumSteps();

        if (steps > 1 && steps <= 200)
            setParamFromAudio (p, currentValue (p) + (float) ticks / (float) (steps - 1));
        else
            setParamFromAudio (p, currentValue (p) + (float) ticks * 0.005f);
    }

    bool GrainProcessor::handleController (int cc, int value)
    {
        using namespace layout::minilab3;
        const auto normalised = (float) value / 127.0f;

        // MIDI learn takes the next CC.
        if (const auto target = learnTarget.load(); target >= 0)
        {
            for (auto& l : learned)
            {
                auto expected = target;
                l.compare_exchange_strong (expected, -1);
            }
            learned[(size_t) cc].store (target);
            learnTarget.store (-1);
            pickups[(size_t) cc].reset();
            touched (params[(size_t) target], cc);
            return true;
        }

        const auto mode = getEncoderMode();
        auto isTemplateEncoder = [&]
        {
            for (auto e : encoderCC) if (e == cc) return true;
            return cc == mainEncoderCC;
        };

        if (const auto index = learned[(size_t) cc].load(); index >= 0 && index < (int) params.size())
        {
            auto* p = params[(size_t) index];
            if (isTemplateEncoder() && mode != midi::EncoderMode::absolute)
                nudgeParam (p, midi::decodeRelative (value, mode));
            else if (pickups[(size_t) cc].process (normalised, currentValue (p)))
                setParamFromAudio (p, normalised);
            touched (p, cc);
            return true;
        }

        for (int slot = 0; slot < layout::numEncoders; ++slot)
        {
            if (encoderCC[(size_t) slot] != cc)
                continue;
            auto* p = encoderSlots[(size_t) page.load()][(size_t) slot];
            if (mode == midi::EncoderMode::absolute)
            {
                if (pickups[(size_t) cc].process (normalised, currentValue (p)))
                    setParamFromAudio (p, normalised);
            }
            else
            {
                nudgeParam (p, midi::decodeRelative (value, mode));
            }
            touched (p, cc);
            return true;
        }

        for (int slot = 0; slot < layout::numFaders; ++slot)
        {
            if (faderCC[(size_t) slot] != cc)
                continue;
            auto* p = faderSlots[(size_t) slot];
            if (pickups[(size_t) cc].process (normalised, currentValue (p)))
                setParamFromAudio (p, normalised);
            touched (p, cc);
            return true;
        }

        if (cc == mainEncoderCC)
        {
            // Fine position: 0.2 % per tick; the encoder accelerates by itself.
            auto* p = mainEncoderSlot;
            const auto ticks = midi::decodeRelative (value, midi::EncoderMode::binaryOffset);
            setParamFromAudio (p, currentValue (p) + (float) ticks * 0.002f);
            touched (p, cc);
            return true;
        }

        if (cc == mainClickCC)
        {
            if (value >= 64)
                page.store ((page.load() + 1) % layout::numPages);
            return true;
        }

        if (cc == modStripCC)
        {
            engine.setModWheel (normalised);
            modStrip.store (normalised);
            return true;
        }

        if (cc == sustainCC)
        {
            engine.setSustainPedal (value >= 64);
            return true;
        }
        return false;
    }

    void GrainProcessor::handlePad (int pad, bool noteOn, float)
    {
        if (noteOn)
        {
            lastPad.store (pad);
            lastPadSerial.fetch_add (1);
        }

        if (pad >= layout::numPads)
        {
            // Bank B: cues. Empty pad: store on release. Filled pad: jump on press; holding
            // for 0.6 s overwrites it with the position the playhead had before the jump.
            const auto i = pad - layout::numPads;
            auto* position = positionParam;
            if (noteOn)
            {
                cuePressTime[(size_t) i] = samplesProcessed;
                cuePressPlayhead[(size_t) i] = engine.getPlayhead();
                if (const auto cue = cues[(size_t) i].load(); cue >= 0.0f)
                {
                    setParamFromAudio (position, cue);
                    engine.resetScan();
                }
            }
            else
            {
                const bool empty = cues[(size_t) i].load() < 0.0f;
                const bool longPress = (double) (samplesProcessed - cuePressTime[(size_t) i]) > 0.6 * hostSampleRate;
                if (empty || longPress)
                {
                    const auto where = cuePressPlayhead[(size_t) i];
                    cues[(size_t) i].store (where);
                    setParamFromAudio (position, where);
                    engine.resetScan();
                }
            }
            return;
        }

        if (noteOn)
            performPadAction (pad);
    }

    void GrainProcessor::performPadAction (int pad)
    {
        auto toggle = [this] (const char* id)
        {
            auto* p = param (id);
            setParamFromAudio (p, currentValue (p) > 0.5f ? 0.0f : 1.0f);
            touched (p, -1);
        };
        // Cycles through the real entries only (the lists are longer, see reservedChoices).
        auto cycle = [this] (const char* id, int realCount)
        {
            auto* p = param (id);
            const auto index = juce::roundToInt (p->convertFrom0to1 (currentValue (p)));
            setParamFromAudio (p, p->convertTo0to1 ((float) ((index + 1) % realCount)));
            touched (p, -1);
        };

        switch (layout::padsBankA[(size_t) juce::jlimit (0, layout::numPads - 1, pad)])
        {
            case layout::PadAction::freeze:         toggle (pid::freeze); break;
            case layout::PadAction::hold:           toggle (pid::hold); break;
            case layout::PadAction::reverse:        toggle (pid::reverse); break;
            case layout::PadAction::sync:           toggle (pid::sync); break;
            case layout::PadAction::filterCycle:    cycle (pid::filterType, filterChoices.size()); break;
            case layout::PadAction::voiceModeCycle: cycle (pid::voiceMode, voiceModeChoices.size()); break;
            case layout::PadAction::abToggle:       abRequested.store (true); break;
            case layout::PadAction::windowCycle:
            {
                auto* p = param (pid::window);
                const auto v = currentValue (p);
                setParamFromAudio (p, v < 0.25f ? 0.5f : (v < 0.75f ? 1.0f : 0.0f));
                touched (p, -1);
                break;
            }
        }
    }

    void GrainProcessor::jumpToCue (int index)
    {
        const auto cue = cues[(size_t) index].load();
        if (cue < 0.0f)
            return;
        if (auto* p = param (pid::position))
            p->setValueNotifyingHost (cue);
        requestScanReset();
    }

    void GrainProcessor::startLearn (const juce::String& id)
    {
        learnTarget.store (indexOf (id));
    }

    juce::String GrainProcessor::getLearningParam() const
    {
        const auto t = learnTarget.load();
        return t >= 0 ? params[(size_t) t]->getParameterID() : juce::String();
    }

    void GrainProcessor::clearLearned (const juce::String& id)
    {
        const auto index = indexOf (id);
        for (auto& l : learned)
        {
            auto expected = index;
            l.compare_exchange_strong (expected, -1);
        }
    }

    void GrainProcessor::clearAllLearned()
    {
        for (auto& l : learned) l.store (-1);
    }

    bool GrainProcessor::isLearned (const juce::String& id) const
    {
        const auto index = indexOf (id);
        for (auto& l : learned)
            if (l.load() == index) return true;
        return false;
    }

    int GrainProcessor::getCcFor (const juce::String& id) const
    {
        const auto index = indexOf (id);
        for (int cc = 0; cc < 128; ++cc)
            if (learned[(size_t) cc].load() == index)
                return cc;

        using namespace layout;
        for (int slot = 0; slot < numEncoders; ++slot)
            if (id == juce::String (std::string (encoderParams[(size_t) page.load()][(size_t) slot])))
                return minilab3::encoderCC[(size_t) slot];
        for (int slot = 0; slot < numFaders; ++slot)
            if (id == juce::String (std::string (faderParams[(size_t) slot])))
                return minilab3::faderCC[(size_t) slot];
        if (id == juce::String (std::string (mainEncoderParam)))
            return minilab3::mainEncoderCC;
        return -1;
    }

    //==============================================================================
    // Samples.
    void GrainProcessor::publishSource (SourceData::Ptr s)
    {
        {
            const juce::SpinLock::ScopedLockType lock (sourceLock);
            if (userSample != nullptr)
                retired.push_back (userSample);
            userSample = s;
        }
        {
            const juce::ScopedLock l (statusLock);
            missingSamplePath.clear();
            lastLoadError.clear();
        }
        sourceSerial.fetch_add (1);
    }

    bool GrainProcessor::isSampleLocked() const
    {
        return keepSample.load() && getUserSample() != nullptr && raw->source->load() < 0.5f;
    }

    void GrainProcessor::setUserSample (SourceData::Ptr s)
    {
        const auto before = captureSnapshot (true);
        const auto previous = getUserSample();

        // Cues and region belong to a sample: remember the old sample's, bring back the new
        // one's if it was used before in this session, otherwise start clean.
        if (s != nullptr && (previous == nullptr || previous->contentHash != s->contentHash))
        {
            if (previous != nullptr)
            {
                SampleMemory m;
                for (size_t i = 0; i < cues.size(); ++i) m.cues[i] = cues[i].load();
                m.regionStart = param (pid::regionStart)->getValue();
                m.regionEnd = param (pid::regionEnd)->getValue();
                sampleMemory[previous->contentHash] = m;
            }
            SampleMemory next;
            next.cues.fill (-1.0f);
            if (auto it = sampleMemory.find (s->contentHash); it != sampleMemory.end())
                next = it->second;
            for (size_t i = 0; i < cues.size(); ++i) cues[i].store (next.cues[i]);
            for (auto [id, value] : { std::pair { pid::regionStart, next.regionStart }, std::pair { pid::regionEnd, next.regionEnd } })
            {
                auto* p = param (id);
                p->beginChangeGesture();
                p->setValueNotifyingHost (value);
                p->endChangeGesture();
            }
        }
        publishSource (s);
        if (auto* p = param (pid::source))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (0.0f);    // "Sample"
            p->endChangeGesture();
        }
        recordChange ("Load sample", before);
    }

    SourceData::Ptr GrainProcessor::getUserSample() const
    {
        const juce::SpinLock::ScopedLockType lock (sourceLock);
        return userSample;
    }

    SourceData::Ptr GrainProcessor::getCurrentSourceForDisplay() const
    {
        const auto choice = (int) raw->source->load();
        if (choice == 0)
            return getUserSample();
        return const_cast<SourceData*> (factory->get (choice));
    }

    namespace
    {
        // Samples too long to travel inside the session are copied once into the user's
        // library (~/Music/thf Grain Samples/Library), and the session points there: moving
        // or deleting the original never breaks the project.
        void keepInLibrary (SourceData& s)
        {
            if (s.embeddedFlac.getSize() > 0 || ! s.file.existsAsFile() || s.file.isAChildOf (library::userFolder()))
                return;
            auto folder = library::userFolder().getChildFile ("Library");
            folder.createDirectory();
            const auto copy = folder.getChildFile (s.contentHash.substring (0, 12) + " " + s.file.getFileName());
            if (copy.existsAsFile() || s.file.copyFileTo (copy))
                s.file = copy;
        }
    }

    sources::LoadOptions GrainProcessor::loadOptions (juce::uint32 generation) const
    {
        sources::LoadOptions options;
        options.targetRate = hostSampleRate.load();
        options.cancelled = [this, generation] { return generation != 0 && generation != loadGeneration.load(); };
        return options;
    }

    bool GrainProcessor::loadSampleSync (const juce::File& file, juce::String& error)
    {
        auto s = sources::loadFile (file, error, loadOptions (0));
        if (s == nullptr)
            return false;
        keepInLibrary (*s);
        setUserSample (s);
        library::addRecent (file);
        return true;
    }

    void GrainProcessor::reloadForSampleRate()
    {
        // The host rate changed: rebuild the sample from its original audio (embedded copy
        // or file) at the new rate, in the background. Until then the old data plays, in tune.
        const auto current = getUserSample();
        if (current == nullptr)
            return;
        const auto generation = ++loadGeneration;
        std::weak_ptr<bool> token = alive;
        loader.addJob ([this, current, generation, token]
        {
            juce::String error;
            const auto options = loadOptions (generation);
            SourceData::Ptr rebuilt;
            if (current->embeddedFlac.getSize() > 0)
                rebuilt = sources::loadFromMemory (current->embeddedFlac.getData(), current->embeddedFlac.getSize(),
                                                   current->getName(), error, options);
            else if (current->file.existsAsFile())
                rebuilt = sources::loadFile (current->file, error, options);
            if (rebuilt == nullptr || token.expired() || generation != loadGeneration.load())
                return;
            rebuilt->file = current->file;
            publishSource (rebuilt);
        });
    }

    void GrainProcessor::clearUserSample()
    {
        publishSource (nullptr);
    }

    bool GrainProcessor::applyDetectedRoot()
    {
        const auto s = getUserSample();
        if (s == nullptr)
            return false;
        // The file's own root note, or the pitch of the part in use (the region).
        auto note = s->detectedNote;
        const auto start = param (pid::regionStart)->getValue(), end = param (pid::regionEnd)->getValue();
        if (! s->pitchFromFile && (start > 0.0f || end < 1.0f))
            note = sources::estimatePitch (*s, start, end).note;
        if (note < 0.0f)
            return false;
        const auto root = juce::roundToInt (note);
        auto set = [this] (const char* id, float value)
        {
            auto* p = param (id);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        };
        undoManager.beginNewTransaction();
        set (pid::root, (float) juce::jlimit (0, 127, root));
        set (pid::fine, juce::jlimit (-100.0f, 100.0f, -(note - (float) root) * 100.0f));
        return true;
    }

    void GrainProcessor::loadSampleAsync (const juce::File& file)
    {
        loading.store (true);
        {
            const juce::ScopedLock l (statusLock);
            lastLoadError.clear();
        }
        sourceSerial.fetch_add (1);
        // Only the newest request (or a session/preset restore after it) may land.
        const auto generation = ++loadGeneration;
        std::weak_ptr<bool> token = alive;
        loader.addJob ([this, file, token, generation]
        {
            juce::String error;
            auto s = sources::loadFile (file, error, loadOptions (generation));
            if (s != nullptr)
                keepInLibrary (*s);
            juce::MessageManager::callAsync ([this, s, error, token, generation]
            {
                if (token.expired() || generation != loadGeneration.load())
                    return;
                loading.store (false);
                if (s != nullptr)
                {
                    setUserSample (s);
                    library::addRecent (s->file);
                }
                else
                {
                    const juce::ScopedLock l (statusLock);
                    lastLoadError = error;
                }
                sourceSerial.fetch_add (1);
            });
        });
    }

    //==============================================================================
    //==============================================================================
    // Undo and A/B.
    namespace
    {
        // One parameter moved by one gesture (mouse drag, hardware move).
        struct ParamAction : juce::UndoableAction
        {
            ParamAction (GrainProcessor& p, int i, float from, float to) : processor (p), index (i), before (from), after (to) {}
            bool perform() override { return set (after); }
            bool undo() override    { return set (before); }
            int getSizeInUnits() override { return 1; }

            bool set (float value)
            {
                auto snapshot = processor.captureSnapshot (false);
                snapshot.params[(size_t) index] = value;
                processor.applySnapshot (snapshot);
                return true;
            }

            GrainProcessor& processor;
            int index;
            float before, after;
        };

        // A bulk change: preset, A/B, sample load.
        struct SnapshotAction : juce::UndoableAction
        {
            SnapshotAction (GrainProcessor& p, GrainProcessor::Snapshot from, GrainProcessor::Snapshot to)
                : processor (p), before (std::move (from)), after (std::move (to)) {}
            bool perform() override { processor.applySnapshot (after); return true; }
            bool undo() override    { processor.applySnapshot (before); return true; }
            // Samples are heavy: keep only a few of them in the history.
            int getSizeInUnits() override { return before.withSample || after.withSample ? 40 : 4; }

            GrainProcessor& processor;
            GrainProcessor::Snapshot before, after;
        };
    }

    GrainProcessor::Snapshot GrainProcessor::captureSnapshot (bool withSample) const
    {
        Snapshot s;
        s.params.reserve (params.size());
        for (auto* p : params)
            s.params.push_back (currentValue (p));
        for (size_t i = 0; i < cues.size(); ++i)
            s.cues[i] = cues[i].load();
        s.withSample = withSample;
        if (withSample)
            s.sample = getUserSample();
        return s;
    }

    void GrainProcessor::applySnapshot (const Snapshot& s)
    {
        const juce::ScopedValueSetter<bool> guard (applyingHistory, true);
        if (s.withSample && s.sample != getUserSample())
            publishSource (s.sample);
        for (size_t i = 0; i < params.size() && i < s.params.size(); ++i)
            if (std::abs (params[i]->getValue() - s.params[i]) > 1.0e-6f)
            {
                params[i]->beginChangeGesture();
                params[i]->setValueNotifyingHost (s.params[i]);
                params[i]->endChangeGesture();
            }
        for (size_t i = 0; i < cues.size(); ++i)
            cues[i].store (s.cues[i]);
    }

    void GrainProcessor::recordChange (const juce::String& name, const Snapshot& before)
    {
        if (applyingHistory || ! juce::MessageManager::existsAndIsCurrentThread())
            return;
        undoManager.beginNewTransaction (name);
        undoManager.perform (new SnapshotAction (*this, before, captureSnapshot (before.withSample)));
    }

    void GrainProcessor::parameterGestureChanged (int index, bool starting)
    {
        // Only the user's own gestures on the message thread become undo steps; automation
        // never sends gestures, and other threads never touch the history.
        if (applyingHistory || index < 0 || index >= (int) params.size()
            || ! juce::MessageManager::existsAndIsCurrentThread())
            return;
        if (starting)
        {
            gestureStartValue[(size_t) index] = params[(size_t) index]->getValue();
            return;
        }
        const auto before = gestureStartValue[(size_t) index];
        const auto after = params[(size_t) index]->getValue();
        if (std::abs (after - before) > 1.0e-6f)
        {
            const juce::ScopedValueSetter<bool> guard (applyingHistory, true);
            undoManager.beginNewTransaction (params[(size_t) index]->getName (32));
            undoManager.perform (new ParamAction (*this, index, before, after));
        }
    }

    void GrainProcessor::fadeForChange()
    {
        // Ask the audio thread to fade out and wait for it (bounded: the audio may not run).
        engine.setFadedOut (true);
        fadeRequested.store (true);
        for (int waited = 0; waited < 20 && ! engine.isFadedOut(); ++waited)
            juce::Thread::sleep (1);
        fadeReleaseAt = juce::Time::getMillisecondCounter() + 15;
    }

    void GrainProcessor::toggleAB()
    {
        // Parameters only (the sample stays), applied like any other change: with gestures,
        // as one undo step, and without clearing the undo history.
        const auto before = captureSnapshot (false);
        abParams[(size_t) abSlot] = before.params;
        abSlot ^= 1;
        if (! abParams[(size_t) abSlot].empty())
        {
            fadeForChange();
            auto target = before;
            target.params = abParams[(size_t) abSlot];
            applySnapshot (target);
        }
        recordChange ("A/B", before);
    }

    void GrainProcessor::timerCallback()
    {
        flushHardwareChanges();

        if (abRequested.exchange (false))
            toggleAB();

        if (fadeRequested.load() && juce::Time::getMillisecondCounter() >= fadeReleaseAt)
        {
            fadeRequested.store (false);
            engine.setFadedOut (false);
        }

        if (clearHistoryPending.exchange (false))
            undoManager.clearUndoHistory();

        // Free samples nobody uses any more (never on the audio thread).
        {
            const juce::SpinLock::ScopedLockType lock (sourceLock);
            retired.erase (std::remove_if (retired.begin(), retired.end(),
                                           [] (const SourceData::Ptr& s) { return s->getReferenceCount() <= 1; }),
                           retired.end());
        }
    }

    //==============================================================================
    // State: parameters + sample (embedded FLAC up to 60 s, plus path) + cues + MIDI setup.
    juce::ValueTree GrainProcessor::saveExtraState (bool includeMidi) const
    {
        juce::ValueTree extra ("Extra");
        juce::StringArray cueList;
        for (auto& c : cues) cueList.add (juce::String (c.load(), 6));
        extra.setProperty ("cues", cueList.joinIntoString (","), nullptr);
        if (includeMidi)   // session setting, never part of a preset
            extra.setProperty ("keepSample", keepSample.load(), nullptr);

        if (auto s = getUserSample())
        {
            juce::ValueTree sample ("Sample");
            sample.setProperty ("path", s->file.getFullPathName(), nullptr);
            sample.setProperty ("name", s->getName(), nullptr);
            sample.setProperty ("hash", s->contentHash, nullptr);
            if (s->embeddedFlac.getSize() > 0)
                sample.setProperty ("flac", s->embeddedFlac, nullptr);
            extra.addChild (sample, -1, nullptr);
        }
        else if (const auto missing = getMissingSamplePath(); missing.isNotEmpty())
        {
            juce::ValueTree sample ("Sample");
            sample.setProperty ("path", missing, nullptr);
            extra.addChild (sample, -1, nullptr);
        }

        if (includeMidi)
        {
            juce::ValueTree midiTree ("Midi");
            midiTree.setProperty ("encoderMode", encoderMode.load(), nullptr);
            midiTree.setProperty ("padsAsControls", padsAsControls.load(), nullptr);
            midiTree.setProperty ("padChannel", padChannel.load(), nullptr);
            midiTree.setProperty ("page", page.load(), nullptr);
            for (int cc = 0; cc < 128; ++cc)
                if (const auto index = learned[(size_t) cc].load(); index >= 0)
                {
                    juce::ValueTree m ("Learn");
                    m.setProperty ("cc", cc, nullptr);
                    m.setProperty ("param", params[(size_t) index]->getParameterID(), nullptr);
                    midiTree.addChild (m, -1, nullptr);
                }
            extra.addChild (midiTree, -1, nullptr);
        }
        return extra;
    }

    void GrainProcessor::restoreExtraState (const juce::ValueTree& extra, bool includeMidi)
    {
        ++loadGeneration;   // a pending asynchronous load must not land on top of this
        if (includeMidi && extra.hasProperty ("keepSample"))
            keepSample.store ((bool) extra.getProperty ("keepSample"));
        const auto cueList = juce::StringArray::fromTokens (extra.getProperty ("cues").toString(), ",", "");
        for (size_t i = 0; i < cues.size(); ++i)
            cues[i].store (i < (size_t) cueList.size() ? juce::jlimit (-1.0f, 1.0f, cueList[(int) i].getFloatValue()) : -1.0f);

        const auto sample = extra.getChildWithName ("Sample");
        SourceData::Ptr loaded;
        juce::String missing, relinkError;
        if (sample.isValid())
        {
            const juce::File file (sample.getProperty ("path").toString());
            juce::String error;
            // Binary in session state, base64 text in preset files (XML).
            juce::MemoryBlock flac;
            const auto& flacValue = sample.getProperty ("flac");
            if (const auto* binary = flacValue.getBinaryData())
                flac = *binary;
            else if (flacValue.isString())
                flac.fromBase64Encoding (flacValue.toString());
            const auto options = loadOptions (0);
            const auto expectedHash = sample.getProperty ("hash").toString();
            if (flac.getSize() > 0)
            {
                loaded = sources::loadFromMemory (flac.getData(), flac.getSize(), sample.getProperty ("name"), error, options);
                if (loaded != nullptr)
                {
                    loaded->file = file;
                    if (expectedHash.isNotEmpty())
                        loaded->contentHash = expectedHash;
                }
            }
            // Not embedded (too long): the file itself, or the same file name where the user
            // keeps samples (moved projects, another computer) -- but only if it is the same
            // audio, never a different file that happens to share the name.
            auto candidate = file;
            if (loaded == nullptr && ! candidate.existsAsFile() && file.getFileName().isNotEmpty())
                candidate = library::findMissing (file);
            if (loaded == nullptr && candidate.existsAsFile())
            {
                loaded = sources::loadFile (candidate, error, options);
                if (loaded != nullptr && expectedHash.isNotEmpty() && loaded->contentHash != expectedHash)
                {
                    loaded = nullptr;
                    relinkError = "A file with this name was found, but its audio is different";
                }
            }
            if (loaded == nullptr && sample.getProperty ("path").toString().isNotEmpty())
                missing = file.getFullPathName();
        }
        publishSource (loaded);
        {
            const juce::ScopedLock l (statusLock);
            missingSamplePath = missing;
            lastLoadError = relinkError;
        }

        const auto midiTree = extra.getChildWithName ("Midi");
        if (includeMidi && midiTree.isValid())
        {
            encoderMode.store (juce::jlimit (0, 3, (int) midiTree.getProperty ("encoderMode", 0)));
            padsAsControls.store ((bool) midiTree.getProperty ("padsAsControls", true));
            padChannel.store (juce::jlimit (1, 16, (int) midiTree.getProperty ("padChannel", layout::minilab3::padChannel)));
            page.store (juce::jlimit (0, layout::numPages - 1, (int) midiTree.getProperty ("page", 0)));
            for (auto& l : learned) l.store (-1);
            for (const auto& m : midiTree)
            {
                const auto cc = (int) m.getProperty ("cc", -1);
                const auto index = indexOf (m.getProperty ("param").toString());
                if (cc >= 0 && cc < 128 && index >= 0)
                    learned[(size_t) cc].store (index);
            }
        }
    }

    void GrainProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        juce::ValueTree root ("thfGrain");
        root.setProperty ("version", stateVersion, nullptr);
        root.setProperty ("preset", presets.getCurrentName(), nullptr);
        root.addChild (state.copyState(), -1, nullptr);
        root.addChild (saveExtraState (true), -1, nullptr);
        juce::MemoryOutputStream out (dest, false);
        out.writeString ("THFG");
        root.writeToStream (out);
    }

    void GrainProcessor::setStateInformation (const void* data, int size)
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        if (in.readString() != "THFG")
            return;
        auto root = juce::ValueTree::readFromStream (in);
        if (! root.hasType ("thfGrain"))
            return;

        // A pending asynchronous sample load must not overwrite what the session restores.
        ++loadGeneration;
        loading.store (false);

        const auto version = (int) root.getProperty ("version", 1);
        switch (version)
        {
            // Future migrations go here, oldest first, each falling through to the next.
            case 1:
            default: break;
        }

        // Parameters the session does not know yet (added in later versions) start at their
        // defaults, not at whatever the instance had before.
        auto paramsTree = root.getChildWithName (state.state.getType()).createCopy();
        if (paramsTree.isValid())
        {
            for (auto* p : params)
                if (! paramsTree.getChildWithProperty ("id", p->getParameterID()).isValid())
                {
                    juce::ValueTree child ("PARAM");
                    child.setProperty ("id", p->getParameterID(), nullptr);
                    child.setProperty ("value", p->convertFrom0to1 (p->getDefaultValue()), nullptr);
                    paramsTree.appendChild (child, nullptr);
                }
            state.replaceState (paramsTree);
        }
        restoreExtraState (root.getChildWithName ("Extra"), true);
        presets.setCurrentName (root.getProperty ("preset", "Init").toString());
        if (juce::MessageManager::existsAndIsCurrentThread())
            undoManager.clearUndoHistory();
        else
            clearHistoryPending.store (true);
    }

    juce::AudioProcessorEditor* GrainProcessor::createEditor()
    {
        return new GrainEditor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new thf::grain::GrainProcessor();
}
