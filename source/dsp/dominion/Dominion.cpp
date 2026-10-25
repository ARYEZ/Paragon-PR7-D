#include "Dominion.h"

namespace dominion
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr double spacingOctaves = 1.0 / 12.0;
        constexpr double detectorQ = 8.65;   // a 1/6-octave band pass
        constexpr double floorBelowLoudestDb = 60.0, floorDb = -100.0;
        constexpr double softMarginDb = 2.0, kneeDb = 3.0;
        constexpr int nearExcluded = 2;
        constexpr double maxReductionDb = 40.0;

        double smoothstep (double x) noexcept
        {
            x = juce::jlimit (0.0, 1.0, x);
            return x * x * (3.0 - 2.0 * x);
        }

        // Soft knee over `kneeDb` round 0: nothing below, then a dB per dB.
        double softKnee (double over) noexcept
        {
            const auto half = 0.5 * kneeDb;
            return over <= -half ? 0.0 : over < half ? (over + half) * (over + half) / (2.0 * kneeDb) : over;
        }

        // The width of a cut at a DETAIL (0 to 100), in octaves to half its depth.
        double widthFor (double detail) noexcept
        {
            return std::pow (2.0, juce::jmap (juce::jlimit (0.0, 100.0, detail) / 100.0, std::log2 (0.5), std::log2 (1.0 / 16.0)));
        }

        double qForWidth (double octaves) noexcept
        {
            const auto ratio = std::pow (2.0, octaves);
            return std::sqrt (ratio) / (ratio - 1.0);
        }

        eq::Band toEq (const Band& band)
        {
            eq::Band b;
            b.used = band.used;
            b.on = band.on;
            b.frequency = band.frequency;
            b.q = band.q;
            b.slope = band.slope;
            b.gainDb = band.depthDb;
            switch (band.shape)
            {
                case Shape::lowCut:     b.shape = eq::Shape::lowCut; break;
                case Shape::lowShelf:   b.shape = eq::Shape::lowShelf; break;
                case Shape::highShelf:  b.shape = eq::Shape::highShelf; break;
                case Shape::highCut:    b.shape = eq::Shape::highCut; break;
                case Shape::bell:       b.shape = eq::Shape::bell; break;
                case Shape::bandPass:   b.shape = eq::Shape::bandPass; break;
                case Shape::bandReject: b.shape = eq::Shape::notch; break;
                case Shape::tilt:       b.shape = eq::Shape::tiltShelf; break;
            }
            return b;
        }

        // A band's (or FOCUS's) weight on channel 0 or 1: -100 all on 0, 100 all on 1.
        double focusWeight (double focus, int channel) noexcept
        {
            return channel == 0 ? juce::jlimit (0.0, 1.0, 1.0 - focus / 100.0) : juce::jlimit (0.0, 1.0, 1.0 + focus / 100.0);
        }
    }

    double lowWeight (double hz) noexcept  { return smoothstep (std::log2 (1200.0 / hz) / std::log2 (1200.0 / 400.0)); }
    double highWeight (double hz) noexcept { return smoothstep (std::log2 (hz / 1000.0) / std::log2 (3000.0 / 1000.0)); }

    double depthCurveDb (const Settings& settings, double hz, int channel, double sampleRate)
    {
        double total = 0.0, parallel = 0.0;
        auto anyBandPass = false;
        for (const auto& band : settings.bands)
        {
            if (! band.isActive())
                continue;
            const auto db = focusWeight (band.focus, channel) * eq::responseDb (eq::design (toEq (band), sampleRate), hz, sampleRate);
            if (band.shape == Shape::bandPass)
            {
                anyBandPass = true;
                parallel += std::pow (10.0, db / 20.0);
            }
            else
            {
                total += db;
            }
        }
        if (anyBandPass)
            total += 20.0 * std::log10 (std::max (parallel, 1.0e-6));
        return juce::jlimit (-120.0, 24.0, total);
    }

    //==========================================================================
    void Processor::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        numFilters = 0;
        for (auto hz = 20.0; hz <= std::min (20000.0, 0.45 * sampleRate) && numFilters < maxFilters; hz *= std::pow (2.0, spacingOctaves))
            frequencies[(size_t) numFilters++] = hz;

        for (int k = 0; k < numFilters; ++k)
        {
            const auto w = 2.0 * pi * frequencies[(size_t) k] / sampleRate;
            const auto a = std::sin (w) / (2.0 * detectorQ), a0 = 1.0 + a;
            detectors[(size_t) k] = { a / a0, 0.0, -a / a0, -2.0 * std::cos (w) / a0, (1.0 - a) / a0 };
            // The level over about 16 cycles (4 to 120 ms): steady enough that
            // noise's own flicker doesn't read as resonances.
            measure[(size_t) k] = 1.0 - std::exp (-1.0 / (juce::jlimit (0.004, 0.12, 16.0 / frequencies[(size_t) k]) * sampleRate));
            cosW[(size_t) k] = std::cos (w);
        }
        dry.setSize (2, maxBlockSize);
        work.setSize (2, maxBlockSize);
        bypass.prepare (sampleRate, maxBlockSize);
        for (auto* smoothed : { &mix, &wetTrim, &out })
            smoothed->reset (sampleRate, 0.02);
        shaped = curved = false;
        reset();
    }

    void Processor::reset() noexcept
    {
        detectorStates = {};
        power = {};
        reduction = {};
        gains = {};
        cutStates = {};
        for (auto& channel : cuts)
            channel.fill (Biquad {});
        listenStates = {};
        listening = -1;
        for (auto& channel : shown)
            for (auto& value : channel)
                value.store (0.0f);
    }

    void Processor::updateShape (const Settings& s)
    {
        // Per filter: DETAIL with its tilts, the cut's width, SOFT's neighbourhood, and the timing.
        auto widest = 0.0;
        std::array<double, maxFilters> q {};
        for (int k = 0; k < numFilters; ++k)
        {
            const auto hz = frequencies[(size_t) k];
            const auto low = lowWeight (hz), high = highWeight (hz);
            const auto detail = juce::jlimit (0.0, 100.0, s.detail + 0.5 * (s.detailTiltLow * low + s.detailTiltHigh * high));
            const auto width = widthFor (detail);
            widest = std::max (widest, width);
            q[(size_t) k] = qForWidth (width);
            alpha[(size_t) k] = std::sin (2.0 * pi * hz / sampleRate) / (2.0 * q[(size_t) k]);
            windowHalf[(size_t) k] = std::max (nearExcluded + 2, (int) std::round (juce::jmap (detail / 100.0, 1.0, 1.0 / 3.0) / spacingOctaves));

            // ATTACK 5 ms and RELEASE 60 ms at 1 kHz at 50%, a quarter to four
            // times that from 0 to 100%, quicker up high and slower down low;
            // the tilts slow (up) or speed (down) the lows and highs.
            const auto byFrequency = std::sqrt (1000.0 / hz);
            const auto attackTilt = std::pow (4.0, (s.attackTiltLow * low + s.attackTiltHigh * high) / 100.0);
            const auto releaseTilt = std::pow (4.0, (s.releaseTiltLow * low + s.releaseTiltHigh * high) / 100.0);
            attackSeconds[(size_t) k] = juce::jlimit (0.0002, 0.3, 0.005 * std::pow (4.0, (s.attack - 50.0) / 50.0) * byFrequency * attackTilt);
            releaseSeconds[(size_t) k] = juce::jlimit (0.005, 3.0, 0.06 * std::pow (4.0, (s.release - 50.0) / 50.0) * byFrequency * releaseTilt);
        }

        // How the bells overlap: bell j's cut, in dB, falls off as a band
        // pass's power does (1 / (1 + Q^2 (r - 1/r)^2)): far enough to matter
        // until it's down to 2%.
        const auto qWidest = qForWidth (widest);
        const auto r = (1.0 / qWidest) * std::sqrt (1.0 / 0.02 - 1.0);   // r - 1/r there
        const auto ratio = 0.5 * (r + std::sqrt (r * r + 4.0));
        reach = juce::jlimit (1, 48, (int) std::ceil (std::log2 (ratio) / spacingOctaves));
        for (int k = 0; k < numFilters; ++k)
            for (int offset = -reach; offset <= reach; ++offset)
            {
                const auto j = k + offset;
                auto& value = overlap[(size_t) k][(size_t) (offset + reach)];
                if (j < 0 || j >= numFilters)
                {
                    value = 0.0f;
                    continue;
                }
                const auto x = frequencies[(size_t) k] / frequencies[(size_t) j];
                const auto d = q[(size_t) j] * (x - 1.0 / x);
                value = (float) (1.0 / (1.0 + d * d));
            }

        shapedFor = s;
        shaped = true;
    }

    void Processor::updateCurve (const Settings& s)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto focus = focusWeight (s.focus, channel);
            for (int k = 0; k < numFilters; ++k)
                curve[(size_t) channel][(size_t) k] = focus * std::pow (10.0, depthCurveDb (s, frequencies[(size_t) k], channel, sampleRate) / 20.0);
        }
        curvedFor = s;
        curved = true;
    }

    void Processor::computeTargets (const Settings& s, int numChannels, double seconds) noexcept
    {
        // Each part's level, in dB (a sine in the middle of a part reads its
        // own level), and with LINK, part of the way to both channels together.
        std::array<std::array<double, maxFilters>, 2> level {};
        auto loudest = floorDb;
        for (int k = 0; k < numFilters; ++k)
        {
            const auto both = 10.0 * std::log10 (power[0][(size_t) k] + (numChannels > 1 ? power[1][(size_t) k] : power[0][(size_t) k]) + 1.0e-30);
            for (int c = 0; c < numChannels; ++c)
            {
                const auto own = 10.0 * std::log10 (2.0 * power[(size_t) c][(size_t) k] + 1.0e-30);
                const auto linked = numChannels > 1 ? own + (both - own) * s.link / 100.0 : own;
                level[(size_t) c][(size_t) k] = linked;
                loudest = std::max (loudest, linked);
            }
        }

        const auto depthFactor = juce::jlimit (0.0, 20.0, s.depth) / 5.0;
        // HARD's fixed level: -20 dB per part at DEPTH 0, 3 dB lower for each
        // step (-35 at 5; a mix's parts mostly sit at -30 to -50).
        const auto hardThreshold = -20.0 - 3.0 * juce::jlimit (0.0, 20.0, s.depth);
        const auto maxCut = juce::jlimit (0.0, maxReductionDb, s.maxCutDb);
        for (int c = 0; c < numChannels; ++c)
        {
            const auto& e = level[(size_t) c];
            // Running sums, for each part's neighbourhood average.
            std::array<double, maxFilters + 1> sums {};
            for (int k = 0; k < numFilters; ++k)
                sums[(size_t) k + 1] = sums[(size_t) k] + e[(size_t) k];

            for (int k = 0; k < numFilters; ++k)
            {
                double target = 0.0;
                if (e[(size_t) k] > loudest - floorBelowLoudestDb && e[(size_t) k] > floorDb)
                {
                    if (s.mode == Mode::soft)
                    {
                        // The parts around it, leaving out its nearest two either
                        // side (they hear a resonance too, and would raise the bar).
                        const auto from = std::max (0, k - windowHalf[(size_t) k]), to = std::min (numFilters - 1, k + windowHalf[(size_t) k]);
                        const auto nearFrom = std::max (from, k - nearExcluded), nearTo = std::min (to, k + nearExcluded);
                        const auto count = (to - from + 1) - (nearTo - nearFrom + 1);
                        const auto around = count > 0 ? ((sums[(size_t) to + 1] - sums[(size_t) from]) - (sums[(size_t) nearTo + 1] - sums[(size_t) nearFrom])) / count
                                                      : e[(size_t) k];
                        target = depthFactor * softKnee (e[(size_t) k] - around - softMarginDb);
                    }
                    else
                    {
                        // HARD: over a fixed level, tilted up 1.5 dB an octave
                        // (music has more in the lows), at 2:1.
                        const auto tilted = e[(size_t) k] + 1.5 * std::log2 (frequencies[(size_t) k] / 1000.0);
                        target = 0.5 * softKnee (tilted - hardThreshold);
                    }
                }
                target = std::min (target * curve[(size_t) c][(size_t) k], maxCut);

                auto& r = reduction[(size_t) c][(size_t) k];
                const auto tau = target > r ? attackSeconds[(size_t) k] : releaseSeconds[(size_t) k];
                r += (target - r) * (1.0 - std::exp (-seconds / tau));
            }

            // Each bell's gain so that, overlapping, they cut what's wanted:
            // a few Gauss-Seidel passes (kept to cuts), carried on from last time.
            auto& g = gains[(size_t) c];
            const auto& wanted = reduction[(size_t) c];
            for (int pass = 0; pass < 3; ++pass)
                for (int j = 0; j < numFilters; ++j)
                {
                    double sum = 0.0;
                    const auto& row = overlap[(size_t) j];
                    for (int offset = -reach; offset <= reach; ++offset)
                        if (const auto k = j + offset; k >= 0 && k < numFilters)
                            sum += row[(size_t) (offset + reach)] * g[(size_t) k];
                    g[(size_t) j] = std::max (0.0, g[(size_t) j] + wanted[(size_t) j] - sum);
                }
        }
    }

    void Processor::process (juce::dsp::AudioBlock<float> block, const Settings& s) noexcept
    {
        const auto numChannels = (int) juce::jmin ((size_t) 2, block.getNumChannels());
        const auto numSamples = (int) block.getNumSamples();
        if (numChannels == 0 || numSamples == 0 || numFilters == 0)
            return;
        if (bypass.isSilent() && ! s.on)
        {
            for (auto& channel : shown)
                for (auto& value : channel)
                    value.store (0.0f);
            return;
        }
        if (bypass.isSilent())
            reset();   // coming back on: from a clean start

        // Only redone when what they depend on has changed at all.
        const auto moved = [] (double a, double b) { return ! juce::exactlyEqual (a, b); };
        const auto shapeChanged = ! shaped || moved (s.detail, shapedFor.detail) || moved (s.attack, shapedFor.attack)
                               || moved (s.release, shapedFor.release)
                               || moved (s.detailTiltLow, shapedFor.detailTiltLow) || moved (s.detailTiltHigh, shapedFor.detailTiltHigh)
                               || moved (s.attackTiltLow, shapedFor.attackTiltLow) || moved (s.attackTiltHigh, shapedFor.attackTiltHigh)
                               || moved (s.releaseTiltLow, shapedFor.releaseTiltLow) || moved (s.releaseTiltHigh, shapedFor.releaseTiltHigh);
        if (shapeChanged)
            updateShape (s);
        auto curveChanged = ! curved || moved (s.focus, curvedFor.focus);
        for (size_t i = 0; i < s.bands.size() && ! curveChanged; ++i)
        {
            const auto& a = s.bands[i];
            const auto& b = curvedFor.bands[i];
            curveChanged = a.used != b.used || a.on != b.on || a.shape != b.shape || moved (a.frequency, b.frequency)
                        || moved (a.depthDb, b.depthDb) || moved (a.q, b.q) || a.slope != b.slope || moved (a.focus, b.focus);
        }
        if (curveChanged)
            updateCurve (s);

        mix.setTargetValue ((float) juce::jlimit (0.0, 1.0, s.mix / 100.0));
        wetTrim.setTargetValue ((float) juce::Decibels::decibelsToGain (s.wetTrimDb));
        out.setTargetValue ((float) juce::Decibels::decibelsToGain (s.outDb));
        const auto midSide = s.stereo == StereoMode::midSide && numChannels == 2;

        // Listening to one band's part of what's taken away.
        const auto listen = s.listenBand >= 0 && s.listenBand < maxBands && s.bands[(size_t) s.listenBand].used ? s.listenBand : -1;
        if (listen != listening)
        {
            listening = listen;
            listenStates = {};
            if (listen >= 0)
                listenDesign = eq::soloDesign (toEq (s.bands[(size_t) listen]), sampleRate);
        }

        bypass.process (block.getSubsetChannelBlock (0, (size_t) numChannels), s.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            for (int c = 0; c < numChannels; ++c)
                dry.copyFrom (c, 0, wet.getChannelPointer ((size_t) c), numSamples);

            for (int start = 0; start < numSamples; start += subBlock)
            {
                const auto count = std::min (subBlock, numSamples - start);

                // Into left/right or mid/side.
                for (int i = 0; i < count; ++i)
                {
                    const auto l = wet.getSample (0, start + i);
                    const auto r = numChannels > 1 ? wet.getSample (1, start + i) : l;
                    work.setSample (0, i, midSide ? 0.5f * (l + r) : l);
                    if (numChannels > 1)
                        work.setSample (1, i, midSide ? 0.5f * (l - r) : r);
                }

                // Listen.
                for (int c = 0; c < numChannels; ++c)
                {
                    const auto* x = work.getReadPointer (c);
                    for (int k = 0; k < numFilters; ++k)
                    {
                        auto& state = detectorStates[(size_t) c][(size_t) k];
                        auto& p = power[(size_t) c][(size_t) k];
                        const auto& detector = detectors[(size_t) k];
                        const auto m = measure[(size_t) k];
                        for (int i = 0; i < count; ++i)
                        {
                            const auto y = tick (detector, state, x[i]);
                            p += m * (y * y - p);
                        }
                    }
                }
                computeTargets (s, numChannels, count / sampleRate);

                // Cut: each bell, redesigned for its gain, in series.
                for (int c = 0; c < numChannels; ++c)
                {
                    auto* x = work.getWritePointer (c);
                    for (int k = 0; k < numFilters; ++k)
                    {
                        auto& gain = gains[(size_t) c][(size_t) k];
                        auto& state = cutStates[(size_t) c][(size_t) k];
                        if (gain < 0.002)
                        {
                            // Doing nothing: once its memory has died away, skip it
                            // (a flat bell with an empty memory stays empty).
                            gain = 0.0;
                            if (std::abs (state.z1) < 1.0e-12 && std::abs (state.z2) < 1.0e-12)
                            {
                                state = {};
                                continue;
                            }
                        }
                        const auto A = std::pow (10.0, -gain / 40.0), a = alpha[(size_t) k], cw = cosW[(size_t) k];
                        const auto a0 = 1.0 + a / A;
                        auto& f = cuts[(size_t) c][(size_t) k];
                        f = { (1.0 + a * A) / a0, -2.0 * cw / a0, (1.0 - a * A) / a0, -2.0 * cw / a0, (1.0 - a / A) / a0 };
                        for (int i = 0; i < count; ++i)
                            x[i] = (float) tick (f, state, x[i]);
                    }
                }

                // Back to left/right.
                for (int i = 0; i < count; ++i)
                {
                    const auto a = work.getSample (0, i);
                    const auto b = numChannels > 1 ? work.getSample (1, i) : a;
                    wet.setSample (0, start + i, midSide ? a + b : a);
                    if (numChannels > 1)
                        wet.setSample (1, start + i, midSide ? a - b : b);
                }
            }

            // WET TRIM, MIX, DELTA (or one band's part of it), OUT.
            for (int i = 0; i < numSamples; ++i)
            {
                const auto trim = wetTrim.getNextValue(), amount = mix.getNextValue(), gain = out.getNextValue();
                for (int c = 0; c < numChannels; ++c)
                {
                    const auto d = dry.getSample (c, i);
                    const auto mixed = d + (wet.getSample (c, i) * trim - d) * amount;
                    double y = mixed;
                    if (listening >= 0)
                    {
                        auto value = (double) (d - mixed);
                        for (int section = 0; section < listenDesign.numSections; ++section)
                        {
                            const auto& q = listenDesign.sections[(size_t) section];
                            auto& z = listenStates[(size_t) c][(size_t) section];
                            const auto out0 = q.b0 * value + z[0];
                            z[0] = q.b1 * value - q.a1 * out0 + z[1];
                            z[1] = q.b2 * value - q.a2 * out0;
                            value = out0;
                        }
                        y = value * listenDesign.gain;
                    }
                    else if (s.delta)
                    {
                        y = d - mixed;
                    }
                    wet.setSample (c, i, (float) y * gain);
                }
            }
        });

        // For the display: the cut, scaled by MIX.
        const auto shownMix = (float) juce::jlimit (0.0, 1.0, s.mix / 100.0);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < numFilters; ++k)
                shown[(size_t) c][(size_t) k].store (s.on && c < numChannels ? (float) reduction[(size_t) c][(size_t) k] * shownMix : 0.0f);
    }
}
