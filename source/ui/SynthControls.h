#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// What a control asks of the window it's in (stage 4f): the routes moving
// its parameter (for its modulation ring and menu), MIDI learn, and what
// happens when a modulation source or a controller is dropped on it. The
// main view answers; a control finds it among its parents.
class ControlHost
{
public:
    virtual ~ControlHost() = default;

    struct RouteInfo
    {
        int index = 0;            // in the matrix
        juce::String source;      // its name, "LFO 1"
        float amount = 0.0f;      // -1 to 1
        bool percent = false;
    };
    virtual std::vector<RouteInfo> routesTo (const juce::String& parameterId) const = 0;
    virtual bool canModulate (const juce::String& parameterId) const = 0;
    virtual void addRoute (const juce::String& sourceId, const juce::String& parameterId) = 0;
    virtual void removeRoute (int index) = 0;

    virtual int linkedController (const juce::String& parameterId) const = 0;   // -1: none
    virtual bool isLearning (const juce::String& parameterId) const = 0;
    virtual void learn (const juce::String& parameterId) = 0;                  // or stop learning, if it is
    virtual void forget (const juce::String& parameterId) = 0;
    virtual void linkController (int controller, const juce::String& parameterId) = 0;

    // What a drag carries: "mod:<source id>" (a modulation source) or "cc:<number>" (a controller).
    static juce::String sourceDrag (const juce::String& sourceId) { return "mod:" + sourceId; }
    static juce::String controllerDrag (int controller) { return "cc:" + juce::String (controller); }
};

// The synth page's controls. One ParamControl shows one parameter, as
// whatever suits its type:
//   a number   -> a knob, its name above and its value below
//   a choice   -> a drop-down menu, its name above
//   an on/off  -> an LED switch, its name above
// It can be pointed at another parameter of the same kind (attach), which is
// how the oscillator tabs work: one set of controls, re-pointed at the
// selected oscillator's parameters.
//
// Stage 4f: a knob the matrix moves shows a ring outside its own, from where
// the routes can take it; right-click for MIDI learn and the routes; drop a
// modulation source on it to route it there, or a controller to link it.
class ParamControl final : public juce::Component,
                           public juce::DragAndDropTarget
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
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    // For tests: what the ring shows (0 to 1 along the knob's travel; empty: no ring).
    juce::Range<float> getModulationRange() const;

    // The cards' height for a knob: name, knob, value.
    static constexpr int knobHeight = 86;

private:
    ControlHost* host() const { return findParentComponentOfClass<ControlHost>(); }
    void showMenu();

    juce::AudioProcessorValueTreeState& state;
    juce::String parameterId;
    bool compact = false, dropHover = false;
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
