#include "PerformanceControls.h"

#include "Parameters.h"
#include "ui/Finish.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/LedDisplay.h"
#include "ui/SlotWindow.h"


namespace
{
    constexpr float screenSize = 7.0f / 11.0f;   // the assign screen's frame and bezel, relative to the main screen's

    const char* pedalNames[] = { "VOLUME", "WAH / FILTER", "MORPH / FX", "PITCH / WHAMMY", "LO-FI", "WHAMMY-WAH", "GLITCH", "TALK BOX" };
    constexpr int lastTarget = ExpressionTargets::talkBox;
    static_assert (std::size (pedalNames) == (size_t) lastTarget && ExpressionAssign::numTargets == lastTarget);

    juce::RangedAudioParameter& parameter (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        return *state.getParameter (id);
    }

    // The dock screen's scroll buttons: a small dark key with an orange
    // arrow, dimmed when the list can't go further that way.
    class ScrollButton final : public juce::Button
    {
    public:
        explicit ScrollButton (bool pointsUp) : juce::Button (pointsUp ? "Scroll up" : "Scroll down"), up (pointsUp)
        {
            setTooltip (pointsUp ? "Scroll the list up" : "Scroll the list down");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (down ? ParagonColours::accentDim : juce::Colour (0xff151217));
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (highlighted && isEnabled() ? ParagonColours::accent.withAlpha (0.8f) : ParagonColours::panelEdge);
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

            const auto c = bounds.getCentre();
            const auto w = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.22f;
            juce::Path triangle;
            triangle.addTriangle (c.x - w, up ? c.y + w * 0.6f : c.y - w * 0.6f, c.x + w, up ? c.y + w * 0.6f : c.y - w * 0.6f,
                                  c.x, up ? c.y - w * 0.8f : c.y + w * 0.8f);
            g.setColour (ParagonColours::accent.withAlpha (isEnabled() ? 1.0f : 0.25f));
            g.fillPath (triangle);
        }

    private:
        bool up;
    };
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icons::Type iconType, const juce::String& buttonText,
                        juce::Justification textJustification)
    : juce::Button (name), icon (iconType), text (buttonText), justification (textJustification)
{
    setTooltip (name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();

    if (on || outlined || down)
    {
        g.setColour (on ? ParagonColours::accentDim.withAlpha (0.55f) : juce::Colour (0xff110e12));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (on ? ParagonColours::accent.withAlpha (0.8f) : ParagonColours::panelEdge);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    auto colour = on ? ParagonColours::accent : (highlighted ? ParagonColours::text : ParagonColours::text.withAlpha (0.85f));
    if (! isEnabled())
        colour = ParagonColours::textDim.withAlpha (0.45f);

    auto content = bounds.reduced (text.isEmpty() ? 4.0f : 8.0f, 0.0f);
    const auto iconSize = juce::jmin (18.0f, bounds.getHeight() * 0.5f);

    if (text.isEmpty())
    {
        Icons::draw (g, icon, content.withSizeKeepingCentre (iconSize, iconSize), colour, 1.6f, on);
        return;
    }

    const auto font = juce::FontOptions (juce::jmin (15.0f, bounds.getHeight() * 0.55f), on ? juce::Font::bold : juce::Font::plain);
    const auto textWidth = (float) juce::GlyphArrangement::getStringWidthInt (font, text);
    const auto gap = iconSize * 0.45f;

    // Centred: icon and text together in the middle. Otherwise from the left.
    auto row = justification == juce::Justification::centred
                 ? content.withSizeKeepingCentre (juce::jmin (content.getWidth(), iconSize + gap + textWidth), content.getHeight())
                 : content;
    Icons::draw (g, icon, row.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize), colour, 1.6f, on);
    row.removeFromLeft (gap);
    g.setColour (colour);
    g.setFont (font);
    g.drawText (text, row, juce::Justification::centredLeft, true);
}

//==============================================================================
ExpressionAssign::ExpressionAssign (juce::AudioProcessorValueTreeState& stateToUse, Style lookStyle)
    : style (lookStyle),
      state (stateToUse),
      attachment (parameter (stateToUse, ParamIDs::expressionTarget), [this] (float value)
      {
          currentTarget = juce::roundToInt (value);
          // Keep the chosen line on the screen.
          if (currentTarget != ExpressionTargets::off)
              firstRow = juce::jlimit (currentTarget - visibleRows, currentTarget - 1, firstRow);
          updateScrollButtons();
          repaint();
      })
{
    // The same screen in two looks, drawing its own menu: plain, and the
    // dock's, with buttons beside it to scroll the list. (The plain one sat
    // on the unit's face until PR7-B 0.7.0; the dock's is the one in use.)
    setTooltip ("Expression pedal assign. Click a line (or scroll) to choose what the pedal controls; "
                "click the highlighted line again to unassign it. The arrows show more of the list.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);   // follows the pedal's position

    if (style == Style::list)
    {
        scrollUp = std::make_unique<ScrollButton> (true);
        scrollDown = std::make_unique<ScrollButton> (false);
        scrollUp->onClick = [this] { scroll (-1); };
        scrollDown->onClick = [this] { scroll (1); };
        addAndMakeVisible (*scrollUp);
        addAndMakeVisible (*scrollDown);
    }

    attachment.sendInitialUpdate();
}

void ExpressionAssign::scroll (int lines)
{
    firstRow = juce::jlimit (0, numTargets - visibleRows, firstRow + lines);
    updateScrollButtons();
    repaint();
}

void ExpressionAssign::updateScrollButtons()
{
    if (scrollUp != nullptr)
    {
        scrollUp->setEnabled (firstRow > 0);
        scrollDown->setEnabled (firstRow + visibleRows < numTargets);
    }
}

void ExpressionAssign::select (int target)
{
    // Clicking the lit one again turns the pedal off.
    const auto newTarget = target == currentTarget ? ExpressionTargets::off : target;

    const auto set = [this] (const char* id, float plainValue)
    {
        auto* parameter = state.getParameter (id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    };

    // PITCH / WHAMMY bends the pitch shifter, so make sure there's one
    // running: switch the FILTER / PITCH block on as a pitch shifter.
    if (newTarget == ExpressionTargets::pitch)
    {
        set (ParamIDs::fpOn, 1.0f);
        set (ParamIDs::fpType, (float) effects::FilterPitch::Type::pitchShifter);
    }

    // WHAMMY-WAH plays the FILTER / PITCH block's Whammy-Wah type: switch it on as one.
    if (newTarget == ExpressionTargets::whammyWah)
    {
        set (ParamIDs::fpOn, 1.0f);
        set (ParamIDs::fpType, (float) effects::FilterPitch::Type::whammyWah);
    }

    // TALK BOX plays the FILTER / PITCH block's Talk Box type: switch it on as one.
    if (newTarget == ExpressionTargets::talkBox)
    {
        set (ParamIDs::fpOn, 1.0f);
        set (ParamIDs::fpType, (float) effects::FilterPitch::Type::talkBox);
    }

    // GLITCH fades the GLITCH block in, so switch it on.
    if (newTarget == ExpressionTargets::glitch)
        set (ParamIDs::glitchOn, 1.0f);

    // LO-FI fades the LO-FI block in, so switch it on.
    if (newTarget == ExpressionTargets::loFi)
        set (ParamIDs::lofiOn, 1.0f);

    attachment.setValueAsCompleteGesture ((float) newTarget);
}

void ExpressionAssign::timerCallback()
{
    // Repaint only when the position on the screen would change.
    if (juce::roundToInt (state.getRawParameterValue (ParamIDs::expression)->load() * 100.0f) != shownPercent)
        repaint();
}

juce::Rectangle<float> ExpressionAssign::screenArea() const
{
    // The dock's screen leaves room on its right for the scroll buttons.
    auto area = getLocalBounds().toFloat();
    if (style == Style::list)
        area.removeFromRight (scrollButtonWidth + 8.0f);
    return area;
}

juce::Rectangle<float> ExpressionAssign::screenGlass() const
{
    return screenArea().reduced (11.0f * screenSize);   // where LedDisplay::paintScreen puts the glass
}

juce::Rectangle<float> ExpressionAssign::scrollArrows() const
{
    // The heading line's right end: an up and a down arrow, side by side.
    return screenGlass().reduced (5.0f * scale, 4.0f * scale).removeFromTop (13.0f * scale).removeFromRight (20.0f * scale);
}

std::array<juce::Rectangle<float>, ExpressionAssign::visibleRows> ExpressionAssign::screenRows() const
{
    // Inside the glass: the EXPRESSION heading and a rule, the lines
    // showing (the list scrolls), then the position bar along the bottom.
    auto content = screenGlass().reduced (5.0f * scale, 4.0f * scale);
    content.removeFromTop (15.0f * scale);
    content.removeFromBottom (10.0f * scale);
    std::array<juce::Rectangle<float>, visibleRows> rows;
    const auto rowHeight = content.getHeight() / (float) rows.size();
    for (auto& row : rows)
        row = content.removeFromTop (rowHeight).reduced (0.0f, 0.5f);
    return rows;
}

void ExpressionAssign::paint (juce::Graphics& g)
{
    // A small version of the main screen: the same chrome frame, bezel and
    // orange-lit glass, showing a menu of what the pedal can control. Its
    // lettering is sized by `scale` (1 on the unit; bigger in the dock,
    // never smaller).
    const auto glass = LedDisplay::paintScreen (g, screenArea(), screenSize);
    const auto orange = LedDisplay::ledOrange;
    const auto assigned = currentTarget != ExpressionTargets::off;
    shownPercent = juce::roundToInt (state.getRawParameterValue (ParamIDs::expression)->load() * 100.0f);
    const auto position = shownPercent;
    const auto k = scale;

    auto content = glass.reduced (5.0f * k, 4.0f * k);
    auto header = content.removeFromTop (13.0f * k);
    g.setColour (orange);
    g.setFont (juce::FontOptions (10.0f * k, juce::Font::bold));
    g.drawFittedText ("EXPRESSION", header.withTrimmedRight (20.0f * k).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (orange.withAlpha (0.35f));
    g.fillRect (content.removeFromTop (juce::jmax (1.0f, k)));

    // Scroll arrows: lit when there's more of the list that way.
    {
        auto arrows = scrollArrows();
        const auto drawArrow = [&] (juce::Rectangle<float> area, bool up, bool more)
        {
            const auto c = area.getCentre();
            juce::Path triangle;
            triangle.addTriangle (c.x - 3.0f * k, up ? c.y + 2.0f * k : c.y - 2.0f * k, c.x + 3.0f * k, up ? c.y + 2.0f * k : c.y - 2.0f * k,
                                  c.x, up ? c.y - 2.5f * k : c.y + 2.5f * k);
            g.setColour (orange.withAlpha (more ? 0.9f : 0.2f));
            g.fillPath (triangle);
        };
        drawArrow (arrows.removeFromLeft (10.0f * k), true, firstRow > 0);
        drawArrow (arrows, false, firstRow + visibleRows < numTargets);
    }

    // The menu: the chosen line inverted (a solid orange bar, dark text),
    // the others dimmed so the choice stands out. With nothing chosen, all
    // of them show normally.
    const auto rows = screenRows();
    g.setFont (juce::FontOptions (8.5f * k, juce::Font::bold));
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto line = firstRow + (int) i;   // which target this line shows
        const auto selected = currentTarget == line + 1;
        if (selected)
        {
            g.setColour (orange);
            g.fillRoundedRectangle (rows[i], 2.0f * k);
            g.setColour (juce::Colour (0xff1c0b06));
        }
        else
        {
            g.setColour (orange.withAlpha (assigned ? 0.5f : 0.85f));
        }
        g.drawFittedText (pedalNames[line], rows[i].reduced (4.0f * k, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }

    // The pedal's position: a segmented bar like a level meter, and the
    // percentage beside it. Dark while the pedal isn't assigned, so it's
    // clear at a glance whether the pedal is doing anything.
    auto bar = content.removeFromBottom (8.0f * k);
    g.setColour (orange.withAlpha (assigned ? 0.85f : 0.3f));
    g.setFont (juce::FontOptions (8.5f * k, juce::Font::bold));
    g.drawText (juce::String (position) + "%", bar.removeFromRight (24.0f * k), juce::Justification::centredRight, false);
    bar.removeFromRight (3.0f * k);
    constexpr int segments = 10;
    const auto segmentWidth = bar.getWidth() / (float) segments;
    for (int i = 0; i < segments; ++i)
    {
        const auto lit = assigned && (float) i < (float) position / 100.0f * (float) segments - 0.001f;
        g.setColour (orange.withAlpha (lit ? 1.0f : 0.14f));
        g.fillRect (juce::Rectangle<float> (bar.getX() + (float) i * segmentWidth + 0.6f, bar.getY() + 1.0f, segmentWidth - 1.2f, bar.getHeight() - 2.0f));
    }

    LedDisplay::paintGlassOverlay (g, glass, screenSize);
}

void ExpressionAssign::resized()
{
    // The lettering grows with the screen's height (the unit's screen is
    // the reference size), and never shrinks below it.
    scale = juce::jmax (1.0f, (float) getHeight() / referenceHeight);

    if (scrollUp != nullptr)
    {
        auto column = getLocalBounds().removeFromRight ((int) scrollButtonWidth);
        const auto buttonHeight = juce::jmin (44, (column.getHeight() - 8) / 2);
        scrollUp->setBounds (column.withHeight (buttonHeight).withY (column.getCentreY() - buttonHeight - 4));
        scrollDown->setBounds (column.withHeight (buttonHeight).withY (column.getCentreY() + 4));
    }
}

void ExpressionAssign::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasClicked())
        return;

    // The arrows scroll the list; a line picks its target.
    if (auto arrows = scrollArrows().expanded (3.0f); arrows.contains (event.position))
    {
        scroll (event.position.x < arrows.getCentreX() ? -1 : 1);
        return;
    }

    const auto rows = screenRows();
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].withX (screenGlass().getX()).withWidth (screenGlass().getWidth()).contains (event.position))
            select (firstRow + (int) i + 1);
}

void ExpressionAssign::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // Scroll to step through the menu: down moves down the list.
    wheelTravel -= wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
    if (std::abs (wheelTravel) < 0.15f)
        return;

    const auto step = wheelTravel > 0.0f ? 1 : -1;
    wheelTravel = 0.0f;
    const auto next = juce::jlimit ((int) ExpressionTargets::volume, lastTarget,
                                    currentTarget == ExpressionTargets::off ? (int) ExpressionTargets::volume : currentTarget + step);
    if (next != currentTarget)
        select (next);
}

namespace
{
    // The pedal's layout, in units of the base's size (211 x 490, the
    // proportions of the mockup's pedal): the treadle covers the whole base,
    // just inside its chrome edge, as on a real floor unit, so the base only
    // shows where the treadle rocks away from it.
    constexpr float housingWidth = 211.0f, housingHeight = 490.0f;
    const juce::Rectangle<float> treadleInHousing { 4.0f, 4.0f, 203.0f, 482.0f };
    constexpr float baseCorner = 16.0f, treadleCorner = 13.0f;
    constexpr int treadleOversample = 2;   // the treadle's face is drawn at twice its size, so it stays sharp when rocked

    // The treadle's face, drawn once: a plate in the unit's own pearl
    // satin finish (white or black, see Finish.h), and two black rubber pads split by a diagonal band
    // (the mockup's design), ribbed across like the DigiTech RP6's. Drawn square and true,
    // so it rocks without any lean or warp.
    juce::Image makeTreadleFace (int w, int h)
    {
        juce::Image face (juce::Image::ARGB, w, h, true);
        juce::Graphics g (face);
        const auto bounds = face.getBounds().toFloat();
        const auto fw = (float) w, fh = (float) h;
        const auto corner = treadleCorner * fw / treadleInHousing.getWidth();

        juce::Path plate;
        plate.addRoundedRectangle (bounds.reduced (1.0f), corner);
        {
            // Pearl, its long edges curving away into shade.
            using Finish::pick;
            juce::ColourGradient pearl (pick (0xffe6d6de, 0xff070707), 0.0f, 0.0f, pick (0xffdccbd4, 0xff050505), fw, 0.0f, false);
            pearl.addColour (0.14, pick (0xfffffdfe, 0xff161415));
            pearl.addColour (0.5, pick (0xfff4ebf0, 0xff0a0a0a));
            pearl.addColour (0.86, pick (0xfffbf6f8, 0xff100f10));
            g.setGradientFill (pearl);
            g.fillPath (plate);

            // The same pearlescent shift as the chassis: rose to peach to lilac.
            const auto black = Finish::isBlack();
            juce::ColourGradient iridescence (black ? juce::Colour (0xffff6a9a).withAlpha (0.05f) : juce::Colour (0xffffc8dc).withAlpha (0.16f), 0.0f, 0.0f,
                                              black ? juce::Colour (0xffff4a6a).withAlpha (0.04f) : juce::Colour (0xffe2d0ff).withAlpha (0.14f), fw, fh, false);
            iridescence.addColour (0.5, black ? juce::Colour (0xffffb07a).withAlpha (0.025f) : juce::Colour (0xffffe0c8).withAlpha (0.12f));
            g.setGradientFill (iridescence);
            g.fillPath (plate);

            // The same satin grain as the chassis.
            const auto grain = Finish::makeSatinGrain (w, h, 7);
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plate);
            g.drawImageAt (grain, 0, 0);
        }

        // Its bevelled edge: lit along the top and left, darker at the bottom right.
        const auto black = Finish::isBlack();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (black ? 0.35f : 0.9f), bounds.getTopLeft(),
                                                 juce::Colour (black ? 0xff050405 : 0xffa88896), bounds.getBottomRight(), false));
        g.strokePath (plate, juce::PathStrokeType (fw * 0.012f));
        juce::Path seam;
        seam.addRoundedRectangle (bounds.reduced (fw * 0.03f), corner * 0.8f);
        g.setColour (black ? juce::Colours::black.withAlpha (0.5f) : juce::Colour (0xff4a2034).withAlpha (0.18f));
        g.strokePath (seam, juce::PathStrokeType (1.5f));

        // The two rubber pads, with the diagonal chrome band between them.
        const auto margin = fw * 0.075f;
        const auto addPad = [&] (std::initializer_list<juce::Point<float>> points)
        {
            juce::Path pad;
            auto first = true;
            for (auto point : points)
            {
                if (first)
                    pad.startNewSubPath (point);
                else
                    pad.lineTo (point);
                first = false;
            }
            pad.closeSubPath();
            pad = pad.createPathWithRoundedCorners (fw * 0.05f);

            // Set into the plate: a dark shadow line round it, deepest along the top.
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (pad, juce::PathStrokeType (fw * 0.014f), juce::AffineTransform::translation (0.0f, -1.0f));
            // Black rubber, or on the pearl black unit the neon of its keys,
            // lit at the top of each pad and shaded towards the bottom.
            const auto neon = Finish::neonKeys();
            if (neon)
            {
                g.setGradientFill (Finish::neonGradient (0.0f, fw));
                g.fillPath (pad);
                g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, pad.getBounds().getY(),
                                                         juce::Colours::black.withAlpha (0.4f), 0.0f, pad.getBounds().getBottom(), false));
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1618), 0.0f, pad.getBounds().getY(),
                                                         juce::Colour (0xff090708), 0.0f, pad.getBounds().getBottom(), false));
            }
            g.fillPath (pad);

            // Ribs across it, each with a lit top edge and a shadow under it.
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (pad);
            const auto pitch = fh * 0.0125f;
            for (auto y = pad.getBounds().getY() + pitch * 0.5f; y < pad.getBounds().getBottom(); y += pitch)
            {
                g.setColour (juce::Colours::white.withAlpha (neon ? 0.05f : 0.07f));
                g.fillRect (0.0f, y, fw, pitch * 0.18f);
                g.setColour (juce::Colours::black.withAlpha (neon ? 0.35f : 0.45f));
                g.fillRect (0.0f, y + pitch * 0.18f, fw, pitch * 0.22f);
            }

            // The unit's red glow caught along the pad's edge.
            g.setColour (ParagonColours::glow.withAlpha (0.28f));
            g.strokePath (pad, juce::PathStrokeType (1.2f));
        };
        addPad ({ { margin, margin }, { fw - margin, margin }, { fw - margin, fh * 0.2f }, { margin, fh * 0.47f } });
        addPad ({ { fw - margin, fh * 0.28f }, { fw - margin, fh * 0.865f }, { margin, fh * 0.865f }, { margin, fh * 0.55f } });

        return face;
    }
}

//==============================================================================
ExpressionPedal::ExpressionPedal (ParagonX1AudioProcessor& processorToUse)
    : processor (processorToUse),
      attachment (parameter (processorToUse.getParameters(), ParamIDs::expression), [this] (float) { timerCallback(); })
{
    setTooltip ("Expression pedal. Drag up and down (or use the mouse wheel) to rock it from heel to toe; "
                "a MIDI expression pedal moves it too (CC 11, or the one chosen in SETTINGS). Assign it with the EXPRESSION buttons.");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    treadle = makeTreadleFace (juce::roundToInt (treadleInHousing.getWidth()) * treadleOversample,
                               juce::roundToInt (treadleInHousing.getHeight()) * treadleOversample);
    setRepaintsOnMouseActivity (true);   // shows the pedal's position while you hover
    startTimerHz (30);   // follows a MIDI pedal smoothly
    timerCallback();
    shownPosition = position;   // start where the pedal is, rather than gliding there
}

void ExpressionPedal::refinish()
{
    treadle = makeTreadleFace (treadle.getWidth(), treadle.getHeight());
    repaint();
}

void ExpressionPedal::timerCallback()
{
    const auto newPosition = processor.getExpressionPosition();
    const auto newTarget = (int) processor.getParameters().getRawParameterValue (ParamIDs::expressionTarget)->load();
    if (std::abs (newPosition - position) > 1.0e-4f || newTarget != target)
    {
        position = newPosition;
        target = newTarget;
        repaint();
    }

    // Glide the drawn treadle to the new position over a few frames (a
    // stepped MIDI pedal or a click then looks like a foot rocking it).
    if (std::abs (position - shownPosition) > 1.0e-3f)
    {
        shownPosition += (position - shownPosition) * 0.35f;
        if (std::abs (position - shownPosition) <= 1.0e-3f)
            shownPosition = position;
        repaint();
    }
}


juce::Rectangle<float> ExpressionPedal::housingArea() const
{
    // The housing fills the component's width and sits at its bottom; the
    // space above is headroom for the toe to rise into when the heel's down.
    const auto bounds = getLocalBounds().toFloat();
    return bounds.withTop (bounds.getBottom() - bounds.getWidth() * housingHeight / housingWidth);
}

juce::Rectangle<float> ExpressionPedal::trackArea() const
{
    // Where the treadle lies when it's flat.
    const auto housingBounds = housingArea();
    return (treadleInHousing * (housingBounds.getWidth() / housingWidth)) + housingBounds.getPosition();
}

void ExpressionPedal::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    //==========================================================================
    // The treadle, rocking in 3D the way Neural DSP and
    // IK Multimedia show theirs, and as a real floor unit's pedal does: it
    // covers the whole base and pivots on an axle across its middle. It's
    // kept within the base's footprint, so it never spills over the unit.
    // The rocking shows as one end dropping away (narrower and darker, the
    // base showing round it) and the other coming up (lit, its shadow
    // spreading across the base).
    //
    // World units are pixels. X runs across the pedal, Y along it (toe at
    // -Y, heel at +Y), Z up towards the camera.
    const auto flat = trackArea();
    const auto scale = housingArea().getWidth() / housingWidth;
    juce::Path base;
    base.addRoundedRectangle (housingArea().reduced (1.0f), baseCorner * scale);

    // The base: dark, with a chrome lip round its edge. It shows only round
    // the treadle's edge and where the treadle rocks away from it.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d171a), 0.0f, housingArea().getY(),
                                             juce::Colour (0xff0b090a), 0.0f, housingArea().getBottom(), false));
    g.fillPath (base);
    g.setGradientFill (juce::ColourGradient (ParagonColours::metalLight, housingArea().getTopLeft(),
                                             ParagonColours::metalDark.darker (0.4f), housingArea().getBottomRight(), false));
    g.strokePath (base, juce::PathStrokeType (2.0f * scale));

    constexpr float maxTilt = 0.16f;                                   // about 9 degrees each way
    const auto tilt = (shownPosition * 2.0f - 1.0f) * maxTilt;         // + = toe down
    const auto length = flat.getHeight(), width = flat.getWidth();
    const auto thickness = width * 0.07f;   // the treadle's body under its face
    const auto pivotZ = thickness * 1.2f;                              // axle height above the base
    const auto cameraZ = length * 3.0f;                                // camera height: how strong the perspective is
    const auto cameraY = length * 0.6f;                                // camera a little towards the player
    const auto centre = flat.getCentre();
    const auto topZ = pivotZ + thickness * 0.5f;
    const auto flatScale = (cameraZ - topZ) / cameraZ;                 // so a flat treadle lands exactly on the artwork's place

    struct Point3 { float x, y, z; };
    const auto rotate = [&] (Point3 p)
    {
        const auto c = std::cos (tilt), s = std::sin (tilt);
        const auto dz = p.z - pivotZ;
        return Point3 { p.x, p.y * c - dz * s, pivotZ + p.y * s + dz * c };
    };
    const auto project = [&] (Point3 p)
    {
        const auto k = cameraZ / (cameraZ - p.z) * flatScale;
        return juce::Point<float> (centre.x + p.x * k, centre.y + (p.y - cameraY) * k + cameraY * flatScale);
    };
    const auto corner = [&] (float u, float v, float z)
    {
        return project (rotate ({ (u - 0.5f) * width, (v - 0.5f) * length, z }));
    };

    // The treadle's outline (0-1 across and along it): square and true.
    const std::array<juce::Point<float>, 4> outline { juce::Point<float> { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

    // The same outline with the face's rounded corners, at height `z`, as
    // the camera sees it. The body under the face follows it too, so no
    // square corner pokes out past the rounded face.
    const auto roundedOutline = [&] (float z)
    {
        const auto ru = treadleCorner / treadleInHousing.getWidth(), rv = treadleCorner / treadleInHousing.getHeight();
        const std::array<std::pair<juce::Point<float>, float>, 4> corners { std::pair { juce::Point<float> { 1.0f - ru, rv }, -90.0f },
                                                                            { { 1.0f - ru, 1.0f - rv }, 0.0f },
                                                                            { { ru, 1.0f - rv }, 90.0f },
                                                                            { { ru, rv }, 180.0f } };
        juce::Path path;
        auto first = true;
        for (const auto& [centreUv, startDegrees] : corners)
            for (int step = 0; step <= 8; ++step)
            {
                const auto angle = juce::degreesToRadians (startDegrees + 90.0f * (float) step / 8.0f);
                const auto point = corner (centreUv.x + ru * std::cos (angle), centreUv.y + rv * std::sin (angle), z);
                if (first)
                    path.startNewSubPath (point);
                else
                    path.lineTo (point);
                first = false;
            }
        path.closeSubPath();
        return path;
    };

    {
        juce::Graphics::ScopedSaveState clipToBase (g);
        g.reduceClipRegion (base);

        // The treadle's shadow on the base, spreading under whichever end lifts.
        {
            juce::Path shadow;
            const auto onBase = [&] (float u, float v)
            {
                const auto p = rotate ({ (u - 0.5f) * width, (v - 0.5f) * length, pivotZ - thickness * 0.5f });
                return project ({ p.x + p.z * 0.1f, p.y + p.z * 0.3f, 0.0f });
            };
            shadow.startNewSubPath (onBase (outline[0].x, outline[0].y));
            for (size_t i = 1; i < outline.size(); ++i)
                shadow.lineTo (onBase (outline[i].x, outline[i].y));
            shadow.closeSubPath();
            juce::DropShadow (juce::Colours::black, 14, {}).drawForPath (g, shadow);
        }

        // The treadle's body under its face (its thickness): pearl, shaded
        // rose at the bottom and lit towards the top, built up in layers so its sides
        // follow the face's rounded corners.
        constexpr int layers = 8;
        for (int layer = 0; layer < layers; ++layer)
        {
            const auto t = (float) layer / (float) (layers - 1);
            g.setColour (Finish::pick (0xff7a5c6a, 0xff030303).interpolatedWith (Finish::pick (0xffeee3e9, 0xff181617), t));
            g.fillPath (roundedOutline (topZ - thickness * (1.0f - t)));
        }

        // Its face, drawn in thin horizontal slices: each slice is
        // placed and sized where the tilted treadle puts it, which bends the
        // whole picture through the perspective.
        const auto sourceHeight = treadle.getHeight();
        constexpr int sliceRows = 2;
        for (int row = 0; row < sourceHeight; row += sliceRows)
        {
            const auto rows = juce::jmin (sliceRows, sourceHeight - row);
            const auto v0 = (float) row / (float) sourceHeight, v1 = (float) (row + rows) / (float) sourceHeight;
            const auto topLeft = corner (0, v0, topZ), topRight = corner (1, v0, topZ);
            const auto bottomLeft = corner (0, v1, topZ), bottomRight = corner (1, v1, topZ);
            const auto x0 = (topLeft.x + bottomLeft.x) * 0.5f, x1 = (topRight.x + bottomRight.x) * 0.5f;
            const auto y0 = topLeft.y, y1 = bottomLeft.y + 0.6f;   // a hair of overlap hides the seams
            const auto slice = treadle.getClippedImage ({ 0, row, treadle.getWidth(), rows });
            g.drawImageTransformed (slice, juce::AffineTransform::scale ((x1 - x0) / (float) treadle.getWidth(), (y1 - y0) / (float) rows)
                                               .translated (x0, y0));
        }

        // Light from above and in front: the end tipped up towards it
        // brightens and the end tipped away darkens, along the treadle.
        {
            const auto face = roundedOutline (topZ);
            const auto amount = tilt / maxTilt;   // +1 toe down (heel up), -1 heel down (toe up)
            const auto toeEnd = corner (0.5f, 0.0f, topZ), heelEnd = corner (0.5f, 1.0f, topZ);
            // On the neon grips white light reads as a milky haze, so there
            // the raised end isn't lightened; the end tipping away still darkens.
            // (Transparent black, not transparent white: the gradient blends
            // between its ends, and from white it would pass through grey.)
            const auto lit = Finish::neonKeys() ? juce::Colours::transparentBlack
                                                : juce::Colours::white.withAlpha (0.10f * std::abs (amount));
            const auto shaded = Finish::isBlack() ? juce::Colours::black.withAlpha (0.45f * std::abs (amount))
                                                  : juce::Colour (0xff4a2034).withAlpha (0.28f * std::abs (amount));   // rose, not black, so the pearl doesn't go grey
            g.setGradientFill (juce::ColourGradient (amount < 0.0f ? lit : shaded, toeEnd,
                                                     amount < 0.0f ? shaded : lit, heelEnd, false));
            g.fillPath (face);
        }
    }

    // While you hover or drag: what the pedal controls and where it is.
    if (isMouseOverOrDragging())
    {
        const auto active = target != ExpressionTargets::off;
        const auto text = (active ? juce::String (pedalNames[juce::jlimit (0, (int) std::size (pedalNames) - 1, target - 1)]) : juce::String ("NOT ASSIGNED"))
                          + "  " + juce::String (juce::roundToInt (position * 100.0f)) + "%";
        const auto font = juce::FontOptions (11.0f, juce::Font::bold);
        const auto pill = juce::Rectangle<float> ((float) juce::GlyphArrangement::getStringWidthInt (font, text) + 18.0f, 20.0f)
                              .withCentre (flat.getCentre());
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (pill, 10.0f);
        g.setColour (active ? ParagonColours::accent : ParagonColours::text);
        g.setFont (font);
        g.drawText (text, pill, juce::Justification::centred, false);
    }
}

void ExpressionPedal::mouseDown (const juce::MouseEvent&)
{
    dragStartPosition = position;
    attachment.beginGesture();
}

void ExpressionPedal::mouseDrag (const juce::MouseEvent& event)
{
    // Dragging the full height of the well rocks it heel to toe.
    const auto travel = juce::jmax (1.0f, trackArea().getHeight());
    attachment.setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, dragStartPosition - (float) event.getDistanceFromDragStartY() / travel));
}

void ExpressionPedal::mouseUp (const juce::MouseEvent&)
{
    attachment.endGesture();
}

void ExpressionPedal::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    attachment.setValueAsCompleteGesture (juce::jlimit (0.0f, 1.0f, position + wheel.deltaY * 0.25f));
}

//==============================================================================
ExpressionPanel::ExpressionPanel (juce::AudioProcessorValueTreeState& state)
    : assign (state, ExpressionAssign::Style::list),
      minAttachment (parameter (state, ParamIDs::expressionMin), [this] (float value)
      {
          range.setMinValue (value, juce::dontSendNotification, true);
          updateRangeText();
      }),
      maxAttachment (parameter (state, ParamIDs::expressionMax), [this] (float value)
      {
          range.setMaxValue (value, juce::dontSendNotification, true);
          updateRangeText();
      })
{
    addAndMakeVisible (assign);

    range.setRange (0.0, 1.0, 0.01);
    range.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    range.setColour (juce::Slider::backgroundColourId, ParagonColours::panelEdge);
    range.setColour (juce::Slider::thumbColourId, ParagonColours::accent);
    range.setTooltip ("Where the pedal's heel (MIN) and toe (MAX) land, so it can sweep just part of the range.");

    // Each drag is one edit (and one undo step), whichever end moves.
    range.onDragStart = [this] { minAttachment.beginGesture(); maxAttachment.beginGesture(); };
    range.onValueChange = [this]
    {
        minAttachment.setValueAsPartOfGesture ((float) range.getMinValue());
        maxAttachment.setValueAsPartOfGesture ((float) range.getMaxValue());
    };
    range.onDragEnd = [this] { minAttachment.endGesture(); maxAttachment.endGesture(); };
    addAndMakeVisible (range);

    for (auto* label : { &minLabel, &maxLabel })
    {
        label->setFont (juce::FontOptions (13.0f));
        label->setColour (juce::Label::textColourId, ParagonColours::textDim);
        addAndMakeVisible (*label);
    }
    maxLabel.setJustificationType (juce::Justification::centredRight);

    minAttachment.sendInitialUpdate();
    maxAttachment.sendInitialUpdate();
}

void ExpressionPanel::updateRangeText()
{
    const auto percent = [] (double value) { return juce::String (juce::roundToInt (value * 100.0)) + "%"; };
    minLabel.setText ("MIN   " + percent (range.getMinValue()), juce::dontSendNotification);
    maxLabel.setText ("MAX   " + percent (range.getMaxValue()), juce::dontSendNotification);
}

void ExpressionPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (ParagonColours::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("EXPRESSION ASSIGN", getLocalBounds().reduced (18, 0).removeFromTop (44), juce::Justification::centredLeft, false);
}

void ExpressionPanel::resized()
{
    // The screen takes everything but one row along the bottom: MIN, the
    // range slider, MAX.
    auto area = getLocalBounds().reduced (16, 12);
    area.removeFromTop (34);
    auto bottom = area.removeFromBottom (28);
    area.removeFromBottom (6);
    assign.setBounds (area);

    minLabel.setBounds (bottom.removeFromLeft (76));
    maxLabel.setBounds (bottom.removeFromRight (86));
    range.setBounds (bottom);
}

//==============================================================================
TunerReader::TunerReader (ParagonX1AudioProcessor& processorToUse) : processor (processorToUse) {}

void TunerReader::setActive (bool shouldBeActive)
{
    if (shouldBeActive == isActive())
        return;

    if (shouldBeActive)
        startTimerHz (30);
    else
        stopTimer();
}

void TunerReader::timerCallback()
{
    // The processor does the listening (so the trace keeps going with the
    // window closed); this just passes its reading on.
    const auto& tuner = processor.getTuner().getReading();
    Reading reading;
    reading.hasNote = tuner.hasNote;
    reading.note = tuner.note;
    reading.frequency = tuner.frequency;
    if (onReading)
        onReading (reading);
}
