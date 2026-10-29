#include "MultiPointEditor.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    constexpr float pointRadius = 5.0f;
    constexpr float maxEnvelopeLength = 30.0f;   // seconds (or beats)
}

MultiPointEditor::MultiPointEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Drag a point to move it, a line up or down to curve it. Double-click to add a point (or, on a point, to delete it). "
                "Right-click a point to put the SUSTAIN or LOOP points there.");
    shown = getShape();
    startTimerHz (15);
}

void MultiPointEditor::setTarget (Kind newKind, int newIndex)
{
    kind = newKind;
    index = newIndex;
    shown = getShape();
    repaint();
}

ModShapeData MultiPointEditor::getShape() const
{
    const auto& matrix = processor.getModMatrix();
    if (kind == Kind::pitchEnvelope)
        return matrix.pitchEnvelope;
    if (kind == Kind::travel)
        return matrix.travel[(size_t) juce::jlimit (0, synth::numOscillators - 1, index)].envelope;
    if (kind == Kind::drumEnvelope)
        return matrix.drumEnvelopes[(size_t) juce::jlimit (0, ModMatrix::numDrumSlots - 1, index)];
    return kind == Kind::envelope ? matrix.envelopes[(size_t) juce::jlimit (0, synth::mod::numEnvelopes - 1, index)]
                                  : matrix.lfoShapes[(size_t) juce::jlimit (0, synth::mod::numLfoShapes - 1, index)];
}

void MultiPointEditor::setShape (const ModShapeData& shape, bool finished)
{
    auto matrix = processor.getModMatrix();
    if (kind == Kind::pitchEnvelope)
        matrix.pitchEnvelope = shape;
    else if (kind == Kind::travel)
        matrix.travel[(size_t) index].envelope = shape;
    else if (kind == Kind::drumEnvelope)
        matrix.drumEnvelopes[(size_t) index] = shape;
    else
        (kind == Kind::envelope ? matrix.envelopes[(size_t) index] : matrix.lfoShapes[(size_t) index]) = shape;
    processor.setModMatrix (matrix);
    shown = shape;
    if (finished)
        processor.modMatrixEditFinished();
    repaint();
}

void MultiPointEditor::timerCallback()
{
    // Undo or a preset changed it.
    if (dragging < 0 && curving < 0 && ! (getShape() == shown))
    {
        shown = getShape();
        repaint();
    }
}

juce::Rectangle<float> MultiPointEditor::getPlotArea() const { return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedBottom (12.0f); }

float MultiPointEditor::viewLength() const
{
    if (isCycle())
        return 1.0f;
    if (lockedLength > 0.0f)
        return lockedLength;
    const auto length = shown.points.empty() ? 1.0f : shown.points.back().x;
    return juce::jmax (0.5f, length * 1.15f);
}

float MultiPointEditor::pixelX (float x) const { const auto a = getPlotArea(); return a.getX() + a.getWidth() * x / viewLength(); }
float MultiPointEditor::pixelY (float y) const { const auto a = getPlotArea(); return a.getBottom() - a.getHeight() * y; }
float MultiPointEditor::xAt (float px) const { const auto a = getPlotArea(); return juce::jmax (0.0f, (px - a.getX()) / a.getWidth() * viewLength()); }
float MultiPointEditor::yAt (float py) const { const auto a = getPlotArea(); return juce::jlimit (0.0f, 1.0f, (a.getBottom() - py) / a.getHeight()); }

juce::Point<float> MultiPointEditor::pointPosition (int point) const
{
    const auto& p = shown.points[(size_t) point];
    return { pixelX (p.x), pixelY (p.y) };
}

int MultiPointEditor::pointNear (juce::Point<float> at) const
{
    for (int i = (int) shown.points.size() - 1; i >= 0; --i)
        if (pointPosition (i).getDistanceFrom (at) <= pointRadius + 4.0f)
            return i;
    return -1;
}

void MultiPointEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = getPlotArea();
    const auto envelope = ! isCycle();   // an envelope's time axis and flags (the PITCH envelope's too)
    const auto sync = kind == Kind::envelope && processor.getParameters().getRawParameterValue (ParamIDs::env (index, "Sync"))->load() > 0.5f;

    // Grid: time marks for an envelope, quarters of the cycle for an LFO shape.
    g.setFont (juce::FontOptions (9.5f));
    const auto length = viewLength();
    float step = 0.25f;
    if (envelope)
    {
        for (const auto candidate : { 0.01f, 0.025f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f })
            if (length / candidate <= 10.0f)
            {
                step = candidate;
                break;
            }
    }
    for (float x = step; x < length; x += step)
    {
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (pixelX (x)), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim.withAlpha (0.7f));
        const auto label = ! envelope ? juce::String (juce::roundToInt (x * 100.0f)) + "%"
                         : sync ? juce::String (x, step < 1.0f ? 2 : 0) + (x == 1.0f ? " beat" : "")
                         : x < 1.0f ? juce::String (juce::roundToInt (x * 1000.0f)) + " ms" : juce::String (x, step < 1.0f ? 2 : 0) + " s";
        g.drawText (label, juce::Rectangle<float> (60.0f, 12.0f).withCentre ({ pixelX (x), area.getBottom() + 8.0f }), juce::Justification::centred, false);
    }
    g.setColour (ParagonColours::panelEdge);
    const auto middle = kind == Kind::envelope || kind == Kind::drumEnvelope ? 0.0f : 0.5f;   // LFO shapes and the PITCH envelope swing both ways
    g.drawHorizontalLine (juce::roundToInt (pixelY (middle)), area.getX(), area.getRight());
    if (kind == Kind::pitchEnvelope)
    {
        // The top and bottom in semitones.
        const auto range = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::pitchEnvRange)->load());
        g.setColour (ParagonColours::textDim.withAlpha (0.7f));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText ("+" + juce::String (range) + " st", juce::Rectangle<float> (area.getX() + 4.0f, area.getY(), 60.0f, 12.0f), juce::Justification::centredLeft, false);
        g.drawText ("-" + juce::String (range) + " st", juce::Rectangle<float> (area.getX() + 4.0f, area.getBottom() - 12.0f, 60.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    if (caption.isNotEmpty())
    {
        // Below the top line (where a flat envelope runs), on a dark backing so the curve can pass behind it.
        const juce::Font font (juce::FontOptions (10.0f, juce::Font::bold));
        const auto box = juce::Rectangle<float> (area.getX() + 6.0f, area.getY() + 12.0f,
                                                 juce::GlyphArrangement::getStringWidth (font, caption) + 10.0f, 14.0f);
        g.setColour (juce::Colour (0xff060407).withAlpha (0.8f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (ParagonColours::textDim);
        g.setFont (font);
        g.drawText (caption, box, juce::Justification::centred, false);
    }
    if (shown.points.empty())
        return;

    // The loop, shaded, and SUSTAIN, marked.
    const auto valid = [this] (int i) { return i >= 0 && i < (int) shown.points.size(); };
    if (envelope && valid (shown.loopStart) && valid (shown.loopEnd) && shown.loopEnd > shown.loopStart)
    {
        const auto x0 = pixelX (shown.points[(size_t) shown.loopStart].x), x1 = pixelX (shown.points[(size_t) shown.loopEnd].x);
        g.setColour (ParagonColours::accent.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, area.getY(), x1, area.getBottom()));
        g.setColour (ParagonColours::accent.withAlpha (0.45f));
        g.drawVerticalLine (juce::roundToInt (x0), area.getY(), area.getBottom());
        g.drawVerticalLine (juce::roundToInt (x1), area.getY(), area.getBottom());
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("L1", juce::Rectangle<float> (x0 + 3.0f, area.getY(), 20.0f, 12.0f), juce::Justification::centredLeft, false);
        g.drawText ("L2", juce::Rectangle<float> (x1 + 3.0f, area.getY(), 20.0f, 12.0f), juce::Justification::centredLeft, false);
    }
    if (envelope && valid (shown.sustain))
    {
        const auto x = pixelX (shown.points[(size_t) shown.sustain].x);
        g.setColour (ParagonColours::text.withAlpha (0.55f));
        const float dashes[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ x, area.getY(), x, area.getBottom() }, dashes, 2, 1.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("S", juce::Rectangle<float> (x + 3.0f, area.getY() + 13.0f, 12.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    // The curve, through every point.
    const auto shape = shown.toShape();
    juce::Path path;
    const auto count = juce::jmax (2, (int) area.getWidth());
    const auto end = envelope ? shape.length() : 1.0f;
    for (int i = 0; i < count; ++i)
    {
        const auto x = length * (float) i / (float) (count - 1);
        const juce::Point<float> p (area.getX() + (float) i, pixelY (shape.valueAt (juce::jmin (x, end))));
        if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        if (envelope && x >= end)
            break;
    }
    DisplayStyle::drawCurve (g, path, area, pixelY (middle), true);

    for (int i = 0; i < (int) shown.points.size(); ++i)
    {
        const auto at = pointPosition (i);
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillEllipse (juce::Rectangle<float> (pointRadius * 2.0f + 2.0f, pointRadius * 2.0f + 2.0f).withCentre (at));
        g.setColour (i == dragging ? ParagonColours::text : ParagonColours::accent);
        g.drawEllipse (juce::Rectangle<float> (pointRadius * 2.0f, pointRadius * 2.0f).withCentre (at), 2.0f);
    }
}

void MultiPointEditor::mouseDown (const juce::MouseEvent& e)
{
    shown = getShape();
    lockedLength = viewLength();
    const auto near = pointNear (e.position);
    if (e.mods.isPopupMenu())
    {
        if (near < 0 && kind == Kind::drumEnvelope)
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Flat (no change)");
            menu.addItem (2, "Back to front");
            menu.addItem (3, "Upside down");
            menu.addSeparator();
            menu.addItem (4, "Fade in");
            menu.addItem (5, "Fade out");
            menu.addItem (6, "Gate (cut halfway)");
            menu.addItem (7, "Swell");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                                [safe = juce::Component::SafePointer<MultiPointEditor> (this)] (int result)
                                {
                                    if (safe != nullptr && result > 0)
                                        safe->applyDrumShape (result);
                                });
            return;
        }
        if (near < 0)
            return;
        juce::PopupMenu menu;
        const auto envelope = ! isCycle();
        const auto canDelete = (int) shown.points.size() > 2 && ! (isCycle() && (near == 0 || near == (int) shown.points.size() - 1)) && near != 0;
        menu.addItem (1, "Delete this point", canDelete);
        if (envelope)
        {
            menu.addSeparator();
            menu.addItem (2, "SUSTAIN here", true, shown.sustain == near);
            menu.addItem (3, "LOOP start (L1) here", near < (int) shown.points.size() - 1, shown.loopStart == near);
            menu.addItem (4, "LOOP end (L2) here", near > 0, shown.loopEnd == near);
            menu.addItem (5, "No SUSTAIN", shown.sustain >= 0);
            menu.addItem (6, "No LOOP", shown.loopStart >= 0);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safe = juce::Component::SafePointer<MultiPointEditor> (this), near] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                auto shape = safe->getShape();
                                const auto fix = [&shape] (int& flag, int removed) { if (flag == removed) flag = -1; else if (flag > removed) --flag; };
                                switch (result)
                                {
                                    case 1:
                                        shape.points.erase (shape.points.begin() + near);
                                        fix (shape.sustain, near);
                                        fix (shape.loopStart, near);
                                        fix (shape.loopEnd, near);
                                        if (shape.loopStart < 0 || shape.loopEnd <= shape.loopStart)
                                            shape.loopStart = shape.loopEnd = -1;
                                        break;
                                    case 2: shape.sustain = near; break;
                                    case 3: shape.loopStart = near; if (shape.loopEnd <= near) shape.loopEnd = near + 1; break;
                                    case 4: shape.loopEnd = near; if (shape.loopStart < 0 || shape.loopStart >= near) shape.loopStart = near - 1; break;
                                    case 5: shape.sustain = -1; break;
                                    case 6: shape.loopStart = shape.loopEnd = -1; break;
                                    default: break;
                                }
                                safe->lockedLength = 0.0f;
                                safe->setShape (shape, true);
                            });
        return;
    }
    if (near >= 0)
    {
        dragging = near;
        return;
    }
    // On a line: curve it.
    const auto x = xAt (e.position.x);
    for (int i = 0; i + 1 < (int) shown.points.size(); ++i)
        if (x >= shown.points[(size_t) i].x && x <= shown.points[(size_t) i + 1].x)
        {
            curving = i;
            dragStartCurve = shown.points[(size_t) i].curve;
            dragStartY = e.position.y;
            break;
        }
}

void MultiPointEditor::applyDrumShape (int choice)
{
    const auto current = getShape();
    ModShapeData shape;
    switch (choice)
    {
        case 1: shape = ModShapeData::flatDrumEnvelope(); break;
        case 2: shape = current.mirrored(); break;
        case 3: shape = current.flipped(); break;
        case 4: shape = { { { 0.0f, 0.0f, 0.4f }, { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 }; break;
        case 5: shape = { { { 0.0f, 1.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 }; break;
        case 6: shape = { { { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.52f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 }; break;
        case 7: shape = { { { 0.0f, 0.15f, 0.6f }, { 0.85f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 }; break;
        default: return;
    }
    setShape (shape, true);
}

void MultiPointEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
    {
        auto shape = shown;
        auto& p = shape.points[(size_t) dragging];
        const auto last = (int) shape.points.size() - 1;
        float x = xAt (e.position.x);
        const auto before = dragging > 0 ? shape.points[(size_t) dragging - 1].x : 0.0f;
        const auto after = dragging < last ? shape.points[(size_t) dragging + 1].x : (isCycle() ? 1.0f : maxEnvelopeLength);
        x = juce::jlimit (before, after, x);
        if (dragging == 0)
            x = 0.0f;                                   // the start stays at the start
        if (isCycle() && dragging == last)
            x = 1.0f;                                   // and a cycle ends at its end
        p.x = x;
        p.y = yAt (e.position.y);
        if (kind == Kind::lfoShape && (dragging == 0 || dragging == last))   // (TRAVEL's needn't: it can end anywhere)
            shape.points[dragging == 0 ? (size_t) last : 0].y = p.y;   // the cycle's ends meet, so it doesn't click
        setShape (shape, false);
    }
    else if (curving >= 0)
    {
        auto shape = shown;
        // Up bends towards the later point's level early (a curve below 0); the drag's direction follows the line's.
        const auto& a = shape.points[(size_t) curving];
        const auto& b = shape.points[(size_t) curving + 1];
        const auto rising = b.y >= a.y ? 1.0f : -1.0f;
        shape.points[(size_t) curving].curve = juce::jlimit (-1.0f, 1.0f, dragStartCurve + rising * (e.position.y - dragStartY) / 120.0f);
        setShape (shape, false);
    }
}

void MultiPointEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 || curving >= 0)
        processor.modMatrixEditFinished();
    dragging = curving = -1;
    lockedLength = 0.0f;
    repaint();
}

void MultiPointEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto shape = getShape();
    shown = shape;
    const auto near = pointNear (e.position);
    if (near >= 0)
    {
        // Delete it (keeping at least two, the first, and an LFO shape's last).
        if ((int) shape.points.size() <= 2 || near == 0 || (isCycle() && near == (int) shape.points.size() - 1))
            return;
        shape.points.erase (shape.points.begin() + near);
        for (auto* flag : { &shape.sustain, &shape.loopStart, &shape.loopEnd })
            *flag = *flag == near ? -1 : (*flag > near ? *flag - 1 : *flag);
        if (shape.loopStart < 0 || shape.loopEnd <= shape.loopStart)
            shape.loopStart = shape.loopEnd = -1;
        setShape (shape, true);
        return;
    }
    if ((int) shape.points.size() >= synth::mod::maxShapePoints)
        return;
    // Add one where clicked (after the last point too, for an envelope).
    const auto x = juce::jmin (xAt (e.position.x), isCycle() ? 1.0f : maxEnvelopeLength);
    const auto y = yAt (e.position.y);
    auto at = (int) shape.points.size();
    for (int i = 0; i < (int) shape.points.size(); ++i)
        if (shape.points[(size_t) i].x > x)
        {
            at = i;
            break;
        }
    if (at == 0 || (isCycle() && at == (int) shape.points.size()))
        return;
    shape.points.insert (shape.points.begin() + at, { x, y, 0.0f });
    for (auto* flag : { &shape.sustain, &shape.loopStart, &shape.loopEnd })
        if (*flag >= at)
            ++*flag;
    setShape (shape, true);
}
