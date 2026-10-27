#include "SpectrumEditor.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    constexpr int bands = synth::SpectrumEdit::bands;
    constexpr float minDb = synth::SpectrumEdit::minDb, maxDb = synth::SpectrumEdit::maxDb;
}

SpectrumEditor::SpectrumEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Drag to draw how loud each harmonic of the wave is (SHIFT: a straight line). The faint bars are the wave before editing; "
                "at the bottom a harmonic is gone.");
    refreshPlain();
    startTimerHz (15);
}

void SpectrumEditor::setView (View newView)
{
    view = newView;
    repaint();
}

void SpectrumEditor::setOscillator (int index)
{
    oscillator = index;
    lastKey.clear();
    refreshPlain();
    repaint();
}

juce::String SpectrumEditor::intervalName (int h)
{
    // Octaves of the root, then each harmonic as the nearest interval above its octave.
    static const char* names[] = { "Root", "8va", "5th", "8va", "3rd", "5th", "b7", "8va", "2nd", "3rd", "#4", "5th",
                                   "b6", "b7", "7th", "8va", "b2", "2nd", "b3", "3rd", "4th", "#4", "#4", "5th" };
    return h >= 1 && h <= harmonicColumns ? juce::String (names[h - 1]) : juce::String ("Rest");
}

juce::Rectangle<float> SpectrumEditor::getPlotArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (14.0f).withTrimmedLeft (26.0f);
}

float SpectrumEditor::xForBand (int band) const
{
    const auto area = getPlotArea();
    switch (view)
    {
        case View::free:     return area.getX() + area.getWidth() * std::log2 ((float) band + 1.0f) / std::log2 ((float) bands);
        case View::bins:     return area.getX() + area.getWidth() * ((float) band + 0.5f) / (float) bands;
        case View::harmonic: return area.getX() + area.getWidth() * ((float) juce::jmin (band, harmonicColumns) + 0.5f) / (float) (harmonicColumns + 1);
    }
    return area.getX();
}

int SpectrumEditor::bandAt (float x) const
{
    const auto area = getPlotArea();
    const auto t = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    switch (view)
    {
        case View::free:     return juce::jlimit (0, bands - 1, juce::roundToInt (std::exp2 (t * std::log2 ((float) bands))) - 1);
        case View::bins:     return juce::jlimit (0, bands - 1, (int) (t * (float) bands));
        case View::harmonic: return juce::jlimit (0, harmonicColumns, (int) (t * (float) (harmonicColumns + 1)));
    }
    return 0;
}

float SpectrumEditor::dbAt (float y) const
{
    const auto area = getPlotArea();
    return juce::jlimit (minDb, maxDb, juce::jmap (y, area.getY(), area.getBottom(), maxDb, minDb));
}

float SpectrumEditor::yForDb (float db) const
{
    const auto area = getPlotArea();
    return juce::jmap (juce::jlimit (minDb, maxDb, db), maxDb, minDb, area.getY(), area.getBottom());
}

void SpectrumEditor::refreshPlain()
{
    // The unedited wave's harmonics (its first frame), redone when its wave or table changes.
    const auto source = processor.getPlainSource (oscillator);
    std::vector<float> key { (float) source.frameSize, (float) (reinterpret_cast<std::uintptr_t> (source.samples.get()) % 16777213u) };
    if (source.isValid())
        key.push_back (source.samples->front());
    if (key == lastKey)
        return;
    lastKey = key;
    plainDb.assign ((size_t) bands + 1, minDb);
    if (! source.isValid())
        return;
    const auto levels = synth::Wavetable::harmonicLevels (source.samples->data(), source.frameSize, bands);
    const auto loudest = *std::max_element (levels.begin() + 1, levels.end());
    for (size_t h = 1; h < levels.size(); ++h)
        plainDb[h] = loudest > 0.0f ? juce::jmax (minDb, juce::Decibels::gainToDecibels (levels[h] / loudest, minDb)) : minDb;
}

void SpectrumEditor::timerCallback()
{
    refreshPlain();
    repaint();
}

void SpectrumEditor::resized() {}

void SpectrumEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = getPlotArea();
    const auto& edit = processor.getSpectrum (oscillator);
    const auto value = [this] (const char* what)
    {
        return processor.getParameters().getRawParameterValue (ParamIDs::osc (oscillator, what))->load();
    };
    const auto sweep = value ("FftSweep");
    const auto flip = value ("FftFlip") > 0.5f;

    // dB lines.
    g.setFont (juce::FontOptions (9.5f));
    for (const auto db : { 24.0f, 12.0f, 0.0f, -12.0f, -24.0f, -36.0f })
    {
        const auto y = yForDb (db);
        g.setColour (ParagonColours::panelEdge.withAlpha (juce::exactlyEqual (db, 0.0f) ? 1.0f : 0.5f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (bounds.getX() + 2.0f, y - 6.0f, 28.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }
    g.setColour (ParagonColours::textDim);
    g.drawText ("off", juce::Rectangle<float> (bounds.getX() + 2.0f, area.getBottom() - 10.0f, 28.0f, 12.0f), juce::Justification::centredLeft, false);

    // Labels along the bottom.
    const auto labels = juce::Rectangle<float> (area.getX(), area.getBottom() + 1.0f, area.getWidth(), 13.0f);
    if (view == View::harmonic)
    {
        for (int column = 0; column <= harmonicColumns; ++column)
        {
            const auto x = xForBand (column);
            g.drawText (column < harmonicColumns ? intervalName (column + 1) : juce::String ("Rest"),
                        juce::Rectangle<float> (x - 14.0f, labels.getY(), 28.0f, labels.getHeight()), juce::Justification::centred, false);
        }
    }
    else
    {
        for (const auto h : { 1, 2, 4, 8, 16, 32, 64, 128, 256 })
        {
            const auto x = xForBand (h - 1);
            g.setColour (ParagonColours::panelEdge.withAlpha (0.5f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
            g.setColour (ParagonColours::textDim);
            g.drawText (juce::String (h), juce::Rectangle<float> (x - 14.0f, labels.getY(), 28.0f, labels.getHeight()), juce::Justification::centred, false);
        }
    }

    // The wave's own harmonics, faint; then what comes out (edited), brighter.
    const auto bandWidth = [&] (int band)
    {
        if (view == View::harmonic)
            return area.getWidth() / (float) (harmonicColumns + 1) * 0.7f;
        if (view == View::bins)
            return juce::jmax (1.0f, area.getWidth() / (float) bands - 0.5f);
        return juce::jmax (1.5f, (xForBand (juce::jmin (band + 1, bands - 1)) - xForBand (band)) * 0.6f);
    };
    const auto columns = view == View::harmonic ? harmonicColumns + 1 : bands;
    const auto lpHarmonic = value ("FftLp"), hpHarmonic = value ("FftHp");
    const auto filteredOut = [&] (int h)   // LP and HP's cut (drawn as if steep)
    {
        return (lpHarmonic < 511.5f && (float) h > lpHarmonic + 0.5f) || (hpHarmonic > 1.01f && (float) h <= hpHarmonic + 0.5f);
    };
    for (int column = 0; column < columns; ++column)
    {
        const auto band = column;
        const auto h = view == View::harmonic && column == harmonicColumns ? harmonicColumns + 1 : column + 1;
        const auto x = xForBand (band);
        const auto w = bandWidth (band);
        const auto plain = plainDb[(size_t) juce::jmin (h, bands)];
        if (plain > minDb + 0.5f)
        {
            g.setColour (ParagonColours::textDim.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (x - 0.5f * w, yForDb (plain), w, area.getBottom() - yForDb (plain)));
            const auto edited = plain + synth::spectrum::gainDbFor (edit, h, sweep, flip);
            if (edited > minDb + 0.5f && ! filteredOut (h))
            {
                g.setColour (ParagonColours::accent.withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (x - 0.25f * w, yForDb (edited), 0.5f * w, area.getBottom() - yForDb (edited)));
            }
        }
    }

    // The drawn curve (with SWEEP and FLIP), bright.
    juce::Path curve;
    for (int column = 0; column < columns; ++column)
    {
        const auto h = view == View::harmonic && column == harmonicColumns ? harmonicColumns + 1 : column + 1;
        const juce::Point<float> point (xForBand (column), yForDb (synth::spectrum::gainDbFor (edit, h, sweep, flip)));
        if (column == 0) curve.startNewSubPath (point); else curve.lineTo (point);
    }
    DisplayStyle::drawCurve (g, curve, area, yForDb (0.0f), true);

    // LP and HP's cutoffs.
    for (const auto& [id, name] : { std::pair { "FftLp", "LP" }, std::pair { "FftHp", "HP" } })
    {
        const auto harmonic = value (id);
        const auto active = juce::String (name) == "LP" ? harmonic < 511.5f : harmonic > 1.01f;
        if (! active || (view == View::harmonic && harmonic > (float) harmonicColumns + 1.0f))
            continue;
        const auto x = view == View::harmonic ? xForBand (juce::roundToInt (harmonic) - 1) : xForBand (juce::jmin (bands - 1, juce::roundToInt (harmonic) - 1));
        g.setColour (ParagonColours::text.withAlpha (0.7f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.drawText (name, juce::Rectangle<float> (x + 3.0f, area.getY(), 24.0f, 12.0f), juce::Justification::centredLeft, false);
    }
}

void SpectrumEditor::drawTo (juce::Point<float> point)
{
    auto edit = processor.getSpectrum (oscillator);
    const auto set = [&] (int from, int to, float dbFrom, float dbTo)
    {
        // Every band between two points, in a line (so a fast drag leaves no gaps).
        if (from > to)
        {
            std::swap (from, to);
            std::swap (dbFrom, dbTo);
        }
        for (int b = from; b <= to; ++b)
        {
            const auto t = to == from ? 1.0f : (float) (b - from) / (float) (to - from);
            const auto db = dbFrom + (dbTo - dbFrom) * t;
            if (view == View::harmonic && b == harmonicColumns)
                std::fill (edit.gainsDb.begin() + harmonicColumns, edit.gainsDb.end(), db);   // REST: all the rest
            else
                edit.gainsDb[(size_t) b] = db;
        }
    };
    const auto snapDb = [] (float db) { return db <= minDb + 1.0f ? minDb : db; };   // near the bottom: gone
    if (juce::ModifierKeys::currentModifiers.isShiftDown())
    {
        edit = startEdit;
        set (bandAt (dragStart.x), bandAt (point.x), snapDb (dbAt (dragStart.y)), snapDb (dbAt (point.y)));
    }
    else
        set (bandAt (lastPoint.x), bandAt (point.x), snapDb (dbAt (lastPoint.y)), snapDb (dbAt (point.y)));
    lastPoint = point;
    processor.setSpectrum (oscillator, edit);
    repaint();
}

void SpectrumEditor::mouseDown (const juce::MouseEvent& e)
{
    dragStart = lastPoint = e.position;
    startEdit = processor.getSpectrum (oscillator);
    drawTo (e.position);
}

void SpectrumEditor::mouseDrag (const juce::MouseEvent& e) { drawTo (e.position); }

void SpectrumEditor::mouseUp (const juce::MouseEvent&) { processor.spectrumEditFinished(); }
