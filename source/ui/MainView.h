#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/DockPanels.h"
#include "ui/Icons.h"
#include "ui/LevelMeter.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/RigBrowser.h"
#include "ui/ModPage.h"
#include "ui/SynthPanels.h"

// A side strip: a title, a gain knob with its value underneath, and a level meter.
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

// Everything in the plugin window, laid out on a fixed 1600 x 900 canvas
// (the editor scales it to the window, staying sharp: it's all vectors).
//
//   header:   logo | SYNTH MOD PRESETS SETTINGS | < preset > | undo redo save settings
//   SYNTH:    OSCILLATORS            | FILTER          | OUTPUT
//             FILTER ENV | AMP ENV | AMP | PLAY         |
//   keyboard: the on-screen keyboard, always there (play it with the mouse)
//
// MOD, PRESETS and SETTINGS cover the synth panels; the keyboard stays.
class MainView final : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainView (ParagonX1AudioProcessor&);
    ~MainView() override;

    static constexpr int width = 1600;
    static constexpr int height = 900;

    enum class Page { synth, presets, settings, modulation };
    void showPage (Page);

    // For snapshots and tests: the oscillator tabs.
    OscillatorPanel& getOscillatorPanel() noexcept { return oscillators; }
    FilterPanel& getFilterPanel() noexcept { return filter; }
    ModPage& getModPage() noexcept { return modPage; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshRig();
    void saveRig();

    ParagonX1AudioProcessor& audioProcessor;

    // Header
    IconButton synthTab { "The synth", Icons::Type::processor, "SYNTH" },
               modTab { "Modulation: LFOs, macros and the mod matrix", Icons::Type::modulation, "MOD" },
               presetsTab { "Browse, save and organise presets", Icons::Type::presets, "PRESETS" },
               settingsTab { "Tuning and other settings", Icons::Type::settings, "SETTINGS" };
    IconButton previousRig { "Previous preset", Icons::Type::chevronLeft }, nextRig { "Next preset", Icons::Type::chevronRight };
    juce::TextButton rigNameButton;
    IconButton undoButton { "Undo (Ctrl+Z)", Icons::Type::undo }, redoButton { "Redo (Ctrl+Shift+Z)", Icons::Type::redo },
               saveButton { "Save the preset (as a new preset if it's a factory one or not saved yet)", Icons::Type::save },
               settingsButton { "Settings", Icons::Type::settings };

    // The synth page
    OscillatorPanel oscillators;
    FilterPanel filter;
    EnvelopePanel filterEnvelope, ampEnvelope;
    AmpPanel amp;
    PlayPanel play;
    GainColumn outputColumn;
    std::vector<juce::Component*> synthPage;

    juce::MidiKeyboardComponent keyboard;

    // Other pages
    ModPage modPage;
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    Page page = Page::synth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
