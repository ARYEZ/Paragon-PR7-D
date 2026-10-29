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
#include "ui/ArpPage.h"
#include "ui/DrumPage.h"
#include "ui/FxPage.h"
#include "ui/ModSourceStrip.h"
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
//   sources:  the modulation sources, to drag onto knobs (stage 4f), and the last MIDI controller moved
//   keyboard: the on-screen keyboard, always there (play it with the mouse)
//
// MOD, PRESETS and SETTINGS cover the synth panels; the keyboard stays.
// The on-screen keyboard, with THE DRUMS' 12 keys tinted while the kit is on (stage 6a).
class KitKeyboard final : public juce::MidiKeyboardComponent
{
public:
    KitKeyboard (ParagonX1AudioProcessor&, juce::MidiKeyboardState&, Orientation);
    bool isKitKey (int note) const noexcept { return kitOn && note >= kitRoot && note < kitRoot + 12; }
    void paint (juce::Graphics&) override;

private:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    void followKit();

    // Its own timer (the keyboard already has one for scrolling).
    struct Poll final : juce::Timer
    {
        std::function<void()> callback;
        void timerCallback() override { callback(); }
    };
    ParagonX1AudioProcessor& processor;
    bool kitOn = false;
    int kitRoot = 36;
    Poll poll;
};

class MainView final : public juce::Component,
                       public juce::DragAndDropContainer,
                       public ControlHost,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainView (ParagonX1AudioProcessor&);
    ~MainView() override;

    static constexpr int width = 1600;
    static constexpr int height = 900;

    enum class Page { synth, presets, settings, modulation, arp, fx, drums };
    void showPage (Page);

    // For snapshots and tests: the oscillator tabs.
    OscillatorPanel& getOscillatorPanel() noexcept { return oscillators; }
    FilterPanel& getFilterPanel() noexcept { return filter; }
    ModPage& getModPage() noexcept { return modPage; }
    ArpPage& getArpPage() noexcept { return arpPage; }
    FxPage& getFxPage() noexcept { return fxPage; }
    DrumPage& getDrumPage() noexcept { return drumPage; }
    ModSourceStrip& getSourceStrip() noexcept { return sources; }
    SettingsPage& getSettingsPage() noexcept { return settingsPage; }

    // ControlHost (stage 4f): the knobs' modulation rings, menus and drops.
    std::vector<RouteInfo> routesTo (const juce::String& parameterId) const override;
    bool canModulate (const juce::String& parameterId) const override;
    void addRoute (const juce::String& sourceId, const juce::String& parameterId) override;
    void removeRoute (int index) override;
    int linkedController (const juce::String& parameterId) const override;
    bool isLearning (const juce::String& parameterId) const override;
    void learn (const juce::String& parameterId) override;
    void forget (const juce::String& parameterId) override;
    void linkController (int controller, const juce::String& parameterId) override;

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
               arpTab { "The arpeggiators", Icons::Type::arpeggio, "ARP" },
               drumsTab { "The drums: a kit of 12, on 12 keys", Icons::Type::drums, "DRUMS" },
               fxTab { "The rack: ECHELON, DOMINION, CONSTRICTOR and the LIMITER after the synth", Icons::Type::rack, "FX" },
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

    ModSourceStrip sources;
    KitKeyboard keyboard;
    // What the knobs show (routes, links, learning), as last drawn: a change repaints them.
    std::vector<ModRouteData> shownRoutes;
    MidiLinks shownLinks;
    juce::String shownLearning;

    // Other pages
    ModPage modPage;
    ArpPage arpPage;
    FxPage fxPage;
    DrumPage drumPage;
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    Page page = Page::synth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
