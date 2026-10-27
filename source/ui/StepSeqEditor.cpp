#include "StepSeqEditor.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    constexpr float linkHeight = 14.0f;
    constexpr float laneGap = 10.0f;
    const char* const shownSettings[] = { ParamIDs::seqLength, ParamIDs::seqMode, ParamIDs::seqGate, ParamIDs::seqContour, ParamIDs::seqDecay,
                                          ParamIDs::shuffle, ParamIDs::seqDivision, ParamIDs::seqTiming, ParamIDs::seqOn };
}

StepSeqEditor::StepSeqEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Click a step to set its height (drag across to draw several); right-click it to delete it. "
                "Click the arrow under a step to link it to the next: the gate stays open from one to the other.");
    shown = getPattern();
    startTimerHz (15);
}

float StepSeqEditor::parameter (const char* id) const
{
    return processor.getParameters().getRawParameterValue (id)->load();
}

int StepSeqEditor::getNumLanes() const { return juce::roundToInt (parameter (ParamIDs::seqMode)) == (int) synth::seq::Mode::stereo ? 2 : 1; }
int StepSeqEditor::getLength() const { return juce::jlimit (2, synth::seq::maxSteps, juce::roundToInt (parameter (ParamIDs::seqLength))); }

juce::Rectangle<float> StepSeqEditor::laneArea (int lane) const
{
    auto area = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
    if (getNumLanes() == 1)
        return area;
    const auto height = (area.getHeight() - laneGap) * 0.5f;
    return lane == 0 ? area.removeFromTop (height) : area.removeFromBottom (height);
}

juce::Rectangle<float> StepSeqEditor::barArea (int lane, int step) const
{
    auto area = laneArea (lane);
    area.removeFromBottom (linkHeight);
    const auto width = area.getWidth() / (float) getLength();
    return { area.getX() + width * (float) step, area.getY(), width, area.getHeight() };
}

juce::Rectangle<float> StepSeqEditor::linkArea (int lane, int step) const
{
    const auto bar = barArea (lane, step);
    return { bar.getX(), bar.getBottom(), bar.getWidth(), linkHeight };
}

void StepSeqEditor::setPattern (const synth::seq::Pattern& pattern, bool finished)
{
    auto matrix = processor.getModMatrix();
    matrix.stepPattern = pattern;
    processor.setModMatrix (matrix);
    shown = pattern;
    if (finished)
        processor.modMatrixEditFinished();
    repaint();
}

void StepSeqEditor::timerCallback()
{
    // Undo, a preset, or a knob that changes the picture.
    std::vector<float> settings;
    for (const auto* id : shownSettings)
        settings.push_back (parameter (id));
    settings.push_back ((float) processor.getHostBpm());
    if (dragLane < 0 && ! (getPattern() == shown))
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

void StepSeqEditor::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    if (dragLane < 0)
        shown = getPattern();   // undo or a preset may have changed it since the last tick
    const auto length = getLength(), lanes = getNumLanes();

    // The gate as it will play: the settings now, with these steps (each lane its own row, before WIDTH).
    synth::StepSeqSettings settings;
    settings.length = length;
    settings.mode = lanes == 2 ? synth::seq::Mode::stereo : synth::seq::Mode::mono;
    settings.gate = parameter (ParamIDs::seqGate);
    settings.contour = parameter (ParamIDs::seqContour);
    settings.decayMs = parameter (ParamIDs::seqDecay);
    settings.shuffle = parameter (ParamIDs::shuffle);
    settings.division = juce::roundToInt (parameter (ParamIDs::seqDivision));
    settings.timing = (synth::seq::Timing) juce::jlimit (0, synth::seq::timingNames.size() - 1, juce::roundToInt (parameter (ParamIDs::seqTiming)));
    settings.pattern = &shown;
    synth::seq::Prepared prepared;
    prepared.prepare (settings, settings.beatsPerStep() * 60.0 / juce::jmax (1.0, processor.getHostBpm()));

    for (int lane = 0; lane < lanes; ++lane)
    {
        const auto& row = shown.rows[(size_t) lane];
        const auto area = laneArea (lane);
        for (int k = 0; k < length; ++k)
        {
            const auto cell = barArea (lane, k);
            const auto& step = row[(size_t) k];
            // Every beat (4 steps) a brighter line.
            g.setColour (ParagonColours::panelEdge.withAlpha (k % 4 == 0 ? 0.9f : 0.45f));
            g.drawVerticalLine (juce::roundToInt (cell.getX()), cell.getY(), cell.getBottom());
            const auto inner = cell.reduced (juce::jmin (3.0f, cell.getWidth() * 0.12f), 0.0f);
            if (step.on)
            {
                const auto bar = inner.withTop (inner.getBottom() - inner.getHeight() * step.level);
                g.setColour (ParagonColours::accent.withAlpha (0.28f));
                g.fillRect (bar);
                g.setColour (ParagonColours::accent.withAlpha (0.85f));
                g.fillRect (bar.withHeight (2.0f));
            }
            else
            {
                g.setColour (ParagonColours::textDim.withAlpha (0.5f));
                const auto mark = inner.withSizeKeepingCentre (juce::jmin (8.0f, inner.getWidth()), 2.0f).withY (inner.getBottom() - 6.0f);
                g.fillRect (mark);
            }
            // The link arrow: bright when linked to the next step.
            const auto link = linkArea (lane, k);
            juce::Path arrow;
            const auto cx = link.getRight() - 1.0f, cy = link.getCentreY();
            arrow.addTriangle (cx - 6.0f, cy - 4.0f, cx - 6.0f, cy + 4.0f, cx + 1.0f, cy);
            g.setColour (step.tied ? ParagonColours::accent : ParagonColours::textDim.withAlpha (0.3f));
            g.fillPath (arrow);
            if (step.tied)
                g.fillRect (juce::Rectangle<float> (link.getX() + 4.0f, cy - 1.0f, link.getWidth() - 10.0f, 2.0f));
        }

        // The gate's line over them.
        auto plot = area;
        plot.removeFromBottom (linkHeight);
        juce::Path path;
        const auto count = juce::jmax (2, (int) plot.getWidth());
        for (int i = 0; i < count; ++i)
        {
            const auto position = (double) length * i / (count - 1);
            const auto value = prepared.value (lane, juce::jmin (position, length - 1.0e-6));
            const juce::Point<float> point (plot.getX() + (float) i, plot.getBottom() - value * plot.getHeight());
            if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        g.setColour (ParagonColours::text.withAlpha (0.75f));
        g.strokePath (path, juce::PathStrokeType (1.3f));

        if (lanes == 2)
        {
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (lane == 0 ? "L" : "R", area.withWidth (14.0f).withHeight (14.0f).translated (-9.0f, -2.0f), juce::Justification::centred, false);
        }
    }
}

void StepSeqEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto position = e.position;
    shown = getPattern();
    for (int lane = 0; lane < getNumLanes(); ++lane)
        for (int k = 0; k < getLength(); ++k)
        {
            if (linkArea (lane, k).contains (position))
            {
                auto& step = shown.rows[(size_t) lane][(size_t) k];
                step.tied = ! step.tied;
                setPattern (shown, true);
                return;
            }
            if (barArea (lane, k).contains (position))
            {
                if (e.mods.isPopupMenu())
                {
                    shown.rows[(size_t) lane][(size_t) k].on = false;
                    setPattern (shown, true);
                    return;
                }
                dragLane = lane;
                drawAt (position);
                return;
            }
        }
}

void StepSeqEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane >= 0)
        drawAt (e.position);
}

void StepSeqEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragLane >= 0)
    {
        dragLane = -1;
        setPattern (shown, true);
    }
}

void StepSeqEditor::drawAt (juce::Point<float> position)
{
    // The step under the mouse (along the lane the drag started in), at the mouse's height.
    const auto first = barArea (dragLane, 0);
    const auto k = juce::jlimit (0, getLength() - 1, (int) std::floor ((position.x - first.getX()) / first.getWidth()));
    auto& step = shown.rows[(size_t) dragLane][(size_t) k];
    step.on = true;
    step.level = (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, (first.getBottom() - position.y) / first.getHeight()) * 100.0f) / 100.0f;
    setPattern (shown, false);
}
