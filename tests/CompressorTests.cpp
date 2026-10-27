// PR7-E's COMP (its 0.9.0, PR7-C's compressor ported), kept in PR7-K for the
// effects rack (a later stage). These are PR7-E's tests of the compressor
// itself, now driving it directly: PR7-K has no COMP parameters yet, so a
// small stand-in (CompUnit) takes the old parameter IDs and sets the
// compressor's settings from them, as PR7-E's processor did. PR7-E's tests of
// how it sat in that plugin's chain, and of its knob's markings, went with it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/effects/Compressor.h"

#include <optional>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // PR7-E's parameter IDs for COMP, as the tests below set them.
    namespace ids
    {
        constexpr auto compOn = "compOn", compThreshold = "compThreshold", compRatio = "compRatio", compAttack = "compAttack",
                       compRelease = "compRelease", compAuto = "compAuto", compMakeUp = "compMakeUp",
                       compCharacter = "compCharacter", compLink = "compLink";
    }

    // The compressor as PR7-E ran it: in pieces of the block size, listening to the audio itself.
    struct CompUnit
    {
        CompUnit()
        {
            compressor.prepare (sampleRate, blockSize);
            detector.setSize (2, blockSize);
        }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
        {
            for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
            {
                const auto n = juce::jmin (blockSize, buffer.getNumSamples() - start);
                juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                    detector.copyFrom (channel, 0, audio, channel, 0, n);
                const juce::AudioBuffer<float> listen (detector.getArrayOfWritePointers(), audio.getNumChannels(), n);
                compressor.process (audio, listen, n, settings);
            }
        }

        float getCompressorReductionDb (int channel) const { return compressor.getReductionDb (channel); }

        effects::Compressor compressor;
        effects::Compressor::Settings settings;
        juce::AudioBuffer<float> detector;
    };

    void set (CompUnit& unit, const juce::String& id, float value)
    {
        auto& s = unit.settings;
        if (id == ids::compOn) s.on = value > 0.5f;
        else if (id == ids::compThreshold) s.thresholdDb = value;
        else if (id == ids::compRatio) s.ratio = value;
        else if (id == ids::compAttack) s.attackMs = value;
        else if (id == ids::compRelease) s.releaseMs = value;
        else if (id == ids::compAuto) s.autoRelease = value > 0.5f;
        else if (id == ids::compMakeUp) s.makeUpDb = value;
        else if (id == ids::compCharacter) s.character = (effects::Compressor::Character) juce::roundToInt (value);
        else if (id == ids::compLink) s.link = value > 0.5f;
        else FAIL ("not a COMP setting: " << id);
    }

    // PR7-E's COMP defaults (VCA, 10 ms attack, 300 ms release, linked), on.
    std::unique_ptr<CompUnit> makeCompressor (float thresholdDb, float ratio)
    {
        auto unit = std::make_unique<CompUnit>();
        set (*unit, ids::compOn, 1.0f);
        set (*unit, ids::compThreshold, thresholdDb);
        set (*unit, ids::compRatio, ratio);
        return unit;
    }

    // A sine whose peak is at `levelDb` dBFS; `rightLevelDb` for the right
    // channel if given.
    juce::AudioBuffer<float> sine (float hz, float levelDb, int numSamples, std::optional<float> rightLevelDb = {})
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto level = juce::Decibels::decibelsToGain (channel == 1 && rightLevelDb ? *rightLevelDb : levelDb);
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (channel, i, level * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) sampleRate));
        }
        return buffer;
    }

    void process (CompUnit& processor, juce::AudioBuffer<float>& buffer, const std::function<void (int)>& atBlock = {})
    {
        juce::MidiBuffer midi;
        for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
        {
            if (atBlock)
                atBlock (start);
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start,
                                            juce::jmin (blockSize, buffer.getNumSamples() - start));
            processor.processBlock (block, midi);
        }
    }

    // The steady change in level (dB) a tone gets, over the second half of a second.
    float settledChangeDb (CompUnit& processor, float levelDb, int channel = 0, std::optional<float> rightLevelDb = {})
    {
        const auto input = sine (1000.0f, levelDb, 48000, rightLevelDb);
        auto output = input;
        process (processor, output);
        return juce::Decibels::gainToDecibels (output.getRMSLevel (channel, 24000, 24000) / input.getRMSLevel (channel, 24000, 24000));
    }

    // The level of one frequency in a signal (Goertzel), as a fraction of full scale.
    float levelAt (const juce::AudioBuffer<float>& buffer, float hz, int from, int length)
    {
        const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < from + length; ++i)
        {
            const auto s0 = buffer.getSample (0, i) + 2.0 * std::cos (w) * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (float) (2.0 * std::sqrt (s1 * s1 + s2 * s2 - 2.0 * std::cos (w) * s1 * s2) / length);
    }

    float sharpestCorner (const juce::AudioBuffer<float>& buffer, int from, int to)
    {
        float sharpest = 0.0f;
        const auto* x = buffer.getReadPointer (0);
        for (int i = juce::jmax (2, from); i < to; ++i)
            sharpest = juce::jmax (sharpest, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
        return sharpest;
    }
}

TEST_CASE ("Compressor: below the threshold, nothing changes", "[comp]")
{
    auto comp = makeCompressor (-20.0f, 4.0f);
    CHECK (std::abs (settledChangeDb (*comp, -30.0f)) < 0.05f);
}

TEST_CASE ("Compressor: a hard ratio turns the overshoot down by the ratio", "[comp]")
{
    // 20 dB over a -30 dB threshold at 4:1 comes out 5 dB over: 15 dB down.
    auto comp = makeCompressor (-30.0f, 4.0f);
    CHECK_THAT (settledChangeDb (*comp, -10.0f), WithinAbs (-15.0, 0.6));

    // The meters see it.
    CHECK_THAT (comp->getCompressorReductionDb (0), WithinAbs (15.0, 1.0));
}

TEST_CASE ("Compressor: at infinite ratio the level stops at the threshold", "[comp]")
{
    auto comp = makeCompressor (-30.0f, 30.0f);
    CHECK_THAT (settledChangeDb (*comp, -10.0f), WithinAbs (-20.0, 0.8));
}

TEST_CASE ("Compressor: the SOFT ratio grows with level", "[comp]")
{
    // The curve: 3:1 at 8 dB over, 6:1 at 20 dB over.
    using effects::Compressor;
    CHECK_THAT (8.0f / (8.0f + Compressor::gainComputerDb (-22.0f, -30.0f, Compressor::softRatio, 0.0f)), WithinAbs (3.0, 0.01));
    CHECK_THAT (20.0f / (20.0f + Compressor::gainComputerDb (-10.0f, -30.0f, Compressor::softRatio, 0.0f)), WithinAbs (6.0, 0.01));

    // And in the sound: the louder it is, the harder it squeezes.
    auto comp = makeCompressor (-30.0f, Compressor::softRatio);
    const auto at8 = -settledChangeDb (*comp, -22.0f);
    const auto at20 = -settledChangeDb (*comp, -10.0f);
    CHECK_THAT (at8, WithinAbs (8.0 - 8.0 / 3.0, 0.6));
    CHECK_THAT (at20, WithinAbs (20.0 - 20.0 / 6.0, 0.8));
}

TEST_CASE ("Compressor: MAKE UP brings the level back up", "[comp]")
{
    auto comp = makeCompressor (-30.0f, 4.0f);
    set (*comp, ids::compMakeUp, 10.0f);
    CHECK_THAT (settledChangeDb (*comp, -10.0f), WithinAbs (-5.0, 0.6));
}

TEST_CASE ("Compressor: ATTACK sets how quickly it clamps down", "[comp]")
{
    // A loud tone after silence: how far down is it 3 ms in?
    const auto downAfter3ms = [] (float attackMs)
    {
        // FET: its peak detector reacts at once, so the attack is all ATTACK's.
        auto comp = makeCompressor (-30.0f, 10.0f);
        set (*comp, ids::compAttack, attackMs);
        set (*comp, ids::compCharacter, 2.0f);
        auto buffer = sine (1000.0f, -10.0f, 9600);
        for (int channel = 0; channel < 2; ++channel)
            buffer.clear (channel, 0, 2400);   // silence first, then the tone at 50 ms
        const auto input = buffer;
        process (*comp, buffer);
        // Around 3 ms in (144 samples), a cycle of the 1 kHz tone either side.
        return juce::Decibels::gainToDecibels (buffer.getRMSLevel (0, 2400 + 120, 48) / input.getRMSLevel (0, 2400 + 120, 48));
    };
    CHECK (downAfter3ms (0.5f) < -10.0f);   // clamped already
    CHECK (downAfter3ms (30.0f) > -4.0f);   // still letting it through
}

TEST_CASE ("Compressor: AUTO lets go quickly after a short peak, slowly after long compression", "[comp]")
{
    // A loud burst then a quiet tone: how long until the quiet tone is
    // within 1 dB of its own level again?
    const auto recoveryMs = [] (int burstSamples)
    {
        auto comp = makeCompressor (-30.0f, 10.0f);
        set (*comp, ids::compAuto, 1.0f);
        set (*comp, ids::compRelease, 300.0f);
        const auto total = burstSamples + 96000;
        auto buffer = sine (1000.0f, -40.0f, total);
        for (int channel = 0; channel < 2; ++channel)
            buffer.applyGain (channel, 0, burstSamples, juce::Decibels::decibelsToGain (30.0f));   // the burst at -10
        process (*comp, buffer);
        for (int start = burstSamples; start + 480 < total; start += 480)
            if (buffer.getRMSLevel (0, start, 480) > juce::Decibels::decibelsToGain (-41.0f) * 0.7071f)
                return (float) (start - burstSamples) / 48.0f;
        return 2000.0f;
    };
    const auto afterShort = recoveryMs (2400);     // 50 ms
    const auto afterLong = recoveryMs (144000);    // 3 s
    CHECK (afterShort < afterLong * 0.5f);
}

TEST_CASE ("Compressor: OPTO's soft knee starts working below the threshold, VCA's hardly", "[comp]")
{
    const auto atThreshold = [] (int character)
    {
        auto comp = makeCompressor (-20.0f, 4.0f);
        set (*comp, ids::compCharacter, (float) character);
        return -settledChangeDb (*comp, -20.0f);
    };
    CHECK (atThreshold (0) < 0.4f);   // VCA
    CHECK (atThreshold (1) > 0.8f);   // OPTO
}

TEST_CASE ("Compressor: FET adds harmonics when it works hard; VCA stays clean", "[comp]")
{
    const auto thirdHarmonicDb = [] (int character)
    {
        auto comp = makeCompressor (-40.0f, 10.0f);
        set (*comp, ids::compCharacter, (float) character);
        auto buffer = sine (500.0f, -3.0f, 48000);
        process (*comp, buffer);
        return juce::Decibels::gainToDecibels (levelAt (buffer, 1500.0f, 24000, 24000) / levelAt (buffer, 500.0f, 24000, 24000));
    };
    CHECK (thirdHarmonicDb (2) > -60.0f);   // FET: audible grit
    CHECK (thirdHarmonicDb (2) < -20.0f);   // but not fuzz
    CHECK (thirdHarmonicDb (0) < thirdHarmonicDb (2) - 20.0f);
}

TEST_CASE ("Compressor: FET's grit doesn't fold back down (aliasing)", "[comp]")
{
    // 9 kHz, squeezed hard: its 3rd harmonic is at 27 kHz, beyond what
    // 48 kHz can hold. Unfiltered it would fold back to 21 kHz.
    auto comp = makeCompressor (-40.0f, 10.0f);
    set (*comp, ids::compCharacter, 2.0f);   // FET
    auto buffer = sine (9000.0f, -3.0f, 48000);
    process (*comp, buffer);
    const auto fundamental = levelAt (buffer, 9000.0f, 24000, 24000);
    CHECK (juce::Decibels::gainToDecibels (levelAt (buffer, 21000.0f, 24000, 24000) / fundamental) < -80.0f);
}

TEST_CASE ("Compressor: LINK turns both sides down together", "[comp]")
{
    // A loud left, a quiet right (below the threshold on its own).
    SECTION ("linked")
    {
        auto comp = makeCompressor (-30.0f, 4.0f);
        CHECK (settledChangeDb (*comp, -10.0f, 1, -40.0f) < -12.0f);
    }
    SECTION ("unlinked")
    {
        auto comp = makeCompressor (-30.0f, 4.0f);
        set (*comp, ids::compLink, 0.0f);
        CHECK (std::abs (settledChangeDb (*comp, -10.0f, 1, -40.0f)) < 0.1f);
    }
}

TEST_CASE ("Compressor: switching it on, or its character, doesn't click", "[comp]")
{
    const auto check = [] (const std::function<void (CompUnit&)>& before, const std::function<void (CompUnit&)>& change)
    {
        auto comp = makeCompressor (-30.0f, 4.0f);
        before (*comp);
        auto buffer = sine (200.0f, -10.0f, 48000);
        process (*comp, buffer, [&] (int start) { if (start == 24064) change (*comp); });
        const auto steady = juce::jmax (sharpestCorner (buffer, 12000, 24000), sharpestCorner (buffer, 40000, 48000));
        CHECK (sharpestCorner (buffer, 24000, 40000) < steady * 3.0f);
    };
    SECTION ("on")
    {
        check ([] (auto& c) { set (c, ids::compOn, 0.0f); }, [] (auto& c) { set (c, ids::compOn, 1.0f); });
    }
    SECTION ("VCA to FET")
    {
        check ([] (auto&) {}, [] (auto& c) { set (c, ids::compCharacter, 2.0f); });
    }
}

TEST_CASE ("Compressor: a block bigger than promised is handled in pieces", "[comp]")
{
    auto comp = makeCompressor (-30.0f, 4.0f);
    auto buffer = sine (1000.0f, -10.0f, 4096);   // 16 times the prepared block size
    juce::MidiBuffer midi;
    comp->processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 2048, 2048) < juce::Decibels::decibelsToGain (-15.0f));
}
