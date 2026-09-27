#pragma once

#include "DspCore.h"
#include "DriveStage.h"
#include "SourceData.h"
#include <atomic>

namespace thf::grain
{
    // Everything the engine needs for one block, in real units. The processor fills it from
    // the parameters; tests fill it directly.
    struct EngineParams
    {
        float position = 0.25f;     // 0..1 of the region
        float regionStart = 0.0f;   // part of the source grains are taken from, 0..1
        float regionEnd = 1.0f;
        bool normalizeSource = true; // play user samples at a standard level
        float scan = 0.0f;          // playhead speed, x realtime of the source
        bool perNoteScan = false;
        int scanLoop = 0;           // at the region's end: 0 loop, 1 ping-pong, 2 once (stop)
        bool freeze = false;
        float spray = 0.04f;        // 0..1 of the region, centred on the playhead
        float sizeMs = 120.0f;
        float density = 24.0f;      // grains per second per voice
        bool sync = false;          // onsets on the tempo grid (host transport when it runs)
        double syncBeats = 0.25;
        double bpm = 120.0;
        bool linkVoices = false;    // all voices share onsets and random choices; only pitch differs
        float chaos = 0.3f;         // 0 = regular onsets, 1 = Poisson
        float window = 0.0f;        // 0 = Hann .. 1 = nearly rectangular
        float pitch = 0.0f;         // semitones, including fine tune
        float jitter = 0.0f;        // +/- semitones of random pitch per grain
        float reverse = 0.0f;       // probability of a reversed grain
        float stereo = 0.5f;        // random pan width
        int quantize = 0;           // pitch jitter snapping: off, octaves, fifths, major, minor
        int root = 60;

        int voices = 8;
        int voiceMode = 0;          // 0 poly, 1 mono, 2 legato
        float glideMs = 0.0f;
        float bendRange = 2.0f;
        float velocitySens = 0.6f;
        float attackMs = 30.0f, decayMs = 500.0f, sustain = 0.8f, releaseMs = 800.0f;

        int filterType = 0;
        float cutoff = 20000.0f;
        float resonance = 0.1f;
        float filterEnv = 0.0f;     // -1..1 = -/+ 5 octaves
        float filterDecayMs = 400.0f;
        float drive = 0.0f;

        float lfoRate = 0.5f, lfoDepth = 0.0f;
        int lfoShape = 0, lfoTarget = 0;
        int modTarget = 1;
        float modDepth = 0.5f;
        bool lfoSync = false;
        double lfoBeats = 1.0;      // beats per cycle when synced

        float space = 0.0f;         // reverb amount
        float spaceSize = 0.6f;

        float outputGain = 1.0f;
        bool safeClip = true;
        bool hq = false;
    };

    // Grain spawn notification for the display (audio thread -> UI, lock-free).
    struct GrainEvent
    {
        float position = 0;     // 0..1, start of the material the grain plays
        float span = 0;         // 0..1, length of that material
        float pan = 0;          // -1..1
        float seconds = 0;      // grain duration
        int voice = 0;
        bool reversed = false;
        int64_t time = 0;       // engine sample clock at the grain's first sample
    };

    class GrainEngine
    {
    public:
        static constexpr int maxVoices = 16;
        static constexpr int maxGrainsPerVoice = 48;
        static constexpr int maxGrains = maxVoices * maxGrainsPerVoice;   // no voice ever starves
        static constexpr int maxControlBlock = 128; // longest render slice; parameters and LFO
                                                    // update every ~0.67 ms at any rate

        GrainEngine();

        void prepare (double sampleRate);
        void reset();
        void setSeed (uint32_t seed) noexcept { seed_ = seed; }

        // Switches the source. Grains of the old one fade out over 8 ms (no click); the engine
        // keeps a reference until they are gone, so the caller may drop its own at any time.
        // The engine never frees a source: the last reference must live elsewhere (the
        // processor's retired list or the factory set), so releasing here only decrements.
        void setSource (SourceData::Ptr s) noexcept;
        void setSource (const SourceData* s) noexcept { setSource (SourceData::Ptr (const_cast<SourceData*> (s))); }

        void noteOn (int note, float velocity, const EngineParams&);
        // A note that plays from its own position (a cue pad): `id` identifies it for
        // noteOff, `pitchNote` sets the pitch, `anchor` is the position in the region (0..1).
        void noteOnAt (int id, int pitchNote, float velocity, float anchor, const EngineParams&);
        void noteOff (int note, const EngineParams&);
        void allNotesOff (bool immediate);
        void setSustainPedal (bool down);
        void setHold (bool on);
        void setPitchBend (float bipolar) noexcept  { bend = bipolar; }
        void setModWheel (float unipolar) noexcept  { modWheel = unipolar; }
        // Aftertouch / pad pressure: modulates like the mod strip (whichever is higher).
        void setPressure (float unipolar) noexcept  { pressure = unipolar; }
        void resetScan() noexcept;
        // 5 ms fade of the whole output (bulk parameter changes). isFadedOut() is true once
        // the output has reached silence; any thread may read it. The output comes back by
        // itself after 100 ms of audio if nobody releases it (a blocked message thread).
        void setFadedOut (bool out) noexcept { fadeTarget.store (out ? 0.0f : 1.0f); }
        bool isFadedOut() const noexcept     { return fadedOut.load (std::memory_order_acquire); }
        // Once per host block: where the song is. While the transport runs, Sync onsets sit on
        // its beat grid; otherwise the engine keeps its own clock (restarted by a new phrase).
        void setTransport (bool playing, double ppqPosition) noexcept;
        // Called once per host block when the LFO follows the transport.
        void syncLfo (double ppqPosition, double beatsPerCycle) noexcept { lfo.setPhase (ppqPosition / beatsPerCycle, rng); }

        // Nearest allowed interval for a random pitch offset (semitones).
        static float quantizeInterval (float semitones, int mode) noexcept;

        // Position + scan to a playhead in the region (0..1) for a Scan Loop mode.
        static double mapPlayhead (double position, int scanLoop) noexcept;

        // Renders and ADDS nothing: overwrites left/right with n samples.
        void render (float* left, float* right, int n, const EngineParams&);

        // Telemetry (audio thread writes, any thread reads). The playhead is 0..1 of the region.
        float getPlayhead() const noexcept    { return playheadForUi.load (std::memory_order_relaxed); }
        int getActiveVoices() const noexcept  { return activeVoicesForUi.load (std::memory_order_relaxed); }
        int getActiveGrains() const noexcept  { return activeGrainsForUi.load (std::memory_order_relaxed); }
        int popGrainEvents (GrainEvent* dest, int maxEvents);

    private:
        struct Grain
        {
            bool active = false;
            int voice = 0;
            int level = 0;
            double readPos = 0;     // in samples of copy `level`
            double step = 1;
            int age = 0, length = 1, startOffset = 0;
            float invLength = 1, fade = 0.5f, invFade = 2.0f;
            float gainL = 1, gainR = 1;
            const SourceData* src = nullptr;
            int fadeOut = 0;        // > 0: samples left of a fade-out (source switch)
        };

        // The random choices of one grain, drawn in a fixed order (reproducible renders);
        // linked voices share them.
        struct GrainDraw { float jitter = 0, spray = 0, reverse = 1, pan = 0; };

        // Grain length, window and gain for this control block.
        struct GrainShape { float sizeSamples = 0, fade = 0.5f, normGain = 1; double interval = 1; };

        struct Voice
        {
            bool active = false, keyDown = false, sustained = false;
            int note = -1;
            float velocityGain = 1;
            uint64_t order = 0;
            dsp::Adsr amp;
            dsp::FilterEnvelope filterEnv;
            dsp::Svf filter[2];
            double phase = 0;       // intervals until the next onset (free timing)
            double delayed = -1;    // samples until a chaos-delayed Sync onset, -1 = none
            double holdoff = 0;     // samples during which Sync ticks are skipped (note-on grain)
            double scanOffset = 0;
            float currentNote = 60, targetNote = 60;
            float anchor = -1;      // own position (cue pads), -1 = follow Position
            int grains = 0;
            float cutoffSmoothed = 1000;
            bool firstBlock = true;

            // Loudness of coherent grains: power of the grain sum vs. sum of grain powers.
            float energy = 0, energySmoothed = 0, powerSmoothed = 0, coherence = 1;

            // Voice stealing: the new note waits for the fade of the old one.
            int pendingNote = -1, pendingPitch = -1;
            float pendingVelocityGain = 1, pendingAnchor = -1;
            bool pendingKeyDown = false;
        };

        struct Modulation
        {
            float position = 0, spray = 0, sizeMul = 1, densityMul = 1, pitch = 0, cutoffMul = 1, level = 1;
        };

        void startVoice (Voice&, int note, float velocityGain, bool keyDown, const EngineParams&);
        static double advanceScan (double scan, double step, int scanLoop) noexcept;
        // Pitch and position of the note being started (noteOnAt), -1 = the note itself / Position.
        int nextPitch = -1;
        float nextAnchor = -1.0f;
        float pitchFor (int note) const noexcept { return nextPitch >= 0 ? (float) nextPitch : (float) note; }
        int allocateVoice (const EngineParams&);
        void releaseVoice (Voice&);
        float stealSamples (int note) const noexcept;
        GrainShape computeShape (const EngineParams&) const;
        GrainDraw draw (dsp::Random&) noexcept;
        void processControl (const EngineParams&, int n);
        Grain* spawnGrain (int voiceIndex, int offset, const EngineParams&, const GrainShape&, const GrainDraw&);
        template <typename Kernel> void renderGrains (int n, const Kernel&);
        void killGrains (int voiceIndex);
        float velocityGain (float velocity, const EngineParams&) const;

        double sampleRate = 48000.0;
        int controlLength = 32;
        uint32_t seed_ = 0x7f4a7c15u;
        const SourceData* source = nullptr;
        SourceData::Ptr heldSource, heldPrevious;
        int nextGrain = 0;
        int switchFadeSamples = 384;
        dsp::Random rng;
        dsp::WindowTable windowTable;
        dsp::GrainKernel kernel { 7.0, 0.8 };
        dsp::GrainKernelHq kernelHq { 9.0, 0.8 };
        dsp::Lfo lfo;
        dsp::AdsrCoefs adsrCoefs;
        dsp::SvfCoefs svfCoefs[maxVoices];
        dsp::DriveStage drive;
        juce::Reverb reverb;
        juce::Reverb::Parameters reverbParams;
        int reverbTail = 0;          // samples to keep running the reverb after Space goes to 0
        float levelSmoothed = 1.0f;
        float fadeGain = 1.0f;
        std::atomic<float> fadeTarget { 1.0f };
        int fadedSamples = 0;
        std::atomic<bool> fadedOut { false };

        std::array<Voice, maxVoices> voices {};
        std::array<Grain, maxGrains> grains {};
        uint64_t noteCounter = 0;

        // Mono / legato note stack.
        std::array<std::pair<int, float>, 32> monoStack {};
        int monoCount = 0;

        // Timing shared by all voices.
        GrainShape shape;
        double beatClock = 0.0;      // beats; follows the host while its transport runs
        bool hostPlaying = false;
        double linkPhase = 0.0;      // free timing of linked voices
        double linkDelayed = -1.0;
        GrainDraw linkDraw;
        uint32_t linkSeed = 1;       // note-on grains of linked voices started together match
        int64_t sampleClock = 0;

        // Filter type changes crossfade over 5 ms.
        int filterTypeNow = -1, filterTypePrevious = 0, typeFadeLeft = 0, typeFadeLength = 240;
        float typeMix[maxControlBlock] {};

        double globalScan = 0.0;
        bool sustainPedal = false, holdOn = false;
        float bend = 0.0f, modWheel = 0.0f, pressure = 0.0f;
        float lfoValue = 0.0f;
        Modulation modulation;
        float driveSmoothed = 0.0f, gainSmoothed = 1.0f, resonanceSmoothed = 0.1f;

        // Per-voice scratch for one control block.
        float voiceBuffer[maxVoices][2][maxControlBlock] {};

        std::atomic<float> playheadForUi { 0.0f };
        std::atomic<int> activeVoicesForUi { 0 }, activeGrainsForUi { 0 };

        static constexpr int eventCapacity = 512;
        std::array<GrainEvent, eventCapacity> events {};
        std::atomic<uint32_t> eventWrite { 0 }, eventRead { 0 };
        void pushEvent (const GrainEvent&) noexcept;
    };
}
