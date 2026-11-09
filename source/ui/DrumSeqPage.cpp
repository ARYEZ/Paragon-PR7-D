#include "DrumSeqPage.h"
#include "NoteNames.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

#include <cmath>
#include <numeric>

namespace
{
    using synth::drumseq::Note;
    using synth::drumseq::Pattern;
    const juce::Colour amber { 0xffffc23a };

    juce::RangedAudioParameter* parameter (ParagonX1AudioProcessor& p, const char* id) { return p.getParameters().getParameter (id); }
}

//==============================================================================
DrumSeqGrid::DrumSeqGrid (ParagonX1AudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    setTooltip ("The beat: a row for each drum of the kit (beside it). PEN: click to add a note, drag along to add more, drag notes to "
                "move them (ALT: off the grid), right-click to remove. SELECT: drag a frame round notes. SHIFT adds to the choice; "
                "DELETE removes the chosen notes; their settings are above.");
    refreshShown();
    startTimerHz (30);
}

int DrumSeqGrid::getPattern() const
{
    return processor.getDrumSeqSettings().pattern;
}

double DrumSeqGrid::getLengthSteps() const { return processor.getDrumSeqSettings().lengthSteps(); }

void DrumSeqGrid::refreshShown()
{
    if (drag != Drag::none)
        return;
    const auto pattern = getPattern();
    const auto& now = processor.getModMatrix().drumPatterns[(size_t) pattern];
    const auto other = pattern != shownPattern;
    if (other || now.notes.size() != shown.notes.size())
        selected.assign (now.notes.size(), false);   // another pattern, or changed elsewhere (undo, a preset)
    shownPattern = pattern;
    shown = now;
    if (other && onSelectionChanged)
        onSelectionChanged();
}

void DrumSeqGrid::setShown (const Pattern& p, bool finished)
{
    auto matrix = processor.getModMatrix();
    auto& to = matrix.drumPatterns[(size_t) getPattern()];
    const auto fx = to.fx;   // the grid edits notes; THE STEP FX LANE (0.73.0) edits its own part
    to = p;
    to.fx = fx;
    processor.setModMatrix (matrix);
    shown = to;
    selected.resize (shown.notes.size(), false);
    if (finished)
        processor.modMatrixEditFinished();
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

std::vector<int> DrumSeqGrid::getSelection() const
{
    std::vector<int> result;
    for (size_t i = 0; i < selected.size(); ++i)
        if (selected[i])
            result.push_back ((int) i);
    return result;
}

void DrumSeqGrid::setSelection (const std::vector<int>& indices)
{
    refreshShown();
    selected.assign (shown.notes.size(), false);
    for (const auto i : indices)
        if (i >= 0 && i < (int) selected.size())
            selected[(size_t) i] = true;
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

void DrumSeqGrid::selectAll()
{
    selected.assign (shown.notes.size(), true);
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

void DrumSeqGrid::deleteSelected()
{
    Pattern kept;
    for (size_t i = 0; i < shown.notes.size(); ++i)
        if (! selected[i])
            kept.notes.push_back (shown.notes[i]);
    if (kept.notes.size() == shown.notes.size())
        return;
    selected.assign (kept.notes.size(), false);
    setShown (kept, true);
}

void DrumSeqGrid::setHighlightedRow (int row)
{
    if (row != highlightedRow)
    {
        highlightedRow = row;
        repaint();
    }
}

juce::Rectangle<float> DrumSeqGrid::rowArea (int row) const
{
    const auto h = (float) getHeight() / (float) synth::drumseq::numRows;
    return juce::Rectangle<float> (0.0f, h * (float) row, (float) getWidth(), h).reduced (0.0f, juce::jmin (2.0f, h * 0.12f));   // as the kit's rows
}

juce::Rectangle<float> DrumSeqGrid::cell (int row, double step) const
{
    const auto stepWidth = (float) getWidth() / (float) getLengthSteps();
    // (0.71.0: the inset in step with the row: at 42 rows a fixed 4 px left the notes no height at all)
    const auto r = rowArea (row);
    return r.reduced (0.0f, juce::jmin (4.0f, r.getHeight() * 0.22f)).withX ((float) step * stepWidth).withWidth (stepWidth).reduced (juce::jmin (1.5f, stepWidth * 0.1f), 0.0f);
}

double DrumSeqGrid::stepAt (float x, bool snap) const
{
    const auto length = getLengthSteps();
    const auto s = juce::jlimit (0.0, length - 1.0e-3, (double) x / getWidth() * length);
    return snap ? std::floor (s) : std::round (s * 16.0) / 16.0;   // off the grid: in 16ths of a step
}

int DrumSeqGrid::rowAt (float y) const
{
    return juce::jlimit (0, synth::drumseq::numRows - 1, (int) std::floor (y / ((float) getHeight() / (float) synth::drumseq::numRows)));
}

int DrumSeqGrid::noteAt (juce::Point<float> at) const
{
    const auto length = getLengthSteps();
    for (auto i = (int) shown.notes.size(); --i >= 0;)
    {
        const auto& n = shown.notes[(size_t) i];
        if (n.start < length && cell (n.row, n.start).expanded (1.0f, 2.0f).contains (at))
            return i;
    }
    return -1;
}

int DrumSeqGrid::noteIn (int row, double step) const
{
    for (size_t i = 0; i < shown.notes.size(); ++i)
        if (shown.notes[i].row == row && std::floor (shown.notes[i].start + 1.0e-9) == step)
            return (int) i;
    return -1;
}

float DrumSeqGrid::playheadX() const
{
    const auto position = processor.getDrumKit().getSequencerPosition();
    if (position < 0.0 || processor.getDrumKit().getSequencerPattern() != getPattern())
        return -1.0f;
    return (float) (position / getLengthSteps() * getWidth());
}

void DrumSeqGrid::timerCallback()
{
    if (! isShowing())
        return;
    const auto before = shown;
    const auto pattern = shownPattern;
    refreshShown();
    const auto x = playheadX();
    if (! (before == shown) || pattern != shownPattern)
    {
        lastPlayhead = x;
        repaint();
    }
    else if (std::abs (x - lastPlayhead) > 0.5f)
    {
        // 0.40.0: the playhead moved: only its old and new places are drawn again (the whole grid redrew 30 times a
        // second while a beat played).
        // (Its line, and its step's shaded column when the step changes.)
        const auto stepWidth = (float) getWidth() / (float) getLengthSteps();
        const auto strip = [this] (float at) { return juce::Rectangle<int> ((int) std::floor (at) - 3, 0, 7, getHeight()); };
        const auto column = [this, stepWidth] (float at)
        {
            const auto step = std::floor (at / stepWidth);
            return juce::Rectangle<int> ((int) std::floor (step * stepWidth) - 1, 0, (int) std::ceil (stepWidth) + 3, getHeight());
        };
        const auto stepOf = [stepWidth] (float at) { return at < 0.0f ? -1 : (int) std::floor (at / stepWidth); };
        for (const auto at : { lastPlayhead, x })
            if (at >= 0.0f)
                repaint (stepOf (lastPlayhead) != stepOf (x) ? column (at) : strip (at));
        lastPlayhead = x;
    }
}

void DrumSeqGrid::paint (juce::Graphics& g)
{
    refreshShown();
    const auto length = getLengthSteps();
    const auto stepWidth = (float) getWidth() / (float) length;
    const auto stepsPerBeat = 1.0 / processor.getDrumSeqSettings().beatsPerStep();

    // The rows, as the kit's: a dark band each, the chosen drum's lighter.
    for (int r = 0; r < synth::drumseq::numRows; ++r)
    {
        const auto area = rowArea (r);
        g.setColour (r == highlightedRow ? juce::Colour (0xff1c1418) : juce::Colour (0xff100c10));
        g.fillRoundedRectangle (area, 4.0f);
        // Every second beat a shade lighter, so beats read at a glance.
        for (int beat = 1; beat * stepsPerBeat < length; beat += 2)
        {
            const auto x0 = (float) (beat * stepsPerBeat) * stepWidth;
            const auto x1 = juce::jmin ((float) getWidth(), (float) ((beat + 1) * stepsPerBeat) * stepWidth);
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.fillRect (juce::Rectangle<float> (x0, area.getY(), x1 - x0, area.getHeight()));
        }
        g.setColour (ParagonColours::panelEdge.withAlpha (r == highlightedRow ? 1.0f : 0.6f));
        g.drawRoundedRectangle (area, 4.0f, 1.0f);
    }

    // Steps: a line each (when there's room), brighter each beat, brightest each bar (4 beats), numbered.
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    for (int k = 1; k < (int) std::ceil (length); ++k)
    {
        const auto beat = std::fmod ((double) k, stepsPerBeat) < 1.0e-6;
        const auto bar = beat && std::fmod (k / stepsPerBeat, 4.0) < 1.0e-6;
        if (! beat && stepWidth < 5.0f)
            continue;
        const auto x = juce::roundToInt ((float) k * stepWidth);
        g.setColour (bar ? ParagonColours::textDim.withAlpha (0.55f) : ParagonColours::panelEdge.withAlpha (beat ? 0.95f : 0.35f));
        g.drawVerticalLine (x, rowArea (0).getY(), rowArea (synth::drumseq::numRows - 1).getBottom());
    }
    for (int bar = 0; bar * 4 * stepsPerBeat < length; ++bar)
    {
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String (bar + 1), juce::Rectangle<float> ((float) (bar * 4 * stepsPerBeat) * stepWidth + 3.0f, rowArea (0).getY() + 1.0f, 20.0f, 10.0f),
                    juce::Justification::centredLeft, false);
    }

    // The notes: filled by velocity; rolls as ticks; a reversed one with a backwards arrow; a short GATE as a line
    // under it as long as it sounds; a pitch written on it.
    for (size_t i = 0; i < shown.notes.size(); ++i)
    {
        const auto& n = shown.notes[i];
        if (n.start >= length)
            continue;
        const auto rect = cell (n.row, n.start);
        // 0.67.0: by how hard it's hit, in the family's three colours (as PR7-S's notes); REVERSE's amber kept.
        const auto colour = n.reverse != synth::drumseq::Reverse::off ? amber : ParagonColours::velocityColour (n.velocity);
        g.setColour (colour.withAlpha (0.25f + 0.7f * n.velocity));
        g.fillRoundedRectangle (rect, 2.5f);
        g.setColour (selected[i] ? ParagonColours::text : colour);
        g.drawRoundedRectangle (rect, 2.5f, selected[i] ? 2.0f : 1.0f);
        if (const auto count = n.rollCount(); count > 1)
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            for (int r = 1; r < count; ++r)
                g.drawVerticalLine (juce::roundToInt (rect.getX() + rect.getWidth() * (float) r / (float) count), rect.getY() + 2.0f, rect.getBottom() - 2.0f);
        }
        if (n.reverse != synth::drumseq::Reverse::off && rect.getWidth() >= 7.0f)
        {
            juce::Path arrow;
            const auto c = rect.getCentre();
            arrow.addTriangle (c.x - 3.0f, c.y, c.x + 2.5f, c.y - 3.5f, c.x + 2.5f, c.y + 3.5f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillPath (arrow);
        }
        else if (! juce::exactlyEqual (n.pitch, 0.0f) && rect.getWidth() >= 16.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ((n.pitch > 0.0f ? "+" : "") + juce::String (juce::roundToInt (n.pitch)), rect, juce::Justification::centred, false);
        }
        if (n.gate < 1.0f)
        {
            const auto steps = n.gate * stepsPerBeat;
            g.setColour (colour.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (rect.getX(), rect.getBottom() + 1.0f, juce::jmax (2.0f, (float) steps * stepWidth), 1.5f));
        }
    }

    if (drag == Drag::frame)
    {
        g.setColour (ParagonColours::text.withAlpha (0.12f));
        g.fillRect (frame);
        g.setColour (ParagonColours::text.withAlpha (0.7f));
        g.drawRect (frame, 1.0f);
    }

    // Where the beat is playing.
    if (const auto x = playheadX(); x >= 0.0f)
    {
        g.setColour (ParagonColours::text.withAlpha (0.08f));
        const auto step = std::floor (x / stepWidth);
        g.fillRect (juce::Rectangle<float> (step * stepWidth, rowArea (0).getY(), stepWidth, rowArea (synth::drumseq::numRows - 1).getBottom() - rowArea (0).getY()));
        g.setColour (ParagonColours::text.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, rowArea (0).getY(), 1.5f, rowArea (synth::drumseq::numRows - 1).getBottom() - rowArea (0).getY()));
    }

    // A groove (or a file) held over the grid: an orange frame and what dropping it would do.
    if (dropText.isNotEmpty())
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        ParagonColours::setFill (g, ParagonColours::accent.withAlpha (0.08f), getLocalBounds());
        g.fillRoundedRectangle (area, 4.0f);
        ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
        g.drawRoundedRectangle (area, 4.0f, 2.0f);
        const auto label = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 20.0f, 560.0f), 34.0f);
        g.setColour (juce::Colour (0xe0141016));
        g.fillRoundedRectangle (label, 5.0f);
        ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        ParagonColours::setTextFill (g, ParagonColours::accent, ParagonColours::textSpan (g.getCurrentFont(), dropText, (label).toFloat(), juce::Justification::centred));
        g.drawText (dropText, label, juce::Justification::centred, true);
        ParagonColours::setFill (g, ParagonColours::accent, getLocalBounds());
    }
}

//==============================================================================
// A GROOVE DROPPED ON IT (0.13.0)
bool DrumSeqGrid::isInterestedInDragSource (const SourceDetails& details)
{
    return describeDrop != nullptr && describeDrop (details.description.toString()).isNotEmpty();
}

void DrumSeqGrid::itemDragEnter (const SourceDetails& details)
{
    dropText = describeDrop != nullptr ? describeDrop (details.description.toString()) : juce::String();
    repaint();
}

void DrumSeqGrid::itemDragExit (const SourceDetails&)
{
    dropText.clear();
    repaint();
}

void DrumSeqGrid::itemDropped (const SourceDetails& details)
{
    dropText.clear();
    if (onDrop != nullptr && onDrop (details.description.toString()))
        refreshShown();
    repaint();
}

bool DrumSeqGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("mid;midi");
}

void DrumSeqGrid::fileDragEnter (const juce::StringArray& files, int, int)
{
    dropText = "DROP: " + juce::File (files[0]).getFileNameWithoutExtension() + " REPLACES PATTERN "
             + synth::drumseq::patternNames[getPattern()];
    repaint();
}

void DrumSeqGrid::fileDragExit (const juce::StringArray&)
{
    dropText.clear();
    repaint();
}

void DrumSeqGrid::filesDropped (const juce::StringArray& files, int, int)
{
    dropText.clear();
    if (processor.grooveFileToPattern (juce::File (files[0]), getPattern()).placed > 0)
        refreshShown();
    repaint();
}

void DrumSeqGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    refreshShown();
    downAt = e.position;
    changed = false;
    const auto hit = noteAt (e.position);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            if (! selected[(size_t) hit])
                selected.assign (shown.notes.size(), false);
            selected[(size_t) hit] = true;
            deleteSelected();
        }
        return;
    }

    if (hit >= 0)
    {
        if (e.mods.isShiftDown())
            selected[(size_t) hit] = ! selected[(size_t) hit];
        else if (! selected[(size_t) hit])
        {
            selected.assign (shown.notes.size(), false);
            selected[(size_t) hit] = true;
        }
        drag = Drag::move;
        dragOriginals = shown.notes;
        if (onRowTouched)
            onRowTouched (shown.notes[(size_t) hit].row);
        processor.previewDrum (shown.notes[(size_t) hit].row);
    }
    else if (tool == Tool::pen && ! e.mods.isShiftDown())
    {
        // A new note in that step, chosen alone; dragging along the row adds more.
        Note n;
        n.row = rowAt (e.position.y);
        n.start = stepAt (e.position.x, ! e.mods.isAltDown());
        auto p = shown;
        p.notes.push_back (n);
        selected.assign (p.notes.size(), false);
        selected.back() = true;
        setShown (p, false);
        changed = true;
        drag = Drag::paint;
        paintRow = n.row;
        if (onRowTouched)
            onRowTouched (n.row);
        processor.previewDrum (n.row);
    }
    else
    {
        if (! e.mods.isShiftDown())
            selected.assign (shown.notes.size(), false);
        drag = Drag::frame;
        dragOriginals = shown.notes;
        frame = {};
    }
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

void DrumSeqGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::move)
    {
        // Every chosen note by the same steps and rows, kept inside the pattern.
        const auto stepWidth = (float) getWidth() / (float) getLengthSteps();
        const auto rawSteps = (double) (e.position.x - downAt.x) / stepWidth;
        const auto steps = e.mods.isAltDown() ? std::round (rawSteps * 16.0) / 16.0 : std::round (rawSteps);
        const auto rows = rowAt (e.position.y) - rowAt (downAt.y);
        if (! changed && juce::exactlyEqual (steps, 0.0) && rows == 0)
            return;
        auto p = shown;
        const auto length = getLengthSteps();
        for (size_t i = 0; i < p.notes.size(); ++i)
            if (selected[i])
            {
                auto& n = p.notes[i];
                const auto& o = dragOriginals[i];
                n.start = juce::jlimit (0.0, length - 1.0 / 16.0, o.start + steps);
                n.row = juce::jlimit (0, synth::drumseq::numRows - 1, o.row + rows);
            }
        if (! (p == shown))
        {
            changed = true;
            setShown (p, false);
        }
    }
    else if (drag == Drag::paint)
    {
        const auto step = stepAt (e.position.x, true);
        if (noteIn (paintRow, step) < 0)
        {
            Note n;
            n.row = paintRow;
            n.start = step;
            auto p = shown;
            p.notes.push_back (n);
            selected.push_back (false);
            setShown (p, false);
            changed = true;
        }
    }
    else if (drag == Drag::frame)
    {
        frame = juce::Rectangle<float> (downAt, e.position);
        const auto length = getLengthSteps();
        for (size_t i = 0; i < shown.notes.size(); ++i)
        {
            const auto& n = shown.notes[i];
            if (n.start < length && cell (n.row, n.start).intersects (frame))
                selected[i] = true;
            else if (! e.mods.isShiftDown())
                selected[i] = false;
        }
        repaint();
        if (onSelectionChanged)
            onSelectionChanged();
    }
}

void DrumSeqGrid::mouseUp (const juce::MouseEvent&)
{
    if (changed)
        processor.modMatrixEditFinished();
    drag = Drag::none;
    changed = false;
    frame = {};
    repaint();
}

bool DrumSeqGrid::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAll();
        return true;
    }
    return false;
}

//==============================================================================
namespace
{
    using synth::drumseq::FxStep;
    using synth::drumseq::FxType;

    // A colour each, in the family's range (red, pink, orange, amber, pearl, rose).
    juce::Colour fxColour (FxType type)
    {
        switch (type)
        {
            case FxType::repeat:   return ParagonColours::familyRed;
            case FxType::chop:     return ParagonColours::familyPink;
            case FxType::tapeStop: return ParagonColours::familyOrange;
            case FxType::reverse:  return amber;
            case FxType::filter:   return juce::Colour (0xffe8d6d6);
            case FxType::bitcrush: return juce::Colour (0xffb0306a);
            case FxType::none:     break;
        }
        return ParagonColours::textDim;
    }
    bool usesRate (FxType type) { return type == FxType::repeat || type == FxType::chop || type == FxType::reverse; }
    juce::String fxLabel (const FxStep& f)
    {
        auto text = synth::drumseq::fxNames[(int) f.type].toUpperCase();
        if (usesRate (f.type))
            text << " " << synth::drumseq::fxRateNames[f.rate];
        return text + " " + juce::String (juce::roundToInt (f.amount * 100.0f)) + "%";
    }
}

StepFxLane::StepFxLane (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("STEP FX: effects on the whole drum mix while the pattern plays through them. Drag along an empty part to add one "
                "(the effect last used), right-click for the list; drag a block to move it, its right edge to stretch it; "
                "right-click a block to change it or remove it; the mouse wheel sets its AMOUNT.");
    startTimerHz (30);
}

int StepFxLane::getPattern() const { return processor.getDrumSeqSettings().pattern; }
double StepFxLane::getLengthSteps() const { return processor.getDrumSeqSettings().lengthSteps(); }

std::vector<FxStep> StepFxLane::getFx() const
{
    return processor.getModMatrix().drumPatterns[(size_t) getPattern()].fx;
}

void StepFxLane::setFx (std::vector<FxStep> fx, bool finished)
{
    auto matrix = processor.getModMatrix();
    auto& pattern = matrix.drumPatterns[(size_t) getPattern()];
    pattern.fx = std::move (fx);
    if ((int) pattern.fx.size() > synth::drumseq::maxFxSteps)
        pattern.fx.resize ((size_t) synth::drumseq::maxFxSteps);
    pattern.sortFx();
    processor.setModMatrix (matrix);
    if (finished)
        processor.modMatrixEditFinished();
    repaint();
}

double StepFxLane::stepAt (float x) const
{
    const auto length = getLengthSteps();
    return juce::jlimit (0.0, length - 1.0, std::floor ((double) x / juce::jmax (1, getWidth()) * length));
}

juce::Rectangle<float> StepFxLane::blockArea (int index) const
{
    const auto fx = getFx();
    if (index < 0 || index >= (int) fx.size())
        return {};
    const auto stepWidth = (float) getWidth() / (float) getLengthSteps();
    const auto& f = fx[(size_t) index];
    const auto x0 = (float) f.start * stepWidth, x1 = juce::jmin ((float) getWidth(), (float) (f.start + f.length) * stepWidth);
    return juce::Rectangle<float> (x0, 2.0f, juce::jmax (2.0f, x1 - x0), (float) getHeight() - 4.0f).reduced (1.0f, 0.0f);
}

int StepFxLane::fxAt (float x) const
{
    const auto fx = getFx();
    for (int i = (int) fx.size(); --i >= 0;)
        if (const auto a = blockArea (i); x >= a.getX() - 1.0f && x <= a.getRight() + 1.0f)
            return i;
    return -1;
}

void StepFxLane::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff100c10));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);

    const auto length = getLengthSteps();
    const auto stepWidth = (float) getWidth() / (float) length;
    const auto stepsPerBeat = 1.0 / processor.getDrumSeqSettings().beatsPerStep();
    for (int k = 1; k < (int) std::ceil (length); ++k)
    {
        const auto beat = std::fmod ((double) k, stepsPerBeat) < 1.0e-6;
        if (! beat && stepWidth < 5.0f)
            continue;
        g.setColour (ParagonColours::panelEdge.withAlpha (beat ? 0.8f : 0.3f));
        g.drawVerticalLine (juce::roundToInt ((float) k * stepWidth), 3.0f, (float) getHeight() - 3.0f);
    }

    const auto fx = getFx();
    if (fx.empty())
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("STEP FX: DRAG HERE TO ADD AN EFFECT (RIGHT-CLICK FOR THE LIST)", getLocalBounds().reduced (8, 0), juce::Justification::centredLeft, false);
    }
    const auto playing = processor.getPlayingStepFx();
    for (int i = 0; i < (int) fx.size(); ++i)
    {
        const auto& f = fx[(size_t) i];
        if (f.start >= length)
            continue;
        const auto r = blockArea (i);
        const auto colour = fxColour (f.type);
        const auto lit = playing == i;
        g.setColour (colour.withAlpha (lit ? 0.75f : 0.2f + 0.3f * f.amount));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (r, 3.0f, lit ? 2.0f : 1.0f);
        if (r.getWidth() > 24.0f)
        {
            g.setColour (lit ? juce::Colours::black : ParagonColours::text);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawFittedText (fxLabel (f), r.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        }
    }

    // Where the beat is playing (as the grid's line).
    const auto position = processor.getDrumKit().getSequencerPosition();
    if (position >= 0.0 && processor.getDrumKit().getSequencerPattern() == getPattern())
    {
        const auto x = (float) (position / length * getWidth());
        g.setColour (ParagonColours::text.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, 2.0f, 1.5f, (float) getHeight() - 4.0f));
    }
}

void StepFxLane::timerCallback()
{
    if (! isShowing())
        return;
    const auto position = processor.getDrumKit().getSequencerPosition();
    const auto x = position >= 0.0 ? (float) (position / getLengthSteps() * getWidth()) : -1.0f;
    const auto playing = processor.getPlayingStepFx();
    if (std::abs (x - lastPlayhead) >= 1.0f || playing != lastPlaying)
    {
        lastPlayhead = x;
        lastPlaying = playing;
        repaint();
    }
}

juce::PopupMenu StepFxLane::menuFor (int index, double step)
{
    juce::PopupMenu m;
    const auto fx = getFx();
    if (index < 0 || index >= (int) fx.size())
    {
        m.addSectionHeader ("ADD AN EFFECT ON THIS STEP");
        for (int t = 1; t < synth::drumseq::fxNames.size(); ++t)
            m.addItem (t, synth::drumseq::fxNames[t]);
        juce::ignoreUnused (step);
        return m;
    }
    const auto& f = fx[(size_t) index];
    m.addSectionHeader (fxLabel (f));
    juce::PopupMenu types, rates, amounts;
    for (int t = 1; t < synth::drumseq::fxNames.size(); ++t)
        types.addItem (t, synth::drumseq::fxNames[t], true, (int) f.type == t);
    m.addSubMenu ("Effect", types);
    for (int r = 0; r < synth::drumseq::fxRateNames.size(); ++r)
        rates.addItem (100 + r, synth::drumseq::fxRateNames[r], true, f.rate == r);
    m.addSubMenu ("Rate", rates, usesRate (f.type));
    for (const auto a : { 25, 50, 75, 100 })
        amounts.addItem (200 + a, juce::String (a) + "%", true, juce::roundToInt (f.amount * 100.0f) == a);
    m.addSubMenu ("Amount", amounts);
    m.addSeparator();
    m.addItem (300, "Remove");
    return m;
}

void StepFxLane::runMenuItem (int index, double step, int item)
{
    if (item <= 0)
        return;
    auto fx = getFx();
    if (index < 0 || index >= (int) fx.size())
    {
        if (item < synth::drumseq::fxNames.size())
        {
            // A new one on that step (cut short of the next one), as long as a beat if there's room.
            FxStep f;
            f.type = (FxType) item;
            f.start = step;
            auto end = juce::jmin (getLengthSteps(), step + juce::jmax (1.0, std::round (1.0 / processor.getDrumSeqSettings().beatsPerStep())));
            for (const auto& other : fx)
                if (other.start > step)
                    end = juce::jmin (end, other.start);
            f.length = juce::jmax (1.0, end - step);
            fx.push_back (f);
            nextType = f.type;
            setFx (std::move (fx), true);
        }
        return;
    }
    auto& f = fx[(size_t) index];
    if (item < synth::drumseq::fxNames.size())
        nextType = f.type = (FxType) item;
    else if (item >= 100 && item < 100 + synth::drumseq::fxRateNames.size())
        f.rate = item - 100;
    else if (item > 200 && item < 300)
        f.amount = (float) (item - 200) / 100.0f;
    else if (item == 300)
        fx.erase (fx.begin() + index);
    setFx (std::move (fx), true);
}

void StepFxLane::mouseDown (const juce::MouseEvent& e)
{
    const auto index = fxAt (e.position.x);
    const auto step = stepAt (e.position.x);
    changed = false;
    if (e.mods.isPopupMenu())
    {
        menuFor (index, step).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                                             [safe = juce::Component::SafePointer<StepFxLane> (this), index, step] (int item)
                                             {
                                                 if (safe != nullptr)
                                                     safe->runMenuItem (index, step, item);
                                             });
        return;
    }
    downStep = step;
    if (index >= 0)
    {
        dragIndex = index;
        original = getFx()[(size_t) index];
        drag = e.position.x > blockArea (index).getRight() - 6.0f ? Drag::stretch : Drag::move;
        return;
    }
    // A new block of the effect last used, on this step, stretched as the mouse drags.
    auto fx = getFx();
    FxStep f;
    f.type = nextType;
    f.start = step;
    f.length = 1.0;
    fx.push_back (f);
    setFx (fx, false);
    changed = true;
    const auto now = getFx();
    for (int i = 0; i < (int) now.size(); ++i)
        if (juce::exactlyEqual (now[(size_t) i].start, step) && now[(size_t) i].type == nextType && juce::exactlyEqual (now[(size_t) i].length, 1.0))
            dragIndex = i;
    original = f;
    drag = Drag::add;
}

void StepFxLane::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || dragIndex < 0)
        return;
    auto fx = getFx();
    if (dragIndex >= (int) fx.size())
        return;
    const auto length = getLengthSteps();
    const auto step = stepAt (e.position.x);
    auto f = original;
    if (drag == Drag::move)
        f.start = juce::jlimit (0.0, juce::jmax (0.0, length - 1.0), original.start + (step - downStep));
    else
        f.length = juce::jmax (1.0, step + 1.0 - f.start);
    if (f == fx[(size_t) dragIndex])
        return;
    fx[(size_t) dragIndex] = f;
    // Keep following the same block through the re-sort.
    setFx (fx, false);
    const auto now = getFx();
    for (int i = 0; i < (int) now.size(); ++i)
        if (now[(size_t) i] == f)
            dragIndex = i;
    changed = true;
}

void StepFxLane::mouseUp (const juce::MouseEvent&)
{
    if (changed)
        processor.modMatrixEditFinished();
    drag = Drag::none;
    dragIndex = -1;
    changed = false;
}

void StepFxLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto index = fxAt (e.position.x);
    if (index < 0)
        return;
    auto fx = getFx();
    auto& f = fx[(size_t) index];
    f.amount = juce::jlimit (0.0f, 1.0f, std::round ((f.amount + (wheel.deltaY > 0.0f ? 0.05f : -0.05f)) * 20.0f) / 20.0f);
    setFx (std::move (fx), true);
}

//==============================================================================
NoteField::NoteField (juce::String l, float lo, float hi, float s, float d, std::function<juce::String (float)> f)
    : label (std::move (l)), min (lo), max (hi), step (s), defaultValue (d), format (std::move (f))
{
}

void NoteField::paint (juce::Graphics& g)
{
    const auto v = get ? get() : std::nanf ("");
    const auto none = std::isnan (v);
    auto area = getLocalBounds().toFloat();
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (label, area.removeFromTop (13.0f), juce::Justification::centred, false);
    area = area.reduced (2.0f, 1.0f);
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (area, 3.0f, 1.0f);
    ParagonColours::setFill (g, none ? ParagonColours::textDim.withAlpha (0.5f) : ParagonColours::accent, getLocalBounds());
    g.setFont (juce::FontOptions (12.0f));
    ParagonColours::setTextFill (g, none ? ParagonColours::textDim.withAlpha (0.5f) : ParagonColours::accent, ParagonColours::textSpan (g.getCurrentFont(), none ? juce::String ("-") : choices.isEmpty() ? format (v) : choices[juce::roundToInt (v)], (area.toNearestInt()).toFloat(), juce::Justification::centred));
    g.drawFittedText (none ? juce::String ("-") : choices.isEmpty() ? format (v) : choices[juce::roundToInt (v)], area.toNearestInt(),
                      juce::Justification::centred, 1, 0.8f);
    ParagonColours::setFill (g, none ? ParagonColours::textDim.withAlpha (0.5f) : ParagonColours::accent, getLocalBounds());
}

void NoteField::mouseDown (const juce::MouseEvent&)
{
    const auto v = get ? get() : std::nanf ("");
    if (std::isnan (v) || ! set)
        return;
    if (! choices.isEmpty())
    {
        juce::PopupMenu menu;
        for (int i = 0; i < choices.size(); ++i)
            menu.addItem (i + 1, choices[i], true, i == juce::roundToInt (v));
        juce::Component::SafePointer<NoteField> self (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self] (int chosen)
        {
            if (self != nullptr && chosen > 0)
            {
                self->set ((float) (chosen - 1), true);
                self->repaint();
            }
        });
        return;
    }
    dragStart = dragValue = v;
    dragging = false;
}

void NoteField::mouseDrag (const juce::MouseEvent& e)
{
    if (! choices.isEmpty() || ! set || std::isnan (get ? get() : std::nanf ("")))
        return;
    dragging = true;
    const auto travel = (float) -e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 1200.0f : 150.0f);
    auto v = juce::jlimit (min, max, dragStart + travel * (max - min));
    if (! e.mods.isShiftDown())
        v = juce::jlimit (min, max, min + std::round ((v - min) / step) * step);
    if (! juce::exactlyEqual (v, dragValue))
    {
        dragValue = v;
        set (v, false);
        repaint();
    }
}

void NoteField::mouseUp (const juce::MouseEvent&)
{
    if (std::exchange (dragging, false) && set)
        set (dragValue, true);
}

void NoteField::mouseDoubleClick (const juce::MouseEvent&)
{
    if (set && ! std::isnan (get ? get() : std::nanf ("")))
    {
        set (defaultValue, true);
        repaint();
    }
}

//==============================================================================
void MidiExportButton::mouseDrag (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDrag (e);
    if (dragged || e.getDistanceFromDragStart() < 6 || ! makeDragFile)
        return;
    dragged = true;
    const auto file = makeDragFile();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void MidiExportButton::clicked()
{
    if (std::exchange (dragged, false))
        return;
    const auto name = fileName.isNotEmpty() ? fileName : juce::String ("Drums");
    chooser = std::make_unique<juce::FileChooser> ("Save " + name + " as MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".mid"),
                                                   "*.mid");
    juce::Component::SafePointer<MidiExportButton> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [self] (const juce::FileChooser& c)
                          {
                              if (self == nullptr || c.getResult() == juce::File() || ! self->exportTo)
                                  return;
                              self->exportTo (c.getResult().withFileExtension ("mid"));
                          });
}

//==============================================================================
void DrumSeqPanel::style (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
    b.setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
    b.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
    b.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
}

DrumSeqPanel::DrumSeqPanel (ParagonX1AudioProcessor& p) : processor (p), grid (p), fxLane (p)
{
    setInterceptsMouseClicks (false, true);   // the page's own buttons show through the gaps
    auto& state = p.getParameters();
    const auto control = [&] (const char* id, const juce::String& label, const juce::String& tip)
    {
        controls.push_back (std::make_unique<ParamControl> (state, id, label, tip));
        addAndMakeVisible (*controls.back());
    };
    control (ParamIDs::dseqOn, "ON", "The drum sequencer on. It plays the kit's drums, which you set up on the DRUM view.");
    control (ParamIDs::dseqTrigger, "TRIGGER", "SONG: the pattern chosen plays while your song plays, in time with its bars. "
                                               "KEY: the four keys under the kit's lowest (its root) play patterns A to D while held, from the key.");
    control (ParamIDs::dseqDivision, "SPEED", "How long a step of the grid is: 1/16 is four steps a beat.");
    control (ParamIDs::dseqTiming, "TIMING", "Straight, triplet (three steps in the time of two) or dotted (one and a half times as long).");
    control (ParamIDs::dseqLength, "LENGTH", "How many beats the pattern lasts before it starts again (1 to 16).");
    control (ParamIDs::shuffle, "SHUFFLE", "Swing: every second step late, up to half a step.");
    control (ParamIDs::dseqVelocity, "VEL AMOUNT", "KEY: how much the key's velocity scales the pattern's (0: the pattern as drawn).");

    for (int k = 0; k < synth::drumseq::numPatterns; ++k)
    {
        auto button = std::make_unique<juce::TextButton> (synth::drumseq::patternNames[k]);
        style (*button);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (961);
        button->setTooltip ("Pattern " + synth::drumseq::patternNames[k] + ": edit it (and with SONG, play it). With KEY, the key "
                            + notenames::name (36 - 4 + k) + " plays it (with the kit's root on " + notenames::name (36) + ").");
        button->onClick = [this, k] { selectPattern (k); };
        addAndMakeVisible (*button);
        patternButtons.push_back (std::move (button));
    }
    for (auto* b : { &pen, &frameTool })
    {
        style (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (962);
        addAndMakeVisible (*b);
    }
    pen.setTooltip ("PEN: click the grid to add notes (drag along for more), drag notes to move them, right-click to remove.");
    frameTool.setTooltip ("SELECT: drag a frame round notes to choose them; drag chosen notes to move them.");
    pen.setToggleState (true, juce::dontSendNotification);
    pen.onClick = [this] { grid.tool = DrumSeqGrid::Tool::pen; };
    frameTool.onClick = [this] { grid.tool = DrumSeqGrid::Tool::select; };

    style (exportButton);
    exportButton.setTooltip ("Click to save this pattern as a MIDI file; or drag this button onto a track in your song. One time "
                             "through, on the kit's keys, at the song's tempo (a note's pan, pitch and reverse can't be written as MIDI).");
    exportButton.makeDragFile = [this] { return processor.drumMidiDragFile (grid.getPattern()); };
    exportButton.exportTo = [this] (const juce::File& f) { return processor.exportDrumMidi (grid.getPattern(), f); };
    addAndMakeVisible (exportButton);
    addAndMakeVisible (grid);
    addAndMakeVisible (fxLane);

    // The chosen notes' settings: set on all of them; shown from the first.
    const auto field = [this] (const juce::String& label, float lo, float hi, float step, float def, std::function<juce::String (float)> format,
                               std::function<float (const Note&)> read, std::function<void (Note&, float)> write, const juce::String& tip)
    {
        auto f = std::make_unique<NoteField> (label, lo, hi, step, def, std::move (format));
        f->setTooltip (tip);
        f->get = [this, read]
        {
            const auto chosen = grid.getSelection();
            return chosen.empty() ? std::nanf ("") : read (grid.getShown().notes[(size_t) chosen.front()]);
        };
        f->set = [this, write] (float v, bool finished)
        {
            auto p = grid.getShown();
            for (const auto i : grid.getSelection())
                write (p.notes[(size_t) i], v);
            grid.setShown (p, finished);
        };
        addAndMakeVisible (*f);
        fields.push_back (std::move (f));
    };
    field ("VELOCITY", 0.0f, 1.0f, 0.01f, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; },
           [] (const Note& n) { return n.velocity; }, [] (Note& n, float v) { n.velocity = v; },
           "How hard the chosen notes hit (the level goes with its square: half is 12 dB down).");
    field ("PAN", -1.0f, 1.0f, 0.01f, 0.0f, [] (float v)
           {
               const auto percent = juce::roundToInt (v * 100.0f);
               return percent == 0 ? juce::String ("C") : (percent < 0 ? "L" : "R") + juce::String (std::abs (percent));
           },
           [] (const Note& n) { return n.pan; }, [] (Note& n, float v) { n.pan = v; }, "The chosen notes to the left or right of the drum's own place.");
    field ("PITCH", -24.0f, 24.0f, 1.0f, 0.0f, [] (float v)
           {
               const auto rounded = std::round (v);
               const auto text = std::abs (v - rounded) < 0.005f ? juce::String ((int) rounded) : juce::String (v, 2);
               return (v > 0.004f ? "+" : "") + text + " st";
           },
           [] (const Note& n) { return n.pitch; }, [] (Note& n, float v) { n.pitch = v; },
           "The chosen notes higher or lower than the drum, in semitones (SHIFT-drag for hundredths).");
    field ("GATE", 0.05f, 1.0f, 0.05f, 1.0f, [] (float v) { return v >= 0.999f ? juce::String ("RING") : juce::String (v, 2) + " beat"; },
           [] (const Note& n) { return n.gate; }, [] (Note& n, float v) { n.gate = v; },
           "RING: the hit rings out. Lower: it's cut after this much of a beat (an open hat closed early, a short kick).");
    field ("ROLL", 0.0f, (float) synth::drumseq::rollNames.size() - 1.0f, 1.0f, 0.0f, {}, [] (const Note& n) { return (float) n.roll; },
           [] (Note& n, float v) { n.roll = juce::roundToInt (v); }, "The note played several times within its step: 2x, 3x (a triplet), 4x ... 16x (a ratchet).");
    fields.back()->choices = synth::drumseq::rollNames;
    field ("REVERSE", 0.0f, 2.0f, 1.0f, 0.0f, {}, [] (const Note& n) { return (float) (int) n.reverse; },
           [] (Note& n, float v) { n.reverse = (synth::drumseq::Reverse) juce::roundToInt (v); },
           "The hit played backwards. END SYNC: it starts early so that it ends where its step does (a swell into the beat).");
    fields.back()->choices = { "Off", "Reverse", "End sync" };
    // 0.72.0: THE RATCHET's ramps (over a ROLL's repeats), CHANCE and CONDITION.
    field ("RAMP", -24.0f, 24.0f, 1.0f, 0.0f, [] (float v)
           {
               const auto r = juce::roundToInt (v);
               return r == 0 ? juce::String ("FLAT") : (r > 0 ? "+" : "") + juce::String (r) + " st";
           },
           [] (const Note& n) { return n.rollPitch; }, [] (Note& n, float v) { n.rollPitch = v; },
           "PITCH RAMP: a ROLL's repeats rising (+) or falling (-) in pitch, the last this many semitones away (a snare roll "
           "that sweeps up, a tom fill falling).");
    field ("FADE", -1.0f, 1.0f, 0.05f, 0.0f, [] (float v)
           {
               const auto p = juce::roundToInt (v * 100.0f);
               return p == 0 ? juce::String ("FLAT") : p < 0 ? "OUT " + juce::String (-p) + "%" : "IN " + juce::String (p) + "%";
           },
           [] (const Note& n) { return n.rollFade; }, [] (Note& n, float v) { n.rollFade = v; },
           "A ROLL's repeats fading OUT (dying away) or IN (swelling up from soft) across the step.");
    field ("CHANCE", 0.0f, 1.0f, 0.01f, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; },
           [] (const Note& n) { return n.chance; }, [] (Note& n, float v) { n.chance = v; },
           "How likely the chosen notes are to play on each pass of the loop (100%: always), so a short pattern keeps changing.");
    field ("CONDITION", 0.0f, (float) synth::drumseq::conditionNames.size() - 1.0f, 1.0f, 0.0f, {}, [] (const Note& n) { return (float) n.condition; },
           [] (Note& n, float v) { n.condition = juce::roundToInt (v); },
           "Which passes of the loop the chosen notes play on: every time, the 2nd of every 2, the 4th of every 4 (a fill at "
           "the end of a phrase), only the first pass, or every pass but the first.");
    fields.back()->choices = synth::drumseq::conditionNames;

    grid.onSelectionChanged = [this]
    {
        for (auto& f : fields)
            f->repaint();
        repaint (notesArea.withHeight (12));
    };
    startTimerHz (15);
    timerCallback();
}

void DrumSeqPanel::selectPattern (int pattern)
{
    auto* p = parameter (processor, ParamIDs::dseqPattern);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, synth::drumseq::numPatterns - 1, pattern)));
    p->endChangeGesture();
    grid.refreshShown();
    grid.repaint();
    timerCallback();
}

void DrumSeqPanel::timerCallback()
{
    const auto pattern = grid.getPattern();
    // The pattern being played (KEY: the key held's) in amber when it isn't the one shown.
    const auto playing = processor.getDrumKit().getSequencerPosition() >= 0.0 ? processor.getDrumKit().getSequencerPattern() : -1;
    for (int k = 0; k < (int) patternButtons.size(); ++k)
    {
        auto& b = *patternButtons[(size_t) k];
        if (b.getToggleState() != (k == pattern))
            b.setToggleState (k == pattern, juce::dontSendNotification);
        const auto colour = k == playing && k != pattern ? amber : ParagonColours::textDim;
        if (b.findColour (juce::TextButton::textColourOffId) != colour)
            b.setColour (juce::TextButton::textColourOffId, colour);
    }
    exportButton.fileName = "Drums " + synth::drumseq::patternNames[pattern];
    for (auto& f : fields)
        f->repaint();
}

void DrumSeqPanel::setGridArea (juce::Rectangle<int> area)
{
    gridArea = area;
    resized();
}

void DrumSeqPanel::resized()
{
    auto d = getLocalBounds().reduced (14, 10);
    // The title line: PATTERN A-D, the tools and EXPORT (the page's view switch is at its right end).
    auto title = d.removeFromTop (30);
    // (0.72.0: packed tighter: EXPORT MIDI ran under the view switch's MACHINE tab, added in 0.38.0.)
    title.removeFromLeft (100);
    for (auto& b : patternButtons)
        b->setBounds (title.removeFromLeft (34).withSizeKeepingCentre (30, 24));
    title.removeFromLeft (8);
    pen.setBounds (title.removeFromLeft (56).withSizeKeepingCentre (52, 24));
    frameTool.setBounds (title.removeFromLeft (64).withSizeKeepingCentre (60, 24));
    title.removeFromLeft (6);
    exportButton.setBounds (title.removeFromLeft (100).withSizeKeepingCentre (96, 24));

    // The sequencer's settings, then the chosen notes' (two rows of three).
    auto row = d.removeFromTop (ParamControl::knobHeight);
    notesArea = row.removeFromRight (360);   // (0.72.0: 5 fields a line, 72 wide)
    row.removeFromRight (14);
    // ON, TRIGGER, SPEED, TIMING, LENGTH, SHUFFLE, VEL AMOUNT: each as wide as it needs, shared out if there's less room.
    constexpr int widths[] = { 46, 96, 82, 102, 70, 70, 78 };
    const auto total = (float) std::accumulate (std::begin (widths), std::end (widths), 0);
    const auto scale = juce::jmin (1.0f, (float) row.getWidth() / total);
    for (size_t i = 0; i < controls.size(); ++i)
        controls[i]->setBounds (row.removeFromLeft (juce::roundToInt ((float) widths[i] * scale)).reduced (2, 0));
    auto notes = notesArea.withTrimmedTop (12);
    for (int line = 0; line < 2; ++line)
    {
        auto strip = notes.removeFromTop (37);
        for (int i = 0; i < 5; ++i)
            fields[(size_t) (line * 5 + i)]->setBounds (strip.removeFromLeft (72));
    }
    grid.setBounds (gridArea);
    // THE STEP FX LANE under it (0.73.0), in the room the page keeps below the rows.
    fxLane.setBounds (gridArea.getX(), gridArea.getBottom() + 4, gridArea.getWidth(), fxLaneHeight - 5);
}

void DrumSeqPanel::paint (juce::Graphics& g)
{
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (grid.getSelection().empty() ? juce::String ("CHOSEN NOTES: CLICK A NOTE") : juce::String ("CHOSEN NOTES"),
                notesArea.withHeight (12), juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::panelEdge);
    g.drawVerticalLine (notesArea.getX() - 9, (float) notesArea.getY() + 4.0f, (float) notesArea.getBottom() - 4.0f);
}
