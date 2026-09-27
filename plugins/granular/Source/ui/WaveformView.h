#pragma once

#include "GrainViews.h"

namespace thf::grain
{
    // The main glass screen: source waveform, playhead, spray range, live grains and cues.
    // Drag = Position (x) and Spray (y), wheel = Size, shift-wheel = Spray.
    class WaveformView : public juce::Component
    {
    public:
        WaveformView (UiContext&);

        // Called by the editor timer (~30 Hz).
        void tick();
        void setDropHighlight (bool b) { if (b != dropHighlight) { dropHighlight = b; repaint(); } }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        std::function<void()> onLoadRequest;

    private:
        juce::Rectangle<float> waveArea() const;
        float xToPosition (float x) const;
        float positionToX (float p) const;
        void showMenu (const juce::MouseEvent&);

        UiContext& ctx;
        SourceData::Ptr source;
        int sourceSerial = -1, sourceChoice = -1;

        struct LiveGrain
        {
            GrainEvent event;
            juce::uint32 born = 0;
        };
        std::vector<LiveGrain> live;
        std::array<GrainEvent, 256> incoming {};

        float playhead = 0.0f;
        bool dropHighlight = false;
        bool dragging = false;
        float dragStartSpray = 0.0f;
        int dragStartY = 0;
    };
}
