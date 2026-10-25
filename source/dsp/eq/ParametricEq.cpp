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
            slot.range.reset (sampleRate, glideSeconds);
            slot.threshold.reset (sampleRate, glideSeconds);
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
            slot.detector = {};
            slot.range.setCurrentAndTargetValue (0.0);
            slot.threshold.setCurrentAndTargetValue (0.0);
            slot.changeDb = 0.0;
        }
        for (auto& reading : readings)
        {
            reading.active = false;
            reading.changeDb = 0.0f;
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

    ParametricEq::DynamicReading ParametricEq::getDynamics (int band) const noexcept
    {
        if (band < 0 || band >= numBands)
            return {};
        const auto& reading = readings[(size_t) band];
        return { reading.active.load(), reading.changeDb.load(), reading.levelDb.load(), reading.thresholdDb.load() };
    }

    void ParametricEq::detect (BandSlot& slot, const Band& target, const float* left, const float* right, int numSamples, int numChannels) noexcept
    {
        auto& d = slot.detector;
        const auto hz = slot.applied.frequency;
        const auto measure = 1.0 - std::exp (-1.0 / (dynamics::measureSeconds (hz) * sampleRate));
        const auto stereo = numChannels > 1 && right != nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            // The power of the band's part of what it works on.
            double power = 0.0;
            switch (target.placement)
            {
                case Placement::stereo:
                {
                    const auto l = tick (d.filter, d.states[0], left[i]);
                    if (! stereo)
                    {
                        power = l * l;
                        break;
                    }
                    const auto r = tick (d.filter, d.states[1], right[i]);
                    power = 0.5 * (l * l + r * r);
                    break;
                }
                case Placement::left:
                case Placement::right:
                case Placement::side:
                {
                    if (! stereo)
                        break;   // the band does nothing on a mono track
                    const auto x = target.placement == Placement::left    ? (double) left[i]
                                 : target.placement == Placement::right   ? (double) right[i]
                                                                          : 0.5 * ((double) left[i] - right[i]);
                    const auto y = tick (d.filter, d.states[0], x);
                    power = y * y;
                    break;
                }
                case Placement::mid:
                {
                    const auto y = tick (d.filter, d.states[0], stereo ? 0.5 * ((double) left[i] + right[i]) : (double) left[i]);
                    power = y * y;
                    break;
                }
            }
            d.power += measure * (power - d.power);
        }

        // A sine's power is half its peak squared: doubled, a full-scale sine reads 0 dB.
        d.levelDb = std::max (dynamics::floorDb, 10.0 * std::log10 (2.0 * d.power + 1.0e-30));

        // AUTO's sense of the band's typical level: the average of what it's
        // heard so far for its first few seconds (so it's right from the
        // start, not dragged down by the first note's onset), then following
        // over a few seconds; barely following pauses and silence.
        const auto seconds = numSamples / sampleRate;
        if (! d.heard)
        {
            if (d.levelDb > dynamics::floorDb + 30.0)
            {
                d.typicalDb = d.levelDb;
                d.heard = true;
                d.heardSeconds = seconds;
            }
        }
        else
        {
            const auto quiet = d.levelDb < d.typicalDb - dynamics::autoIgnoreBelowDb;
            d.heardSeconds += seconds;
            const auto follow = d.heardSeconds < dynamics::autoSeconds ? seconds / d.heardSeconds
                                                                      : 1.0 - std::exp (-seconds / dynamics::autoSeconds);
            d.typicalDb += (d.levelDb - d.typicalDb) * follow * (quiet ? 0.1 : 1.0);
        }

        const auto threshold = target.autoThreshold ? (d.heard ? d.typicalDb + dynamics::autoAboveDb : dynamics::maxThresholdDb)
                                                    : juce::jlimit (dynamics::minThresholdDb, dynamics::maxThresholdDb, target.thresholdDb);
        slot.threshold.setTargetValue (threshold);
        slot.range.setTargetValue (target.isDynamic() ? juce::jlimit (-dynamics::maxRangeDb, dynamics::maxRangeDb, target.dynamicRangeDb) : 0.0);
        const auto thresholdNow = slot.threshold.skip (numSamples);
        const auto rangeNow = slot.range.skip (numSamples);

        // The gain moves towards where the level says over the attack time
        // when it's doing more, over the release time when it's letting go.
        const auto wanted = dynamics::gainChangeDb (d.levelDb, thresholdNow, rangeNow);
        const auto more = std::abs (wanted) > std::abs (slot.changeDb);
        const auto tau = more ? dynamics::attackSeconds (hz, target.attack) : dynamics::releaseSeconds (hz, target.release);
        slot.changeDb += (wanted - slot.changeDb) * (1.0 - std::exp (-seconds / tau));
    }

    void ParametricEq::updateBand (int index, const Band& target, double scale, bool scaleMoved,
                                   const float* dryLeft, const float* dryRight, int numSamples, int numChannels) noexcept
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

        // Dynamics: listening from when the band becomes dynamic until its
        // range has glided back to 0 after it stops being.
        const auto changeWas = slot.changeDb;
        auto& detector = slot.detector;
        if (target.isDynamic() && ! detector.running)
        {
            detector = {};
            detector.running = true;
            detector.filter = soloDesign (slot.applied, sampleRate);
            slot.range.setCurrentAndTargetValue (0.0);
            slot.threshold.setCurrentAndTargetValue (target.autoThreshold ? dynamics::maxThresholdDb : target.thresholdDb);
        }
        if (detector.running)
        {
            if (redesign)
                detector.filter = soloDesign (slot.applied, sampleRate);
            detect (slot, target, dryLeft, dryRight, numSamples, numChannels);
            if (! target.isDynamic() && ! slot.range.isSmoothing() && std::abs (slot.changeDb) < 0.001)
            {
                detector.running = false;
                slot.changeDb = 0.0;
            }
        }
        slot.applied.gainDb = juce::jlimit (-maxGainDb, maxGainDb, slot.gain.getCurrentValue() + slot.changeDb);

        if (redesign)
            autoGainStale = true;   // the set curve moved (AUTO GAIN ignores the dynamics)
        if (redesign || ! juce::exactlyEqual (slot.changeDb, changeWas))
            slot.current.design = design (slot.applied, sampleRate, scale);
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
            const auto anyDynamic = std::any_of (settings.bands.begin(), settings.bands.end(), [] (const Band& b) { return b.isDynamic(); })
                                 || std::any_of (slots.begin(), slots.end(), [] (const BandSlot& slot) { return slot.detector.running; });
            if (soloing || anyDynamic)   // the EQ's input, for SOLO and for the dynamic bands to listen to
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
                    updateBand (band, settings.bands[(size_t) band], scale, scaleMoved, dry.getReadPointer (0, start),
                                numChannels > 1 ? dry.getReadPointer (1, start) : nullptr, count, numChannels);
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

            for (size_t band = 0; band < slots.size(); ++band)
            {
                const auto& slot = slots[band];
                auto& reading = readings[band];
                reading.active = slot.detector.running;
                reading.changeDb = (float) slot.changeDb;
                reading.levelDb = (float) slot.detector.levelDb;
                reading.thresholdDb = (float) slot.threshold.getCurrentValue();
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
