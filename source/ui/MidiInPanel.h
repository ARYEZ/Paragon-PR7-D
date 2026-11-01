#pragma once

#include "PluginProcessor.h"
#include "ui/VelocityCurveEditor.h"

#include <deque>

// MIDI IN / E-DRUMS (PR7-D 0.18.0), beside SETTINGS' note list: what's done to MIDI from the DAW or a controller.
// Inspired by drum software's MIDI In / E-Drums settings (chapter 9 of the manual the owner sent); our own design and
// words. Three tabs for the note chosen in the list, and the MIDI MONITOR under them:
//   - MAPPING: the note's drum; LEARN (hit a pad: that pad's note plays this drum); CC ZONES for a positional pad (how
//     many, which controller, with its LEARN, each zone's drum, and where each begins, over a live meter);
//   - VELOCITY: the curve every incoming note goes through (with SOFT, GATE, STRAIGHT), the last hit shown on it;
//   - HI-HAT: the pedal's steps (live meter, drag the marks), three settings to start from, and CALIBRATE (press the
//     pedal fully down and up a few times, then DONE: the steps spread over the range the pedal really sends);
//   - THE MIDI MONITOR: what came in (note, controller, aftertouch; channel, note, velocity) and what it played (the
//     drum, the velocity after the curve). Click a note to choose it.
class MidiInPanel final : public juce::Component,
                          private juce::Timer
{
public:
    explicit MidiInPanel (ParagonX1AudioProcessor&);
    ~MidiInPanel() override;

    void setNote (int note);
    int getNote() const noexcept { return note; }
    std::function<void (int note)> onChooseNote;   // a note chosen here (LEARN, the monitor): the list follows

    enum class Tab { mapping, velocity, hihat };
    void showTab (Tab);
    Tab getTab() const noexcept { return tab; }

    // For tests: the monitor's lines (newest first); learning; zones; calibrating.
    juce::StringArray getMonitorLines() const;
    void startLearn();
    bool isLearning() const noexcept { return learning; }
    void startLearnController();
    void setZoneCount (int count);              // 0 (off), 2, 3 or 4, for the chosen note
    void setZoneSlot (int zone, int slot);
    void setZoneBound (int index, int value);
    void startCalibration();
    void finishCalibration();
    bool isCalibrating() const noexcept { return calibrating; }
    void pollMidi();                            // the kit's new events (the timer; tests call it after playing)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void refreshMapping();
    void setStep (int index, int value);
    juce::String describe (const synth::drums::Kit::MidiEvent&) const;
    juce::Rectangle<int> zoneBar() const;
    juce::Rectangle<int> pedalBar() const;
    juce::Rectangle<int> monitorArea() const;
    int zoneIndexForDrag (int x) const;

    ParagonX1AudioProcessor& processor;
    int note = 38;
    Tab tab = Tab::mapping;
    juce::TextButton mappingTab { "MAPPING" }, velocityTab { "VELOCITY" }, hihatTab { "HI-HAT" };

    // MAPPING.
    juce::ComboBox drumBox, zoneCount, zoneController;
    std::array<juce::ComboBox, 4> zoneSlots;
    juce::TextButton learn { "LEARN" }, learnController { "LEARN CC" };
    bool learning = false, learningController = false;

    // VELOCITY.
    VelocityCurveEditor curveEditor;
    juce::TextButton softButton { "SOFT" }, gateButton { "GATE" }, straight { "STRAIGHT" };

    // HI-HAT.
    juce::TextButton stepsMenu { "STARTING POINTS..." }, calibrate { "CALIBRATE" };
    bool calibrating = false;
    int calibrationLow = 127, calibrationHigh = 0;

    // THE MIDI MONITOR.
    struct Line { synth::drums::Kit::MidiEvent event; juce::String text; };
    std::deque<Line> lines;
    juce::TextButton clearMonitor { "CLEAR" };
    std::array<int, 128> controllerValues {};

    int dragZone = -1, dragStep = -1;
    juce::RangedAudioParameter* draggingStep = nullptr;
};
