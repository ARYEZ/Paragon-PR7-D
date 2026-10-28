// PR7-G 0.52.0's MOD and LO-FI tests (stage 7c), unchanged but for:
//  - the rig: PR7-K has no audio input, so `EffectRig` stands in for PR7-G's plugin. It keeps the settings in a
//    PR7-K processor's parameters (the same IDs, ranges and defaults, read by the same getModSettings /
//    getLoFiSettings the plugin uses) and runs the input, copied to both sides as PR7-G's stereo stages get it,
//    through MOD, MOD 2 and LO-FI as PR7-G's chain does;
//  - the Vortex's STEREO: on PR7-G the chorus alone on the left and the guitar alone on the right; PR7-K keeps each
//    side's sound and adds the chorus on the left, takes it away on the right. So those tests read the pedal's two
//    jacks back from PR7-K's sides (`pedalJacks`: half the difference, half the sum);
//  - the speaker types: mono on PR7-G, each side its own on PR7-K (that test replaced);
//  - MOD in PRE (before PR7-G's amp): not on PR7-K (that test left out).
// PR7-K's own (each side on its own, in the synth, the page) are in ModFxStereoTests.cpp.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Aeon.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Modulation.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct EffectRig
    {
        EffectRig()
        {
            for (auto& block : modulation)
                block.prepare (sampleRate, blockSize);
            loFi.prepare (sampleRate, blockSize);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        std::vector<std::vector<float>> run (const std::vector<float>& left, const std::vector<float>& right)
        {
            std::vector<std::vector<float>> output (2);
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (size_t start = 0; start < left.size(); start += blockSize)
            {
                const auto count = (int) std::min<size_t> (blockSize, left.size() - start);
                buffer.setSize (2, count, false, false, true);
                std::copy (left.begin() + (long) start, left.begin() + (long) start + count, buffer.getWritePointer (0));
                std::copy (right.begin() + (long) start, right.begin() + (long) start + count, buffer.getWritePointer (1));
                juce::dsp::AudioBlock<float> block (buffer);
                modulation[0].process (block, processor.getModSettings (0));   // at the processor's tempo: 120 BPM
                modulation[1].process (block, processor.getModSettings (1));
                loFi.process (block, processor.getLoFiSettings());
                for (int ch = 0; ch < 2; ++ch)
                    output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + count);
            }
            return output;
        }

        std::vector<std::vector<float>> run (const std::vector<float>& input) { return run (input, input); }
        std::vector<float> runMono (const std::vector<float>& input) { return run (input)[0]; }

        ParagonX1AudioProcessor processor;
        std::array<effects::Modulation, 2> modulation;
        effects::LoFi loFi;
    };

    // PR7-G's Vortex in STEREO had the chorus alone on its A jack (left) and the guitar on B (right). PR7-K's sides
    // are guitar + chorus and guitar - chorus: the jacks are half their difference and half their sum.
    std::vector<std::vector<float>> pedalJacks (const std::vector<std::vector<float>>& sides)
    {
        std::vector<std::vector<float>> jacks (2, std::vector<float> (sides[0].size()));
        for (size_t i = 0; i < sides[0].size(); ++i)
        {
            jacks[0][i] = 0.5f * (sides[0][i] - sides[1][i]);
            jacks[1][i] = 0.5f * (sides[0][i] + sides[1][i]);
        }
        return jacks;
    }

    std::vector<float> sine (double hz, float amplitude, double seconds)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        return samples;
    }

    std::vector<float> impulse (double seconds, float amplitude = 1.0f)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate), 0.0f);
        samples[0] = amplitude;
        return samples;
    }

    std::vector<float> silence (double seconds) { return std::vector<float> ((size_t) (seconds * sampleRate), 0.0f); }

    float rms (const std::vector<float>& samples, double fromSeconds = 0.0, double toSeconds = 1.0e9)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto to = std::min (samples.size(), (size_t) (toSeconds * sampleRate));
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += samples[i] * samples[i];
        return (float) std::sqrt (sum / (double) std::max<size_t> (1, to - from));
    }

    float dB (float ratio) { return 20.0f * std::log10 (ratio); }

    // Level of one frequency in a signal (Goertzel algorithm), skipping the
    // first `fromSeconds` so filters and fades have settled.
    float levelAt (const std::vector<float>& samples, double hz, double fromSeconds = 0.1)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto n = samples.size() - from;
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (float) (2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) n);
    }

    bool allFinite (const std::vector<float>& samples)
    {
        return std::all_of (samples.begin(), samples.end(), [] (float s) { return std::isfinite (s); });
    }

    float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
    {
        float result = 0.0f;
        for (auto i = from; i < std::min (a.size(), b.size()); ++i)
            result = std::max (result, std::abs (a[i] - b[i]));
        return result;
    }
}

TEST_CASE ("Modulation effects", "[effects][mod]")
{
    EffectRig rig;
    rig.set (ParamIDs::modOn, 1.0f);
    const auto input = sine (440.0, 0.3f, 1.0);

    // The difference between two signals, sample by sample.
    const auto minus = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> difference (std::min (a.size(), b.size()));
        for (size_t i = 0; i < difference.size(); ++i)
            difference[i] = a[i] - b[i];
        return difference;
    };

    SECTION ("Vortex, MONO: the guitar untouched, the chorus added on top, the same both sides")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
        rig.set (ParamIDs::modMix, 0.0f);   // E.LEVEL down: just the guitar, exactly
        const auto dryOnly = rig.run (input);
        CHECK (maxAbsDifference (dryOnly[0], input, (size_t) (0.1 * sampleRate)) < 1.0e-6f);

        rig.set (ParamIDs::modMix, 1.0f);
        const auto output = rig.run (input);
        REQUIRE (allFinite (output[0]));
        CHECK (maxAbsDifference (output[0], output[1]) < 1.0e-6f);              // one chip: mono
        CHECK (rms (minus (output[0], input), 0.2) > 0.5f * rms (input, 0.2));   // the chorus is there, at about the guitar's level
    }

    SECTION ("Vortex, STEREO: chorus alone on the left, the guitar alone on the right, as the pedal's A and B outputs")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
        rig.set (ParamIDs::modStereo, 1.0f);
        rig.set (ParamIDs::modMix, 1.0f);
        const auto output = pedalJacks (rig.run (input));
        REQUIRE (allFinite (output[0]));
        CHECK (maxAbsDifference (output[1], input, (size_t) (0.1 * sampleRate)) < 1.0e-6f);
        // The left is the chorus alone: at full E.LEVEL a little louder than
        // the guitar (as the pedal's knob goes past an even mix), and not the guitar.
        CHECK_THAT (dB (rms (output[0], 0.2) / rms (input, 0.2)), WithinAbs (3.5, 1.0));
        CHECK (rms (minus (output[0], input), 0.2) > 0.3f * rms (input, 0.2));

        // E.LEVEL down moves it towards the dry side, as on the pedal.
        rig.set (ParamIDs::modMix, 0.25f);
        const auto quieter = pedalJacks (rig.run (input));
        CHECK (dB (rms (quieter[0], 0.2) / rms (output[0], 0.2)) < -10.0f);
    }

    SECTION ("MOD 2 runs a second effect after MOD: a chorus and a flanger together")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
        rig.set (ParamIDs::modMix, 0.7f);
        const auto chorusOnly = rig.run (input);

        rig.set (ParamIDs::modOn, 0.0f);
        rig.set (ParamIDs::mod2On, 1.0f);
        rig.set (ParamIDs::mod2Type, (float) effects::Modulation::Type::flanger);
        const auto flangerOnly = rig.run (input);

        rig.set (ParamIDs::modOn, 1.0f);
        const auto both = rig.run (input);
        REQUIRE (allFinite (both[0]));
        // Both together sound like neither alone.
        CHECK (rms (minus (both[0], chorusOnly[0]), 0.3) > 0.1f * rms (both[0], 0.3));
        CHECK (rms (minus (both[0], flangerOnly[0]), 0.3) > 0.1f * rms (both[0], 0.3));

        // MOD 2 has its own controls: MOD's don't change it. (Two fresh
        // plugins, so both sweeps start at the same point.)
        const auto mod2FlangerWith = [&] (float modRate)
        {
            EffectRig fresh;
            fresh.set (ParamIDs::mod2On, 1.0f);
            fresh.set (ParamIDs::mod2Type, (float) effects::Modulation::Type::flanger);
            fresh.set (ParamIDs::modRate, modRate);
            return fresh.run (input)[0];
        };
        CHECK (maxAbsDifference (mod2FlangerWith (0.8f), mod2FlangerWith (5.0f)) < 1.0e-6f);

        // MOD 2's AEON has its own MANUAL and RES: they change its sound, MOD's don't.
        const auto mod2AeonWith = [&] (const char* id, float value)
        {
            EffectRig fresh;
            fresh.set (ParamIDs::mod2On, 1.0f);
            fresh.set (ParamIDs::mod2Type, (float) effects::Modulation::Type::flanger);
            fresh.set (id, value);
            return fresh.run (input)[0];
        };
        const auto aeon = mod2AeonWith (ParamIDs::mod2Manual, 5.0f);
        CHECK (rms (minus (mod2AeonWith (ParamIDs::mod2Manual, 1.0f), aeon), 0.3) > 0.05f * rms (aeon, 0.3));
        CHECK (rms (minus (mod2AeonWith (ParamIDs::mod2Res, 10.0f), mod2AeonWith (ParamIDs::mod2Res, 0.0f)), 0.3) > 0.05f * rms (aeon, 0.3));
        CHECK (maxAbsDifference (mod2AeonWith (ParamIDs::modManual, 1.0f), aeon) < 1.0e-6f);
    }

    SECTION ("Vortex: DEPTH bends the pitch, and the sweep is uneven, as the chip's clock is what's swept")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
        rig.set (ParamIDs::modStereo, 1.0f);   // the chorus alone, on the left
        rig.set (ParamIDs::modMix, 1.0f);
        rig.set (ParamIDs::modRate, 1.0f);
        const auto tone = sine (1000.0, 0.3f, 2.0);

        rig.set (ParamIDs::modDepth, 0.0f);
        const auto still = pedalJacks (rig.run (tone))[0];
        rig.set (ParamIDs::modDepth, 1.0f);
        const auto swept = pedalJacks (rig.run (tone))[0];

        // With no depth the chorus is a steady (delayed) 1 kHz; with full
        // depth its pitch wanders, so less of it stays exactly at 1 kHz.
        CHECK (levelAt (still, 1000.0, 0.3) > 0.8f * rms (still, 0.3) * std::sqrt (2.0f));
        CHECK (levelAt (swept, 1000.0, 0.3) < 0.6f * levelAt (still, 1000.0, 0.3));
    }

    SECTION ("Vortex: EQ shapes the chorus's treble, not the guitar's")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
        rig.set (ParamIDs::modStereo, 1.0f);
        rig.set (ParamIDs::modMix, 1.0f);
        rig.set (ParamIDs::modDepth, 0.0f);
        const auto trebleOverMid = [&] (float eq)
        {
            rig.set (ParamIDs::modEq, eq);
            const auto output = pedalJacks (rig.run (sine (6000.0, 0.1f, 0.5)));
            const auto mid = pedalJacks (rig.run (sine (500.0, 0.1f, 0.5)));
            CHECK (maxAbsDifference (output[1], sine (6000.0, 0.1f, 0.5), (size_t) (0.1 * sampleRate)) < 1.0e-6f);   // the dry side untouched
            return dB (rms (output[0], 0.2) / rms (mid[0], 0.2));
        };
        const auto dark = trebleOverMid (-1.0f), flat = trebleOverMid (0.0f), bright = trebleOverMid (1.0f);
        CHECK (bright - flat > 6.0f);
        CHECK (flat - dark > 6.0f);
    }

    SECTION ("Vortex: the filters round the chorus's top end off, like the chip's")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
        rig.set (ParamIDs::modStereo, 1.0f);
        rig.set (ParamIDs::modMix, 1.0f);
        rig.set (ParamIDs::modDepth, 0.0f);
        const auto chorusLevel = [&] (double hz) { return rms (pedalJacks (rig.run (sine (hz, 0.1f, 0.5)))[0], 0.2); };
        CHECK (dB (chorusLevel (16000.0) / chorusLevel (1000.0)) < -20.0f);
        CHECK_THAT (dB (chorusLevel (3000.0) / chorusLevel (300.0)), WithinAbs (0.0, 2.5));   // flat through the guitar's range
    }

    SECTION ("Vortex: clean at normal levels; a hot, bright signal rounds off, as the manual warns")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
        rig.set (ParamIDs::modStereo, 1.0f);
        rig.set (ParamIDs::modMix, 1.0f);
        rig.set (ParamIDs::modDepth, 0.0f);
        const auto thirdHarmonic = [&] (float amplitude)
        {
            const auto output = pedalJacks (rig.run (sine (1500.0, amplitude, 0.5)))[0];
            return dB (levelAt (output, 4500.0) / levelAt (output, 1500.0));
        };
        CHECK (thirdHarmonic (0.2f) < -45.0f);
        CHECK (thirdHarmonic (0.9f) > -35.0f);
    }

    SECTION ("Carbonite: the guitar plus the chorus at full level, mono, and MIX does nothing (the pedal's is fixed 50/50)")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::carbonite);
        rig.set (ParamIDs::modMix, 0.0f);
        const auto output = rig.run (input);
        REQUIRE (allFinite (output[0]));
        CHECK (maxAbsDifference (output[0], output[1]) < 1.0e-6f);
        CHECK (rms (minus (output[0], input), 0.2) > 0.5f * rms (input, 0.2));   // the chorus is there, near the guitar's level
        // (Fresh plugins, so both sweeps start at the same point.)
        const auto withMix = [&] (float mixValue)
        {
            EffectRig fresh;
            fresh.set (ParamIDs::modOn, 1.0f);
            fresh.set (ParamIDs::modType, (float) effects::Modulation::Type::carbonite);
            fresh.set (ParamIDs::modMix, mixValue);
            return fresh.run (input)[0];
        };
        CHECK (maxAbsDifference (withMix (0.0f), withMix (1.0f)) < 1.0e-6f);
    }

    SECTION ("Carbonite: the delay sits at 3.4 ms and sweeps 2.2 to 4.5 ms at full DEPTH, from a real pedal's clock")
    {
        CHECK_THAT (effects::Modulation::carboniteDelayMs (0.0f, 0.7f), WithinAbs (3.41, 0.01));
        CHECK_THAT (effects::Modulation::carboniteDelayMs (1.0f, -1.0f), WithinAbs (4.53, 0.05));
        CHECK_THAT (effects::Modulation::carboniteDelayMs (1.0f, 1.0f), WithinAbs (2.20, 0.05));

        // With DEPTH down, an impulse's echo arrives 3.4 ms later.
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::carbonite);
        rig.set (ParamIDs::modDepth, 0.0f);
        std::vector<float> click ((size_t) (0.1 * sampleRate), 0.0f);
        click[(size_t) (0.05 * sampleRate)] = 0.5f;
        const auto output = rig.run (click)[0];
        const auto echo = minus (output, click);
        const auto peak = std::distance (echo.begin(), std::max_element (echo.begin(), echo.end(),
                                                                        [] (float a, float b) { return std::abs (a) < std::abs (b); }));
        const auto delayMs = 1000.0 * (double) (peak - (long) (0.05 * sampleRate)) / sampleRate;
        CHECK_THAT (delayMs, WithinAbs (3.41 + 0.1, 0.25));   // (plus the filters' own small delay)
    }

    SECTION ("Carbonite is darker than Vortex: its filters stop at 6.6 kHz, not 9 kHz")
    {
        rig.set (ParamIDs::modDepth, 0.0f);
        const auto chorusTreble = [&] (effects::Modulation::Type type)
        {
            rig.set (ParamIDs::modType, (float) type);
            rig.set (ParamIDs::modStereo, 1.0f);   // Vortex: its chorus alone on the left
            rig.set (ParamIDs::modMix, 1.0f);
            const auto chorusOf = [&] (double hz)
            {
                const auto tone = sine (hz, 0.1f, 0.5);
                const auto out = type == effects::Modulation::Type::carbonite ? rig.run (tone)[0] : pedalJacks (rig.run (tone))[0];
                return type == effects::Modulation::Type::carbonite ? rms (minus (out, tone), 0.2) : rms (out, 0.2);
            };
            return dB (chorusOf (10000.0) / chorusOf (1000.0));
        };
        const auto carbonite = chorusTreble (effects::Modulation::Type::carbonite);
        const auto vortex = chorusTreble (effects::Modulation::Type::chorus);
        CHECK (carbonite < -18.0f);
        CHECK (carbonite < vortex - 8.0f);
    }

    SECTION ("Carbonite: DEPTH bends the pitch")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::carbonite);
        rig.set (ParamIDs::modRate, 1.0f);
        const auto tone = sine (1000.0, 0.3f, 2.0);
        const auto chorusWith = [&] (float depthValue)
        {
            rig.set (ParamIDs::modDepth, depthValue);
            return minus (rig.run (tone)[0], tone);
        };
        const auto still = chorusWith (0.0f), swept = chorusWith (1.0f);
        CHECK (levelAt (swept, 1000.0, 0.3) < 0.8f * levelAt (still, 1000.0, 0.3));
    }

    SECTION ("Hadron: each stage's turn sweeps about 330 Hz to 3 kHz, in step with the LFO's voltage")
    {
        CHECK_THAT (effects::Modulation::hadronStageHz (effects::Modulation::hadronLfoLowVolts), WithinAbs (330.0, 15.0));
        CHECK_THAT (effects::Modulation::hadronStageHz (effects::Modulation::hadronLfoHighVolts), WithinAbs (3000.0, 50.0));
    }

    SECTION ("Hadron: the guitar plus the phase stages half and half cuts deep moving notches; MIX and DEPTH do nothing")
    {
        // White noise in: MIX and DEPTH change nothing.
        juce::Random random (7);
        std::vector<float> noise ((size_t) sampleRate);
        for (auto& sample : noise)
            sample = 0.2f * (random.nextFloat() * 2.0f - 1.0f);
        const auto withSettings = [&] (float mixValue, float depthValue, float circuit)
        {
            EffectRig fresh;
            fresh.set (ParamIDs::modOn, 1.0f);
            fresh.set (ParamIDs::modType, (float) effects::Modulation::Type::phaser);
            fresh.set (ParamIDs::modMix, mixValue);
            fresh.set (ParamIDs::modDepth, depthValue);
            fresh.set (ParamIDs::modHadron, circuit);
            return fresh.run (noise)[0];
        };
        const auto script = withSettings (0.5f, 0.5f, 0.0f);
        REQUIRE (allFinite (script));
        CHECK (maxAbsDifference (script, withSettings (1.0f, 0.0f, 0.0f)) < 1.0e-6f);

        // A sine at a stage's 45-degree point (four stages: 180 degrees) is
        // cancelled: with the LFO held low, a 137 Hz tone almost vanishes.
        EffectRig slow;
        slow.set (ParamIDs::modOn, 1.0f);
        slow.set (ParamIDs::modType, (float) effects::Modulation::Type::phaser);
        slow.set (ParamIDs::modRate, 0.05f);   // so slow it barely moves in the test
        // (Aimed at where the LFO is halfway through the measurement, 0.225 s
        // in: 0.05 Hz, rising for 65% of the cycle.)
        const auto lfoVolts = effects::Modulation::hadronLfoLowVolts
                            + (effects::Modulation::hadronLfoHighVolts - effects::Modulation::hadronLfoLowVolts) * (0.225f * 0.05f / 0.65f);
        const auto notchHz = 0.4142 * effects::Modulation::hadronStageHz (lfoVolts);
        const auto low = slow.run (sine (notchHz, 0.3f, 0.3))[0];
        CHECK (dB (rms (low, 0.15) / 0.3f * std::sqrt (2.0f)) < -20.0f);
    }

    SECTION ("Hadron: BLOCK's feedback makes the sweep stronger, and stays stable")
    {
        juce::Random random (11);
        std::vector<float> noise ((size_t) (2.0 * sampleRate));
        for (auto& sample : noise)
            sample = 0.2f * (random.nextFloat() * 2.0f - 1.0f);
        const auto run = [&] (float circuit)
        {
            EffectRig fresh;
            fresh.set (ParamIDs::modOn, 1.0f);
            fresh.set (ParamIDs::modType, (float) effects::Modulation::Type::phaser);
            fresh.set (ParamIDs::modHadron, circuit);
            return fresh.run (noise)[0];
        };
        const auto script = run (0.0f), block = run (1.0f);
        REQUIRE (allFinite (block));
        CHECK (rms (minus (block, script), 0.2) > 0.1f * rms (script, 0.2));
        CHECK (rms (block, 0.2) < 2.0f * rms (noise, 0.2));   // nothing runs away

        // The mid hump: at the stages' turn, between the two notches, BLOCK
        // is about 2.7 dB up where SCRIPT is flat.
        const auto lfoVolts = effects::Modulation::hadronLfoLowVolts
                            + (effects::Modulation::hadronLfoHighVolts - effects::Modulation::hadronLfoLowVolts) * (0.225f * 0.05f / 0.65f);
        const auto tone = sine (effects::Modulation::hadronStageHz (lfoVolts), 0.2f, 0.3);
        const auto levelWith = [&] (float circuit)
        {
            EffectRig fresh;
            fresh.set (ParamIDs::modOn, 1.0f);
            fresh.set (ParamIDs::modType, (float) effects::Modulation::Type::phaser);
            fresh.set (ParamIDs::modRate, 0.05f);
            fresh.set (ParamIDs::modHadron, circuit);
            return dB (rms (fresh.run (tone)[0], 0.15) / rms (tone, 0.15));
        };
        CHECK_THAT (levelWith (0.0f), WithinAbs (0.0, 0.5));
        CHECK_THAT (levelWith (1.0f), WithinAbs (2.7, 0.7));
    }

    SECTION ("Razor's WAVE: a triangle, a trapezoid at noon, nearly a square fully up")
    {
        // The share of each cycle spent at (near) full volume or the deepest dip.
        const auto flatShare = [] (float wave)
        {
            int flat = 0;
            for (int i = 0; i < 1000; ++i)
            {
                const auto v = effects::Modulation::razorShape ((float) i / 1000.0f, wave);
                flat += (v < 0.02f || v > 0.98f) ? 1 : 0;
            }
            return (float) flat / 1000.0f;
        };
        CHECK (flatShare (0.0f) < 0.05f);
        CHECK_THAT (flatShare (5.0f), WithinAbs (0.82, 0.05));
        CHECK (flatShare (10.0f) > 0.95f);
    }

    SECTION ("Razor: WAVE fully up chops on and off, without clicks")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::tremolo);
        rig.set (ParamIDs::modRate, 4.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        rig.set (ParamIDs::modWave, 10.0f);
        const auto output = rig.runMono (input);
        // Mostly fully on or fully off: few 10 ms windows in between.
        int between = 0, windows = 0;
        for (double t = 0.2; t < 0.95; t += 0.01, ++windows)
        {
            const auto level = rms (output, t, t + 0.01) / rms (input, 0.2);
            between += (level > 0.1f && level < 0.9f) ? 1 : 0;
        }
        CHECK ((float) between / (float) windows < 0.2f);
        // The chop is rounded off (C4): no sample jumps by more than the
        // input itself could.
        float biggestStep = 0.0f;
        for (size_t i = (size_t) (0.2 * sampleRate); i < output.size(); ++i)
            biggestStep = std::max (biggestStep, std::abs (output[i] - output[i - 1]));
        CHECK (biggestStep < 0.03f);
    }

    SECTION ("Razor only turns the volume up and down: every sample is the guitar's, scaled between 0 and 1")
    {
        rig.set (ParamIDs::modType, (float) effects::Modulation::Type::tremolo);
        rig.set (ParamIDs::modRate, 16.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        rig.set (ParamIDs::modWave, 10.0f);
        const auto note = sine (82.4, 0.3f, 1.0);   // low E
        const auto output = rig.runMono (note);
        int checked = 0;
        for (size_t i = (size_t) (0.2 * sampleRate); i < output.size(); ++i)
        {
            if (std::abs (note[i]) < 0.05f)
                continue;
            const auto gain = output[i] / note[i];
            REQUIRE (gain > -1.0e-4f);
            REQUIRE (gain < 1.0f + 1.0e-4f);
            ++checked;
        }
        CHECK (checked > 10000);
    }

    SECTION ("Razor pulses the volume at the rate setting, full DEPTH from silence to full")
    {
        rig.set (ParamIDs::modType, 3.0f);
        rig.set (ParamIDs::modRate, 4.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        const auto output = rig.runMono (input);

        // Measure loudness in 10 ms windows: full depth swings from silence to full.
        float loudest = 0.0f, quietest = 1.0f;
        for (double t = 0.2; t < 0.95; t += 0.01)
        {
            const auto level = rms (output, t, t + 0.01);
            loudest = std::max (loudest, level);
            quietest = std::min (quietest, level);
        }
        CHECK (loudest > 0.18f);
        CHECK (quietest < 0.02f);
    }

    SECTION ("Flanger and phaser change the sound and stay stable")
    {
        for (const auto type : { 1.0f, 2.0f })
        {
            rig.set (ParamIDs::modType, type);
            const auto output = rig.runMono (input);
            REQUIRE (allFinite (output));
            CHECK (maxAbsDifference (input, output, (size_t) (0.2 * sampleRate)) > 0.01f);
        }
    }
}

namespace
{
    // Loudness in 10 ms windows from `fromSeconds` on: how the volume moves.
    std::vector<float> envelope (const std::vector<float>& samples, double fromSeconds = 0.2)
    {
        std::vector<float> levels;
        for (auto t = fromSeconds; (t + 0.01) * sampleRate <= (double) samples.size(); t += 0.01)
            levels.push_back (rms (samples, t, t + 0.01));
        return levels;
    }

    // +1 when two envelopes rise and fall together, -1 when one rises as the other falls.
    double correlation (const std::vector<float>& a, const std::vector<float>& b)
    {
        const auto n = std::min (a.size(), b.size());
        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < n; ++i) { meanA += a[i]; meanB += b[i]; }
        meanA /= (double) n;
        meanB /= (double) n;
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            ab += (a[i] - meanA) * (b[i] - meanB);
            aa += (a[i] - meanA) * (a[i] - meanA);
            bb += (b[i] - meanB) * (b[i] - meanB);
        }
        return ab / std::sqrt (aa * bb + 1.0e-30);
    }

    using ModType = effects::Modulation::Type;
    float typeIndex (ModType type) { return (float) (int) type; }
}

TEST_CASE ("The newer modulation types", "[effects][mod]")
{
    const auto input = sine (440.0, 0.3f, 1.0);

    SECTION ("Every type changes the sound and stays stable")
    {
        for (const auto type : { ModType::dimension, ModType::uniVibe, ModType::rotary, ModType::vibrato,
                                 ModType::harmonicTremolo, ModType::autoPan })
        {
            EffectRig rig;
            rig.set (ParamIDs::modOn, 1.0f);
            rig.set (ParamIDs::modType, typeIndex (type));
            rig.set (ParamIDs::modDepth, 0.8f);
            rig.set (ParamIDs::modMix, 0.5f);
            const auto output = rig.run (input);
            INFO (effects::Modulation::typeNames[(int) type]);
            REQUIRE (allFinite (output[0]));
            REQUIRE (allFinite (output[1]));
            CHECK (maxAbsDifference (input, output[0], (size_t) (0.2 * sampleRate)) > 0.01f);
            CHECK (rms (output[0], 0.2) < 2.0f * rms (input, 0.2));   // nothing runs away
        }
    }

    SECTION ("Dimension chorus and rotary are stereo")
    {
        for (const auto type : { ModType::dimension, ModType::rotary })
        {
            EffectRig rig;
            rig.set (ParamIDs::modOn, 1.0f);
            rig.set (ParamIDs::modType, typeIndex (type));
            rig.set (ParamIDs::modMix, 1.0f);
            const auto output = rig.run (input);
            std::vector<float> difference (output[0].size());
            for (size_t i = 0; i < difference.size(); ++i)
                difference[i] = output[0][i] - output[1][i];
            INFO (effects::Modulation::typeNames[(int) type]);
            CHECK (rms (difference, 0.2) > 0.05f * rms (output[0], 0.2));
        }
    }

    SECTION ("Uni-Vibe's wet signal only shifts phase, so it keeps the level")
    {
        EffectRig rig;
        rig.set (ParamIDs::modOn, 1.0f);
        rig.set (ParamIDs::modType, typeIndex (ModType::uniVibe));
        rig.set (ParamIDs::modMix, 1.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        CHECK_THAT (rms (rig.runMono (input), 0.2) / rms (input, 0.2), WithinAbs (1.0, 0.05));
    }

    SECTION ("Auto-pan moves the sound from one speaker to the other")
    {
        EffectRig rig;
        rig.set (ParamIDs::modOn, 1.0f);
        rig.set (ParamIDs::modType, typeIndex (ModType::autoPan));
        rig.set (ParamIDs::modRate, 2.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        const auto output = rig.run (input);
        CHECK (correlation (envelope (output[0]), envelope (output[1])) < -0.9);
    }

    SECTION ("Harmonic tremolo pulses the lows and highs in turn")
    {
        const auto pulse = [] (double hz)
        {
            EffectRig rig;
            rig.set (ParamIDs::modOn, 1.0f);
            rig.set (ParamIDs::modType, typeIndex (ModType::harmonicTremolo));
            rig.set (ParamIDs::modRate, 3.0f);
            rig.set (ParamIDs::modDepth, 1.0f);
            return envelope (rig.runMono (sine (hz, 0.3f, 1.0)));
        };
        CHECK (correlation (pulse (150.0), pulse (5000.0)) < -0.8);
    }
}

TEST_CASE ("The doubler adds a take on each side", "[effects][mod]")
{
    EffectRig rig;
    rig.set (ParamIDs::modOn, 1.0f);
    rig.set (ParamIDs::modType, typeIndex (ModType::doubler));
    rig.set (ParamIDs::modMix, 0.7f);
    const auto input = sine (440.0, 0.3f, 1.0);
    const auto output = rig.run (input);
    REQUIRE (allFinite (output[0]));
    REQUIRE (allFinite (output[1]));

    std::vector<float> difference (output[0].size());
    for (size_t i = 0; i < difference.size(); ++i)
        difference[i] = output[0][i] - output[1][i];
    CHECK (rms (difference, 0.2) > 0.1f * rms (output[0], 0.2));   // the two sides differ
    CHECK (rms (output[0], 0.2) < 2.0f * rms (input, 0.2));
}

TEST_CASE ("LO-FI: telephone, radio, megaphone, cassette and bitcrusher", "[effects][lofi]")
{
    using LoFiType = effects::LoFi::Type;
    const auto makeRig = [] (LoFiType type, float amount = 5.0f)
    {
        auto rig = std::make_unique<EffectRig>();
        rig->set (ParamIDs::lofiOn, 1.0f);
        rig->set (ParamIDs::lofiType, (float) type);
        rig->set (ParamIDs::lofiAmount, amount);
        rig->set (ParamIDs::lofiNoise, 0.0f);   // measure the tone without hiss
        return rig;
    };

    SECTION ("Telephone lets only the voice band through")
    {
        auto rig = makeRig (LoFiType::telephone);
        const auto gainAt = [&] (double hz)
        {
            const auto input = sine (hz, 0.05f, 0.5);
            return dB (rms (rig->runMono (input), 0.2) / rms (input, 0.2));
        };
        const auto mid = gainAt (1000.0);
        CHECK (mid - gainAt (100.0) > 25.0f);
        CHECK (mid - gainAt (8000.0) > 20.0f);
        CHECK_THAT (mid, WithinAbs (0.0, 6.0));   // and the voice band is about as loud as before
    }

    SECTION ("PR7-K: the speaker types keep each side its own (on PR7-G they're mono)")
    {
        for (const auto type : { LoFiType::telephone, LoFiType::amRadio, LoFiType::megaphone })
        {
            auto rig = makeRig (type);
            const auto output = rig->run (sine (800.0, 0.3f, 0.6), silence (0.6));
            INFO (effects::LoFi::typeNames[(int) type]);
            REQUIRE (allFinite (output[0]));
            CHECK (rms (output[0], 0.1) > 0.05f);
            CHECK (maxAbsDifference (output[1], silence (0.6)) == 0.0f);   // nothing on the right (NOISE is off)
        }
    }

    SECTION ("Megaphone is driven harder than the telephone: more harmonics")
    {
        const auto thirdHarmonic = [&] (LoFiType type)
        {
            auto rig = makeRig (type, 8.0f);
            const auto output = rig->runMono (sine (500.0, 0.3f, 0.5));
            return dB (levelAt (output, 1500.0) / levelAt (output, 500.0));
        };
        CHECK (thirdHarmonic (LoFiType::megaphone) > thirdHarmonic (LoFiType::telephone) + 6.0f);
    }

    SECTION ("Cassette wobbles the pitch")
    {
        // A steady 1 kHz tone comes out smeared across neighbouring frequencies.
        auto rig = makeRig (LoFiType::cassette, 10.0f);
        const auto output = rig->runMono (sine (1000.0, 0.3f, 2.0));
        REQUIRE (allFinite (output));
        auto clean = makeRig (LoFiType::cassette, 0.0f);
        const auto steady = clean->runMono (sine (1000.0, 0.3f, 2.0));
        CHECK (levelAt (output, 1000.0, 0.3) < 0.7f * levelAt (steady, 1000.0, 0.3));
    }

    SECTION ("Bitcrusher at full AMOUNT leaves only a few levels and holds samples")
    {
        auto rig = makeRig (LoFiType::bitcrusher, 10.0f);
        const auto output = rig->runMono (sine (220.0, 0.8f, 1.0));
        std::set<int> levels;
        int repeats = 0;
        for (size_t i = 24000; i < output.size(); ++i)
        {
            levels.insert (juce::roundToInt (output[i] * 10000.0f));
            repeats += juce::approximatelyEqual (output[i], output[i - 1]) ? 1 : 0;
        }
        CHECK (levels.size() <= 9);   // 3 bits: up to 9 levels from -1 to +1
        CHECK (repeats > (int) (0.8 * (double) (output.size() - 24000)));   // each sample held for many
    }

    SECTION ("NOISE adds hiss")
    {
        auto rig = makeRig (LoFiType::telephone);
        rig->set (ParamIDs::lofiNoise, 10.0f);
        const auto output = rig->runMono (silence (0.5));
        REQUIRE (allFinite (output));
        CHECK (dB (rms (output, 0.1)) > -60.0f);
        CHECK (dB (rms (output, 0.1)) < -25.0f);
    }
}

TEST_CASE ("Modulation syncs to the tempo", "[effects][mod]")
{
    // Tremolo synced to quarter notes at the DAW's 120 BPM is two pulses a
    // second, whatever RATE says: the same as RATE at 2 Hz unsynced.
    const auto input = sine (440.0, 0.3f, 1.0);
    const auto tremolo = [&] (float rate, float sync)
    {
        EffectRig rig;
        rig.set (ParamIDs::modOn, 1.0f);
        rig.set (ParamIDs::modType, typeIndex (ModType::tremolo));
        rig.set (ParamIDs::modDepth, 1.0f);
        rig.set (ParamIDs::modRate, rate);
        rig.set (ParamIDs::modSync, sync);
        return rig.runMono (input);
    };

    const auto synced = tremolo (7.0f, 3.0f);   // sync 3 = "1/4"
    const auto free = tremolo (2.0f, 0.0f);
    CHECK (maxAbsDifference (synced, free, 0) < 0.01f);
    CHECK (maxAbsDifference (tremolo (7.0f, 0.0f), free, (size_t) (0.2 * sampleRate)) > 0.05f);   // and RATE alone does matter
}

TEST_CASE ("AEON: the flanger follows the BF-2's circuit and specifications", "[effects][modulation]")
{
    using effects::Aeon;
    const auto makeRig = [] (float manual, float depth, float res, float rateHz = 0.5f)
    {
        auto rig = std::make_unique<EffectRig>();
        rig->set (ParamIDs::modOn, 1.0f);
        rig->set (ParamIDs::modType, (float) effects::Modulation::Type::flanger);
        rig->set (ParamIDs::modManual, manual);
        rig->set (ParamIDs::modDepth, depth);
        rig->set (ParamIDs::modRes, res);
        rig->set (ParamIDs::modRate, rateHz);
        return rig;
    };

    // With DEPTH at 0 the delay holds still: the response to a small click
    // (after a second, so everything has settled), as gain in dB per band.
    struct Band { double lowest, highest; };
    const auto impulseResponse = [&] (float manual, float res)
    {
        auto rig = makeRig (manual, 0.0f, res);
        rig->runMono (silence (1.0));
        return rig->runMono (impulse (1.0, 0.01f));
    };
    const auto spectrum = [] (const std::vector<float>& ir)
    {
        constexpr int order = 17;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) (2 << order), 0.0f);
        for (size_t i = 0; i < std::min (ir.size(), (size_t) (1 << order)); ++i)
            data[i] = ir[i] / 0.01f;
        fft.performFrequencyOnlyForwardTransform (data.data());
        return data;   // bin i is i * sampleRate / 2^17 Hz
    };
    const auto bandExtremes = [] (const std::vector<float>& magnitudes, Band band)
    {
        const auto binHz = sampleRate / (double) (1 << 17);
        float highest = -1000.0f, lowest = 1000.0f;
        for (auto i = (size_t) (band.lowest / binHz); i < (size_t) (band.highest / binHz); ++i)
        {
            highest = std::max (highest, dB (magnitudes[i] + 1.0e-9f));
            lowest = std::min (lowest, dB (magnitudes[i] + 1.0e-9f));
        }
        return std::make_pair (lowest, highest);
    };

    SECTION ("MANUAL sets the delay across the pedal's specified 13 ms to 1 ms (DEPTH at 0)")
    {
        CHECK_THAT (Aeon::delayMsForControl (Aeon::manualVolts (0.0f), 0.0), WithinAbs (13.0, 0.01));
        CHECK_THAT (Aeon::delayMsForControl (Aeon::manualVolts (10.0f), 0.0), WithinAbs (1.0, 0.01));

        // The echo of a click lands there, plus about 0.15 ms through the circuit's filters.
        const auto echoMs = [&] (float manual)
        {
            const auto ir = impulseResponse (manual, 0.0f);
            size_t peak = 20;
            for (size_t i = 20; i < ir.size(); ++i)
                if (std::abs (ir[i]) > std::abs (ir[peak]))
                    peak = i;
            return 1000.0 * (double) peak / sampleRate;
        };
        CHECK_THAT (echoMs (0.0f), WithinAbs (13.15, 0.2));
        CHECK_THAT (echoMs (5.0f), WithinAbs (3.6, 0.2));
        CHECK_THAT (echoMs (10.0f), WithinAbs (1.15, 0.2));
    }

    SECTION ("Dry and wet mix in phase: deep notches between the peaks")
    {
        const auto magnitudes = spectrum (impulseResponse (5.0f, 0.0f));
        const auto [notch, peak] = bandExtremes (magnitudes, { 2000.0, 6000.0 });
        CHECK (notch < -30.0f);
        CHECK_THAT (peak, WithinAbs (6.0, 1.5));   // dry and wet adding up: about twice the level
    }

    SECTION ("The low end keeps more of its level: the wet path lets little through below about 40 Hz")
    {
        const auto magnitudes = spectrum (impulseResponse (5.0f, 0.0f));
        CHECK (bandExtremes (magnitudes, { 40.0, 150.0 }).first > -10.0f);
    }

    SECTION ("RES rings the peaks, most in the treble, and never howls")
    {
        const auto still = spectrum (impulseResponse (5.0f, 0.0f));
        const auto ringing = spectrum (impulseResponse (5.0f, 10.0f));
        CHECK (bandExtremes (ringing, { 600.0, 2000.0 }).second > bandExtremes (still, { 600.0, 2000.0 }).second + 3.0f);
        CHECK (bandExtremes (ringing, { 6000.0, 12000.0 }).second > bandExtremes (still, { 6000.0, 12000.0 }).second + 15.0f);

        // A click at full RES dies away rather than building up...
        const auto ir = impulseResponse (5.0f, 10.0f);
        CHECK (rms (ir, 0.9, 1.0) < 1.0e-4f * rms (ir, 0.0, 0.1));
        // ...and loud playing stays loud playing: IC2b's diodes hold the BBD's input down.
        auto rig = makeRig (10.0f, 1.0f, 10.0f, 3.0f);
        auto loud = sine (110.0, 1.0f, 3.0);
        for (size_t i = 0; i < loud.size(); ++i)
            loud[i] += 0.8f * (float) std::sin (0.37 * (double) i);
        const auto output = rig->runMono (loud);
        REQUIRE (allFinite (output));
        CHECK (*std::max_element (output.begin(), output.end()) < 3.0f);
    }

    SECTION ("Full DEPTH sweeps by the LFO alone: MANUAL does nothing, as the manual says")
    {
        const auto input = sine (700.0, 0.05f, 1.5);
        const auto atZero = makeRig (0.0f, 1.0f, 3.0f)->runMono (input);
        const auto atTen = makeRig (10.0f, 1.0f, 3.0f)->runMono (input);
        CHECK (maxAbsDifference (atZero, atTen) < 1.0e-5f);
        CHECK_THAT (Aeon::delayMsForControl (Aeon::lfoVolts (-1.0), 1.0), WithinAbs (9.1, 0.3));
        CHECK_THAT (Aeon::delayMsForControl (Aeon::lfoVolts (1.0), 1.0), WithinAbs (1.2, 0.1));
    }

    // A steady 1 kHz tone through a full-depth sweep: its loudness rises
    // and falls as the notches pass. Its envelope, in 10 ms steps.
    const auto envelope = [&] (float rateHz, double seconds)
    {
        auto rig = makeRig (5.0f, 1.0f, 0.0f, rateHz);
        const auto output = rig->runMono (sine (1000.0, 0.05f, seconds));
        std::vector<float> levels;
        for (double t = 1.0; t + 0.01 < seconds; t += 0.01)
            levels.push_back (rms (output, t, t + 0.01));
        return levels;
    };

    SECTION ("RATE: one sweep per cycle")
    {
        const auto levels = envelope (1.0f, 6.0);
        float mean = 0.0f;
        for (auto l : levels)
            mean += l / (float) levels.size();
        auto bestLag = 0;
        double best = -1.0e9;
        for (int lag = 70; lag <= 130; ++lag)   // 0.7 to 1.3 s
        {
            double sum = 0.0;
            for (size_t i = 0; i + (size_t) lag < levels.size(); ++i)
                sum += (levels[i] - mean) * (levels[i + (size_t) lag] - mean);
            if (sum > best)
            {
                best = sum;
                bestLag = lag;
            }
        }
        CHECK (std::abs (bestLag - 100) <= 3);
    }

    SECTION ("Fast sweeps are narrower: C32 (22 uF) smooths the sweep")
    {
        const auto swing = [] (const std::vector<float>& levels)
        {
            const auto [lowest, highest] = std::minmax_element (levels.begin(), levels.end());
            return dB (*highest / *lowest);
        };
        CHECK (swing (envelope (0.3f, 5.0)) > swing (envelope (8.0f, 5.0)) + 2.0f);
    }
}



