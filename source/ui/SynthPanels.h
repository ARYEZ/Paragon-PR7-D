#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"
#include "ui/SynthDisplays.h"

// The SYNTH page's panels, laid out like Avenger's main page: the
// oscillators (tabs), the FILTER, the two envelopes, the AMP, and how the
// keyboard plays it. Our own drawing, in the PR7 family's look.

// TABLE: which wavetable an oscillator plays. The factory's, a table loaded
// from a file (kept with the preset), or "Load WAV file..." to load one.
class WavetableChooser final : public juce::Component
{
public:
    explicit WavetableChooser (ParagonX1AudioProcessor&);
    void setOscillator (int index);
    // Shows what the oscillator plays now (after an undo, a preset...).
    void refresh();
    void resized() override;

    // Opens the file chooser, as "Load WAV file..." does.
    void browse();

private:
    ParagonX1AudioProcessor& processor;
    int oscillator = 0;
    juce::Label label;
    juce::ComboBox box;
    std::unique_ptr<juce::FileChooser> chooser;
    static constexpr int loadedId = 1000, loadFileId = 1001;
};

// OSCILLATORS: eight tabs (each with its own on/off LED), and the selected
// oscillator's wave picture and controls: its wave, level, pitch and start,
// its NOISE and its SUB. A WAV file dropped on it loads as the oscillator's wavetable.
class OscillatorPanel final : public SynthPanel,
                              public juce::FileDragAndDropTarget,
                              private juce::Timer
{
public:
    explicit OscillatorPanel (ParagonX1AudioProcessor&);

    void selectOscillator (int index);
    int getSelected() const noexcept { return selected; }

    // WAVE: the oscillator's sound (wave, pitch, start, noise, sub, level).
    // SHAPE: bending its wave (stage 3b).
    // FM: SYNC, FM and AM (stage 3c).
    // VOICES: its unison copies, vibrato and chorder (stage 2).
    enum class Page { wave, voices, shape, fm };
    void showPage (Page);
    Page getPage() const noexcept { return page; }

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    WavetableChooser& getWavetableChooser() noexcept { return tableChooser; }

private:
    void timerCallback() override;
    // WAVETABLE's controls show when the selected oscillator plays one; WIDTH when it doesn't.
    bool isWavetable() const;
    bool isShown (const char* what) const;
    void updateVisibility();

    struct Tab final : public juce::Button
    {
        Tab (juce::AudioProcessorValueTreeState&, int index);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void resized() override;

        int index;
        juce::ToggleButton power;
        juce::AudioProcessorValueTreeState::ButtonAttachment powerAttachment;
    };

    ParagonX1AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<Tab>> tabs;
    WavetableChooser tableChooser;
    bool showingWavetable = false;
    juce::TextButton wavePageButton { "WAVE" }, shapePageButton { "SHAPE" }, fmPageButton { "FM" }, voicesPageButton { "VOICES" };
    Page page = Page::wave;
    WaveDisplay wave;
    VoicesDisplay voicesDisplay;
    ShapeDisplay shapeDisplay;
    std::vector<std::unique_ptr<ParamControl>> controls;   // in oscSettings' order where they appear
    std::vector<const char*> settingNames;                 // ...which setting each is
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> sectionTitles;   // PITCH, START, NOISE, SUB, LEVEL
    int selected = 0;

    ParamControl& control (const char* what);
    Page pageOf (const char* what) const;   // which page a setting's control is on (WAVE's left column shows on SHAPE too)
};

// FILTER: ON and TYPE beside the title, its response, and its knobs.
class FilterPanel final : public SynthPanel
{
public:
    explicit FilterPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

private:
    FilterDisplay display;
    ParamControl on, type, cutoff, resonance, drive, keytrack, envelope, velocity, resonanceVelocity, envelopeVelocity;
};

// An envelope: its shape, and ATTACK HOLD DECAY SUSTAIN RELEASE with their curves.
class EnvelopePanel final : public SynthPanel
{
public:
    EnvelopePanel (juce::AudioProcessorValueTreeState&, const juce::String& title, std::array<juce::String, 8> ids);
    void resized() override;

private:
    EnvelopeDisplay display;
    std::vector<std::unique_ptr<ParamControl>> controls;
};

// AMP: level, velocity, pan and SPIKE.
class AmpPanel final : public SynthPanel
{
public:
    explicit AmpPanel (juce::AudioProcessorValueTreeState&);
    void resized() override;

private:
    ParamControl volume, velocity, pan, panKeytrack, spread, spike, spikeCurve, spikeVelocity;
};

// PLAY: poly or mono, glide, the pitch wheel's range, how many notes at once,
// and the preset's own LEVEL; how many notes are sounding now beside the title.
class PlayPanel final : public SynthPanel,
                        private juce::Timer
{
public:
    explicit PlayPanel (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    ParamControl mode, legato, glide, glideMode, bendUp, bendDown, voices, level;
    int shownVoices = -1;
};
