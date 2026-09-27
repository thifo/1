// The on-screen layout must mirror the controller: the same table drives the MIDI template
// and the editor, and the geometry keeps the hardware order.

#include <PluginEditor.h>
#include <set>

using namespace thf::grain;

namespace
{
    juce::String str (std::string_view s) { return juce::String (std::string (s)); }

    // Delivers pending async messages (slider notifications, timers) as a host would.
    void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (30); }
}

class GrainLayoutTest : public juce::UnitTest
{
public:
    GrainLayoutTest() : juce::UnitTest ("Grain layout", "thf") {}

    void runTest() override
    {
        GrainProcessor processor;

        beginTest ("Every slot names an existing parameter, nothing is mapped twice");
        {
            std::set<std::string_view> faders (layout::faderParams.begin(), layout::faderParams.end());
            expectEquals ((int) faders.size(), layout::numFaders);
            for (auto id : layout::faderParams)
                expect (processor.param (str (id)) != nullptr, str (id));

            for (int page = 0; page < layout::numPages; ++page)
            {
                std::set<std::string_view> seen;
                for (auto id : layout::encoderParams[(size_t) page])
                {
                    expect (processor.param (str (id)) != nullptr, str (id));
                    expect (seen.insert (id).second, "duplicate on page: " + str (id));
                    expect (faders.count (id) == 0, "also on a fader: " + str (id));
                    expect (id != layout::mainEncoderParam, "also on the main encoder: " + str (id));
                }
            }
            expect (processor.param (str (layout::mainEncoderParam)) != nullptr);
            for (auto id : layout::padParams)
                if (! id.empty())
                    expect (processor.param (str (id)) != nullptr, str (id));
        }

        beginTest ("Controller template: CCs are unique");
        {
            using namespace layout::minilab3;
            std::set<int> ccs;
            for (auto cc : encoderCC) expect (ccs.insert (cc).second, "CC " + juce::String (cc));
            for (auto cc : faderCC)   expect (ccs.insert (cc).second, "CC " + juce::String (cc));
            for (auto cc : { mainEncoderCC, mainClickCC, modStripCC, sustainCC })
                expect (ccs.insert (cc).second, "CC " + juce::String (cc));
            expect (padBankBNote >= padBankANote + layout::numPads);
        }

        beginTest ("Hardware CC N moves the parameter shown in on-screen slot N, on every page");
        {
            processor.prepareToPlay (48000.0, 512);
            for (int page = 0; page < layout::numPages; ++page)
            {
                processor.setPage (page);
                GrainEditor editor (processor);
                for (int slot = 0; slot < layout::numEncoders; ++slot)
                {
                    const auto shown = editor.getEncoderParam (slot);
                    expectEquals (shown, str (layout::encoderParams[(size_t) page][(size_t) slot]));

                    auto* p = processor.param (shown);
                    p->setValueNotifyingHost (0.5f);
                    juce::MidiBuffer midi;
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, layout::minilab3::encoderCC[(size_t) slot], 66), 0);
                    juce::AudioBuffer<float> buffer (2, 64);
                    processor.processBlock (buffer, midi);
                    expect (p->getValue() > 0.5f, "slot " + juce::String (slot + 1) + " on page " + juce::String (page)
                                                  + ": " + juce::String (p->getValue()) + " steps " + juce::String (p->getNumSteps()));
                    expectEquals (processor.getCcFor (shown), layout::minilab3::encoderCC[(size_t) slot]);
                    pump();
                }
            }
            processor.setPage (0);
        }

        beginTest ("Geometry mirrors the controller");
        {
            GrainEditor editor (processor);

            // Encoders: 1-4 on the top row left to right, 5-8 below in the same columns.
            for (int slot = 0; slot < layout::numEncoders; ++slot)
            {
                const auto b = editor.getEncoderBounds (slot);
                if (layout::encoderColumn (slot) > 0)
                    expect (b.getX() > editor.getEncoderBounds (slot - 1).getX());
                if (slot >= 4)
                {
                    const auto above = editor.getEncoderBounds (slot - 4);
                    expectEquals (b.getX(), above.getX());
                    expect (b.getY() > above.getY());
                }
            }

            // Main encoder left of the encoders; faders right of them, left to right.
            const auto main = editor.getMainKnobBounds();
            expect (main.getRight() <= editor.getEncoderBounds (0).getX());
            for (int f = 0; f < layout::numFaders; ++f)
            {
                expect (editor.getFaderBounds (f).getX() >= editor.getEncoderBounds (3).getRight());
                if (f > 0) expect (editor.getFaderBounds (f).getX() > editor.getFaderBounds (f - 1).getX());
            }

            // Pads: one row below everything, starting under the display / main encoder block.
            for (int pad = 0; pad < layout::numPads; ++pad)
            {
                const auto b = editor.getPadBounds (pad);
                expect (b.getY() >= editor.getEncoderBounds (7).getBottom());
                expect (b.getY() >= editor.getFaderBounds (0).getBottom());
                if (pad > 0) expect (b.getX() > editor.getPadBounds (pad - 1).getX());
            }
            expect (editor.getPadBounds (0).getX() <= main.getX());
            expect (editor.getPadBounds (layout::numPads - 1).getRight() >= editor.getFaderBounds (layout::numFaders - 1).getX());
            pump();
        }
    }
};

static GrainLayoutTest grainLayoutTest;
