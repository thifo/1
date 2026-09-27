#pragma once

#include "DspCore.h"
#include "SourceData.h"
#include <atomic>

namespace thf::grain
{
    // Everything the engine needs for one block, in real units. The processor fills it from
    // the parameters; tests fill it directly.
    struct EngineParams
    {
        float position = 0.25f;     // 0..1 of the source
        float scan = 0.0f;          // playhead speed, x realtime of the source
        bool perNoteScan = false;
        bool freeze = false;
        float spray = 0.04f;        // 0..1 of the source, centred on the playhead
        float sizeMs = 120.0f;
        float density = 24.0f;      // grains per second per voice
        bool sync = false;
        double syncBeats = 0.25;
        double bpm = 120.0;
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
    };

    class GrainEngine
    {
    public:
        static constexpr int maxVoices = 16;
        static constexpr int maxGrains = 384;
        static constexpr int maxGrainsPerVoice = 48;
        static constexpr int controlBlock = 32;     // samples between parameter/LFO updates

        GrainEngine();

        void prepare (double sampleRate);
        void reset();
        void setSeed (uint32_t seed) noexcept { seed_ = seed; }

        // The source must stay alive while render() runs; nullptr = silence.
        void setSource (const SourceData* s) noexcept;

        void noteOn (int note, float velocity, const EngineParams&);
        void noteOff (int note, const EngineParams&);
        void allNotesOff (bool immediate);
        void setSustainPedal (bool down);
        void setHold (bool on);
        void setPitchBend (float bipolar) noexcept  { bend = bipolar; }
        void setModWheel (float unipolar) noexcept  { modWheel = unipolar; }
        void resetScan() noexcept;
        // Called once per host block when the LFO follows the transport.
        void syncLfo (double ppqPosition, double beatsPerCycle) noexcept { lfo.setPhase (ppqPosition / beatsPerCycle); }

        // Nearest allowed interval for a random pitch offset (semitones).
        static float quantizeInterval (float semitones, int mode) noexcept;

        // Renders and ADDS nothing: overwrites left/right with n samples.
        void render (float* left, float* right, int n, const EngineParams&);

        // Telemetry (audio thread writes, any thread reads).
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
            double readPos = 0;     // in samples of `level`
            double step = 1;
            float stretch = 1;      // sinc stretch (HQ)
            int age = 0, length = 1, startOffset = 0;
            float invLength = 1, fade = 0.5f;
            float gainL = 1, gainR = 1;
        };

        struct Voice
        {
            bool active = false, keyDown = false, sustained = false;
            int note = -1;
            float velocityGain = 1;
            uint64_t order = 0;
            dsp::Adsr amp;
            dsp::FilterEnvelope filterEnv;
            dsp::Svf filter[2];
            double countdown = 0;
            double scanOffset = 0;
            float currentNote = 60, targetNote = 60;
            int grains = 0;
            float cutoffSmoothed = 1000;
            bool firstBlock = true;

            // Voice stealing: the new note waits for a 3 ms fade of the old one.
            int pendingNote = -1;
            float pendingVelocityGain = 1;
            bool pendingKeyDown = false;
        };

        struct Modulation
        {
            float position = 0, spray = 0, sizeMul = 1, densityMul = 1, pitch = 0, cutoffMul = 1, level = 1;
        };

        void startVoice (Voice&, int note, float velocityGain, bool keyDown, const EngineParams&);
        int allocateVoice (const EngineParams&);
        void releaseVoice (Voice&);
        void processControl (const EngineParams&, int n);
        void spawnGrain (int voiceIndex, int offset, const EngineParams&, float sizeSamples, float fade, float normGain);
        void renderGrains (int n, bool hq);
        void killGrains (int voiceIndex);
        float velocityGain (float velocity, const EngineParams&) const;

        double sampleRate = 48000.0;
        uint32_t seed_ = 0x7f4a7c15u;
        const SourceData* source = nullptr;
        dsp::Random rng;
        dsp::WindowTable windowTable;
        dsp::SincTable sincTable;
        dsp::Lfo lfo;
        dsp::AdsrCoefs adsrCoefs;
        dsp::SvfCoefs svfCoefs[maxVoices];
        dsp::Drive drive;
        juce::Reverb reverb;
        juce::Reverb::Parameters reverbParams;
        int reverbTail = 0;          // samples to keep running the reverb after Space goes to 0
        float levelSmoothed = 1.0f;

        std::array<Voice, maxVoices> voices {};
        std::array<Grain, maxGrains> grains {};
        uint64_t noteCounter = 0;

        // Mono / legato note stack.
        std::array<std::pair<int, float>, 32> monoStack {};
        int monoCount = 0;

        double globalScan = 0.0;
        bool sustainPedal = false, holdOn = false;
        float bend = 0.0f, modWheel = 0.0f;
        float lfoValue = 0.0f;
        Modulation modulation;
        float driveSmoothed = 0.0f, gainSmoothed = 1.0f, resonanceSmoothed = 0.1f;

        // Per-voice scratch for one control block.
        float voiceBuffer[maxVoices][2][controlBlock] {};

        std::atomic<float> playheadForUi { 0.0f };
        std::atomic<int> activeVoicesForUi { 0 }, activeGrainsForUi { 0 };

        static constexpr int eventCapacity = 512;
        std::array<GrainEvent, eventCapacity> events {};
        std::atomic<uint32_t> eventWrite { 0 }, eventRead { 0 };
        void pushEvent (const GrainEvent&) noexcept;
    };
}
