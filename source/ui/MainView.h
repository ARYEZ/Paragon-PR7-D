#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/DockPanels.h"
#include "ui/EffectPanel.h"
#include "ui/Finish.h"
#include "ui/GraphicEqPanel.h"
#include "ui/Footswitch.h"
#include "ui/LedDisplay.h"
#include "ui/LevelMeter.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/PedalBody.h"
#include "ui/PerformanceControls.h"
#include "ui/RigBrowser.h"
#include "ui/SignalChain.h"
#include "ui/TunerPanel.h"

// A side strip from the mockup: a title, a gain knob with its value
// underneath, and a level meter.
class GainColumn final : public juce::Component
{
public:
    GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                const juce::String& parameterId, LevelMeterSource& meterSource);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    LevelMeter meter;
};

// A solid triangle printed on the unit beside the screen (the rig up and
// down arrows), in the finish's ink: like juce::ArrowButton, whose colour
// can't change after it's made.
class InkArrowButton final : public juce::Button
{
public:
    InkArrowButton (const juce::String& name, float direction);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Path path;
};

// Everything in the plugin window, laid out on a fixed 1600 x 900 canvas.
// That's the mockup image (1672 x 941) scaled by 0.957, so positions are
// written in the mockup's own pixel coordinates and scaled: what you see in
// the mockup at (x, y) is drawn at (x, y) * 0.957. The editor then scales
// the whole view to the window size, staying sharp (everything is vectors).
//
//   header:  logo | PROCESSOR PRESETS SETTINGS | < rig > | BPM TAP | undo redo save settings
//   middle:  INPUT | the pedal unit (7 stomps: 3 over 4) with its expression pedal | OUTPUT
//   dock:    RIG BROWSER | SELECTED RIG | TAGS + RIG SUMMARY (or a block's knobs) | EXPRESSION ASSIGN
//   bottom:  SIGNAL CHAIN
class MainView final : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainView (ParagonX1AudioProcessor&);
    ~MainView() override;

    static constexpr int width = 1600;
    static constexpr int height = 900;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // The blocks in signal-chain order; footswitches, the chain strip and
    // the summary cards select these to show their knobs.
    // tunerBlock isn't in the chain (the tuner only listens): it's the TUNER stomp's panel.
    enum Block { pitchBlock, compBlock, filterPitchBlock, eqBlock, glitchBlock, modBlock, delayBlock, reverbBlock, loFiBlock, numBlocks,
                 tunerBlock = numBlocks };

    enum class Page { processor, presets, settings };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyFinish();   // colours the printed labels and redraws the unit in the current finish
    void timerCallback() override;

    void selectBlock (int block);
    void refreshPerformance();
    void toggleTuner();
    void tap();
    void showPage (Page);
    void refreshRig();
    void saveRig();
    juce::String valueText (const char* parameterId) const;
    float rawValue (const char* parameterId) const;

    ParagonX1AudioProcessor& audioProcessor;

    // Header
    IconButton processorTab { "Pedalboard and effects", Icons::Type::processor, "PROCESSOR" },
               presetsTab { "Browse, save and organise rigs", Icons::Type::presets, "PRESETS" },
               settingsTab { "Tuner, MIDI and other settings", Icons::Type::settings, "SETTINGS" };
    IconButton previousRig { "Previous rig", Icons::Type::chevronLeft }, nextRig { "Next rig", Icons::Type::chevronRight };
    juce::TextButton rigNameButton, tempoButton;
    IconButton tapButton { "Tap the tempo for the delay. Hold to open the tuner.", Icons::Type::tap, "TAP" },
               undoButton { "Undo (Ctrl+Z)", Icons::Type::undo }, redoButton { "Redo (Ctrl+Shift+Z)", Icons::Type::redo },
               saveButton { "Save the rig (as a new rig if it's a factory rig or not saved yet)", Icons::Type::save },
               settingsButton { "Settings", Icons::Type::settings };
    HoldDetector headerTapHold, pedalTapHold;

    GainColumn inputColumn, outputColumn;

    // The pedal unit and what sits on it
    PedalBody pedal;
    LedDisplay display;
    InkArrowButton rigUp { "Previous rig", 0.75f }, rigDown { "Next rig", 0.25f };
    juce::TextButton pedalSave { "SAVE" }, pedalSaveAs { "SAVE AS..." }, bypassButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    // The 2 x 3 button cluster: UNDO REDO BYPASS / TUNER RESET TAP
    juce::TextButton undoPedal, redoPedal, tunerPedal, resetPedal, tapPedal;
    juce::AudioProcessorValueTreeState::ButtonAttachment tunerAttachment;
    juce::Label undoLabel { {}, "UNDO" }, redoLabel { {}, "REDO" }, bypassLabel { {}, "BYPASS" },
                tunerLabel { {}, "TUNER" }, resetLabel { {}, "RESET" }, tapLabel { {}, "TAP" }, holdLabel { {}, "HOLD TO TUNER" };

    juce::Slider rigLevelKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                 presenceKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::AudioProcessorValueTreeState::SliderAttachment rigLevelAttachment, presenceAttachment;
    juce::Label rigLevelLabel { {}, "RIG LEVEL" }, presenceLabel { {}, "PRESENCE" };
    // The seven stomps: 1-3 on the top row, 4-7 on the bottom.
    std::array<std::unique_ptr<Footswitch>, 7> footswitches;

    // Expression pedal and tuner
    ExpressionPedal expressionPedal;
    TunerReader tunerReader;
    LedDisplay::Contents::Tuner tunerState;
    int ticks = 0;

    // Dock and signal chain
    RigNavigator rigNavigator;
    SelectedRigPanel selectedRig;
    ExpressionPanel expressionPanel;
    std::array<std::unique_ptr<EffectPanel>, numBlocks> panels;   // empty at eqBlock: the EQ has its own panel
    GraphicEqPanel eqPanel;
    TunerPanel tunerPanel;
    juce::TextButton closePanel { "GRAPHIC EQ" };   // back to the EQ, the dock's default
    SignalChain signalChain;
    int selectedBlock = eqBlock;

    // Other pages
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    std::vector<juce::Component*> processorPage;
    Page page = Page::processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
