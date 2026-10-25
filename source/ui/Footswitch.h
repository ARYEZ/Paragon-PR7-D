#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// One of the seven footswitches on the pedal: a status LED,
// a big rubber pad, and the block's name printed on the metal. Pressing the
// pad turns the block on or off; clicking the name selects the block so its
// knobs show in the edit panel.
//
// A footswitch can control several on/off parameters at once. If any of
// them is on, the LED is lit and pressing turns them all off; otherwise
// pressing turns them all on.
//
// With no parameters it's a stomp still to be built: it shows its name, its
// LED stays dark and pressing it does nothing.
class Footswitch final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    Footswitch (int number, const juce::String& name, juce::AudioProcessorValueTreeState& parameters,
                juce::StringArray onParameterIds);

    // Returns the text under the name, e.g. "Drive 5.0  Tone 5.0".
    std::function<juce::String()> describe;
    std::function<void()> onSelect;

    void setSelected (bool shouldBeSelected);

    // Re-reads the parameters now (also happens on a timer).
    void refresh();
    bool isAnyOn() const;
    bool isPlaceholder() const { return onParameterIds.isEmpty(); }
    void toggle();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }

    int number;
    juce::String name;
    juce::AudioProcessorValueTreeState& parameters;
    juce::StringArray onParameterIds;
    bool selected = false, lastOn = false;
    juce::String summary;
    juce::Rectangle<int> ledArea, switchArea, nameArea;
};
