#include "LoFi.h"

namespace effects
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        // The band each speaker type lets through, from AMOUNT 0 to AMOUNT 10
        // (it narrows as AMOUNT rises), its resonant bump, and how hard it's driven.
        struct Speaker
        {
            float lowHz0, lowHz10, highHz0, highHz10;
            float bumpHz, bumpQ, bumpGain0, bumpGain10;
            float drive0, drive10;
            bool mono;
        };
        constexpr Speaker telephone { 250.0f, 500.0f, 3800.0f, 2400.0f, 1600.0f, 1.0f, 0.0f, 0.4f, 1.0f, 4.0f, true };
        constexpr Speaker amRadio   { 100.0f, 280.0f, 5500.0f, 3200.0f, 1400.0f, 1.2f, 0.3f, 1.0f, 1.0f, 2.5f, true };
        constexpr Speaker megaphone { 400.0f, 700.0f, 3200.0f, 2200.0f, 1100.0f, 2.5f, 0.8f, 2.5f, 3.0f, 14.0f, true };

        const Speaker& speakerFor (LoFi::Type type)
        {
            return type == LoFi::Type::amRadio ? amRadio : type == LoFi::Type::megaphone ? megaphone : telephone;
        }

        float lerp (float a, float b, float t) { return a + (b - a) * t; }

        // Noise at NOISE 10, as a gain: about -34 dB, clearly audible hiss
        // under the playing without burying it.
        constexpr float maxNoise = 0.02f;
    }

    LoFi::LoFi() : tape (4096) {}

    float LoFi::saturate (float x, float driveAmount) noexcept
    {
        // tanh clips softly; dividing by the square root of the drive keeps
        // quiet signals a little louder and loud ones a little quieter, so
        // turning AMOUNT up adds grit without a big jump in volume.
        return std::tanh (driveAmount * x) / std::sqrt (driveAmount);
    }

    void LoFi::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) maxChannels };
        for (auto* filter : { &lowCuts[0], &lowCuts[1] })
        {
            filter->prepare (spec);
            filter->setType (juce::dsp::StateVariableTPTFilterType::highpass);
        }
        for (auto* filter : { &highCuts[0], &highCuts[1] })
        {
            filter->prepare (spec);
            filter->setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        }
        resonance.prepare (spec);
        resonance.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        lastAmount = -1.0f;

        tape.setMaximumDelayInSamples ((int) std::ceil (0.02 * sampleRate) + 4);
        tape.prepare (spec);
        wowPhase = flutterPhase = drift = driftTarget = 0.0f;
        driftCountdown = 0;
        holdCountdown = 0;
        std::fill (std::begin (heldSample), std::end (heldSample), 0.0f);
        std::fill (std::begin (noiseTone), std::end (noiseTone), 0.0f);

        mix.reset (sampleRate, 0.05);
        level.reset (sampleRate, 0.05);
        bypass.prepare (sampleRate, maxBlockSize);
    }

    float LoFi::nextNoise() noexcept
    {
        return random.nextFloat() * 2.0f - 1.0f;
    }

    LoFi::Band LoFi::bandFor (Type type, float amount, double rate) noexcept
    {
        const auto t = juce::jlimit (0.0f, 10.0f, amount) / 10.0f;
        if (type == Type::cassette)
        {
            // Tape: barely any low cut, and a top end that dulls from 12 kHz
            // (a good tape) to 4 kHz (a worn one).
            return { 35.0f, lerp (12000.0f, 4000.0f, t), 0.0f, 0.0f };
        }
        if (type == Type::bitcrusher)
            return { 0.0f, 0.5f * (float) rate / (float) crusherHold (amount, rate), 0.0f, 0.0f };   // what holding leaves below its folding
        const auto& speaker = speakerFor (type);
        const auto nyquistGuard = (float) rate * 0.45f;
        return { lerp (speaker.lowHz0, speaker.lowHz10, t), juce::jmin (nyquistGuard, lerp (speaker.highHz0, speaker.highHz10, t)),
                 speaker.bumpHz, lerp (speaker.bumpGain0, speaker.bumpGain10, t) };
    }

    float LoFi::crusherBits (float amount) noexcept
    {
        return lerp (12.0f, 3.0f, juce::jlimit (0.0f, 10.0f, amount) / 10.0f);
    }

    int LoFi::crusherHold (float amount, double rate) noexcept
    {
        const auto t = juce::jlimit (0.0f, 10.0f, amount) / 10.0f;
        const auto targetRate = lerp ((float) rate, 3000.0f, t * t);
        return juce::jmax (1, juce::roundToInt ((float) rate / targetRate));
    }

    void LoFi::setUpFilters (Type type, float amount) noexcept
    {
        // Retuned only when TYPE or AMOUNT changes.
        if (type == lastType && juce::approximatelyEqual (amount, lastAmount))
            return;
        lastType = type;
        lastAmount = amount;

        const auto t = juce::jlimit (0.0f, 10.0f, amount) / 10.0f;
        // (The bitcrusher doesn't use the filters: tuned as the telephone, as on PR7-G.)
        const auto band = bandFor (type == Type::bitcrusher ? Type::telephone : type, amount, sampleRate);
        if (type == Type::cassette)
        {
            for (auto& filter : lowCuts)
                filter.setCutoffFrequency (band.lowHz);
            for (auto& filter : highCuts)
                filter.setCutoffFrequency (band.highHz);
            resonanceGain = 0.0f;
            drive = lerp (1.0f, 3.0f, t);
            return;
        }

        const auto& speaker = speakerFor (type);
        for (auto& filter : lowCuts)
            filter.setCutoffFrequency (band.lowHz);
        for (auto& filter : highCuts)
            filter.setCutoffFrequency (band.highHz);
        resonance.setCutoffFrequency (speaker.bumpHz);
        resonance.setResonance (speaker.bumpQ);
        resonanceGain = band.bumpGain / speaker.bumpQ;   // JUCE's band-pass peaks at Q
        drive = lerp (speaker.drive0, speaker.drive10, t);
    }

    void LoFi::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            mix.setTargetValue (settings.mix);
            level.setTargetValue (juce::Decibels::decibelsToGain (settings.levelDb));
            setUpFilters (settings.type, settings.amount);

            switch (settings.type)
            {
                case Type::telephone:
                case Type::amRadio:
                case Type::megaphone:   processSpeaker (wet, settings); break;
                case Type::cassette:    processCassette (wet, settings); break;
                case Type::bitcrusher:  processBitcrusher (wet, settings); break;
            }
        });
    }

    void LoFi::processSpeaker (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // PR7-G sums the two sides first, as phones, radios and megaphones
        // are mono. PR7-K keeps its stereo: each side goes through its own
        // speaker (the same speaker, so a sound in the middle comes out as
        // on PR7-G), with one line hiss on both, in the middle.
        const auto numChannels = juce::jmin ((int) block.getNumChannels(), maxChannels);
        const auto noiseGain = maxNoise * juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto hiss = noiseGain * nextNoise();
            const auto mixNow = mix.getNextValue();
            const auto gain = level.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];

                // The line hiss goes through the speaker too, so it sounds like part of it.
                auto x = sample + hiss;
                for (auto& filter : lowCuts)
                    x = filter.processSample (ch, x);
                x += resonance.processSample (ch, x) * resonanceGain;
                x = saturate (x, drive);
                for (auto& filter : highCuts)
                    x = filter.processSample (ch, x);   // after the clipping, to tame its fizz as a small speaker does

                sample = (sample * (1.0f - mixNow) + x * mixNow) * gain;
            }
        }
    }

    void LoFi::processCassette (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Wow (a slow, seasick pitch drift from an uneven capstan), flutter
        // (a fast shimmer from the tape scraping the heads) and a random
        // wander, all as a delay that moves, so the pitch bends. Then tape
        // saturation, a dulled top end and hiss.
        const auto t = juce::jlimit (0.0f, 10.0f, settings.amount) / 10.0f;
        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto wowMs = 1.6f * t, flutterMs = 0.08f * t, driftMs = 0.8f * t;
        const auto noiseGain = maxNoise * juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;
        const auto driftSmoothing = 1.0f - std::exp (-1.0f / (0.3f * (float) sampleRate));
        const auto hissTone = 1.0f - std::exp (-twoPi * 6000.0f / (float) sampleRate);
        const auto numChannels = (int) block.getNumChannels();

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            wowPhase += 0.55f / (float) sampleRate;
            wowPhase -= std::floor (wowPhase);
            flutterPhase += 7.0f / (float) sampleRate;
            flutterPhase -= std::floor (flutterPhase);
            if (--driftCountdown <= 0)
            {
                driftTarget = nextNoise();
                driftCountdown = (int) (sampleRate * 0.4);
            }
            drift += (driftTarget - drift) * driftSmoothing;

            const auto delayMs = 6.0f + wowMs * std::sin (twoPi * wowPhase) + flutterMs * std::sin (twoPi * flutterPhase) + driftMs * drift;
            const auto mixNow = mix.getNextValue();
            const auto gain = level.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                tape.pushSample (ch, sample);
                auto x = saturate (tape.popSample (ch, delayMs * msToSamples), drive);

                noiseTone[ch] += (nextNoise() - noiseTone[ch]) * hissTone;   // tape hiss is soft rather than harsh
                x += noiseGain * 1.5f * noiseTone[ch];

                for (auto& filter : lowCuts)
                    x = filter.processSample (ch, x);
                for (auto& filter : highCuts)
                    x = filter.processSample (ch, x);

                sample = (sample * (1.0f - mixNow) + x * mixNow) * gain;
            }
        }
    }

    void LoFi::processBitcrusher (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Fewer bits (from 12 at AMOUNT 0 down to 3 at 10) gives the grainy
        // fuzz of early samplers and games; holding each sample for longer
        // (an effective rate from the full rate down to 3 kHz) gives the
        // metallic, aliased ring. Both are what make it sound "digital".
        const auto bits = crusherBits (settings.amount);
        const auto steps = std::exp2 (bits - 1.0f);
        const auto holdFor = crusherHold (settings.amount, sampleRate);
        const auto noiseGain = maxNoise * juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;
        const auto numChannels = juce::jmin ((int) block.getNumChannels(), maxChannels);

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto takeNew = --holdCountdown <= 0;
            if (takeNew)
                holdCountdown = holdFor;
            const auto mixNow = mix.getNextValue();
            const auto gain = level.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                if (takeNew)
                    heldSample[ch] = std::round (juce::jlimit (-1.0f, 1.0f, sample + noiseGain * nextNoise()) * steps) / steps;
                sample = (sample * (1.0f - mixNow) + heldSample[ch] * mixNow) * gain;
            }
        }
    }
}
