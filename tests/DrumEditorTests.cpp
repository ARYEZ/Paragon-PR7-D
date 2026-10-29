// THE SAMPLE EDITOR (stage 6c) for every slot of the kit: the drawn envelope, GAIN, MONO / POLY, DELAY and LP / HP,
// and what each does to the sound.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/synth/Drums.h"

using namespace synth;
using namespace synth::drums;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<Sample> tone (double hz, double seconds, float level = 1.0f)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        const auto n = (int) (seconds * rate);
        s->channels = { std::vector<float> ((size_t) n) };
        for (int i = 0; i < n; ++i)
            s->channels[0][(size_t) i] = hz <= 0.0 ? level : level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate);
        return s;
    }

    SlotSettings sampleSlot (const Sample& s)
    {
        SlotSettings slot;
        slot.model = Model::sample;
        slot.decay = slot.tone = slot.snap = 0.5f;
        slot.sample = &s;
        return slot;
    }

    mod::Shape shape (std::initializer_list<mod::ShapePoint> points)
    {
        mod::Shape result;
        for (const auto& p : points)
            result.points[(size_t) result.count++] = p;
        return result;
    }

    float peak (const juce::AudioBuffer<float>& b, double from, double to, int channel = 0)
    {
        auto p = 0.0f;
        for (int i = (int) (from * rate); i < juce::jmin (b.getNumSamples(), (int) (to * rate)); ++i)
            p = juce::jmax (p, std::abs (b.getSample (channel, i)));
        return p;
    }

    int lastSound (const juce::AudioBuffer<float>& b)
    {
        for (int i = b.getNumSamples(); --i >= 0;)
            if (b.getSample (0, i) != 0.0f)
                return i;
        return -1;
    }

    // A kit with every slot set as given, played into plain buffers.
    struct Rig
    {
        Kit kit;
        KitSettings settings;
        juce::AudioBuffer<float> out;

        explicit Rig (const SlotSettings& slot)
        {
            kit.prepare (rate, 256);
            settings.on = true;
            settings.slots.fill (slot);
        }
        // Plays `seconds` with keys (slot, at seconds), velocity 1.
        void play (double seconds, std::vector<std::pair<int, double>> hits)
        {
            out.setSize (2, (int) (seconds * rate));
            out.clear();
            for (int done = 0; done < out.getNumSamples(); done += 256)
            {
                const auto count = juce::jmin (256, out.getNumSamples() - done);
                juce::MidiBuffer midi;
                for (const auto& [slot, at] : hits)
                    if (const auto position = (int) (at * rate) - done; position >= 0 && position < count)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 36 + slot, 1.0f), position);
                Outputs o;
                o.left = out.getWritePointer (0, done);
                o.right = out.getWritePointer (1, done);
                kit.process (o, count, midi, 0, settings);
            }
        }
    };
}

TEST_CASE ("SAMPLE EDITOR: the envelope shapes a sample across START to END, whatever its pitch", "[drumeditor]")
{
    const auto sine = tone (1000.0, 0.5);
    auto slot = sampleSlot (*sine);
    // Full, then straight down to nothing halfway, nothing after.
    const auto fall = shape ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
    slot.envelope = &fall;
    auto hit = Kit::renderHit (slot, 1.0f, rate, 0.6);
    CHECK (peak (hit, 0.0, 0.002) > 0.95f);
    CHECK_THAT (peak (hit, 0.124, 0.126), WithinAbs (0.5, 0.02));   // a quarter of the way: half
    CHECK (lastSound (hit) < (int) (0.252 * rate));                 // over at the halfway point
    // An octave up the sample takes half as long: the envelope goes with it.
    slot.pitch = 12.0f;
    hit = Kit::renderHit (slot, 1.0f, rate, 0.6);
    CHECK_THAT (peak (hit, 0.062, 0.063), WithinAbs (0.5, 0.03));
    CHECK (lastSound (hit) < (int) (0.127 * rate));

    // Flat at the top: exactly the same as none.
    const auto flat = shape ({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
    slot.pitch = 0.0f;
    slot.envelope = &flat;
    const auto a = Kit::renderHit (slot, 1.0f, rate, 0.6);
    slot.envelope = nullptr;
    const auto b = Kit::renderHit (slot, 1.0f, rate, 0.6);
    for (int i = 0; i < a.getNumSamples(); ++i)
        REQUIRE (a.getSample (0, i) == b.getSample (0, i));
}

TEST_CASE ("SAMPLE EDITOR: the envelope on a drum synth spans its hit; a dip and a swell", "[drumeditor]")
{
    SlotSettings cymbal;
    cymbal.model = Model::openHat;
    cymbal.decay = 0.8f;
    cymbal.envelopeSeconds = (float) Kit::hitSeconds (cymbal);
    REQUIRE (cymbal.envelopeSeconds > 1.0f);
    const auto plain = Kit::renderHit (cymbal, 1.0f, rate, 4.0);
    // Silent through its first half, then back at full: what's heard is the tail alone.
    const auto gap = shape ({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.5001f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } });
    cymbal.envelope = &gap;
    const auto shaped = Kit::renderHit (cymbal, 1.0f, rate, 4.0);
    const auto half = 0.5 * cymbal.envelopeSeconds;
    CHECK (peak (shaped, 0.0, half - 0.01) < 1.0e-6f);
    CHECK_THAT (peak (shaped, half + 0.05, half + 0.2), WithinAbs (peak (plain, half + 0.05, half + 0.2), 1.0e-6));

    // A longer DECAY makes a longer hit, which the envelope then spans.
    SlotSettings shorter;
    shorter.model = Model::openHat;
    shorter.decay = 0.3f;
    CHECK (Kit::hitSeconds (shorter) < cymbal.envelopeSeconds * 0.7);
    // SAMPLE's length is exact: START to END at its pitch.
    const auto sine = tone (440.0, 0.5);
    auto slot = sampleSlot (*sine);
    slot.end = 0.5f;
    slot.pitch = 12.0f;
    CHECK_THAT (Kit::hitSeconds (slot), WithinAbs (0.125, 1.0e-6));
}

TEST_CASE ("SAMPLE EDITOR: GAIN, MONO / POLY", "[drumeditor]")
{
    const auto level = tone (0.0, 0.5, 0.25f);   // a steady level, a quarter
    auto slot = sampleSlot (*level);
    slot.trim = 2.0f;
    CHECK_THAT (peak (Kit::renderHit (slot, 1.0f, rate, 0.4), 0.0, 0.4), WithinAbs (0.5, 1.0e-6));

    // POLY: a second hit 0.1 s in overlaps the first (a quarter each, half together); MONO: it takes over.
    slot.trim = 1.0f;
    Rig poly (slot);
    poly.play (0.3, { { 0, 0.0 }, { 0, 0.1 } });
    CHECK_THAT (peak (poly.out, 0.15, 0.2), WithinAbs (0.5, 1.0e-5));
    slot.mono = true;
    Rig mono (slot);
    mono.play (0.3, { { 0, 0.0 }, { 0, 0.1 } });
    CHECK_THAT (peak (mono.out, 0.11, 0.2), WithinAbs (0.25, 1.0e-5));   // the first faded within 2 ms
    CHECK (peak (mono.out, 0.1, 0.1025) <= 0.5f);
    // Other slots aren't touched by it.
    mono.play (0.3, { { 0, 0.0 }, { 1, 0.1 } });
    CHECK_THAT (peak (mono.out, 0.15, 0.2), WithinAbs (0.5, 1.0e-5));
}

TEST_CASE ("SAMPLE EDITOR: DELAY holds a hit back; set early, a key still plays at once", "[drumeditor]")
{
    const auto level = tone (0.0, 0.2, 0.5f);
    auto slot = sampleSlot (*level);
    slot.delay = 0.05f;
    Rig later (slot);
    later.play (0.4, { { 0, 0.0 } });
    CHECK (peak (later.out, 0.0, 0.05 - 1.0 / rate) == 0.0f);
    CHECK_THAT (later.out.getSample (0, 2400), WithinAbs (0.5, 1.0e-6));
    CHECK (lastSound (later.out) == 2400 + 9598);   // the whole sample (to its last but one, as ever), just later

    slot.delay = -0.05f;   // an anchor: the sequencer's business
    Rig early (slot);
    early.play (0.4, { { 0, 0.0 } });
    CHECK_THAT (early.out.getSample (0, 0), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (slot.anchorSeconds(), WithinAbs (0.05, 1.0e-7));
}

TEST_CASE ("SAMPLE EDITOR: LP and HP, 6 dB an octave", "[drumeditor]")
{
    // A one-pole: at 5x its corner, about -14 dB (a hair more for the low-pass up high: the digital filter's
    // frequencies squeeze towards the top); at its corner -3 dB; well inside, nothing.
    const auto check = [] (double hz, float lowPass, float highPass, double expectedDb)
    {
        const auto sine = tone (hz, 1.0);
        auto slot = sampleSlot (*sine);
        slot.lowPass = lowPass;
        slot.highPass = highPass;
        const auto hit = Kit::renderHit (slot, 1.0f, rate, 1.0);
        CHECK_THAT (juce::Decibels::gainToDecibels (peak (hit, 0.5, 0.9)), WithinAbs (expectedDb, 0.35));
    };
    check (5000.0, 1000.0f, 20.0f, -14.3);
    check (1000.0, 1000.0f, 20.0f, -3.01);
    check (100.0, 1000.0f, 20.0f, -0.04);
    check (1000.0, 20000.0f, 5000.0f, -14.15);
    check (5000.0, 20000.0f, 5000.0f, -3.01);
    check (5000.0, 20000.0f, 20.0f, 0.0);   // both off: untouched
}

