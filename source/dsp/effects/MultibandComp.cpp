#include "dsp/effects/MultibandComp.h"

namespace effects::multiband
{
namespace
{
    float coefficientFor (double sampleRate, float seconds) noexcept
    {
        return 1.0f - std::exp (-1.0f / (juce::jmax (1.0e-5f, seconds) * (float) sampleRate));
    }

    constexpr float rmsToPeakDb = 3.0103f;   // as COMP: a sine peaking at -10 dBFS reads -10 dB

    // Appends `extra`'s sections (and gain) to `into`.
    void append (eq::Design& into, const eq::Design& extra) noexcept
    {
        for (int i = 0; i < extra.numSections && into.numSections < eq::maxSections; ++i)
            into.sections[(size_t) into.numSections++] = extra.sections[(size_t) i];
        into.gain *= extra.gain;
    }

    eq::Band filterBand (eq::Shape shape, double hz, double gainDb, int slope) noexcept
    {
        eq::Band b;
        b.used = true;
        b.shape = shape;
        b.frequency = hz;
        b.gainDb = gainDb;
        b.slope = juce::jlimit (0, numSlopes - 1, slope);
        return b;
    }

    bool sameShape (const Band& a, const Band& b) noexcept
    {
        return juce::approximatelyEqual (a.lowHz, b.lowHz) && juce::approximatelyEqual (a.highHz, b.highHz) && a.slope == b.slope
            && a.stereo == b.stereo;
    }
}

float gainChangeDb (float levelDb, const Band& band) noexcept
{
    const auto range = juce::jlimit (-maxRangeDb, maxRangeDb, band.rangeDb);
    if (std::abs (range) < 1.0e-4f)
        return 0.0f;

    // Downward compression and upward expansion act on what's over the
    // threshold; upward compression and downward expansion on what's under it.
    const auto compress = band.mode == Mode::compress;
    const auto actsAbove = compress == (range < 0.0f);
    const auto into = actsAbove ? levelDb - band.thresholdDb : band.thresholdDb - levelDb;

    // Compression moves each dB in by (1 - 1/ratio); expansion by (ratio - 1).
    const auto ratio = juce::jmax (1.0f, band.ratio);
    const auto slope = compress ? (ratio >= Compressor::infiniteRatio ? 1.0f : 1.0f - 1.0f / ratio)
                                : juce::jmin (ratio - 1.0f, 100.0f);
    const auto knee = juce::jmax (0.0f, band.kneeDb);
    float amount = 0.0f;
    if (knee > 0.0f && 2.0f * into >= -knee && 2.0f * into <= knee)
        amount = slope * juce::square (into + knee * 0.5f) / (2.0f * knee);
    else if (into > 0.0f)
        amount = slope * into;
    return std::copysign (juce::jmin (amount, std::abs (range)), range);
}

eq::Design bandDesign (const Band& band, double gainDb, double sampleRate) noexcept
{
    eq::Design design;
    if (std::abs (gainDb) < 1.0e-6)
        return design;   // no change: no filter at all

    if (band.hasLowEdge() && band.hasHighEdge())
    {
        // Up (or down) from the low edge, and back from the high edge.
        append (design, eq::design (filterBand (eq::Shape::highShelf, band.lowHz, gainDb, band.slope), sampleRate));
        append (design, eq::design (filterBand (eq::Shape::highShelf, band.highHz, -gainDb, band.slope), sampleRate));
    }
    else if (band.hasHighEdge())
        append (design, eq::design (filterBand (eq::Shape::lowShelf, band.highHz, gainDb, band.slope), sampleRate));
    else if (band.hasLowEdge())
        append (design, eq::design (filterBand (eq::Shape::highShelf, band.lowHz, gainDb, band.slope), sampleRate));
    else
        design.gain = juce::Decibels::decibelsToGain (gainDb);
    return design;
}

eq::Design detectorDesign (const Band& band, double sampleRate) noexcept
{
    eq::Design design;
    if (band.hasLowEdge())
        append (design, eq::design (filterBand (eq::Shape::lowCut, band.lowHz, 0.0, band.slope), sampleRate));
    if (band.hasHighEdge())
        append (design, eq::design (filterBand (eq::Shape::highCut, band.highHz, 0.0, band.slope), sampleRate));
    return design;
}

//==============================================================================
void Processor::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    historyLength = (int) std::ceil (lookaheadSeconds * sampleRate) + maxBlockSize + 8;
    for (auto& h : history)
        h.assign ((size_t) historyLength, 0.0f);
    bypass.prepare (sampleRate, maxBlockSize);
    dry.setSize (2, maxBlockSize);
    mix.reset (sampleRate, 0.05);
    reset();
    fresh = true;
}

void Processor::reset() noexcept
{
    written = 0;
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0f);
    for (auto& b : bands)
    {
        b.active = false;
        b.shownChangeDb = 0.0f;
        b.shownLevelDb = -100.0f;
    }
}

Processor::Reading Processor::getReading (int band) const noexcept
{
    if (! juce::isPositiveAndBelow (band, maxBands))
        return {};
    return { bands[(size_t) band].shownChangeDb.load(), bands[(size_t) band].shownLevelDb.load() };
}

double Processor::run (const eq::Design& design, Memory& memory, double x) noexcept
{
    // Transposed direct form II, section by section.
    for (int s = 0; s < design.numSections; ++s)
    {
        const auto& c = design.sections[(size_t) s];
        auto& z = memory[(size_t) s];
        const auto y = c.b0 * x + z[0];
        z[0] = c.b1 * x - c.a1 * y + z[1];
        z[1] = c.b2 * x - c.a2 * y;
        x = y;
    }
    return x * design.gain;
}

void Processor::startBand (int index, const Band& band) noexcept
{
    auto& r = bands[(size_t) index];
    r.active = true;
    r.shape = band;
    r.detector = detectorDesign (band, sampleRate);
    r.detectorMemory = {};
    r.bandMemory = {};
    r.soloMemory = {};
    r.design = {};
    r.designedDb = { 1.0e9, 1.0e9 };   // not made yet
    r.meanSquare = {};
    r.peak = {};
    r.changeDb = {};
    r.stretch = {};
}

void Processor::process (juce::dsp::AudioBlock<float> block, const Settings& settings, bool on) noexcept
{
    const auto numChannels = (int) juce::jmin ((size_t) 2, block.getNumChannels());
    const auto numSamples = (int) block.getNumSamples();
    if (numChannels == 0 || numSamples == 0 || history[0].empty())
        return;

    // Look-ahead: the sound comes out this late (on or off, so the delay the
    // DAW was told about stays put); each band hears up to that far ahead.
    lookaheadLength = settings.lookahead ? (int) std::lround (lookaheadSeconds * sampleRate) : 0;
    const auto start = written;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* in = block.getChannelPointer ((size_t) juce::jmin (ch, numChannels - 1));
        auto& h = history[(size_t) ch];
        for (int i = 0; i < numSamples; ++i)
            h[(size_t) ((start + i) % historyLength)] = in[i];
    }
    written = (start + numSamples) % historyLength;
    const auto at = [this] (int ch, int index) { return history[(size_t) ch][(size_t) (((index % historyLength) + historyLength) % historyLength)]; };
    if (lookaheadLength > 0)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = block.getChannelPointer ((size_t) ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] = at (ch, start + i - lookaheadLength);
        }

    // Bands coming and going: a new band (or new edges) starts afresh.
    bool anyActive = false;
    for (int b = 0; b < maxBands; ++b)
    {
        const auto& band = settings.bands[(size_t) b];
        auto& r = bands[(size_t) b];
        if (! band.isActive())
            r.active = false;
        else if (! r.active || ! sameShape (r.shape, band))
            startBand (b, band);
        anyActive = anyActive || band.isActive();
        if (! band.isActive())
        {
            r.shownChangeDb = 0.0f;
            r.shownLevelDb = -100.0f;
        }
    }

    if (fresh)
    {
        fresh = false;
        bypass.setImmediately (on && anyActive);
        mix.setCurrentAndTargetValue (settings.mix);
    }
    mix.setTargetValue (juce::jlimit (0.0f, 2.0f, settings.mix));

    bypass.process (block, on && anyActive, [&] (juce::dsp::AudioBlock<float> wet)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            dry.copyFrom (ch, 0, wet.getChannelPointer ((size_t) ch), numSamples);

        for (int from = 0; from < numSamples; from += subBlock)
        {
            const auto count = juce::jmin (subBlock, numSamples - from);
            for (int b = 0; b < maxBands; ++b)
            {
                const auto& band = settings.bands[(size_t) b];
                auto& r = bands[(size_t) b];
                if (! r.active)
                    continue;
                const auto midOrSide = band.stereo != Stereo::stereo;
                if (band.stereo == Stereo::side && numChannels < 2)
                    continue;   // mono has no sides
                const auto detectors = midOrSide ? 1 : numChannels;

                // How far ahead this band hears (with look-ahead on).
                const auto ahead = juce::jlimit (0, lookaheadLength, (int) std::lround (band.lookaheadMs * 0.001 * sampleRate));
                const auto character = band.character;
                const auto rmsCoefficient = coefficientFor (sampleRate, character == Compressor::Character::opto ? 0.02f : 0.005f);
                const auto attack = coefficientFor (sampleRate, band.attackMs * 0.001f * (character == Compressor::Character::opto ? 2.0f : 1.0f));
                const auto peakRelease = coefficientFor (sampleRate, 0.01f);
                const auto stretchCoefficient = coefficientFor (sampleRate, 1.5f);
                const auto range = std::abs (juce::jlimit (-maxRangeDb, maxRangeDb, band.rangeDb));

                // The band's detector, over this piece: what it hears, its level, how far it moves.
                for (int i = 0; i < count; ++i)
                {
                    const auto index = start + from + i - lookaheadLength + ahead;
                    std::array<float, 2> levelDb {};
                    for (int c = 0; c < detectors; ++c)
                    {
                        float x = 0.0f;
                        if (! midOrSide)
                            x = at (c, index);
                        else if (numChannels < 2)
                            x = at (0, index);
                        else
                            x = 0.5f * (band.stereo == Stereo::mid ? at (0, index) + at (1, index) : at (0, index) - at (1, index));
                        const auto heard = (float) run (r.detector, r.detectorMemory[(size_t) c], x);
                        const auto level = std::abs (heard);
                        r.peak[(size_t) c] = level > r.peak[(size_t) c] ? level : r.peak[(size_t) c] + (level - r.peak[(size_t) c]) * peakRelease;
                        r.meanSquare[(size_t) c] += (heard * heard - r.meanSquare[(size_t) c]) * rmsCoefficient;
                        levelDb[(size_t) c] = character == Compressor::Character::fet
                                                ? juce::Decibels::gainToDecibels (r.peak[(size_t) c], -120.0f)
                                                : 10.0f * std::log10 (r.meanSquare[(size_t) c] + 1.0e-12f) + rmsToPeakDb;
                    }
                    if (detectors == 2)
                    {
                        const auto loudest = juce::jmax (levelDb[0], levelDb[1]);
                        for (auto& l : levelDb)
                            l += (loudest - l) * juce::jlimit (0.0f, 1.0f, band.link);
                    }
                    for (int c = 0; c < detectors; ++c)
                    {
                        auto& change = r.changeDb[(size_t) c];
                        const auto target = gainChangeDb (levelDb[(size_t) c], band);
                        // Releases stretch with how long and hard it has worked (OPTO), as COMP's.
                        r.stretch[(size_t) c] += (juce::jmin (1.0f, std::abs (change) / 10.0f) - r.stretch[(size_t) c]) * stretchCoefficient;
                        const auto releaseSeconds = band.releaseMs * 0.001f
                                                  * (character == Compressor::Character::opto ? 0.35f + 2.65f * r.stretch[(size_t) c] : 1.0f);
                        const auto moreWork = std::abs (target) > std::abs (change) || target * change < 0.0f;
                        change += (target - change) * (moreWork ? attack : coefficientFor (sampleRate, releaseSeconds));
                        change = juce::jlimit (-range, range, change);
                    }
                    if (i == count - 1)
                    {
                        r.shownLevelDb = juce::jmax (levelDb[0], detectors == 2 ? levelDb[1] : -200.0f);
                        r.shownChangeDb = detectors == 2 && std::abs (r.changeDb[1]) > std::abs (r.changeDb[0]) ? r.changeDb[1] : r.changeDb[0];
                    }
                }

                // The band's filters for where it is now (made again only when it has moved).
                for (int c = 0; c < detectors; ++c)
                {
                    const auto totalDb = (double) band.outputDb + r.changeDb[(size_t) c];
                    if (std::abs (totalDb - r.designedDb[(size_t) c]) > 0.005)
                    {
                        r.design[(size_t) c] = bandDesign (band, totalDb, sampleRate);
                        r.designedDb[(size_t) c] = totalDb;
                    }
                }

                // And on the sound.
                if (! midOrSide)
                {
                    for (int c = 0; c < numChannels; ++c)
                    {
                        auto* samples = wet.getChannelPointer ((size_t) c) + from;
                        for (int i = 0; i < count; ++i)
                            samples[i] = (float) run (r.design[(size_t) c], r.bandMemory[(size_t) c], samples[i]);
                    }
                }
                else if (numChannels < 2)
                {
                    auto* samples = wet.getChannelPointer (0) + from;
                    for (int i = 0; i < count; ++i)
                        samples[i] = (float) run (r.design[0], r.bandMemory[0], samples[i]);
                }
                else
                {
                    auto* left = wet.getChannelPointer (0) + from;
                    auto* right = wet.getChannelPointer (1) + from;
                    const auto onMid = band.stereo == Stereo::mid;
                    for (int i = 0; i < count; ++i)
                    {
                        const auto mid = 0.5 * ((double) left[i] + right[i]), side = 0.5 * ((double) left[i] - right[i]);
                        const auto m = onMid ? run (r.design[0], r.bandMemory[0], mid) : mid;
                        const auto s = onMid ? side : run (r.design[0], r.bandMemory[0], side);
                        left[i] = (float) (m + s);
                        right[i] = (float) (m - s);
                    }
                }
            }
        }

        // MIX: the bands' effect, from none to twice as much.
        for (int i = 0; i < numSamples; ++i)
        {
            const auto amount = mix.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* out = wet.getChannelPointer ((size_t) ch);
                const auto d = dry.getSample (ch, i);
                out[i] = d + (out[i] - d) * amount;
            }
        }

        // SOLO: only that band's part of the sound.
        if (juce::isPositiveAndBelow (settings.soloBand, maxBands) && bands[(size_t) settings.soloBand].active)
        {
            auto& r = bands[(size_t) settings.soloBand];
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* out = wet.getChannelPointer ((size_t) ch);
                for (int i = 0; i < numSamples; ++i)
                    out[i] = (float) run (r.detector, r.soloMemory[(size_t) ch], out[i]);
            }
        }
    });
}
}
