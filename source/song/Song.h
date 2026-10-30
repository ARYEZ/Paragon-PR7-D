// THE SONG TRACK (PR7-D 0.8.0, plan stage d, the song track part 1): grooves laid out as a song. Our own design,
// inspired by the song track of commercial drum software (chapter 7 of the manual the owner sent): blocks, each a
// groove copied in (its notes already as the kit's drums), placed bar by bar, played with the DAW's song position.
//
// Placing (our rule): a block dropped inside another goes before or after it (the nearer edge), and the blocks after it
// move along to make room, so nothing overlaps and nothing is cut; dropped past the end it stays where it's dropped (a
// gap before it). Lengthening a block pushes the ones after it; shortening leaves a gap.
#pragma once

#include "grooves/Groove.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace song
{
    struct Hit
    {
        double beat = 0.0;   // quarter notes (in a block: from its start; in a song's events: from the song's)
        int slot = 0;        // the kit's drum
        float velocity = 1.0f;
        bool operator== (const Hit& o) const noexcept
        {
            return juce::exactlyEqual (beat, o.beat) && slot == o.slot && juce::exactlyEqual (velocity, o.velocity);
        }
    };

    struct Block
    {
        juce::String name;
        juce::File source;                       // the groove it came from (for FIND SIMILAR and the like)
        grooves::Part part = grooves::Part::beat;   // SONG PART: its colour and kind
        double start = 0.0;                      // beats from the song's start
        double length = 4.0;                     // beats on the track
        double loopLength = 4.0;                 // the groove's own length: played round and round to fill `length`
        std::vector<Hit> hits;                   // within [0, loopLength), in time order
        bool muted = false;

        double end() const noexcept { return start + length; }
        bool operator== (const Block& o) const noexcept
        {
            return name == o.name && source == o.source && part == o.part && juce::exactlyEqual (start, o.start)
                && juce::exactlyEqual (length, o.length) && juce::exactlyEqual (loopLength, o.loopLength) && hits == o.hits
                && muted == o.muted;
        }
    };

    struct Song
    {
        int numerator = 4, denominator = 4;
        std::vector<Block> blocks;               // in time order, never overlapping

        double beatsPerBar() const noexcept { return numerator * 4.0 / juce::jmax (1, denominator); }
        double endBeat() const noexcept { return blocks.empty() ? 0.0 : blocks.back().end(); }
        bool isEmpty() const noexcept { return blocks.empty(); }
        bool operator== (const Song& o) const noexcept
        {
            return numerator == o.numerator && denominator == o.denominator && blocks == o.blocks;
        }

        // EDITING (each keeps the blocks in order and apart, as above). Beats are snapped to whole bars.
        int place (Block, double atBeat);                 // returns where it went (its index)
        int move (int index, double toBeat);              // returns where it went
        void resize (int index, double newLength);        // at least a bar
        int duplicate (int index);                        // a copy right after it
        void remove (int index);
        int blockAt (double beat) const noexcept;         // -1: none there
        double snap (double beat) const noexcept;         // to the nearest bar line, not before 0

        // SPLIT (0.9.0): the block cut in two at the bar line nearest `atBeat` (inside it), each half its own notes as
        // played there (no longer repeating); returns the second half's index (-1: no bar line inside it).
        int split (int index, double atBeat);
        // MERGE WITH NEXT (0.9.0): the block and the one after it as one (named and coloured as the first; a gap between
        // them kept as rests); returns its index (-1: no block after it).
        int mergeWithNext (int index);

        // Every hit the song plays (muted blocks left out), in time order, each block's groove repeated to fill it.
        std::vector<Hit> events() const;
        // The whole song as a MIDI file: tempo `bpm`, its time signature, each drum on `noteFor`'s note (below 0: left out).
        juce::MidiFile toMidi (double bpm, const std::function<int (int slot)>& noteFor) const;

        std::unique_ptr<juce::XmlElement> toXml() const;
        static Song fromXml (const juce::XmlElement&);

    private:
        void settle (int keepFirst = -1);   // in order, then each block after the one before it
    };

    // A block from a groove: its notes through `layout` as drums (those no drum plays left out), at `factor` (2x: the
    // notes twice as close) and each velocity times `velocityScale`; named after the file; its SONG PART from the names.
    Block blockFrom (const grooves::Groove&, const synth::drums::notemap::Map& layout, grooves::TempoFactor factor = grooves::TempoFactor::normal,
                     float velocityScale = 1.0f);
    // CHANGE TEMPO on a block: its notes and lengths times 1 / factor (2x: twice as fast, half as long).
    void changeTempo (Block&, grooves::TempoFactor);

    // EDITING INSIDE A BLOCK (0.9.0, the song track part 2). Our own design, inspired by the block editing in chapter 7
    // of the manual the owner sent. Each works on the block's groove (one time round: `loopLength`), on the drums in
    // `slots` (bit n: the kit's drum n; allSlots: every drum), and keeps its hits in time order.
    inline constexpr unsigned allSlots = (1u << synth::drums::numSlots) - 1u;
    unsigned slotsOf (unsigned parts) noexcept;                  // grooves::DrumPart bits -> the drums in them
    unsigned slotsIn (const Block&) noexcept;                    // the drums the block plays
    std::vector<Hit> unrolled (const Block&);                    // every hit over its whole length (repeats written out)
    void flatten (Block&);                                       // the repeats written out: loopLength = length
    void removeDrums (Block&, unsigned slots);
    // The clip's notes on those drums in place of the block's own (the clip repeated, or cut, to the block's groove).
    void pasteDrums (Block&, const Block& clip, unsigned slots);
    // Each note to the nearest `grid` beats (a note pushed to the groove's end goes round to its start); two on the same
    // drum and beat become one (the louder).
    void quantize (Block&, double grid, unsigned slots = allSlots);
    // Each velocity times `factor` (the balance between them kept), between 1/127 and full.
    void scaleVelocity (Block&, float factor, unsigned slots = allSlots);
    // Every note on drum `from` played by drum `to` instead, with `to`'s own (on the same beat: one, the louder).
    void moveDrum (Block&, int from, int to);
    void addHit (Block&, Hit);                                   // one more (replacing one on that drum and beat)
    bool removeHitNear (Block&, int slot, double beat, double within);   // the nearest within that many beats

    // SONG PART colours (our own palette, a colour each kind).
    juce::Colour colourOf (grooves::Part) noexcept;
}
