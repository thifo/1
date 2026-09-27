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
            for (const auto& pad : layout::padsBankA)
                if (! pad.param.empty())
                    expect (processor.param (str (pad.param)) != nullptr, str (pad.param));
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
                    // Choices start at their first entry (the rest of the list is reserved).
                    p->setValueNotifyingHost (dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr ? 0.0f : 0.5f);
                    const auto start = p->getValue();
                    juce::MidiBuffer midi;
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, layout::minilab3::encoderCC[(size_t) slot], 66), 0);
                    juce::AudioBuffer<float> buffer (2, 64);
                    processor.processBlock (buffer, midi);
                    processor.flushHardwareChanges();
                    expect (p->getValue() > start, "slot " + juce::String (slot + 1) + " on page " + juce::String (page)
                                                  + ": " + juce::String (p->getValue()) + " steps " + juce::String (p->getNumSteps()));
                    expectEquals (processor.getCcFor (shown), layout::minilab3::encoderCC[(size_t) slot]);
                    pump();
                }
            }
            processor.setPage (0);
        }

        beginTest ("Pads: note -> action -> label, strips at the far left");
        {
            GrainEditor editor (processor);
            for (int pad = 0; pad < layout::numPads; ++pad)
            {
                const auto& slot = layout::padsBankA[(size_t) pad];
                expect (! slot.label.empty());
                // The note of pad N (bank A) triggers slot N's action: its parameter moves.
                if (slot.param.empty()) continue;
                auto* p = processor.param (str (slot.param));
                p->setValueNotifyingHost (p->getDefaultValue());
                const auto before = p->getValue();
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (layout::minilab3::padChannel, layout::minilab3::padBankANote + pad, 1.0f), 0);
                midi.addEvent (juce::MidiMessage::noteOff (layout::minilab3::padChannel, layout::minilab3::padBankANote + pad), 1);
                juce::AudioBuffer<float> buffer (2, 64);
                processor.processBlock (buffer, midi);
                processor.flushHardwareChanges();
                expect (std::abs (p->getValue() - before) > 1.0e-4f, "pad " + juce::String (pad + 1) + " " + str (slot.label));
                pump();
            }
            expect (editor.getStripsBounds().getRight() <= editor.getMainKnobBounds().getX());
        }

        beginTest ("Slot numbers are readable (contrast >= 4.5:1 on the plate)");
        {
            auto luminance = [] (juce::Colour c)
            {
                auto lin = [] (float v) { return v <= 0.04045f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
                return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
            };
            const auto a = luminance (thf::palette::plate), b = luminance (thf::palette::inkLabel);
            const auto contrast = (std::max (a, b) + 0.05f) / (std::min (a, b) + 0.05f);
            logMessage ("  inkLabel on plate: " + juce::String (contrast, 2) + ":1");
            expect (contrast >= 4.5f);
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

            // Keys: below the pads, across the width, like on the controller.
            const auto keys = editor.getKeyboardBounds();
            expect (keys.getY() >= editor.getPadBounds (0).getBottom());
            expect (keys.getX() <= editor.getPadBounds (0).getX());
            expect (keys.getRight() >= editor.getPadBounds (layout::numPads - 1).getRight());
            expect (editor.getPadBounds (layout::numPads - 1).getRight() >= editor.getFaderBounds (layout::numFaders - 1).getX());
            pump();
        }
    }
};

static GrainLayoutTest grainLayoutTest;
