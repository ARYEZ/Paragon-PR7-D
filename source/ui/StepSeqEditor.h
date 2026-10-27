#pragma once

#include "PluginProcessor.h"

// The STEP SEQUENCER's steps (stage 4d), edited as Avenger's are:
//   - click a step to set its height (drag across to draw several)
//   - right-click a step to delete it (a rest)
//   - click the arrow under a step to link it to the next (the gate stays open)
// LENGTH steps are shown; in STEREO the top lane is the left channel and the
// bottom the right. The line over the steps is the gate as it will play, with
// GATE, CONTOUR, DECAY and SHUFFLE.
class StepSeqEditor final : public juce::Component,
                            public juce::SettableTooltipClient,
                            private juce::Timer
{
public:
    explicit StepSeqEditor (ParagonX1AudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Where things are (for the tests): a step's bar area, and its link arrow.
    int getNumLanes() const;
    int getLength() const;
    juce::Rectangle<float> barArea (int lane, int step) const;
    juce::Rectangle<float> linkArea (int lane, int step) const;

    // The steps now, and a change to them (an undo step when `finished`).
    synth::seq::Pattern getPattern() const { return processor.getModMatrix().stepPattern; }
    void setPattern (const synth::seq::Pattern&, bool finished);

private:
    void timerCallback() override;
    juce::Rectangle<float> laneArea (int lane) const;
    void drawAt (juce::Point<float>);   // a click or drag on the bars
    float parameter (const char* id) const;

    ParagonX1AudioProcessor& processor;
    synth::seq::Pattern shown;
    std::vector<float> lastSettings;
    int dragLane = -1;
};
