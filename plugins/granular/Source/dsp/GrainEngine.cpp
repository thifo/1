#include "GrainEngine.h"

namespace thf::grain
{
    namespace
    {
        float wrap01 (double x) noexcept
        {
            x -= std::floor (x);
            return (float) (x >= 1.0 ? 0.0 : x);
        }

        // Soft knee above -1 dB: transparent below, never exceeds 0 dBFS.
        float safeClip (float x) noexcept
        {
            constexpr float knee = 0.89f;
            const auto a = std::fabs (x);
            if (a <= knee)
                return x;
            const auto y = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
            return x < 0.0f ? -y : y;
        }
    }

    GrainEngine::GrainEngine() { reset(); }

    void GrainEngine::prepare (double sr)
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        switchFadeSamples = juce::jmax (16, (int) (0.008 * sampleRate));
        controlLength = juce::jlimit (16, maxControlBlock, (int) std::lround (32.0 * sampleRate / 48000.0));
        typeFadeLength = juce::jmax (16, (int) (0.005 * sampleRate));
        reverb.prepare (sampleRate);
        limiter.prepare (sampleRate);
        sideCoefs[0].setQ (200.0f, 0.5412f, (float) sampleRate);   // Butterworth, 4th order
        sideCoefs[1].setQ (200.0f, 1.3066f, (float) sampleRate);
        drive.prepare (sampleRate);
        reset();
    }

    float GrainEngine::quantizeInterval (float semitones, int mode) noexcept
    {
        static constexpr int octaves[] = { 0 };
        static constexpr int fifths[]  = { 0, 7 };
        static constexpr int major[]   = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int minor[]   = { 0, 2, 3, 5, 7, 8, 10 };
        const int* set = nullptr;
        int count = 0;
        switch (mode)
        {
            case 1: set = octaves; count = 1; break;
            case 2: set = fifths;  count = 2; break;
            case 3: set = major;   count = 7; break;
            case 4: set = minor;   count = 7; break;
            default: return semitones;
        }
        const auto octave = (int) std::floor (semitones / 12.0f);
        float best = 0.0f, bestDistance = 1.0e9f;
        for (int o = octave - 1; o <= octave + 1; ++o)
            for (int i = 0; i < count; ++i)
            {
                const auto candidate = (float) (12 * o + set[i]);
                const auto d = std::abs (candidate - semitones);
                if (d < bestDistance) { bestDistance = d; best = candidate; }
            }
        return best;
    }

    double GrainEngine::mapPlayhead (double x, int scanLoop) noexcept
    {
        switch (scanLoop)
        {
            case 1:  { const auto t = x - 2.0 * std::floor (x * 0.5); return t <= 1.0 ? t : 2.0 - t; }   // ping-pong
            case 2:  return juce::jlimit (0.0, 1.0, x);                                                    // once
            default: return wrap01 (x);                                                                    // loop
        }
    }

    double GrainEngine::advanceScan (double scan, double step, int scanLoop) noexcept
    {
        scan += step;
        switch (scanLoop)
        {
            case 1:  return scan - 2.0 * std::floor (scan * 0.5);     // the fold does the rest
            case 2:  return juce::jlimit (-2.0, 2.0, scan);
            default: return wrap01 (scan);
        }
    }

    void GrainEngine::noteOnAt (int id, int pitchNote, float velocity, float anchor, const EngineParams& p)
    {
        nextPitch = pitchNote;
        nextAnchor = juce::jlimit (0.0f, 1.0f, anchor);
        noteOn (id, velocity, p);
        nextPitch = -1;
        nextAnchor = -1.0f;
    }

    void GrainEngine::reset()
    {
        rng.setSeed (seed_);
        lfo.reset (rng);
        for (auto& v : voices)
        {
            v = Voice {};
        }
        for (auto& g : grains)
            g.active = false;
        heldPrevious = nullptr;
        drive.reset();
        reverb.reset();
        shape = {};
        beatClock = 0.0;
        hostPlaying = false;
        linkPhase = 0.0;
        linkDelayed = -1.0;
        linkSeed = rng.next() | 1u;
        sampleClock = 0;
        filterTypeNow = -1;
        typeFadeLeft = 0;
        reverbRunning = false;
        limiter.reset();
        sideHighPass[0].reset();
        sideHighPass[1].reset();
        levelSmoothed = 1.0f;
        globalScan = 0.0;
        monoCount = 0;
        noteCounter = 0;
        sustainPedal = holdOn = false;
        bend = modWheel = pressure = 0.0f;
        driveSmoothed = 0.0f;
        gainSmoothed = -1.0f;      // "not initialised": the first block jumps to the target
        resonanceSmoothed = -1.0f;
        activeVoicesForUi.store (0);
        activeGrainsForUi.store (0);
    }

    void GrainEngine::setSource (SourceData::Ptr s) noexcept
    {
        if (s.get() == source)
            return;
        // Grains still fading from an even older source are cut: their source is about to
        // lose the engine's reference.
        for (auto& g : grains)
            if (g.active && g.src != source)
            {
                g.active = false;
                --voices[(size_t) g.voice].grains;
            }
        // Grains of the current source fade out quickly instead of stopping dead.
        for (auto& g : grains)
            if (g.active && g.fadeOut == 0)
                g.fadeOut = switchFadeSamples;
        heldPrevious = heldSource;
        heldSource = s;
        source = s.get();
    }

    void GrainEngine::resetScan() noexcept
    {
        globalScan = 0.0;
        for (auto& v : voices)
            v.scanOffset = 0.0;
    }

    void GrainEngine::setTransport (bool playing, double ppqPosition) noexcept
    {
        hostPlaying = playing;
        if (playing)
            beatClock = ppqPosition;
    }

    float GrainEngine::stealSamples (int note) const noexcept
    {
        // Long enough not to tick, longer for low notes whose cycles are long.
        return (float) ((note < 48 ? 0.008 : 0.004) * sampleRate);
    }

    GrainEngine::GrainDraw GrainEngine::draw (dsp::Random& r) noexcept
    {
        GrainDraw d;
        d.jitter = r.bipolar();
        d.spray = r.uniform() - 0.5f;
        d.reverse = r.uniform();
        d.pan = r.bipolar();
        return d;
    }

    GrainEngine::GrainShape GrainEngine::computeShape (const EngineParams& p) const
    {
        const auto sr = (float) sampleRate;
        GrainShape g;
        auto density = p.sync ? p.bpm / 60.0 / juce::jmax (1.0e-3, p.syncBeats)
                              : (double) p.density * modulation.densityMul;
        density = juce::jlimit (0.1, 1000.0, density);
        g.interval = sampleRate / density;
        g.sizeSamples = juce::jlimit (0.002f * sr, 4.0f * sr, p.sizeMs * modulation.sizeMul * 0.001f * sr);
        g.fade = dsp::windowFade (p.window, g.sizeSamples, sr);
        const auto overlap = juce::jmin ((double) maxGrainsPerVoice, (double) g.sizeSamples / g.interval);

        // Regular onsets: a Tukey window sums to a constant when its fall lines up with the
        // rise of the k-th next grain, fade = 1 - k / overlap. Flat windows are widened to
        // that fade (never narrowed), fully at chaos 0 and fading out by chaos 0.25.
        if (overlap >= 1.0 && p.chaos < 0.25f)
        {
            const auto k = std::ceil (overlap * 0.5);
            const auto constantSum = (float) (1.0 - k / overlap);
            const auto weight = 1.0f - p.chaos / 0.25f;
            g.fade += weight * (juce::jmax (g.fade, constantSum) - g.fade);
        }
        g.normGain = 1.0f / std::sqrt (juce::jmax (1.0f, (float) overlap * dsp::windowEnergy (g.fade)));
        return g;
    }

    float GrainEngine::velocityGain (float velocity, const EngineParams& p) const
    {
        const auto v = juce::jlimit (0.0f, 1.0f, velocity);
        return (1.0f - p.velocitySens) + p.velocitySens * v * v;
    }

    //==============================================================================
    void GrainEngine::startVoice (Voice& v, int note, float gain, bool keyDown, const EngineParams& p)
    {
        const auto index = (int) (&v - voices.data());
        bool anyActive = false;
        for (auto& other : voices)
            anyActive = anyActive || (other.active && &other != &v);
        if (! anyActive)
        {
            globalScan = 0.0;     // a new phrase starts at Position
            if (! hostPlaying)
                beatClock = 0.0;  // ... and, without a running transport, on the beat
        }

        killGrains (index);
        const auto previousNote = v.currentNote;
        const bool glideFromPrevious = p.voiceMode != 0 && p.glideMs > 0.0f && v.note >= 0;

        v.active = true;
        v.note = note;
        v.keyDown = keyDown;
        v.sustained = false;
        v.velocityGain = gain;
        v.order = ++noteCounter;
        v.amp.reset();
        v.amp.gateOn();
        v.filterEnv.reset();
        v.filterEnv.trigger();
        v.filter[0].reset();
        v.filter[1].reset();
        v.scanOffset = 0.0;
        v.targetNote = pitchFor (note);
        v.currentNote = glideFromPrevious ? previousNote : v.targetNote;
        v.anchor = nextAnchor;
        v.firstBlock = true;
        v.pendingNote = -1;
        v.energy = v.energySmoothed = v.powerSmoothed = 0.0f;
        v.coherence = 1.0f;
        v.delayed = -1.0;

        // The cloud is there from the first sample: one grain now, plus the grains a running
        // cloud would already be playing (ages spaced by the onset interval). Linked voices
        // started together draw the same choices.
        shape = computeShape (p);
        dsp::Random linked (linkSeed);
        auto& r = p.linkVoices ? linked : rng;
        spawnGrain (index, 0, p, shape, draw (r));
        const auto gap = [&r, &p] { return (double) ((1.0f - p.chaos) + p.chaos * r.exponential()); };
        for (double age = shape.interval * gap(); age < (double) shape.sizeSamples; age += shape.interval * gap())
            if (auto* g = spawnGrain (index, 0, p, shape, draw (r)))
            {
                g->age = (int) age;
                g->readPos += g->step * (double) g->age;
            }
        v.phase = gap();
        v.holdoff = 0.25 * shape.interval;

        if (! keyDown)
            releaseVoice (v);
    }

    int GrainEngine::allocateVoice (const EngineParams& p)
    {
        const auto limit = juce::jlimit (1, maxVoices, p.voices);
        for (int i = 0; i < limit; ++i)
            if (! voices[(size_t) i].active)
                return i;

        // Steal: the quietest releasing voice, otherwise the oldest.
        int best = -1;
        float bestLevel = 2.0f;
        for (int i = 0; i < limit; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (v.amp.isReleasing() && v.pendingNote < 0 && v.amp.getLevel() < bestLevel)
            {
                bestLevel = v.amp.getLevel();
                best = i;
            }
        }
        if (best >= 0)
            return best;

        uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < limit; ++i)
            if (voices[(size_t) i].order < oldest)
            {
                oldest = voices[(size_t) i].order;
                best = i;
            }
        return best;
    }

    void GrainEngine::releaseVoice (Voice& v)
    {
        v.sustained = false;
        v.amp.gateOff();
    }

    void GrainEngine::noteOn (int note, float velocity, const EngineParams& p)
    {
        const auto gain = velocityGain (velocity, p);

        if (p.voiceMode != 0)
        {
            // Mono / legato: one voice and a stack of held keys.
            for (int i = 0; i < monoCount; ++i)
                if (monoStack[(size_t) i].first == note)
                {
                    for (int j = i; j < monoCount - 1; ++j) monoStack[(size_t) j] = monoStack[(size_t) j + 1];
                    --monoCount;
                    break;
                }
            if (monoCount < (int) monoStack.size())
                monoStack[(size_t) monoCount++] = { note, gain };

            auto& v = voices[0];
            for (size_t i = 1; i < voices.size(); ++i)
                if (voices[i].active) voices[i].amp.steal (stealSamples (voices[i].note));

            if (v.active && ! v.amp.isReleasing() && p.voiceMode == 2)
            {
                v.note = note;
                v.targetNote = pitchFor (note);
                v.anchor = nextAnchor;
                v.keyDown = true;
                if (p.glideMs <= 0.0f) v.currentNote = v.targetNote;
            }
            else if (v.active && v.amp.getStage() != dsp::Adsr::Stage::steal)
            {
                // Mono retrigger from the current level: no reset, no click.
                v.note = note;
                v.targetNote = pitchFor (note);
                v.anchor = nextAnchor;
                v.keyDown = true;
                v.sustained = false;
                v.velocityGain = gain;
                v.order = ++noteCounter;
                if (p.glideMs <= 0.0f) v.currentNote = v.targetNote;
                v.amp.gateOn();
                v.filterEnv.trigger();
            }
            else
            {
                startVoice (v, note, gain, true, p);
            }
            return;
        }

        // Poly: a key that is still sounding is released before it is played again.
        for (auto& v : voices)
            if (v.active && v.note == note && (v.keyDown || v.sustained) && ! v.amp.isReleasing())
                releaseVoice (v);

        const auto index = allocateVoice (p);
        auto& v = voices[(size_t) index];
        if (v.active)
        {
            v.amp.steal (stealSamples (v.note));
            v.pendingNote = note;
            v.pendingPitch = nextPitch;
            v.pendingAnchor = nextAnchor;
            v.pendingVelocityGain = gain;
            v.pendingKeyDown = true;
        }
        else
        {
            startVoice (v, note, gain, true, p);
        }
    }

    void GrainEngine::noteOff (int note, const EngineParams& p)
    {
        const bool latched = sustainPedal || holdOn;

        if (p.voiceMode != 0)
        {
            for (int i = 0; i < monoCount; ++i)
                if (monoStack[(size_t) i].first == note)
                {
                    for (int j = i; j < monoCount - 1; ++j) monoStack[(size_t) j] = monoStack[(size_t) j + 1];
                    --monoCount;
                    break;
                }

            auto& v = voices[0];
            if (! v.active || v.note != note)
                return;
            if (monoCount > 0 && ! latched)
            {
                const auto top = monoStack[(size_t) monoCount - 1].first;
                v.note = top;
                v.targetNote = (float) top;
                if (p.glideMs <= 0.0f) v.currentNote = (float) top;
                return;
            }
            v.keyDown = false;
            if (latched) v.sustained = true;
            else         releaseVoice (v);
            return;
        }

        for (auto& v : voices)
        {
            if (v.pendingNote == note && v.pendingKeyDown)
                v.pendingKeyDown = latched;
            if (v.active && v.note == note && v.keyDown)
            {
                v.keyDown = false;
                if (latched) v.sustained = true;
                else         releaseVoice (v);
            }
        }
    }

    void GrainEngine::allNotesOff (bool immediate)
    {
        monoCount = 0;
        for (size_t i = 0; i < voices.size(); ++i)
        {
            auto& v = voices[i];
            v.pendingNote = -1;
            if (! v.active)
                continue;
            if (immediate)
            {
                killGrains ((int) i);
                v.active = false;
                v.amp.reset();
            }
            else
            {
                v.keyDown = false;
                releaseVoice (v);
            }
        }
    }

    void GrainEngine::setSustainPedal (bool down)
    {
        sustainPedal = down;
        if (! sustainPedal && ! holdOn)
            for (auto& v : voices)
                if (v.active && v.sustained && ! v.keyDown)
                    releaseVoice (v);
    }

    void GrainEngine::setHold (bool on)
    {
        if (holdOn == on)
            return;
        holdOn = on;
        setSustainPedal (sustainPedal);
    }

    //==============================================================================
    void GrainEngine::killGrains (int voiceIndex)
    {
        for (auto& g : grains)
            if (g.active && g.voice == voiceIndex)
                g.active = false;
        voices[(size_t) voiceIndex].grains = 0;
    }

    void GrainEngine::pushEvent (const GrainEvent& e) noexcept
    {
        const auto w = eventWrite.load (std::memory_order_relaxed);
        const auto r = eventRead.load (std::memory_order_acquire);
        if (w - r >= (uint32_t) eventCapacity)
            return;
        events[w % eventCapacity] = e;
        eventWrite.store (w + 1, std::memory_order_release);
    }

    int GrainEngine::popGrainEvents (GrainEvent* dest, int maxEvents)
    {
        const auto r = eventRead.load (std::memory_order_relaxed);
        const auto w = eventWrite.load (std::memory_order_acquire);
        const auto count = (int) juce::jmin ((uint32_t) maxEvents, w - r);
        for (int i = 0; i < count; ++i)
            dest[i] = events[(r + (uint32_t) i) % eventCapacity];
        eventRead.store (r + (uint32_t) count, std::memory_order_release);
        return count;
    }

    //==============================================================================
    GrainEngine::Grain* GrainEngine::spawnGrain (int voiceIndex, int offset, const EngineParams& p,
                                                 const GrainShape& gs, const GrainDraw& d)
    {
        auto& v = voices[(size_t) voiceIndex];
        if (source == nullptr || v.grains >= maxGrainsPerVoice)
            return nullptr;

        Grain* g = nullptr;
        for (int k = 0; k < maxGrains && g == nullptr; ++k)
        {
            auto& candidate = grains[(size_t) ((nextGrain + k) % maxGrains)];
            if (! candidate.active) { g = &candidate; nextGrain = (nextGrain + k + 1) % maxGrains; }
        }
        if (g == nullptr)
            return nullptr;

        auto sizeSamples = gs.sizeSamples;
        auto fade = gs.fade;
        const auto length0 = (double) source->getLength();
        const auto semis = (v.currentNote - (float) p.root) + p.pitch + bend * p.bendRange
                         + modulation.pitch + quantizeInterval (p.jitter * d.jitter, p.quantize);
        const auto ratio = juce::jlimit (1.0 / 64.0, 32.0,
                                         std::exp2 ((double) semis / 12.0) * source->getSampleRate() / sampleRate);
        auto span0 = (double) sizeSamples * ratio;

        // Position, spray and scan are relative to the region; grains stay inside it when they fit.
        const auto regionStart = juce::jlimit (0.0, 1.0, (double) juce::jmin (p.regionStart, p.regionEnd));
        const auto regionLength = juce::jmax (1.0e-3, juce::jlimit (0.0, 1.0, (double) juce::jmax (p.regionStart, p.regionEnd)) - regionStart);
        // A voice with its own position (cue pad) scans from there on its own.
        const bool anchored = v.anchor >= 0.0f;
        const auto scanOffset = p.perNoteScan || anchored ? v.scanOffset : globalScan;
        const auto spray = juce::jlimit (0.0f, 1.0f, p.spray + modulation.spray);
        const auto base = anchored ? (double) v.anchor : (double) p.position + modulation.position;
        // The playhead loops, folds or stops at the region's end (Scan Loop); the spray around
        // it reflects at the edges so no grains pile up on the boundary.
        auto relative = mapPlayhead (base + scanOffset, p.scanLoop) + d.spray * spray;
        if (relative < 0.0) relative = -relative;
        if (relative > 1.0) relative = 2.0 - relative;
        relative = juce::jlimit (0.0, 1.0, relative);
        const auto lo = regionStart * length0;
        const auto hi = (regionStart + regionLength) * length0;

        // A grain longer than the material it may read is shortened to fit, so it never
        // plays silence past the end.
        if (span0 > hi - lo)
        {
            sizeSamples = juce::jmax (1.0f, (float) ((hi - lo) / ratio));
            span0 = (double) sizeSamples * ratio;
            fade = dsp::windowFade (p.window, sizeSamples, (float) sampleRate);
        }
        // Grain starts are spread over the usable part of the region, [lo, hi - span].
        const auto start0 = juce::jlimit (0.0, juce::jmax (0.0, length0 - span0), lo + relative * juce::jmax (0.0, hi - lo - span0));
        const bool reversed = d.reverse < p.reverse;

        // Copy: the one with the most content that still reads at most 2 stored samples per
        // output sample (see SourceData).
        const auto level = juce::jlimit (0, source->getNumLevels() - 1,
                                         ratio <= 1.0 ? 0 : (int) std::ceil (2.0 * std::log2 (ratio) - 1.0e-9));
        const auto scale = SourceData::levelScale (level);
        const auto effective = ratio * scale;

        g->active = true;
        g->src = source;
        g->fadeOut = 0;
        g->voice = voiceIndex;
        g->level = level;
        g->step = reversed ? -effective : effective;
        g->readPos = (reversed ? start0 + span0 - ratio : start0) * scale;
        g->age = 0;
        g->length = juce::jmax (1, (int) sizeSamples);
        g->invLength = 1.0f / (float) g->length;
        g->fade = fade;
        g->invFade = 1.0f / juce::jmax (1.0e-6f, fade);
        g->startOffset = offset;

        auto normGain = gs.normGain;
        if (p.normalizeSource)
            normGain *= source->getNormalGain();

        const auto pan = p.stereo * d.pan;
        const auto angle = (pan + 1.0f) * dsp::pi * 0.25f;
        g->gainL = std::cos (angle) * 1.41421356f * normGain;
        g->gainR = std::sin (angle) * 1.41421356f * normGain;
        ++v.grains;

        pushEvent ({ (float) (start0 / length0), (float) (span0 / length0), pan,
                     sizeSamples / (float) sampleRate, voiceIndex, reversed, sampleClock + offset });
        return g;
    }

    void GrainEngine::processControl (const EngineParams& p, int n)
    {
        const auto sr = (float) sampleRate;
        adsrCoefs.set (sr, p.attackMs, p.decayMs, p.sustain, p.releaseMs);

        const auto lfoRate = p.lfoSync ? (float) (p.bpm / 60.0 / juce::jmax (1.0e-3, p.lfoBeats)) : p.lfoRate;
        lfoValue = lfo.advance (n, lfoRate, sr, (dsp::Lfo::Shape) p.lfoShape, rng);
        modulation = {};
        auto apply = [this] (int target, float amount)
        {
            switch (target)
            {
                case 0: modulation.position += 0.5f * amount; break;
                case 1: modulation.spray += 0.5f * amount; break;
                case 2: modulation.sizeMul *= std::exp2 (2.0f * amount); break;
                case 3: modulation.densityMul *= std::exp2 (2.0f * amount); break;
                case 4: modulation.pitch += 12.0f * amount; break;
                case 5: modulation.cutoffMul *= std::exp2 (5.0f * amount); break;
                default: break;
            }
        };
        apply (p.lfoTarget, lfoValue * p.lfoDepth);
        const auto modAmount = juce::jmax (modWheel, pressure);
        apply (p.modTarget, modAmount * p.modDepth);

        // Level: the LFO ducks from full level (LFO at +1) down by its depth (LFO at -1):
        // a rising saw gives the classic pump on every cycle. The mod strip just turns down.
        if (p.lfoTarget == 6)
            modulation.level *= 1.0f - p.lfoDepth * 0.5f * (1.0f - lfoValue);
        if (p.modTarget == 6)
            modulation.level *= 1.0f - modAmount * p.modDepth;

        const auto smooth20ms = 1.0f - std::exp (-(float) n / (0.02f * sr));
        const auto smooth5ms = 1.0f - std::exp (-(float) n / (0.005f * sr));
        if (resonanceSmoothed < 0.0f) resonanceSmoothed = p.resonance;
        resonanceSmoothed += (p.resonance - resonanceSmoothed) * smooth20ms;

        // Playhead.
        double scanStep = 0.0;
        if (source != nullptr && ! p.freeze)
        {
            const auto regionLength = juce::jmax (1.0e-3f, std::abs (p.regionEnd - p.regionStart));
            scanStep = (double) p.scan * source->getSampleRate() / (sampleRate * source->getLength() * regionLength) * n;
        }
        globalScan = advanceScan (globalScan, scanStep, p.scanLoop);

        shape = computeShape (p);
        linkSeed = rng.next() | 1u;
        const auto gap = [this, &p] { return (double) ((1.0f - p.chaos) + p.chaos * rng.exponential()); };

        int activeVoices = 0;
        float newestOffset = -1.0f, newestAnchor = -1.0f;
        uint64_t newestOrder = 0;
        for (int vi = 0; vi < maxVoices; ++vi)
        {
            auto& v = voices[(size_t) vi];
            if (! v.active)
                continue;
            ++activeVoices;

            if (p.voiceMode != 0 && p.glideMs > 0.0f)
                v.currentNote = v.targetNote + (v.currentNote - v.targetNote) * std::exp (-(float) n / (p.glideMs * 0.001f * sr));
            else
                v.currentNote = v.targetNote;

            v.scanOffset = advanceScan (v.scanOffset, scanStep, p.scanLoop);
            if (v.order > newestOrder)
            {
                newestOrder = v.order;
                newestOffset = (float) v.scanOffset;
                newestAnchor = v.anchor;
            }

            // Only the knob position is smoothed; the envelope moves the cutoff directly so
            // its attack stays sharp.
            v.filterEnv.set (sr, p.filterDecayMs);
            const auto env = v.filterEnv.advance (n);
            const auto base = juce::jlimit (10.0f, 0.49f * sr, p.cutoff * modulation.cutoffMul);
            if (v.firstBlock) v.cutoffSmoothed = base;
            v.cutoffSmoothed *= std::exp2 (std::log2 (base / v.cutoffSmoothed) * smooth5ms);
            const auto cutoff = juce::jlimit (10.0f, 0.49f * sr, v.cutoffSmoothed * std::exp2 (p.filterEnv * 5.0f * env));
            svfCoefs[vi].set (cutoff, resonanceSmoothed, sr);
            v.firstBlock = false;
        }

        // Onsets. A voice being stolen spawns nothing.
        auto spawning = [this] (const Voice& v) { return v.active && v.amp.getStage() != dsp::Adsr::Stage::steal; };
        if (p.sync)
        {
            // On the beat grid (host transport or the engine's own clock), all voices together.
            const auto beatsPerSample = juce::jmax (1.0, p.bpm) / (60.0 * sampleRate);
            const auto spacing = juce::jmax (1.0e-3, p.syncBeats);
            const auto b0 = beatClock, b1 = beatClock + n * beatsPerSample;
            for (auto tick = std::ceil (b0 / spacing - 1.0e-9) * spacing; tick < b1; tick += spacing)
            {
                const auto offset = juce::jlimit (0.0, (double) n - 1.0, (tick - b0) / beatsPerSample);
                // Chaos delays an onset by up to half an interval instead of moving the grid.
                const auto sharedDelay = p.chaos * rng.uniform() * 0.5 * shape.interval;
                if (p.linkVoices) linkDraw = draw (rng);
                for (int vi = 0; vi < maxVoices; ++vi)
                {
                    auto& v = voices[(size_t) vi];
                    if (! spawning (v) || offset < v.holdoff)
                        continue;
                    const auto delay = p.linkVoices ? sharedDelay : p.chaos * rng.uniform() * 0.5 * shape.interval;
                    if (delay < 1.0)
                        spawnGrain (vi, (int) offset, p, shape, p.linkVoices ? linkDraw : draw (rng));
                    else
                        v.delayed = offset + delay;
                }
            }
            for (int vi = 0; vi < maxVoices; ++vi)
            {
                auto& v = voices[(size_t) vi];
                if (v.delayed >= 0.0 && v.delayed < (double) n)
                {
                    if (spawning (v))
                        spawnGrain (vi, (int) v.delayed, p, shape, p.linkVoices ? linkDraw : draw (rng));
                    v.delayed = -1.0;
                }
                else if (v.delayed >= 0.0)
                {
                    v.delayed -= n;
                }
                v.holdoff = juce::jmax (0.0, v.holdoff - n);
            }
        }
        else if (p.linkVoices)
        {
            // One clock and one set of random choices for every voice.
            for (auto t = linkPhase * shape.interval; t < (double) n; t = linkPhase * shape.interval)
            {
                linkDraw = draw (rng);
                for (int vi = 0; vi < maxVoices; ++vi)
                    if (spawning (voices[(size_t) vi]) && t >= voices[(size_t) vi].holdoff)
                        spawnGrain (vi, (int) t, p, shape, linkDraw);
                linkPhase += gap();
            }
            linkPhase -= n / shape.interval;
            for (auto& v : voices)
                v.holdoff = juce::jmax (0.0, v.holdoff - n);
        }
        else
        {
            // Each voice runs its own clock in units of the current interval, so a new
            // Density applies at once instead of after the onset already scheduled.
            for (int vi = 0; vi < maxVoices; ++vi)
            {
                auto& v = voices[(size_t) vi];
                if (! v.active)
                    continue;
                for (auto t = v.phase * shape.interval; t < (double) n; t = v.phase * shape.interval)
                {
                    if (spawning (v))
                        spawnGrain (vi, (int) t, p, shape, draw (rng));
                    v.phase += gap();
                }
                v.phase -= n / shape.interval;
            }
        }

        beatClock += n * juce::jmax (1.0, p.bpm) / (60.0 * sampleRate);

        const auto shownScan = (p.perNoteScan || newestAnchor >= 0.0f) && newestOffset >= 0.0f ? (double) newestOffset : globalScan;
        const auto shownBase = newestAnchor >= 0.0f ? (double) newestAnchor : (double) p.position;
        playheadForUi.store ((float) mapPlayhead (shownBase + shownScan, p.scanLoop), std::memory_order_relaxed);
        activeVoicesForUi.store (activeVoices, std::memory_order_relaxed);
    }

    template <typename Kernel>
    void GrainEngine::renderGrains (int n, const Kernel& k)
    {
        constexpr int margin = SourceData::padding - Kernel::reach - 2;
        const auto fadeScale = 1.0f / (float) switchFadeSamples;

        int active = 0;
        bool previousInUse = false;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;
            ++active;
            const auto* src = g.src;
            previousInUse = previousInUse || src != source;
            const bool stereoSource = src->getNumChannels() > 1;
            auto* outL = voiceBuffer[g.voice][0];
            auto* outR = voiceBuffer[g.voice][1];
            const auto* srcL = src->channel (g.level, 0);
            const auto* srcR = src->channel (g.level, 1);
            const auto levelLength = src->getLevelLength (g.level);
            float energy = 0.0f;
            float scratch[(size_t) Kernel::taps];

            for (int i = g.startOffset; i < n; ++i)
            {
                if (g.age >= g.length)
                    break;
                auto w = windowTable.value ((float) g.age * g.invLength, g.fade, g.invFade);
                if (g.fadeOut > 0)
                {
                    w *= (float) g.fadeOut * fadeScale;
                    if (--g.fadeOut == 0) { g.age = g.length; break; }
                }
                const auto ip = (int) std::floor (g.readPos);
                float l = 0.0f, r = 0.0f;
                if (ip > -margin && ip < levelLength + margin)
                {
                    const auto* weights = k.weights ((float) (g.readPos - ip), scratch);
                    if (stereoSource) Kernel::dot2 (srcL + ip, srcR + ip, weights, l, r);
                    else              r = l = Kernel::dot (srcL + ip, weights);
                }
                const auto cl = l * w * g.gainL, cr = r * w * g.gainR;
                outL[i] += cl;
                outR[i] += cr;
                energy += cl * cl + cr * cr;
                g.readPos += g.step;
                ++g.age;
            }
            voices[(size_t) g.voice].energy += energy;
            g.startOffset = 0;
            if (g.age >= g.length)
            {
                g.active = false;
                --voices[(size_t) g.voice].grains;
            }
        }
        activeGrainsForUi.store (active, std::memory_order_relaxed);

        // The old source can go once its last grain has faded (this only drops a reference).
        if (! previousInUse && heldPrevious != nullptr)
            heldPrevious = nullptr;
    }

    //==============================================================================
    void GrainEngine::render (float* left, float* right, int n, const EngineParams& p)
    {
        if (gainSmoothed < 0.0f)
            gainSmoothed = p.outputGain;

        const auto requestedType = juce::jlimit (0, 2, p.filterType);
        if (filterTypeNow < 0)
        {
            filterTypeNow = requestedType;
        }
        else if (requestedType != filterTypeNow)
        {
            filterTypePrevious = filterTypeNow;
            filterTypeNow = requestedType;
            typeFadeLeft = typeFadeLength;
        }
        const auto typeNow = (dsp::Svf::Type) filterTypeNow;
        const auto typePrevious = (dsp::Svf::Type) filterTypePrevious;

        int done = 0;
        while (done < n)
        {
            const auto len = juce::jmin (controlLength, n - done);
            processControl (p, len);

            for (int v = 0; v < maxVoices; ++v)
                if (voices[(size_t) v].active)
                    for (int ch = 0; ch < 2; ++ch)
                        std::fill (voiceBuffer[v][ch], voiceBuffer[v][ch] + len, 0.0f);

            if (p.hq) renderGrains (len, kernelHq);
            else      renderGrains (len, kernel);

            auto* L = left + done;
            auto* R = right + done;
            std::fill (L, L + len, 0.0f);
            std::fill (R, R + len, 0.0f);

            // Weight of the previous filter type while a type change crossfades.
            const bool typeFading = typeFadeLeft > 0;
            for (int i = 0; i < len; ++i)
                typeMix[i] = typeFadeLeft > i ? (float) (typeFadeLeft - i) / (float) typeFadeLength : 0.0f;
            typeFadeLeft = juce::jmax (0, typeFadeLeft - len);

            const auto powerSmoothing = 1.0f - std::exp (-(float) len / (0.05f * (float) sampleRate));
            for (int vi = 0; vi < maxVoices; ++vi)
            {
                auto& v = voices[(size_t) vi];
                if (! v.active)
                    continue;

                // Coherent grains (same material, same phase) add up louder than the
                // uncorrelated sum the normalisation assumes; bring such clouds down to the
                // level of the sum of their grain powers. Attenuation only.
                float power = 0.0f;
                for (int i = 0; i < len; ++i)
                    power += voiceBuffer[vi][0][i] * voiceBuffer[vi][0][i] + voiceBuffer[vi][1][i] * voiceBuffer[vi][1][i];
                v.powerSmoothed += (power - v.powerSmoothed) * powerSmoothing;
                v.energySmoothed += (v.energy - v.energySmoothed) * powerSmoothing;
                v.energy = 0.0f;
                constexpr float eps = 1.0e-9f;
                const auto coherenceTarget = juce::jlimit (0.1f, 1.0f, std::sqrt ((v.energySmoothed + eps) / (v.powerSmoothed + eps)));
                const auto coherenceStart = v.coherence;
                const auto coherenceStep = (coherenceTarget - coherenceStart) / (float) len;
                v.coherence = coherenceTarget;

                const auto& coefs = svfCoefs[vi];
                for (int i = 0; i < len; ++i)
                {
                    const auto env = v.amp.process (adsrCoefs) * v.velocityGain * (coherenceStart + coherenceStep * (float) (i + 1));
                    const auto outL = v.filter[0].processAll (voiceBuffer[vi][0][i], coefs);
                    const auto outR = v.filter[1].processAll (voiceBuffer[vi][1][i], coefs);
                    auto l = dsp::Svf::select (outL, typeNow), r = dsp::Svf::select (outR, typeNow);
                    if (typeFading)
                    {
                        l += (dsp::Svf::select (outL, typePrevious) - l) * typeMix[i];
                        r += (dsp::Svf::select (outR, typePrevious) - r) * typeMix[i];
                    }
                    L[i] += l * env;
                    R[i] += r * env;
                }

                if (! v.amp.isActive())
                {
                    if (v.pendingNote >= 0)
                    {
                        nextPitch = v.pendingPitch;
                        nextAnchor = v.pendingAnchor;
                        startVoice (v, v.pendingNote, v.pendingVelocityGain, v.pendingKeyDown, p);
                        nextPitch = -1;
                        nextAnchor = -1.0f;
                    }
                    else
                    {
                        v.active = false;
                        killGrains (vi);
                    }
                }
            }

            // Drive: +0..30 dB into an oversampled, anti-aliased tanh, blended in from zero.
            {
                const auto start = driveSmoothed;
                driveSmoothed += (p.drive - driveSmoothed) * (1.0f - std::exp (-(float) len / (0.02f * (float) sampleRate)));
                drive.process (L, R, len, start, driveSmoothed);
            }

            // Space after the drive: added to the dry signal, which is never touched.
            if (p.space > 1.0e-3f)
                reverbRunning = true;
            if (reverbRunning)
            {
                reverb.process (L, R, len, p.space, p.spaceSize);
                if (p.space <= 1.0e-3f && ! reverb.isRinging())
                    reverbRunning = false;
            }

            // Low end in mono: the side signal is high-passed at 200 Hz (4th order, -10 dB at
            // 150 Hz), so the bass sums to mono and survives mono playback.
            for (int i = 0; i < len; ++i)
            {
                const auto mid = 0.5f * (L[i] + R[i]);
                const auto side = sideHighPass[1].processAll (sideHighPass[0].processAll (0.5f * (L[i] - R[i]), sideCoefs[0]).hp, sideCoefs[1]).hp;
                L[i] = mid + side;
                R[i] = mid - side;
            }

            // Level modulation (pump) and output gain, both ramped across the block.
            const auto startLevel = levelSmoothed;
            levelSmoothed += (modulation.level - levelSmoothed) * (1.0f - std::exp (-(float) len / (0.002f * (float) sampleRate)));
            const auto levelStep = (levelSmoothed - startLevel) / (float) len;
            for (int i = 0; i < len; ++i)
            {
                const auto lv = startLevel + levelStep * (float) (i + 1);
                L[i] *= lv;
                R[i] *= lv;
            }

            // Output gain ramps linearly across the control block. The fixed -3 dB leaves room
            // for several voices before the limiter (Safe Clip) has to act.
            constexpr float headroom = 0.708f;
            const auto startGain = gainSmoothed;
            gainSmoothed += (p.outputGain - gainSmoothed) * (1.0f - std::exp (-(float) len / (0.01f * (float) sampleRate)));
            const auto gainStep = (gainSmoothed - startGain) / (float) len;
            bool bad = false;
            const auto fadeStep = 1.0f / (0.005f * (float) sampleRate);
            auto target = fadeTarget.load (std::memory_order_relaxed);
            if (target <= 0.0f && fadeGain <= 0.0f)
            {
                fadedSamples += len;
                if (fadedSamples > (int) (0.1 * sampleRate))
                {
                    fadeTarget.store (1.0f);
                    target = 1.0f;
                }
            }
            else
            {
                fadedSamples = 0;
            }
            for (int i = 0; i < len; ++i)
            {
                if (std::abs (fadeGain - target) > 0.0f)
                    fadeGain = target > fadeGain ? juce::jmin (target, fadeGain + fadeStep)
                                                 : juce::jmax (target, fadeGain - fadeStep);
                const auto gain = (startGain + gainStep * (float) (i + 1)) * headroom * fadeGain;
                auto l = L[i] * gain, r = R[i] * gain;
                if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; bad = true; }
                L[i] = l;
                R[i] = r;
            }

            // Safe Clip: look-ahead limiter to -0.5 dBFS, then the soft clipper as a ceiling.
            limiter.process (L, R, len, p.safeClip);
            if (p.safeClip)
                for (int i = 0; i < len; ++i)
                {
                    L[i] = safeClip (L[i]);
                    R[i] = safeClip (R[i]);
                }
            if (bad)
            {
                // Something blew up (should never happen): silence and clear every state that
                // could hold the NaN, including the reverb and the smoothers.
                for (auto& v : voices)
                {
                    v.filter[0].reset(); v.filter[1].reset();
                    v.energy = v.energySmoothed = v.powerSmoothed = 0.0f;
                    v.coherence = 1.0f;
                }
                drive.reset();
                reverb.reset();
                limiter.reset();
                sideHighPass[0].reset();
                sideHighPass[1].reset();
                levelSmoothed = 1.0f;
                gainSmoothed = -1.0f;
                driveSmoothed = 0.0f;
                resonanceSmoothed = -1.0f;
            }
            sampleClock += len;
            done += len;
        }
        fadedOut.store (fadeGain <= 0.0f, std::memory_order_release);
    }
}
