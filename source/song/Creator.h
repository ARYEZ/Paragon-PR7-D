// THE SONG CREATOR (PR7-D 0.10.0, the song track part 3): a whole drum track from one groove. Our own design, inspired by
// the song creator in chapter 7 of the manual the owner sent: the grooves of the source's song folder in columns by song
// part (its "variations": that software finds them in its makers' own database, which we can't read; ours come from the
// player's linked folders), and SONG STRUCTURES (sections of song parts and bars) that pick from those columns.
#pragma once

#include "song/Song.h"
#include "grooves/GrooveLibrary.h"

namespace song
{
    inline constexpr int numSongParts = 9;   // grooves::partNames' size
    static_assert (grooves::Part::countIn == (grooves::Part) (numSongParts - 1));

    // A section of a structure: a song part, so many bars.
    struct Section
    {
        grooves::Part part = grooves::Part::verse;
        int bars = 8;
        bool operator== (const Section& o) const noexcept { return part == o.part && bars == o.bars; }
    };

    struct Structure
    {
        juce::String name;
        std::vector<Section> sections;
        bool user = false;   // the player's own (saved, renamed, removed); else one of ours
        int totalBars() const noexcept;
    };

    // As text: "Intro 4, Verse 8, Fill 1" (the song parts' names, any case); unknown words left out.
    juce::String encode (const std::vector<Section>&);
    std::vector<Section> decode (const juce::String&);

    // Our structures (common song forms: A the verse, B the chorus, C the bridge, fills before the choruses).
    const std::vector<Structure>& factoryStructures();

    // A song's structure: each block's song part and its whole bars (at least one), in order (gaps left out).
    std::vector<Section> structureOf (const Song&);

    // THE COLUMNS: a groove library's grooves by song part.
    using Columns = std::array<std::vector<grooves::Entry>, numSongParts>;
    // The grooves around `source`: its folder's, or (when that has only one kind of part) its parent's, and so on up to
    // (not past) `root`, the linked folder; by song part, each column by name, the source first in its own.
    Columns variationsOf (const juce::File& source, const std::vector<grooves::Entry>& all, const juce::File& root);

    // Which column a section of `part` takes when its own is empty (the nearest kind), -1 if none will do: a missing
    // FILL (or COUNT-IN) isn't played by another part's groove: the groove before it plays on.
    int standInFor (grooves::Part part, const std::array<int, numSongParts>& counts) noexcept;
    struct Pick { int part = -1; int index = 0; };   // a column (-1: none) and a groove in it
    // Each section's groove: its own part's column (or the stand-in's), that column's next groove each time it's used,
    // round and round.
    std::vector<Pick> pick (const std::vector<Section>&, const std::array<int, numSongParts>& counts);

    // REPLACE: `target` plays `with`'s groove instead (its place, length, song part and MUTE kept).
    void replaceGroove (Block& target, const Block& with);

    // THE PLAYER's STRUCTURES, in a file of their own ("Song Structures.xml" in the presets folder).
    std::vector<Structure> loadStructures (const juce::File&);
    bool saveStructures (const juce::File&, const std::vector<Structure>&);
}
