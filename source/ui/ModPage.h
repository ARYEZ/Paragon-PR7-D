#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"
#include "ui/MultiPointEditor.h"
#include "ui/StepSeqEditor.h"
#include "ui/SynthDisplays.h"

// The MOD page (stage 4a; envelopes and drawn shapes 4b):
//
//   MODULATORS: LFO 1-4 | ENV 1-8 | SHAPE 1-3 | PITCH | STEP, a picture or editor and its controls
//   MACROS: three knobs (renamed by double-clicking),  |  MATRIX: one row per route
//   two buttons, an on-screen MOD WHEEL                |

// An LFO's wave, two cycles of it, as set (with OFFSET; the start marked by PHASE).
class LfoDisplay final : public ParameterDisplay
{
public:
    explicit LfoDisplay (juce::AudioProcessorValueTreeState&);
    void setLfo (int index);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> snapshot() const override;
    float lfoValue (const char* what) const;
    int lfo = 0;
};

// MODULATORS: the LFOs, the modulation envelopes, the drawn LFO shapes, the PITCH module and the STEP SEQUENCER,
// a tab each (one set of controls, pointed at the tab chosen): a picture or
// editor on the left, its controls on the right.
class ModulatorPanel final : public SynthPanel,
                             private juce::Timer
{
public:
    explicit ModulatorPanel (ParagonX1AudioProcessor&);
    enum class Kind { lfo, envelope, shape, pitch, step };
    void selectLfo (int index) { select (Kind::lfo, index); }
    void selectEnvelope (int index) { select (Kind::envelope, index); }
    void selectShape (int index) { select (Kind::shape, index); }
    void selectPitch() { select (Kind::pitch, 0); }
    void selectStepSeq() { select (Kind::step, 0); }
    Kind getKind() const noexcept { return kind; }
    int getSelected() const noexcept { return selected; }
    MultiPointEditor& getEditor() noexcept { return editor; }
    StepSeqEditor& getStepEditor() noexcept { return stepEditor; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void select (Kind, int index);
    void timerCallback() override;
    bool isSynced() const;
    void startFrom (int choice);

    juce::AudioProcessorValueTreeState& state;
    ParagonX1AudioProcessor& processor;
    struct Tab { Kind kind; int index; std::unique_ptr<juce::TextButton> button; };
    std::vector<Tab> tabs;
    LfoDisplay display;
    MultiPointEditor editor;
    StepSeqEditor stepEditor;
    ParamControl wave, sync, rate, division, trigger, phase, offset, delay, fadeIn, oneShot;
    ParamControl envMode, envTrigger, envSync, envSpeed, envSteps, envFlip, envOffset;
    // The PITCH module (stage 4c), and which oscillators follow its envelope.
    ParamControl pitchOn, pitchMode, pitchSpeed, pitchRange, pitchSteps, pitchFlip, keytrack, bendLag, glideCurve, glideSplit;
    std::vector<std::unique_ptr<ParamControl>> pitchRoutes;
    juce::Label pitchRouteLabel;
    // The STEP SEQUENCER (stage 4d).
    ParamControl seqOn, seqTrigger, seqSpeed, seqTiming, seqMode, seqLength, seqGate, seqContour,
                 seqDecay, seqVolMix, seqWidth, shuffle, seqDelay, seqFadeIn, seqLowKey, seqHighKey;
    std::vector<ParamControl*> seqControls() noexcept;
    juce::ComboBox startFromBox;
    juce::Label startFromLabel, hint;
    Kind kind = Kind::lfo;
    int selected = 0;
    bool showingSync = false;
};

class MacroPanel final : public SynthPanel,
                         private juce::Timer
{
public:
    explicit MacroPanel (ParagonX1AudioProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    ParagonX1AudioProcessor& processor;
    std::vector<std::unique_ptr<ParamControl>> knobs, buttons;
    std::vector<std::unique_ptr<juce::Label>> names;   // double-click to rename
    juce::Slider modWheel { juce::Slider::LinearBarVertical, juce::Slider::NoTextBox };
    juce::Label modWheelLabel;
};

// The routes, one a row.
class MatrixPanel final : public SynthPanel,
                          private juce::Timer
{
public:
    explicit MatrixPanel (ParagonX1AudioProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

    // For tests: add a route as the + button does, and the rows shown.
    void addRoute();
    int getNumRows() const noexcept { return (int) rows.size(); }

    struct Row final : public juce::Component
    {
        Row (MatrixPanel&, int index);
        void resized() override;
        void show (const ModRouteData&);

        MatrixPanel& owner;
        int index;
        juce::ToggleButton on;
        juce::ComboBox source;
        juce::TextButton destination, mode, remove { "X" };
        juce::Slider amount { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                     lag { juce::Slider::LinearBar, juce::Slider::TextBoxLeft },
                     curve { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    };
    Row* getRow (int index) { return index >= 0 && index < (int) rows.size() ? rows[(size_t) index].get() : nullptr; }

private:
    friend struct Row;
    void timerCallback() override;
    void rebuildRows();
    void change (int index, const std::function<void (ModRouteData&)>&, bool finished);
    void chooseDestination (int index);

    ParagonX1AudioProcessor& processor;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton addButton { "+ ADD ROUTE" };
    juce::Label countLabel, emptyLabel;
    ModMatrix shown;
};

class ModPage final : public juce::Component
{
public:
    explicit ModPage (ParagonX1AudioProcessor&);
    void resized() override;

    ModulatorPanel& getModulatorPanel() noexcept { return modulators; }
    MatrixPanel& getMatrixPanel() noexcept { return matrix; }

private:
    ModulatorPanel modulators;
    MacroPanel macros;
    MatrixPanel matrix;
};
