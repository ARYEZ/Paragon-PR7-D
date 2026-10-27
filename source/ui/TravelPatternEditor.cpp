#include "TravelPatternEditor.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    constexpr float labelWidth = 34.0f;
    constexpr float rowGap = 6.0f;

    // Iterations in order: 0.5, then 1 to 16.
    float iterationsAt (int position) { return position <= 0 ? 0.5f : (float) juce::jmin (16, position); }
    int positionOf (float iterations) { return iterations < 0.75f ? 0 : juce::roundToInt (iterations); }

    juce::String stepText (const synth::travel::Step& s)
    {
        const auto mark = synth::travel::stepModeMarks[(int) s.mode];
        if (! synth::travel::takesIterations (s.mode))
            return mark;
        const auto n = s.iterations < 0.75f ? juce::String ("1/2") : juce::String (juce::roundToInt (s.iterations));
        return mark + n;
    }
}

TravelPatternEditor::TravelPatternEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("SEQ: drag a step up or down for more or fewer iterations; right-click it for its mode and length; double-click for one or two "
                "periods long. MOD: drag a step's strength at its start, middle or end (shift-drag bends it); double-click to add or remove "
                "the middle point.");
    startTimerHz (15);
}

void TravelPatternEditor::setOscillator (int index)
{
    oscillator = juce::jlimit (0, synth::numOscillators - 1, index);
    shown = getPattern();
    lastSettings.clear();
    repaint();
}

float TravelPatternEditor::parameter (const char* what) const
{
    return processor.getParameters().getRawParameterValue (ParamIDs::osc (oscillator, what))->load();
}

bool TravelPatternEditor::sequenceOn() const { return juce::roundToInt (parameter ("TrEditors")) != (int) synth::travel::Editors::env; }
bool TravelPatternEditor::strengthOn() const { return juce::roundToInt (parameter ("TrEditors")) == (int) synth::travel::Editors::envSeqMod; }
int TravelPatternEditor::getNumSteps() const { return juce::jlimit (1, synth::travel::maxSteps, juce::roundToInt (parameter ("TrSteps"))); }

juce::Rectangle<float> TravelPatternEditor::stepsRow() const
{
    auto area = getLocalBounds().toFloat().reduced (6.0f, 4.0f).withTrimmedLeft (labelWidth);
    return area.removeFromTop ((area.getHeight() - rowGap) * 0.42f);
}

juce::Rectangle<float> TravelPatternEditor::strengthRow() const
{
    auto area = getLocalBounds().toFloat().reduced (6.0f, 4.0f).withTrimmedLeft (labelWidth);
    return area.removeFromBottom ((area.getHeight() - rowGap) * 0.58f);
}

juce::Rectangle<float> TravelPatternEditor::stepArea (int step) const
{
    // Each step as wide as its length in periods.
    const auto count = getNumSteps();
    float units = 0.0f, before = 0.0f;
    for (int k = 0; k < count; ++k)
    {
        const auto length = (float) juce::jlimit (1, 2, shown.steps[(size_t) k].length);
        if (k < step)
            before += length;
        units += length;
    }
    const auto row = stepsRow();
    const auto unit = row.getWidth() / units;
    return { row.getX() + unit * before, row.getY(), unit * (float) juce::jlimit (1, 2, shown.steps[(size_t) step].length), row.getHeight() };
}

juce::Rectangle<float> TravelPatternEditor::strengthArea (int step) const
{
    const auto above = stepArea (step);
    const auto row = strengthRow();
    return { above.getX(), row.getY(), above.getWidth(), row.getHeight() };
}

int TravelPatternEditor::stepAt (juce::Point<float> at) const
{
    for (int k = 0; k < getNumSteps(); ++k)
        if (stepArea (k).contains (at) || strengthArea (k).contains (at))
            return k;
    return -1;
}

void TravelPatternEditor::setPattern (const synth::travel::Pattern& pattern, bool finished)
{
    auto matrix = processor.getModMatrix();
    matrix.travel[(size_t) oscillator].pattern = pattern;
    processor.setModMatrix (matrix);
    shown = pattern;
    if (finished)
        processor.modMatrixEditFinished();
    repaint();
}

void TravelPatternEditor::setStepMode (int step, synth::travel::StepMode mode)
{
    shown = getPattern();
    shown.steps[(size_t) juce::jlimit (0, synth::travel::maxSteps - 1, step)].mode = mode;
    setPattern (shown, true);
}

void TravelPatternEditor::setStepLength (int step, int length)
{
    shown = getPattern();
    shown.steps[(size_t) juce::jlimit (0, synth::travel::maxSteps - 1, step)].length = juce::jlimit (1, 2, length);
    setPattern (shown, true);
}

void TravelPatternEditor::timerCallback()
{
    const std::vector<float> settings { parameter ("TrSteps"), parameter ("TrEditors") };
    if (dragStep < 0 && ! (getPattern() == shown))
    {
        shown = getPattern();
        repaint();
    }
    if (settings != lastSettings)
    {
        lastSettings = settings;
        repaint();
    }
}

void TravelPatternEditor::paint (juce::Graphics& g)
{
    if (dragStep < 0)
        shown = getPattern();
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto sequence = sequenceOn(), strength = strengthOn();

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (ParagonColours::textDim);
    g.drawText ("SEQ", stepsRow().withX (6.0f).withWidth (labelWidth - 4.0f), juce::Justification::centredLeft, false);
    g.drawText ("MOD", strengthRow().withX (6.0f).withWidth (labelWidth - 4.0f), juce::Justification::centredLeft, false);

    for (int k = 0; k < getNumSteps(); ++k)
    {
        const auto& step = shown.steps[(size_t) k];
        const auto cell = stepArea (k).reduced (1.5f, 0.0f);
        g.setColour (ParagonColours::accent.withAlpha (sequence ? (step.mode == synth::travel::StepMode::silence ? 0.08f : 0.22f) : 0.06f));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (ParagonColours::accent.withAlpha (sequence ? 0.7f : 0.2f));
        g.drawRoundedRectangle (cell, 3.0f, 1.0f);
        g.setColour (sequence ? ParagonColours::text : ParagonColours::textDim.withAlpha (0.5f));
        g.setFont (juce::FontOptions (juce::jmin (13.0f, cell.getWidth() * 0.42f), juce::Font::bold));
        g.drawText (stepText (step), cell, juce::Justification::centred, false);

        // The strength curve under it.
        const auto box = strengthArea (k).reduced (1.5f, 0.0f);
        g.setColour (ParagonColours::panelEdge.withAlpha (0.8f));
        g.drawRect (box, 1.0f);
        const auto& st = shown.strength[(size_t) k];
        juce::Path curve;
        const auto points = juce::jmax (2, (int) box.getWidth());
        for (int i = 0; i < points; ++i)
        {
            const auto u = (float) i / (float) (points - 1);
            const juce::Point<float> at (box.getX() + u * box.getWidth(), box.getBottom() - st.at (u) * (box.getHeight() - 4.0f) - 2.0f);
            if (i == 0) curve.startNewSubPath (at); else curve.lineTo (at);
        }
        g.setColour (ParagonColours::accent.withAlpha (strength ? 0.9f : 0.2f));
        g.strokePath (curve, juce::PathStrokeType (1.5f));
        if (st.hasMiddle)
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ box.getCentreX(), box.getBottom() - st.middle * (box.getHeight() - 4.0f) - 2.0f }));
    }
    if (! sequence)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("EDITORS: choose Env+Seq to play the steps, Env+Seq+Mod for their strengths too", getLocalBounds().toFloat(),
                    juce::Justification::centred, false);
    }
}

void TravelPatternEditor::mouseDown (const juce::MouseEvent& e)
{
    shown = getPattern();
    const auto k = stepAt (e.position);
    dragStep = -1;
    if (k < 0)
        return;
    if (stepArea (k).contains (e.position))
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            for (int m = 0; m < synth::travel::stepModeNames.size(); ++m)
                menu.addItem (m + 1, synth::travel::stepModeMarks[m] + "   " + synth::travel::stepModeNames[m], true, (int) shown.steps[(size_t) k].mode == m);
            menu.addSeparator();
            menu.addItem (101, "1 period long", true, shown.steps[(size_t) k].length == 1);
            menu.addItem (102, "2 periods long", true, shown.steps[(size_t) k].length == 2);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [safe = juce::Component::SafePointer<TravelPatternEditor> (this), k] (int chosen)
                                {
                                    if (safe == nullptr || chosen <= 0)
                                        return;
                                    if (chosen > 100)
                                        safe->setStepLength (k, chosen - 100);
                                    else
                                        safe->setStepMode (k, (synth::travel::StepMode) (chosen - 1));
                                });
            return;
        }
        dragStep = k;
        dragWhat = 0;
        dragStartY = e.position.y;
        dragStartValue = (float) positionOf (shown.steps[(size_t) k].iterations);
        return;
    }
    // The strength: its start, middle or end (or the curve, with shift).
    const auto box = strengthArea (k);
    const auto u = (e.position.x - box.getX()) / box.getWidth();
    auto& st = shown.strength[(size_t) k];
    dragStep = k;
    dragWhat = e.mods.isShiftDown() ? 4 : u < 0.34f ? 1 : u > 0.66f ? 3 : (st.hasMiddle ? 2 : 0);
    if (dragWhat == 0)
        dragWhat = u < 0.5f ? 1 : 3;
    dragStartY = e.position.y;
    dragStartValue = dragWhat == 4 ? st.curve : 0.0f;
    mouseDrag (e);
}

void TravelPatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0)
        return;
    if (dragWhat == 0)
    {
        // Up for more iterations: one more every 10 pixels.
        const auto position = juce::jlimit (0, 16, juce::roundToInt (dragStartValue + (dragStartY - e.position.y) / 10.0f));
        shown.steps[(size_t) dragStep].iterations = iterationsAt (position);
        setPattern (shown, false);
        return;
    }
    auto& st = shown.strength[(size_t) dragStep];
    const auto box = strengthArea (dragStep);
    const auto level = juce::jlimit (0.0f, 1.0f, (box.getBottom() - 2.0f - e.position.y) / (box.getHeight() - 4.0f));
    switch (dragWhat)
    {
        case 1: st.start = level; break;
        case 2: st.middle = level; break;
        case 3: st.end = level; break;
        default: st.curve = juce::jlimit (-1.0f, 1.0f, dragStartValue + (e.position.y - dragStartY) / 80.0f); break;
    }
    setPattern (shown, false);
}

void TravelPatternEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragStep >= 0)
        setPattern (shown, true);
    dragStep = -1;
}

void TravelPatternEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    shown = getPattern();
    const auto k = stepAt (e.position);
    if (k < 0)
        return;
    if (stepArea (k).contains (e.position))
        shown.steps[(size_t) k].length = shown.steps[(size_t) k].length == 1 ? 2 : 1;
    else
    {
        auto& st = shown.strength[(size_t) k];
        st.hasMiddle = ! st.hasMiddle;
        if (st.hasMiddle)
            st.middle = 0.5f * (st.start + st.end);
    }
    setPattern (shown, true);
}
