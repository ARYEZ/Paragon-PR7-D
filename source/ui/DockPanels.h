#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"

// The panels' shared style, and the SETTINGS page.

// Dark rounded panel with a small grey title, shared by the panels.
namespace DockStyle
{
    void drawPanel (juce::Graphics&, juce::Rectangle<float> area);
    void drawTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
}

//==============================================================================
// SETTINGS: the synth's tuning, where presets are kept, and what the plugin is built from.
class SettingsPage final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit SettingsPage (ParagonX1AudioProcessor&);
    ~SettingsPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ParagonX1AudioProcessor& processor;
    juce::Slider tune { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::AudioProcessorValueTreeState::SliderAttachment tuneAttachment;
    juce::TextButton openFolder { "OPEN PRESETS FOLDER" };
    juce::Rectangle<int> tuneArea, rigsArea, aboutArea;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};
