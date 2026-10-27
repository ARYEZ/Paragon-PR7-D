#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The synth page's controls. One ParamControl shows one parameter, as
// whatever suits its type:
//   a number   -> a knob, its name above and its value below
//   a choice   -> a drop-down menu, its name above
//   an on/off  -> an LED switch, its name above
// It can be pointed at another parameter of the same kind (attach), which is
// how the oscillator tabs work: one set of controls, re-pointed at the
// selected oscillator's parameters.
class ParamControl final : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& label,
                  const juce::String& tooltip = {});
    ~ParamControl() override;

    void attach (const juce::String& parameterId);

    // Compact: no name above (for a strip beside a panel's title). A switch
    // shows its name beside its LED instead; a menu fills the space.
    void setCompact (bool);
    const juce::String& getParameterId() const noexcept { return parameterId; }

    void resized() override;

    // The cards' height for a knob: name, knob, value.
    static constexpr int knobHeight = 86;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String parameterId;
    bool compact = false;
    juce::Label label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> box;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> boxAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
};

// A titled dark panel that lays its controls out in rows.
class SynthPanel : public juce::Component
{
public:
    explicit SynthPanel (const juce::String& title);

    void paint (juce::Graphics&) override;

    // Where the content goes, under the title, and the title's strip (for things beside it).
    juce::Rectangle<int> getContentArea() const;
    juce::Rectangle<int> getTitleArea() const { return getLocalBounds().reduced (14, 0).withHeight (32); }

    // Lays `controls` out side by side across `row`, each `width` wide (or
    // sharing the row, if 0), centred.
    static void layoutRow (juce::Rectangle<int> row, const std::vector<juce::Component*>& controls, int width = 0, int gap = 4);

protected:
    juce::String title;
};
