#include "ModSourceStrip.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthControls.h"

ModSourceStrip::Chip::Chip (juce::String dragDescription, juce::String chipText, juce::String tip)
    : description (std::move (dragDescription)), text (std::move (chipText))
{
    setTooltip (tip);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ModSourceStrip::Chip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    g.setColour (juce::Colour (0xff151217));
    g.fillRoundedRectangle (area, 3.0f);
    if (level >= 0.0f)
    {
        g.setColour (ParagonColours::accent.withAlpha (0.25f));
        g.fillRoundedRectangle (area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 3.0f);
    }
    g.setColour (ParagonColours::panelEdge.brighter (0.3f));
    g.drawRoundedRectangle (area, 3.0f, 1.0f);
    if (toWheel != nullptr)
        area.removeFromRight ((float) toWheel->getWidth() + 2.0f);
    g.setColour (description.isEmpty() ? ParagonColours::textDim : ParagonColours::text);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

void ModSourceStrip::Chip::mouseDrag (const juce::MouseEvent& e)
{
    if (description.isEmpty() || e.getDistanceFromDragStart() < 4)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
            container->startDragging (description, this);
}

ModSourceStrip::ModSourceStrip (ParagonX1AudioProcessor& p) : processor (p)
{
    using synth::mod::Source;
    const auto add = [this] (Source source, const juce::String& text, const char* toWheelId = nullptr)
    {
        const auto id = juce::String (synth::mod::sourceIds[(size_t) source]);
        const auto name = juce::String (synth::mod::sourceNames[(size_t) source]);
        auto chip = std::make_unique<Chip> (ControlHost::sourceDrag (id), text, "Drag onto a knob to modulate it with " + name
                                                                             + " (the route appears in the MOD page's matrix, at +50%).");
        if (toWheelId != nullptr)
        {
            chip->toWheel = std::make_unique<juce::TextButton> ("MW");
            chip->toWheel->setClickingTogglesState (true);
            chip->toWheel->setTooltip (name + " drives the mod wheel too: sounds made for the wheel follow it (as the wheel does, "
                                       "whichever is higher). Kept when you change presets.");
            chip->toWheel->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
            chip->toWheel->setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim);
            chip->toWheel->setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
            chip->toWheel->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
            chip->addAndMakeVisible (*chip->toWheel);
            chip->toWheelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.getParameters(), toWheelId,
                                                                                                             *chip->toWheel);
        }
        addAndMakeVisible (*chip);
        chips.push_back (std::move (chip));
    };
    const auto group = [this] (const juce::String& name) { groups.push_back ({ (int) chips.size(), name }); };

    group ("LFO");
    for (int k = 0; k < 4; ++k)
        add ((Source) ((int) Source::lfo1 + k), juce::String (k + 1));
    group ("ENV");
    for (int k = 0; k < 8; ++k)
        add ((Source) ((int) Source::env1 + k), juce::String (k + 1));
    group ("MACRO");
    for (int k = 0; k < 3; ++k)
        add ((Source) ((int) Source::macro1 + k), juce::String (k + 1));
    add (Source::button1, "B1");
    add (Source::button2, "B2");
    group ("PLAY");
    add (Source::modWheel, "WHEEL");
    add (Source::pitchBend, "BEND");
    add (Source::velocity, "VEL");
    add (Source::aftertouch, "AT", ParamIDs::mwAftertouch);
    add (Source::keytrack, "KEY");
    add (Source::breath, "BREATH", ParamIDs::mwBreath);
    add (Source::expression, "EXPR", ParamIDs::mwExpression);
    add (Source::sustain, "SUS", ParamIDs::mwSustain);
    add (Source::random, "RND");
    group ("MORE");
    add (Source::pitchEnvelope, "P.ENV");
    add (Source::stepSeq, "STEP");

    addAndMakeVisible (controller);
    controller.level = 0.0f;
    startTimerHz (15);
}

ModSourceStrip::~ModSourceStrip() = default;

ModSourceStrip::Chip* ModSourceStrip::chipFor (const juce::String& description)
{
    if (controller.description == description)
        return &controller;
    for (auto& chip : chips)
        if (chip->description == description)
            return chip.get();
    return nullptr;
}

void ModSourceStrip::timerCallback()
{
    // The last controller moved, and how far.
    const auto cc = processor.getLastController();
    const auto level = cc >= 0 ? processor.getLastControllerValue() : 0.0f;
    if (cc != shownController || ! juce::approximatelyEqual (level, controller.level))
    {
        shownController = cc;
        controller.description = cc >= 0 ? ControlHost::controllerDrag (cc) : juce::String();
        controller.text = cc >= 0 ? "CC " + juce::String (cc) : "CC --";
        controller.level = level;
        controller.repaint();
    }
}

void ModSourceStrip::paint (juce::Graphics& g)
{
    // Each group's name before its chips.
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (const auto& [first, name] : groups)
    {
        const auto b = chips[(size_t) first]->getBounds();
        g.drawText (name, juce::Rectangle<int> (b.getX() - 46, b.getY(), 42, b.getHeight()), juce::Justification::centredRight, false);
    }
}

void ModSourceStrip::resized()
{
    auto area = getLocalBounds();
    controller.setBounds (area.removeFromRight (78));
    area.removeFromRight (8);
    // Chips: the numbered ones narrow, the named ones wider, the MW ones widest; group names between.
    const auto widthOf = [] (const Chip& c) { return c.toWheel != nullptr ? 2.0f : c.text.length() > 2 ? 1.5f : 0.75f; };
    auto units = 0.0f;
    for (auto& c : chips)
        units += widthOf (*c);
    const auto gaps = (int) groups.size() * 48;
    const auto unit = ((float) area.getWidth() - (float) gaps) / units;
    auto x = (float) area.getX();
    size_t nextGroup = 0;
    for (size_t i = 0; i < chips.size(); ++i)
    {
        if (nextGroup < groups.size() && groups[nextGroup].first == (int) i)
        {
            x += 48.0f;
            ++nextGroup;
        }
        const auto w = unit * widthOf (*chips[i]);
        // Each chip's edges rounded (not its width), so neighbours meet without overlapping.
        const auto left = juce::roundToInt (x), right = juce::roundToInt (x + w);
        chips[i]->setBounds (left, area.getY(), right - left, area.getHeight());
        if (auto& button = chips[i]->toWheel)
            button->setBounds (chips[i]->getLocalBounds().reduced (3, 4).removeFromRight (26));
        x += w;
    }
}
