#include "ParametricEq.h"

namespace eq
{
    namespace
    {
        constexpr double glideSeconds = 0.04, crossfadeSeconds = 0.03;
        constexpr int autoGainEvery = 1024;   // samples between AUTO GAIN estimates while the curve changes
    }

    void ParametricEq::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        for (auto& slot : slots)
        {
            slot.frequency.reset (sampleRate, glideSeconds);
            slot.q.reset (sampleRate, glideSeconds);
            slot.gain.reset (sampleRate, glideSeconds);
        }
        gainScale.reset (sampleRate, glideSeconds);
        soloAmount.reset (sampleRate, 0.02);
        output.reset (sampleRate, 0.02);
        scratch.setSize (effects::maxChannels, maxBlockSize);
        dry.setSize (effects::maxChannels, maxBlockSize);
        bypass.prepare (sampleRate, maxBlockSize);
        reset();
    }

    void ParametricEq::reset() noexcept
    {
        for (auto& slot : slots)
        {
            slot.applied = {};
            slot.frequency.setCurrentAndTargetValue (slot.applied.frequency);
            slot.q.setCurrentAndTargetValue (slot.applied.q);
            slot.gain.setCurrentAndTargetValue (slot.applied.gainDb);
            slot.current = {};
            slot.previous = {};
            slot.fade = 1.0;
        }
        gainScale.setCurrentAndTargetValue (1.0);
        solo = {};
        soloBand = -1;
        soloAmount.setCurrentAndTargetValue (0.0f);
        output.setCurrentAndTargetValue (1.0f);
        autoGainDb = 0.0;
        autoGainStale = true;
        samplesSinceAutoGain = autoGainEvery;
        bypass.setImmediately (false);
    }

    bool ParametricEq::sameStructure (const Band& a, const Band& b) noexcept
    {
        return a.used == b.used && a.on == b.on && a.shape == b.shape && a.placement == b.placement
            && slopeFor (a.shape, a.slope) == slopeFor (b.shape, b.slope);
    }

    double ParametricEq::tick (const Design& design, State& state, double x) noexcept
    {
        // Transposed direct form II, in double precision: low, narrow bands
        // stay accurate and quiet.
        for (int i = 0; i < design.numSections; ++i)
        {
            const auto& s = design.sections[(size_t) i];
            auto& z = state.z[(size_t) i];
            const auto y = s.b0 * x + z[0];
            z[0] = s.b1 * x - s.a1 * y + z[1];
            z[1] = s.b2 * x - s.a2 * y;
            x = y;
        }
        return x * design.gain;
    }

    void ParametricEq::run (Running& running, float* left, float* right, int numSamples, int numChannels) noexcept
    {
        if (running.design.isIdentity())
            return;

        const auto& design = running.design;
        const auto stereo = numChannels > 1 && right != nullptr;
        switch (running.placement)
        {
            case Placement::stereo:
                for (int i = 0; i < numSamples; ++i)
                    left[i] = (float) tick (design, running.states[0], left[i]);
                if (stereo)
                    for (int i = 0; i < numSamples; ++i)
                        right[i] = (float) tick (design, running.states[1], right[i]);
                break;
            case Placement::left:
                if (stereo)
                    for (int i = 0; i < numSamples; ++i)
                        left[i] = (float) tick (design, running.states[0], left[i]);
                break;
            case Placement::right:
                if (stereo)
                    for (int i = 0; i < numSamples; ++i)
                        right[i] = (float) tick (design, running.states[1], right[i]);
                break;
            case Placement::mid:
                if (! stereo)
                {
                    for (int i = 0; i < numSamples; ++i)   // a mono track is all mid
                        left[i] = (float) tick (design, running.states[0], left[i]);
                    break;
                }
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto mid = tick (design, running.states[0], 0.5 * ((double) left[i] + right[i]));
                    const auto side = 0.5 * ((double) left[i] - right[i]);
                    left[i] = (float) (mid + side);
                    right[i] = (float) (mid - side);
                }
                break;
            case Placement::side:
                if (! stereo)
                    break;   // a mono track has no side
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto mid = 0.5 * ((double) left[i] + right[i]);
                    const auto side = tick (design, running.states[0], 0.5 * ((double) left[i] - right[i]));
                    left[i] = (float) (mid + side);
                    right[i] = (float) (mid - side);
                }
                break;
        }
    }

    void ParametricEq::updateBand (int index, const Band& target, double scale, bool scaleMoved, int numSamples) noexcept
    {
        auto& slot = slots[(size_t) index];
        auto redesign = scaleMoved;

        if (! sameStructure (slot.applied, target))
        {
            // A new kind of band: keep the old one running, fading out, and
            // start the new one from silence (its filters' memories empty).
            slot.previous = slot.current;
            slot.current.states = {};
            slot.current.placement = target.placement;
            slot.fade = 0.0;

            // A band switched on or created starts at its settings, not
            // gliding there from wherever it was left.
            if (! slot.applied.isActive() && target.isActive())
            {
                slot.frequency.setCurrentAndTargetValue (target.frequency);
                slot.q.setCurrentAndTargetValue (target.q);
                slot.gain.setCurrentAndTargetValue (target.gainDb);
            }
            slot.applied.used = target.used;
            slot.applied.on = target.on;
            slot.applied.shape = target.shape;
            slot.applied.slope = target.slope;
            slot.applied.placement = target.placement;
            redesign = true;
        }

        slot.frequency.setTargetValue (juce::jlimit (minFrequency, maxFrequency, target.frequency));
        slot.q.setTargetValue (juce::jlimit (minQ, maxQ, target.q));
        slot.gain.setTargetValue (target.gainDb);
        if (slot.frequency.isSmoothing() || slot.q.isSmoothing() || slot.gain.isSmoothing())
        {
            slot.frequency.skip (numSamples);
            slot.q.skip (numSamples);
            slot.gain.skip (numSamples);
            redesign = true;
        }
        // The band is designed from where the glides are now (which is
        // where it's set, for a band just switched on).
        slot.applied.frequency = slot.frequency.getCurrentValue();
        slot.applied.q = slot.q.getCurrentValue();
        slot.applied.gainDb = slot.gain.getCurrentValue();

        if (redesign)
        {
            slot.current.design = design (slot.applied, sampleRate, scale);
            autoGainStale = true;
        }
    }

    void ParametricEq::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto numChannels = (int) juce::jmin (block.getNumChannels(), (size_t) effects::maxChannels);
        const auto numSamples = (int) block.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        // While switched off the EQ isn't run at all; when it comes back,
        // its bands start from where they're set, with empty filters.
        if (bypass.isSilent() && ! settings.on)
            return;
        if (bypass.isSilent())
            reset();

        bypass.process (block.getSubsetChannelBlock (0, (size_t) numChannels), settings.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            auto* left = wet.getChannelPointer (0);
            auto* right = numChannels > 1 ? wet.getChannelPointer (1) : nullptr;

            // SOLO listens to the EQ's input.
            const auto soloTarget = settings.soloBand >= 0 && settings.soloBand < numBands
                                    && settings.bands[(size_t) settings.soloBand].used;
            if (soloTarget && settings.soloBand != soloBand)
            {
                soloBand = settings.soloBand;
                solo.states = {};
            }
            soloAmount.setTargetValue (soloTarget ? 1.0f : 0.0f);
            const auto soloing = soloAmount.isSmoothing() || soloTarget;
            if (soloing)
                for (int ch = 0; ch < numChannels; ++ch)
                    dry.copyFrom (ch, 0, wet.getChannelPointer ((size_t) ch), numSamples);

            for (int start = 0; start < numSamples; start += subBlock)
            {
                const auto count = juce::jmin (subBlock, numSamples - start);
                gainScale.setTargetValue (juce::jlimit (0.0, 2.0, settings.gainScale));
                const auto scaleMoved = gainScale.isSmoothing();
                const auto scale = scaleMoved ? gainScale.skip (count) : gainScale.getCurrentValue();

                for (int band = 0; band < numBands; ++band)
                {
                    updateBand (band, settings.bands[(size_t) band], scale, scaleMoved, count);
                    auto& slot = slots[(size_t) band];
                    float* l = left + start;
                    float* r = right != nullptr ? right + start : nullptr;

                    if (slot.fade >= 1.0)
                    {
                        run (slot.current, l, r, count, numChannels);
                        continue;
                    }

                    // Crossfading from the band as it was.
                    auto* oldLeft = scratch.getWritePointer (0);
                    auto* oldRight = numChannels > 1 ? scratch.getWritePointer (1) : nullptr;
                    std::copy (l, l + count, oldLeft);
                    if (r != nullptr)
                        std::copy (r, r + count, oldRight);
                    run (slot.previous, oldLeft, oldRight, count, numChannels);
                    run (slot.current, l, r, count, numChannels);

                    const auto step = 1.0 / (crossfadeSeconds * sampleRate);
                    for (int i = 0; i < count; ++i)
                    {
                        slot.fade = juce::jmin (1.0, slot.fade + step);
                        const auto f = (float) slot.fade;
                        l[i] = oldLeft[i] + (l[i] - oldLeft[i]) * f;
                        if (r != nullptr)
                            r[i] = oldRight[i] + (r[i] - oldRight[i]) * f;
                    }
                }
            }

            if (soloing)
            {
                if (soloBand >= 0)
                    solo.design = soloDesign (slots[(size_t) soloBand].applied, sampleRate);
                solo.placement = Placement::stereo;
                auto* soloLeft = dry.getWritePointer (0);
                auto* soloRight = numChannels > 1 ? dry.getWritePointer (1) : nullptr;
                run (solo, soloLeft, soloRight, numSamples, numChannels);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto amount = soloAmount.getNextValue();
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        auto& sample = wet.getChannelPointer ((size_t) ch)[i];
                        sample += (dry.getSample (ch, i) - sample) * amount;
                    }
                }
            }

            // OUTPUT, and AUTO GAIN's estimate (redone at most every 1024
            // samples while the curve is moving).
            samplesSinceAutoGain += numSamples;
            if (! settings.autoGain)
            {
                autoGainDb = 0.0;
                autoGainStale = true;
            }
            else if (autoGainStale && samplesSinceAutoGain >= autoGainEvery)
            {
                autoGainDb = -averageChangeDb (settings, sampleRate);
                autoGainStale = false;
                samplesSinceAutoGain = 0;
            }
            output.setTargetValue ((float) juce::Decibels::decibelsToGain (settings.outputDb + autoGainDb, -200.0));
            if (output.isSmoothing() || ! juce::approximatelyEqual (output.getTargetValue(), 1.0f))
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto gain = output.getNextValue();
                    for (int ch = 0; ch < numChannels; ++ch)
                        wet.getChannelPointer ((size_t) ch)[i] *= gain;
                }
        });
    }

    double ParametricEq::curveDb (const Settings& settings, double hz, double sampleRate, Placement which) noexcept
    {
        double total = 0.0;
        for (const auto& band : settings.bands)
            if (band.isActive() && (band.placement == Placement::stereo || band.placement == which))
                total += responseDb (design (band, sampleRate, settings.gainScale), hz, sampleRate);
        return total;
    }

    double ParametricEq::averageChangeDb (const Settings& settings, double sampleRate) noexcept
    {
        std::array<Design, numBands> designs;
        for (size_t i = 0; i < designs.size(); ++i)
            designs[i] = design (settings.bands[i], sampleRate, settings.gainScale);

        // Three points an octave, 20 Hz to 20 kHz (and no higher than the top of the range).
        double sum = 0.0;
        int count = 0;
        for (auto hz = 20.0; hz <= juce::jmin (20000.0, 0.49 * sampleRate); hz *= std::pow (2.0, 1.0 / 3.0), ++count)
            for (size_t i = 0; i < designs.size(); ++i)
                if (settings.bands[i].isActive())
                    sum += responseDb (designs[i], hz, sampleRate) * (settings.bands[i].placement == Placement::stereo ? 1.0 : 0.5);
        return count > 0 ? sum / count : 0.0;
    }
}
