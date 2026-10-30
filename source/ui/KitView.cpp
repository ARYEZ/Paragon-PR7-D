#include "ui/KitView.h"

#include "KitImages.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr const char* pieceNames[KitView::numPieces] = { "kick", "snare", "tom1", "tom2", "floor", "hihat",
                                                              "crash", "crash2", "ride", "china", "splash", "tambourine" };

    // Each slot's piece (synth::drums::slotDefaults' order): BD, RIM, SD, CLP, SD2, LT, CH, MT, PH, HT, OH, CY,
    // RD, RB, CR2, CHN, SPL, HH, TMB, SHK.
    constexpr int slotPieces[synth::drums::numSlots] = { 0, 1, 1, -1, 1, 4, 5, 3, 5, 2, 5, 6, 8, 8, 7, 9, 10, 5, 11, -1 };

    constexpr float chosenGlow = 0.3f, hoverGlow = 0.18f;

    // The family's colours only: any hue outside 318 to 42 degrees (through red) turned to the nearer end, its
    // saturation and brightness kept. The renders were made that way; this undoes what JPEG moves (a few degrees, and
    // ringing beside bright glints), once, as the pictures load.
    void familyHues (juce::Image& image)
    {
        juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                const auto hue = c.getHue() * 360.0f;
                if (c.getSaturation() <= 0.0f || hue <= 42.0f || hue >= 318.0f)
                    continue;
                const auto to = hue < 180.0f ? 40.0f : 320.0f;
                pixels.setPixelColour (x, y, juce::Colour::fromHSV (to / 360.0f, c.getSaturation(), c.getBrightness(), c.getFloatAlpha()));
            }
    }

    juce::Image load (const char* name)
    {
        int size = 0;
        if (const auto* data = KitImages::getNamedResource (name, size))
            if (auto image = juce::ImageFileFormat::loadFrom (data, (size_t) size); image.isValid())
            {
                image = image.convertedToFormat (image.hasAlphaChannel() ? juce::Image::ARGB : juce::Image::RGB);
                familyHues (image);
                return image;
            }
        return {};
    }
}

const char* KitView::pieceName (int piece) noexcept
{
    return piece >= 0 && piece < numPieces ? pieceNames[piece] : "";
}

int KitView::pieceOf (int slot) noexcept
{
    return slot >= 0 && slot < synth::drums::numSlots ? slotPieces[slot] : -1;
}

const KitView::Pictures& KitView::pictures()
{
    static const Pictures loaded = []
    {
        Pictures p;
        p.kit = load ("kit_jpg");
        p.zones = load ("kitzones_png");
        // kitlayout.txt: a line per piece, "<name> <x> <y> <width> <height>", where its glow goes in the kit's pixels.
        int size = 0;
        const auto* text = KitImages::getNamedResource ("kitlayout_txt", size);
        const auto lines = juce::StringArray::fromLines (text != nullptr ? juce::String::fromUTF8 (text, size) : juce::String());
        for (const auto& line : lines)
        {
            const auto words = juce::StringArray::fromTokens (line, false);
            if (words.size() != 5)
                continue;
            for (int k = 0; k < numPieces; ++k)
                if (words[0] == pieceNames[k])
                {
                    p.litArea[(size_t) k] = { words[1].getIntValue(), words[2].getIntValue(), words[3].getIntValue(), words[4].getIntValue() };
                    p.lit[(size_t) k] = load (("lit" + juce::String (pieceNames[k]) + "_png").toRawUTF8());
                }
        }
        return p;
    }();
    return loaded;
}

KitView::KitView (ParagonX1AudioProcessor& p) : processor (p)
{
    setRepaintsOnMouseActivity (false);
}

juce::Rectangle<float> KitView::pictureArea() const
{
    const auto& kit = pictures().kit;
    if (! kit.isValid())
        return {};
    return juce::RectanglePlacement (juce::RectanglePlacement::centred)
        .appliedTo (kit.getBounds().toFloat(), getLocalBounds().toFloat());
}

int KitView::slotAt (juce::Point<float> at) const
{
    const auto& zones = pictures().zones;
    const auto area = pictureArea();
    if (! zones.isValid() || area.isEmpty() || ! area.contains (at))
        return -1;
    const auto scale = (float) zones.getWidth() / area.getWidth();
    const auto x = (int) ((at.x - area.getX()) * scale), y = (int) ((at.y - area.getY()) * scale);
    const auto zoneAt = [&zones] (int px, int py)
    {
        if (px < 0 || py < 0 || px >= zones.getWidth() || py >= zones.getHeight())
            return -1;
        return (int) zones.getPixelAt (px, py).getRed() - 1;
    };
    if (const auto exact = zoneAt (x, y); exact >= 0)
        return exact;
    // Thin parts (a hoop, a pedal) are easy to miss: the nearest part within 4 of the kit's pixels.
    auto best = -1;
    auto bestDistance = 1000;
    for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx)
            if (const auto z = zoneAt (x + dx, y + dy); z >= 0 && dx * dx + dy * dy < bestDistance)
            {
                best = z;
                bestDistance = dx * dx + dy * dy;
            }
    return best;
}

std::optional<juce::Point<float>> KitView::pointFor (int slot) const
{
    const auto& zones = pictures().zones;
    const auto area = pictureArea();
    if (! zones.isValid() || area.isEmpty())
        return {};
    // The middle of the slot's part: the zone pixel nearest the average of its pixels.
    double sx = 0.0, sy = 0.0;
    int count = 0;
    for (int y = 0; y < zones.getHeight(); ++y)
        for (int x = 0; x < zones.getWidth(); ++x)
            if ((int) zones.getPixelAt (x, y).getRed() - 1 == slot)
            {
                sx += x;
                sy += y;
                ++count;
            }
    if (count == 0)
        return {};
    const juce::Point<double> mean (sx / count, sy / count);
    juce::Point<int> best;
    auto bestDistance = 1.0e12;
    for (int y = 0; y < zones.getHeight(); ++y)
        for (int x = 0; x < zones.getWidth(); ++x)
            if ((int) zones.getPixelAt (x, y).getRed() - 1 == slot)
                if (const auto d = mean.getDistanceSquaredFrom ({ (double) x, (double) y }); d < bestDistance)
                {
                    bestDistance = d;
                    best = { x, y };
                }
    const auto scale = area.getWidth() / (float) zones.getWidth();
    return juce::Point<float> (area.getX() + ((float) best.x + 0.5f) * scale, area.getY() + ((float) best.y + 0.5f) * scale);
}

void KitView::click (juce::Point<float> at, juce::ModifierKeys mods)
{
    const auto slot = slotAt (at);
    if (slot < 0)
        return;
    if (onChoose)
        onChoose (slot, mods);
    processor.previewDrum (slot);
}

void KitView::setSelected (int slot)
{
    if (slot != selected)
    {
        selected = slot;
        repaint();
    }
}

void KitView::updateLights()
{
    const auto& kit = processor.getDrumKit();
    std::array<float, numPieces> now {};
    for (int slot = 0; slot < synth::drums::numSlots; ++slot)
    {
        const auto piece = pieceOf (slot);
        const auto age = kit.getHitAge (slot);
        if (piece >= 0 && age >= 0.0f && kit.isSounding (slot))
            now[(size_t) piece] = juce::jmax (now[(size_t) piece], (1.0f - age) * (1.0f - age));
    }
    for (int k = 0; k < numPieces; ++k)
        if (std::abs (now[(size_t) k] - glow[(size_t) k]) > 0.02f || (now[(size_t) k] <= 0.0f) != (glow[(size_t) k] <= 0.0f))
        {
            glow[(size_t) k] = now[(size_t) k];
            repaint (areaOf (k));   // only that piece
        }
}

juce::Rectangle<int> KitView::areaOf (int piece) const
{
    const auto& pics = pictures();
    const auto area = pictureArea();
    if (! pics.kit.isValid() || area.isEmpty())
        return {};
    const auto scale = area.getWidth() / (float) pics.kit.getWidth();
    const auto at = pics.litArea[(size_t) piece].toFloat();
    return juce::Rectangle<float> (area.getX() + at.getX() * scale, area.getY() + at.getY() * scale, at.getWidth() * scale, at.getHeight() * scale)
        .getSmallestIntegerContainer().expanded (1);
}

void KitView::paint (juce::Graphics& g)
{
    const auto& pics = pictures();
    const auto area = pictureArea();
    g.setColour (ParagonColours::background);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    if (! pics.kit.isValid())
        return;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (pics.kit, area);
    const auto scale = area.getWidth() / (float) pics.kit.getWidth();
    for (int k = 0; k < numPieces; ++k)
    {
        auto level = glow[(size_t) k];
        if (pieceOf (selected) == k)
            level = juce::jmax (level, chosenGlow);
        if (pieceOf (hovered) == k)
            level = juce::jmax (level, hoverGlow);
        const auto& lit = pics.lit[(size_t) k];
        if (level <= 0.0f || ! lit.isValid())
            continue;
        const auto at = pics.litArea[(size_t) k].toFloat();
        g.setOpacity (juce::jmin (1.0f, level));
        g.drawImage (lit, { area.getX() + at.getX() * scale, area.getY() + at.getY() * scale, at.getWidth() * scale, at.getHeight() * scale });
    }
    g.setOpacity (1.0f);

    // The picture's edges fade into the panel, so it sits in it without a frame.
    const auto fade = juce::jmin (40.0f, area.getHeight() * 0.08f);
    const auto clear = ParagonColours::background.withAlpha (0.0f);
    const auto edge = [&g, clear] (juce::Rectangle<float> band, juce::Point<float> solid, juce::Point<float> gone)
    {
        g.setGradientFill (juce::ColourGradient (ParagonColours::background, solid, clear, gone, false));
        g.fillRect (band);
    };
    edge (area.withHeight (fade), area.getTopLeft(), area.getTopLeft().translated (0.0f, fade));
    edge (area.withTrimmedTop (area.getHeight() - fade), area.getBottomLeft(), area.getBottomLeft().translated (0.0f, -fade));
    edge (area.withWidth (fade), area.getTopLeft(), area.getTopLeft().translated (fade, 0.0f));
    edge (area.withTrimmedLeft (area.getWidth() - fade), area.getTopRight(), area.getTopRight().translated (-fade, 0.0f));

    // What's under the mouse (or chosen), at the bottom left.
    const auto shown = hovered >= 0 ? hovered : selected;
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (ParagonColours::text.withAlpha (0.9f));
    g.drawText (describe (shown), getLocalBounds().reduced (14, 10).removeFromBottom (18), juce::Justification::bottomLeft, false);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (ParagonColours::textDim);
    g.drawText ("CLICK A DRUM: CHOOSE IT AND HEAR IT", getLocalBounds().reduced (14, 10).removeFromBottom (18), juce::Justification::bottomRight, false);
}

juce::String KitView::describe (int slot) const
{
    if (slot < 0 || slot >= synth::drums::numSlots)
        return {};
    const auto home = processor.homeNote (slot);
    const auto kit = processor.getKitSettings();
    const auto model = (int) kit.slots[(size_t) slot].model;
    return juce::String (synth::drums::slotDefaults[(size_t) slot].key) + "  "
           + synth::drums::modelNames[model].toUpperCase()
           + (home >= 0 ? "  " + juce::MidiMessage::getMidiNoteName (home, true, true, 4) : juce::String());
}

void KitView::mouseDown (const juce::MouseEvent& e)
{
    click (e.position, e.mods);
}

void KitView::mouseMove (const juce::MouseEvent& e)
{
    const auto slot = slotAt (e.position);
    if (slot != hovered)
    {
        hovered = slot;
        setMouseCursor (slot >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        setTooltip (slot >= 0 ? describe (slot) : juce::String());
        repaint();
    }
}

void KitView::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}
