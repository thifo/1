#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace thf::grain
{
    // An immutable, ready-to-play grain source: the audio plus band-limited octave copies
    // ("mip levels") used to pitch grains up without aliasing, and a peak overview for the
    // waveform display. Built off the audio thread, then handed over by reference count.
    class SourceData : public juce::ReferenceCountedObject
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<SourceData>;

        static constexpr int maxLevels = 6;       // 1, 1/2, ..., 1/32 of the original rate
        static constexpr int padding = 48;        // zeros around each level for interpolation
        static constexpr int overviewSize = 2048; // peak buckets for the display

        // Copies and sanitises the audio (non-finite samples become 0), builds levels and peaks.
        static Ptr fromBuffer (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name);

        int getNumChannels() const noexcept     { return numChannels; }
        int getLength() const noexcept          { return length; }
        double getSampleRate() const noexcept   { return sampleRate; }
        double getDurationSeconds() const noexcept { return (double) length / sampleRate; }
        int getNumLevels() const noexcept       { return (int) levels.size(); }
        int getLevelLength (int level) const noexcept { return levelLengths[(size_t) level]; }
        const juce::String& getName() const noexcept  { return name; }

        // Pointer to sample 0 of a level; indices -padding .. length+padding-1 are readable.
        const float* channel (int level, int ch) const noexcept
        {
            return levels[(size_t) level].getReadPointer (juce::jmin (ch, numChannels - 1)) + padding;
        }

        // The original audio, unpadded (for saving into a session).
        juce::AudioBuffer<float> copyOriginal() const;

        const std::vector<float>& getPeakMin() const noexcept { return peakMin; }
        const std::vector<float>& getPeakMax() const noexcept { return peakMax; }

        // Metadata carried alongside, not used by the audio thread.
        juce::File file;
        juce::String contentHash;
        juce::MemoryBlock embeddedFlac;   // for sessions; empty if the sample is too long

    private:
        SourceData() = default;

        int numChannels = 1, length = 0;
        double sampleRate = 48000.0;
        juce::String name;
        std::vector<juce::AudioBuffer<float>> levels;
        std::vector<int> levelLengths;
        std::vector<float> peakMin, peakMax;
    };

    // Loading and generation helpers (message or background thread only).
    namespace sources
    {
        // Maximum length accepted from a file, in seconds.
        inline constexpr double maxFileSeconds = 600.0;

        // Reads wav / aiff / flac (and whatever else the format manager knows).
        // Returns nullptr and fills `error` on failure.
        SourceData::Ptr loadFile (const juce::File&, juce::String& error);

        // Decodes a file image kept in memory (sessions embed small samples as FLAC).
        SourceData::Ptr loadFromMemory (const void* data, size_t size, const juce::String& name, juce::String& error);

        // Encodes audio as 24-bit FLAC for embedding into the session.
        juce::MemoryBlock encodeFlac (const juce::AudioBuffer<float>& audio, double sampleRate);

        // Built-in sources (choice index 1..5 of the Source parameter), rendered on demand at
        // 48 kHz with their fundamental at MIDI note 60.
        SourceData::Ptr generate (int sourceChoice);

        juce::String hashOf (const juce::AudioBuffer<float>&);
    }
}
