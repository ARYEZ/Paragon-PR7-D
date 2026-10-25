#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// The TUNER stomp's panel in the dock, for checking the pitch of a recorded
// part as it plays: the current note, a trace of the last 30 seconds against
// the notes, and how sharp or flat the part sits on average. RESET empties
// the trace. Reads the processor's tuner (dsp/Tuner.h); changes nothing.
class TunerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerPanel (ParagonX1AudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // The trace's colours: in tune (within 5 cents), close (15) and off.
    static juce::Colour colourForCents (double cents);

private:
    void timerCallback() override { repaint(); }
    void paintTrace (juce::Graphics&, juce::Rectangle<float> area, double referenceHz) const;

    ParagonX1AudioProcessor& processor;
    juce::TextButton resetButton { "RESET" };
    juce::Rectangle<float> readingArea, traceArea, averageArea;
};
