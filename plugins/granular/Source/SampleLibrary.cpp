#include "SampleLibrary.h"
#include "dsp/SourceData.h"
#include <juce_data_structures/juce_data_structures.h>

namespace thf::grain::library
{
    namespace
    {
        constexpr const char* recentKey = "grainRecentSamples";
        constexpr int maxRecent = 12;

        // Same per-user settings file as the rest of the series.
        juce::PropertiesFile::Options settingsOptions()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "thf";
            o.filenameSuffix = ".settings";
            o.folderName = "thf";
            o.osxLibrarySubFolder = "Application Support";
            o.storageFormat = juce::PropertiesFile::storeAsXML;
            return o;
        }
    }

    juce::File userFolder()
    {
        auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("thf Grain Samples");
        folder.createDirectory();
        return folder;
    }

    bool isAudioFile (const juce::File& f)
    {
        static const auto extensions = sources::audioExtensions();
        return f.existsAsFile() && f.hasFileExtension (extensions);
    }

    juce::Array<juce::File> audioFilesIn (const juce::File& folder)
    {
        juce::Array<juce::File> files;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
            if (isAudioFile (entry.getFile()))
                files.add (entry.getFile());
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        return files;
    }

    juce::File neighbour (const juce::File& current, int delta)
    {
        const auto files = audioFilesIn (current.getParentDirectory());
        if (files.isEmpty())
            return {};
        auto index = files.indexOf (current);
        if (index < 0)
            index = delta > 0 ? -1 : 0;
        const auto n = files.size();
        return files[((index + delta) % n + n) % n];
    }

    juce::Array<juce::File> recent()
    {
        juce::PropertiesFile settings (settingsOptions());
        juce::Array<juce::File> files;
        for (const auto& path : juce::StringArray::fromLines (settings.getValue (recentKey)))
            if (path.isNotEmpty() && juce::File::isAbsolutePath (path) && isAudioFile (juce::File (path)))
                files.add (juce::File (path));
        return files;
    }

    void addRecent (const juce::File& file)
    {
        if (! file.existsAsFile())
            return;
        juce::StringArray paths;
        paths.add (file.getFullPathName());
        for (const auto& f : recent())
            if (f != file && paths.size() < maxRecent)
                paths.add (f.getFullPathName());
        juce::PropertiesFile settings (settingsOptions());
        settings.setValue (recentKey, paths.joinIntoString ("\n"));
        settings.saveIfNeeded();
    }

    juce::File findMissing (const juce::File& original)
    {
        const auto name = original.getFileName();
        if (name.isEmpty())
            return {};

        juce::Array<juce::File> folders;
        folders.add (userFolder());
        for (const auto& entry : juce::RangedDirectoryIterator (userFolder(), false, "*", juce::File::findDirectories))
            folders.add (entry.getFile());
        for (const auto& f : recent())
            folders.addIfNotAlreadyThere (f.getParentDirectory());

        for (const auto& folder : folders)
            if (const auto candidate = folder.getChildFile (name); isAudioFile (candidate))
                return candidate;
        return {};
    }
}
