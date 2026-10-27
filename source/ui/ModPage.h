#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"
#include "ui/SynthDisplays.h"

// The MOD page (stage 4a): the LFOs, the macros and the mod matrix.
//
//   LFO 1-4 (one set of controls, pointed at the tab chosen)  |  MATRIX:
//   MACROS: three knobs (renamed by double-clicking), two     |  one row per route
//           buttons, and an on-screen MOD WHEEL               |

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

class LfoPanel final : public SynthPanel,
                       private juce::Timer
{
public:
    explicit LfoPanel (juce::AudioProcessorValueTreeState&);
    void selectLfo (int index);
    int getSelected() const noexcept { return selected; }
    void resized() override;

private:
    void timerCallback() override;
    bool isSynced() const;

    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    LfoDisplay display;
    ParamControl wave, sync, rate, division, trigger, phase, offset, delay, fadeIn, oneShot;
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

    LfoPanel& getLfoPanel() noexcept { return lfos; }
    MatrixPanel& getMatrixPanel() noexcept { return matrix; }

private:
    LfoPanel lfos;
    MacroPanel macros;
    MatrixPanel matrix;
};
