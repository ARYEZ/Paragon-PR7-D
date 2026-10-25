#include "TunerPanel.h"

#include "Icons.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    const juce::Colour inTune { 0xff3dff9a };   // the same green as the tuner on the unit's screen

    juce::String centsText (double cents)
    {
        const auto rounded = juce::roundToInt (cents);
        return (rounded > 0 ? "+" : "") + juce::String (rounded) + (std::abs (rounded) == 1 ? " cent" : " cents");
    }
}

TunerPanel::TunerPanel (ParagonX1AudioProcessor& processorToUse) : processor (processorToUse)
{
    resetButton.setTooltip ("Empty the trace and start the average again, e.g. before playing the passage you want to check");
    resetButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    resetButton.onClick = [this]
    {
        processor.resetTuner();
        repaint();
    };
    addAndMakeVisible (resetButton);
}

juce::Colour TunerPanel::colourForCents (double cents)
{
    const auto off = std::abs (cents);
    return off <= 5.0 ? inTune : off <= 15.0 ? ParagonColours::accent : ParagonColours::meterRed;
}

void TunerPanel::visibilityChanged()
{
    // Redraw only while it can be seen.
    if (isVisible())
        startTimerHz (30);
    else
        stopTimer();
}

void TunerPanel::resized()
{
    // Header: icon and title on the left, RESET on the right, clear of the
    // dock's GRAPHIC EQ button, which sits in this panel's top right corner.
    auto area = getLocalBounds().reduced (16, 10);
    auto header = area.removeFromTop (24);
    resetButton.setBounds (header.withTrimmedRight (126).removeFromRight (80));
    area.removeFromTop (8);

    auto body = area.toFloat();
    readingArea = body.removeFromLeft (150.0f);
    body.removeFromLeft (12.0f);
    averageArea = body.removeFromBottom (22.0f);
    body.removeFromBottom (4.0f);
    traceArea = body;
}

void TunerPanel::paint (juce::Graphics& g)
{
    // The dock's dark panel and a header, as on the other blocks' panels.
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, bounds.getY(),
                                             juce::Colour (0xff09070a), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
    Icons::draw (g, Icons::Type::tuner, header.removeFromLeft (26.0f).withSizeKeepingCentre (22.0f, 20.0f), inTune, 1.6f, true);
    header.removeFromLeft (8.0f);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("TUNER", header, juce::Justification::centredLeft, false);

    const auto referenceHz = processor.getTunerReferenceHz();
    const auto on = processor.getParameters().getRawParameterValue (ParamIDs::tunerOn)->load() > 0.5f;
    const auto& tuner = processor.getTuner();
    const auto& reading = tuner.getReading();

    // The current note: name and octave, cents, frequency, and the reference.
    {
        auto area = readingArea;
        auto noteLine = area.removeFromTop (64.0f);
        g.setColour (reading.hasNote ? colourForCents (reading.note.cents) : ParagonColours::textDim.withAlpha (0.4f));
        g.setFont (juce::FontOptions (56.0f, juce::Font::bold));
        const auto name = reading.hasNote ? reading.note.name : juce::String ("-");
        const auto nameWidth = (float) juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (56.0f, juce::Font::bold), name);
        g.drawText (name, noteLine.removeFromLeft (nameWidth + 4.0f), juce::Justification::centredLeft, false);
        if (reading.hasNote)
        {
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            g.drawText (juce::String (reading.note.octave), noteLine.withTrimmedTop (22.0f), juce::Justification::centredLeft, false);
        }

        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.setColour (reading.hasNote ? colourForCents (reading.note.cents) : ParagonColours::textDim);
        g.drawText (reading.hasNote ? centsText (reading.note.cents) : juce::String (on ? "No note" : "Tuner off"),
                    area.removeFromTop (22.0f), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (reading.hasNote ? juce::String (reading.frequency, 1) + " Hz" : juce::String(),
                    area.removeFromTop (20.0f), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("A4 = " + juce::String (referenceHz, juce::exactlyEqual (referenceHz, std::round (referenceHz)) ? 0 : 1) + " Hz",
                    area.removeFromBottom (18.0f), juce::Justification::centredLeft, false);
    }

    paintTrace (g, traceArea, referenceHz);

    // The average over the trace.
    {
        auto area = averageArea;
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (ParagonColours::textDim);
        g.drawText ("AVERAGE", area.removeFromLeft (70.0f), juce::Justification::centredLeft, false);
        if (const auto average = tuner.getAverage (referenceHz))
        {
            const auto cents = average->cents;
            const auto verdict = std::abs (cents) <= 5.0 ? juce::String ("in tune")   // the green of colourForCents
                                                         : cents > 0.0 ? juce::String ("sharp") : juce::String ("flat");
            g.setColour (colourForCents (cents));
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            const auto text = centsText (cents) + ", " + verdict;
            const auto width = (float) juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (15.0f, juce::Font::bold), text);
            g.drawText (text, area.removeFromLeft (width + 16.0f), juce::Justification::centredLeft, false);
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("over " + juce::String (average->seconds, 1) + " s of notes in the trace", area, juce::Justification::centredLeft, false);
        }
        else
        {
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Appears once a part has played", area, juce::Justification::centredLeft, false);
        }
    }
}

void TunerPanel::paintTrace (juce::Graphics& g, juce::Rectangle<float> area, double referenceHz) const
{
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    const auto& trace = processor.getTuner().getTrace();
    const auto plot = area.reduced (8.0f, 6.0f).withTrimmedLeft (30.0f).withTrimmedBottom (14.0f);

    // The notes, measured against the reference: line n is the nth note above A4's MIDI 0.
    const auto offset = 12.0 * std::log2 (referenceHz / 440.0);
    double lowest = 1000.0, highest = -1000.0;
    for (const auto& point : trace)
        if (point.hasNote())
        {
            lowest = juce::jmin (lowest, point.note - offset);
            highest = juce::jmax (highest, point.note - offset);
        }

    const auto hasNotes = highest >= lowest;
    // Fit the notes heard, with a note to spare above and below, and at least
    // a fifth (7 semitones) tall so a held note isn't blown up to fill it.
    auto bottom = hasNotes ? std::floor (lowest) - 1.0 : 45.0;
    auto top = hasNotes ? std::ceil (highest) + 1.0 : 52.0;
    if (const auto missing = 7.0 - (top - bottom); missing > 0.0)
    {
        bottom -= std::floor (missing / 2.0);
        top += std::ceil (missing / 2.0);
    }

    const auto yFor = [&] (double semitones)
    {
        return juce::jmap ((float) semitones, (float) bottom, (float) top, plot.getBottom(), plot.getY());
    };
    const auto newest = trace.empty() ? 0.0 : trace.back().seconds;
    const auto xFor = [&] (double seconds)
    {
        return plot.getRight() - (float) ((newest - seconds) / Tuner::traceSeconds) * plot.getWidth();
    };

    // A line for every note, named at the left (only the natural notes when they're packed close).
    const auto spacing = plot.getHeight() / (float) (top - bottom);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (auto n = (int) bottom; n <= (int) top; ++n)
    {
        const auto note = PitchDetector::toNote (440.0 * std::pow (2.0, (n - 69) / 12.0));
        const auto natural = ! note.name.containsChar ('#');
        const auto y = yFor (n);
        g.setColour (juce::Colours::white.withAlpha (natural ? 0.1f : 0.05f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        if (spacing >= 11.0f || natural)
        {
            g.setColour (ParagonColours::textDim.withAlpha (natural ? 1.0f : 0.6f));
            g.drawText (note.name + juce::String (note.octave), juce::Rectangle<float> (area.getX() + 6.0f, y - 6.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // Seconds ago, along the bottom.
    g.setColour (ParagonColours::textDim.withAlpha (0.7f));
    for (int ago = 30; ago >= 0; ago -= 10)
    {
        const auto x = plot.getRight() - (float) ago / (float) Tuner::traceSeconds * plot.getWidth();
        g.drawText (ago == 0 ? juce::String ("now") : "-" + juce::String (ago) + " s",
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f),
                    ago == 30 ? juce::Justification::centredLeft : ago == 0 ? juce::Justification::centredRight : juce::Justification::centred, false);
    }

    if (! hasNotes)
    {
        const auto on = processor.getParameters().getRawParameterValue (ParamIDs::tunerOn)->load() > 0.5f;
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (on ? "Play the recording: the pitch of a single part (a vocal, a bass line) draws here."
                             : "The tuner is off. Press stomp 1 (TUNER) to start listening.",
                          plot.reduced (20.0f, 0.0f).toNearestInt(), juce::Justification::centred, 2);
        return;
    }

    // The pitch: joined where it's continuous. The line keeps its real shape
    // (vibrato, slides), but its colour says where the note centres, over a
    // quarter of a second either side: coloured point by point, vibrato would
    // flicker through all three colours and hide whether the note is in tune.
    const auto sameNote = [&] (size_t i, size_t j)
    {
        return trace[j].hasNote() && std::abs (trace[j].note - trace[i].note) < 1.0 && std::abs (trace[j].seconds - trace[i].seconds) <= 0.25;
    };
    std::vector<double> centred (trace.size(), 0.0);
    for (size_t i = 0; i < trace.size(); ++i)
    {
        if (! trace[i].hasNote())
            continue;
        double x = 0.0, y = 0.0;   // round a circle, as Tuner::getAverage does
        const auto add = [&] (size_t j)
        {
            const auto angle = juce::MathConstants<double>::twoPi * Tuner::centsFromNearest (trace[j].note, referenceHz) / 100.0;
            x += std::cos (angle);
            y += std::sin (angle);
        };
        add (i);
        for (auto j = i; j > 0 && sameNote (i, j - 1); --j)
            add (j - 1);
        for (auto j = i + 1; j < trace.size() && sameNote (i, j); ++j)
            add (j);
        centred[i] = std::atan2 (y, x) / juce::MathConstants<double>::twoPi * 100.0;
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.expanded (0.0f, 2.0f).toNearestInt());
    for (size_t i = 1; i < trace.size(); ++i)
    {
        const auto& a = trace[i - 1];
        const auto& b = trace[i];
        if (! b.hasNote())
            continue;
        g.setColour (colourForCents (centred[i]));
        if (a.hasNote() && b.seconds - a.seconds < 0.1 && std::abs (b.note - a.note) < 1.0)
            g.drawLine (xFor (a.seconds), yFor (a.note - offset), xFor (b.seconds), yFor (b.note - offset), 2.0f);
        else
            g.fillEllipse (juce::Rectangle<float> (2.5f, 2.5f).withCentre ({ xFor (b.seconds), yFor (b.note - offset) }));
    }
}
