#pragma once

#include "PluginProcessor.h"

// The strip above the keyboard (stage 4f), as Avenger has one: every
// modulation source as a chip to drag onto a knob (which routes it there),
// "MW" switches on the controllers that can drive the mod wheel too, and the
// last MIDI controller moved, to drag onto a knob to link it (MIDI learn).
class ModSourceStrip final : public juce::Component,
                             private juce::Timer
{
public:
    explicit ModSourceStrip (ParagonX1AudioProcessor&);
    ~ModSourceStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    struct Chip final : public juce::Component,
                        public juce::SettableTooltipClient
    {
        Chip (juce::String dragDescription, juce::String text, juce::String tooltip);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        juce::String description, text;
        float level = -1.0f;   // the controller chip's value bar (-1: none)
        std::unique_ptr<juce::TextButton> toWheel;   // "MW", on the controllers that can drive the mod wheel
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toWheelAttachment;
    };
    // For tests: the chips, by what they carry when dragged.
    Chip* chipFor (const juce::String& description);
    void refresh() { timerCallback(); }   // the controller chip, now (it follows on a timer)

private:
    void timerCallback() override;

    ParagonX1AudioProcessor& processor;
    std::vector<std::unique_ptr<Chip>> chips;
    std::vector<std::pair<int, juce::String>> groups;   // the first chip of each group, and its name
    Chip controller { "", "CC --", "The last MIDI controller you moved: drag it onto a knob to link them (MIDI learn). "
                                   "SETTINGS lists your links." };
    int shownController = -2;
};
