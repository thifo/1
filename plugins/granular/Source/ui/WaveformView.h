#pragma once

#include "GrainViews.h"
#include "WaveformMath.h"

namespace thf::grain
{
    // The main glass screen: source waveform, region, playhead, spray range, live grains,
    // attacks and cues, plus the sample bar (< Sample >). Drag = Position (x) or Spray (y),
    // region handles = Sample Start / End (snapping to attacks and zero crossings, Alt: free),
    // wheel = zoom, sideways scroll = pan, double-click = whole sample, Cmd-wheel = Size,
    // Shift-wheel = Spray. When zoomed, the strip at the bottom shows the whole sample.
    class WaveformView : public juce::Component
    {
    public:
        WaveformView (UiContext&);

        // Called by the editor timer (~30 Hz).
        void tick();
        void setDropHighlight (bool b) { if (b != dropHighlight) { dropHighlight = b; repaint(); } }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        const wave::View& getView() const noexcept { return view; }

        std::function<void()> onLoadRequest;

    private:
        juce::Rectangle<float> waveArea() const;
        juce::Rectangle<float> minimapArea() const;
        float snapHandle (float position, bool free) const;
        float xToPosition (float x) const;
        float positionToX (float p) const;
        void showMenu (const juce::MouseEvent&);
        void showSampleMenu();
        void stepSample (int delta);
        float regionStart() const;
        float regionEnd() const;
        float toAbsolute (float relative) const;   // region-relative 0..1 -> file 0..1
        float toRelative (float absolute) const;

        enum class Drag { none, position, regionStart, regionEnd, minimap };
        wave::View view;
        Drag drag = Drag::none;
        enum class Axis { undecided, position, spray };
        Axis axis = Axis::undecided;
        juce::TextButton prevSample { "<" }, sampleButton, nextSample { ">" };

        UiContext& ctx;
        SourceData::Ptr source;
        int sourceSerial = -1, sourceChoice = -1;

        struct LiveGrain
        {
            GrainEvent event;
            juce::uint32 born = 0;
        };
        std::vector<LiveGrain> live;
        std::vector<float> columnMin, columnMax;
        std::array<GrainEvent, 256> incoming {};

        float playhead = 0.0f;
        bool dropHighlight = false;
        bool dragging = false;
        float dragStartSpray = 0.0f;
        int dragStartY = 0;
    };
}
