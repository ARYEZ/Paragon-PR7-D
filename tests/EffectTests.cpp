#include <catch2/catch_test_macros.hpp>
#include <set>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Circuit.h"
#include "ui/EqPresets.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // Reports a fixed tempo, like a DAW with its transport set to `bpm`.
    struct FakePlayHead final : juce::AudioPlayHead
    {
        double bpm = 120.0;
        bool playing = false;              // a transport that's playing reports the song position
        double startBeats = 0.0;           // where the song was when the run began, in beats
        juce::int64 samplesPlayed = 0;     // moved on by EffectRig::run

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (playing);
            if (playing)
                info.setPpqPosition (startBeats + (double) samplesPlayed / sampleRate * bpm / 60.0);
            return info;
        }
    };

    struct EffectRig
    {
        explicit EffectRig (int outputChannels = 2, int inputChannels = 1) : channels (outputChannels)
        {
            processor.setPlayConfigDetails (inputChannels, channels, sampleRate, blockSize);
            processor.setPlayHead (&playHead);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        // Runs `input` through the plugin in blocks; returns every output channel.
        std::vector<std::vector<float>> run (const std::vector<float>& input)
        {
            std::vector<std::vector<float>> output ((size_t) channels);
            juce::AudioBuffer<float> buffer (channels, blockSize);
            juce::MidiBuffer midi;

            for (size_t start = 0; start < input.size(); start += blockSize)
            {
                const auto count = (int) std::min<size_t> (blockSize, input.size() - start);
                buffer.setSize (channels, count, false, false, true);
                buffer.clear();
                std::copy (input.begin() + (long) start, input.begin() + (long) start + count, buffer.getWritePointer (0));
                processor.processBlock (buffer, midi);
                playHead.samplesPlayed += count;
                for (int ch = 0; ch < channels; ++ch)
                    output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + count);
            }
            return output;
        }

        std::vector<float> runMono (const std::vector<float>& input) { return run (input)[0]; }

        int channels;
        FakePlayHead playHead;
        ParagonX1AudioProcessor processor;
    };

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

TEST_CASE ("With every effect off, both channels are the clean input", "[effects]")
{
    EffectRig rig;
    const auto input = sine (440.0, 0.3f, 0.5);
    const auto output = rig.run (input);
    CHECK (maxAbsDifference (input, output[0]) < 1.0e-6f);   // a mono track: on both speakers
    CHECK (maxAbsDifference (input, output[1]) < 1.0e-6f);
}

TEST_CASE ("Stereo stays stereo: left and right keep their own sound, on and bypassed", "[effects][stereo]")
{
    // A stereo recording: a low note on the left, a high one on the right.
    // Before 0.3.0 PR7-E played the left channel on both sides.
    const auto left = sine (110.0, 0.3f, 0.5), right = sine (1760.0, 0.2f, 0.5);
    const auto play = [&] (EffectRig& rig)
    {
        std::vector<std::vector<float>> output (2);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (size_t start = 0; start + blockSize <= left.size(); start += blockSize)
        {
            std::copy (left.begin() + (long) start, left.begin() + (long) start + blockSize, buffer.getWritePointer (0));
            std::copy (right.begin() + (long) start, right.begin() + (long) start + blockSize, buffer.getWritePointer (1));
            rig.processor.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + blockSize);
        }
        return output;
    };

    SECTION ("Everything off: untouched, sample for sample")
    {
        EffectRig rig (2, 2);
        const auto output = play (rig);
        CHECK (maxAbsDifference (left, output[0]) < 1.0e-6f);
        CHECK (maxAbsDifference (right, output[1]) < 1.0e-6f);
    }

    SECTION ("The EQ and PRESENCE work on each side without mixing them")
    {
        EffectRig rig (2, 2);
        rig.set (ParamIDs::eqOn, 1.0f);
        rig.set (ParamIDs::presence, 6.0f);
        const auto output = play (rig);
        CHECK (levelAt (output[0], 110.0) > 0.25f);
        CHECK (levelAt (output[0], 1760.0) < 0.001f);   // nothing of the right channel on the left
        CHECK (levelAt (output[1], 1760.0) > 0.15f);
        CHECK (levelAt (output[1], 110.0) < 0.001f);
    }

    SECTION ("BYPASS passes both channels as they came in")
    {
        EffectRig rig (2, 2);
        rig.set (ParamIDs::presence, 10.0f);
        rig.set (ParamIDs::bypass, 1.0f);
        const auto output = play (rig);
        CHECK (maxAbsDifference (left, output[0], (size_t) (0.05 * sampleRate)) < 1.0e-6f);
        CHECK (maxAbsDifference (right, output[1], (size_t) (0.05 * sampleRate)) < 1.0e-6f);
    }
}

TEST_CASE ("Turning the EQ on and off doesn't click, and off really is off", "[effects]")
{
    EffectRig rig;
    rig.set (ParamIDs::eqBands[2].toRawUTF8(), 8.0f);   // so ON makes a real difference
    const auto input = sine (220.0, 0.3f, 0.3);
    rig.set (ParamIDs::eqOn, 1.0f);
    rig.runMono (input);

    // Switching off mid-note: no sample may jump further than the biggest
    // step the EQ makes on its own.
    const auto on = rig.runMono (input);
    rig.set (ParamIDs::eqOn, 0.0f);
    const auto switching = rig.runMono (sine (220.0, 0.3f, 0.05));
    float normalStep = 0.0f, switchStep = 0.0f;
    for (size_t i = 1; i < on.size(); ++i)
        normalStep = std::max (normalStep, std::abs (on[i] - on[i - 1]));
    for (size_t i = 1; i < switching.size(); ++i)
        switchStep = std::max (switchStep, std::abs (switching[i] - switching[i - 1]));
    CHECK (switchStep < std::max (normalStep, 0.03f) * 1.5f);

    rig.runMono (silence (0.5));
    const auto after = rig.runMono (input);
    CHECK (maxAbsDifference (input, after) < 1.0e-4f);
}

TEST_CASE ("The chain copes with a mono track and with oversized blocks", "[effects]")
{
    const auto turnEverythingOn = [] (EffectRig& rig)
    {
        rig.set (ParamIDs::eqOn, 1.0f);
        rig.set (ParamIDs::presence, 5.0f);
    };

    SECTION ("Mono output")
    {
        EffectRig rig (1);
        turnEverythingOn (rig);
        const auto output = rig.runMono (sine (220.0, 0.3f, 0.5));
        REQUIRE (allFinite (output));
        CHECK (rms (output, 0.2) > 0.01f);
    }

    SECTION ("A host sending 3x the promised block size")
    {
        EffectRig rig;
        turnEverythingOn (rig);
        juce::AudioBuffer<float> big (2, blockSize * 3);
        juce::MidiBuffer midi;
        for (int repeat = 0; repeat < 20; ++repeat)
        {
            big.clear();
            const auto input = sine (220.0, 0.3f, (double) big.getNumSamples() / sampleRate);
            std::copy (input.begin(), input.end(), big.getWritePointer (0));
            rig.processor.processBlock (big, midi);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < big.getNumSamples(); ++i)
                REQUIRE (std::isfinite (big.getSample (ch, i)));
    }
}

TEST_CASE ("Full chain runs comfortably faster than real time", "[effects][performance]")
{
    EffectRig rig (2, 2);
    rig.set (ParamIDs::eqOn, 1.0f);
    rig.set (ParamIDs::presence, 5.0f);
    rig.set (ParamIDs::tunerOn, 1.0f);

    const auto input = sine (220.0, 0.3f, 5.0);
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    rig.runMono (input);
    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    const auto realtimeFraction = elapsedSeconds / 5.0;

    WARN ("Full chain uses " << juce::String (realtimeFraction * 100.0, 1) << "% of one core in real time");
    CHECK (realtimeFraction < 0.5);
}

TEST_CASE ("The circuit solver behaves like the circuit: an RC low-pass", "[effects][circuit]")
{
    // 1k and 1 uF: the corner is at 159 Hz, falling 6 dB per octave.
    effects::Circuit c;
    const auto in = c.node(), out = c.node();
    c.resistor (in, out, 1.0e3);
    c.capacitor (out, 0, 1.0e-6);
    c.setInput (in);
    c.setOutput (out);
    c.prepare (sampleRate);

    const auto gainAt = [&c] (double hz)
    {
        c.reset();
        std::vector<float> output;
        for (int i = 0; i < (int) sampleRate; ++i)
            output.push_back ((float) c.process (std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate)));
        return dB (levelAt (output, hz, 0.5));
    };
    CHECK_THAT (gainAt (159.15), WithinAbs (-3.01, 0.1));
    CHECK_THAT (gainAt (1591.5), WithinAbs (-20.04, 0.2));
    CHECK_THAT (gainAt (20.0), WithinAbs (0.0, 0.1));
}

TEST_CASE ("Presence boosts the top end and leaves the lows alone", "[effects][presence]")
{
    EffectRig rig;
    rig.set (ParamIDs::presence, 10.0f);
    const auto highs = sine (9000.0, 0.1f, 0.5);
    const auto lows = sine (150.0, 0.1f, 0.5);
    CHECK (dB (rms (rig.runMono (highs), 0.2) / rms (highs, 0.2)) > 8.0f);
    CHECK_THAT (dB (rms (rig.runMono (lows), 0.2) / rms (lows, 0.2)), WithinAbs (0.0, 0.5));
}

TEST_CASE ("Graphic EQ: flat is transparent, each band moves only its own frequencies", "[effects][eq]")
{
    // The peak level of a steady tone after the plugin, with the EQ set by `setUp`.
    auto levelOf = [] (float frequency, const std::function<void (EffectRig&)>& setUp)
    {
        EffectRig rig;
        setUp (rig);
        std::vector<float> tone ((size_t) (sampleRate * 0.5));
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
        const auto out = rig.run (tone)[0];
        return *std::max_element (out.end() - 4800, out.end());
    };
    const auto off = [] (EffectRig&) {};
    const auto toDb = [] (float ratio) { return juce::Decibels::gainToDecibels (ratio); };

    for (auto frequency : { 33.0f, 300.0f, 900.0f, 5000.0f })
    {
        // Switched on but flat: no change at all.
        const auto flat = levelOf (frequency, [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); });
        CHECK_THAT (toDb (flat / levelOf (frequency, off)), WithinAbs (0.0, 0.05));
    }

    // +12 dB at 900 Hz: a 900 Hz tone comes up by about 12 dB; far-off bands barely
    // move (a band's skirt spills a little, as on a hardware graphic EQ: under
    // 0.5 dB at 33 Hz and 9 kHz).
    const auto boost900 = [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); rig.set ("eq900", 12.0f); };
    CHECK_THAT (toDb (levelOf (900.0f, boost900) / levelOf (900.0f, off)), WithinAbs (12.0, 0.5));
    CHECK (std::abs (toDb (levelOf (33.0f, boost900) / levelOf (33.0f, off))) < 0.5f);
    CHECK (std::abs (toDb (levelOf (9000.0f, boost900) / levelOf (9000.0f, off))) < 0.5f);

    // Cuts work the same way, down at the bass's low end too, and LEVEL moves everything.
    const auto cut150 = [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); rig.set ("eq150", -12.0f); };
    CHECK_THAT (toDb (levelOf (150.0f, cut150) / levelOf (150.0f, off)), WithinAbs (-12.0, 0.5));
    const auto boost33 = [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); rig.set ("eq33", 12.0f); };
    CHECK_THAT (toDb (levelOf (33.0f, boost33) / levelOf (33.0f, off)), WithinAbs (12.0, 0.5));
    const auto louder = [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); rig.set (ParamIDs::eqLevel, 6.0f); };
    CHECK_THAT (toDb (levelOf (500.0f, louder) / levelOf (500.0f, off)), WithinAbs (6.0, 0.1));

    // Off means off, whatever the faders say.
    const auto bypassed = [] (EffectRig& rig) { rig.set ("eq900", 12.0f); };
    CHECK_THAT (toDb (levelOf (900.0f, bypassed) / levelOf (900.0f, off)), WithinAbs (0.0, 0.01));

    // ULTRA LO: a little more deep low end, a deep scoop at 500 Hz, the top
    // untouched. It works with the EQ's faders switched off, as on the amp.
    const auto ultraLo = [] (EffectRig& rig) { rig.set (ParamIDs::eqUltraLo, 1.0f); };
    CHECK_THAT (toDb (levelOf (40.0f, ultraLo) / levelOf (40.0f, off)), WithinAbs (2.0, 0.5));
    CHECK_THAT (toDb (levelOf (500.0f, ultraLo) / levelOf (500.0f, off)), WithinAbs (-10.0, 0.5));
    CHECK (std::abs (toDb (levelOf (5000.0f, ultraLo) / levelOf (5000.0f, off))) < 0.3f);

    // ULTRA HI: +9 dB at 8 kHz, leaving the lows and low mids alone.
    const auto ultraHi = [] (EffectRig& rig) { rig.set (ParamIDs::eqUltraHi, 1.0f); };
    CHECK_THAT (toDb (levelOf (8000.0f, ultraHi) / levelOf (8000.0f, off)), WithinAbs (9.0, 0.5));
    CHECK (std::abs (toDb (levelOf (100.0f, ultraHi) / levelOf (100.0f, off))) < 0.1f);
    CHECK (std::abs (toDb (levelOf (500.0f, ultraHi) / levelOf (500.0f, off))) < 0.3f);

    // Both together, with the faders: the effects add up.
    const auto everything = [] (EffectRig& rig)
    {
        rig.set (ParamIDs::eqOn, 1.0f); rig.set ("eq500", 6.0f);
        rig.set (ParamIDs::eqUltraLo, 1.0f); rig.set (ParamIDs::eqUltraHi, 1.0f);
    };
    std::array<float, effects::GraphicEq::numBands> gains {};
    gains[4] = 6.0f;
    CHECK_THAT (toDb (levelOf (500.0f, everything) / levelOf (500.0f, off)),
                WithinAbs (effects::GraphicEq::responseDb (gains, 500.0f, sampleRate, true, true), 0.5));
}

TEST_CASE ("Graphic EQ's drawn curve matches what it does", "[effects][eq]")
{
    std::array<float, effects::GraphicEq::numBands> gains {};
    CHECK_THAT (effects::GraphicEq::responseDb (gains, 1000.0f), WithinAbs (0.0, 1.0e-6));
    gains[5] = 12.0f;   // 900 Hz
    CHECK_THAT (effects::GraphicEq::responseDb (gains, 900.0f), WithinAbs (12.0, 0.05));
    CHECK (std::abs (effects::GraphicEq::responseDb (gains, 33.0f)) < 0.5f);

    // The tone switches are drawn too: ULTRA LO's scoop and ULTRA HI's lift.
    const std::array<float, effects::GraphicEq::numBands> flat {};
    CHECK_THAT (effects::GraphicEq::responseDb (flat, 500.0f, 48000.0, true, false), WithinAbs (-10.0, 0.1));
    CHECK_THAT (effects::GraphicEq::responseDb (flat, 8000.0f, 48000.0, false, true), WithinAbs (9.0, 0.1));
    // A band above half a low sample rate is pulled down rather than breaking.
    CHECK (effects::GraphicEq::bandFrequency (8, 22050.0) < 11025.0f);
}

TEST_CASE ("EQ presets: in range, named once each, and loudness-matched by LEVEL", "[effects][eq]")
{
    std::set<std::string> names;
    for (const auto& preset : EqPresets::all)
    {
        INFO (preset.name);
        CHECK (names.insert (preset.name).second);
        for (const auto gain : preset.gainsDb)
            CHECK (std::abs (gain) <= effects::GraphicEq::maxGainDb);
        CHECK (std::abs (preset.levelDb) <= 12.0f);

        // The curve's average power change over a bass's range (40 Hz to
        // 5 kHz, sampled every 1/24 octave), plus LEVEL, is
        // close to nothing: switching presets changes the tone, not the volume.
        auto power = 0.0;
        auto count = 0;
        for (auto frequency = 40.0f; frequency <= 5000.0f; frequency *= std::pow (2.0f, 1.0f / 24.0f), ++count)
            power += std::pow (10.0, effects::GraphicEq::responseDb (preset.gainsDb, frequency, 48000.0, preset.ultraLo, preset.ultraHi) / 10.0);
        const auto changeDb = 10.0 * std::log10 (power / count) + preset.levelDb;
        CHECK (std::abs (changeDb) <= 0.5);
    }
}

TEST_CASE ("EQ presets sound as drawn: the plugin's output follows each preset's curve", "[effects][eq]")
{
    // A steady tone's level through the plugin with `preset` loaded (or the
    // EQ off), at the bass's punch, its low-mid mud, and its clank.
    const auto levelOf = [] (float frequency, const EqPresets::Preset* preset)
    {
        EffectRig rig;
        if (preset != nullptr)
        {
            rig.set (ParamIDs::eqOn, 1.0f);
            for (int band = 0; band < effects::GraphicEq::numBands; ++band)
                rig.set (ParamIDs::eqBands[band].toRawUTF8(), preset->gainsDb[(size_t) band]);
            rig.set (ParamIDs::eqLevel, preset->levelDb);
            rig.set (ParamIDs::eqUltraLo, preset->ultraLo ? 1.0f : 0.0f);
            rig.set (ParamIDs::eqUltraHi, preset->ultraHi ? 1.0f : 0.0f);
        }
        std::vector<float> tone ((size_t) (sampleRate * 0.4));
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
        const auto out = rig.run (tone)[0];
        return *std::max_element (out.end() - 4800, out.end());
    };

    for (const auto frequency : { 80.0f, 300.0f, 2000.0f })
    {
        const auto dry = levelOf (frequency, nullptr);
        for (const auto& preset : EqPresets::all)
        {
            INFO (preset.name << " at " << frequency << " Hz");
            const auto expected = effects::GraphicEq::responseDb (preset.gainsDb, frequency, sampleRate, preset.ultraLo, preset.ultraHi)
                                + preset.levelDb;
            CHECK_THAT (juce::Decibels::gainToDecibels (levelOf (frequency, &preset) / dry), WithinAbs (expected, 0.5));
        }
    }

    // And the metal presets do what they say: the clank (2 kHz) is lifted
    // over the low-mid mud (300 Hz) by a good margin.
    for (const auto* name : { "Modern Metal Clank", "Djent Growl", "Grind Through the Guitars" })
    {
        INFO (name);
        const auto& preset = *std::find_if (EqPresets::all.begin(), EqPresets::all.end(),
                                            [name] (const auto& p) { return juce::String (p.name) == name; });
        const auto clank = levelOf (2000.0f, &preset) / levelOf (2000.0f, nullptr);
        const auto mud = levelOf (300.0f, &preset) / levelOf (300.0f, nullptr);
        CHECK (juce::Decibels::gainToDecibels (clank / mud) > 6.0f);
    }
}

