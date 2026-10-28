// PR7-G 0.52.0's DELAY and REVERB tests (stage 7b), unchanged but for the rig: PR7-K has no audio input, so
// `EffectRig` stands in for PR7-G's plugin. It keeps the settings in a PR7-K processor's parameters (the same IDs,
// ranges and defaults, read by the same getDelaySettings / getReverbSettings the plugin uses) and runs the input,
// copied to both sides as PR7-G's stereo stages get it, through the DELAY then the REVERB as PR7-G's chain does
// (as inserts: the input plus their echoes). The sends themselves are tested in SendTests.cpp.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Reverb.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct PlayHead { double bpm = 120.0; };

    struct EffectRig
    {
        EffectRig()
        {
            delay.prepare (sampleRate, blockSize);
            reverb.prepare (sampleRate, blockSize);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        std::vector<std::vector<float>> run (const std::vector<float>& input)
        {
            std::vector<std::vector<float>> output (2);
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (size_t start = 0; start < input.size(); start += blockSize)
            {
                const auto count = (int) std::min<size_t> (blockSize, input.size() - start);
                buffer.setSize (2, count, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    std::copy (input.begin() + (long) start, input.begin() + (long) start + count, buffer.getWritePointer (ch));
                juce::dsp::AudioBlock<float> block (buffer);
                auto delaySettings = processor.getDelaySettings();
                auto reverbSettings = processor.getReverbSettings();
                delaySettings.returnOnly = reverbSettings.returnOnly = false;   // as PR7-G's inserts
                delay.process (block, delaySettings, playHead.bpm);
                reverb.process (block, reverbSettings);
                for (int ch = 0; ch < 2; ++ch)
                    output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + count);
            }
            return output;
        }

        std::vector<float> runMono (const std::vector<float>& input) { return run (input)[0]; }

        PlayHead playHead;
        ParagonX1AudioProcessor processor;
        effects::Delay delay;
        effects::Reverb reverb;
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

TEST_CASE ("Delay repeats at the set time", "[effects][delay]")
{
    EffectRig rig;
    rig.set (ParamIDs::delayOn, 1.0f);
    rig.set (ParamIDs::delayMix, 1.0f);
    rig.set (ParamIDs::delayFeedback, 0.5f);

    const auto peakNear = [] (const std::vector<float>& samples, size_t at, size_t window = 64)
    {
        float peak = 0.0f;
        for (auto i = at - window; i <= at + window; ++i)
            peak = std::max (peak, std::abs (samples[i]));
        return peak;
    };

    SECTION ("Free time in milliseconds")
    {
        rig.set (ParamIDs::delaySync, 0.0f);   // Off
        rig.set (ParamIDs::delayTime, 100.0f);
        const auto output = rig.runMono (impulse (0.5, 0.5f));

        // The first echo is an exact copy 100 ms (4800 samples) later.
        CHECK_THAT (output[4800], WithinAbs (0.5, 0.01));
        CHECK (peakNear (output, 2400) < 0.001f);
        // The second has half the energy (feedback 50%). The tone filter in
        // the loop softens it (darker repeats), which spreads it over a few
        // samples, so add those up rather than looking at the peak.
        float secondEcho = 0.0f;
        for (size_t i = 9600 - 8; i < 9600 + 64; ++i)
            secondEcho += output[i];
        CHECK_THAT (secondEcho, WithinAbs (0.25, 0.01));
    }

    SECTION ("Synced to the DAW tempo: 1/4 note at 120 BPM is 500 ms")
    {
        rig.set (ParamIDs::delaySync, 3.0f);   // 1/4
        rig.playHead.bpm = 120.0;
        const auto output = rig.runMono (impulse (0.7, 0.5f));
        CHECK_THAT (output[24000], WithinAbs (0.5, 0.01));

        // Dotted eighth at 90 BPM: 0.75 beats * 60/90 s = 500 ms as well.
        EffectRig other;
        other.set (ParamIDs::delayOn, 1.0f);
        other.set (ParamIDs::delayMix, 1.0f);
        other.set (ParamIDs::delaySync, 7.0f);   // 1/8 dotted
        other.playHead.bpm = 90.0;
        CHECK_THAT (other.runMono (impulse (0.7, 0.5f))[24000], WithinAbs (0.5, 0.01));
    }

    SECTION ("Ping-pong alternates the repeats between left and right")
    {
        rig.set (ParamIDs::delaySync, 0.0f);
        rig.set (ParamIDs::delayTime, 100.0f);
        rig.set (ParamIDs::delayPingPong, 1.0f);
        const auto output = rig.run (impulse (0.5, 0.5f));
        CHECK (peakNear (output[0], 4800) > 0.1f);    // 1st echo: left
        CHECK (peakNear (output[1], 4800) < 0.001f);
        CHECK (peakNear (output[1], 9600) > 0.05f);   // 2nd echo: right
        CHECK (peakNear (output[0], 9600) < 0.001f);
    }
}

TEST_CASE ("Switching delay off lets the echoes ring out but stops new ones", "[effects][delay]")
{
    EffectRig rig;
    rig.set (ParamIDs::delayOn, 1.0f);
    rig.set (ParamIDs::delayMix, 1.0f);
    rig.set (ParamIDs::delayFeedback, 0.0f);
    rig.set (ParamIDs::delaySync, 0.0f);
    rig.set (ParamIDs::delayTime, 200.0f);

    // Play a click, then switch the delay off before its echo arrives.
    auto first = rig.runMono (impulse (0.1, 0.5f));
    rig.set (ParamIDs::delayOn, 0.0f);
    auto rest = rig.runMono (silence (0.2));
    first.insert (first.end(), rest.begin(), rest.end());
    CHECK (std::abs (first[9600]) > 0.4f);   // the echo still came, 200 ms later

    // A click played now, with the delay off, gets no echo.
    const auto later = rig.runMono (impulse (0.5, 0.5f));
    CHECK (std::abs (later[9600]) < 1.0e-4f);
}

TEST_CASE ("Delay types each do what they say", "[effects][delay]")
{
    using DelayType = effects::Delay::Type;
    const auto makeRig = [] (DelayType type, float timeMs, float feedback)
    {
        auto rig = std::make_unique<EffectRig>();
        rig->set (ParamIDs::delayOn, 1.0f);
        rig->set (ParamIDs::delayType, (float) type);
        rig->set (ParamIDs::delaySync, 0.0f);
        rig->set (ParamIDs::delayTime, timeMs);
        rig->set (ParamIDs::delayFeedback, feedback);
        rig->set (ParamIDs::delayMix, 1.0f);
        return rig;
    };
    const auto minus = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> difference (a.size());
        for (size_t i = 0; i < a.size(); ++i)
            difference[i] = a[i] - b[i];
        return difference;
    };

    SECTION ("Every type stays stable with FEEDBACK all the way up, and its echoes stop when switched off")
    {
        for (int type = 0; type < effects::Delay::typeNames.size(); ++type)
        {
            INFO (effects::Delay::typeNames[type]);
            auto rig = makeRig ((DelayType) type, 300.0f, 0.95f);
            auto burst = sine (220.0, 0.5f, 0.25);   // a played phrase, then letting it ring
            burst.resize ((size_t) (4.0 * sampleRate), 0.0f);
            const auto output = rig->run (burst);
            REQUIRE (allFinite (output[0]));
            REQUIRE (allFinite (output[1]));
            float peak = 0.0f;
            for (auto sample : output[0])
                peak = std::max (peak, std::abs (sample));
            CHECK (peak < 2.0f);   // it can build, but it never runs away
        }
    }

    SECTION ("Analog's repeats are darker than digital's")
    {
        juce::Random random (3);
        std::vector<float> noise ((size_t) (0.05 * sampleRate));
        for (auto& sample : noise)
            sample = random.nextFloat() * 0.6f - 0.3f;
        noise.resize ((size_t) (0.6 * sampleRate), 0.0f);
        const auto highsInEcho = [&] (DelayType type)
        {
            auto output = makeRig (type, 250.0f, 0.0f)->runMono (noise);
            juce::dsp::IIR::Filter<float> highPass;
            highPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 5000.0f);
            for (auto& sample : output)
                sample = highPass.processSample (sample);
            return rms (output, 0.25, 0.32);
        };
        CHECK (highsInEcho (DelayType::analog) < 0.3f * highsInEcho (DelayType::digital));
    }

    SECTION ("Tape's wow and flutter smear a steady note's pitch")
    {
        const auto input = sine (1000.0, 0.3f, 2.0);
        const auto echoAt1k = [&] (DelayType type)
        {
            auto rig = makeRig (type, 300.0f, 0.0f);
            const auto output = rig->runMono (input);
            return levelAt (minus (output, input), 1000.0, 0.4);
        };
        CHECK (echoAt1k (DelayType::tape) < 0.8f * echoAt1k (DelayType::digital));
    }

    SECTION ("Mod's chorused repeats differ left and right")
    {
        const auto input = sine (440.0, 0.3f, 1.5);
        const auto output = makeRig (DelayType::mod, 300.0f, 0.3f)->run (input);
        CHECK (rms (minus (output[0], output[1]), 0.4) > 0.1f * rms (minus (output[0], input), 0.4));
    }

    SECTION ("Multi-tap repeats at a quarter, half, three quarters and the full time")
    {
        const auto output = makeRig (DelayType::multiTap, 400.0f, 0.0f)->runMono (impulse (0.6, 0.5f));
        for (const auto ms : { 100, 200, 300, 400 })
        {
            INFO (ms << " ms");
            float peak = 0.0f;
            for (int i = -8; i <= 8; ++i)
                peak = std::max (peak, std::abs (output[(size_t) (ms * 48 + i)]));
            CHECK (peak > 0.1f);
        }
        CHECK (rms (output, 0.12, 0.18) < 1.0e-4f);   // and nothing in between
    }

    SECTION ("Reverse plays each repeat backwards: a note's echo swells up instead of dying away")
    {
        // A plucked note: sharp attack, then a fade.
        std::vector<float> note ((size_t) (1.5 * sampleRate));
        for (size_t i = 0; i < (size_t) (0.4 * sampleRate); ++i)
            note[i] = 0.4f * (float) (std::sin (juce::MathConstants<double>::twoPi * 330.0 * (double) i / sampleRate)
                                      * std::exp (-(double) i / (0.08 * sampleRate)));
        const auto reversed = minus (makeRig (DelayType::reverse, 400.0f, 0.0f)->runMono (note), note);
        const auto forwards = minus (makeRig (DelayType::digital, 400.0f, 0.0f)->runMono (note), note);
        // In the second chunk (0.4-0.8 s): digital's echo is loud at the
        // start and fades; reverse's is quiet at the start and loudest late.
        CHECK (rms (forwards, 0.40, 0.48) > 3.0f * rms (forwards, 0.62, 0.7));
        CHECK (rms (reversed, 0.62, 0.78) > 3.0f * rms (reversed, 0.40, 0.48));
    }

    SECTION ("Shimmer's repeats climb an octave")
    {
        const auto input = [&]
        {
            auto burst = sine (330.0, 0.4f, 0.2);
            burst.resize ((size_t) (2.0 * sampleRate), 0.0f);
            return burst;
        }();
        const auto octaveUp = [&] (DelayType type)
        {
            const auto output = makeRig (type, 250.0f, 0.6f)->runMono (input);
            return levelAt (output, 660.0, 0.5) / (levelAt (output, 330.0, 0.5) + 1.0e-9f);
        };
        CHECK (octaveUp (DelayType::shimmer) > 0.3f);
        CHECK (octaveUp (DelayType::digital) < 0.05f);
    }

    SECTION ("Ducking keeps the repeats down while you play and lets them back in when you stop")
    {
        auto input = sine (220.0, 0.3f, 1.0);
        input.resize ((size_t) (2.0 * sampleRate), 0.0f);
        const auto ducked = minus (makeRig (DelayType::ducking, 250.0f, 0.5f)->runMono (input), input);
        const auto plain = minus (makeRig (DelayType::digital, 250.0f, 0.5f)->runMono (input), input);
        CHECK (rms (ducked, 0.5, 0.95) < 0.25f * rms (plain, 0.5, 0.95));    // at least 12 dB down while playing
        CHECK (rms (ducked, 1.5, 1.8) > 0.5f * rms (plain, 1.5, 1.8));      // and back once you stop
    }
}

TEST_CASE ("Reverb adds a stereo tail that fades out", "[effects][reverb]")
{
    EffectRig rig;
    rig.set (ParamIDs::reverbOn, 1.0f);
    rig.set (ParamIDs::reverbMix, 0.5f);
    const auto output = rig.run (impulse (3.0, 0.5f));

    REQUIRE (allFinite (output[0]));
    const auto early = rms (output[0], 0.05, 0.3);
    const auto late = rms (output[0], 2.5, 3.0);
    CHECK (early > 1.0e-3f);   // there is a tail
    CHECK (late < early * 0.1f);   // and it decays

    std::vector<float> difference (output[0].size());
    for (size_t i = 0; i < difference.size(); ++i)
        difference[i] = output[0][i] - output[1][i];
    CHECK (rms (difference, 0.05, 0.3) > 0.1f * early);   // left and right differ: it's wide
}


TEST_CASE ("Reverb types: each is a stable stereo space of its own", "[effects][reverb]")
{
    using ReverbType = effects::Reverb::Type;
    const auto play = [] (ReverbType type, const std::vector<float>& input, float preDelayMs = 0.0f)
    {
        EffectRig rig;
        rig.set (ParamIDs::reverbOn, 1.0f);
        rig.set (ParamIDs::reverbType, (float) type);
        rig.set (ParamIDs::reverbMix, 1.0f);
        rig.set (ParamIDs::reverbPreDelay, preDelayMs);
        return rig.run (input);
    };

    SECTION ("Every type makes a stereo tail that fades away")
    {
        for (int type = 0; type < effects::Reverb::typeNames.size(); ++type)
        {
            INFO (effects::Reverb::typeNames[type]);
            const auto output = play ((ReverbType) type, impulse (14.0, 0.5f));
            REQUIRE (allFinite (output[0]));
            REQUIRE (allFinite (output[1]));
            CHECK (rms (output[0], 0.05, 1.0) > 1.0e-3f);    // there's a tail
            CHECK (rms (output[0], 13.0, 14.0) < 1.0e-4f);   // and it ends, even the longest cathedral

            std::vector<float> difference (output[0].size());
            for (size_t i = 0; i < difference.size(); ++i)
                difference[i] = output[0][i] - output[1][i];
            CHECK (rms (difference, 0.05, 1.0) > 0.1f * rms (output[0], 0.05, 1.0));   // and it's wide
        }
    }

    SECTION ("Bigger spaces ring longer: room, then hall, then cathedral")
    {
        const auto tailAt = [&] (ReverbType type)
        {
            return rms (play (type, impulse (2.0, 0.5f))[0], 1.4, 1.6);
        };
        const auto room = tailAt (ReverbType::room), hall = tailAt (ReverbType::hall), cathedral = tailAt (ReverbType::cathedral);
        CHECK (hall > 10.0f * room);
        CHECK (cathedral > 3.0f * hall);
    }

    SECTION ("PRE-DELAY: nothing comes back until it's over")
    {
        const auto output = play (ReverbType::hall, impulse (0.5, 0.5f), 100.0f);
        CHECK (rms (output[0], 0.005, 0.095) < 1.0e-5f);   // the click itself is at 0; then silence
        CHECK (rms (output[0], 0.1, 0.3) > 1.0e-3f);       // then the reverb
    }

    SECTION ("Reverse swells up rather than dying away")
    {
        const auto output = play (ReverbType::reverse, impulse (1.5, 0.5f));
        const auto start = rms (output[0], 0.005, 0.15);
        const auto swell = rms (output[0], 0.35, 0.8);
        CHECK (swell > 3.0f * start);
    }

    SECTION ("Spring is band-limited like a real tank: little deep bass or fizz")
    {
        const auto output = play (ReverbType::spring, impulse (2.0, 0.5f));
        // The tail's energy in an octave round a frequency.
        const auto band = [&] (float hz)
        {
            juce::dsp::IIR::Filter<float> filter;
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, hz, 1.4f);
            std::vector<float> filtered (output[0]);
            for (auto& sample : filtered)
                sample = filter.processSample (sample);
            return rms (filtered, 0.02, 1.5);
        };
        const auto mids = band (1200.0f);
        CHECK (mids > 4.0f * band (50.0f));
        CHECK (mids > 4.0f * band (12000.0f));
    }
}

TEST_CASE ("DUCK tucks the delay and the reverb under the playing, on any type", "[effects][delay][reverb]")
{
    // Play for a second, then stop. With DUCK at 100% the wet signal is well
    // down while playing, and comes back once the playing stops.
    auto input = sine (220.0, 0.3f, 1.0);
    input.resize ((size_t) (2.5 * sampleRate), 0.0f);
    const auto wetOf = [&] (const char* onId, const char* duckId, const char* typeId, float type, float duck)
    {
        EffectRig rig;
        rig.set (onId, 1.0f);
        rig.set (typeId, type);
        rig.set (duckId, duck);
        if (std::string (onId) == ParamIDs::delayOn)
        {
            rig.set (ParamIDs::delaySync, 0.0f);
            rig.set (ParamIDs::delayTime, 250.0f);
            rig.set (ParamIDs::delayFeedback, 0.5f);
            rig.set (ParamIDs::delayMix, 1.0f);
        }
        else
        {
            rig.set (ParamIDs::reverbMix, 1.0f);
        }
        auto output = rig.runMono (input);
        for (size_t i = 0; i < output.size(); ++i)
            output[i] -= input[i];
        return output;
    };

    SECTION ("Delay: a ducked tape delay")
    {
        const auto plain = wetOf (ParamIDs::delayOn, ParamIDs::delayDuck, ParamIDs::delayType, (float) effects::Delay::Type::tape, 0.0f);
        const auto ducked = wetOf (ParamIDs::delayOn, ParamIDs::delayDuck, ParamIDs::delayType, (float) effects::Delay::Type::tape, 1.0f);
        CHECK (rms (ducked, 0.5, 0.95) < 0.1f * rms (plain, 0.5, 0.95));   // 20 dB down while playing
        CHECK (rms (ducked, 1.5, 1.8) > 0.5f * rms (plain, 1.5, 1.8));     // back after
    }

    SECTION ("Reverb: a ducked cathedral")
    {
        const auto plain = wetOf (ParamIDs::reverbOn, ParamIDs::reverbDuck, ParamIDs::reverbType, (float) effects::Reverb::Type::cathedral, 0.0f);
        const auto ducked = wetOf (ParamIDs::reverbOn, ParamIDs::reverbDuck, ParamIDs::reverbType, (float) effects::Reverb::Type::cathedral, 1.0f);
        CHECK (rms (ducked, 0.5, 0.95) < 0.1f * rms (plain, 0.5, 0.95));
        CHECK (rms (ducked, 1.5, 1.8) > 0.5f * rms (plain, 1.5, 1.8));
    }

    SECTION ("DUCK at 0% changes nothing")
    {
        EffectRig a, b;
        for (auto* rig : { &a, &b })
        {
            rig->set (ParamIDs::reverbOn, 1.0f);
            rig->set (ParamIDs::delayOn, 1.0f);
        }
        b.set (ParamIDs::reverbDuck, 0.0f);
        b.set (ParamIDs::delayDuck, 0.0f);
        CHECK (maxAbsDifference (a.runMono (input), b.runMono (input)) < 1.0e-6f);
    }
}

