#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/synth/StepSequencer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

// THE DRUM SEQUENCER (stage 6b): a polyphonic beat for THE DRUMS' kit, as Avenger's drum sequencer (the manual's
// pages 81-82: 12 rows, any number of hits at once, up to 16 beats, 4 patterns; per note velocity, pan, pitch, gate,
// roll and reverse; shuffle, speed with triplets, velocity amount). Our own design and numbers where the manual gives
// none.
//
// Notes sit on a grid of steps (SPEED: 1/16 is 4 a beat); a note's start is in steps and needn't be whole (placed
// off the grid). The pattern loops every LENGTH beats. Its hits come from one function, `forEachHit`, used by the
// plugin, the MIDI export and the tests alike, so they can't disagree.
namespace synth::drumseq
{
    inline constexpr int numPatterns = 4, numRows = 42, maxBeats = 16, maxNotes = 1024;   // a row per drum slot (12 until PR7-D 0.2.0)
    inline const juce::StringArray patternNames { "A", "B", "C", "D" };

    // Saved by index: only ever append.
    enum class Trigger { song, key };
    inline const juce::StringArray triggerNames { "Song", "Key" };
    enum class Reverse { off, reverse, endSync };
    inline const juce::StringArray reverseNames { "Off", "Reverse", "Reverse, end sync" };
    // ROLL: the hit played this many times within its step (3: a triplet).
    inline constexpr std::array<int, 8> rollCounts { 1, 2, 3, 4, 6, 8, 12, 16 };   // (12x, 16x: 0.72.0's ratchets)
    inline const juce::StringArray rollNames { "Off", "2x", "3x", "4x", "6x", "8x", "12x", "16x" };
    // 0.72.0: CONDITION: which passes of the loop a note plays on (counted from the pattern's first pass, loop 0). Our
    // reading of drum machines' trig conditions. Saved by index: only ever append.
    inline const juce::StringArray conditionNames { "Always", "1st of 2", "2nd of 2", "1st of 3", "2nd of 3", "3rd of 3",
                                                    "1st of 4", "2nd of 4", "3rd of 4", "4th of 4", "First pass only", "Not the first pass" };
    inline bool passes (int condition, int loop) noexcept
    {
        switch (condition)
        {
            case 1: case 2: return loop % 2 == condition - 1;
            case 3: case 4: case 5: return loop % 3 == condition - 3;
            case 6: case 7: case 8: case 9: return loop % 4 == condition - 6;
            case 10: return loop == 0;
            case 11: return loop != 0;
            default: return true;
        }
    }
    // 0.72.0: CHANCE: the same pass of the same note always decides alike (bounces, exports and tests repeat), each
    // pass afresh: a hash of the pass and the note's place, 0 to 1.
    struct Note;
    inline float chanceRoll (int loop, int index, const Note& n) noexcept;

    struct Note
    {
        int row = 0;                  // the kit's slot, 0 (the kick) to numRows - 1
        double start = 0.0;           // in steps from the pattern's start
        float velocity = 1.0f;        // 0 to 1
        float pan = 0.0f;             // -1 to 1, added to the drum's own
        float pitch = 0.0f;           // semitones, added to the drum's own
        float gate = 1.0f;            // 1: the hit rings out; below, it's cut after this share of a beat
        int roll = 0;                 // index into rollCounts
        Reverse reverse = Reverse::off;
        // 0.72.0: THE RATCHET's ramps, over a roll's repeats (the first as the note, the last with all of it):
        float rollPitch = 0.0f;       // PITCH RAMP: semitones added by the last repeat (-24 to 24: rising or falling rolls)
        float rollFade = 0.0f;        // FADE: -1 the repeats die away to nothing, +1 they swell up from nothing
        // 0.72.0: CHANCE and CONDITION: whether the note plays on a pass of the loop.
        float chance = 1.0f;          // 0 to 1 (1: always)
        int condition = 0;            // conditionNames

        int rollCount() const noexcept { return rollCounts[(size_t) juce::jlimit (0, (int) rollCounts.size() - 1, roll)]; }
        bool operator== (const Note& o) const noexcept
        {
            return row == o.row && juce::exactlyEqual (start, o.start) && juce::exactlyEqual (velocity, o.velocity)
                && juce::exactlyEqual (pan, o.pan) && juce::exactlyEqual (pitch, o.pitch) && juce::exactlyEqual (gate, o.gate)
                && roll == o.roll && reverse == o.reverse && juce::exactlyEqual (rollPitch, o.rollPitch)
                && juce::exactlyEqual (rollFade, o.rollFade) && juce::exactlyEqual (chance, o.chance) && condition == o.condition;
        }
        // A roll's repeat r of its count: the note as that repeat plays (its pitch and velocity on the ramps).
        Note repeat (int r) const noexcept
        {
            const auto count = rollCount();
            if (count < 2 || (juce::exactlyEqual (rollPitch, 0.0f) && juce::exactlyEqual (rollFade, 0.0f)))
                return *this;
            const auto x = (float) r / (float) (count - 1);
            auto m = *this;
            m.pitch = juce::jlimit (-48.0f, 48.0f, pitch + rollPitch * x);
            const auto fade = juce::jlimit (-1.0f, 1.0f, rollFade);
            m.velocity = velocity * (fade < 0.0f ? 1.0f + fade * x : 1.0f - fade + fade * x);
            return m;
        }
    };

    inline float chanceRoll (int loop, int index, const Note& n) noexcept
    {
        auto h = (std::uint32_t) loop * 0x9E3779B1u ^ (std::uint32_t) index * 0x85EBCA77u ^ (std::uint32_t) n.row * 0xC2B2AE3Du
               ^ (std::uint32_t) std::llround (n.start * 1024.0) * 0x27D4EB2Fu;
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        return (float) (h >> 8) / 16777216.0f;
    }

    // 0.73.0: THE STEP FX LANE: effects on the whole drum mix, in time with the pattern (our design, after what
    // electronic beat effects do). Each a stretch of steps; one at a time (where two overlap, the earlier starting
    // one plays). Choices saved by index: only append.
    enum class FxType { none, repeat, chop, tapeStop, reverse, filter, bitcrush };
    inline const juce::StringArray fxNames { "None", "Repeat", "Chop", "Tape Stop", "Reverse", "Filter", "Bitcrush" };
    // RATE: the slice REPEAT loops and REVERSE turns round, and CHOP's gate: notes, in beats.
    inline const juce::StringArray fxRateNames { "1/2", "1/4", "1/8", "1/16", "1/32" };
    inline constexpr std::array<double, 5> fxRateBeats { 2.0, 1.0, 0.5, 0.25, 0.125 };
    inline constexpr int maxFxSteps = 64;
    struct FxStep
    {
        double start = 0.0;    // in steps from the pattern's start
        double length = 1.0;   // in steps
        FxType type = FxType::repeat;
        int rate = 3;          // fxRateNames: 1/16
        float amount = 1.0f;   // 0 to 1: what it means is the effect's (see StepFx.h)

        bool operator== (const FxStep& o) const noexcept
        {
            return juce::exactlyEqual (start, o.start) && juce::exactlyEqual (length, o.length) && type == o.type && rate == o.rate
                && juce::exactlyEqual (amount, o.amount);
        }
        double rateBeats() const noexcept { return fxRateBeats[(size_t) juce::jlimit (0, (int) fxRateBeats.size() - 1, rate)]; }
    };

    struct Pattern
    {
        std::vector<Note> notes;   // on the heap: patterns are copied with presets and undo steps
        std::vector<FxStep> fx;    // THE STEP FX LANE (0.73.0), in start order

        bool operator== (const Pattern& o) const noexcept { return notes == o.notes && fx == o.fx; }
        bool isEmpty() const noexcept { return notes.empty() && fx.empty(); }

        // The lane as text: "type:start:length:rate:amount" separated by spaces (kept apart from the notes' text, so
        // older versions read the notes as before).
        juce::String encodeFx() const
        {
            juce::StringArray parts;
            for (const auto& f : fx)
                parts.add (juce::String ((int) f.type) + ":" + juce::String (f.start, 4) + ":" + juce::String (f.length, 4) + ":"
                           + juce::String (f.rate) + ":" + juce::String (f.amount, 3));
            return parts.joinIntoString (" ");
        }
        void decodeFx (const juce::String& text)
        {
            fx.clear();
            for (const auto& part : juce::StringArray::fromTokens (text, " ", {}))
            {
                const auto f = juce::StringArray::fromTokens (part, ":", {});
                if (f.size() < 5 || (int) fx.size() >= maxFxSteps)
                    continue;
                FxStep s;
                s.type = (FxType) juce::jlimit (0, fxNames.size() - 1, f[0].getIntValue());
                s.start = juce::jmax (0.0, f[1].getDoubleValue());
                s.length = juce::jmax (1.0 / 16.0, f[2].getDoubleValue());
                s.rate = juce::jlimit (0, fxRateNames.size() - 1, f[3].getIntValue());
                s.amount = juce::jlimit (0.0f, 1.0f, f[4].getFloatValue());
                if (s.type != FxType::none)
                    fx.push_back (s);
            }
            sortFx();
        }
        void sortFx() { std::stable_sort (fx.begin(), fx.end(), [] (const auto& a, const auto& b) { return a.start < b.start; }); }
        // The effect playing at `step` (within the pattern's length), or -1.
        int fxAt (double step) const noexcept
        {
            for (int i = 0; i < (int) fx.size(); ++i)
                if (step >= fx[(size_t) i].start && step < fx[(size_t) i].start + fx[(size_t) i].length)
                    return i;
            return -1;
        }

        // As text: notes "row:start:velocity:pan:pitch:gate:roll:reverse", separated by spaces (0.72.0: then
        // ":pitchRamp:fade:chance:condition", only when any isn't its default, so older versions still read the rest).
        juce::String encode() const
        {
            juce::StringArray parts;
            for (const auto& n : notes)
                parts.add (juce::String (n.row) + ":" + juce::String (n.start, 4) + ":" + juce::String (n.velocity, 3) + ":"
                           + juce::String (n.pan, 3) + ":" + juce::String (n.pitch, 2) + ":" + juce::String (n.gate, 3) + ":"
                           + juce::String (n.roll) + ":" + juce::String ((int) n.reverse)
                           + (! juce::exactlyEqual (n.rollPitch, 0.0f) || ! juce::exactlyEqual (n.rollFade, 0.0f) || n.chance < 1.0f || n.condition != 0
                                  ? ":" + juce::String (n.rollPitch, 2) + ":" + juce::String (n.rollFade, 3) + ":" + juce::String (n.chance, 3)
                                        + ":" + juce::String (n.condition)
                                  : juce::String()));
            return parts.joinIntoString (" ");
        }

        static Pattern decode (const juce::String& text)
        {
            Pattern p;
            for (const auto& part : juce::StringArray::fromTokens (text, " ", {}))
            {
                const auto f = juce::StringArray::fromTokens (part, ":", {});
                if (f.size() < 2 || (int) p.notes.size() >= maxNotes)
                    continue;
                Note n;
                n.row = juce::jlimit (0, numRows - 1, f[0].getIntValue());
                n.start = juce::jmax (0.0, f[1].getDoubleValue());
                if (f.size() >= 8)
                {
                    n.velocity = juce::jlimit (0.0f, 1.0f, f[2].getFloatValue());
                    n.pan = juce::jlimit (-1.0f, 1.0f, f[3].getFloatValue());
                    n.pitch = juce::jlimit (-24.0f, 24.0f, f[4].getFloatValue());
                    n.gate = juce::jlimit (0.01f, 1.0f, f[5].getFloatValue());
                    n.roll = juce::jlimit (0, (int) rollCounts.size() - 1, f[6].getIntValue());
                    n.reverse = (Reverse) juce::jlimit (0, 2, f[7].getIntValue());
                }
                if (f.size() >= 12)
                {
                    n.rollPitch = juce::jlimit (-24.0f, 24.0f, f[8].getFloatValue());
                    n.rollFade = juce::jlimit (-1.0f, 1.0f, f[9].getFloatValue());
                    n.chance = juce::jlimit (0.0f, 1.0f, f[10].getFloatValue());
                    n.condition = juce::jlimit (0, conditionNames.size() - 1, f[11].getIntValue());
                }
                p.notes.push_back (n);
            }
            return p;
        }
    };

    using Patterns = std::array<Pattern, numPatterns>;

    struct Settings
    {
        bool on = false;
        Trigger trigger = Trigger::song;
        int pattern = 0;              // SONG: the pattern played (KEY: the key held chooses)
        int division = 7;             // SPEED: seq::divisionNames, 1/16
        seq::Timing timing = seq::Timing::straight;
        int lengthBeats = 4;          // 1 to 16
        float shuffle = 0.0f;         // the synth's SHUFFLE: odd steps up to half a step late
        float velocityAmount = 1.0f;  // how much the key's velocity scales the pattern (KEY)
        const Patterns* patterns = nullptr;

        double beatsPerStep() const noexcept
        {
            auto beats = seq::divisionBeats[(size_t) juce::jlimit (0, (int) seq::divisionBeats.size() - 1, division)];
            if (timing == seq::Timing::triplet)
                beats *= 2.0 / 3.0;
            else if (timing == seq::Timing::dotted)
                beats *= 1.5;
            return beats;
        }
        // The pattern's length in steps (at least 1).
        double lengthSteps() const noexcept { return juce::jmax (1.0, std::round (juce::jlimit (1, maxBeats, lengthBeats) / beatsPerStep())); }
    };

    // When a note's hits sound, in steps from the pattern's start (loop 0): its start, an odd step moved late by
    // SHUFFLE (up to half a step), each roll hit spread across the step; "end sync" reversed hits placed so they end
    // at their slot's end (`reverseSteps`: the reversed hit's length in steps); and all of it `leadSteps` early for a
    // drum whose DELAY sets an anchor (6c: that point in the hit lands on the step).
    inline double hitTime (const Note& n, const Settings& s, int rollIndex, double reverseSteps, double leadSteps = 0.0) noexcept
    {
        auto t = n.start - leadSteps;
        if (((int) std::floor (n.start + 1.0e-9)) % 2 == 1)
            t += 0.5 * juce::jlimit (0.0f, 1.0f, s.shuffle);
        const auto count = n.rollCount();
        if (n.reverse == Reverse::endSync)
            return t + (double) (rollIndex + 1) / count - reverseSteps;
        return t + (double) rollIndex / count;
    }

    // Calls onHit (time in steps, note, roll index) for every hit of `pattern`, looped, whose time falls in
    // [from, to) steps, loop 0 starting at 0 (nothing before it). `reverseSteps (note)` gives its reversed hit's length,
    // `leadSteps (note)` how early its drum's anchor starts it.
    template <typename ReverseSteps, typename LeadSteps, typename OnHit>
    void forEachHit (const Pattern& pattern, const Settings& s, double from, double to, ReverseSteps&& reverseSteps,
                     LeadSteps&& leadSteps, OnHit&& onHit)
    {
        if (pattern.notes.empty() || to <= from)
            return;
        const auto length = s.lengthSteps();
        auto earliest = 0.0;   // how far before its own start a hit can sound (end-synced reverses, anchors)
        for (const auto& n : pattern.notes)
            earliest = juce::jmax (earliest, (n.reverse == Reverse::endSync ? reverseSteps (n) : 0.0) + leadSteps (n));
        const auto firstLoop = juce::jmax (0, (int) std::floor ((from - 1.0) / length) - 1);
        const auto lastLoop = (int) std::floor ((to + earliest + 1.0) / length) + 1;
        for (int loop = firstLoop; loop <= lastLoop; ++loop)
            for (int index = 0; index < (int) pattern.notes.size(); ++index)
            {
                const auto& n = pattern.notes[(size_t) index];
                if (n.start >= length)
                    continue;   // beyond LENGTH: kept, but not played
                // 0.72.0: this pass's CONDITION and CHANCE (the whole note, all its repeats).
                if (! passes (n.condition, loop) || (n.chance < 1.0f && chanceRoll (loop, index, n) >= n.chance))
                    continue;
                for (int r = 0; r < n.rollCount(); ++r)
                {
                    const auto t = loop * length + hitTime (n, s, r, n.reverse == Reverse::endSync ? reverseSteps (n) : 0.0, leadSteps (n));
                    if (t >= from && t < to && t >= 0.0)
                        onHit (t, n.repeat (r), r);   // (0.72.0: on the ratchet's ramps)
                }
            }
    }

    template <typename ReverseSteps, typename OnHit>
    void forEachHit (const Pattern& pattern, const Settings& s, double from, double to, ReverseSteps&& reverseSteps, OnHit&& onHit)
    {
        forEachHit (pattern, s, from, to, reverseSteps, [] (const Note&) { return 0.0; }, onHit);
    }

    // MIDI EXPORT: one pass of the pattern, the drums on the kit's keys (ROOT up), 960 ticks a beat; a roll's hits
    // written out; a note lasts its gate (a ringing one, its slot of the step). Pan, pitch and reverse can't be
    // written as notes, so they aren't. A drum with an anchor (`leadSeconds` by row) is written that much early, so
    // played back from the track its anchor still lands on the beat (one at the very start wraps to the end).
    // `notes`: the MIDI note each row is written on (-1: that row's notes are left out).
    // The notes of the ROOT layout: row k on root + k.
    inline std::array<int, numRows> rootNotes (int root)
    {
        std::array<int, numRows> notes {};
        for (int k = 0; k < numRows; ++k)
            notes[(size_t) k] = root + k;
        return notes;
    }

    inline juce::MidiFile toMidi (const Pattern& pattern, const Settings& s, const std::array<int, numRows>& notes, double bpm,
                                  const std::array<double, numRows>& leadSeconds = {})
    {
        constexpr int ticksPerBeat = 960;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / juce::jmax (1.0, bpm))), 0.0);
        const auto ticksPerStep = s.beatsPerStep() * ticksPerBeat;
        const auto lengthTicks = s.lengthSteps() * ticksPerStep;
        const auto stepsPerSecond = juce::jmax (1.0, bpm) / 60.0 / s.beatsPerStep();
        const auto lead = [&] (const Note& n) { return leadSeconds[(size_t) juce::jlimit (0, numRows - 1, n.row)] * stepsPerSecond; };
        forEachHit (pattern, s, 0.0, s.lengthSteps(), [] (const Note&) { return 0.0; }, lead, [&] (double t, const Note& n, int)
        {
            const auto at = std::round (t * ticksPerStep);   // a MIDI file keeps whole ticks (and a hair off isn't a tick early)
            const auto lasts = n.gate < 1.0f ? n.gate * ticksPerBeat : ticksPerStep / n.rollCount();
            const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));
            const auto note = notes[(size_t) juce::jlimit (0, numRows - 1, n.row)];
            if (note < 0 || note > 127)
                return;
            track.addEvent (juce::MidiMessage::noteOn (10, note, velocity), at);
            track.addEvent (juce::MidiMessage::noteOff (10, note), juce::jmin (lengthTicks, at + juce::jmax (1.0, lasts - 1.0)));
        });
        track.updateMatchedPairs();
        track.sort();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ticksPerBeat);
        file.addTrack (track);
        return file;
    }
}
