// Shared entry point: runs every juce::UnitTest linked into the executable and returns
// non-zero on any failure.

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cstdlib>
#include <new>

// Allocation counter for "no allocations on the audio thread" checks.
namespace thf::test
{
    std::atomic<bool> countAllocations { false };
    std::atomic<long> allocations { 0 };
}

void* operator new (std::size_t size)
{
    if (thf::test::countAllocations.load (std::memory_order_relaxed))
        thf::test::allocations.fetch_add (1, std::memory_order_relaxed);
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    if (thf::test::countAllocations.load (std::memory_order_relaxed))
        thf::test::allocations.fetch_add (1, std::memory_order_relaxed);
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

// The nothrow forms too (std::stable_sort's buffer), so every new pairs with our delete.
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (thf::test::countAllocations.load (std::memory_order_relaxed))
        thf::test::allocations.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size == 0 ? 1 : size);
}

void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    if (thf::test::countAllocations.load (std::memory_order_relaxed))
        thf::test::allocations.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (size == 0 ? 1 : size);
}

void operator delete (void* p, const std::nothrow_t&) noexcept   { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete (void* p) noexcept              { std::free (p); }
void operator delete[] (void* p) noexcept            { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

// Every JUCE assertion fails the run, except the Linux-only "message queue is full" warning:
// tests build editors without a running event loop, so async updates queue up by design.
class AssertionCounter : public juce::Logger
{
public:
    int failures = 0;

    void logMessage (const juce::String& message) override
    {
        if (message.startsWith ("JUCE Assertion failure"))
        {
            if (message.contains ("juce_Messaging_linux.cpp"))
                return;
            ++failures;
        }
        std::fprintf (stderr, "%s\n", message.toRawUTF8());
    }
};

int main()
{
    // Tests use their own settings file, never the user's (recent samples, preferences).
    {
        const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("thf-tests.settings");
        settings.deleteFile();
       #if JUCE_WINDOWS
        _putenv_s ("THF_SETTINGS_FILE", settings.getFullPathName().toRawUTF8());
       #else
        setenv ("THF_SETTINGS_FILE", settings.getFullPathName().toRawUTF8(), 1);
       #endif
    }
    juce::ScopedJuceInitialiser_GUI gui;
    AssertionCounter assertions;
    juce::Logger::setCurrentLogger (&assertions);
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;
    std::printf ("\n%s: %d failure(s), %d assertion(s)\n", failures + assertions.failures == 0 ? "PASS" : "FAIL",
                 failures, assertions.failures);
    juce::Logger::setCurrentLogger (nullptr);
    return failures + assertions.failures == 0 ? 0 : 1;
}
