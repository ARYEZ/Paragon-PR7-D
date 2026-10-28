#include "Delay.h"

namespace effects
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        float onePole (double sampleRate, float hz)
        {
            return 1.0f - std::exp (-twoPi * juce::jmin (hz, (float) sampleRate * 0.45f) / (float) sampleRate);
        }

        // Multi-tap: four repeats spread through each delay time, the last
        // the loudest and the one that feeds back. Alternate taps lean left
        // and right, so the pattern dances across the speakers.
        constexpr float tapPosition[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
        constexpr float tapGain[4] = { 0.45f, 0.6f, 0.45f, 0.85f };
        constexpr float tapPan[4] = { 0.25f, 0.75f, 0.3f, 0.5f };   // 0 left, 1 right

    }

    void Delay::Ring::prepare (int minimumLength)
    {
        data.assign ((size_t) juce::nextPowerOfTwo (juce::jmax (4, minimumLength)), 0.0f);
        mask = (int) data.size() - 1;
        write = 0;
    }

    float Delay::Ring::at (float samplesAgo) const noexcept
    {
        const auto position = (float) write - juce::jmax (1.0f, samplesAgo);
        const auto whole = (int) std::floor (position);
        const auto t = position - (float) whole;
        const auto a = data[(size_t) (whole & mask)], b = data[(size_t) ((whole + 1) & mask)];
        return a + (b - a) * t;
    }

    void Delay::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        // Room for the longest time plus the drift of the wobbling types.
        delayLine.setMaximumDelayInSamples ((int) std::ceil ((maxDelaySeconds + 0.01) * sampleRate) + 4);
        delayLine.prepare ({ sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) maxChannels });
        for (auto& ring : reverseRings)
            ring.prepare ((int) std::ceil (maxDelaySeconds * 2.0 * sampleRate) + 8);
        for (auto& shifter : shimmerShifters)
            shifter.prepare (sampleRate);

        // Time changes glide over 100 ms (a short pitch bend, like turning
        // the time knob on a tape delay) instead of jumping with a click.
        delaySamples.reset (sampleRate, 0.1);
        send.reset (sampleRate, 0.02);
        feedback.reset (sampleRate, 0.05);
        mix.reset (sampleRate, 0.05);

        ducker.prepare (sampleRate);
        resetState();
        tail.prepare (sampleRate, maxDelaySeconds * 2.0 + 0.5);   // reverse plays a chunk late, so allow for that
        wet.setSize (maxChannels, maxBlockSize);
        running = false;
    }

    void Delay::resetState() noexcept
    {
        delayLine.reset();
        std::fill (std::begin (feedbackFilter), std::end (feedbackFilter), 0.0f);
        std::fill (std::begin (feedbackFilter2), std::end (feedbackFilter2), 0.0f);
        std::fill (std::begin (lowCutState), std::end (lowCutState), 0.0f);
        for (auto& ring : reverseRings) ring.clear();
        for (auto& shifter : shimmerShifters) shifter.reset();
        reverseHeads = {};
        wowPhase = flutterPhase = drift = driftTarget = 0.0f;
        driftCountdown = 0;
        ducker.reset();
    }

    float Delay::delaySeconds (const Settings& settings, double bpm) noexcept
    {
        const auto index = juce::jlimit (0, (int) std::size (syncBeats) - 1, settings.syncIndex);
        const auto seconds = index == 0 || bpm <= 0.0
                               ? settings.timeMs * 0.001f
                               : syncBeats[index] * 60.0f / (float) bpm;
        return juce::jlimit (0.001f, maxDelaySeconds, seconds);
    }

    void Delay::process (juce::dsp::AudioBlock<float> block, const Settings& settings, double bpm) noexcept
    {
        if (settings.on && ! running)
        {
            // Starting from silence: jump straight to the right time and
            // levels rather than gliding up from wherever they were. The send
            // opens fully at once: there are no echoes yet to click against,
            // and fading it in would swallow a note played on the switch.
            resetState();
            delaySamples.setCurrentAndTargetValue (delaySeconds (settings, bpm) * (float) sampleRate);
            feedback.setCurrentAndTargetValue (settings.feedback);
            mix.setCurrentAndTargetValue (settings.mix);
            send.setCurrentAndTargetValue (1.0f);
            tail.reset();
            running = true;
            lastType = settings.type;
        }

        if (! running)
        {
            if (settings.returnOnly)
                block.clear();   // a send's return with nothing to give: silence, not the send
            return;
        }

        // A different kind of delay: start it clean rather than feeding one
        // type's echoes through another.
        if (settings.type != lastType)
        {
            resetState();
            lastType = settings.type;
        }

        delaySamples.setTargetValue (delaySeconds (settings, bpm) * (float) sampleRate);
        send.setTargetValue (settings.on ? 1.0f : 0.0f);
        feedback.setTargetValue (juce::jlimit (0.0f, 0.95f, settings.feedback));
        mix.setTargetValue (settings.mix);

        const auto type = settings.type;
        const auto ms = (float) sampleRate * 0.001f;

        // The high cut in the feedback loop that TONE sets. Digital and the
        // types built on it: 1.5 kHz (dark) to 16 kHz, one pole. Analog: a
        // bucket-brigade chip's narrower band, 800 Hz to 6 kHz, two poles.
        // Tape: 1.8 to 9 kHz, two poles, for repeats that warm as they fade.
        const auto analogue = type == Type::analog || type == Type::tape;
        const auto cutoff = type == Type::analog ? knobToFrequency (settings.tone, 800.0f, 6000.0f)
                          : type == Type::tape   ? knobToFrequency (settings.tone, 1800.0f, 9000.0f)
                                                 : knobToFrequency (settings.tone, 1500.0f, 16000.0f);
        const auto filterCoefficient = onePole (sampleRate, cutoff);
        const auto lowCut = onePole (sampleRate, type == Type::tape ? 90.0f : 60.0f);   // the analog types lose their deepest bass each pass

        // How far the read position wobbles, in samples, for the types that drift.
        const auto wowDepth = type == Type::tape ? 0.9f * ms : type == Type::analog ? 0.25f * ms : type == Type::mod ? 1.8f * ms : 0.0f;
        const auto wowRate = type == Type::tape ? 0.6f : type == Type::analog ? 0.4f : 0.9f;
        const auto flutterDepth = type == Type::tape ? 0.07f * ms : 0.0f;
        const auto driftDepth = type == Type::tape ? 0.4f * ms : 0.0f;
        const auto driftSmoothing = 1.0f - std::exp (-1.0f / (0.25f * (float) sampleRate));

        // DUCK: the Ducking type has it at least at 93% (about 18 dB).
        const auto duckAmount = type == Type::ducking ? juce::jmax (settings.duck, 0.934f) : juce::jlimit (0.0f, 1.0f, settings.duck);

        const auto numChannels = (int) block.getNumChannels();
        const auto numSamples = (int) block.getNumSamples();
        const auto pingPong = settings.pingPong && numChannels == 2;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto delayNow = delaySamples.getNextValue();
            const auto sendNow = send.getNextValue();
            const auto feedbackNow = feedback.getNextValue();
            const auto mixNow = mix.getNextValue();

            // The wobble: a slow wow, a faster flutter and (for tape) a random drift.
            wowPhase += wowRate / (float) sampleRate;
            wowPhase -= std::floor (wowPhase);
            flutterPhase += 6.5f / (float) sampleRate;
            flutterPhase -= std::floor (flutterPhase);
            if (driftDepth > 0.0f && --driftCountdown <= 0)
            {
                driftTarget = random.nextFloat() * 2.0f - 1.0f;
                driftCountdown = (int) (sampleRate * 0.5);
            }
            drift += (driftTarget - drift) * driftSmoothing;

            // What comes out of the delay: `heard` is what you hear, `looped`
            // what's fed back for the next repeat.
            float heard[maxChannels] {}, looped[maxChannels] {};
            if (type == Type::reverse)
            {
                // Two heads, half a chunk apart, read each chunk of what was
                // recorded backwards, cross-fading so there are no gaps.
                // A new time takes effect as each chunk starts, so a chunk
                // never changes length part-way through.
                const auto length = juce::jmax (64, juce::roundToInt (delayNow));
                if (reverseHeads[0].length == 0)
                {
                    reverseHeads[0] = { reverseRings[0].write, 0, length };
                    reverseHeads[1] = { reverseRings[0].write, length / 2, length };
                }
                for (auto& head : reverseHeads)
                {
                    if (head.count >= head.length)
                        head = { reverseRings[0].write, 0, length };
                    const auto window = std::sin (juce::MathConstants<float>::pi * (float) head.count / (float) head.length);
                    const auto position = head.start - 1 - head.count;
                    for (int ch = 0; ch < numChannels; ++ch)
                        heard[ch] += reverseRings[(size_t) ch].data[(size_t) (position & reverseRings[(size_t) ch].mask)] * window * window;
                    ++head.count;
                }
                for (int ch = 0; ch < numChannels; ++ch)
                    looped[ch] = heard[ch];
            }
            else if (type == Type::multiTap)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    for (int tap = 0; tap < 3; ++tap)
                    {
                        const auto pan = numChannels == 2 ? (ch == 0 ? 1.0f - tapPan[tap] : tapPan[tap]) * 2.0f : 1.0f;
                        heard[ch] += delayLine.popSample (ch, juce::jmax (1.0f, delayNow * tapPosition[tap]), false) * tapGain[tap] * pan;
                    }
                    looped[ch] = delayLine.popSample (ch, delayNow, true);
                    heard[ch] += looped[ch] * tapGain[3];
                }
            }
            else
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    // Mod's chorus runs the two sides a quarter-cycle apart, for width.
                    const auto phase = wowPhase + (type == Type::mod ? 0.25f * (float) ch : 0.0f);
                    const auto wobble = wowDepth * std::sin (twoPi * phase) + flutterDepth * std::sin (twoPi * flutterPhase)
                                      + driftDepth * drift;
                    looped[ch] = delayLine.popSample (ch, juce::jlimit (1.0f, (float) (delayLine.getMaximumDelayInSamples() - 2),
                                                                        delayNow + wobble + wowDepth + driftDepth));
                    heard[ch] = looped[ch];
                }
            }

            // The feedback path: darken (and for the analog types, saturate
            // and thin out the bass) each repeat.
            float filtered[maxChannels] {};
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto x = looped[ch];
                feedbackFilter[ch] += filterCoefficient * (x - feedbackFilter[ch]);
                x = feedbackFilter[ch];
                if (analogue)
                {
                    feedbackFilter2[ch] += filterCoefficient * (x - feedbackFilter2[ch]);
                    x = feedbackFilter2[ch];
                    lowCutState[ch] += lowCut * (x - lowCutState[ch]);
                    x -= lowCutState[ch];
                    // Soft saturation, a little more than unity gain at the
                    // centre: loud repeats compress, and FEEDBACK near the top
                    // makes them build and sing rather than just fade.
                    x = std::tanh (x * (type == Type::analog ? 1.12f : 1.25f)) / (type == Type::analog ? 1.0f : 1.15f);
                    if (type == Type::tape)
                        x += (random.nextFloat() * 2.0f - 1.0f) * 0.0004f;   // hiss
                    // You hear the darkened repeat, as from a real analog unit.
                    heard[ch] = x;
                }
                filtered[ch] = x;
            }

            // Shimmer: the repeats go round through an octave-up shifter, so
            // each one is an octave above the last.
            if (type == Type::shimmer)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto up = shimmerShifters[(size_t) ch].processSample (filtered[ch], 2.0f);
                    // Keep some of the plain repeat, so it doesn't climb out of
                    // reach at once; a soft limit keeps the wash from building
                    // past full level with FEEDBACK high.
                    filtered[ch] = std::tanh (0.55f * filtered[ch] + 0.75f * up);
                }
            }

            // DUCK: follow how loud the playing is, and turn the repeats down under it.
            auto duckGain = 1.0f;
            if (duckAmount > 0.0f)
            {
                auto level = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    level = juce::jmax (level, std::abs (block.getChannelPointer ((size_t) ch)[i]));
                duckGain = ducker.next (level, duckAmount);
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto input = block.getChannelPointer ((size_t) ch)[i];
                float toLine = 0.0f;
                if (pingPong)
                {
                    // Feed the mono guitar into the left line only, and each
                    // line's repeats into the other: echoes alternate L, R, L...
                    const auto monoIn = 0.5f * (block.getChannelPointer (0)[i] + block.getChannelPointer (1)[i]);
                    toLine = (ch == 0 ? monoIn * sendNow : 0.0f) + filtered[1 - ch] * feedbackNow;
                }
                else
                {
                    toLine = input * sendNow + filtered[ch] * feedbackNow;
                }

                if (type == Type::reverse)
                    reverseRings[(size_t) ch].push (toLine);
                else
                    delayLine.pushSample (ch, toLine);
                wet.setSample (ch, i, heard[ch]);
            }

            const auto gain = mixNow * duckGain;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                sample = (settings.returnOnly ? 0.0f : sample) + heard[ch] * gain;
            }
        }

        if (! settings.on)
        {
            juce::dsp::AudioBlock<const float> wetBlock (wet.getArrayOfReadPointers(), (size_t) numChannels, (size_t) numSamples);
            if (tail.isFinished (wetBlock))
                running = false;
        }
    }
}
