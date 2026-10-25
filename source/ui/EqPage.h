#pragma once

#include "ui/EqDisplay.h"

// The EQ's own page (stomp 4), over the pedal and the dock: a top bar (ON,
// OUTPUT, GAIN SCALE, AUTO GAIN, the analyser's IN and OUT, the display
// range, and BACK TO PEDAL), the big display, and the selected band's
// controls (ON, SHAPE, SLOPE, FREQ, GAIN, Q, STEREO placement, SOLO while
// held, DELETE, and arrows to step through the bands).
class EqPage final : public juce::Component,
                     private juce::Timer
{
public:
    explicit EqPage (ParagonX1AudioProcessor&);
    ~EqPage() override;

    std::function<void()> onClose;
    EqDisplay& getDisplay() noexcept { return display; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showBand (int band);   // attaches the band controls to `band` (-1: none)
    void step (int direction);

    ParagonX1AudioProcessor& processor;
    EqDisplay display;

    // Top bar
    juce::ToggleButton onButton { "ON" }, autoGain { "AUTO GAIN" }, showInput { "IN" }, showOutput { "OUT" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment, autoGainAttachment;
    juce::Slider output { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight },
                 gainScale { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    juce::AudioProcessorValueTreeState::SliderAttachment outputAttachment, gainScaleAttachment;
    juce::ComboBox range;
    juce::TextButton back { "BACK TO PEDAL" };
    juce::Label outputLabel { {}, "OUTPUT" }, gainScaleLabel { {}, "GAIN SCALE" }, analyserLabel { {}, "ANALYSER" }, rangeLabel { {}, "RANGE" };

    // The selected band
    int band = -1;
    juce::ToggleButton bandOn { "ON" };
    juce::ComboBox shape, slope, placement;
    juce::Slider frequency { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 gain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 q { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label frequencyLabel { {}, "FREQ" }, gainLabel { {}, "GAIN" }, qLabel { {}, "Q" }, shapeLabel { {}, "SHAPE" },
                slopeLabel { {}, "SLOPE" }, placementLabel { {}, "STEREO" };
    juce::TextButton previous { "<" }, next { ">" }, solo { "SOLO" }, remove { "DELETE" };
    std::unique_ptr<juce::ButtonParameterAttachment> bandOnAttachment;
    std::unique_ptr<juce::ComboBoxParameterAttachment> shapeAttachment, slopeAttachment, placementAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> frequencyAttachment, gainAttachment, qAttachment;
    juce::Rectangle<int> topBar, controlsArea;
};

// The dock's EQ panel (where the graphic EQ was): the EQ's curve, how many
// bands, and OPEN EQ. Clicking anywhere opens the page too.
class EqOverview final : public juce::Component,
                         private juce::Timer
{
public:
    explicit EqOverview (ParagonX1AudioProcessor&);

    std::function<void()> onOpen;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    ParagonX1AudioProcessor& processor;
    juce::TextButton open { "OPEN EQ" };
    juce::Rectangle<float> graph;
};
