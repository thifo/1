#include "Translator.h"

namespace thf
{
    namespace
    {
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

        // Reads a "quoted string" starting at pos; handles \" \\ \n escapes.
        bool readQuoted (juce::String::CharPointerType& p, juce::String& out)
        {
            while (! p.isEmpty() && *p != '"') ++p;
            if (p.isEmpty()) return false;
            ++p;
            juce::String result;
            while (! p.isEmpty() && *p != '"')
            {
                auto c = *p;
                if (c == '\\')
                {
                    ++p;
                    if (p.isEmpty()) return false;
                    c = *p;
                    if (c == 'n') c = '\n';
                }
                result += juce::String::charToString (c);
                ++p;
            }
            if (p.isEmpty()) return false;
            ++p;
            out = result;
            return true;
        }
    }

    Translator::Translator()
    {
        juce::PropertiesFile settings (settingsOptions());
        language = settings.getValue ("language", "ru") == "en" ? Language::english : Language::russian;
    }

    std::unordered_map<juce::String, juce::String> Translator::parse (const juce::String& text)
    {
        std::unordered_map<juce::String, juce::String> table;
        for (auto line : juce::StringArray::fromLines (text))
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWithChar ('#'))
                continue;
            auto p = line.getCharPointer();
            juce::String from, to;
            if (readQuoted (p, from) && readQuoted (p, to) && from.isNotEmpty())
                table[from] = to;
        }
        return table;
    }

    void Translator::addTable (Language lang, const char* utf8Data, size_t size)
    {
        if (lang != Language::russian)
            return;
        for (auto& [k, v] : parse (juce::String::fromUTF8 (utf8Data, (int) size)))
            russian[k] = v;
    }

    void Translator::setLanguage (Language l)
    {
        if (l == language)
            return;
        language = l;
        juce::PropertiesFile settings (settingsOptions());
        settings.setValue ("language", l == Language::english ? "en" : "ru");
        settings.saveIfNeeded();
        sendChangeMessage();
    }

    juce::String Translator::translate (const juce::String& english) const
    {
        if (language == Language::english)
            return english;
        if (auto it = russian.find (english); it != russian.end())
            return it->second;
        return english;
    }
}
