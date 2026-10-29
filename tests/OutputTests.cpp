// THE OUTPUTS (PR7-D 0.3.0): MAIN and 15 more stereo outputs. Each drum, insert bus and send return plays into the
// one it's set to, while the DAW has it on (else into MAIN); OUT 2 to 16 skip MAIN's chain but stay in time with it;
// BYPASS silences them all; presets and kits leave the routing alone.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int numOutputs = 16;

    using testkit::set;

    // A processor with MAIN (stereo or mono) and the outputs in `on` switched on (as the DAW would).
    std::unique_ptr<ParagonX1AudioProcessor> makeProcessor (std::initializer_list<int> on = {}, bool monoMain = false)
    {
        auto processor = std::make_unique<ParagonX1AudioProcessor>();
        auto layout = processor->getBusesLayout();
        REQUIRE (layout.outputBuses.size() == numOutputs);
        if (monoMain)
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        for (const auto o : on)
            layout.outputBuses.getReference (o - 1) = juce::AudioChannelSet::stereo();   // `o`: 2 is OUT 2
        REQUIRE (processor->setBusesLayout (layout));
        processor->prepareToPlay (sampleRate, blockSize);
        return processor;
    }

    // Every channel the plugin plays, `seconds` of it, with `midi` (positions from the start).
    std::vector<std::vector<float>> play (ParagonX1AudioProcessor& processor, double seconds, const juce::MidiBuffer& midi = {})
    {
        const auto channels = processor.getTotalNumOutputChannels();
        std::vector<std::vector<float>> out ((size_t) channels);
        juce::AudioBuffer<float> buffer (channels, blockSize);
        for (int start = 0; start < (int) (seconds * sampleRate); start += blockSize)
        {
            juce::MidiBuffer slice;
            slice.addEvents (midi, start, blockSize, -start);
            buffer.clear();
            processor.processBlock (buffer, slice);
            for (int c = 0; c < channels; ++c)
                out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
        }
        return out;
    }

    // Output `o`'s left channel (1 is MAIN) in what play() returned.
    const std::vector<float>& leftOf (const ParagonX1AudioProcessor& processor, const std::vector<std::vector<float>>& out, int o)
    {
        const auto channel = processor.getChannelIndexInProcessBlockBuffer (false, o - 1, 0);
        REQUIRE (channel >= 0);
        return out[(size_t) channel];
    }

    float peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        auto p = 0.0f;
        for (auto i = from; i < std::min (to, x.size()); ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }

    juce::MidiBuffer hits (std::initializer_list<int> notes, int at = 0)
    {
        juce::MidiBuffer midi;
        for (const auto n : notes)
            midi.addEvent (juce::MidiMessage::noteOn (10, n, (juce::uint8) 127), at);
        return midi;
    }

    // The kick and snare slots play a 440 Hz sine and a 1 kHz sine (as recorded), so where each lands can be told.
    void tones (ParagonX1AudioProcessor& p, double seconds = 1.0)
    {
        testkit::useSample (p, 0, testkit::sine (440.0, seconds));
        testkit::useSample (p, 2, testkit::sine (1000.0, seconds));
    }

    // How much of a `hz` sine is in x (the amplitude of its correlation with one).
    double toneIn (const std::vector<float>& x, double hz, size_t from, size_t to)
    {
        double re = 0.0, im = 0.0;
        for (auto i = from; i < to; ++i)
        {
            const auto w = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
            re += x[i] * std::cos (w);
            im += x[i] * std::sin (w);
        }
        return 2.0 * std::sqrt (re * re + im * im) / (double) (to - from);
    }

    int outputOf (ParagonX1AudioProcessor& p, const juce::String& id)
    {
        return juce::roundToInt (p.getParameters().getRawParameterValue (id)->load());
    }
}

TEST_CASE ("OUTPUTS: a drum set to OUT 2 plays there, and only there; the rest stay in MAIN", "[outputs]")
{
    auto p = makeProcessor ({ 2 });
    tones (*p);
    set (*p, ParamIDs::drum (0, "Output"), 1.0f);   // the kick: OUT 2
    const auto out = play (*p, 0.5, hits ({ testkit::keyFor (0), testkit::keyFor (2) }));
    const auto& main = leftOf (*p, out, 1);
    const auto& out2 = leftOf (*p, out, 2);
    const auto from = (size_t) (0.1 * sampleRate), to = (size_t) (0.4 * sampleRate);
    INFO ("MAIN: 440 Hz " << toneIn (main, 440.0, from, to) << ", 1 kHz " << toneIn (main, 1000.0, from, to)
          << "; OUT 2: 440 Hz " << toneIn (out2, 440.0, from, to) << ", 1 kHz " << toneIn (out2, 1000.0, from, to));
    // The kick's sine comes out of OUT 2 as recorded (0.5 peak, centre pan: both sides at full level) less OUTPUT's
    // 6 dB (it's the whole plugin's volume), none in MAIN.
    CHECK_THAT (toneIn (out2, 440.0, from, to), WithinAbs (0.25, 0.005));
    CHECK (toneIn (main, 440.0, from, to) < 1.0e-4);
    // The snare stays in MAIN, at the same level the kick has on OUT 2: moving a drum doesn't change its level.
    CHECK_THAT (toneIn (main, 1000.0, from, to), WithinAbs (0.25, 0.005));
    CHECK (toneIn (out2, 1000.0, from, to) < 1.0e-4);
    // Both sides of OUT 2.
    const auto right = (size_t) p->getChannelIndexInProcessBlockBuffer (false, 1, 1);
    CHECK_THAT (toneIn (out[right], 440.0, from, to), WithinAbs (0.25, 0.005));
}

TEST_CASE ("OUTPUTS: a drum set to an output the DAW hasn't switched on plays in MAIN, exactly as if set to MAIN", "[outputs]")
{
    auto onMain = makeProcessor();
    auto offOutput = makeProcessor();
    for (auto* p : { onMain.get(), offOutput.get() })
    {
        tones (*p);
        p->seedRandomForTest (1);
    }
    set (*offOutput, ParamIDs::drum (0, "Output"), 5.0f);   // OUT 6: off
    const auto a = play (*onMain, 0.3, hits ({ testkit::keyFor (0) }));
    const auto b = play (*offOutput, 0.3, hits ({ testkit::keyFor (0) }));
    REQUIRE (a.size() == 2);
    REQUIRE (b.size() == 2);
    CHECK (peak (a[0]) > 0.2f);
    CHECK (a == b);
}

TEST_CASE ("OUTPUTS: a drum on an insert bus goes where the bus goes; send returns have their own", "[outputs]")
{
    SECTION ("the bus's OUTPUT, not the drum's")
    {
        auto p = makeProcessor ({ 2, 3 });
        tones (*p);
        set (*p, ParamIDs::drum (0, "Insert"), 1.0f);    // the kick on bus A
        set (*p, ParamIDs::drum (0, "Output"), 1.0f);    // its own: OUT 2 (not used while on a bus)
        set (*p, ParamIDs::ins (0, "Output"), 2.0f);     // bus A: OUT 3
        const auto out = play (*p, 0.5, hits ({ testkit::keyFor (0) }));
        const auto from = (size_t) (0.1 * sampleRate), to = (size_t) (0.4 * sampleRate);
        CHECK_THAT (toneIn (leftOf (*p, out, 3), 440.0, from, to), WithinAbs (0.25, 0.005));
        CHECK (toneIn (leftOf (*p, out, 2), 440.0, from, to) < 1.0e-4);
        CHECK (toneIn (leftOf (*p, out, 1), 440.0, from, to) < 1.0e-4);
    }
    SECTION ("REVERB's return on OUT 4: MAIN keeps the dry hit, OUT 4 has the room")
    {
        auto p = makeProcessor ({ 4 });
        testkit::useSample (*p, 0, testkit::sine (440.0, 0.05));   // a 50 ms hit
        set (*p, ParamIDs::reverbOn, 1.0f);
        set (*p, ParamIDs::reverbMix, 1.0f);
        set (*p, ParamIDs::reverbOutput, 3.0f);
        const auto out = play (*p, 1.0, hits ({ testkit::keyFor (0) }));
        const auto& main = leftOf (*p, out, 1);
        const auto& out4 = leftOf (*p, out, 4);
        const auto afterHit = (size_t) (0.15 * sampleRate);
        INFO ("MAIN after the hit " << peak (main, afterHit) << ", OUT 4 " << peak (out4, afterHit));
        CHECK (peak (main, 0, afterHit) > 0.2f);   // the hit
        CHECK (peak (main, afterHit) < 1.0e-4f);   // and no tail
        CHECK (peak (out4, afterHit) > 1.0e-3f);   // the tail is on OUT 4
        CHECK (peak (out4, 0, (size_t) (0.04 * sampleRate)) < 0.2f);   // with no dry hit in it
    }
}

TEST_CASE ("OUTPUTS: OUT 2 to 16 stay in time with MAIN when its chain adds latency (CONSTRICTOR, the LIMITER)", "[outputs]")
{
    auto p = makeProcessor ({ 2 });
    testkit::useSample (*p, 0, testkit::sine (440.0, 0.2, 0.1f));
    testkit::useSample (*p, 2, testkit::sine (440.0, 0.2, 0.1f));   // the same hit on the snare's slot
    set (*p, ParamIDs::drum (2, "Output"), 1.0f);                   // the snare's: OUT 2
    set (*p, ParamIDs::mbLookahead, 1.0f);
    set (*p, ParamIDs::limitOn, 1.0f);
    set (*p, ParamIDs::limitTruePeak, 1.0f);
    set (*p, ParamIDs::limitOversampling, 2.0f);
    p->prepareToPlay (sampleRate, blockSize);
    const auto latency = p->mainLatencySamples();
    INFO ("MAIN's latency: " << latency << " samples");
    REQUIRE (latency > 960);   // CONSTRICTOR's 20 ms look-ahead and the LIMITER's
    CHECK (p->getLatencySamples() == latency);   // what the DAW is told
    const auto out = play (*p, 0.5, hits ({ testkit::keyFor (0), testkit::keyFor (2) }, 100));
    const auto& main = leftOf (*p, out, 1);
    const auto& out2 = leftOf (*p, out, 2);
    // Where each hit starts, and where it lines up best with the other (to the sample).
    const auto onset = [] (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > 0.01f)
                return (int) i;
        return -1;
    };
    INFO ("onsets: MAIN " << onset (main) << ", OUT 2 " << onset (out2));
    CHECK (onset (main) >= 100 + latency - 2);   // played late by its chain...
    // ...and OUT 2 with it: the LIMITER's oversampling filters round the start of MAIN's wave, so where it first crosses
    // the threshold can move a sample or two; lined up whole (below), the two are exact.
    CHECK (std::abs (onset (main) - onset (out2)) <= 2);
    auto bestLag = 999;
    auto bestSum = -1.0;
    for (int lag = -64; lag <= 64; ++lag)
    {
        auto sum = 0.0;
        for (int i = 200 + latency; i < 200 + latency + 4800; ++i)
            sum += (double) main[(size_t) i] * out2[(size_t) (i + lag)];
        if (sum > bestSum)
        {
            bestSum = sum;
            bestLag = lag;
        }
    }
    CHECK (bestLag == 0);
}

TEST_CASE ("OUTPUTS: OUTPUT turns every output up and down together", "[outputs]")
{
    auto p = makeProcessor ({ 2 });
    tones (*p);
    set (*p, ParamIDs::drum (0, "Output"), 1.0f);
    set (*p, ParamIDs::outputGain, -18.0f);
    p->prepareToPlay (sampleRate, blockSize);   // straight to the new level (no glide)
    const auto out = play (*p, 0.5, hits ({ testkit::keyFor (0), testkit::keyFor (2) }));
    const auto from = (size_t) (0.1 * sampleRate), to = (size_t) (0.4 * sampleRate);
    const auto expected = 0.5 * juce::Decibels::decibelsToGain (-18.0);
    CHECK_THAT (toneIn (leftOf (*p, out, 2), 440.0, from, to), WithinAbs (expected, 0.002));
    CHECK_THAT (toneIn (leftOf (*p, out, 1), 1000.0, from, to), WithinAbs (expected, 0.002));
}

TEST_CASE ("OUTPUTS: BYPASS silences OUT 2 to 16 too", "[outputs]")
{
    auto p = makeProcessor ({ 2 });
    tones (*p, 2.0);
    set (*p, ParamIDs::drum (0, "Output"), 1.0f);
    set (*p, ParamIDs::bypass, 1.0f);
    const auto out = play (*p, 0.5, hits ({ testkit::keyFor (0) }));
    CHECK (peak (leftOf (*p, out, 2), (size_t) (0.1 * sampleRate)) == 0.0f);
}

TEST_CASE ("OUTPUTS: with MAIN mono, OUT 2 is still stereo and gets its drum", "[outputs]")
{
    auto p = makeProcessor ({ 2 }, true);
    REQUIRE (p->getTotalNumOutputChannels() == 3);
    tones (*p);
    set (*p, ParamIDs::drum (0, "Output"), 1.0f);
    const auto out = play (*p, 0.5, hits ({ testkit::keyFor (0), testkit::keyFor (2) }));
    const auto from = (size_t) (0.1 * sampleRate), to = (size_t) (0.4 * sampleRate);
    CHECK_THAT (toneIn (out[1], 440.0, from, to), WithinAbs (0.25, 0.005));
    CHECK_THAT (toneIn (out[2], 440.0, from, to), WithinAbs (0.25, 0.005));
    CHECK (toneIn (out[0], 440.0, from, to) < 1.0e-4);
    CHECK_THAT (toneIn (out[0], 1000.0, from, to), WithinAbs (0.25, 0.005));   // the snare in MAIN
}

TEST_CASE ("OUTPUTS: presets and kits leave the routing alone", "[outputs][rigs]")
{
    auto p = makeProcessor ({ 2, 3 });
    set (*p, ParamIDs::drum (0, "Output"), 1.0f);
    set (*p, ParamIDs::ins (1, "Output"), 2.0f);
    set (*p, ParamIDs::reverbOutput, 2.0f);
    p->loadRig (*p->getRigLibrary().findFactory ("Room Kit"));
    CHECK (outputOf (*p, ParamIDs::drum (0, "Output")) == 1);
    CHECK (outputOf (*p, ParamIDs::ins (1, "Output")) == 2);
    CHECK (outputOf (*p, ParamIDs::reverbOutput) == 2);
    CHECK (! p->isRigModified());

    const auto rig = p->captureRig();
    CHECK (rig.parameters.find (ParamIDs::drum (0, "Output")) == rig.parameters.end());

    const auto kit = p->captureDrumKit();
    for (const auto* e : kit->getChildWithTagNameIterator ("Param"))
        CHECK_FALSE (e->getStringAttribute ("id").endsWith ("Output"));
    set (*p, ParamIDs::drum (0, "Output"), 4.0f);
    REQUIRE (p->applyDrumKit (*kit));
    CHECK (outputOf (*p, ParamIDs::drum (0, "Output")) == 4);
}

TEST_CASE ("OUTPUTS: the project keeps the routing", "[outputs]")
{
    juce::MemoryBlock state;
    {
        auto p = makeProcessor();
        set (*p, ParamIDs::drum (5, "Output"), 7.0f);
        set (*p, ParamIDs::delayOutput, 15.0f);
        p->getStateInformation (state);
    }
    auto restored = makeProcessor();
    restored->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (outputOf (*restored, ParamIDs::drum (5, "Output")) == 7);
    CHECK (outputOf (*restored, ParamIDs::delayOutput) == 15);
}
