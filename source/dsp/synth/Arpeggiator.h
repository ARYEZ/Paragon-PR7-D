#pragma once

#include <juce_core/juce_core.h>

#include "StepSequencer.h"

#include <algorithm>
#include <array>
#include <bit>

// The ARPEGGIATOR (stage 5a), modelled on Avenger's (the manual's pages
// 74-80; our own design and code). Up to 8 arp modules; each oscillator can be
// routed to one (its notes then come from that arp, not straight from the
// keys). A module has 4 patterns (A to D, played on their own or one after
// another) of up to 32 steps. A block on a step plays the arp's next note
// (or, in POLY, every held note), moved by its TRANSPOSE, BLOCKS steps long,
// at its VELOCITY, repeated RATCHET times within its length, and SLIDE to the
// next block (no new note: the pitch glides, with PLAY's GLIDE time).
//
// The manual names UP, POLY and CHORDS among the modes; DOWN, UP/DOWN,
// DOWN/UP, AS PLAYED and RANDOM are the usual others (our list).
//
// Stage 5b: CHORDS (each block a chord of its own type, tones switched off
// as wanted, a sub note), FIXED NOTE (a held chord's root alone goes into the
// arp; "+" also moves the pattern's minor intervals to major ones over a major
// chord), the note RANDOMIZER and MIDI export. The manual doesn't list
// Avenger's chord types or scales: ours are the common ones.
namespace synth
{
    namespace arp
    {
        inline constexpr int numArps = 8, maxSteps = 32, numPatterns = 4, maxHeld = 32, maxRatchet = 8;

        // Saved by index (arp*Mode, arp*Velocity, arp*Trigger, osc*Arp): only ever append.
        enum class Mode { up, down, upDown, downUp, asPlayed, random, poly,
                          chords };   // 0.16.0
        inline const juce::StringArray modeNames { "Up", "Down", "Up/Down", "Down/Up", "As Played", "Random", "Poly", "Chords" };
        // FIXED NOTE (0.16.0): what goes into the arp from the keys: the keys themselves, or a held chord's root alone (+: and the pattern follows the chord).
        enum class NoteSource { keys, fixedNote, fixedNotePlus };
        inline const juce::StringArray noteSourceNames { "Keys", "Fixed Note", "Fixed Note +" };

        // CHORDS' types (0.16.0; saved by index: only ever append), as intervals from the root (up to 5 tones).
        inline const juce::StringArray chordNames { "Single", "Major", "Minor", "Sus2", "Sus4", "Dim", "Aug", "6", "Min6", "7", "Maj7",
                                                    "Min7", "Min7b5", "Dim7", "Add9", "9", "Maj9", "Min9", "Power", "Octave" };
        inline constexpr int maxChordTones = 5;
        struct ChordShape { int count; std::array<int, maxChordTones> intervals; };
        inline constexpr std::array<ChordShape, 20> chordShapes { {
            { 1, { 0 } }, { 3, { 0, 4, 7 } }, { 3, { 0, 3, 7 } }, { 3, { 0, 2, 7 } }, { 3, { 0, 5, 7 } }, { 3, { 0, 3, 6 } }, { 3, { 0, 4, 8 } },
            { 4, { 0, 4, 7, 9 } }, { 4, { 0, 3, 7, 9 } }, { 4, { 0, 4, 7, 10 } }, { 4, { 0, 4, 7, 11 } }, { 4, { 0, 3, 7, 10 } },
            { 4, { 0, 3, 6, 10 } }, { 4, { 0, 3, 6, 9 } }, { 4, { 0, 4, 7, 14 } }, { 5, { 0, 4, 7, 10, 14 } }, { 5, { 0, 4, 7, 11, 14 } },
            { 5, { 0, 3, 7, 10, 14 } }, { 3, { 0, 7, 12 } }, { 2, { 0, 12 } } } };
        inline const ChordShape& chordShape (int type) noexcept { return chordShapes[(size_t) juce::jlimit (0, (int) chordShapes.size() - 1, type)]; }
        enum class VelocityMode { eachNote, firstNote, lastNote, lowestNote };
        inline const juce::StringArray velocityModeNames { "Each Note", "First Note", "Last Note", "Lowest Note" };
        // SONG: in step with the song while it plays (from the first key when it's stopped); KEY: from the first key, always.
        enum class Trigger { song, key };
        inline const juce::StringArray triggerNames { "Song", "Key" };
        inline const juce::StringArray routeNames { "Off", "Arp 1", "Arp 2", "Arp 3", "Arp 4", "Arp 5", "Arp 6", "Arp 7", "Arp 8" };
        inline const juce::StringArray patternNames { "A", "B", "C", "D" };

        struct Block
        {
            bool on = false;
            int length = 1;          // steps, 1 to 32
            int transpose = 0;       // semitones, -36 to 36
            float velocity = 1.0f;   // 0 to 1 (times the note's)
            bool slide = false;      // glide into the next block instead of a new note
            int ratchet = 1;         // 1 to 8: played that many times within its length
            // CHORDS (0.16.0): the chord's type, which of its tones sound (bit per tone), and a sub note under it.
            int chord = 1;           // Major
            int chordMask = 0x1f;
            bool sub = false;
            int subTranspose = -12;  // semitones from the root, -24 to 0

            bool operator== (const Block& o) const noexcept
            {
                return on == o.on && length == o.length && transpose == o.transpose && juce::exactlyEqual (velocity, o.velocity)
                    && slide == o.slide && ratchet == o.ratchet && chord == o.chord && chordMask == o.chordMask && sub == o.sub
                    && subTranspose == o.subTranspose;
            }
        };

        struct Pattern
        {
            std::array<Block, maxSteps> blocks {};
            bool operator== (const Pattern& o) const noexcept { return blocks == o.blocks; }
            bool isEmpty() const noexcept { return std::none_of (blocks.begin(), blocks.end(), [] (const Block& b) { return b.on; }); }
        };

        struct Patterns
        {
            std::array<Pattern, numPatterns> patterns {};
            bool operator== (const Patterns& o) const noexcept { return patterns == o.patterns; }

            // A new arp: pattern A a block on each of the first 16 steps (a plain arpeggio); B to D empty.
            static Patterns makeDefault() noexcept
            {
                Patterns p;
                for (int k = 0; k < 16; ++k)
                    p.patterns[0].blocks[(size_t) k].on = true;
                return p;
            }
        };

        // A key the arp holds: its note, velocity and when it was pressed.
        struct Held
        {
            int note = 60;
            float velocity = 1.0f;
            int order = 0;
        };

        // The notes the arp walks through (MODE over OCTAVES), into `out`: how many.
        // POLY and RANDOM: the held notes over the octaves, lowest first.
        inline int sequence (Mode mode, const Held* held, int count, int octaves, int* out, int maxOut) noexcept
        {
            if (count <= 0)
                return 0;
            std::array<Held, maxHeld> sorted {};
            const auto n = juce::jmin (count, maxHeld);
            std::copy (held, held + n, sorted.begin());
            if (mode == Mode::asPlayed)
                std::sort (sorted.begin(), sorted.begin() + n, [] (const Held& a, const Held& b) { return a.order < b.order; });
            else
                std::sort (sorted.begin(), sorted.begin() + n, [] (const Held& a, const Held& b) { return a.note < b.note; });
            int up[maxHeld * 4] {};
            auto length = 0;
            for (int o = 0; o < juce::jlimit (1, 4, octaves); ++o)
                for (int i = 0; i < n; ++i)
                    up[length++] = sorted[(size_t) i].note + 12 * o;
            auto written = 0;
            const auto put = [&] (int note) { if (written < maxOut) out[written++] = juce::jlimit (0, 127, note); };
            switch (mode)
            {
                case Mode::down:
                    for (int i = length - 1; i >= 0; --i) put (up[i]);
                    break;
                case Mode::upDown:
                    for (int i = 0; i < length; ++i) put (up[i]);
                    for (int i = length - 2; i >= 1; --i) put (up[i]);   // the ends once each
                    break;
                case Mode::downUp:
                    for (int i = length - 1; i >= 0; --i) put (up[i]);
                    for (int i = 1; i < length - 1; ++i) put (up[i]);
                    break;
                case Mode::up:
                case Mode::asPlayed:
                case Mode::random:
                case Mode::poly:
                case Mode::chords:
                    for (int i = 0; i < length; ++i) put (up[i]);
                    break;
            }
            return written;
        }

        //======================================================================
        // FIXED NOTE: the chord a set of keys makes, by its root and whether it's major.
        struct DetectedChord
        {
            int root = -1;          // a MIDI note: the root, in the lowest key's octave or above (-1: no keys)
            bool major = false;     // major (or dominant): "+" moves the pattern's minor intervals to major ones
        };
        inline DetectedChord detectChord (const Held* held, int count) noexcept
        {
            DetectedChord d;
            if (count <= 0)
                return d;
            auto lowest = 127;
            auto set = 0;   // pitch classes held
            for (int i = 0; i < count; ++i)
            {
                lowest = juce::jmin (lowest, held[i].note);
                set |= 1 << (held[i].note % 12);
            }
            // Each pitch class held as a root, against each chord type: the most tones matched, fewest left over.
            // (The lowest key wins a tie: the chord as voiced.)
            auto bestScore = -100, bestRoot = lowest % 12, bestType = 0;
            for (int offset = 0; offset < 12; ++offset)
            {
                const auto root = (lowest % 12 + offset) % 12;
                if ((set & (1 << root)) == 0)
                    continue;
                for (int type = 1; type < 18; ++type)
                {
                    const auto& shape = chordShapes[(size_t) type];
                    auto shapeSet = 0;
                    for (int t = 0; t < shape.count; ++t)
                        shapeSet |= 1 << ((root + shape.intervals[(size_t) t]) % 12);
                    const auto matched = std::popcount ((unsigned) (set & shapeSet)), extra = std::popcount ((unsigned) (set & ~shapeSet) & 0xfffu);
                    const auto score = 4 * matched - 5 * extra - shape.count;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestRoot = root;
                        bestType = type;
                    }
                }
            }
            d.root = lowest + (bestRoot - lowest % 12 + 12) % 12;
            const auto& found = chordShapes[(size_t) bestType];
            d.major = std::find (found.intervals.begin(), found.intervals.begin() + found.count, 4) != found.intervals.begin() + found.count;
            return d;
        }

        // FIXED NOTE +: the pattern is written as if over a minor chord; over a major one its minor third,
        // sixth and seventh become major (+3 to +4, +8 to +9, +10 to +11), in every octave.
        inline int plusInterval (int transpose, bool major) noexcept
        {
            if (! major)
                return transpose;
            const auto octave = (int) std::floor ((double) transpose / 12.0);
            const auto within = transpose - 12 * octave;
            const auto moved = within == 3 ? 4 : within == 8 ? 9 : within == 10 ? 11 : within;
            return 12 * octave + moved;
        }

        //======================================================================
        // The note RANDOMIZER (0.16.0): a new pattern from chances. The notes are
        // relative to the key played (a pattern made in a major scale plays in D
        // major on a D), kept to the SCALE (other notes get no chance).
        inline const juce::StringArray scaleNames { "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian",
                                                    "Lydian", "Mixolydian", "Locrian", "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone" };
        inline constexpr std::array<int, 14> scaleMasks {   // bit per semitone above the root
            0xfff, 0xab5, 0x5ad, 0x9ad, 0xaad, 0x6ad, 0x5ab, 0xad5, 0x6b5, 0x56b, 0x295, 0x4a9, 0x4e9, 0x555 };
        inline bool inScale (int scale, int semitone) noexcept
        {
            return (scaleMasks[(size_t) juce::jlimit (0, (int) scaleMasks.size() - 1, scale)] >> (((semitone % 12) + 12) % 12)) & 1;
        }

        struct Randomizer
        {
            int scale = 1;                                                   // Major
            std::array<float, 12> noteChances { 1, 0.3f, 0.6f, 0.3f, 0.6f, 0.6f, 0.3f, 0.8f, 0.3f, 0.5f, 0.3f, 0.4f };   // by semitone above the key
            std::array<float, 5> octaveChances { 0.0f, 0.15f, 1.0f, 0.4f, 0.0f };        // -2 to +2 octaves
            std::array<float, 4> lengthChances { 1.0f, 0.35f, 0.0f, 0.1f };              // 1 to 4 steps
            float restChance = 0.15f;
            float slideChance = 0.0f, ratchetChance = 0.0f;
            float velocityLow = 0.6f, velocityHigh = 1.0f;
            bool rollNotes = true, rollOctaves = true, rollLengths = true, rollVelocity = true, rollRests = true;
            bool symmetry = false;     // the second half the first, backwards
            bool autoDice = false;     // a new pattern at the start of every cycle
            int seed = 1;

            bool operator== (const Randomizer& o) const noexcept
            {
                const auto same = [] (const auto& a, const auto& b)
                {
                    for (size_t i = 0; i < a.size(); ++i)
                        if (! juce::exactlyEqual (a[i], b[i]))
                            return false;
                    return true;
                };
                return scale == o.scale && same (noteChances, o.noteChances) && same (octaveChances, o.octaveChances)
                    && same (lengthChances, o.lengthChances) && juce::exactlyEqual (restChance, o.restChance)
                    && juce::exactlyEqual (slideChance, o.slideChance) && juce::exactlyEqual (ratchetChance, o.ratchetChance)
                    && juce::exactlyEqual (velocityLow, o.velocityLow) && juce::exactlyEqual (velocityHigh, o.velocityHigh)
                    && rollNotes == o.rollNotes && rollOctaves == o.rollOctaves && rollLengths == o.rollLengths
                    && rollVelocity == o.rollVelocity && rollRests == o.rollRests && symmetry == o.symmetry && autoDice == o.autoDice
                    && seed == o.seed;
            }
        };

        // One choice by chances (all 0: the first).
        template <size_t N>
        int pick (const std::array<float, N>& chances, juce::Random& random) noexcept
        {
            auto total = 0.0f;
            for (const auto c : chances)
                total += juce::jmax (0.0f, c);
            if (total <= 0.0f)
                return 0;
            auto x = random.nextFloat() * total;
            for (size_t i = 0; i < N; ++i)
            {
                x -= juce::jmax (0.0f, chances[i]);
                if (x < 0.0f)
                    return (int) i;
            }
            return (int) N - 1;
        }

        // A pattern rolled from `r` (with `seed`) over `length` steps; what isn't rolled is kept from `current`.
        inline Pattern roll (const Randomizer& r, int length, const Pattern& current, juce::int64 seed) noexcept
        {
            juce::Random random (seed);
            auto p = current;
            length = juce::jlimit (2, maxSteps, length);
            std::array<float, 12> notes {};
            for (int n = 0; n < 12; ++n)
                notes[(size_t) n] = inScale (r.scale, n) ? r.noteChances[(size_t) n] : 0.0f;
            const auto half = r.symmetry ? (length + 1) / 2 : length;
            for (int k = 0; k < half; )
            {
                auto& b = p.blocks[(size_t) k];
                if (r.rollRests)
                    b.on = random.nextFloat() >= r.restChance;   // otherwise its rest or note stays
                if (r.rollLengths)
                    b.length = 1 + pick (r.lengthChances, random);
                if (r.rollNotes || r.rollOctaves)
                {
                    const auto semitone = r.rollNotes ? pick (notes, random) : ((b.transpose % 12) + 12) % 12;
                    const auto octave = r.rollOctaves ? pick (r.octaveChances, random) - 2 : (int) std::floor ((double) b.transpose / 12.0);
                    b.transpose = juce::jlimit (-36, 36, semitone + 12 * octave);
                }
                if (r.rollVelocity)
                    b.velocity = (float) juce::roundToInt ((r.velocityLow + (r.velocityHigh - r.velocityLow) * random.nextFloat()) * 100.0f) / 100.0f;
                b.slide = random.nextFloat() < r.slideChance;
                b.ratchet = random.nextFloat() < r.ratchetChance ? 2 + random.nextInt (3) : 1;
                // A long block covers the steps after it: they rest.
                const auto steps = b.on ? juce::jlimit (1, half - k, b.length) : 1;
                b.length = steps;
                for (int j = 1; j < steps; ++j)
                    p.blocks[(size_t) (k + j)].on = false;
                k += steps;
            }
            if (r.symmetry)
                for (int k = 0; k < length / 2; ++k)
                    p.blocks[(size_t) (length - 1 - k)] = p.blocks[(size_t) k].length == 1 ? p.blocks[(size_t) k] : Block { false };
            for (int k = length; k < maxSteps; ++k)
                p.blocks[(size_t) k].on = false;
            return p;
        }
    }

    struct ArpSettings
    {
        bool on = false;
        arp::Mode mode = arp::Mode::up;
        int division = 7;                    // 1/16 (the LFOs' list)
        seq::Timing timing = seq::Timing::straight;
        int octaves = 1;                     // 1 to 4
        float gate = 0.8f;                   // 0.05 to 1: how much of its length a block sounds
        int length = 16;                     // steps, 2 to 32
        arp::VelocityMode velocityMode = arp::VelocityMode::eachNote;
        int lowKey = 0, highKey = 127;       // the keys it takes (keys outside them don't play its oscillators at all)
        float humanize = 0.0f;               // 0 to 1: each block late by up to a quarter of a step, at random
        float strum = 0.0f;                  // 0 to 1: POLY's notes one after another, up to half a step in all
        int pattern = 0;                     // A to D: the one played (or the first, with CHAIN)
        bool chain = false;                  // play A to D (those with blocks) one after another
        arp::Trigger trigger = arp::Trigger::song;
        arp::NoteSource noteSource = arp::NoteSource::keys;   // 0.16.0: FIXED NOTE
        const arp::Patterns* patterns = nullptr;
        const arp::Randomizer* randomizer = nullptr;          // 0.16.0: AUTODICE rolls from it

        double beatsPerStep() const noexcept
        {
            auto beats = seq::divisionBeats[(size_t) juce::jlimit (0, (int) seq::divisionBeats.size() - 1, division)];
            if (timing == seq::Timing::triplet)
                beats *= 2.0 / 3.0;
            else if (timing == seq::Timing::dotted)
                beats *= 1.5;
            return beats;
        }
        bool takes (int note) const noexcept { return note >= lowKey && note <= highKey; }
    };

    namespace arp
    {
        // The block a step plays (CHAIN: the patterns with blocks in turn), from `live` if given (AUTODICE's).
        inline const Block& blockFor (const ArpSettings& s, const Patterns& patterns, std::int64_t step, const Pattern* live = nullptr) noexcept
        {
            const auto length = juce::jlimit (2, maxSteps, s.length);
            if (live != nullptr)
                return live->blocks[(size_t) (step % length)];
            auto pattern = juce::jlimit (0, numPatterns - 1, s.pattern);
            if (s.chain)
            {
                std::array<int, numPatterns> active {};
                auto count = 0;
                for (int p = 0; p < numPatterns; ++p)
                    if (! patterns.patterns[(size_t) p].isEmpty())
                        active[(size_t) count++] = p;
                if (count > 0)
                    pattern = active[(size_t) ((step / length) % count)];
            }
            return patterns.patterns[(size_t) pattern].blocks[(size_t) (step % length)];
        }

        // What an arp knows of the keys while it plays.
        struct KeysState
        {
            const Held* held = nullptr;
            int numHeld = 0;
            int* noteCounter = nullptr;   // its place in the notes (moves on)
            float firstVelocity = 1.0f, lastVelocity = 1.0f;
        };

        // A note a block plays, in samples from the block's start.
        struct Played
        {
            double start = 0.0, end = 0.0;
            int note = 60;
            float velocity = 1.0f;
            bool slide = false;   // glides on into the next block (its end is a safety, just after the block)
        };
        inline constexpr int maxPlayed = 256;

        // The notes `block` plays (what the engine schedules and MIDI export writes): how many.
        inline int playBlock (const ArpSettings& s, const Block& block, double samplesPerStep, KeysState& keys,
                              juce::Random& random, Played* out, int maxOut) noexcept
        {
            if (! block.on || keys.numHeld <= 0)
                return 0;

            // FIXED NOTE: a held chord's root alone (in the lowest key's octave), at the keys' last velocity.
            auto held = keys.held;
            auto numHeld = keys.numHeld;
            Held root;
            DetectedChord chord;
            if (s.noteSource != NoteSource::keys)
            {
                chord = detectChord (keys.held, keys.numHeld);
                root = { chord.root, keys.lastVelocity, 0 };
                held = &root;
                numHeld = 1;
            }
            const auto transpose = s.noteSource == NoteSource::fixedNotePlus ? plusInterval (block.transpose, chord.major) : block.transpose;

            std::array<int, maxHeld * 4> notes {};
            const auto count = sequence (s.mode, held, numHeld, s.octaves, notes.data(), (int) notes.size());
            if (count == 0)
                return 0;

            // This block's notes: POLY all of them, CHORDS the block's chord on the next note, RANDOM one at random,
            // the others the next in turn.
            std::array<int, maxHeld * 4> playing {}, fromKey {};   // each note, and the key's note it came from (for its velocity)
            auto voicesNow = 1;
            if (s.mode == Mode::poly)
            {
                voicesNow = count;
                for (int i = 0; i < count; ++i)
                {
                    playing[(size_t) i] = notes[(size_t) i] + transpose;
                    fromKey[(size_t) i] = notes[(size_t) i];
                }
            }
            else
            {
                const auto from = s.mode == Mode::random ? notes[(size_t) random.nextInt (count)]
                                                         : notes[(size_t) ((*keys.noteCounter)++ % count)];
                fromKey.fill (from);
                if (s.mode == Mode::chords)
                {
                    voicesNow = 0;
                    if (block.sub)
                        playing[(size_t) voicesNow++] = from + transpose + juce::jlimit (-24, 0, block.subTranspose);
                    const auto& shape = chordShape (block.chord);
                    for (int t = 0; t < shape.count; ++t)
                        if ((block.chordMask >> t) & 1)
                            playing[(size_t) voicesNow++] = from + transpose + shape.intervals[(size_t) t];
                    if (voicesNow == 0)
                        return 0;
                }
                else
                    playing[0] = from + transpose;
            }

            // VELOCITY: the note's own, the first key's, the last key's or the lowest key's; times the block's.
            const auto velocityOf = [&] (int note)
            {
                auto v = keys.lastVelocity;
                switch (s.velocityMode)
                {
                    case VelocityMode::firstNote: v = keys.firstVelocity; break;
                    case VelocityMode::lastNote: v = keys.lastVelocity; break;
                    case VelocityMode::lowestNote:
                        v = std::min_element (keys.held, keys.held + keys.numHeld, [] (const Held& x, const Held& y) { return x.note < y.note; })->velocity;
                        break;
                    case VelocityMode::eachNote:
                        for (int i = 0; i < keys.numHeld; ++i)
                            if (((keys.held[i].note - note) % 12 + 12) % 12 == 0)   // the key it came from (an octave up or not)
                                v = keys.held[i].velocity;
                        break;
                }
                return juce::jlimit (0.01f, 1.0f, v * juce::jlimit (0.0f, 1.0f, block.velocity));
            };

            const auto blockLength = juce::jlimit (1, maxSteps, block.length) * samplesPerStep;
            const auto ratchets = juce::jlimit (1, maxRatchet, block.ratchet);
            const auto part = blockLength / ratchets;
            const auto gate = juce::jlimit (0.05, 1.0, (double) s.gate);
            const auto late = s.humanize > 0.0f ? (double) s.humanize * 0.25 * samplesPerStep * random.nextFloat() : 0.0;
            const auto slides = block.slide && voicesNow == 1;
            auto written = 0;
            for (int r = 0; r < ratchets; ++r)
                for (int i = 0; i < voicesNow && written < maxOut; ++i)
                {
                    const auto strum = voicesNow > 1 ? (double) s.strum * 0.5 * samplesPerStep * i / (voicesNow - 1) : 0.0;
                    const auto start = late + strum + r * part;
                    const auto glidesOn = slides && r == ratchets - 1;
                    const auto note = juce::jlimit (0, 127, playing[(size_t) i]);
                    // Its end: GATE of its share; a gliding note's just after its block (the next block's note,
                    // if it starts right there, takes the voice over).
                    const auto end = glidesOn ? late + blockLength + 1.0 : start + juce::jmax (1.0, part * gate);
                    out[written++] = { start, end, note, velocityOf (fromKey[(size_t) i]), glidesOn };
                }
            return written;
        }
    }
}
