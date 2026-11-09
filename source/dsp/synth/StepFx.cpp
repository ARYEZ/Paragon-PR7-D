#include "dsp/synth/StepFx.h"

namespace synth::stepfx
{
    void Processor::prepare (double rate)
    {
        sampleRate = juce::jmax (8000.0, rate);
        ringSize = (int64_t) std::ceil (sampleRate * 16.0);
        for (auto& r : ring)
            r.assign ((size_t) ringSize, 0.0f);
        offsetDecay = (float) std::exp (-1.0 / (0.003 * sampleRate));
        gateCoefficient = (float) (1.0 - std::exp (-1.0 / (0.001 * sampleRate)));
        reset();
    }

    void Processor::reset() noexcept
    {
        for (auto& r : ring)
            std::fill (r.begin(), r.end(), 0.0f);
        written = 0;
        activeKey = -1;
        lastSlice = -1;
        gate = 1.0f;
        holdLeft = 0;
        for (int c = 0; c < 2; ++c)
            lowState[c] = bandState[c] = held[c] = offset[c] = previous[c] = beforePrevious[c] = 0.0f;
    }

    float Processor::read (int channel, double position) const noexcept
    {
        // Linear between the two samples; a place not recorded yet, or recorded over, is silence.
        const auto whole = (int64_t) std::floor (position);
        const auto frac = (float) (position - (double) whole);
        const auto at = [&] (int64_t p)
        {
            if (p < 0 || p >= written || written - p > ringSize)
                return 0.0f;
            return ring[channel][(size_t) (p % ringSize)];
        };
        return at (whole) + (at (whole + 1) - at (whole)) * frac;
    }

    void Processor::process (float* left, float* right, int numSamples, double startSteps, double stepsPerSample, double beatsPerStep,
                             const drumseq::Pattern* pattern, double lengthSteps) noexcept
    {
        if (ringSize == 0 || left == nullptr)
            return;
        using drumseq::FxType;
        const auto channels = right != nullptr ? 2 : 1;
        float* io[2] { left, right };
        const auto running = startSteps >= 0.0 && stepsPerSample > 0.0 && pattern != nullptr && lengthSteps >= 1.0;
        const auto samplesPerStep = running ? 1.0 / stepsPerSample : 0.0;
        const auto samplesPerBeat = running ? samplesPerStep / juce::jmax (1.0e-6, beatsPerStep) : 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto here = written;
            for (int c = 0; c < channels; ++c)
                ring[c][(size_t) (here % ringSize)] = io[c][i];
            ++written;

            // Which effect plays at this sample.
            auto index = -1;
            int64_t key = -1;
            double t = 0.0;
            if (running && ! pattern->fx.empty())
            {
                const auto step = startSteps + i * stepsPerSample;
                const auto loop = std::floor (step / lengthSteps);
                const auto q = step - loop * lengthSteps;
                index = pattern->fxAt (q);
                if (index >= 0)
                {
                    key = (int64_t) loop * drumseq::maxFxSteps + index;
                    t = (q - pattern->fx[(size_t) index].start) * samplesPerStep;
                }
            }
            auto jump = false;
            if (key != activeKey)
            {
                activeKey = key;
                jump = true;
                if (index >= 0)
                {
                    fx = pattern->fx[(size_t) index];
                    origin = (double) here - t;   // where the stretch began (before now, if it started part-way in)
                    tapeRead = t;
                    lastSlice = -1;
                    gate = 1.0f;
                    holdLeft = 0;
                    // FILTER starts settled on the sound coming in (from nothing it rings: a click).
                    for (int c = 0; c < 2; ++c)
                    {
                        lowState[c] = c < channels ? io[c][i] : 0.0f;
                        bandState[c] = 0.0f;
                    }
                }
            }

            float out[2] { io[0][i], channels > 1 ? io[1][i] : 0.0f };
            if (activeKey >= 0)
            {
                const auto stretch = juce::jmax (1.0, fx.length * samplesPerStep);
                const auto slice = juce::jmax (8.0, fx.rateBeats() * samplesPerBeat);
                const auto x = juce::jlimit (0.0, 1.0, t / stretch);
                switch (fx.type)
                {
                    case FxType::repeat:
                    {
                        const auto k = (int64_t) std::floor (t / slice);
                        if (k != lastSlice)
                        {
                            jump = jump || lastSlice >= 0;
                            lastSlice = k;
                        }
                        if (k > 0)
                        {
                            const auto gain = std::pow (fx.amount, (float) k);
                            for (int c = 0; c < channels; ++c)
                                out[c] = gain * read (c, origin + (t - (double) k * slice));
                        }
                        break;
                    }
                    case FxType::reverse:
                    {
                        const auto k = (int64_t) std::floor (t / slice);
                        if (k != lastSlice)
                        {
                            jump = jump || lastSlice >= 0;
                            lastSlice = k;
                        }
                        const auto u = t - (double) k * slice;
                        for (int c = 0; c < channels; ++c)
                            out[c] = read (c, origin + (double) k * slice - 1.0 - u);
                        break;
                    }
                    case FxType::chop:
                    {
                        const auto open = std::fmod (t, slice) < 0.5 * slice;
                        gate += ((open ? 1.0f : 1.0f - fx.amount) - gate) * gateCoefficient;
                        for (int c = 0; c < channels; ++c)
                            out[c] *= gate;
                        break;
                    }
                    case FxType::tapeStop:
                    {
                        const auto speed = 1.0 - fx.amount * x;
                        tapeRead += speed;
                        const auto fade = (float) juce::jlimit (0.0, 1.0, speed / 0.1);   // fading out as it nears a stop
                        const auto gain = fade * fade;
                        for (int c = 0; c < channels; ++c)
                            out[c] = gain * read (c, origin + juce::jmin (tapeRead, t));
                        break;
                    }
                    case FxType::filter:
                    {
                        // A state-variable low-pass (Q about 0.9), its cutoff sweeping down from 18 kHz.
                        const auto cutoff = 18000.0 * std::pow (150.0 / 18000.0, fx.amount * x);
                        const auto g = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin (cutoff, 0.45 * sampleRate) / sampleRate);
                        constexpr auto k = 1.1f;   // 1 / Q
                        const auto a1 = 1.0f / (1.0f + g * (g + k));
                        for (int c = 0; c < channels; ++c)
                        {
                            const auto v1 = a1 * (bandState[c] + g * (out[c] - lowState[c]));
                            const auto v2 = lowState[c] + g * v1;
                            bandState[c] = 2.0f * v1 - bandState[c];
                            lowState[c] = 2.0f * v2 - lowState[c];
                            out[c] = v2;
                        }
                        break;
                    }
                    case FxType::bitcrush:
                    {
                        if (--holdLeft < 0)
                        {
                            holdLeft = juce::roundToInt (15.0f * fx.amount * fx.amount);
                            const auto levels = std::pow (2.0f, 15.0f - 11.0f * fx.amount);
                            for (int c = 0; c < channels; ++c)
                                held[c] = std::round (out[c] * levels) / levels;
                        }
                        for (int c = 0; c < channels; ++c)
                            out[c] = held[c];
                        break;
                    }
                    case FxType::none:
                        break;
                }
            }

            // The click smoothing: at a jump, the difference from where the sound out was heading (the last two samples
            // carried on), fading away over 3 ms.
            for (int c = 0; c < channels; ++c)
            {
                if (jump)
                    offset[c] = 2.0f * previous[c] - beforePrevious[c] - out[c];
                const auto v = out[c] + offset[c];
                offset[c] *= offsetDecay;
                if (std::abs (offset[c]) < 1.0e-9f)
                    offset[c] = 0.0f;
                beforePrevious[c] = previous[c];
                previous[c] = v;
                io[c][i] = v;
            }
        }
    }
}
