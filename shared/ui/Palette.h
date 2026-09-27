#pragma once

#include <juce_graphics/juce_graphics.h>

// Colour tokens of the thf series. The look is a white controller "plate" with dark glass
// screens, faceted white knobs and pastel pads. Plug-ins use these tokens and never
// hard-code colours of their own.
namespace thf::palette
{
    // Plate: the white body and its panels.
    inline const juce::Colour plate        { 0xfff3f4f6 };
    inline const juce::Colour plateRaised  { 0xfffafbfc };
    inline const juce::Colour plateInset   { 0xffe9ebee };
    inline const juce::Colour plateEdge    { 0xffd9dce0 };
    inline const juce::Colour ink          { 0xff1d2025 };
    inline const juce::Colour inkDim       { 0xff6c727a };
    inline const juce::Colour inkFaint     { 0xffa3a8ae };

    // Glass: dark screens.
    inline const juce::Colour glass        { 0xff111316 };
    inline const juce::Colour glassRaised  { 0xff1b1e22 };
    inline const juce::Colour glassEdge    { 0xff2b2f34 };
    inline const juce::Colour glassGrid    { 0xff23272c };
    inline const juce::Colour glassText    { 0xffdde1e6 };
    inline const juce::Colour glassDim     { 0xff7d848c };
    inline const juce::Colour glassTrace   { 0xfff2f4f6 };

    // Controls.
    inline const juce::Colour knobFace     { 0xfffcfcfd };
    inline const juce::Colour knobFacet    { 0xffe4e6e9 };
    inline const juce::Colour knobTrack    { 0xffd6d9dd };
    inline const juce::Colour knobArc      { 0xff5a8fc0 };
    inline const juce::Colour knobPointer  { 0xff1d2025 };
    inline const juce::Colour faderSlot    { 0xff25282d };
    inline const juce::Colour buttonOn     { 0xff2a2d32 };
    inline const juce::Colour buttonOnText { 0xfff5f6f7 };
    inline const juce::Colour warning      { 0xffd9534f };

    // Accents, one per "channel" of a plug-in (bands, voices, pages...).
    inline const juce::Colour accentBlue   { 0xff6fb6e8 };
    inline const juce::Colour accentTeal   { 0xff5cc3b1 };
    inline const juce::Colour accentGreen  { 0xff7ccb7f };
    inline const juce::Colour accentYellow { 0xfff0c64c };
    inline const juce::Colour accentOrange { 0xfff0925a };
    inline const juce::Colour accentRose   { 0xffc9706f };

    // Pastel pad faces (off state); the lit state is derived with padLit().
    inline const juce::Colour padBlue      { 0xffcfe6f7 };
    inline const juce::Colour padTeal      { 0xffd3ede9 };
    inline const juce::Colour padGreen     { 0xffdcefd9 };
    inline const juce::Colour padYellow    { 0xfff7eed3 };
    inline const juce::Colour padSalmon    { 0xfff8dfd4 };
    inline const juce::Colour padGrey      { 0xffeaebed };
    inline const juce::Colour padPink      { 0xfff6d9dd };
    inline const juce::Colour padCream     { 0xfff6efdb };

    inline juce::Colour padLit (juce::Colour pastel)
    {
        return pastel.withMultipliedSaturation (2.4f).withMultipliedBrightness (0.97f);
    }
}
