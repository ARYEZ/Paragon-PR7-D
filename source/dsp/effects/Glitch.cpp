#include "Glitch.h"

namespace effects
{
    namespace
    {
        constexpr double historySeconds = 8.0;     // more than the longest piece ever reaches back
        constexpr float blendSeconds = 0.0015f;    // the fade in and out of a stutter, dropout or cut: a straight line, done in
                                                   // 1.5 ms, so the edges are sharp and a cut is fully silent
    }

    void Glitch::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const auto size = juce::nextPowerOfTwo ((int) (historySeconds * sampleRate));
        history.assign ((size_t) size, 0.0f);
        echoes.assign ((size_t) size, 0.0f);
        historyMask = size - 1;
        shifter.prepare (sampleRate);
        mix.reset (sampleRate, 0.05);
        echoDelay.reset (sampleRate, 0.05);
        bypass.prepare (sampleRate, maxBlockSize);
        reset();
    }

    void Glitch::reset()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        std::fill (echoes.begin(), echoes.end(), 0.0f);
        shifter.reset();
        for (auto& grain : grains)
            grain.active = false;
        action = Action::live;
        stepRemaining = stepPlayed = echoCountdown = spawnCountdown = 0;
        blend = 0.0f;
    }

    int Glitch::lengthInSamples (const Settings& settings, double bpm) const noexcept
    {
        auto seconds = (double) settings.lengthMs / 1000.0;
        if (settings.sync)
        {
            const auto division = (size_t) juce::jlimit (0, (int) std::size (syncBeats) - 1, settings.division);
            seconds = 60.0 / (bpm > 0.0 ? bpm : 120.0) * (double) syncBeats[division];
        }
        return juce::jlimit ((int) (0.003 * sampleRate), (int) (2.0 * sampleRate), (int) (seconds * sampleRate));
    }

    float Glitch::readHistory (double position) const noexcept
    {
        const auto whole = (juce::int64) std::floor (position);
        const auto fraction = (float) (position - (double) whole);
        const auto a = history[(size_t) (whole & historyMask)], b = history[(size_t) ((whole + 1) & historyMask)];
        return a + (b - a) * fraction;
    }

    float Glitch::semitonesWithChaos (float semitones, float chaos) noexcept
    {
        // A little drift in pitch, and now and then an octave jump, both
        // growing with CHAOS.
        semitones += (chance() * 2.0f - 1.0f) * chaos * 0.05f;
        if (chance() < chaos * 0.03f)
            semitones += chance() < 0.5f ? 12.0f : -12.0f;
        return juce::jlimit (-24.0f, 24.0f, semitones);
    }

    void Glitch::process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings, double bpm, double songBeats) noexcept
    {
        if (! settings.on && bypass.isSilent())
        {
            // Off: forget what was played, so switching on doesn't replay old audio.
            if (written != 0)
            {
                reset();
                written = 0;
            }
            return;
        }

        bypass.process (monoBlock, settings.on, [&] (juce::dsp::AudioBlock<float> block)
        {
            if (settings.mode != lastMode)
            {
                // A new mode starts clean: nothing left playing from the last one.
                for (auto& grain : grains)
                    grain.active = false;
                action = Action::live;
                stepRemaining = echoCountdown = spawnCountdown = 0;
                lastMode = settings.mode;
            }

            mix.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.mix));
            const auto length = lengthInSamples (settings, bpm);
            auto* samples = block.getChannelPointer (0);

            // On the song's grid: only for synced steps, while the song plays.
            onGrid = songBeats >= 0.0 && settings.sync && bpm > 0.0
                     && (settings.mode == Mode::sequence || settings.mode == Mode::interfere || settings.mode == Mode::killswitch);
            if (! onGrid)
                lastGridStep = std::numeric_limits<juce::int64>::min();
            const auto stepBeats = (double) syncBeats[(size_t) juce::jlimit (0, (int) std::size (syncBeats) - 1, settings.division)];
            const auto beatsPerSample = bpm / 60.0 / sampleRate;

            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                const auto dry = samples[i];
                history[(size_t) (written & historyMask)] = dry;

                if (onGrid)
                {
                    // Which grid step this sample is in; a new one (or a jump,
                    // when the DAW loops or seeks) starts a step here.
                    const auto gridStep = (juce::int64) std::floor ((songBeats + (double) i * beatsPerSample) / stepBeats + 1.0e-9);
                    gridLine = gridStep != lastGridStep;
                    lastGridStep = gridStep;
                }

                float wet = 0.0f;
                switch (settings.mode)
                {
                    case Mode::pitchDelay: wet = pitchDelaySample (dry, settings, length); break;
                    case Mode::sequence:   wet = sequenceSample (dry, settings, length); break;
                    case Mode::interfere:  wet = interfereSample (dry, settings, length); break;
                    case Mode::killswitch: wet = killswitchSample (dry, settings, length); break;
                    case Mode::texturizer: wet = texturizerSample (settings, length); break;
                }

                const auto mixNow = mix.getNextValue();
                samples[i] = dry * (1.0f - mixNow) + wet * mixNow;
                ++written;
            }
        });
    }

    float Glitch::pitchDelaySample (float input, const Settings& settings, int length) noexcept
    {
        // Echoes LENGTH apart, each fed back through the pitch shifter, so
        // every repeat moves PITCH further from the last. CHAOS nudges each
        // repeat's timing and pitch.
        if (--echoCountdown <= 0)
        {
            echoCountdown = length;
            const auto jitter = 1.0f + (chance() * 2.0f - 1.0f) * settings.chaos * 0.03f;
            echoDelay.setTargetValue (juce::jlimit (32.0f, (float) (2.0 * sampleRate), (float) length * jitter));
            echoStepSemitones = semitonesWithChaos (settings.pitch, settings.chaos);
        }

        const auto delay = echoDelay.getNextValue();
        const auto readAt = (double) written - (double) delay;
        const auto whole = (juce::int64) std::floor (readAt);
        const auto fraction = (float) (readAt - (double) whole);
        const auto a = echoes[(size_t) (whole & historyMask)], b = echoes[(size_t) ((whole + 1) & historyMask)];
        const auto shifted = shifter.processSample (a + (b - a) * fraction, std::exp2 (echoStepSemitones / 12.0f));

        // DENSITY: how much of each repeat feeds the next (a few repeats to many).
        const auto feedback = juce::jlimit (0.0f, 0.92f, 0.1f + 0.082f * settings.density);
        auto next = input + shifted * feedback;
        if (std::abs (next) > 1.0f)
            next = std::tanh (next);   // keep a long climb from running away
        echoes[(size_t) (written & historyMask)] = next;
        return shifted;
    }

    void Glitch::moveBlend() noexcept
    {
        const auto target = action == Action::live ? 0.0f : 1.0f;
        const auto step = 1.0f / (blendSeconds * (float) sampleRate);
        blend = blend < target ? juce::jmin (target, blend + step) : juce::jmax (target, blend - step);
    }

    bool Glitch::startsStep() noexcept
    {
        if (! onGrid)
            return stepRemaining <= 0;   // free-running: one step straight after another

        // On the grid, steps start only on its lines. A step that ends early
        // (Interfere shortens them) goes back to the live guitar until the next line.
        if (! gridLine && stepRemaining <= 0)
            action = Action::live;
        return gridLine;
    }

    float Glitch::sequenceSample (float input, const Settings& settings, int length) noexcept
    {
        // At each step, maybe (DENSITY) repeat the slice just played for the
        // whole step instead of the live guitar. CHAOS chops the repeat finer
        // (halves, quarters, eighths of the step) and sometimes plays it backwards.
        if (startsStep())
        {
            stepRemaining = length;
            stepPlayed = 0;
            action = Action::live;
            if (chance() < settings.density / 10.0f)
            {
                const auto finest = juce::roundToInt (settings.chaos / 10.0f * 3.0f);
                const auto divide = 1 << (finest > 0 ? random.nextInt (finest + 1) : 0);
                loopLength = juce::jmax (32, length / divide);
                sliceStart = written - loopLength;
                action = chance() < settings.chaos * 0.04f ? Action::reverse : Action::repeat;
                loopStep = std::exp2 (semitonesWithChaos (settings.pitch, settings.chaos) / 12.0f);
            }
        }

        float acted = 0.0f;
        if (action != Action::live)
        {
            const auto inLoop = std::fmod ((double) stepPlayed * loopStep, (double) loopLength);
            const auto position = action == Action::reverse ? (double) sliceStart + (double) loopLength - 1.0 - inLoop
                                                            : (double) sliceStart + inLoop;
            // A short fade at each end of the loop, so the repeats don't click.
            const auto fade = (double) juce::jmin (64, loopLength / 4);
            const auto edge = juce::jmin (inLoop, (double) loopLength - inLoop);
            acted = readHistory (position) * (float) juce::jmin (1.0, edge / fade);
        }

        --stepRemaining;
        ++stepPlayed;
        moveBlend();
        return input * (1.0f - blend) + acted * blend;
    }

    float Glitch::killswitchSample (float input, const Settings& settings, int length) noexcept
    {
        // Each step plays, then cuts out for its last part (DENSITY: from a
        // tenth to nine tenths of the step), like flicking a guitar's toggle
        // to a pickup turned all the way down. CHAOS leaves some steps uncut.
        if (startsStep())
        {
            stepRemaining = length;
            stepPlayed = 0;
            const auto cutFraction = 0.1f + 0.08f * juce::jlimit (0.0f, 10.0f, settings.density);
            loopLength = chance() < settings.chaos * 0.05f ? length + 1 : (int) ((float) length * (1.0f - cutFraction));   // where the cut starts
        }

        action = stepPlayed >= loopLength && stepRemaining > 0 ? Action::drop : Action::live;
        --stepRemaining;
        ++stepPlayed;
        moveBlend();
        return input * (1.0f - blend);
    }

    float Glitch::interfereSample (float input, const Settings& settings, int length) noexcept
    {
        // The guitar in segments (LENGTH, shortened at random by CHAOS); some
        // (DENSITY) are broken: dropped out, played backwards, or caught in a
        // short buzzing loop.
        if (startsStep())
        {
            const auto shorten = 1.0f - settings.chaos / 10.0f * 0.75f * chance();
            stepRemaining = juce::jmax (32, (int) ((float) length * shorten));
            stepPlayed = 0;
            action = Action::live;
            if (chance() < settings.density / 10.0f)
            {
                const auto which = chance();
                action = which < 0.45f ? Action::drop : which < 0.8f ? Action::reverse : Action::buzz;
                loopStep = juce::jlimit (0.25, 2.0, (double) std::exp2 (semitonesWithChaos (settings.pitch, settings.chaos) / 12.0f));
                loopLength = juce::jmax (32, (int) (sampleRate * (0.003 + 0.027 * chance())));
                sliceStart = action == Action::buzz ? written - loopLength : written;
            }
        }

        float acted = 0.0f;
        if (action == Action::reverse)
        {
            acted = readHistory ((double) sliceStart - 1.0 - (double) stepPlayed * loopStep);
        }
        else if (action == Action::buzz)
        {
            const auto inLoop = std::fmod ((double) stepPlayed * loopStep, (double) loopLength);
            acted = readHistory ((double) sliceStart + inLoop);
        }

        --stepRemaining;
        ++stepPlayed;
        moveBlend();
        return input * (1.0f - blend) + acted * blend;
    }

    float Glitch::texturizerSample (const Settings& settings, int length) noexcept
    {
        // New grains DENSITY times a second (2 to 42), each LENGTH long (20 to
        // 500 ms), from somewhere in the last moments of playing (further
        // back, and more scattered, with CHAOS), smoothly faded in and out.
        const auto grainsPerSecond = 2.0f + 4.0f * juce::jlimit (0.0f, 10.0f, settings.density);
        if (--spawnCountdown <= 0)
        {
            const auto interval = (float) sampleRate / grainsPerSecond;
            spawnCountdown = juce::jmax (1, (int) (interval * (1.0f + (chance() * 2.0f - 1.0f) * settings.chaos * 0.08f)));

            for (auto& grain : grains)
            {
                if (grain.active)
                    continue;
                const auto grainLength = juce::jlimit (0.02 * sampleRate, 0.5 * sampleRate, (double) length)
                                         * (1.0 + (chance() * 2.0 - 1.0) * settings.chaos * 0.06);
                const auto rate = (double) std::exp2 (semitonesWithChaos (settings.pitch, settings.chaos) / 12.0f);
                const auto backwards = chance() < settings.chaos * 0.05f;
                const auto spread = sampleRate * (0.05 + settings.chaos * 0.1);
                const auto start = (double) written - (grainLength * rate + 4.0 + chance() * spread);

                grain.active = true;
                grain.length = (int) grainLength;
                grain.played = 0;
                grain.step = backwards ? -rate : rate;
                grain.position = backwards ? start + grainLength * rate : start;
                // Overlapping grains add up: keep the cloud about as loud as the guitar.
                grain.gain = 1.0f / std::sqrt (juce::jmax (1.0f, grainsPerSecond * (float) (grainLength / sampleRate)));
                break;
            }
        }

        auto sum = 0.0f;
        for (auto& grain : grains)
        {
            if (! grain.active)
                continue;
            const auto phase = (float) grain.played / (float) grain.length;
            const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);
            sum += readHistory (grain.position) * window * grain.gain;
            grain.position += grain.step;
            if (++grain.played >= grain.length)
                grain.active = false;
        }
        return sum;
    }
}
