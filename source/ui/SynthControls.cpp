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
    addMouseListener (this, true);   // right-clicks on the knob, menu or switch open the control's menu
}

ParamControl::~ParamControl() = default;

juce::Range<float> ParamControl::getModulationRange() const
{
    const auto* h = host();
    auto* parameter = state.getParameter (parameterId);
    if (h == nullptr || parameter == nullptr)
        return {};
    const auto routes = h->routesTo (parameterId);
    if (routes.empty())
        return {};
    // Where the routes can take the knob from where it is: + adds the amount of the
    // travel, % a share of what's left (as the engine combines them).
    const auto at = parameter->getValue();
    auto up = 0.0f, down = 0.0f;
    for (const auto& r : routes)
    {
        const auto push = r.percent ? r.amount * (r.amount >= 0.0f ? 1.0f - at : at) : r.amount;
        // A source swinging both ways (an LFO, the pitch wheel) pushes both ways.
        (push >= 0.0f ? up : down) += push;
    }
    return { juce::jlimit (0.0f, 1.0f, at + down), juce::jlimit (0.0f, 1.0f, at + up) };
}

void ParamControl::paintOverChildren (juce::Graphics& g)
{
    if (dropHover)
    {
        g.setColour (ParagonColours::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
    }
    const auto* h = host();
    if (h == nullptr)
        return;
    if (h->isLearning (parameterId))
    {
        // Waiting for a controller: a dashed outline.
        g.setColour (ParagonColours::glow);
        juce::Path outline;
        outline.addRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 4.0f);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.fillPath (dashed);
    }
    if (const auto cc = h->linkedController (parameterId); cc >= 0)
    {
        // Linked to a controller: "CC" over its number, in the space left of a knob (else top right).
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        if (slider != nullptr && slider->isVisible())
        {
            const auto knob = slider->getLookAndFeel().getSliderLayout (*slider).sliderBounds.translated (slider->getX(), slider->getY());
            const auto size = juce::jmin (knob.getWidth(), knob.getHeight());
            const auto left = knob.getCentreX() - size / 2 - 2;
            const juce::Rectangle<int> mark (getX() >= 0 ? 0 : 0, knob.getY(), juce::jmax (16, left), 22);
            g.drawFittedText ("CC\n" + juce::String (cc), mark, juce::Justification::centredRight, 2, 1.0f);
        }
        else
            g.drawText ("CC" + juce::String (cc), getLocalBounds().removeFromTop (12).removeFromRight (30), juce::Justification::centredRight, false);
    }

    // The modulation ring, outside the knob's own arc.
    if (slider == nullptr || ! slider->isVisible())
        return;
    const auto range = getModulationRange();
    if (range.isEmpty() && h->routesTo (parameterId).empty())
        return;
    const auto layout = slider->getLookAndFeel().getSliderLayout (*slider);
    const auto bounds = layout.sliderBounds.toFloat().translated ((float) slider->getX(), (float) slider->getY()).reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto ring = radius + 1.5f;
    const auto rotary = slider->getRotaryParameters();
    const auto angle = [&rotary] (float p) { return rotary.startAngleRadians + p * (rotary.endAngleRadians - rotary.startAngleRadians); };
    juce::Path arc;
    arc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), ring, ring, 0.0f, angle (range.getStart()), angle (juce::jmax (range.getEnd(), range.getStart() + 0.005f)), true);
    g.setColour (ParagonColours::text.withAlpha (0.85f));
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ParamControl::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && host() != nullptr)
        showMenu();
}

void ParamControl::showMenu()
{
    auto* h = host();
    juce::PopupMenu menu;
    const auto cc = h->linkedController (parameterId);
    menu.addSectionHeader (label.getText().isNotEmpty() ? label.getText() : parameterId);
    menu.addItem (1, h->isLearning (parameterId) ? "Stop MIDI learn" : "MIDI learn (move a knob or fader on your controller)");
    if (cc >= 0)
        menu.addItem (2, "Forget MIDI (CC " + juce::String (cc) + ")");
    const auto routes = h->routesTo (parameterId);
    if (! routes.empty())
    {
        menu.addSeparator();
        for (const auto& r : routes)
            menu.addItem (100 + r.index, "Remove modulation: " + r.source + " (" + (r.amount >= 0.0f ? "+" : "")
                                         + juce::String (juce::roundToInt (r.amount * 100.0f)) + (r.percent ? "% of what's left)" : "%)"));
    }
    else if (h->canModulate (parameterId))
    {
        menu.addSeparator();
        menu.addItem (3, "To modulate it: drag a source from the strip above the keyboard onto it", false);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ParamControl> (this)] (int chosen)
                        {
                            if (safe == nullptr || chosen <= 0)
                                return;
                            auto* h = safe->host();
                            if (h == nullptr)
                                return;
                            if (chosen == 1)
                                h->learn (safe->parameterId);
                            else if (chosen == 2)
                                h->forget (safe->parameterId);
                            else if (chosen >= 100)
                                h->removeRoute (chosen - 100);
                            safe->repaint();
                        });
}

bool ParamControl::isInterestedInDragSource (const SourceDetails& details)
{
    const auto what = details.description.toString();
    const auto* h = host();
    if (h == nullptr)
        return false;
    if (what.startsWith ("mod:"))
        return h->canModulate (parameterId);
    return what.startsWith ("cc:");
}

void ParamControl::itemDropped (const SourceDetails& details)
{
    dropHover = false;
    const auto what = details.description.toString();
    if (auto* h = host())
    {
        if (what.startsWith ("mod:"))
            h->addRoute (what.fromFirstOccurrenceOf ("mod:", false, false), parameterId);
        else if (what.startsWith ("cc:"))
            h->linkController (what.fromFirstOccurrenceOf ("cc:", false, false).getIntValue(), parameterId);
    }
    repaint();
}

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
