#include "Footswitch.h"

#include "Finish.h"
#include "ParagonLookAndFeel.h"

Footswitch::Footswitch (int numberToShow, const juce::String& nameToShow, juce::AudioProcessorValueTreeState& parametersToUse,
                        juce::StringArray onParameterIdsToUse)
    : number (numberToShow),
      name (nameToShow),
      parameters (parametersToUse),
      onParameterIds (std::move (onParameterIdsToUse))
{
    setTitle (name + " footswitch");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity (true);
    // Parameters can change from the DAW (automation) as well as from here,
    // so poll them rather than only reacting to our own clicks.
    startTimerHz (15);
}

bool Footswitch::isAnyOn() const
{
    for (const auto& id : onParameterIds)
        if (parameters.getRawParameterValue (id)->load() > 0.5f)
            return true;
    return false;
}

void Footswitch::toggle()
{
    const auto newValue = isAnyOn() ? 0.0f : 1.0f;
    for (const auto& id : onParameterIds)
    {
        auto* parameter = parameters.getParameter (id);
        // Begin/end tell the DAW this is one user action, for automation recording.
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (newValue);
        parameter->endChangeGesture();
    }
    refresh();
}

void Footswitch::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void Footswitch::refresh()
{
    const auto on = isAnyOn();
    const auto newSummary = describe ? describe() : juce::String();
    if (on != lastOn || newSummary != summary)
    {
        lastOn = on;
        summary = newSummary;
        // The settings show on hover; the RIG SUMMARY cards in the dock show them too.
        setTooltip (name + (summary.isNotEmpty() ? ":  " + summary : juce::String())
                    + (isPlaceholder() ? juce::String ("\nNot built yet: this stomp is still being planned.")
                                       : juce::String ("\nPress the pad to switch on or off; click the name to edit.")));
        repaint();
    }
}

void Footswitch::resized()
{
    // LED bar on top, the pad with its number to the left, the name underneath.
    auto area = getLocalBounds();
    ledArea = area.removeFromTop (14);
    area.removeFromTop (9);
    nameArea = area.removeFromBottom (22);
    area.removeFromBottom (5);
    switchArea = area.withTrimmedLeft (46).withTrimmedRight (10);
}

void Footswitch::paint (juce::Graphics& g)
{
    // Status LED: a short neon bar above the pad, as in the mockup, behind a
    // recessed lens: a dark slot with a chrome rim round it.
    {
        const auto bar = ledArea.toFloat().withSizeKeepingCentre (26.0f, 5.0f).withX ((float) switchArea.getCentreX() - 13.0f);
        const auto lens = bar.expanded (5.0f, 3.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a4a52), 0.0f, lens.getY(),
                                                 juce::Colour (0xfff3e9ee), 0.0f, lens.getBottom(), false));
        g.fillRoundedRectangle (lens, lens.getHeight() * 0.5f);
        g.setColour (juce::Colour (0xff140c10));
        g.fillRoundedRectangle (lens.reduced (1.2f), lens.getHeight() * 0.5f - 1.2f);
        ParagonLookAndFeel::drawNeonBar (g, bar, lastOn);
        // The lens itself: a faint reflection across its top.
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (lens.reduced (2.0f).withHeight (lens.getHeight() * 0.3f), 1.5f);
    }

    // The pad: a big piano key (black, or neon on the pearl black unit) standing in a raised chrome bezel, the
    // way a real floor unit's footswitch is mounted. The bezel casts a
    // shadow on the panel, is lit along its top and darker at the front,
    // and holds the key in a dark well.
    const auto pressed = isMouseButtonDown() && switchArea.contains (getMouseXYRelative());
    const auto hover = isMouseOver() && switchArea.contains (getMouseXYRelative());
    const auto bezel = switchArea.toFloat();
    ParagonLookAndFeel::drawChromeBezel (g, bezel, 11.0f);
    ParagonLookAndFeel::drawPianoKey (g, bezel.reduced (7.0f, 6.5f).withTrimmedBottom (1.0f), 6.0f, 6.0f, pressed, hover, Finish::neonKeys());

    // Number and name stamped into the metal: ink, with a fine light edge
    // underneath where the stamped letters' lower walls catch the light.
    const auto stamped = [&g] (const juce::String& text, juce::Rectangle<int> area, juce::Colour colour)
    {
        ParagonLookAndFeel::drawStampedText (g, text, area.toFloat(), juce::Justification::centred, colour);
    };
    g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
    stamped (juce::String (number), switchArea.withX (0).withWidth (switchArea.getX() - 8).withTrimmedLeft (switchArea.getX() - 34),
             Finish::ink());

    // Name under the pad; the selected block's is underlined in red.
    const auto nameBox = nameArea.withX (switchArea.getX()).withWidth (switchArea.getWidth());
    const auto font = juce::FontOptions (14.5f, juce::Font::bold);
    g.setFont (font);
    stamped (name, nameBox, selected ? ParagonColours::glow : Finish::ink());
    if (selected)
    {
        const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, name);
        g.fillRect (juce::Rectangle<float> (width, 2.0f).withCentre ({ (float) nameBox.getCentreX(), (float) nameBox.getBottom() - 1.0f }));
    }
}

void Footswitch::mouseUp (const juce::MouseEvent& event)
{
    if (switchArea.contains (event.getPosition()))
        toggle();
    else if (onSelect)
        onSelect();
    repaint();
}
