#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace thf::grain
{
    // An immutable, ready-to-play grain source: band-limited copies of the audio that grains
    // read from, and a peak overview for the waveform display. Built off the audio thread,
    // then handed over by reference count.
    //
    // Copy j is stored at 2 * 2^(-j/2) times the sample's rate and holds content up to a
    // quarter of its own rate. A grain reads the copy that needs at most 2 stored samples per
    // output sample: nothing folds back above Nyquist, the top of the band is at least 0.33 of
    // the output rate, and the 2x storage lets a short kernel remove interpolation images.
    class SourceData : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<SourceData>;

        static constexpr int maxLevels = 11;      // half-octave steps: pitch up to +5 octaves
        static constexpr double contentFraction = 0.23;   // of each copy's storage rate
        static constexpr int padding = 48;        // zeros around each level for interpolation
        static constexpr int overviewSize = 2048; // peak buckets for the display

        // Copies and sanitises the audio (non-finite samples become 0, DC removed), converts it
        // to targetRate if given (band-limited sinc), builds levels and peaks.
        static Ptr fromBuffer (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name,
                               double targetRate = 0.0);

        int getNumChannels() const noexcept     { return numChannels; }
        int getLength() const noexcept          { return length; }
        double getSampleRate() const noexcept   { return sampleRate; }
        double getDurationSeconds() const noexcept { return (double) length / sampleRate; }
        int getNumLevels() const noexcept       { return (int) levels.size(); }
        int getLevelLength (int level) const noexcept { return levelLengths[(size_t) level]; }
        // Stored samples of copy `level` per sample of the source.
        static double levelScale (int level) noexcept { return 2.0 * std::exp2 (-0.5 * level); }
        const juce::String& getName() const noexcept  { return name; }

        // Pointer to sample 0 of a copy; indices -padding .. length+padding-1 are readable.
        const float* channel (int level, int ch) const noexcept
        {
            return levels[(size_t) level].getReadPointer (juce::jmin (ch, numChannels - 1)) + padding;
        }

        // The audio at the source's own rate (analysis, tests): every other sample of copy 0.
        float sample (int ch, int index) const noexcept { return channel (0, ch)[2 * index]; }

        // Gain that brings the sample's peak to the level of the built-in sources (-3 dBFS).
        float getNormalGain() const noexcept { return normalGain; }

        const std::vector<float>& getPeakMin() const noexcept { return peakMin; }
        const std::vector<float>& getPeakMax() const noexcept { return peakMax; }

        // Min/max of all channels between start and end (0..1 of the sample), `buckets`
        // columns, for a zoomed waveform: blocks of a peak pyramid, raw samples up close.
        void getPeaks (double start, double end, int buckets, float* mins, float* maxs) const;

        // Attacks (sample positions at the source's rate), in time order, with their strength.
        struct Onset { int position = 0; float strength = 0.0f; };
        const std::vector<Onset>& getOnsets() const noexcept { return onsets; }

        // The zero crossing of the mono mix closest to `index`, within `radius` samples
        // (index itself if there is none).
        int nearestZeroCrossing (int index, int radius) const;

        // Metadata carried alongside, not used by the audio thread.
        juce::File file;
        juce::String contentHash;
        juce::MemoryBlock embeddedFlac;   // for sessions; empty if the sample is too long
        float detectedNote = -1.0f;       // fractional MIDI note of the sample's pitch, -1 = none
        float pitchConfidence = 0.0f;     // 0..1
        bool pitchFromFile = false;       // from the file's own root-note metadata
        double originalRate = 0.0;        // the file's rate (the data may be converted)
        float originalPeak = 0.0f;

    private:
        SourceData() = default;

        int numChannels = 1, length = 0;
        double sampleRate = 48000.0;
        juce::String name;
        std::vector<juce::AudioBuffer<float>> levels;
        std::vector<int> levelLengths;
        std::vector<float> peakMin, peakMax;
        static constexpr int pyramidBlocks[] = { 16, 256, 4096 };
        std::array<std::vector<float>, 3> pyramidMin, pyramidMax;
        std::vector<Onset> onsets;
        void buildPyramid();
        friend void detectOnsetsInto (SourceData&);
        float normalGain = 1.0f;
    };

    // Loading and generation helpers (message or background thread only).
    namespace sources
    {
        // Limits for files: length and memory (frames x channels at the playback rate; the
        // grain copies take about 6.7 times that in floats, ~640 MB at the limit).
        inline constexpr double maxFileSeconds = 600.0;
        inline constexpr juce::int64 maxTotalSamples = 24 * 1024 * 1024;
        // Samples up to this length travel inside the session (FLAC of the original audio).
        inline constexpr double maxEmbedSeconds = 60.0;

        struct LoadOptions
        {
            double targetRate = 0.0;               // convert to the host rate (0 = keep)
            std::function<bool()> cancelled;       // polled while reading; true = give up
            bool embed = true;                     // prepare the FLAC for the session
            juce::int64 maxSamples = maxTotalSamples;   // frames x channels this load may take
        };

        // Reads any format the platform knows. Returns nullptr and fills `error` on failure
        // (including running out of memory: never throws).
        SourceData::Ptr loadFile (const juce::File&, juce::String& error, const LoadOptions& = {});

        // Decodes a file image kept in memory (sessions embed samples as FLAC); the image
        // itself becomes the embedded copy.
        SourceData::Ptr loadFromMemory (const void* data, size_t size, const juce::String& name, juce::String& error,
                                        const LoadOptions& = {});

        // Encodes audio as 24-bit FLAC for embedding into the session.
        juce::MemoryBlock encodeFlac (const juce::AudioBuffer<float>& audio, double sampleRate);

        // Built-in sources (choice index 1..5 of the Source parameter), rendered on demand at
        // 48 kHz with their fundamental at MIDI note 60.
        SourceData::Ptr generate (int sourceChoice);

        juce::String hashOf (const juce::AudioBuffer<float>&);

        // Extensions the platform can read, e.g. "wav;aif;aiff;flac;mp3;m4a;caf" on macOS.
        juce::String audioExtensions();

        // Fundamental of a pitched sample (YIN over several loud windows) between start and end
        // (0..1 of the sample). note = -1 for unpitched material.
        struct PitchEstimate { float note = -1.0f, confidence = 0.0f; };
        PitchEstimate estimatePitch (const SourceData&, float start = 0.0f, float end = 1.0f);

        // Fills detectedNote / pitchConfidence from the whole sample (unless the file's own
        // metadata already did).
        void detectPitch (SourceData&);

        // A root note written in a file name: "Vox F#3.wav" (note and octave, C3 = 60),
        // "Pad_Fmin.wav" or "Bb.wav" (pitch class only). Bare letters are not notes.
        struct NameRoot { int pitchClass = -1; int midiNote = -1; };   // -1 = none
        NameRoot rootFromName (const juce::String& fileName);
    }
}
