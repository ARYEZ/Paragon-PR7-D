#pragma once

#include "PluginProcessor.h"

// TRAVEL's pattern (SEQ) and strength (MOD) editors for one oscillator (stage 4e):
//   - a row of steps, each a SPEED period wide (or two), marked with its mode
//     and iterations: drag a step up or down for more or fewer iterations,
//     right-click it for its mode and length, double-click to make it one or
//     two periods long;
//   - under each step, its strength curve: drag its left, middle or right
//     for the start, middle and end (shift-drag bends it), double-click to
//     add or remove the middle point.
// Dimmed when EDITORS leaves them out.
class TravelPatternEditor final : public juce::Component,
                                  public juce::SettableTooltipClient,
                                  private juce::Timer
{
public:
    explicit TravelPatternEditor (ParagonX1AudioProcessor&);
    void setOscillator (int index);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Where things are (for the tests), the steps shown, and changes as the menus make them.
    int getNumSteps() const;
    juce::Rectangle<float> stepArea (int step) const;
    juce::Rectangle<float> strengthArea (int step) const;
    synth::travel::Pattern getPattern() const { return processor.getModMatrix().travel[(size_t) oscillator].pattern; }
    void setPattern (const synth::travel::Pattern&, bool finished);
    void setStepMode (int step, synth::travel::StepMode);
    void setStepLength (int step, int length);

private:
    void timerCallback() override;
    int stepAt (juce::Point<float>) const;
    float parameter (const char* what) const;
    bool sequenceOn() const;
    bool strengthOn() const;
    juce::Rectangle<float> stepsRow() const;
    juce::Rectangle<float> strengthRow() const;

    ParagonX1AudioProcessor& processor;
    int oscillator = 0;
    synth::travel::Pattern shown;
    std::vector<float> lastSettings;
    // A drag: on which step, what it moves (0 iterations; 1 start, 2 middle, 3 end, 4 curve), from where.
    int dragStep = -1, dragWhat = 0;
    float dragStartY = 0.0f, dragStartValue = 0.0f;
};
