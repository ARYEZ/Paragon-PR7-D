#pragma once

#include <juce_core/juce_core.h>

#include "StepSequencer.h"

#include <algorithm>
#include <array>

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
// DOWN/UP, AS PLAYED and RANDOM are the usual others (our list). CHORDS,
// chord detection (FIXED NOTE, "+"), the note randomizer and MIDI export
// come in stage 5b.
namespace synth
{
    namespace arp
    {
        inline constexpr int numArps = 8, maxSteps = 32, numPatterns = 4, maxHeld = 32, maxRatchet = 8;

        // Saved by index (arp*Mode, arp*Velocity, arp*Trigger, osc*Arp): only ever append.
        enum class Mode { up, down, upDown, downUp, asPlayed, random, poly };
        inline const juce::StringArray modeNames { "Up", "Down", "Up/Down", "Down/Up", "As Played", "Random", "Poly" };
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

            bool operator== (const Block& o) const noexcept
            {
                return on == o.on && length == o.length && transpose == o.transpose && juce::exactlyEqual (velocity, o.velocity)
                    && slide == o.slide && ratchet == o.ratchet;
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
                    for (int i = 0; i < length; ++i) put (up[i]);
                    break;
            }
            return written;
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
        const arp::Patterns* patterns = nullptr;

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
}
