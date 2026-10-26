#include "OraclePage.h"

#include "DockPanels.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    constexpr double minimumLength = 0.02;   // seconds: the shortest a note can be dragged to

    juce::String noteName (int pitch)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((pitch % 12) + 12) % 12]) + juce::String (pitch / 12 - 1);
    }

    bool isBlackKey (int pitch)
    {
        const auto step = ((pitch % 12) + 12) % 12;
        return step == 1 || step == 3 || step == 6 || step == 8 || step == 10;
    }

    juce::String centsText (double cents)
    {
        const auto rounded = juce::roundToInt (cents);
        return (rounded > 0 ? "+" : "") + juce::String (rounded) + " cents";
    }

    juce::String verdict (double cents)
    {
        return std::abs (cents) <= 5.0 ? "in tune" : cents > 0.0 ? "sharp" : "flat";
    }

    void styleKnob (juce::Slider& knob)
    {
        knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    }

    void styleLabel (juce::Label& label)
    {
        label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setJustificationType (juce::Justification::centred);
    }

    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    }
}

//==============================================================================
OracleRoll::OracleRoll (ParagonX1AudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void OracleRoll::resized()
{
    auto area = getLocalBounds().toFloat();
    ruler = area.removeFromTop (22.0f);
    keys = area.removeFromLeft (46.0f).withTrimmedBottom (6.0f);
    plot = area.withTrimmedRight (6.0f).withTrimmedBottom (6.0f);
    ruler = ruler.withLeft (plot.getX()).withRight (plot.getRight());
}

float OracleRoll::xForSeconds (double seconds) const noexcept
{
    return plot.getX() + (float) ((seconds - viewStart) / viewSeconds) * plot.getWidth();
}

double OracleRoll::secondsForX (float x) const noexcept
{
    return viewStart + (double) ((x - plot.getX()) / plot.getWidth()) * viewSeconds;
}

float OracleRoll::yForPitch (double pitch) const noexcept
{
    return plot.getBottom() - (float) ((pitch - lowPitch + 0.5) / (highPitch - lowPitch + 1.0)) * plot.getHeight();
}

double OracleRoll::pitchForY (float y) const noexcept
{
    return lowPitch - 0.5 + (double) ((plot.getBottom() - y) / plot.getHeight()) * (highPitch - lowPitch + 1.0);
}

juce::Rectangle<float> OracleRoll::rectFor (const oracle::Note& note) const noexcept
{
    const auto row = plot.getHeight() / (float) (highPitch - lowPitch + 1.0);
    const auto left = xForSeconds (note.start), right = xForSeconds (note.end);
    return { left, yForPitch (note.pitch) - row * 0.5f + 1.0f, std::max (2.0f, right - left), std::max (2.0f, row - 2.0f) };
}

void OracleRoll::setView (double start, double seconds)
{
    viewSeconds = juce::jlimit (0.5, 1800.0, seconds);
    viewStart = start;
    following = false;
    repaint();
}

void OracleRoll::fitPitch()
{
    const auto& notes = processor.getOracle().getNotes();
    if (notes.empty())
    {
        lowPitch = 52.0;
        highPitch = 76.0;
        return;
    }
    auto low = 127, high = 0;
    for (const auto& n : notes)
    {
        low = std::min (low, n.pitch);
        high = std::max (high, n.pitch);
    }
    auto from = low - 3.0, to = high + 3.0;
    if (to - from < 18.0)
    {
        const auto middle = (from + to) * 0.5;
        from = middle - 9.0;
        to = middle + 9.0;
    }
    lowPitch = juce::jlimit (0.0, 110.0, from);
    highPitch = juce::jlimit (lowPitch + 8.0, 127.0, to);
}

void OracleRoll::fit()
{
    const auto& notes = processor.getOracle().getNotes();
    if (! notes.empty())
    {
        const auto first = notes.front().start;
        auto last = first;
        for (const auto& n : notes)
            last = std::max (last, n.end);
        setView (first - 0.5, std::max (2.0, last - first + 1.0));
    }
    pitchFitted = false;
    fitPitch();
    repaint();
}

void OracleRoll::selectAll()
{
    std::set<juce::uint32> all;
    for (const auto& n : processor.getOracle().getNotes())
        all.insert (n.id);
    setSelection (std::move (all));
}

void OracleRoll::setSelection (std::set<juce::uint32> newSelection)
{
    if (newSelection == selection)
        return;
    selection = std::move (newSelection);
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

void OracleRoll::timerCallback()
{
    auto& t = transcriber();
    const auto hearing = t.isHearing();
    if (hearing && ! wasHearing)
        following = true;   // a new take: follow it along
    wasHearing = hearing;

    if (hearing && following)
    {
        const auto now = t.getNowSeconds();
        if (now > viewStart + viewSeconds * 0.92 || now < viewStart)
            viewStart = now - viewSeconds * 0.25;
    }

    if (t.getVersion() != shownVersion)
    {
        shownVersion = t.getVersion();
        // Forget selected notes that have gone (found again, or undone).
        auto kept = selection;
        for (auto it = kept.begin(); it != kept.end();)
            it = t.find (*it) == nullptr ? kept.erase (it) : std::next (it);
        setSelection (std::move (kept));
        if (! pitchFitted && drag == Drag::none)
            fitPitch();
        repaint();
    }
    else if (hearing && isShowing())
    {
        repaint();
    }
}

const oracle::Note* OracleRoll::noteAt (juce::Point<float> position) const
{
    const oracle::Note* found = nullptr;
    for (const auto& n : processor.getOracle().getNotes())
        if (rectFor (n).expanded (2.0f, 0.0f).contains (position))
        {
            found = &n;
            if (selection.count (n.id) > 0)
                break;   // a selected one wins
        }
    return found;
}

OracleRoll::Drag OracleRoll::dragFor (const oracle::Note& note, juce::Point<float> position) const
{
    const auto r = rectFor (note);
    const auto grip = juce::jlimit (2.0f, 7.0f, r.getWidth() * 0.25f);
    if (position.x < r.getX() + grip)
        return Drag::start;
    if (position.x > r.getRight() - grip)
        return Drag::end;
    return Drag::pitch;
}

void OracleRoll::paint (juce::Graphics& g)
{
    const juce::ScopedLock scope (processor.getOracle().getLock());
    const auto& t = processor.getOracle();
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto row = plot.getHeight() / (float) (highPitch - lowPitch + 1.0);

    // The rows, darker for the black keys, and the keyboard down the side.
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (auto pitch = (int) std::floor (lowPitch); pitch <= (int) std::ceil (highPitch); ++pitch)
    {
        const auto y = yForPitch (pitch);
        const auto lane = juce::Rectangle<float> (plot.getX(), y - row * 0.5f, plot.getWidth(), row).getIntersection (plot);
        const auto key = juce::Rectangle<float> (keys.getX() + 4.0f, y - row * 0.5f, keys.getWidth() - 6.0f, row).getIntersection (keys);
        if (lane.isEmpty())
            continue;
        const auto black = isBlackKey (pitch);
        g.setColour (black ? juce::Colour (0xff060508) : juce::Colour (0xff0d0b0f));
        g.fillRect (lane);
        g.setColour (black ? juce::Colour (0xff141117) : juce::Colour (0xff2b272f));
        g.fillRect (black ? key.withTrimmedRight (key.getWidth() * 0.35f) : key);
        if (! black && ! isBlackKey (pitch - 1))   // between two white keys (E-F, B-C)
        {
            g.setColour (juce::Colour (0xff060508));
            g.drawHorizontalLine (juce::roundToInt (y + row * 0.5f), key.getX(), key.getRight());
        }
        else if (! black)
        {
            g.setColour (juce::Colour (0xff060508));
            g.drawHorizontalLine (juce::roundToInt (y + row * 0.5f), key.getRight() - key.getWidth() * 0.35f, key.getRight());
        }
        if (pitch % 12 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawHorizontalLine (juce::roundToInt (y + row * 0.5f), plot.getX(), plot.getRight());
            if (row >= 7.0f)
            {
                g.setColour (ParagonColours::textDim);
                g.drawText (noteName (pitch), key.withTrimmedLeft (2.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    // Bars and beats, from the song's tempo, with the bar numbers along the top.
    const auto& tempo = t.getTempo();
    {
        const auto& point = tempo.pointAt (viewStart);
        const auto barQuarters = point.numerator * 4.0 / point.denominator;
        const auto q0 = tempo.ppqAt (viewStart), q1 = tempo.ppqAt (viewStart + viewSeconds);
        const auto pixelsPerQuarter = plot.getWidth() / (float) std::max (1.0e-6, q1 - q0);
        const auto showBeats = pixelsPerQuarter >= 14.0f;
        auto everyBars = 1;
        while ((float) (barQuarters * everyBars) * pixelsPerQuarter < 46.0f && everyBars < 4096)
            everyBars *= 2;
        const auto step = showBeats ? 1.0 : barQuarters * everyBars;
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (auto q = std::ceil (q0 / step) * step; q <= q1; q += step)
        {
            const auto x = xForSeconds (tempo.secondsAt (q));
            const auto barIndex = q / barQuarters;
            const auto isBar = std::abs (barIndex - std::round (barIndex)) < 1.0e-6;
            g.setColour (juce::Colours::white.withAlpha (isBar ? 0.13f : 0.045f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            const auto bar = juce::roundToInt (barIndex);
            if (isBar && bar >= 0 && bar % everyBars == 0)
            {
                g.setColour (ParagonColours::textDim);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, ruler.getY() + 4.0f, 40.0f, 14.0f),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());

    // What was heard: its pitch as a thin line.
    {
        const auto& frames = t.getFrames();
        auto at = std::lower_bound (frames.begin(), frames.end(), viewStart - 0.1, [] (const oracle::Frame& f, double s) { return f.seconds < s; });
        juce::Path line;
        double last = -1.0e12;
        for (; at != frames.end() && at->seconds < viewStart + viewSeconds + 0.1; ++at)
        {
            if (! at->voiced())
            {
                last = -1.0e12;
                continue;
            }
            const juce::Point<float> point (xForSeconds (at->seconds), yForPitch (at->pitch));
            if (at->seconds - last < 0.03)
                line.lineTo (point);
            else
                line.startNewSubPath (point);
            last = at->seconds;
        }
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The notes, in the menus' red to pink to orange from low to high.
    const auto gradient = ParagonLookAndFeel::menuHighlight ({ 0.0f, 0.0f, 1.0f, 1.0f });
    g.setFont (juce::FontOptions (juce::jlimit (8.0f, 11.0f, row - 3.0f), juce::Font::bold));
    for (const auto& n : t.getNotes())
    {
        if (n.end < viewStart || n.start > viewStart + viewSeconds)
            continue;
        const auto r = rectFor (n);
        const auto chosen = selection.count (n.id) > 0;
        const auto colour = gradient.getColourAtPosition (juce::jlimit (0.0, 1.0, (n.pitch - lowPitch) / std::max (1.0, highPitch - lowPitch)));
        g.setColour (colour.withAlpha (chosen ? 0.95f : 0.62f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (chosen ? juce::Colours::white : colour.brighter (0.3f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, chosen ? 1.6f : 1.0f);
        if (r.getWidth() > 30.0f && row >= 10.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.drawText (noteName (n.pitch), r.withTrimmedLeft (4.0f), juce::Justification::centredLeft, false);
        }
    }

    // Where the song is, while it's being heard.
    if (t.isHearing())
    {
        const auto x = xForSeconds (t.getNowSeconds());
        g.setColour (ParagonColours::accent.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }

    if (drag == Drag::select)
    {
        const juce::Rectangle<float> band (dragFrom, dragTo);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (band);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawRect (band, 1.0f);
    }

    if (t.getNotes().empty() && t.getFrames().empty())
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Switch ORACLE on and play the part in your DAW (one voice or instrument at a time):\n"
                          "its notes appear here as it plays. Then fix any here, and drag the MIDI onto a track.",
                          plot.reduced (40.0f).toNearestInt(), juce::Justification::centred, 3);
    }
}

void OracleRoll::mouseMove (const juce::MouseEvent& event)
{
    if (const auto* n = noteAt (event.position))
        setMouseCursor (event.mods.isAltDown() ? juce::MouseCursor::DraggingHandCursor
                        : dragFor (*n, event.position) == Drag::pitch ? juce::MouseCursor::UpDownResizeCursor
                                                                      : juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void OracleRoll::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    drag = Drag::none;
    edited = false;
    dragFrom = dragTo = event.position;
    if (! plot.contains (event.position))
        return;

    const auto adding = event.mods.isShiftDown() || event.mods.isCommandDown();
    if (const auto* n = noteAt (event.position))
    {
        if (event.mods.isPopupMenu())
        {
            if (selection.count (n->id) == 0)
                setSelection ({ n->id });
            showMenu (*n, event.position);
            return;
        }
        if (adding)
        {
            auto changed = selection;
            if (changed.count (n->id) > 0)
            {
                changed.erase (n->id);
                setSelection (std::move (changed));
                return;
            }
            changed.insert (n->id);
            setSelection (std::move (changed));
        }
        else if (selection.count (n->id) == 0)
        {
            setSelection ({ n->id });
        }
        grabbed = n->id;
        drag = event.mods.isAltDown() ? Drag::move : dragFor (*n, event.position);
        beforeDrag = transcriber().getNotes();
        return;
    }

    if (event.mods.isPopupMenu())
        return;
    if (! adding)
        setSelection ({});
    beforeDrag.clear();
    drag = Drag::select;
}

void OracleRoll::mouseDrag (const juce::MouseEvent& event)
{
    dragTo = event.position;
    if (drag == Drag::select)
    {
        const juce::Rectangle<float> band (dragFrom, dragTo);
        auto chosen = event.mods.isShiftDown() || event.mods.isCommandDown() ? selection : std::set<juce::uint32> {};
        for (const auto& n : transcriber().getNotes())
            if (rectFor (n).intersects (band))
                chosen.insert (n.id);
        setSelection (std::move (chosen));
        repaint();
        return;
    }
    if (drag == Drag::none)
        return;

    const auto row = plot.getHeight() / (float) (highPitch - lowPitch + 1.0);
    const auto seconds = (double) ((dragTo.x - dragFrom.x) / plot.getWidth()) * viewSeconds;
    const auto semitones = juce::roundToInt ((dragFrom.y - dragTo.y) / row);
    if (! edited && std::abs (dragTo.x - dragFrom.x) < 3.0f && semitones == 0)
        return;   // not a drag yet

    auto notes = beforeDrag;
    for (auto& n : notes)
    {
        const auto chosen = selection.count (n.id) > 0;
        if (drag == Drag::pitch && chosen)
            n.pitch = juce::jlimit (0, 127, n.pitch + semitones);
        else if (drag == Drag::move && chosen)
        {
            n.start += seconds;
            n.end += seconds;
        }
        else if (drag == Drag::start && n.id == grabbed)
            n.start = std::min (n.start + seconds, n.end - minimumLength);
        else if (drag == Drag::end && n.id == grabbed)
            n.end = std::max (n.end + seconds, n.start + minimumLength);
    }
    if (! edited)
    {
        transcriber().beginEdit();   // the whole drag is one undo step
        edited = true;
    }
    transcriber().setNotes (std::move (notes), false);
}

void OracleRoll::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    beforeDrag.clear();
    repaint();
}

void OracleRoll::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! plot.contains (event.position) || event.mods.isPopupMenu())
        return;
    const auto seconds = secondsForX (event.position.x);
    if (const auto* n = noteAt (event.position))
    {
        if (const auto right = transcriber().split (n->id, seconds))
            setSelection ({ right });
        return;
    }

    // A new note here: a beat long, or up to the next note.
    auto end = seconds + 60.0 / transcriber().getTempo().pointAt (seconds).bpm;
    for (const auto& n : transcriber().getNotes())
        if (n.start > seconds)
        {
            end = std::min (end, n.start);
            break;
        }
    if (end - seconds >= minimumLength)
        if (const auto id = transcriber().add (seconds, end, juce::jlimit (0, 127, juce::roundToInt (pitchForY (event.position.y)))))
            setSelection ({ id });
}

void OracleRoll::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto amount = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
    if (event.mods.isCommandDown())
    {
        // Zoom in time round the mouse.
        const auto factor = std::pow (0.75, amount * 4.0);
        const auto at = secondsForX (event.position.x);
        const auto seconds = juce::jlimit (0.5, 1800.0, viewSeconds * factor);
        setView (at - (at - viewStart) * seconds / viewSeconds, seconds);
    }
    else if (event.mods.isAltDown())
    {
        // Zoom in pitch round the mouse.
        const auto at = pitchForY (event.position.y);
        const auto span = juce::jlimit (8.0, 100.0, (highPitch - lowPitch) * std::pow (0.8, amount * 4.0));
        const auto below = (at - lowPitch) / std::max (1.0, highPitch - lowPitch);
        lowPitch = juce::jlimit (0.0, 127.0 - span, at - span * below);
        highPitch = lowPitch + span;
        pitchFitted = true;
        repaint();
    }
    else if (event.mods.isShiftDown())
    {
        const auto span = highPitch - lowPitch;
        lowPitch = juce::jlimit (0.0, 127.0 - span, lowPitch + amount * 8.0);
        highPitch = lowPitch + span;
        pitchFitted = true;
        repaint();
    }
    else
    {
        setView (viewStart - amount * viewSeconds * 0.3, viewSeconds);
    }
}

bool OracleRoll::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
    const auto command = key.getModifiers().isCommandDown();
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        transcriber().remove (selected());
        setSelection ({});
        return true;
    }
    if (command && letter == 'Z')
    {
        key.getModifiers().isShiftDown() ? transcriber().redo() : transcriber().undo();
        return true;
    }
    if (command && letter == 'Y')
    {
        transcriber().redo();
        return true;
    }
    if (command && letter == 'A')
    {
        selectAll();
        return true;
    }
    if (! command && letter == 'J')
    {
        transcriber().join (selected());
        return true;
    }
    if (! command && letter == 'F')
    {
        fit();
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        if (! selection.empty())
            transcriber().transpose (selected(), (code == juce::KeyPress::upKey ? 1 : -1) * (key.getModifiers().isShiftDown() ? 12 : 1));
        return true;
    }
    return false;
}

void OracleRoll::showMenu (const oracle::Note& note, juce::Point<float> position)
{
    const auto id = note.id;
    const auto seconds = secondsForX (position.x);
    juce::PopupMenu menu;
    menu.addSectionHeader (noteName (note.pitch));
    menu.addItem (1, "Split here");
    menu.addItem (2, selection.size() > 1 ? "Join the selected notes" : "Join with the next note");
    menu.addItem (3, "Octave up");
    menu.addItem (4, "Octave down");
    menu.addSeparator();
    menu.addItem (5, selection.size() > 1 ? "Delete the selected notes" : "Delete");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, id, seconds] (int chosen)
    {
        if (chosen == 1)
        {
            if (const auto right = transcriber().split (id, seconds))
                setSelection ({ right });
        }
        else if (chosen == 2)
            transcriber().join (selected());
        else if (chosen == 3 || chosen == 4)
            transcriber().transpose (selected(), chosen == 3 ? 12 : -12);
        else if (chosen == 5)
        {
            transcriber().remove (selected());
            setSelection ({});
        }
    });
}

//==============================================================================
MidiDragTile::MidiDragTile (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Drag onto a MIDI track in your DAW, at bar 1, to line up with the song (the file starts at bar 1). "
                "Click to show the file in its folder instead");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

juce::File MidiDragTile::write() const
{
    return processor.getOracle().writeMidiFile (ParagonX1AudioProcessor::getOracleMidiFolder());
}

void MidiDragTile::paint (juce::Graphics& g)
{
    const auto any = ! processor.getOracle().getNotes().empty();
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (bounds));
    g.setOpacity (any ? 1.0f : 0.25f);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setOpacity (1.0f);
    g.setColour (juce::Colours::white.withAlpha (any ? 1.0f : 0.5f));
    auto text = bounds.reduced (10.0f, 6.0f);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("DRAG MIDI", text.removeFromTop (text.getHeight() * 0.58f), juce::Justification::centredBottom, false);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (any ? "onto a track, at bar 1" : "no notes yet", text, juce::Justification::centredTop, false);
}

void MidiDragTile::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void MidiDragTile::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging || event.getDistanceFromDragStart() < 6 || processor.getOracle().getNotes().empty())
        return;
    dragging = true;
    if (const auto file = write(); file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void MidiDragTile::mouseUp (const juce::MouseEvent& event)
{
    if (! dragging && getLocalBounds().contains (event.getPosition()) && ! processor.getOracle().getNotes().empty())
        if (const auto file = write(); file.existsAsFile())
            file.revealToUser();
    dragging = false;
}

//==============================================================================
OraclePage::OraclePage (ParagonX1AudioProcessor& p)
    : processor (p),
      roll (p),
      midi (p),
      onAttachment (p.getParameters(), ParamIDs::tunerOn, onButton)
{
    auto& oracle = p.getOracle();
    addAndMakeVisible (roll);
    addAndMakeVisible (midi);
    roll.onSelectionChanged = [this] { repaint (bottomBar); };

    onButton.setTooltip ("Switch ORACLE on or off (the same as stomp 1). On, it listens whenever the DAW plays; "
                         "playing a passage again replaces what it heard there");

    for (auto* knob : { &sensitivity, &minNote })
        styleKnob (*knob);
    sensitivity.setRange (0.0, 100.0, 1.0);
    sensitivity.setDoubleClickReturnValue (true, 50.0);
    sensitivity.setTooltip ("How readily it splits the part into notes: higher catches quick notes and small steps, "
                            "lower keeps slides and wide vibrato as one note. Changing it finds every note again (one undo)");
    minNote.setRange (20.0, 300.0, 1.0);
    minNote.setTextValueSuffix (" ms");
    minNote.setDoubleClickReturnValue (true, 80.0);
    minNote.setTooltip ("The shortest note it keeps: anything shorter joins a neighbour or goes. Changing it finds every note again (one undo)");
    sensitivity.setValue (oracle.getSettings().sensitivity, juce::dontSendNotification);
    minNote.setValue (oracle.getSettings().minNoteMs, juce::dontSendNotification);
    for (auto* knob : { &sensitivity, &minNote })
    {
        knob->onValueChange = [this, knob] { if (! knob->isMouseButtonDown()) applySettings(); };
        knob->onDragEnd = [this] { applySettings(); };
        addAndMakeVisible (*knob);
    }

    quantize.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff09070a));
    quantize.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    quantize.setColour (juce::ComboBox::textColourId, ParagonColours::text);
    quantize.setColour (juce::ComboBox::arrowColourId, ParagonColours::text);
    quantize.addItemList (oracle::quantizeNames, 1);
    quantize.setSelectedItemIndex (oracle.getQuantize(), juce::dontSendNotification);
    quantize.setTooltip ("Snaps the MIDI file's notes to the grid (T: triplets). The notes here stay as they were sung");
    quantize.onChange = [this] { processor.getOracle().setQuantize (quantize.getSelectedItemIndex()); };
    addAndMakeVisible (quantize);

    for (auto* label : { &sensitivityLabel, &minNoteLabel, &quantizeLabel })
    {
        styleLabel (*label);
        addAndMakeVisible (*label);
    }
    for (auto* button : { &undo, &redo, &clear, &fit, &back })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }
    undo.setTooltip ("Undo the last change to the notes (ORACLE's own undo, Ctrl+Z on the notes)");
    redo.setTooltip ("Redo (Ctrl+Shift+Z on the notes)");
    clear.setTooltip ("Forget everything heard and its notes, to start again (UNDO brings the notes back)");
    fit.setTooltip ("Show all the notes (F on the notes)");
    undo.onClick = [this] { processor.getOracle().undo(); };
    redo.onClick = [this] { processor.getOracle().redo(); };
    clear.onClick = [this] { processor.getOracle().clear(); };
    fit.onClick = [this] { roll.fit(); };
    back.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (onButton);

    startTimerHz (10);
    timerCallback();
}

void OraclePage::applySettings()
{
    auto& oracle = processor.getOracle();
    oracle::Settings settings { (float) sensitivity.getValue(), (float) minNote.getValue() };
    if (! juce::exactlyEqual (settings.sensitivity, oracle.getSettings().sensitivity)
        || ! juce::exactlyEqual (settings.minNoteMs, oracle.getSettings().minNoteMs))
        oracle.setSettings (settings);
}

void OraclePage::timerCallback()
{
    auto& oracle = processor.getOracle();
    undo.setEnabled (oracle.canUndo());
    redo.setEnabled (oracle.canRedo());
    if (oracle.getVersion() != shownVersion)
    {
        shownVersion = oracle.getVersion();
        if (! sensitivity.isMouseButtonDown())
            sensitivity.setValue (oracle.getSettings().sensitivity, juce::dontSendNotification);
        if (! minNote.isMouseButtonDown())
            minNote.setValue (oracle.getSettings().minNoteMs, juce::dontSendNotification);
        quantize.setSelectedItemIndex (oracle.getQuantize(), juce::dontSendNotification);
        midi.repaint();
        repaint (bottomBar);
    }
    repaint (statusArea);
    repaint (nowArea);
}

void OraclePage::resized()
{
    auto area = getLocalBounds().reduced (12);
    topBar = area.removeFromTop (92);
    area.removeFromTop (8);
    bottomBar = area.removeFromBottom (48);
    area.removeFromBottom (8);
    roll.setBounds (area);

    auto bar = topBar.reduced (14, 8);
    bar.removeFromLeft (150);   // the name, drawn
    onButton.setBounds (bar.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    bar.removeFromLeft (10);
    statusArea = bar.removeFromLeft (160);
    nowArea = bar.removeFromLeft (108);
    bar.removeFromLeft (10);

    const auto place = [&] (juce::Label& label, juce::Component& component, int width, bool knob)
    {
        auto column = bar.removeFromLeft (width);
        label.setBounds (column.removeFromTop (14));
        component.setBounds (knob ? column : column.withSizeKeepingCentre (width - 10, 28));
        bar.removeFromLeft (8);
    };
    place (sensitivityLabel, sensitivity, 92, true);
    place (minNoteLabel, minNote, 92, true);
    place (quantizeLabel, quantize, 110, false);

    back.setBounds (bar.removeFromRight (140).withSizeKeepingCentre (140, 30));
    bar.removeFromRight (16);
    midi.setBounds (bar.removeFromRight (170).reduced (0, 4));
    bar.removeFromRight (16);
    auto buttons = bar.removeFromRight (152).withSizeKeepingCentre (152, 62);
    auto top = buttons.removeFromTop (28), bottom = buttons.withTrimmedTop (6);
    undo.setBounds (top.removeFromLeft (72));
    redo.setBounds (top.withTrimmedLeft (8));
    clear.setBounds (bottom.removeFromLeft (72));
    fit.setBounds (bottom.withTrimmedLeft (8));
}

void OraclePage::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    for (auto area : { topBar, bottomBar })
        DockStyle::drawPanel (g, area.toFloat());

    // The name in the red to pink to orange gradient, across its letters.
    auto title = topBar.reduced (14, 8).removeFromLeft (150);
    const auto titleFont = juce::FontOptions (19.0f, juce::Font::bold);
    const auto titleArea = title.removeFromTop (title.getHeight() / 2 + 6);
    const auto titleWidth = (float) juce::GlyphArrangement::getStringWidthInt (titleFont, "ORACLE");
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (titleArea.toFloat().withWidth (titleWidth)));
    g.setFont (titleFont);
    g.drawText ("ORACLE", titleArea, juce::Justification::bottomLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Melody to MIDI", title, juce::Justification::topLeft, false);

    // What it's doing.
    auto& oracle = processor.getOracle();
    const auto on = processor.getParameters().getRawParameterValue (ParamIDs::tunerOn)->load() > 0.5f;
    {
        auto status = statusArea.toFloat().reduced (0.0f, 22.0f);
        const auto hearing = on && oracle.isHearing();
        const auto dot = status.removeFromLeft (16.0f).withSizeKeepingCentre (9.0f, 9.0f);
        g.setColour (hearing ? ParagonColours::meterRed : on ? ParagonColours::accent.withAlpha (0.5f) : ParagonColours::panelEdge);
        g.fillEllipse (dot);
        status.removeFromLeft (4.0f);
        g.setColour (hearing ? ParagonColours::text : ParagonColours::textDim);
        g.setFont (juce::FontOptions (hearing ? 13.0f : 11.5f, hearing ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (hearing ? juce::String ("LISTENING") : on ? juce::String ("Press play in your DAW") : juce::String ("Off: switch ON to listen"),
                          status.toNearestInt(), juce::Justification::centredLeft, 2);
    }

    // The note now (the tuner's reading).
    {
        const auto& reading = processor.getTuner().getReading();
        auto now = nowArea.toFloat().reduced (0.0f, 14.0f);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("NOW", now.removeFromTop (14.0f), juce::Justification::centredLeft, false);
        if (on && reading.hasNote)
        {
            g.setColour (ParagonColours::accent);
            g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
            const auto name = reading.note.name + juce::String (reading.note.octave);
            g.drawText (name, now.removeFromLeft (58.0f), juce::Justification::centredLeft, false);
            g.setColour (ParagonColours::text);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (centsText (reading.note.cents), now, juce::Justification::centredLeft, false);
        }
        else
        {
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText ("-", now, juce::Justification::centredLeft, false);
        }
    }

    // The selected notes, or all of them: where, how long, and how in tune they were sung.
    {
        const juce::ScopedLock scope (oracle.getLock());
        const auto& notes = oracle.getNotes();
        const auto& chosen = roll.getSelection();
        std::vector<const oracle::Note*> shown;
        for (const auto& n : notes)
            if (chosen.empty() || chosen.count (n.id) > 0)
                shown.push_back (&n);

        double weighted = 0.0, total = 0.0;
        for (const auto* n : shown)
        {
            weighted += n->cents * n->length();
            total += n->length();
        }
        juce::String text;
        if (shown.empty())
            text = "No notes yet.";
        else if (chosen.size() == 1)
        {
            const auto& n = *shown.front();
            const auto& tempo = oracle.getTempo();
            const auto& point = tempo.pointAt (n.start);
            const auto barQuarters = point.numerator * 4.0 / point.denominator;
            const auto q = tempo.ppqAt (n.start);
            const auto bar = std::floor (q / barQuarters);
            const auto beat = (q - bar * barQuarters) * point.denominator / 4.0 + 1.0;
            text << noteName (n.pitch) << "   bar " << (int) bar + 1 << ", beat " << juce::String (beat, 2)
                 << "   " << juce::String (tempo.ppqAt (n.end) - q, 2) << " beats   velocity " << n.velocity
                 << "   sung " << centsText (n.cents) << " (" << verdict (n.cents) << ")";
        }
        else
        {
            const auto average = total > 0.0 ? weighted / total : 0.0;
            text << (int) shown.size() << (chosen.empty() ? " notes" : " notes selected") << "   sung on average "
                 << centsText (average) << " (" << verdict (average) << ")";
        }
        auto strip = bottomBar.reduced (16, 0);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (text, strip.removeFromLeft (strip.getWidth() / 2), juce::Justification::centredLeft, true);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Drag a note: pitch.  Its ends: start, length.  Alt-drag: move.  Double-click: split (or add).  "
                          "J: join.  Delete.  Wheel: scroll, Ctrl+wheel: zoom.",
                          strip, juce::Justification::centredRight, 2);
    }
}
