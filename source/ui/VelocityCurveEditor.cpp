#include "ui/VelocityCurveEditor.h"

#include "ui/ParagonLookAndFeel.h"

VelocityCurveEditor::VelocityCurveEditor()
{
    setTooltip ("How hard a hit comes in (along the bottom) against how hard it plays (up the side). Drag the end points "
                "in to limit what comes out; double-click the line to add a point, right-click one to remove it.");
}

juce::Rectangle<float> VelocityCurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (26.0f, 14.0f).withTrimmedBottom (8.0f).withTrimmedLeft (4.0f);
}

juce::Point<float> VelocityCurveEditor::toScreen (juce::Point<float> p) const
{
    const auto a = plotArea();
    return { a.getX() + p.x * a.getWidth(), a.getBottom() - p.y * a.getHeight() };
}

juce::Point<float> VelocityCurveEditor::fromScreen (juce::Point<float> p) const
{
    const auto a = plotArea();
    return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / juce::jmax (1.0f, a.getWidth())),
             juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / juce::jmax (1.0f, a.getHeight())) };
}

void VelocityCurveEditor::showVelocity (int velocityIn)
{
    if (velocityIn != lastIn)
    {
        lastIn = velocityIn;
        repaint();
    }
}

int VelocityCurveEditor::pointAt (juce::Point<float> at) const
{
    const auto c = curve();
    auto best = -1;
    auto bestDistance = 10.0f;
    for (int i = 0; i < c.count; ++i)
        if (const auto d = toScreen (c.points[(size_t) i]).getDistanceFrom (at); d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    return best;
}

void VelocityCurveEditor::paint (juce::Graphics& g)
{
    const auto c = dragging >= 0 ? dragged : curve();
    const auto a = plotArea();
    g.setColour (ParagonColours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);

    // A grid at 32, 64, 96 and the straight line (what changes nothing), faint.
    g.setColour (ParagonColours::panelEdge.withAlpha (0.7f));
    for (const auto v : { 32.0f, 64.0f, 96.0f })
    {
        const auto x = a.getX() + a.getWidth() * v / 127.0f, y = a.getBottom() - a.getHeight() * v / 127.0f;
        g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
    }
    g.drawLine ({ a.getBottomLeft(), a.getTopRight() }, 1.0f);
    g.drawRect (a, 1.0f);
    g.setFont (juce::FontOptions (9.5f));
    g.setColour (ParagonColours::textDim);
    g.drawText ("IN", a.withY (a.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centred, false);
    g.drawText ("0", juce::Rectangle<float> (a.getX() - 22.0f, a.getBottom() - 6.0f, 18.0f, 12.0f), juce::Justification::centredRight, false);
    g.drawText ("127", juce::Rectangle<float> (a.getX() - 24.0f, a.getY() - 6.0f, 20.0f, 12.0f), juce::Justification::centredRight, false);
    g.drawText ("OUT", juce::Rectangle<float> (a.getX() - 26.0f, a.getCentreY() - 6.0f, 22.0f, 12.0f), juce::Justification::centredRight, false);
    if (label.isNotEmpty())
        g.drawText (label, a.reduced (6.0f, 4.0f), juce::Justification::topLeft, false);

    // The curve: flat from the left edge to the first point (or nothing there with GATE), through the points, flat to
    // the right edge.
    juce::Path path;
    const auto steps = 128;
    auto started = false;
    for (int i = 0; i <= steps; ++i)
    {
        const auto in = (float) i / (float) steps;
        const auto out = c.apply (in);
        if (out < 0.0f)
        {
            started = false;
            continue;
        }
        const auto p = toScreen ({ in, out });
        if (! started)
            path.startNewSubPath (p);
        else
            path.lineTo (p);
        started = true;
    }
    g.setColour (ParagonColours::accent);
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // The points: the limits as bars of three dots, the rest round.
    for (int i = 0; i < c.count; ++i)
    {
        const auto p = toScreen (c.points[(size_t) i]);
        const auto limit = i == 0 || i == c.count - 1;
        g.setColour (i == dragging ? ParagonColours::text : ParagonColours::accent);
        if (limit)
            for (int d = -1; d <= 1; ++d)
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (p.translated (0.0f, (float) d * 5.0f)));
        else
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
    }
    // The last velocity that came in, on the line.
    if (lastIn >= 0)
        if (const auto out = c.apply ((float) lastIn / 127.0f); out >= 0.0f)
        {
            g.setColour (ParagonColours::text);
            g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (toScreen ({ (float) lastIn / 127.0f, out })), 1.5f);
        }
    // While dragging: in -> out.
    if (dragging >= 0)
    {
        const auto& q = c.points[(size_t) dragging];
        const auto text = juce::String (juce::roundToInt (q.x * 127.0f)) + " " + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")) + " "
                          + juce::String (juce::roundToInt (q.y * 127.0f));
        const auto p = toScreen (q);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (ParagonColours::text);
        g.drawText (text, juce::Rectangle<float> (p.x + 8.0f, p.y - 18.0f, 70.0f, 14.0f).constrainedWithin (a), juce::Justification::centredLeft, false);
    }
}

void VelocityCurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto index = pointAt (e.position);
    if (e.mods.isPopupMenu())
    {
        auto c = curve();
        if (index > 0 && index < c.count - 1)
        {
            c.remove (index);
            if (onChange)
                onChange (c, true);
            repaint();
        }
        return;
    }
    dragging = index;
    dragged = curve();
}

void VelocityCurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    dragged.move (dragging, fromScreen (e.position));
    if (onChange)
        onChange (dragged, false);
    repaint();
}

void VelocityCurveEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 && onChange)
        onChange (dragged, true);
    dragging = -1;
    repaint();
}

void VelocityCurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pointAt (e.position) >= 0)
        return;
    auto c = curve();
    if (c.add (fromScreen (e.position)) >= 0 && onChange)
        onChange (c, true);
    repaint();
}
