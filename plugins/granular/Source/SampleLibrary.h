#pragma once

#include <juce_core/juce_core.h>

// Where the user's samples live and how to find them again. Message thread only.
namespace thf::grain::library
{
    // ~/Music/thf Grain Samples (created on demand).
    juce::File userFolder();

    bool isAudioFile (const juce::File&);

    // Audio files directly inside a folder, natural sort order.
    juce::Array<juce::File> audioFilesIn (const juce::File& folder);

    // The file `delta` steps away from `current` in its folder (wraps around).
    juce::File neighbour (const juce::File& current, int delta);

    // Recently loaded samples, newest first (only files that still exist).
    juce::Array<juce::File> recent();
    void addRecent (const juce::File&);

    // A sample saved in a session whose file moved: looks for the same file name in the user
    // folder (and one level below) and in the folders of recent samples.
    juce::File findMissing (const juce::File& original);
}
