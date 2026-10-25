#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/PitchDetector.h"
#include "ui/Icons.h"

// Phase 6 controls: header icon buttons, TAP's hold-for-tuner, the
// expression pedal and its assign buttons, and the tuner's pitch reader.

//==============================================================================
// A button with a line icon and optional text beside it, as on the mockup's
// header (PROCESSOR, PRESETS, undo, redo, TAP) and EXPRESSION ASSIGN list.
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
// Notices a button being held down (for TAP: hold it to open the tuner, as
// "HOLD TO TUNER" on the mockup). Add it to the button as a mouse listener,
// and in the button's onClick ask consumeHold() whether this "click" was
// really the end of a hold, which shouldn't also count as a tap.
class HoldDetector final : public juce::MouseListener,
                           private juce::Timer
{
public:
    explicit HoldDetector (int holdMs = 700) : holdTime (holdMs) {}

    std::function<void()> onHold;

    bool consumeHold() { return std::exchange (held, false); }

    void mouseDown (const juce::MouseEvent&) override { held = false; startTimer (holdTime); }
    void mouseUp (const juce::MouseEvent&) override { stopTimer(); }

private:
    void timerCallback() override
    {
        stopTimer();
        held = true;
        if (onHold)
            onHold();
    }

    int holdTime;
    bool held = false;
};

//==============================================================================
// VOLUME / WAH-FILTER / MORPH-FX / PITCH-WHAMMY / LO-FI / WHAMMY-WAH / GLITCH / TALK BOX: what the expression
// pedal controls. Click one to assign the pedal to it, or click the lit one
// again to switch the pedal off. An LCD screen (a menu with the choice
// highlighted, the pedal's position underneath) in two sizes: small on the
// unit's face, and bigger in the dock, filling its EXPRESSION ASSIGN panel
// with buttons beside it to scroll the list.
class ExpressionAssign final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::Timer
{
public:
    enum class Style { pedal, list };
    static constexpr int numTargets = 1;    // what the pedal can control: everything but "Off" (VOLUME, for now)
    static constexpr int visibleRows = 6;   // the screen's lines; the list scrolls

    ExpressionAssign (juce::AudioProcessorValueTreeState&, Style);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr float referenceHeight = 126.0f;    // the unit's screen: its lettering's size is scale 1
    static constexpr float scrollButtonWidth = 30.0f;   // the dock's scroll buttons, on the screen's right

    void select (int target);
    void scroll (int lines);
    void updateScrollButtons();
    void timerCallback() override;
    juce::Rectangle<float> screenArea() const;          // the screen, frame and all (the dock's leaves room for its buttons)
    juce::Rectangle<float> screenGlass() const;
    std::array<juce::Rectangle<float>, visibleRows> screenRows() const;   // the menu's lines showing
    juce::Rectangle<float> scrollArrows() const;                           // the up and down arrows in the heading

    Style style;
    juce::AudioProcessorValueTreeState& state;
    std::unique_ptr<juce::Button> scrollUp, scrollDown;   // the dock's scroll buttons (none on the unit)
    float scale = 1.0f;      // the lettering's size relative to the unit's screen
    juce::ParameterAttachment attachment;
    int currentTarget = 0;
    int firstRow = 0;        // the first target the screen shows (it scrolls)
    int shownPercent = -1;   // the pedal position last drawn on the screen, so it repaints only when that changes
    float wheelTravel = 0.0f;
};

//==============================================================================
// The expression pedal beside the unit: a base, and the treadle covering it,
// rocking in 3D between heel (0) and toe (1). Drag it up and down, or
// use the mouse wheel; a MIDI pedal on CC 11 moves it too. The LED strip
// shows the position, and the label what it's assigned to.
class ExpressionPedal final : public juce::Component,
                              public juce::SettableTooltipClient,
                              private juce::Timer
{
public:
    explicit ExpressionPedal (ParagonX1AudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Redraws the treadle in the current finish (see Finish.h).
    void refinish();

private:
    void timerCallback() override;
    juce::Rectangle<float> trackArea() const;
    juce::Rectangle<float> housingArea() const;

    ParagonX1AudioProcessor& processor;
    juce::ParameterAttachment attachment;
    float position = 1.0f, dragStartPosition = 1.0f;
    float shownPosition = 1.0f;   // where the drawn treadle is; eases towards `position` so it glides
    juce::Image treadle;   // the treadle's face, drawn once and rocked in 3D
    int target = 0;
};

//==============================================================================
// The dock's EXPRESSION ASSIGN panel: the target list, and the MIN / MAX
// range the pedal sweeps between (heel and toe).
class ExpressionPanel final : public juce::Component
{
public:
    explicit ExpressionPanel (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateRangeText();

    ExpressionAssign assign;
    juce::Slider range { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    juce::ParameterAttachment minAttachment, maxAttachment;
    juce::Label minLabel, maxLabel;
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
