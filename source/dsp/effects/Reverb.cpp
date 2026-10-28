#include "Reverb.h"

namespace effects
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        // How each type of space behaves. Delay-line lengths are scaled from
        // a base set chosen not to share common factors (so their echoes
        // don't line up into a pitched ring); a bigger scale is a bigger
        // room. Decay times are the time for the tail to fall by 60 dB, from
        // SIZE 0 to SIZE 1.
        struct Space
        {
            float scale0, scale1;          // delay-line lengths, from SIZE 0 to 1
            float decay0, decay1;          // seconds
            float earlyLevel, earlyScale;  // early reflections: how loud, how spread out
            float diffusion;               // input all-pass gain: higher is denser from the start
            float brightness;              // multiplies TONE's damping frequency
            float modulationSamples;       // how far the lines' lengths drift, to soften the tail
            float modulationHz;
            float lowCutHz;
            float level;                   // output level, so the types sit at a similar loudness
        };

        const Space& spaceFor (Reverb::Type type)
        {
            static const Space room      { 0.35f, 0.65f, 0.3f, 1.4f, 0.9f, 0.55f, 0.6f, 1.0f, 2.0f, 0.9f, 140.0f, 1.2f };
            static const Space hall      { 0.95f, 1.35f, 1.3f, 5.0f, 0.45f, 1.0f, 0.65f, 0.85f, 6.0f, 0.6f, 90.0f, 0.8f };
            static const Space cathedral { 1.55f, 2.1f, 3.5f, 12.0f, 0.35f, 1.8f, 0.7f, 0.6f, 9.0f, 0.4f, 70.0f, 0.55f };
            static const Space plate     { 0.32f, 0.5f, 0.8f, 4.5f, 0.0f, 1.0f, 0.75f, 1.35f, 3.0f, 1.1f, 110.0f, 0.35f };
            static const Space digital   { 1.0f, 1.5f, 1.0f, 8.0f, 0.2f, 0.8f, 0.7f, 1.2f, 22.0f, 0.45f, 90.0f, 0.8f };
            static const Space reverse   { 0.9f, 1.2f, 0.6f, 2.2f, 0.0f, 1.0f, 0.7f, 1.0f, 6.0f, 0.6f, 120.0f, 0.9f };
            switch (type)
            {
                case Reverb::Type::room:      return room;
                case Reverb::Type::cathedral: return cathedral;
                case Reverb::Type::plate:     return plate;
                case Reverb::Type::digital:   return digital;
                case Reverb::Type::reverse:   return reverse;
                case Reverb::Type::classic:
                case Reverb::Type::spring:
                case Reverb::Type::hall:      break;
            }
            return hall;
        }

        constexpr float lineMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.5f, 67.1f, 73.3f };
        constexpr float diffuserMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
        constexpr float earlyMs[2][6] = { { 7.1f, 11.3f, 17.9f, 23.3f, 31.7f, 39.1f }, { 8.3f, 13.1f, 19.7f, 26.9f, 33.1f, 42.7f } };
        constexpr float earlyGain[6] = { 0.6f, -0.5f, 0.42f, -0.35f, 0.28f, -0.22f };
        constexpr float maxScale = 2.1f, maxModulation = 24.0f, maxEarlyScale = 1.8f;
        constexpr float networkGain = 1.4f;   // the network's output, so the new types sit near CLASSIC's level

        // Each line's sign in the left and right outputs: different patterns,
        // so the two sides are uncorrelated, which is what makes it wide.
        constexpr float outLeft[8] = { 1, -1, 1, -1, 1, -1, 1, -1 };
        constexpr float outRight[8] = { 1, 1, -1, -1, 1, 1, -1, -1 };

        float onePole (double sampleRate, float hz)
        {
            return 1.0f - std::exp (-twoPi * juce::jmin (hz, (float) sampleRate * 0.45f) / (float) sampleRate);
        }

        float lerp (float a, float b, float t) { return a + (b - a) * t; }
        float logLerp (float a, float b, float t) { return a * std::pow (b / a, t); }
    }

    void Reverb::Line::prepare (int minimumLength)
    {
        data.assign ((size_t) juce::nextPowerOfTwo (juce::jmax (4, minimumLength)), 0.0f);
        mask = (int) data.size() - 1;
        write = 0;
    }

    float Reverb::Line::at (float delay) const noexcept
    {
        const auto position = (float) write - juce::jmax (1.0f, delay);
        const auto whole = (int) std::floor (position);
        const auto t = position - (float) whole;
        const auto a = data[(size_t) (whole & mask)], b = data[(size_t) ((whole + 1) & mask)];
        return a + (b - a) * t;
    }

    float Reverb::decaySeconds (Type type, float size) noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, size);
        if (type == Type::spring)
            return logLerp (1.2f, 4.5f, t);
        const auto& space = spaceFor (type);
        return logLerp (space.decay0, space.decay1, t);
    }

    void Reverb::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const auto ms = (float) sampleRate * 0.001f;
        classic.prepare ({ sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) maxChannels });

        preDelayLine.setMaximumDelayInSamples ((int) std::ceil (maxPreDelayMs * ms) + 4);
        preDelayLine.prepare ({ sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) maxChannels });

        for (size_t i = 0; i < lines.size(); ++i)
            lines[i].prepare ((int) std::ceil (lineMs[i] * maxScale * ms + maxModulation) + 8);
        for (size_t i = 0; i < diffusers.size(); ++i)
            diffusers[i].prepare ((int) std::ceil (diffuserMs[i % 4] * 1.12f * ms) + 8);
        for (auto& line : earlyTaps)
            line.prepare ((int) std::ceil (45.0f * maxEarlyScale * ms) + 8);
        for (auto& line : springLoops)
            line.prepare ((int) std::ceil (55.0f * ms) + 8);
        for (auto& line : reverseBuffers)
            line.prepare ((int) std::ceil (sampleRate * 3.2));

        send.reset (sampleRate, 0.02);
        mix.reset (sampleRate, 0.05);
        preDelay.reset (sampleRate, 0.1);
        ducker.prepare (sampleRate);
        wet.setSize (maxChannels, maxBlockSize);
        tail.prepare (sampleRate, 1.6);   // longer than a reversed swell, which can sound after a quiet gap
        running = false;
        lastSize = lastDamping = -1.0f;
        resetAll();
    }

    void Reverb::resetAll() noexcept
    {
        classic.reset();
        preDelayLine.reset();
        for (auto* group : { &lines })
            for (auto& line : *group)
                line.clear();
        for (auto& line : diffusers) line.clear();
        for (auto& line : earlyTaps) line.clear();
        for (auto& line : springLoops) line.clear();
        for (auto& line : reverseBuffers) line.clear();
        lineDamping.fill (0.0f);
        for (size_t i = 0; i < linePhase.size(); ++i)
            linePhase[i] = (float) i / (float) numLines;   // spread out, so the lines don't drift together
        std::fill (std::begin (outputLowCut), std::end (outputLowCut), 0.0f);
        for (auto& channel : springAllPass)
            for (auto& stage : channel)
                stage[0] = stage[1] = 0.0f;
        for (auto& stages : springLowPass) stages[0] = stages[1] = 0.0f;
        for (auto& stages : springOutput) stages[0] = stages[1] = 0.0f;
        std::fill (std::begin (springHighPass), std::end (springHighPass), 0.0f);
        std::fill (std::begin (springInputHigh), std::end (springInputHigh), 0.0f);
        reverseHeads[0] = { 0, 0 };
        reverseHeads[1] = { 0, 0 };
        reverseLength = 0;
    }

    void Reverb::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        if (settings.on && ! running)
        {
            resetAll();
            send.setCurrentAndTargetValue (1.0f);   // no tail yet, so nothing to click against
            mix.setCurrentAndTargetValue (settings.mix);
            preDelay.setCurrentAndTargetValue (settings.preDelayMs * (float) sampleRate * 0.001f);
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

        // A new type is a different space: start it empty rather than
        // feeding one space's echoes into another.
        if (settings.type != lastType)
        {
            resetAll();
            lastSize = lastDamping = -1.0f;
            lastType = settings.type;
        }

        send.setTargetValue (settings.on ? 1.0f : 0.0f);
        mix.setTargetValue (settings.mix);
        preDelay.setTargetValue (juce::jlimit (0.0f, maxPreDelayMs, settings.preDelayMs) * (float) sampleRate * 0.001f);

        const auto numChannels = (int) block.getNumChannels();
        const auto numSamples = (int) block.getNumSamples();

        // The send (muted for trails), then the pre-delay.
        for (int ch = 0; ch < numChannels; ++ch)
            wet.copyFrom (ch, 0, block.getChannelPointer ((size_t) ch), numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto gain = send.getNextValue();
            const auto delay = preDelay.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                preDelayLine.pushSample (ch, wet.getSample (ch, i) * gain);
                wet.setSample (ch, i, preDelayLine.popSample (ch, delay));
            }
        }

        juce::dsp::AudioBlock<float> wetBlock (wet.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);
        switch (settings.type)
        {
            case Type::classic: processClassic (wetBlock, settings); break;
            case Type::spring:  processSpring (wetBlock, settings); break;
            case Type::reverse:
                processNetwork (wetBlock, settings);
                reverseChunks (wetBlock, settings);
                break;
            case Type::room:
            case Type::hall:
            case Type::cathedral:
            case Type::plate:
            case Type::digital: processNetwork (wetBlock, settings); break;
        }

        // Mix in the reverb, ducked under the playing if DUCK is up (the
        // block still holds just the playing at this point).
        const auto duckAmount = juce::jlimit (0.0f, 1.0f, settings.duck);
        for (int i = 0; i < numSamples; ++i)
        {
            auto gain = mix.getNextValue();
            if (duckAmount > 0.0f)
            {
                auto level = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    level = juce::jmax (level, std::abs (block.getChannelPointer ((size_t) ch)[i]));
                gain *= ducker.next (level, duckAmount);
            }
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                sample = (settings.returnOnly ? 0.0f : sample) + wet.getSample (ch, i) * gain;
            }
        }

        if (! settings.on && tail.isFinished (wetBlock))
            running = false;
    }

    void Reverb::processClassic (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept
    {
        juce::dsp::Reverb::Parameters parameters;
        parameters.roomSize = juce::jlimit (0.0f, 1.0f, settings.size);
        parameters.damping = 1.0f - juce::jlimit (0.0f, 10.0f, settings.tone) / 10.0f;
        parameters.wetLevel = 1.0f;
        parameters.dryLevel = 0.0f;
        parameters.width = 1.0f;
        if (! juce::approximatelyEqual (parameters.roomSize, lastSize) || ! juce::approximatelyEqual (parameters.damping, lastDamping))
        {
            classic.setParameters (parameters);
            lastSize = parameters.roomSize;
            lastDamping = parameters.damping;
        }
        classic.process (juce::dsp::ProcessContextReplacing<float> (wetBlock));
    }

    void Reverb::processNetwork (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept
    {
        const auto& space = spaceFor (settings.type);
        const auto t = juce::jlimit (0.0f, 1.0f, settings.size);
        const auto ms = (float) sampleRate * 0.001f;
        const auto scale = lerp (space.scale0, space.scale1, t);

        // Reverse plays back chunks of the tail, so its own tail is set by
        // the chunk length rather than straight from SIZE.
        const auto decay = settings.type == Type::reverse ? lerp (0.6f, 2.2f, t) : decaySeconds (settings.type, t);

        std::array<float, numLines> length {}, feedback {};
        for (size_t i = 0; i < (size_t) numLines; ++i)
        {
            length[i] = std::round (lineMs[i] * scale * ms) + (float) i;
            // Each pass through a line loses the right amount for the whole
            // tail to fall 60 dB in `decay` seconds.
            feedback[i] = std::pow (10.0f, -3.0f * length[i] / (decay * (float) sampleRate));
        }

        const auto damping = onePole (sampleRate, knobToFrequency (settings.tone, 1500.0f, 16000.0f) * space.brightness);
        const auto lowCut = onePole (sampleRate, space.lowCutHz);
        const auto modulationStep = space.modulationHz / (float) sampleRate;
        const auto numChannels = (int) wetBlock.getNumChannels();

        for (size_t i = 0; i < wetBlock.getNumSamples(); ++i)
        {
            float input[maxChannels] {}, early[maxChannels] {};
            for (int ch = 0; ch < numChannels; ++ch)
                input[ch] = wetBlock.getChannelPointer ((size_t) ch)[i];
            if (numChannels == 1)
                input[1] = input[0];

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                // Early reflections: the first echoes off nearby walls.
                earlyTaps[(size_t) ch].push (input[ch]);
                if (space.earlyLevel > 0.0f)
                    for (int tap = 0; tap < 6; ++tap)
                        early[ch] += earlyGain[tap] * earlyTaps[(size_t) ch].at (earlyMs[ch][tap] * space.earlyScale * ms);
                early[ch] *= space.earlyLevel;

                // Input diffusion: four all-passes smear each sound into a
                // dense burst before it enters the network.
                auto x = input[ch] + early[ch] * 0.5f;
                for (int stage = 0; stage < 4; ++stage)
                {
                    auto& line = diffusers[(size_t) (ch * 4 + stage)];
                    const auto delayed = line.at (diffuserMs[stage] * (ch == 0 ? 1.0f : 1.12f) * ms);
                    const auto v = x + space.diffusion * delayed;
                    line.push (v);
                    x = delayed - space.diffusion * v;
                }
                input[ch] = x;
            }

            // The network: read each line (its length drifting slightly),
            // darken it, and lose a little for the decay.
            std::array<float, numLines> out {};
            auto sum = 0.0f;
            for (size_t l = 0; l < (size_t) numLines; ++l)
            {
                linePhase[l] += modulationStep * (1.0f + 0.13f * (float) l);
                linePhase[l] -= std::floor (linePhase[l]);
                const auto drift = space.modulationSamples * std::sin (twoPi * linePhase[l]);
                auto y = lines[l].at (length[l] + drift);
                lineDamping[l] += (y - lineDamping[l]) * damping;   // highs fade faster than lows, as in a real room
                out[l] = lineDamping[l] * feedback[l];
                sum += out[l];
            }

            // Mix the lines into each other (a Householder matrix: every
            // line gets a little of all the others, without losing energy)
            // and add the new input, left into the even lines, right into the odd.
            const auto spread = sum * (2.0f / (float) numLines);
            for (size_t l = 0; l < (size_t) numLines; ++l)
                lines[l].push (out[l] - spread + input[l % 2] * 0.5f);

            float left = 0.0f, right = 0.0f;
            for (size_t l = 0; l < (size_t) numLines; ++l)
            {
                left += outLeft[l] * out[l];
                right += outRight[l] * out[l];
            }
            float result[maxChannels] = { (left * networkGain + early[0]) * space.level, (right * networkGain + early[1]) * space.level };

            // Low cut on the way out (once, not on every pass round the
            // network, where it would shorten the tail): keeps the reverb
            // from turning the low strings to mud.
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                outputLowCut[ch] += (result[ch] - outputLowCut[ch]) * lowCut;
                result[ch] -= outputLowCut[ch];
            }
            for (int ch = 0; ch < numChannels; ++ch)
                wetBlock.getChannelPointer ((size_t) ch)[i] = numChannels == 1 ? 0.5f * (result[0] + result[1]) : result[ch];
        }
    }

    void Reverb::processSpring (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept
    {
        // An amp's spring tank: the signal goes down a spring and back many
        // times. A spring carries low frequencies more slowly than high
        // ones, so each echo arrives as a falling "chirp", smeared out in
        // time: that's the drip and the boing. Modelled as a delay loop
        // through a chain of all-pass filters (which delay the lows more than
        // the highs), band-limited the way a real tank's transducers are.
        const auto t = juce::jlimit (0.0f, 1.0f, settings.size);
        const auto ms = (float) sampleRate * 0.001f;
        const auto decay = decaySeconds (Type::spring, t);
        constexpr float springMs[maxChannels] = { 33.0f, 41.0f };   // two springs of different lengths, for stereo
        constexpr float dispersion = -0.6f;
        const auto toneCut = onePole (sampleRate, knobToFrequency (settings.tone, 2200.0f, 6500.0f));
        const auto pickupCut = onePole (sampleRate, 4500.0f);   // the output transducer: little gets through above 5 kHz
        const auto loopLowCut = onePole (sampleRate, 120.0f);
        const auto inputLowCut = onePole (sampleRate, 300.0f);
        const auto numChannels = (int) wetBlock.getNumChannels();

        for (size_t i = 0; i < wetBlock.getNumSamples(); ++i)
        {
            auto input = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                input += wetBlock.getChannelPointer ((size_t) ch)[i];
            input /= (float) numChannels;

            float result[maxChannels] {};
            for (int s = 0; s < maxChannels; ++s)
            {
                const auto loopSamples = springMs[s] * lerp (0.85f, 1.15f, t) * ms;
                const auto feedback = std::pow (10.0f, -3.0f * loopSamples / (decay * (float) sampleRate));

                // The driver only passes the mids: the tank's thin, bright sound.
                springInputHigh[s] += (input - springInputHigh[s]) * inputLowCut;
                const auto driven = input - springInputHigh[s];

                auto x = springLoops[(size_t) s].at (loopSamples);
                for (int stage = 0; stage < springStages; ++stage)
                {
                    auto& state = springAllPass[s][stage];
                    const auto y = dispersion * x + state[0] - dispersion * state[1];
                    state[0] = x;
                    state[1] = y;
                    x = y;
                }
                // Two poles of treble loss per trip down the spring.
                springLowPass[s][0] += (x - springLowPass[s][0]) * toneCut;
                springLowPass[s][1] += (springLowPass[s][0] - springLowPass[s][1]) * toneCut;
                springHighPass[s] += (springLowPass[s][1] - springHighPass[s]) * loopLowCut;
                const auto shaped = springLowPass[s][1] - springHighPass[s];
                springLoops[(size_t) s].push (driven + feedback * shaped);

                springOutput[s][0] += (shaped - springOutput[s][0]) * pickupCut;
                springOutput[s][1] += (springOutput[s][0] - springOutput[s][1]) * pickupCut;
                result[s] = springOutput[s][1] * 1.6f;
            }
            for (int ch = 0; ch < numChannels; ++ch)
                wetBlock.getChannelPointer ((size_t) ch)[i] = numChannels == 1 ? 0.5f * (result[0] + result[1]) : result[ch];
        }
    }

    void Reverb::reverseChunks (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept
    {
        // The reverb's tail, recorded and played backwards in chunks: each
        // chunk is the last moment of the tail run in reverse, so it starts
        // quiet and swells up. Two read heads half a chunk apart cross-fade
        // (sine-squared windows, which add up to one), so there are no gaps
        // or clicks between chunks.
        const auto t = juce::jlimit (0.0f, 1.0f, settings.size);
        const auto length = juce::jmax (64, (int) ((0.25f + 1.25f * t) * (float) sampleRate));
        if (length != reverseLength)
        {
            reverseLength = length;
            reverseHeads[0] = { reverseBuffers[0].write, 0 };
            reverseHeads[1] = { reverseBuffers[0].write, length / 2 };
        }

        const auto numChannels = (int) wetBlock.getNumChannels();
        for (size_t i = 0; i < wetBlock.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < maxChannels; ++ch)
                reverseBuffers[(size_t) ch].push (wetBlock.getChannelPointer ((size_t) juce::jmin (ch, numChannels - 1))[i]);

            float out[maxChannels] {};
            for (auto& head : reverseHeads)
            {
                if (head.count >= length)
                    head = { reverseBuffers[0].write, 0 };   // start a new chunk from the latest moment
                const auto window = std::sin (juce::MathConstants<float>::pi * (float) head.count / (float) length);
                const auto gain = window * window;
                const auto position = head.start - 1 - head.count;   // backwards through the recording
                for (int ch = 0; ch < maxChannels; ++ch)
                {
                    const auto& buffer = reverseBuffers[(size_t) ch];
                    out[ch] += buffer.data[(size_t) (position & buffer.mask)] * gain;
                }
                ++head.count;
            }
            for (int ch = 0; ch < numChannels; ++ch)
                wetBlock.getChannelPointer ((size_t) ch)[i] = out[ch];
        }
    }
}
