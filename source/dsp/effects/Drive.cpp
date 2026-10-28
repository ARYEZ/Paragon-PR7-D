#include "Drive.h"

namespace effects
{
    namespace
    {
        constexpr float abyssOutputScale = 0.274f;      // the same for Abyss, at DIST noon and a flat EQ
        constexpr float chimeraOutputScale = 0.234f;   // the same for Chimera, at GAIN noon
        constexpr float rabidOutputScale = 0.37f;      // so LEVEL 0 dB is roughly the guitar's own level, at noon

        // TS808 component values: the op-amp's gain is 1 + (51k + DRIVE pot)
        // / 4.7k above the 4.7k / 0.047 uF corner (720 Hz); the 51 pF across
        // the feedback softens the very top; the 1k / 0.22 uF after the
        // diodes is the fixed treble cut the TONE knob works against.
        constexpr float feedbackR = 51.0e3f, drivePotR = 500.0e3f, groundR = 4.7e3f, feedbackC = 51.0e-12f;
        constexpr float toneCornerHz = 723.0f;
        constexpr float diodeVolts = 0.6f;         // about where a silicon diode starts conducting
        constexpr float screamerOutputScale = 0.35f;   // so LEVEL 0 dB is roughly the guitar's own level
        constexpr float cosmonautOutputScale = 0.25f;  // the same for the fuzzes, at SUSTAIN noon
        constexpr float haloOutputScale = 1.04f;       // the same for Halo, at FUZZ noon (the original's output is a low tap)

        float onePoleCoefficient (double rate, float hz)
        {
            return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (hz, (float) rate * 0.45f) / (float) rate);
        }
    }

    Drive::Drive()
        : oversampling (1, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true),   // 3 stages = 8x, steep filters
          screamerOversampling (2, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true)
    {
    }

    float Drive::diodeClip (float x) noexcept
    {
        // Diodes in the feedback loop round the peaks off gradually rather
        // than flattening them (softer than tanh), levelling out at about 0.6 V.
        const auto ratio = std::abs (x) / diodeVolts;
        return x / std::pow (1.0f + std::pow (ratio, 2.5f), 0.4f);
    }

    void Drive::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        oversampling.initProcessing ((size_t) maxBlockSize);
        oversampling.reset();
        screamerOversampling.initProcessing ((size_t) maxBlockSize);
        screamerOversampling.reset();
        screamerPaths.setSize (3, maxBlockSize);   // clean, clipping path, and Skyline's gate
        screamerLowCutNow = skylineAttackHz (skylineTubeScreamerAttack);   // the TS808's 4.7k and 47 nF: 720 Hz
        gateEnvelope = 0.0f;
        gateGain = 1.0f;
        screamerLowCut.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, screamerLowCutNow);
        screamerLowCut.prepare (spec);
        clipLowPassState = toneState = outputCouplingState = 0.0f;
        screamerGain.reset (sampleRate, 0.02);
        screamerTone.reset (sampleRate, 0.02);
        screamerLevel.reset (sampleRate, 0.02);
        cosmonaut.prepare (sampleRate * (double) oversampling.getOversamplingFactor(), sampleRate);
        fuzzGain.reset (sampleRate, 0.02);
        fuzzTone.reset (sampleRate, 0.02);
        fuzzLevel.reset (sampleRate, 0.02);

        rabid.prepare (sampleRate * (double) oversampling.getOversamplingFactor());
        chimera.prepare (sampleRate * (double) oversampling.getOversamplingFactor(), sampleRate);
        abyss.prepare (sampleRate * (double) oversampling.getOversamplingFactor(), sampleRate);
        halo.prepare (sampleRate * (double) oversampling.getOversamplingFactor());
        bypass.prepare (sampleRate, maxBlockSize);
    }

    void Drive::process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings) noexcept
    {
        bypass.process (monoBlock, settings.on, [&] (juce::dsp::AudioBlock<float> block)
        {
            // Switching model: start the other model's filters from silence
            // rather than from whatever they last held.
            if (settings.model != lastModel)
            {
                lastModel = settings.model;
                screamerLowCut.reset();
                screamerOversampling.reset();
                oversampling.reset();
                rabid.reset();
                chimera.reset();
                abyss.reset();
                clipLowPassState = toneState = outputCouplingState = 0.0f;
                cosmonaut.reset();
                halo.reset();
            }

            switch (settings.model)
            {
                case Model::screamer: processScreamer (block, settings, skylineAttackHz (skylineTubeScreamerAttack), 0.0f); break;
                case Model::skyline:  processScreamer (block, settings, skylineAttackHz (settings.attack), settings.gate); break;
                case Model::muff:     processCosmonaut (block, settings); break;
                case Model::halo:     processHalo (block, settings); break;
                case Model::rabid:    processRabid (block, settings); break;
                case Model::chimera:  processChimera (block, settings); break;
                case Model::abyss:    processAbyss (block, settings); break;
            }
        });
    }

    void Drive::processAbyss (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // DIST glides (per block) so turning it doesn't click; the EQ re-solves when it moves.
        fuzzGain.setTargetValue (juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f);
        fuzzLevel.setTargetValue (abyssOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));
        const auto numSamples = (int) block.getNumSamples();
        abyss.setDistortion (fuzzGain.skip (numSamples));
        const auto knob = [] (float value) { return juce::jlimit (0.0f, 10.0f, value) / 10.0f; };
        abyss.setEq (knob (settings.high), knob (settings.low), knob (settings.middle), knob (settings.midFreq));

        // The gain stages and diodes at 8x, against aliasing; the EQ after, at the base rate.
        auto upsampled = oversampling.processSamplesUp (block);
        auto* fast = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
            fast[i] = (float) abyss.clip (fast[i]);
        oversampling.processSamplesDown (block);

        auto* samples = block.getChannelPointer (0);
        for (int i = 0; i < numSamples; ++i)
            samples[i] = (float) abyss.equalise (samples[i]) * fuzzLevel.getNextValue();
    }

    void Drive::processChimera (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // GAIN and TREBLE are the pedal's pots; their positions glide (per block) so turning them doesn't click.
        fuzzGain.setTargetValue (juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f);
        fuzzTone.setTargetValue (juce::jlimit (0.0f, 10.0f, settings.tone) / 10.0f);
        fuzzLevel.setTargetValue (chimeraOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));
        const auto numSamples = (int) block.getNumSamples();
        chimera.setGain (fuzzGain.skip (numSamples));
        chimera.setTreble (fuzzTone.skip (numSamples));

        // The guitar, kept for the clean feed and bleed; the gain stage and
        // diodes at 8x; then the rest at the base rate.
        auto* samples = block.getChannelPointer (0);
        screamerPaths.copyFrom (0, 0, samples, numSamples);
        auto upsampled = oversampling.processSamplesUp (block);
        auto* fast = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
            fast[i] = (float) chimera.clip (fast[i]);
        oversampling.processSamplesDown (block);

        const auto* guitar = screamerPaths.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i)
            samples[i] = (float) chimera.finish (guitar[i], samples[i]) * fuzzLevel.getNextValue();
    }

    void Drive::processRabid (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // DISTORTION and FILTER are the pedal's pots; their positions glide (per block) so turning them doesn't click.
        fuzzGain.setTargetValue (juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f);
        fuzzTone.setTargetValue (1.0f - juce::jlimit (0.0f, 10.0f, settings.tone) / 10.0f);   // FILTER: up is darker
        fuzzLevel.setTargetValue (rabidOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));
        const auto numSamples = (int) block.getNumSamples();
        rabid.setDistortion (fuzzGain.skip (numSamples));
        rabid.setFilter (fuzzTone.skip (numSamples));

        auto upsampled = oversampling.processSamplesUp (block);
        auto* fast = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
            fast[i] = (float) rabid.process (fast[i]);
        oversampling.processSamplesDown (block);

        auto* samples = block.getChannelPointer (0);
        for (int i = 0; i < numSamples; ++i)
            samples[i] *= fuzzLevel.getNextValue();
    }

    float Drive::skylineAttackHz (int attack) noexcept
    {
        const auto nanofarads = skylineAttackNanofarads[(size_t) juce::jlimit (0, (int) std::size (skylineAttackNanofarads) - 1, attack)];
        return 1.0f / (juce::MathConstants<float>::twoPi * groundR * nanofarads * 1.0e-9f);
    }

    void Drive::processScreamer (juce::dsp::AudioBlock<float> block, const Settings& settings, float lowCutHz, float gate) noexcept
    {
        const auto numSamples = (int) block.getNumSamples();
        auto* samples = block.getChannelPointer (0);

        // The capacitor in the clipping stage's 4.7k leg (Skyline's ATTACK),
        // retuned in place when it changes: no allocation.
        if (! juce::approximatelyEqual (lowCutHz, screamerLowCutNow))
        {
            *screamerLowCut.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (sampleRate, lowCutHz);
            screamerLowCutNow = lowCutHz;
        }

        // Skyline's GATE listens to the guitar itself, before the drive, so
        // the hiss the drive adds can't hold it open. (Worked out here, applied at the end.)
        auto* gateGains = screamerPaths.getWritePointer (2);
        if (gate > 0.0f)
        {
            const auto g = juce::jlimit (0.0f, 10.0f, gate) / 10.0f;
            const auto threshold = juce::Decibels::decibelsToGain (-75.0f + 35.0f * g);   // -75 to -40 dB
            const auto ratio = 1.5f + 4.5f * g;                                          // how hard it closes: 1.5 to 6
            const auto release = 1.0f - std::exp (-1.0f / ((0.15f - 0.13f * g) * (float) sampleRate));   // 150 ms (loose) to 20 ms (fast)
            const auto attackCoefficient = 1.0f - std::exp (-1.0f / (0.0005f * (float) sampleRate));
            const auto follow = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
            for (int i = 0; i < numSamples; ++i)
            {
                gateEnvelope += (std::abs (samples[i]) - gateEnvelope) * follow;
                const auto target = gateEnvelope >= threshold ? 1.0f : std::pow (gateEnvelope / threshold, ratio - 1.0f);
                gateGain += (target - gateGain) * (target > gateGain ? attackCoefficient : release);
                gateGains[i] = gateGain;
            }
        }
        else
        {
            gateGain = 1.0f;
            std::fill (gateGains, gateGains + numSamples, 1.0f);
        }

        // DRIVE is a 500k audio-taper (logarithmic) pot, as on the pedal.
        const auto knob = juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f;
        const auto potR = drivePotR * (std::pow (10.0f, 2.0f * knob) - 1.0f) / 99.0f;
        screamerGain.setTargetValue ((feedbackR + potR) / groundR);   // the part above unity: what reaches the diodes
        const auto feedbackHz = 1.0f / (juce::MathConstants<float>::twoPi * (feedbackR + potR) * feedbackC);

        // Channel 0: the clean guitar. Channel 1: the low-cut, amplified
        // signal the diodes will clip.
        screamerPaths.copyFrom (0, 0, samples, numSamples);
        screamerPaths.copyFrom (1, 0, samples, numSamples);
        auto paths = juce::dsp::AudioBlock<float> (screamerPaths).getSubsetChannelBlock (0, 2).getSubBlock (0, (size_t) numSamples);
        auto clipPathBlock = paths.getSingleChannelBlock (1);
        juce::dsp::ProcessContextReplacing<float> clipContext (clipPathBlock);
        screamerLowCut.process (clipContext);
        auto* clipPath = paths.getChannelPointer (1);
        for (int i = 0; i < numSamples; ++i)
            clipPath[i] *= screamerGain.getNextValue();

        // At 8x the rate: soften the top (51 pF), clip, and add to the clean guitar.
        auto upsampled = screamerOversampling.processSamplesUp (paths);
        const auto fastRate = sampleRate * (double) screamerOversampling.getOversamplingFactor();
        const auto softening = onePoleCoefficient (fastRate, feedbackHz);
        auto* clean = upsampled.getChannelPointer (0);
        auto* driven = upsampled.getChannelPointer (1);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
        {
            clipLowPassState += (driven[i] - clipLowPassState) * softening;
            clean[i] += diodeClip (clipLowPassState);
        }
        screamerOversampling.processSamplesDown (paths);

        // TONE: the fixed 723 Hz treble cut, with the TONE stage adding the
        // treble back: from a little (dark) to a little more than flat (bright).
        const auto toneCut = onePoleCoefficient (sampleRate, toneCornerHz);
        const auto coupling = onePoleCoefficient (sampleRate, 20.0f);   // the output capacitor: blocks any DC from the clipping
        screamerTone.setTargetValue (0.03f + 1.37f * std::pow (juce::jlimit (0.0f, 10.0f, settings.tone) / 10.0f, 1.5f));
        screamerLevel.setTargetValue (screamerOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));

        const auto* summed = paths.getChannelPointer (0);
        for (int i = 0; i < numSamples; ++i)
        {
            toneState += (summed[i] - toneState) * toneCut;
            const auto toned = toneState + screamerTone.getNextValue() * (summed[i] - toneState);
            outputCouplingState += (toned - outputCouplingState) * coupling;
            samples[i] = (toned - outputCouplingState) * screamerLevel.getNextValue() * gateGains[i];
        }
    }

    void Drive::processCosmonaut (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // SUSTAIN and TONE are the pedal's pots; their positions glide (per block) so turning them doesn't click.
        fuzzGain.setTargetValue (juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f);
        fuzzTone.setTargetValue (juce::jlimit (0.0f, 10.0f, settings.tone) / 10.0f);
        fuzzLevel.setTargetValue (cosmonautOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));
        const auto numSamples = (int) block.getNumSamples();
        cosmonaut.setSustain (fuzzGain.skip (numSamples));
        cosmonaut.setTone (fuzzTone.skip (numSamples));

        // The three clipping-capable stages at 8x the rate, against aliasing.
        auto upsampled = oversampling.processSamplesUp (block);
        auto* fast = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
            fast[i] = (float) cosmonaut.clip (fast[i]);
        oversampling.processSamplesDown (block);

        auto* samples = block.getChannelPointer (0);
        for (int i = 0; i < numSamples; ++i)
            samples[i] = (float) cosmonaut.finish (samples[i]) * fuzzLevel.getNextValue();
    }

    void Drive::processHalo (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // The whole circuit at 8x the rate (against aliasing): FUZZ is its 1k
        // pot (Halo glides it), LEVEL stands in for its volume pot. The
        // original has no tone control, so TONE does nothing here.
        halo.setFuzz (juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f);
        fuzzLevel.setTargetValue (haloOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));
        auto upsampled = oversampling.processSamplesUp (block);
        auto* fast = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
            fast[i] = (float) halo.process (fast[i]);
        oversampling.processSamplesDown (block);

        auto* samples = block.getChannelPointer (0);
        for (int i = 0; i < (int) block.getNumSamples(); ++i)
            samples[i] *= fuzzLevel.getNextValue();
    }
}
