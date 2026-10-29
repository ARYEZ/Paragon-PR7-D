// THE DRUM SEQUENCER (stage 6b): its patterns as text, when its hits sound, and what each note's settings do to the
// kit's sound.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/synth/Drums.h"

using namespace synth;
using namespace synth::drums;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int stepSamples = 6000;   // a sixteenth at 120 BPM

    // A one-sample click (or a tone), played as it is by a SAMPLE slot: the output is the hit's gain exactly.
    std::shared_ptr<Sample> click()
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        s->channels = { std::vector<float> (400, 0.0f) };
        s->channels[0][0] = 1.0f;
        return s;
    }

    std::shared_ptr<Sample> tone (double hz, double seconds, bool fading = false)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        const auto n = (int) (seconds * rate);
        s->channels = { std::vector<float> ((size_t) n) };
        for (int i = 0; i < n; ++i)
            s->channels[0][(size_t) i] = (fading ? 1.0f - (float) i / (float) n : 1.0f)
                                         * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate);
        return s;
    }

    struct Rig
    {
        Kit kit;
        KitSettings settings;
        drumseq::Settings sequencer;
        drumseq::Patterns patterns;
        std::shared_ptr<Sample> sample = click();
        static constexpr int block = 256;
        std::vector<float> left, right;

        Rig()
        {
            kit.prepare (rate, block);
            kit.seedRandom (3);
            settings.on = true;
            for (auto& s : settings.slots)
            {
                s.model = Model::sample;
                s.decay = s.tone = s.snap = 0.5f;
                s.sample = sample.get();
                s.reverseSource = sample.get();
            }
            sequencer.on = true;
            sequencer.patterns = &patterns;
        }

        void use (const std::shared_ptr<Sample>& s)
        {
            sample = s;
            for (auto& slot : settings.slots)
                slot.sample = slot.reverseSource = sample.get();
        }

        drumseq::Note& add (int row, double start, int pattern = 0)
        {
            drumseq::Note n;
            n.row = row;
            n.start = start;
            patterns[(size_t) pattern].notes.push_back (n);
            return patterns[(size_t) pattern].notes.back();
        }

        // Plays `seconds` at 120 BPM from song position `ppq` (beats; below 0: the song stopped), with these keys
        // (note, velocity, on at, off at: in samples).
        void play (double seconds, double ppq = 0.0, std::vector<std::tuple<int, float, int, int>> keys = {})
        {
            const auto n = (int) (seconds * rate);
            left.assign ((size_t) n, 0.0f);
            right.assign ((size_t) n, 0.0f);
            for (int done = 0; done < n; done += block)
            {
                const auto count = std::min (block, n - done);
                juce::MidiBuffer midi;
                for (const auto& [note, velocity, on, off] : keys)
                {
                    if (on >= done && on < done + count)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), on - done);
                    if (off >= done && off < done + count)
                        midi.addEvent (juce::MidiMessage::noteOff (1, note), off - done);
                }
                Outputs out;
                out.left = left.data() + done;
                out.right = right.data() + done;
                kit.process (out, count, midi, 0, settings, sequencer, ppq < 0.0 ? -1.0 : ppq + done / rate * 2.0, 120.0);
            }
        }

        // Where the clicks are (samples over half the loudest... or over `above`).
        std::vector<int> clicks (float above = 0.01f) const
        {
            std::vector<int> at;
            for (size_t i = 0; i < left.size(); ++i)
                if (std::abs (left[i]) > above)
                    at.push_back ((int) i);
            return at;
        }

        static float peak (const std::vector<float>& v, int from = 0, int to = 1 << 30)
        {
            auto p = 0.0f;
            for (int i = from; i < std::min ((int) v.size(), to); ++i)
                p = std::max (p, std::abs (v[(size_t) i]));
            return p;
        }
    };
}

TEST_CASE ("DRUM SEQUENCER: patterns save and load as text", "[drumseq]")
{
    drumseq::Pattern p;
    drumseq::Note a;
    a.row = 2;
    a.start = 3.5;
    a.velocity = 0.75f;
    a.pan = -0.5f;
    a.pitch = 7.0f;
    a.gate = 0.25f;
    a.roll = 2;
    a.reverse = drumseq::Reverse::endSync;
    p.notes = { a, drumseq::Note {} };
    CHECK (drumseq::Pattern::decode (p.encode()) == p);
    CHECK (drumseq::Pattern::decode ({}).isEmpty());
    // Only row and start: the rest as new.
    const auto short_ = drumseq::Pattern::decode ("11:6");
    REQUIRE (short_.notes.size() == 1);
    CHECK (short_.notes[0].row == 11);
    CHECK (short_.notes[0].start == 6.0);
    CHECK (short_.notes[0].velocity == 1.0f);
    // Out of range: kept in range.
    CHECK (drumseq::Pattern::decode ("40:-2:3:5:99:0:9:7").notes[0] .row == 11);
}

TEST_CASE ("DRUM SEQUENCER: hit times: steps, SPEED, SHUFFLE on odd steps, ROLLs across the step, end sync, LENGTH",
           "[drumseq]")
{
    drumseq::Settings s;
    CHECK (s.beatsPerStep() == 0.25);       // 1/16
    CHECK (s.lengthSteps() == 16.0);        // 4 beats
    s.timing = seq::Timing::triplet;
    CHECK_THAT (s.beatsPerStep(), WithinAbs (1.0 / 6.0, 1e-12));
    CHECK (s.lengthSteps() == 24.0);
    s.timing = seq::Timing::straight;

    drumseq::Note n;
    n.start = 1.0;
    CHECK (drumseq::hitTime (n, s, 0, 0.0) == 1.0);
    s.shuffle = 1.0f;
    CHECK (drumseq::hitTime (n, s, 0, 0.0) == 1.5);   // an odd step: half a step late at full SHUFFLE
    n.start = 2.0;
    CHECK (drumseq::hitTime (n, s, 0, 0.0) == 2.0);   // even ones stay
    s.shuffle = 0.0f;
    n.roll = 3;                                       // 4x
    CHECK (drumseq::hitTime (n, s, 3, 0.0) == 2.75);
    n.roll = 2;                                       // 3x: a triplet
    CHECK_THAT (drumseq::hitTime (n, s, 1, 0.0), WithinAbs (2.0 + 1.0 / 3.0, 1e-12));
    n.roll = 0;
    n.reverse = drumseq::Reverse::endSync;            // ends at the end of its step
    CHECK (drumseq::hitTime (n, s, 0, 2.5) == 0.5);

    // Looped every LENGTH; a note past LENGTH is kept but silent; nothing before the start.
    drumseq::Pattern p;
    drumseq::Note a, b, c;
    a.start = 0.0;
    b.start = 7.0;
    c.start = 20.0;
    p.notes = { a, b, c };
    s.lengthBeats = 2;   // 8 steps
    std::vector<double> times;
    drumseq::forEachHit (p, s, 0.0, 24.0, [] (const auto&) { return 0.0; }, [&] (double t, const auto&, int) { times.push_back (t); });
    std::sort (times.begin(), times.end());
    CHECK (times == std::vector<double> { 0.0, 7.0, 8.0, 15.0, 16.0, 23.0 });
    times.clear();
    drumseq::forEachHit (p, s, 7.5, 15.0, [] (const auto&) { return 0.0; }, [&] (double t, const auto&, int) { times.push_back (t); });
    CHECK (times == std::vector<double> { 8.0 });
}

TEST_CASE ("DRUM SEQUENCER: SONG plays each hit on its sample, follows the song and loops; stopped, nothing", "[drumseq]")
{
    Rig rig;
    rig.sequencer.lengthBeats = 2;   // 8 steps
    rig.add (0, 0.0);
    rig.add (1, 3.0);
    rig.play (1.5);
    CHECK (rig.clicks() == std::vector<int> { 0, 3 * stepSamples, 8 * stepSamples, 11 * stepSamples });

    // From half a beat in (step 2): the next is step 3, a step later.
    rig.play (0.5, 0.5);
    CHECK (rig.clicks() == std::vector<int> { stepSamples });

    rig.play (0.5, -1.0);
    CHECK (rig.clicks().empty());
    rig.sequencer.on = false;
    rig.play (0.5);
    CHECK (rig.clicks().empty());
    rig.sequencer.on = true;
    rig.settings.on = false;   // the kit off: its sequencer too
    rig.play (0.5);
    CHECK (rig.clicks().empty());
}

TEST_CASE ("DRUM SEQUENCER: SHUFFLE, ROLL and SPEED move the hits", "[drumseq]")
{
    Rig rig;
    rig.add (0, 1.0);
    rig.add (0, 2.0);
    rig.sequencer.shuffle = 1.0f;
    rig.play (0.3);
    CHECK (rig.clicks() == std::vector<int> { 9000, 12000 });

    Rig roll;
    roll.add (0, 0.0).roll = 3;   // 4x
    roll.add (1, 4.0).roll = 2;   // 3x
    roll.play (0.7);
    CHECK (roll.clicks() == std::vector<int> { 0, 1500, 3000, 4500, 24000, 26000, 28000 });

    Rig triplets;
    triplets.sequencer.timing = seq::Timing::triplet;   // 1/16 triplets: 4000 samples a step
    triplets.add (0, 3.0);
    triplets.play (0.3);
    CHECK (triplets.clicks() == std::vector<int> { 12000 });
}

TEST_CASE ("DRUM SEQUENCER: a note's VELOCITY, PAN, PITCH and GATE", "[drumseq]")
{
    Rig rig;
    rig.add (0, 0.0).velocity = 0.5f;
    rig.add (1, 4.0).pan = 1.0f;
    rig.add (2, 8.0).pan = -0.5f;
    rig.play (1.1);
    CHECK_THAT (Rig::peak (rig.left, 0, 100), WithinAbs (0.25, 1e-6));   // its velocity squared
    CHECK_THAT (Rig::peak (rig.right, 0, 100), WithinAbs (0.25, 1e-6));
    CHECK (Rig::peak (rig.left, 24000, 24100) == 0.0f);                  // full right
    CHECK_THAT (Rig::peak (rig.right, 24000, 24100), WithinAbs (1.0, 1e-6));
    CHECK_THAT (Rig::peak (rig.left, 48000 - 10, 48100), WithinAbs (1.0, 1e-6));
    CHECK_THAT (Rig::peak (rig.right, 48000 - 10, 48100), WithinAbs (0.5, 1e-6));

    // PITCH: an octave up plays the tone twice as fast; GATE 1/4 beat cuts it after 125 ms (and a 3 ms fade).
    Rig pitched;
    pitched.use (tone (500.0, 1.0));
    auto& n = pitched.add (0, 0.0);
    n.pitch = 12.0f;
    n.gate = 0.25f;
    pitched.play (0.6);
    auto crossings = 0;
    for (int i = 1; i < 4800; ++i)
        crossings += pitched.left[(size_t) i - 1] < 0.0f && pitched.left[(size_t) i] >= 0.0f;
    CHECK (std::abs (crossings - 100) <= 1);   // 1 kHz for 0.1 s
    CHECK (Rig::peak (pitched.left, 5000, 6000) > 0.9f);
    CHECK (Rig::peak (pitched.left, 6000 + 150) == 0.0f);

    // A full gate rings out: the tone lasts its half second (at the octave).
    Rig ringing;
    ringing.use (tone (500.0, 1.0));
    ringing.add (0, 0.0).pitch = 12.0f;
    ringing.play (0.6);
    CHECK (Rig::peak (ringing.left, 23000, 23900) > 0.9f);
    CHECK (Rig::peak (ringing.left, 24100) == 0.0f);
}

TEST_CASE ("DRUM SEQUENCER: REVERSE plays the hit backwards; end sync ends it at its step's end", "[drumseq]")
{
    // A tone that fades out over 0.25 s (12000 samples, two steps), backwards: fading in.
    Rig rig;
    rig.use (tone (1000.0, 0.25, true));
    rig.add (0, 0.0).reverse = drumseq::Reverse::reverse;
    rig.play (0.5);
    CHECK (Rig::peak (rig.left, 0, 1200) < 0.12f);
    CHECK (Rig::peak (rig.left, 10800, 11900) > 0.85f);
    CHECK (Rig::peak (rig.left, 12000) == 0.0f);

    // End sync on step 4: it ends where step 4 does (step 5, 30000 samples), so it starts two steps before.
    Rig synced;
    synced.use (tone (1000.0, 0.25, true));
    synced.add (0, 4.0).reverse = drumseq::Reverse::endSync;
    synced.play (0.75);
    const auto heard = synced.clicks (1.0e-4f);
    REQUIRE_FALSE (heard.empty());
    CHECK (std::abs (heard.front() - 18000) < 30);
    CHECK (std::abs (heard.back() - 30000) < 30);

    // Without a source to reverse (a SAMPLE slot with no file), nothing; a plain note still plays.
    Rig none;
    none.settings.slots[0].reverseSource = none.settings.slots[0].sample = nullptr;
    none.add (0, 0.0).reverse = drumseq::Reverse::reverse;
    none.add (1, 1.0);
    none.play (0.2);
    CHECK (none.clicks() == std::vector<int> { stepSamples });
    CHECK_THAT (Kit::reverseSeconds (rig.settings.slots[0], 12.0f, rate), WithinAbs (0.125, 1e-9));
}

TEST_CASE ("DRUM SEQUENCER: KEY: the four keys under the kit start patterns A to D while held", "[drumseq]")
{
    Rig rig;
    rig.sequencer.trigger = drumseq::Trigger::key;
    rig.sequencer.lengthBeats = 1;   // 4 steps, 24000 samples
    rig.add (0, 0.0, 0);
    rig.add (0, 0.0, 1).velocity = 0.5f;
    CHECK (Kit::patternForKey (32, 36) == 0);
    CHECK (Kit::patternForKey (35, 36) == 3);
    CHECK (Kit::patternForKey (36, 36) == -1);
    CHECK (Kit::patternForKey (31, 36) == -1);

    // B (key 33) from sample 1000 to 30000: its clock starts at the key; the song position doesn't matter.
    rig.play (1.0, 3.3, { { 33, 1.0f, 1000, 30000 } });
    CHECK (rig.clicks() == std::vector<int> { 1000, 25000 });
    CHECK_THAT (Rig::peak (rig.left), WithinAbs (0.25, 1e-6));

    // A held, then B over it from 13000 (B takes over on A's clock), let go at 26000: back to A.
    rig.play (1.2, 0.0, { { 32, 1.0f, 1000, 55000 }, { 33, 1.0f, 13000, 26000 } });
    CHECK (rig.clicks() == std::vector<int> { 1000, 25000, 49000 });
    CHECK_THAT (Rig::peak (rig.left, 0, 2000), WithinAbs (1.0, 1e-6));
    CHECK_THAT (Rig::peak (rig.left, 24000, 26000), WithinAbs (0.25, 1e-6));
    CHECK_THAT (Rig::peak (rig.left, 48000, 50000), WithinAbs (1.0, 1e-6));

    // VELOCITY AMOUNT: the key's velocity scales the pattern's.
    rig.sequencer.velocityAmount = 1.0f;
    rig.play (0.2, 0.0, { { 32, 0.5f, 0, 9000 } });
    const auto v = juce::MidiMessage::noteOn (1, 32, 0.5f).getFloatVelocity();
    CHECK_THAT (Rig::peak (rig.left), WithinAbs (v * v, 1e-6));

    // The kit's own keys still play; with SONG, the four keys do nothing.
    rig.play (0.2, 0.0, { { 37, 1.0f, 500, 600 } });
    CHECK (rig.clicks() == std::vector<int> { 500 });
    rig.sequencer.trigger = drumseq::Trigger::song;
    rig.play (0.2, -1.0, { { 32, 1.0f, 500, 9000 } });
    CHECK (rig.clicks().empty());
}

TEST_CASE ("DRUM SEQUENCER: the page's playhead", "[drumseq]")
{
    Rig rig;
    rig.add (0, 0.0);
    rig.play (0.25, 1.0);   // from beat 1 (step 4), a quarter second: 2 steps on
    CHECK_THAT (rig.kit.getSequencerPosition(), WithinAbs (6.0, 1e-6));
    rig.play (0.1, -1.0);
    CHECK (rig.kit.getSequencerPosition() < 0.0);
}

TEST_CASE ("DRUM SEQUENCER: MIDI export writes one pass on the kit's keys", "[drumseq]")
{
    drumseq::Settings s;
    s.lengthBeats = 2;
    drumseq::Pattern p;
    drumseq::Note a, b, past;
    a.row = 2;
    a.roll = 1;   // 2x
    a.velocity = 0.5f;
    b.row = 0;
    b.start = 4.0;
    b.gate = 0.5f;
    past.start = 9.0;   // past LENGTH
    p.notes = { a, b, past };
    const auto file = drumseq::toMidi (p, s, 36, 100.0);
    REQUIRE (file.getNumTracks() == 1);
    std::vector<std::tuple<int, int, double>> ons;   // note, velocity, tick
    double tempo = 0.0;
    for (const auto* e : *file.getTrack (0))
    {
        if (e->message.isNoteOn())
            ons.emplace_back (e->message.getNoteNumber(), e->message.getVelocity(), e->message.getTimeStamp());
        if (e->message.isTempoMetaEvent())
            tempo = 60.0 / e->message.getTempoSecondsPerQuarterNote();
        if (e->message.isNoteOff() && e->message.getNoteNumber() == 36)
            CHECK (e->message.getTimeStamp() == 960.0 + 479.0);   // half a beat
    }
    CHECK_THAT (tempo, WithinAbs (100.0, 0.01));
    REQUIRE (ons.size() == 3);
    CHECK (ons[0] == std::tuple<int, int, double> { 38, 64, 0.0 });
    CHECK (ons[1] == std::tuple<int, int, double> { 38, 64, 120.0 });   // the roll's second, half a step on
    CHECK (ons[2] == std::tuple<int, int, double> { 36, 127, 960.0 });
}

TEST_CASE ("DRUM SEQUENCER: a drum's anchor (DELAY set early) starts it that much before its step; MIDI export too", "[drumseq]")
{
    Rig rig;
    for (auto& s : rig.settings.slots)
        s.delay = -0.0625f;   // half a step at 120 BPM in 1/16s
    rig.add (0, 4.0);
    rig.add (0, 0.0);
    rig.play (2.1);
    // Step 4 starts 3000 samples early; step 0 can't start before the song does, so the first is the next bar's.
    CHECK (rig.clicks() == std::vector<int> { 21000, 93000 });

    // Set late, both play late (the kit holds them back).
    Rig late;
    for (auto& s : late.settings.slots)
        s.delay = 0.0625f;
    late.add (0, 4.0);
    late.play (0.6);
    CHECK (late.clicks() == std::vector<int> { 27000 });

    // Exported, a drum with an anchor is written early by its lead (the first note wraps to the end of the bar).
    drumseq::Settings s;
    drumseq::Pattern p;
    drumseq::Note n;
    p.notes = { n };
    n.start = 4.0;
    p.notes.push_back (n);
    std::array<double, drumseq::numRows> lead {};
    lead[0] = 0.0625;
    std::vector<double> ticks;
    const auto file = drumseq::toMidi (p, s, 36, 120.0, lead);
    for (const auto* e : *file.getTrack (0))
        if (e->message.isNoteOn())
            ticks.push_back (e->message.getTimeStamp());
    CHECK (ticks == std::vector<double> { 840.0, 3720.0 });
}
