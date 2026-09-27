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
        reset();
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
        drive.reset();
        globalScan = 0.0;
        monoCount = 0;
        noteCounter = 0;
        sustainPedal = holdOn = false;
        bend = modWheel = 0.0f;
        driveSmoothed = 0.0f;
        gainSmoothed = -1.0f;      // "not initialised": the first block jumps to the target
        resonanceSmoothed = -1.0f;
        activeVoicesForUi.store (0);
        activeGrainsForUi.store (0);
    }

    void GrainEngine::setSource (const SourceData* s) noexcept
    {
        if (s == source)
            return;
        // Grains point into the old source's octave copies: drop them.
        for (auto& g : grains)
            g.active = false;
        for (auto& v : voices)
            v.grains = 0;
        source = s;
    }

    void GrainEngine::resetScan() noexcept
    {
        globalScan = 0.0;
        for (auto& v : voices)
            v.scanOffset = 0.0;
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
            globalScan = 0.0;     // a new phrase starts at Position

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
        v.countdown = 0.0;
        v.scanOffset = 0.0;
        v.targetNote = (float) note;
        v.currentNote = glideFromPrevious ? previousNote : (float) note;
        v.firstBlock = true;
        v.pendingNote = -1;

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
                if (voices[i].active) voices[i].amp.steal();

            if (v.active && ! v.amp.isReleasing() && p.voiceMode == 2)
            {
                v.note = note;
                v.targetNote = (float) note;
                v.keyDown = true;
                if (p.glideMs <= 0.0f) v.currentNote = (float) note;
            }
            else if (v.active && v.amp.getStage() != dsp::Adsr::Stage::steal)
            {
                // Mono retrigger from the current level: no reset, no click.
                v.note = note;
                v.targetNote = (float) note;
                v.keyDown = true;
                v.sustained = false;
                v.velocityGain = gain;
                v.order = ++noteCounter;
                if (p.glideMs <= 0.0f) v.currentNote = (float) note;
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
            v.amp.steal();
            v.pendingNote = note;
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
    void GrainEngine::spawnGrain (int voiceIndex, int offset, const EngineParams& p, float sizeSamples,
                                  float fade, float normGain)
    {
        auto& v = voices[(size_t) voiceIndex];
        if (source == nullptr || v.grains >= maxGrainsPerVoice)
            return;

        Grain* g = nullptr;
        for (auto& candidate : grains)
            if (! candidate.active) { g = &candidate; break; }
        if (g == nullptr)
            return;

        // Random draws happen in a fixed order so renders are reproducible.
        const auto rJitter = rng.bipolar();
        const auto rSpray = rng.uniform() - 0.5f;
        const auto rReverse = rng.uniform();
        const auto rPan = rng.bipolar();

        const auto length0 = (double) source->getLength();
        const auto semis = (v.currentNote - (float) p.root) + p.pitch + bend * p.bendRange
                         + modulation.pitch + p.jitter * rJitter;
        const auto ratio = juce::jlimit (1.0 / 64.0, 32.0,
                                         std::exp2 ((double) semis / 12.0) * source->getSampleRate() / sampleRate);
        const auto span0 = (double) sizeSamples * ratio;

        const auto scanOffset = p.perNoteScan ? v.scanOffset : globalScan;
        const auto spray = juce::jlimit (0.0f, 1.0f, p.spray + modulation.spray);
        const auto start01 = wrap01 ((double) p.position + modulation.position + scanOffset + rSpray * spray);
        const auto start0 = juce::jlimit (0.0, juce::jmax (0.0, length0 - span0), (double) start01 * length0);
        const bool reversed = rReverse < p.reverse;

        // Octave copy: HQ reads the copy just below the ratio and band-limits the rest with a
        // stretched sinc; normal mode reads the nearest copy with Hermite interpolation.
        int level = 0;
        if (ratio > 1.0)
            level = (int) std::floor (std::log2 (ratio) + (p.hq ? 0.0 : 0.5));
        level = juce::jlimit (0, source->getNumLevels() - 1, level);
        const auto scale = std::ldexp (1.0, -level);
        const auto effective = ratio * scale;

        g->active = true;
        g->voice = voiceIndex;
        g->level = level;
        g->step = reversed ? -effective : effective;
        g->readPos = (reversed ? start0 + span0 - ratio : start0) * scale;
        g->stretch = p.hq ? (float) juce::jmax (1.0, effective) : 1.0f;
        g->age = 0;
        g->length = juce::jmax (1, (int) sizeSamples);
        g->invLength = 1.0f / (float) g->length;
        g->fade = fade;
        g->startOffset = offset;

        const auto pan = p.stereo * rPan;
        const auto angle = (pan + 1.0f) * dsp::pi * 0.25f;
        g->gainL = std::cos (angle) * 1.41421356f * normGain;
        g->gainR = std::sin (angle) * 1.41421356f * normGain;
        ++v.grains;

        pushEvent ({ (float) (start0 / length0), (float) (span0 / length0), pan,
                     sizeSamples / (float) sampleRate, voiceIndex, reversed });
    }

    void GrainEngine::processControl (const EngineParams& p, int n)
    {
        const auto sr = (float) sampleRate;
        adsrCoefs.set (sr, p.attackMs, p.decayMs, p.sustain, p.releaseMs);

        lfoValue = lfo.advance (n, p.lfoRate, sr, (dsp::Lfo::Shape) p.lfoShape, rng);
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
        apply (p.modTarget, modWheel * p.modDepth);

        const auto smooth20ms = 1.0f - std::exp (-(float) n / (0.02f * sr));
        const auto smooth5ms = 1.0f - std::exp (-(float) n / (0.005f * sr));
        if (resonanceSmoothed < 0.0f) resonanceSmoothed = p.resonance;
        resonanceSmoothed += (p.resonance - resonanceSmoothed) * smooth20ms;
        driveSmoothed += (p.drive - driveSmoothed) * smooth20ms;

        // Playhead.
        double scanStep = 0.0;
        if (source != nullptr && ! p.freeze)
            scanStep = (double) p.scan * source->getSampleRate() / (sampleRate * source->getLength()) * n;
        globalScan = wrap01 (globalScan + scanStep);

        // Grain timing and size (shared by all voices this block).
        auto density = p.sync ? 1.0 / juce::jmax (1.0e-3, p.syncBeats * 60.0 / juce::jmax (1.0, p.bpm))
                              : (double) p.density * modulation.densityMul;
        density = juce::jlimit (0.1, 1000.0, density);
        const auto interval = sampleRate / density;
        const auto sizeSamples = juce::jlimit (0.002f * sr, 4.0f * sr, p.sizeMs * modulation.sizeMul * 0.001f * sr);
        const auto fade = dsp::windowFade (p.window, sizeSamples, sr);
        const auto overlap = juce::jmin ((double) maxGrainsPerVoice, density * sizeSamples / sampleRate);
        const auto normGain = 1.0f / std::sqrt (juce::jmax (1.0f, (float) overlap * dsp::windowEnergy (fade)));

        int activeVoices = 0;
        float newestOffset = -1.0f;
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

            v.scanOffset = wrap01 (v.scanOffset + scanStep);
            if (v.order > newestOrder) { newestOrder = v.order; newestOffset = (float) v.scanOffset; }

            v.filterEnv.set (sr, p.filterDecayMs);
            const auto env = v.filterEnv.advance (n);
            const auto cutoff = juce::jlimit (10.0f, 0.49f * sr,
                                              p.cutoff * modulation.cutoffMul * std::exp2 (p.filterEnv * 5.0f * env));
            if (v.firstBlock) v.cutoffSmoothed = cutoff;
            v.cutoffSmoothed *= std::exp2 (std::log2 (cutoff / v.cutoffSmoothed) * smooth5ms);
            svfCoefs[vi].set (v.cutoffSmoothed, resonanceSmoothed, sr);
            v.firstBlock = false;

            const bool spawning = v.amp.getStage() != dsp::Adsr::Stage::steal;
            while (v.countdown < (double) n)
            {
                if (spawning)
                    spawnGrain (vi, juce::jmax (0, (int) v.countdown), p, sizeSamples, fade, normGain);
                const auto jitter = (1.0f - p.chaos) + p.chaos * rng.exponential();
                v.countdown += juce::jmax (1.0, interval * jitter);
            }
            v.countdown -= (double) n;
        }

        const auto shownScan = p.perNoteScan && newestOffset >= 0.0f ? (double) newestOffset : globalScan;
        playheadForUi.store (wrap01 ((double) p.position + shownScan), std::memory_order_relaxed);
        activeVoicesForUi.store (activeVoices, std::memory_order_relaxed);
    }

    void GrainEngine::renderGrains (int n, bool hq)
    {
        if (source == nullptr)
            return;
        const bool stereoSource = source->getNumChannels() > 1;
        constexpr int margin = SourceData::padding - 2 * dsp::SincTable::halfTaps - 4;

        int active = 0;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;
            ++active;
            auto* outL = voiceBuffer[g.voice][0];
            auto* outR = voiceBuffer[g.voice][1];
            const auto* srcL = source->channel (g.level, 0);
            const auto* srcR = source->channel (g.level, 1);
            const auto levelLength = source->getLevelLength (g.level);

            for (int i = g.startOffset; i < n; ++i)
            {
                if (g.age >= g.length)
                    break;
                const auto w = windowTable.value ((float) g.age * g.invLength, g.fade);
                const auto ip = (int) std::floor (g.readPos);
                const auto frac = (float) (g.readPos - ip);
                float l = 0.0f, r = 0.0f;
                if (ip > -margin && ip < levelLength + margin)
                {
                    if (hq)
                    {
                        float weights[dsp::SincTable::maxWeights];
                        int first = 0;
                        const auto count = sincTable.weights (frac, g.stretch, 1.0f / g.stretch, weights, first);
                        l = dsp::SincTable::dot (srcL + ip + first, weights, count);
                        r = stereoSource ? dsp::SincTable::dot (srcR + ip + first, weights, count) : l;
                    }
                    else
                    {
                        l = dsp::hermite (srcL + ip, frac);
                        r = stereoSource ? dsp::hermite (srcR + ip, frac) : l;
                    }
                }
                outL[i] += l * w * g.gainL;
                outR[i] += r * w * g.gainR;
                g.readPos += g.step;
                ++g.age;
            }
            g.startOffset = 0;
            if (g.age >= g.length)
            {
                g.active = false;
                --voices[(size_t) g.voice].grains;
            }
        }
        activeGrainsForUi.store (active, std::memory_order_relaxed);
    }

    //==============================================================================
    void GrainEngine::render (float* left, float* right, int n, const EngineParams& p)
    {
        if (gainSmoothed < 0.0f)
            gainSmoothed = p.outputGain;

        const auto filterType = (dsp::Svf::Type) juce::jlimit (0, 2, p.filterType);
        int done = 0;
        while (done < n)
        {
            const auto len = juce::jmin (controlBlock, n - done);
            processControl (p, len);

            for (int v = 0; v < maxVoices; ++v)
                if (voices[(size_t) v].active)
                    for (int ch = 0; ch < 2; ++ch)
                        std::fill (voiceBuffer[v][ch], voiceBuffer[v][ch] + len, 0.0f);

            renderGrains (len, p.hq);

            auto* L = left + done;
            auto* R = right + done;
            std::fill (L, L + len, 0.0f);
            std::fill (R, R + len, 0.0f);

            for (int vi = 0; vi < maxVoices; ++vi)
            {
                auto& v = voices[(size_t) vi];
                if (! v.active)
                    continue;
                const auto& coefs = svfCoefs[vi];
                for (int i = 0; i < len; ++i)
                {
                    const auto env = v.amp.process (adsrCoefs) * v.velocityGain;
                    L[i] += v.filter[0].process (voiceBuffer[vi][0][i], coefs, filterType) * env;
                    R[i] += v.filter[1].process (voiceBuffer[vi][1][i], coefs, filterType) * env;
                }

                if (! v.amp.isActive())
                {
                    if (v.pendingNote >= 0)
                        startVoice (v, v.pendingNote, v.pendingVelocityGain, v.pendingKeyDown, p);
                    else
                    {
                        v.active = false;
                        killGrains (vi);
                    }
                }
            }

            // Drive: +0..30 dB into an anti-aliased tanh; small signals gain pre^0.25.
            if (driveSmoothed > 1.0e-3f)
            {
                const auto pre = std::exp2 (driveSmoothed * 30.0f / 6.0206f);
                const auto makeup = std::pow (pre, -0.75f);
                for (int i = 0; i < len; ++i)
                {
                    L[i] = drive.process (0, L[i] * pre) * makeup;
                    R[i] = drive.process (1, R[i] * pre) * makeup;
                }
            }
            else
            {
                drive.reset();
            }

            // Output gain ramps linearly across the control block. The fixed -6 dB leaves room
            // for several voices of dense, correlated grains before Safe Clip has to act.
            constexpr float headroom = 0.5f;
            const auto startGain = gainSmoothed;
            gainSmoothed += (p.outputGain - gainSmoothed) * (1.0f - std::exp (-(float) len / (0.01f * (float) sampleRate)));
            const auto gainStep = (gainSmoothed - startGain) / (float) len;
            bool bad = false;
            for (int i = 0; i < len; ++i)
            {
                const auto gain = (startGain + gainStep * (float) (i + 1)) * headroom;
                auto l = L[i] * gain, r = R[i] * gain;
                if (p.safeClip) { l = safeClip (l); r = safeClip (r); }
                if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; bad = true; }
                L[i] = l;
                R[i] = r;
            }
            if (bad)
            {
                // Something blew up (should never happen): silence and clear filter state.
                for (auto& v : voices) { v.filter[0].reset(); v.filter[1].reset(); }
                drive.reset();
            }
            done += len;
        }
    }
}
