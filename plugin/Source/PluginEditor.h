#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// The panel. Drawn rather than assembled from widgets: every edge in this design
// is a 1 px hairline and every readout is a specific typeface pairing, which is
// faster to draw directly than to coax out of a LookAndFeel.
class TapeTopEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit TapeTopEditor (TapeTopProcessor&);
    ~TapeTopEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void buildBackground();
    void layoutPanel();
    void drawLanes (juce::Graphics&);
    void drawReels (juce::Graphics&, juce::Rectangle<int>);
    void drawKey (juce::Graphics&, juce::Rectangle<int>, int index, bool active);
    void drawScope (juce::Graphics&, juce::Rectangle<int>);
    void handleTrick (int index);

    juce::String timecode (double seconds) const;

    TapeTopProcessor& proc;
    TapeTopProcessor::Snapshot snap;
    std::vector<tt::Take> takes;

    juce::Image background;
    float scale = 1.0f;
    double reelPhase = 0.0;
    double lastFrame = 0.0;
    float levelPeak[8] { };

    // hit areas, computed in layoutPanel()
    std::array<juce::Rectangle<int>, 4> styleRects;
    std::array<juce::Rectangle<int>, 5> keyRects;
    std::array<juce::Rectangle<int>, 8> trickRects;
    std::array<juce::Rectangle<int>, 8> laneRects;
    std::array<juce::Rectangle<int>, 5> knobRects;    // low, mid, high, output, spare
    std::array<juce::Rectangle<int>, 8> stripRects;
    juce::Rectangle<int> loopToggleRect, setInRect, setOutRect;
    juce::Rectangle<int> tapeHeadRect, scopeRect, specRect, glueRect;

    int selectedTrack = 0;
    int dragIndex = -1;
    float dragStartValue = 0.0f;
    int dragStartY = 0;
    float eqValues[3] { 0.5f, 0.5f, 0.5f };
    float trackGain[8];
    int hoverIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeTopEditor)
};
