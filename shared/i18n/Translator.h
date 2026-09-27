#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <unordered_map>

namespace thf
{
    // UI language switch for the series. Source strings are English; each plug-in ships a
    // table of lines like   "English" = "Русский"   and wraps UI text in tr("English").
    // Parameter names are never translated: hosts show them as they are.
    //
    // The language is a per-user preference shared by all thf plug-ins (not saved in
    // sessions). Message thread only. Deleted with the JUCE GUI shutdown, never later.
    class Translator : public juce::ChangeBroadcaster,
                       private juce::DeletedAtShutdown
    {
    public:
        enum class Language { english, russian };

        static Translator& get() { return *getInstance(); }
        ~Translator() override { clearSingletonInstance(); }
        JUCE_DECLARE_SINGLETON_INLINE (Translator, false)

        void addTable (Language, const char* utf8Data, size_t size);

        Language getLanguage() const noexcept { return language; }
        void setLanguage (Language);

        juce::String translate (const juce::String& english) const;

        // Parses a table; exposed for tests.
        static std::unordered_map<juce::String, juce::String> parse (const juce::String& text);

    private:
        Translator();

        Language language = Language::russian;
        std::unordered_map<juce::String, juce::String> russian;
    };

    inline juce::String tr (const juce::String& english) { return Translator::get().translate (english); }

    // "  ·  " separator for status texts (UTF-8 kept out of C++ literals on purpose).
    inline juce::String sep() { return juce::String::fromUTF8 ("  \xc2\xb7  "); }
}
