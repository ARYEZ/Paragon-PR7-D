#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/PitchDetector.h"
#include "ui/Icons.h"

// Header icon buttons, and the tuner's pitch reader for the unit's screen.
// (The expression pedal and its assign screens went in 0.12.0.)

//==============================================================================
// A button with a line icon and optional text beside it, as on the mockup's
// header (PROCESSOR, PRESETS, undo, redo).
// When it's a toggle and on, it's drawn in the accent colour.
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, Icons::Type icon, const juce::String& text = {},
                juce::Justification justification = juce::Justification::centred);

    // Draws a box round the button (on the header the tabs have none).
    void setOutlined (bool shouldBeOutlined) { outlined = shouldBeOutlined; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icons::Type icon;
    juce::String text;
    juce::Justification justification;
    bool outlined = false;
};

//==============================================================================
// Passes the processor's tuner reading (see dsp/Tuner.h) to the screen on
// the unit, about 30 times a second, while the tuner is on.
class TunerReader final : private juce::Timer
{
public:
    explicit TunerReader (ParagonX1AudioProcessor&);

    struct Reading
    {
        bool hasNote = false;
        PitchDetector::Note note;
        double frequency = 0.0;
    };

    std::function<void (const Reading&)> onReading;

    void setActive (bool shouldBeActive);
    bool isActive() const noexcept { return isTimerRunning(); }

private:
    void timerCallback() override;

    ParagonX1AudioProcessor& processor;
};
