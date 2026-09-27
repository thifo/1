#pragma once

#include "ControlLayout.h"
#include "Parameters.h"
#include "Presets.h"
#include "dsp/GrainEngine.h"
#include "dsp/SourceData.h"
#include <midi/ControllerInput.h>

namespace thf::grain
{
    // Built-in sources, generated once per process in the background and shared by all
    // instances. Entries never change after they are published.
    class FactorySources
    {
    public:
        FactorySources();
        ~FactorySources();
        const SourceData* get (int sourceChoice) const noexcept;
        void waitUntilReady() const;

    private:
        static constexpr int count = 10;   // = sourceChoices.size()
        std::array<SourceData::Ptr, count> owned;
        std::array<std::atomic<const SourceData*>, count> published {};
        std::thread worker;
    };

    class GrainProcessor : public juce::AudioProcessor,
                           private juce::Timer,
                           private juce::ValueTree::Listener
    {
    public:
        GrainProcessor();
        ~GrainProcessor() override;

        //==============================================================================
        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override  { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override;

        int getNumPrograms() override    { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return "Default"; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        //==============================================================================
        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
        juce::UndoManager& getUndoManager() noexcept           { return undoManager; }
        PresetManager& getPresets() noexcept                   { return presets; }
        GrainEngine& getEngine() noexcept                      { return engine; }
        // Notes from the on-screen / computer keyboard (and a mirror of incoming notes).
        juce::MidiKeyboardState& getKeyboardState() noexcept   { return keyboardState; }

        // Sample handling (message thread).
        void loadSampleAsync (const juce::File&);
        bool loadSampleSync (const juce::File&, juce::String& error);
        void setUserSample (SourceData::Ptr);
        SourceData::Ptr getUserSample() const;
        const SourceData* getFactorySource (int choice) const noexcept { return factory->get (choice); }
        // What the engine plays now (message thread; may be null).
        SourceData::Ptr getCurrentSourceForDisplay() const;
        juce::String getMissingSamplePath() const   { const juce::ScopedLock l (statusLock); return missingSamplePath; }
        juce::String getLastLoadError() const       { const juce::ScopedLock l (statusLock); return lastLoadError; }
        bool isLoading() const noexcept             { return loading.load(); }
        // Changes whenever the sample, its status or the load state changes (UI polls it).
        int getSourceSerial() const noexcept        { return sourceSerial.load(); }

        // Cue points 0..1, -1 = empty.
        float getCue (int index) const noexcept     { return cues[(size_t) index].load(); }
        void setCue (int index, float value)        { cues[(size_t) index].store (value); }
        void jumpToCue (int index);
        // Restart scanning from Position (thread-safe; applied at the next block).
        void requestScanReset() noexcept            { scanResetRequested.store (true); }

        // Controller page (encoders): 0 engine, 1 tone.
        int getPage() const noexcept                { return page.load(); }
        void setPage (int p) noexcept               { page.store (juce::jlimit (0, layout::numPages - 1, p)); }

        // Bank A pad function (0-7), same as pressing the hardware pad. Any thread.
        void performPadAction (int pad);

        // A/B comparison of parameter sets.
        void toggleAB();
        int getABSlot() const noexcept              { return abSlot; }

        // MIDI settings and learn.
        midi::EncoderMode getEncoderMode() const noexcept { return (midi::EncoderMode) encoderMode.load(); }
        void setEncoderMode (midi::EncoderMode m)         { encoderMode.store ((int) m); }
        bool getPadsAsControls() const noexcept     { return padsAsControls.load(); }
        void setPadsAsControls (bool b)             { padsAsControls.store (b); }
        int getPadChannel() const noexcept          { return padChannel.load(); }
        void setPadChannel (int ch)                 { padChannel.store (juce::jlimit (1, 16, ch)); }
        void startLearn (const juce::String& paramId);
        void cancelLearn()                          { learnTarget.store (-1); }
        bool isLearning() const noexcept            { return learnTarget.load() >= 0; }
        juce::String getLearningParam() const;
        void clearLearned (const juce::String& paramId);
        void clearAllLearned();
        // CC that moves this parameter now: learned first, then the controller template for the
        // current page. -1 if none.
        int getCcFor (const juce::String& paramId) const;
        bool isLearned (const juce::String& paramId) const;

        // Last parameter moved from hardware, for the status line (-1 = none).
        int getLastTouchedParam() const noexcept    { return lastTouchedParam.load(); }
        int getLastTouchedSerial() const noexcept   { return lastTouchedSerial.load(); }
        // Hardware strips, 0..1 (pitch: 0.5 = centre) for the on-screen mirrors.
        float getPitchStrip() const noexcept        { return pitchStrip.load(); }
        float getModStrip() const noexcept          { return modStrip.load(); }
        // Pad flash: index 0-7 bank A, 8-15 bank B; serial changes on every hit.
        int getLastPad() const noexcept             { return lastPad.load(); }
        int getLastPadSerial() const noexcept       { return lastPadSerial.load(); }
        std::pair<float, float> getOutputPeaks() noexcept
        {
            return { peakL.exchange (0.0f), peakR.exchange (0.0f) };
        }

        juce::RangedAudioParameter* param (juce::StringRef id) const { return state.getParameter (id); }
        int indexOf (juce::StringRef id) const;

        // Fills engine parameters from the current parameter values.
        EngineParams makeEngineParams() const;

        static constexpr int stateVersion = 1;

    private:
        void timerCallback() override;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

        void handleMidi (const juce::MidiMessage&, const EngineParams&);
        bool handleController (int cc, int value);
        void handlePad (int pad, bool noteOn, float velocity);
        void nudgeParam (juce::RangedAudioParameter*, int ticks);
        void setParamFromAudio (juce::RangedAudioParameter*, float normalised);
        void touched (juce::RangedAudioParameter*, int cc);
        void publishSource (SourceData::Ptr);

        juce::ValueTree saveExtraState (bool includeMidi) const;
        void restoreExtraState (const juce::ValueTree&, bool includeMidi);
        friend class PresetManager;

        juce::UndoManager undoManager;
        juce::AudioProcessorValueTreeState state;
        PresetManager presets { *this };

        juce::SharedResourcePointer<FactorySources> factory;
        juce::MidiKeyboardState keyboardState;
        GrainEngine engine;

        // Parameter pointers cached for the audio thread (no string lookups there).
        std::vector<juce::RangedAudioParameter*> params;
        std::array<std::array<juce::RangedAudioParameter*, layout::numEncoders>, layout::numPages> encoderSlots {};
        std::array<juce::RangedAudioParameter*, layout::numFaders> faderSlots {};
        juce::RangedAudioParameter* mainEncoderSlot = nullptr;
        juce::RangedAudioParameter* positionParam = nullptr;
        struct Raw;
        std::unique_ptr<Raw> raw;

        // User sample: swapped on the message thread under the lock; the audio thread only
        // tries the lock and keeps its own reference. Old samples are freed by the timer.
        mutable juce::SpinLock sourceLock;
        SourceData::Ptr userSample;
        SourceData::Ptr audioSource;
        std::vector<SourceData::Ptr> retired;
        juce::ThreadPool loader { 1 };
        std::atomic<bool> loading { false };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
        juce::CriticalSection statusLock;
        juce::String missingSamplePath, lastLoadError;
        std::atomic<int> sourceSerial { 0 };

        std::array<std::atomic<float>, 8> cues;
        std::atomic<int> page { 0 };
        int abSlot = 0;
        std::array<juce::ValueTree, 2> abStates;
        std::atomic<bool> abRequested { false };

        // MIDI mapping.
        std::atomic<int> encoderMode { (int) midi::EncoderMode::binaryOffset };
        std::atomic<bool> padsAsControls { true };
        std::atomic<int> padChannel { layout::minilab3::padChannel };
        std::array<std::atomic<int>, 128> learned;     // CC -> parameter index, -1 = none
        std::array<midi::Pickup, 128> pickups;
        std::array<float, 128> encoderRemainder {};
        std::atomic<int> learnTarget { -1 };
        std::atomic<int> lastTouchedParam { -1 }, lastTouchedSerial { 0 };
        std::atomic<float> pitchStrip { 0.5f }, modStrip { 0.0f };
        std::atomic<int> lastPad { -1 }, lastPadSerial { 0 };
        std::array<int64_t, 8> cuePressTime {};
        std::array<float, 8> cuePressPlayhead {};

        std::atomic<bool> scanResetRequested { false };
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
        int64_t samplesProcessed = 0;
        juce::uint32 lastTreeChangeMs = 0;
        double hostSampleRate = 48000.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainProcessor)
    };
}
