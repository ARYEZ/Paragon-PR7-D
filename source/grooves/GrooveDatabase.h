#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

// GROOVE DATABASES (0.15.0): some drum software installs its MIDI grooves not as .mid files but inside one database
// file per pack (an SQLite file named "midiDB"). This reads those from the user's own computer when a linked folder
// holds one, so their grooves list in GROOVES like any MIDI file. Nothing of them is shipped or copied into presets.
//
// Each groove gets a path as if it were a file inside the database: the database file, the pack's name, its folders
// (the database's own, with their sorting codes taken off, and the song's name where the folders don't say it), then
// "<groove name>.mid": e.g. ".../midiDB/Heavy Rock/Straight 4-4/Ordinary Man 72 BPM/Fills/Fills 01.mid". grooves::read
// reads such a path from the database, so everything that works on a groove file (preview, filters, tap to find,
// groove parts, the song track, dragging out) works on these.
//
// What's in such a database, as far as we can tell (the format isn't published: this is our reading of the files the
// owner sent): each groove's notes as (beat, note) pairs, its tempo, length in beats and time signature, but no
// velocities. Two numbers per groove mark the strong hits of its first two bars on a grid of 32 steps a bar: a hit on a
// marked step gets velocity 112, the others 80 (those two bars' marks repeat through longer grooves); a groove with no
// marks plays at 100. That's our guess at how loud each hit is, not the pack's own velocities.
namespace grooves::db
{
    // Whether a file is a groove database: named "midiDB" (any case, or ending in ".mididb") with an SQLite file inside.
    bool isDatabase (const juce::File&);

    struct Contents
    {
        juce::String pack;                       // the pack's name ("Heavy Rock Grooves")
        struct Item
        {
            juce::File file;                     // its path inside the database (see above)
            juce::MemoryBlock midi;              // a standard MIDI file of it: tempo, time signature, notes, length
            // 0.16.0: what the database says of it, each "Group:Value": its genre ("Genre:..."), its tags by their
            // group ("Play Style:...", "Type:..."), its power hand ("Power Hand:..."); and its priority (lower first).
            juce::StringArray tags;
            int priority = 0;
        };
        std::vector<Item> items;
        std::vector<juce::File> folders;         // every folder inside the database, the pack's own first

        const Item* find (const juce::File&) const;
    };

    // A database's grooves, read once and kept while the file stays the same (size and date). Empty if unreadable.
    std::shared_ptr<const Contents> load (const juce::File& database);

    // The groove at a path inside a database (any database on the way up from it), or nothing.
    const Contents::Item* itemFor (const juce::File& path, std::shared_ptr<const Contents>& keepAlive);

    // The folders inside databases directly in `folder` (a real folder: each database's pack), or directly in a folder
    // inside a database: what the folder tree shows under it.
    std::vector<juce::File> foldersIn (const juce::File& folder);

    // What the library looks for in linked folders (with *.mid and *.midi).
    inline constexpr const char* wildcard = "*mididb;*midiDB;*MIDIDB;*MidiDB";

    // The velocity (0 to 1) of a hit `beat` quarter notes into a groove, from its accent marks (see above).
    float velocityFor (double beat, double beatsPerBar, juce::uint32 accentBar0, juce::uint32 accentBar1);
}
