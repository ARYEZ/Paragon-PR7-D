#pragma once

#include "PluginProcessor.h"

// FREEFORM's drawing area (stage 3d, part 2): one cycle, drawn by dragging
// (with SHIFT held, a straight line from where the drag began). The thin
// line is what's drawn; the bright one what plays (smoothed, its edges
// joined, normalised). SPLIT's place shows when it's in use. Drawing on an
// oscillator whose WAVE isn't FREEFORM switches it to FREEFORM, so you hear
// what you draw.
class FreeformEditor final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
{
public:
    explicit FreeformEditor (ParagonX1AudioProcessor&);
    void setOscillator (int index);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<float> getPlotArea() const;
    int pointAt (float x) const;
    float valueAt (float y) const;

private:
    void timerCallback() override;
    void drawTo (juce::Point<float>);

    ParagonX1AudioProcessor& processor;
    int oscillator = 0;
    juce::Point<float> dragStart, lastPoint;
    synth::FreeformDrawing startDrawing;
    synth::FreeformDrawing shown;
};

// The controls under it: where to start from, SMOOTH A and B, SPLIT,
// NORMALIZE, EDGE and its mode, and EXPORT (the cycle as a WAV file). They
// belong to the drawing (kept with it in the preset), not to the knobs a
// DAW can automate.
class FreeformControls final : public juce::Component,
                               private juce::Timer
{
public:
    explicit FreeformControls (ParagonX1AudioProcessor&);
    void setOscillator (int index);
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void change (const std::function<void (synth::FreeformDrawing&)>&);

    struct Item final : public juce::Component
    {
        Item (const juce::String& name, juce::Component& control);
        void resized() override;
        juce::Label label;
        juce::Component& control;
    };

    ParagonX1AudioProcessor& processor;
    int oscillator = 0;
    juce::ComboBox start, edgeMode;
    juce::Slider smoothA, smoothB, split, edge;
    juce::ToggleButton normalise;
    juce::TextButton exportButton { "EXPORT" };
    std::vector<std::unique_ptr<Item>> items;
    std::unique_ptr<juce::FileChooser> chooser;
    bool refreshing = false;
};
