#include "SynthDisplays.h"

#include "Parameters.h"
#include "dsp/synth/Engine.h"
#include "dsp/synth/OscModulation.h"
#include "dsp/synth/WaveBend.h"
#include "dsp/synth/WaveTables.h"
#include "ui/ParagonLookAndFeel.h"

#include <complex>

namespace DisplayStyle
{
    void drawScreen (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (juce::Colour (0xff060407));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);
    }

    void drawCurve (juce::Graphics& g, const juce::Path& curve, juce::Rectangle<float> area, float baseline, bool lit)
    {
        const auto gradient = ParagonLookAndFeel::menuHighlight (area);

        // A faint fill between the curve and the baseline.
        juce::Path fill (curve);
        fill.lineTo (curve.getCurrentPosition().x, baseline);
        fill.lineTo (area.getX(), baseline);
        fill.closeSubPath();
        auto fillGradient = gradient;
        fillGradient.multiplyOpacity (lit ? 0.18f : 0.06f);
        g.setGradientFill (fillGradient);
        g.fillPath (fill);

        auto glow = gradient;
        glow.multiplyOpacity (lit ? 0.25f : 0.08f);
        g.setGradientFill (glow);
        g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto line = gradient;
        line.multiplyOpacity (lit ? 1.0f : 0.35f);
        g.setGradientFill (line);
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
ParameterDisplay::ParameterDisplay (juce::AudioProcessorValueTreeState& stateToUse) : state (stateToUse)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

float ParameterDisplay::value (const juce::String& id) const
{
    const auto* raw = state.getRawParameterValue (id);
    jassert (raw != nullptr);
    return raw != nullptr ? raw->load() : 0.0f;
}

void ParameterDisplay::timerCallback()
{
    auto now = snapshot();
    if (now != last)
    {
        last = std::move (now);
        repaint();
    }
}

//==============================================================================
namespace
{
    float raw (const juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        const auto* value = state.getRawParameterValue (id);
        return value != nullptr ? value->load() : 0.0f;
    }

    // Oscillator `osc`'s SHAPE, SYNC, FM and AM settings, as the processor reads them.
    synth::OscSettings shapeOf (const juce::AudioProcessorValueTreeState& state, int osc)
    {
        synth::OscSettings s;
        const auto choice = [&] (const char* what, int count) { return juce::jlimit (0, count - 1, juce::roundToInt (raw (state, ParamIDs::osc (osc, what)))); };
        s.syncAmount = raw (state, ParamIDs::osc (osc, "SyncAmount"));
        s.syncMode = (synth::SyncMode) choice ("SyncMode", synth::syncModeNames.size());
        s.fmAmount = raw (state, ParamIDs::osc (osc, "FmAmount"));
        s.fmSource = choice ("FmSource", synth::fmSourceNames.size());
        s.amAmount = raw (state, ParamIDs::osc (osc, "AmAmount"));
        s.amShape = (synth::ModShape) choice ("AmShape", synth::modShapeNames.size());
        s.modRate = raw (state, ParamIDs::osc (osc, "ModRate"));
        s.modGrid = (synth::ModGrid) choice ("ModGrid", synth::modGridNames.size());
        s.skew = raw (state, ParamIDs::osc (osc, "Skew"));
        s.pinch = raw (state, ParamIDs::osc (osc, "Pinch"));
        s.formant = raw (state, ParamIDs::osc (osc, "Formant"));
        s.formantMode = (synth::FormantMode) juce::jlimit (0, synth::formantModeNames.size() - 1, juce::roundToInt (raw (state, ParamIDs::osc (osc, "FormantMode"))));
        s.crush = raw (state, ParamIDs::osc (osc, "Crush"));
        s.crushMode = (synth::CrushMode) juce::jlimit (0, synth::crushModeNames.size() - 1, juce::roundToInt (raw (state, ParamIDs::osc (osc, "CrushMode"))));
        return s;
    }

    // A wave as the oscillator plays it: bent by SHAPE, with SYNC (its
    // envelope left out), FM from a shape and AM, the modulator in step with
    // the wave. (Drawn, so its edges needn't be smoothed.)
    template <typename Read>
    float bentAt (const synth::OscSettings& s, const synth::WaveBend& bending, float phase, Read&& read)
    {
        namespace mod = synth::modulation;
        phase -= std::floor (phase);
        const auto ratio = mod::ratio (s.modRate, s.modGrid);
        auto modPhase = phase * ratio;
        modPhase -= std::floor (modPhase);
        const auto level = synth::tables::levelFor (220.0 * ratio, 48000.0);
        auto x = phase;
        if (s.hasFm() && s.fmSource < synth::fmFirstOscillator)
        {
            x += mod::fmDepth (s.fmAmount) * mod::shapeAt ((synth::ModShape) s.fmSource, modPhase, level);
            x -= std::floor (x);
        }
        const auto syncRatio = mod::syncRatio (s.syncAmount);
        const auto synced = [&] (float y)
        {
            if (! s.hasSync())
                return read (y);
            auto copy = read (y * syncRatio - std::floor (y * syncRatio));
            if (s.syncMode == synth::SyncMode::pureWindowed || s.syncMode == synth::SyncMode::addWindowed)
                copy *= mod::window (y);
            return s.syncMode == synth::SyncMode::add || s.syncMode == synth::SyncMode::addWindowed ? 0.5f * (read (y) + copy) : copy;
        };
        auto v = bending.stepped (x, synced);
        if (s.hasAm())
            v *= 1.0f - s.amAmount + s.amAmount * mod::shapeAt (s.amShape, modPhase, level);
        if (bending.levels > 0)
            v = (std::floor (v * (float) bending.levels) + 0.5f) / (float) bending.levels * bending.bitGain;
        return v;
    }

    const char* const shapeSettings[] = { "Skew", "Pinch", "Formant", "FormantMode", "Crush", "CrushMode",
                                          "SyncAmount", "SyncMode", "FmAmount", "FmSource", "AmAmount", "AmShape", "ModRate", "ModGrid" };
}

WaveDisplay::WaveDisplay (juce::AudioProcessorValueTreeState& s) : ParameterDisplay (s) {}

juce::String WaveDisplay::id (const char* what) const { return ParamIDs::osc (oscillator, what); }

void WaveDisplay::setOscillator (int index)
{
    oscillator = index;
    repaint();
}

std::vector<float> WaveDisplay::snapshot() const
{
    std::vector<float> values { (float) oscillator };
    for (const auto* what : { "On", "Wave", "Gain", "Width", "Invert", "Sub", "SubWave", "SubOct", "SubInvert", "Noise", "Phase", "PhaseRand",
                              "Table", "WtIndex", "WtMode", "WtFrames" })
        values.push_back (value (id (what)));
    for (const auto* what : shapeSettings)
        values.push_back (value (id (what)));
    // Which table (a file can be loaded, or the FFT editor used, without any parameter changing).
    if (juce::roundToInt (value (id ("Wave"))) >= (int) synth::OscWave::wavetable && tableFor)
        if (const auto table = tableFor (oscillator))
            values.push_back ((float) (reinterpret_cast<std::uintptr_t> (table.get()) % 16777213u));
    if (editedFor)
        if (const auto table = editedFor (oscillator))
            values.push_back ((float) (reinterpret_cast<std::uintptr_t> (table.get()) % 16777213u));
    return values;
}

void WaveDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = bounds.reduced (10.0f, 12.0f);
    const auto mid = area.getCentreY();

    g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (mid), area.getX(), area.getRight());
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * (float) i / 4.0f), area.getY(), area.getBottom());

    const auto on = value (id ("On")) > 0.5f;
    const auto wave = (synth::OscWave) juce::roundToInt (value (id ("Wave")));
    if (wave == synth::OscWave::wavetable || wave == synth::OscWave::freeform || (editedFor && editedFor (oscillator) != nullptr))
    {
        paintWavetable (g, bounds.reduced (10.0f, 8.0f), on);
        if (! on)
        {
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("OFF", bounds.reduced (8.0f), juce::Justification::topRight, false);
        }
        return;
    }
    const auto gain = value (id ("Gain"));
    const auto width = juce::jlimit (0.01f, 0.99f, value (id ("Width")));
    const auto invert = value (id ("Invert")) > 0.5f ? -1.0f : 1.0f;
    const auto sub = value (id ("Sub"));
    const auto subWave = (synth::SubWave) juce::roundToInt (value (id ("SubWave")));
    const auto subOctaves = juce::roundToInt (value (id ("SubOct"))) - 2;
    const auto subSign = value (id ("SubInvert")) > 0.5f ? -1.0f : 1.0f;
    const auto noise = value (id ("Noise"));

    // Read the same tables the oscillators play, at a level with a moderate
    // number of harmonics (as a note round A3 would), so what's drawn is what's heard.
    const auto level = synth::tables::levelFor (220.0, 48000.0);
    const auto read = [level] (synth::tables::Wave w, float phase) { return synth::tables::read (synth::tables::get (w, level), phase - std::floor (phase)); };
    const auto waveAt = [&] (float phase)
    {
        switch (wave)
        {
            case synth::OscWave::saw:      return read (synth::tables::Wave::saw, phase);
            case synth::OscWave::pulse:    return read (synth::tables::Wave::saw, phase) - read (synth::tables::Wave::saw, phase + width);
            case synth::OscWave::triangle: return read (synth::tables::Wave::triangle, phase);
            case synth::OscWave::sine:     return read (synth::tables::Wave::sine, phase);
            case synth::OscWave::wavetable:
            case synth::OscWave::freeform:  return 0.0f;   // drawn as tables (paintWavetable)
        }
        return 0.0f;
    };
    const auto subAt = [&] (float phase)
    {
        const auto p = phase * std::exp2 ((float) subOctaves);
        switch (subWave)
        {
            case synth::SubWave::sine:     return read (synth::tables::Wave::sine, p);
            case synth::SubWave::triangle: return read (synth::tables::Wave::triangle, p);
            case synth::SubWave::square:   return read (synth::tables::Wave::saw, p) - read (synth::tables::Wave::saw, p + 0.5f);
            case synth::SubWave::saw:      return read (synth::tables::Wave::saw, p);
        }
        return 0.0f;
    };

    const auto shape = shapeOf (state, oscillator);
    synth::WarpTable warp;
    warp.update (shape.skew, shape.pinch);
    const auto bending = synth::WaveBend::make (shape, warp);
    const auto points = juce::jmax (2, (int) area.getWidth());
    std::vector<float> main ((size_t) points), under ((size_t) points);
    auto peak = 1.0f;
    for (int i = 0; i < points; ++i)
    {
        const auto phase = (float) i / (float) (points - 1);
        main[(size_t) i] = (bending.active || shape.hasModulation() ? bentAt (shape, bending, phase * 0.9999f, waveAt) : waveAt (phase)) * gain * invert;
        under[(size_t) i] = subAt (phase) * sub * subSign * invert;
        peak = juce::jmax (peak, std::abs (main[(size_t) i] + under[(size_t) i]));
    }
    const auto scale = area.getHeight() * 0.5f / (peak * 1.1f);
    const auto y = [mid, scale] (float v) { return mid - v * scale; };

    // NOISE: a fuzz of fine jagged lines round the wave, as wide as the noise is loud.
    if (noise > 0.0f && on)
    {
        juce::Random random (7);
        juce::Path fuzz;
        for (int i = 0; i < points; i += 2)
        {
            const auto x = area.getX() + (float) i;
            const auto centre = y (main[(size_t) i] + under[(size_t) i]);
            const auto point = juce::Point<float> (x, centre + (random.nextFloat() * 2.0f - 1.0f) * juce::jmin (1.0f, noise) * scale * 0.6f);
            if (i == 0) fuzz.startNewSubPath (point); else fuzz.lineTo (point);
        }
        g.setColour (ParagonColours::accent.withAlpha (0.35f));
        g.strokePath (fuzz, juce::PathStrokeType (0.8f));
    }

    if (sub > 0.0f)
    {
        juce::Path subPath;
        for (int i = 0; i < points; ++i)
        {
            const auto point = juce::Point<float> (area.getX() + (float) i, y (under[(size_t) i]));
            if (i == 0) subPath.startNewSubPath (point); else subPath.lineTo (point);
        }
        g.setColour (ParagonColours::textDim.withAlpha (on ? 0.6f : 0.25f));
        g.strokePath (subPath, juce::PathStrokeType (1.2f));
    }

    juce::Path path;
    for (int i = 0; i < points; ++i)
    {
        const auto point = juce::Point<float> (area.getX() + (float) i, y (main[(size_t) i] + under[(size_t) i]));
        if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
    }
    DisplayStyle::drawCurve (g, path, area, mid, on);

    if (! on)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("OFF", bounds.reduced (8.0f), juce::Justification::topRight, false);
    }
}

void WaveDisplay::paintWavetable (juce::Graphics& g, juce::Rectangle<float> area, bool on) const
{
    // The FFT-edited copy, if there is one, else the wavetable.
    const auto edited = editedFor ? editedFor (oscillator) : nullptr;
    const auto table = edited != nullptr ? edited : tableFor ? tableFor (oscillator) : nullptr;
    if (table == nullptr || table->getNumFrames() == 0)
        return;

    const auto used = juce::jlimit (2, juce::jmax (2, table->getNumFrames()), juce::roundToInt (value (id ("WtFrames"))));
    const auto steppy = juce::roundToInt (value (id ("WtMode"))) == 1;
    const auto index = juce::jlimit (0.0f, 1.0f, value (id ("WtIndex")));
    const auto gain = value (id ("Gain")) * (value (id ("Invert")) > 0.5f ? -1.0f : 1.0f);
    const auto level = synth::tables::levelFor (220.0, 48000.0);   // as a note round A3 hears it

    // Each frame sits on its own plane: the first at the front (bottom left),
    // the last at the back (top right).
    const auto header = 16.0f;
    const auto stack = area.withTrimmedTop (header);
    const auto plane = [stack] (float depth)
    {
        const auto w = stack.getWidth() * 0.78f, h = stack.getHeight() * 0.62f;
        return juce::Rectangle<float> (stack.getX() + depth * (stack.getWidth() - w), stack.getBottom() - h - depth * (stack.getHeight() - h), w, h);
    };
    const auto curve = [&] (const auto& valueAt, juce::Rectangle<float> r)
    {
        juce::Path path;
        const auto points = juce::jmax (2, (int) r.getWidth());
        for (int i = 0; i < points; ++i)
        {
            const auto phase = (float) i / (float) (points - 1) * 0.9999f;
            const juce::Point<float> point (r.getX() + (float) i, r.getCentreY() - juce::jlimit (-1.2f, 1.2f, valueAt (phase) * gain) * r.getHeight() * 0.42f);
            if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        return path;
    };

    // The frames INDEX moves through (up to 40 of them drawn), faint, back to front.
    const auto shown = juce::jmin (used, 40, table->getNumFrames());
    for (int k = shown - 1; k >= 0; --k)
    {
        const auto depth = shown > 1 ? (float) k / (float) (shown - 1) : 0.0f;
        const auto frame = table->get (table->locate (depth, used, true).below, level);
        const auto path = curve ([frame] (float phase) { return synth::Wavetable::read (frame, phase); }, plane (depth));
        g.setColour (ParagonColours::accent.withAlpha ((on ? 0.10f : 0.05f) + (on ? 0.14f : 0.05f) * (1.0f - depth)));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    // Where INDEX is: the wave as heard, bright.
    const auto position = table->locate (index, used, steppy);
    const auto below = table->get (position.below, level), above = table->get (position.above, level);
    const auto between = position.between;
    const auto now = plane (index);
    const auto shape = shapeOf (state, oscillator);
    synth::WarpTable warp;
    warp.update (shape.skew, shape.pinch);
    const auto bending = synth::WaveBend::make (shape, warp);
    const auto plain = [&] (float phase)
    {
        const auto a = synth::Wavetable::read (below, phase);
        return a + (synth::Wavetable::read (above, phase) - a) * between;
    };
    const auto path = curve ([&] (float phase) { return bending.active || shape.hasModulation() ? bentAt (shape, bending, phase, plain) : plain (phase); }, now);
    DisplayStyle::drawCurve (g, path, now, now.getCentreY(), on);

    // The table's name, and which frame.
    const auto frameNumber = juce::roundToInt ((float) position.below + between * (float) (position.above - position.below)) + 1;
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (ParagonColours::text.withAlpha (on ? 0.85f : 0.4f));
    g.drawText (table->getName().toUpperCase() + (edited != nullptr ? "  (FFT)" : ""), area.withHeight (header), juce::Justification::topLeft, true);
    g.setColour (ParagonColours::textDim.withAlpha (on ? 0.9f : 0.4f));
    g.drawText ("FRAME " + juce::String (frameNumber) + " / " + juce::String (table->getNumFrames()), area.withHeight (header).withTrimmedRight (on ? 0.0f : 34.0f),
                juce::Justification::topRight, false);
}

//==============================================================================
ShapeDisplay::ShapeDisplay (juce::AudioProcessorValueTreeState& s) : ParameterDisplay (s) {}

void ShapeDisplay::setOscillator (int index)
{
    oscillator = index;
    repaint();
}

std::vector<float> ShapeDisplay::snapshot() const
{
    std::vector<float> values { (float) oscillator };
    for (const auto* what : { "On", "Wave", "Width", "Invert", "Table", "WtIndex", "WtMode", "WtFrames" })
        values.push_back (value (ParamIDs::osc (oscillator, what)));
    for (const auto* what : shapeSettings)
        values.push_back (value (ParamIDs::osc (oscillator, what)));
    if (tableFor)
        if (const auto table = tableFor (oscillator))
            values.push_back ((float) (reinterpret_cast<std::uintptr_t> (table.get()) % 16777213u));
    return values;
}

void ShapeDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = bounds.reduced (14.0f, 22.0f);
    const auto mid = area.getCentreY();
    const auto id = [this] (const char* what) { return ParamIDs::osc (oscillator, what); };

    g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (mid), area.getX(), area.getRight());
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), area.getBottom());

    // The plain wave, as the oscillator reads it (round A3).
    const auto level = synth::tables::levelFor (220.0, 48000.0);
    const auto wave = (synth::OscWave) juce::roundToInt (value (id ("Wave")));
    const auto width = juce::jlimit (0.01f, 0.99f, value (id ("Width")));
    const auto sign = value (id ("Invert")) > 0.5f ? -1.0f : 1.0f;
    const auto basic = [level] (synth::tables::Wave w, float x) { return synth::tables::read (synth::tables::get (w, level), x - std::floor (x)); };
    std::shared_ptr<const synth::Wavetable> table;
    synth::Wavetable::Position position;
    if ((wave == synth::OscWave::wavetable || wave == synth::OscWave::freeform) && tableFor)
        if ((table = tableFor (oscillator)) != nullptr)
            position = table->locate (value (id ("WtIndex")), juce::roundToInt (value (id ("WtFrames"))), juce::roundToInt (value (id ("WtMode"))) == 1);
    const auto plain = [&] (float x) -> float
    {
        switch (wave)
        {
            case synth::OscWave::saw:      return basic (synth::tables::Wave::saw, x);
            case synth::OscWave::pulse:    return basic (synth::tables::Wave::saw, x) - basic (synth::tables::Wave::saw, x + width);
            case synth::OscWave::triangle: return basic (synth::tables::Wave::triangle, x);
            case synth::OscWave::sine:     return basic (synth::tables::Wave::sine, x);
            case synth::OscWave::wavetable:
            case synth::OscWave::freeform:
                if (table == nullptr)
                    return 0.0f;
                {
                    const auto a = synth::Wavetable::read (table->get (position.below, level), x);
                    return a + (synth::Wavetable::read (table->get (position.above, level), x) - a) * position.between;
                }
        }
        return 0.0f;
    };
    const auto shape = shapeOf (state, oscillator);
    synth::WarpTable warp;
    warp.update (shape.skew, shape.pinch);
    const auto bending = synth::WaveBend::make (shape, warp);
    const auto on = value (id ("On")) > 0.5f;

    // Two cycles of each.
    const auto points = juce::jmax (2, (int) area.getWidth());
    const auto trace = [&] (bool bent)
    {
        juce::Path path;
        for (int i = 0; i < points; ++i)
        {
            const auto phase = 2.0f * (float) i / (float) (points - 1) * 0.99995f;
            const auto x = phase - std::floor (phase);
            const auto v = (bent ? bentAt (shape, bending, x, plain) : plain (x)) * sign;
            const juce::Point<float> point (area.getX() + (float) i, mid - juce::jlimit (-1.25f, 1.25f, v) * area.getHeight() * 0.4f);
            if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        return path;
    };
    const auto changed = bending.active || shape.hasModulation();
    g.setColour (ParagonColours::textDim.withAlpha (changed ? 0.45f : 0.7f));
    g.strokePath (trace (false), juce::PathStrokeType (1.2f));
    if (changed)
        DisplayStyle::drawCurve (g, trace (true), area, mid, on);

    // What's what.
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const auto header = bounds.reduced (12.0f, 6.0f).withHeight (14.0f);
    g.setColour (ParagonColours::textDim);
    g.drawText ("PLAIN", header, juce::Justification::centredLeft, false);
    juce::StringArray notes;
    if (shape.hasFm() || shape.hasAm())
        notes.add ("RATE x" + juce::String (synth::modulation::ratio (shape.modRate, shape.modGrid), 2));
    if (shape.hasSync())
        notes.add ("SYNC x" + juce::String (synth::modulation::syncRatio (shape.syncAmount), 2) + " (its envelope not drawn)");
    if (shape.hasFm() && shape.fmSource >= synth::fmFirstOscillator)
        notes.add ("FM from " + synth::fmSourceNames[shape.fmSource] + ": not drawn");
    if (changed)
    {
        g.setColour (ParagonColours::accent);
        g.drawText ("RESULT", header.withTrimmedLeft (50.0f), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.drawText (notes.joinIntoString ("   "), header, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (ParagonColours::textDim);
        g.drawText ("Bend or modulate the wave and the result shows here", header, juce::Justification::centredRight, false);
    }
}

//==============================================================================
FilterDisplay::FilterDisplay (juce::AudioProcessorValueTreeState& s) : ParameterDisplay (s) {}

std::vector<float> FilterDisplay::snapshot() const
{
    return { value (ParamIDs::fltOn), value (ParamIDs::fltType), value (ParamIDs::fltCutoff), value (ParamIDs::fltRes) };
}

double FilterDisplay::responseDb (synth::FilterType type, double cutoffHz, double resonance, double hz)
{
    // The analog prototypes the filters are built on (the digital ones match
    // them closely below about 10 kHz at 48 kHz): s is frequency over cutoff.
    using C = std::complex<double>;
    const C s (0.0, hz / juce::jmax (1.0, cutoffHz));
    const auto q = 0.5 + 29.5 * std::pow (juce::jlimit (0.0, 1.0, resonance), 2.5);   // as FilterChannel::Coefficients::make
    const auto k = 1.0 / q, kFlat = std::sqrt (2.0);
    const auto denominator = s * s + k * s + 1.0;
    const auto flatDenominator = s * s + kFlat * s + 1.0;
    const auto onePole = 1.0 / (s + 1.0);

    C h (1.0, 0.0);
    switch (type)
    {
        case synth::FilterType::lp6:  h = onePole; break;
        case synth::FilterType::hp6:  h = s * onePole; break;
        case synth::FilterType::lp12: h = 1.0 / denominator; break;
        case synth::FilterType::hp12: h = s * s / denominator; break;
        case synth::FilterType::lp18: h = onePole / denominator; break;
        case synth::FilterType::hp18: h = s * s / denominator * s * onePole; break;
        case synth::FilterType::lp24: h = 1.0 / (denominator * flatDenominator); break;
        case synth::FilterType::hp24: h = s * s / denominator * s * s / flatDenominator; break;
        case synth::FilterType::bp12: h = k * s / denominator; break;
        case synth::FilterType::bp24: h = k * s / denominator * kFlat * s / flatDenominator; break;
        case synth::FilterType::bs12: h = (s * s + 1.0) / denominator; break;
        case synth::FilterType::bs24: h = (s * s + 1.0) / denominator * (s * s + 1.0) / flatDenominator; break;
    }
    return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h)));
}

void FilterDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = bounds.reduced (10.0f, 10.0f).withTrimmedBottom (12.0f);

    constexpr double minHz = 20.0, maxHz = 20000.0, topDb = 24.0, bottomDb = -36.0;
    const auto xFor = [&area] (double hz) { return area.getX() + area.getWidth() * (float) (std::log (hz / minHz) / std::log (maxHz / minHz)); };
    const auto yFor = [&area] (double db) { return area.getY() + area.getHeight() * (float) ((topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb)); };

    // Grid: octaves of decades, and 0 dB.
    g.setFont (juce::FontOptions (9.5f));
    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (xFor (hz)), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim.withAlpha (0.7f));
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ xFor (hz), area.getBottom() + 8.0f }), juce::Justification::centred, false);
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0)), area.getX(), area.getRight());

    const auto on = value (ParamIDs::fltOn) > 0.5f;
    const auto type = (synth::FilterType) juce::jlimit (0, synth::filterTypeNames.size() - 1, juce::roundToInt (value (ParamIDs::fltType)));
    const auto cutoff = (double) value (ParamIDs::fltCutoff);
    const auto resonance = (double) value (ParamIDs::fltRes);

    juce::Path path;
    const auto points = juce::jmax (2, (int) area.getWidth());
    for (int i = 0; i < points; ++i)
    {
        const auto hz = minHz * std::pow (maxHz / minHz, (double) i / (points - 1));
        const auto point = juce::Point<float> (area.getX() + (float) i, yFor (responseDb (type, cutoff, resonance, hz)));
        if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
    }
    DisplayStyle::drawCurve (g, path, area, area.getBottom(), on);

    // The cutoff, marked.
    const auto x = xFor (juce::jlimit (minHz, maxHz, cutoff));
    g.setColour (ParagonColours::accent.withAlpha (on ? 0.8f : 0.3f));
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, yFor (responseDb (type, cutoff, resonance, cutoff)) }));

    if (! on)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("OFF", bounds.reduced (8.0f), juce::Justification::topRight, false);
    }
}

//==============================================================================
ShaperDisplay::ShaperDisplay (juce::AudioProcessorValueTreeState& s) : ParameterDisplay (s) {}

void ShaperDisplay::setShaper (int index)
{
    shaper = juce::jlimit (0, ParamIDs::numShapers - 1, index);
    repaint();
}

float ShaperDisplay::shaperValue (const char* what) const { return value (ParamIDs::shp (shaper, what)); }

std::vector<float> ShaperDisplay::snapshot() const
{
    std::vector<float> values { (float) shaper };
    for (const auto* what : ParamIDs::shaperSettings)
        values.push_back (shaperValue (what));
    return values;
}

double ShaperDisplay::bellDb (double gainDb, double freqHz, double q, double hz)
{
    // The analog bell the EQ band is made from: (s^2 + s A / Q + 1) / (s^2 + s / (A Q) + 1).
    if (std::abs (gainDb) < 0.05)
        return 0.0;
    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto w = hz / freqHz;
    const std::complex<double> s (0.0, w);
    const auto h = (s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0);
    return 20.0 * std::log10 (std::abs (h));
}

void ShaperDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    auto inner = bounds.reduced (10.0f, 8.0f);
    const auto on = shaperValue ("On") > 0.5f;

    // Left: the curve, square.
    const auto curveArea = inner.removeFromLeft (inner.getHeight()).reduced (2.0f);
    inner.removeFromLeft (14.0f);
    const auto model = (synth::ShaperModel) juce::jlimit (0, synth::shaperModelNames.size() - 1, juce::roundToInt (shaperValue ("Model")));
    const auto gain = synth::shaper::driveGain (shaperValue ("Drive"));
    const auto makeUp = synth::shaper::makeUp (model, gain);
    const auto mix = shaperValue ("Mix");
    const auto output = juce::Decibels::decibelsToGain (shaperValue ("Output"));
    g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (curveArea.getCentreY()), curveArea.getX(), curveArea.getRight());
    g.drawVerticalLine (juce::roundToInt (curveArea.getCentreX()), curveArea.getY(), curveArea.getBottom());
    g.drawLine ({ curveArea.getBottomLeft(), curveArea.getTopRight() }, 1.0f);
    juce::Path curve;
    const auto points = juce::jmax (2, (int) curveArea.getWidth());
    for (int i = 0; i < points; ++i)
    {
        const auto x = -1.0 + 2.0 * i / (points - 1);
        const auto wet = synth::shaper::curve (model, x * gain) * makeUp;
        const auto y = juce::jlimit (-1.1, 1.1, (wet * mix + x * (1.0f - mix)) * output);
        const juce::Point<float> point (curveArea.getX() + (float) i, curveArea.getCentreY() - (float) y * curveArea.getHeight() * 0.5f);
        if (i == 0) curve.startNewSubPath (point); else curve.lineTo (point);
    }
    DisplayStyle::drawCurve (g, curve, curveArea, curveArea.getCentreY(), on);

    // Right: the EQ bands that colour what's distorted.
    const auto area = inner.withTrimmedBottom (12.0f);
    constexpr double minHz = 20.0, maxHz = 20000.0, rangeDb = 20.0;
    const auto xFor = [&area] (double hz) { return area.getX() + area.getWidth() * (float) (std::log (hz / minHz) / std::log (maxHz / minHz)); };
    const auto yFor = [&area] (double db) { return area.getCentreY() - area.getHeight() * 0.5f * (float) (juce::jlimit (-rangeDb, rangeDb, db) / rangeDb); };
    g.setFont (juce::FontOptions (9.5f));
    for (const auto hz : { 100.0, 1000.0, 10000.0 })
    {
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (xFor (hz)), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim.withAlpha (0.7f));
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ xFor (hz), area.getBottom() + 8.0f }), juce::Justification::centred, false);
    }
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0)), area.getX(), area.getRight());

    const std::array<double, 2> gains { shaperValue ("Eq1Gain"), shaperValue ("Eq2Gain") };
    const std::array<double, 2> freqs { shaperValue ("Eq1Freq"), shaperValue ("Eq2Freq") };
    const std::array<double, 2> qs { shaperValue ("Eq1Q"), shaperValue ("Eq2Q") };
    const auto split = shaperValue ("EqSplit") > 0.5f;
    const auto response = [&] (std::initializer_list<int> bands)
    {
        juce::Path path;
        const auto count = juce::jmax (2, (int) area.getWidth());
        for (int i = 0; i < count; ++i)
        {
            const auto hz = minHz * std::pow (maxHz / minHz, (double) i / (count - 1));
            double db = 0.0;
            for (const auto b : bands)
                db += bellDb (gains[(size_t) b], freqs[(size_t) b], qs[(size_t) b], hz);
            const juce::Point<float> point (area.getX() + (float) i, yFor (db));
            if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
        }
        return path;
    };
    if (split)
    {
        // EQ SPLIT: band 1 on the left channel, band 2 on the right, drawn apart.
        DisplayStyle::drawCurve (g, response ({ 0 }), area, yFor (0.0), on);
        g.setColour (ParagonColours::text.withAlpha (on ? 0.55f : 0.25f));
        g.strokePath (response ({ 1 }), juce::PathStrokeType (1.5f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (ParagonColours::textDim);
        g.drawText ("SPLIT: 1 LEFT, 2 RIGHT", area.reduced (4.0f, 2.0f), juce::Justification::topLeft, false);
    }
    else
        DisplayStyle::drawCurve (g, response ({ 0, 1 }), area, yFor (0.0), on);
    for (int b = 0; b < 2; ++b)
    {
        if (std::abs (gains[(size_t) b]) < 0.05)
            continue;
        const juce::Point<float> centre (xFor (juce::jlimit (minHz, maxHz, freqs[(size_t) b])), yFor (gains[(size_t) b]));
        g.setColour (ParagonColours::accent.withAlpha (on ? 0.85f : 0.3f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre));
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre), juce::Justification::centred, false);
    }

    if (! on)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("OFF", bounds.reduced (8.0f), juce::Justification::topRight, false);
    }
}

//==============================================================================
EnvelopeDisplay::EnvelopeDisplay (juce::AudioProcessorValueTreeState& s, std::array<juce::String, 8> idsToShow)
    : ParameterDisplay (s), ids (std::move (idsToShow)) {}

std::vector<float> EnvelopeDisplay::snapshot() const
{
    std::vector<float> values;
    for (const auto& id : ids)
        values.push_back (value (id));
    return values;
}

void EnvelopeDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = bounds.reduced (10.0f, 10.0f);

    const auto attack = value (ids[0]), hold = value (ids[1]), decay = value (ids[2]), sustain = value (ids[3]), release = value (ids[4]);
    const auto attackCurve = value (ids[5]), decayCurve = value (ids[6]), releaseCurve = value (ids[7]);

    // Squeezed time: each stage's width grows with the square root of its length.
    const auto width = [] (float ms) { return std::sqrt (juce::jmax (0.0f, ms)) + 2.0f; };
    const auto sustainWidth = width (600.0f);
    const auto total = width (attack) + width (hold) + width (decay) + sustainWidth + width (release);
    const auto scale = area.getWidth() / total;
    const auto yFor = [&area] (float level) { return area.getBottom() - level * area.getHeight(); };

    juce::Path path;
    auto x = area.getX();
    path.startNewSubPath (x, yFor (0.0f));
    const auto segment = [&] (float ms, float from, float to, float curve)
    {
        const auto w = width (ms) * scale;
        constexpr int steps = 40;
        for (int i = 1; i <= steps; ++i)
        {
            const auto p = (float) i / steps;
            path.lineTo (x + w * p, yFor (from + (to - from) * synth::bend (p, curve)));
        }
        x += w;
    };
    segment (attack, 0.0f, 1.0f, attackCurve);
    segment (hold, 1.0f, 1.0f, 0.0f);
    segment (decay, 1.0f, sustain, decayCurve);
    const auto releaseStart = x + sustainWidth * scale;
    path.lineTo (releaseStart, yFor (sustain));
    x = releaseStart;
    segment (release, sustain, 0.0f, releaseCurve);

    // The point where the key is let go, marked.
    g.setColour (ParagonColours::panelEdge);
    for (float y = area.getY(); y < area.getBottom(); y += 6.0f)
        g.drawVerticalLine (juce::roundToInt (releaseStart), y, y + 3.0f);

    DisplayStyle::drawCurve (g, path, area, area.getBottom());
}

//==============================================================================
VoicesDisplay::VoicesDisplay (juce::AudioProcessorValueTreeState& s) : ParameterDisplay (s) {}

juce::String VoicesDisplay::id (const char* what) const { return ParamIDs::osc (oscillator, what); }

void VoicesDisplay::setOscillator (int index)
{
    oscillator = index;
    repaint();
}

synth::OscSettings VoicesDisplay::settings() const
{
    synth::OscSettings o;
    o.on = value (id ("On")) > 0.5f;
    o.pan = value (id ("Pan"));
    o.unisonVoices = juce::roundToInt (value (id ("UniVoices")));
    o.unisonMix = value (id ("UniMix"));
    o.unisonDetune = value (id ("UniDetune"));
    o.unisonSpread = value (id ("UniSpread"));
    o.unisonOctaves = juce::roundToInt (value (id ("UniOctaves")));
    for (int k = 0; k < synth::numChordVoices; ++k)
    {
        const auto prefix = "Ch" + juce::String (k + 1);
        auto& chord = o.chord[(size_t) k];
        chord.on = value (id ((prefix + "On").toRawUTF8())) > 0.5f;
        chord.coarse = value (id ((prefix + "Coarse").toRawUTF8()));
        chord.fineCents = value (id ((prefix + "Fine").toRawUTF8()));
        chord.pan = value (id ((prefix + "Pan").toRawUTF8()));
        chord.volume = value (id ((prefix + "Vol").toRawUTF8()));
    }
    return o;
}

std::vector<float> VoicesDisplay::snapshot() const
{
    std::vector<float> values { (float) oscillator };
    for (const auto* what : ParamIDs::oscSettings)
        values.push_back (value (id (what)));
    return values;
}

std::vector<VoicesDisplay::Copy> VoicesDisplay::copiesFor (const synth::OscSettings& s)
{
    std::vector<Copy> copies;
    const auto unison = s.hasUnison() ? s.unisonVoices : 1;
    const auto detuneCents = s.unisonDetune * s.unisonDetune * synth::maxDetuneCents;
    for (int note = 0; note <= synth::numChordVoices; ++note)
    {
        const auto* chord = note > 0 ? &s.chord[(size_t) note - 1] : nullptr;
        if (chord != nullptr && ! chord->on)
            continue;
        const auto notePitch = chord != nullptr ? chord->coarse + chord->fineCents * 0.01f : 0.0f;
        for (int u = 0; u < unison; ++u)
        {
            const auto position = synth::Engine::unisonPosition (u, unison);
            const auto octave = u > 0 && s.unisonOctaves > 0 ? (float) ((u - 1) % (s.unisonOctaves + 1)) * 12.0f : 0.0f;
            copies.push_back ({ juce::jlimit (-1.0f, 1.0f, s.pan + (chord != nullptr ? chord->pan : 0.0f) + position * s.unisonSpread),
                                notePitch + octave + position * detuneCents * 0.01f,
                                (u == 0 ? 1.0f : s.unisonMix) * (chord != nullptr ? chord->volume : 1.0f),
                                note == 0 && u == 0 });
        }
    }
    return copies;
}

void VoicesDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    auto area = bounds.reduced (12.0f, 10.0f);
    const auto footer = area.removeFromBottom (14.0f);

    const auto s = settings();
    const auto copies = copiesFor (s);

    // Pitch range: the copies', at least a semitone either side of the key.
    auto low = -1.0f, high = 1.0f;
    for (const auto& c : copies)
    {
        low = juce::jmin (low, c.semitones - 0.5f);
        high = juce::jmax (high, c.semitones + 0.5f);
    }
    const auto xFor = [&area] (float pan) { return area.getX() + (pan + 1.0f) * 0.5f * area.getWidth(); };
    const auto yFor = [&area, low, high] (float semitones) { return area.getBottom() - (semitones - low) / (high - low) * area.getHeight(); };

    // Grid: the middle (centre of the stereo field) and the key's pitch.
    g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
    g.drawVerticalLine (juce::roundToInt (xFor (0.0f)), area.getY(), area.getBottom());
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), area.getX(), area.getRight());
    g.setFont (juce::FontOptions (9.5f));
    g.setColour (ParagonColours::textDim.withAlpha (0.7f));
    g.drawText ("L", area.withWidth (12.0f), juce::Justification::bottomLeft, false);
    g.drawText ("R", area.withTrimmedLeft (area.getWidth() - 12.0f), juce::Justification::bottomRight, false);

    // Each copy: a short glowing bar, brighter the louder.
    const auto gradient = ParagonLookAndFeel::menuHighlight (area);
    for (const auto& c : copies)
    {
        const auto x = xFor (c.pan), y = yFor (c.semitones);
        const auto alpha = s.on ? juce::jlimit (0.15f, 1.0f, c.level) : 0.15f;
        const auto colour = gradient.getColourAtPosition ((double) juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth()));
        const auto bar = juce::Rectangle<float> (c.isKey ? 4.0f : 3.0f, 14.0f).withCentre ({ x, y });
        g.setColour (colour.withAlpha (alpha * 0.3f));
        g.fillRoundedRectangle (bar.expanded (3.0f), 3.0f);
        g.setColour (colour.withAlpha (alpha));
        g.fillRoundedRectangle (bar, 1.5f);
    }

    // How many: copies per note, and notes per key.
    const auto notes = 1 + (int) std::count_if (s.chord.begin(), s.chord.end(), [] (const synth::ChordVoice& c) { return c.on; });
    const auto perNote = s.hasUnison() ? s.unisonVoices : 1;
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String (perNote) + (perNote == 1 ? " COPY" : " COPIES") + " x " + juce::String (notes) + (notes == 1 ? " NOTE" : " NOTES")
                    + " = " + juce::String (perNote * notes) + " PER KEY",
                footer, juce::Justification::centred, false);
}
