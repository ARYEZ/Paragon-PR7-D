#include "SynthControls.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

ParamControl::ParamControl (juce::AudioProcessorValueTreeState& stateToUse, const juce::String& id, const juce::String& labelText,
                            const juce::String& tooltip)
    : state (stateToUse)
{
    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, ParagonColours::textDim);
    label.setMinimumHorizontalScale (0.6f);
    label.setBorderSize ({});
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    auto* parameter = state.getParameter (id);
    jassert (parameter != nullptr);   // a typo in a panel's parameter list
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
    {
        box = std::make_unique<juce::ComboBox>();
        box->addItemList (choice->choices, 1);
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
        box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box->setColour (juce::ComboBox::textColourId, ParagonColours::accent);
        box->setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
        box->setTooltip (tooltip);
        addAndMakeVisible (*box);
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
    {
        toggle = std::make_unique<juce::ToggleButton>();
        toggle->setTooltip (tooltip);
        addAndMakeVisible (*toggle);
    }
    else
    {
        slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        slider->setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        slider->setTooltip (tooltip);
        addAndMakeVisible (*slider);
    }
    attach (id);
}

ParamControl::~ParamControl() = default;

void ParamControl::attach (const juce::String& id)
{
    if (id == parameterId)
        return;
    parameterId = id;
    auto* parameter = state.getParameter (id);
    jassert (parameter != nullptr);

    // Drop the old attachment first, so it can't write the old value into the new parameter.
    sliderAttachment.reset();
    boxAttachment.reset();
    toggleAttachment.reset();
    if (slider != nullptr)
    {
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, *slider);
        // Double-click a knob to put it back to its default.
        slider->setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    else if (box != nullptr)
        boxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, *box);
    else if (toggle != nullptr)
        toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, *toggle);
}

void ParamControl::setCompact (bool shouldBeCompact)
{
    compact = shouldBeCompact;
    label.setVisible (! compact);
    if (toggle != nullptr)
    {
        toggle->setButtonText (compact ? label.getText() : juce::String());
        toggle->setColour (juce::ToggleButton::textColourId, ParagonColours::text);
    }
    resized();
}

void ParamControl::resized()
{
    auto area = getLocalBounds();
    if (compact)
    {
        if (slider != nullptr)
            slider->setBounds (area);
        else if (box != nullptr)
            box->setBounds (area.withSizeKeepingCentre (area.getWidth(), juce::jmin (24, area.getHeight())));
        else if (toggle != nullptr)
            toggle->setBounds (area);
        return;
    }
    label.setBounds (area.removeFromTop (14));
    area.removeFromTop (2);
    if (slider != nullptr)
        slider->setBounds (area);
    else if (box != nullptr)
        box->setBounds (area.withSizeKeepingCentre (area.getWidth() - 2, juce::jmin (26, area.getHeight())).withY (area.getY() + 4));
    else if (toggle != nullptr)
        toggle->setBounds (area.withSizeKeepingCentre (24, 24).withY (area.getY() + 4));
}

//==============================================================================
SynthPanel::SynthPanel (const juce::String& panelTitle) : title (panelTitle) {}

void SynthPanel::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    DockStyle::drawTitle (g, getTitleArea(), title);
}

juce::Rectangle<int> SynthPanel::getContentArea() const
{
    return getLocalBounds().reduced (10, 0).withTrimmedTop (34).withTrimmedBottom (8);
}

void SynthPanel::layoutRow (juce::Rectangle<int> row, const std::vector<juce::Component*>& controls, int width, int gap)
{
    if (controls.empty())
        return;
    const auto count = (int) controls.size();
    if (width <= 0)
        width = (row.getWidth() - gap * (count - 1)) / count;
    const auto total = width * count + gap * (count - 1);
    auto x = row.getX() + (row.getWidth() - total) / 2;
    for (auto* control : controls)
    {
        if (control != nullptr)
            control->setBounds (x, row.getY(), width, row.getHeight());
        x += width + gap;
    }
}
