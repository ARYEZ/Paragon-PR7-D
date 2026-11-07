// THE GROOVE CATALOGUE (PR7-D 0.62.0): a listing of the grooves made once in the background and shared, so the GROOVES
// page doesn't copy 100,000 grooves on every click: the filter columns' values and each groove's as numbers, the
// folders inside groove databases (worked out from the grooves' paths, no database opened), the packs; paths checked by
// their text; the index written whole.
#include <catch2/catch_test_macros.hpp>

#include "grooves/GrooveLibrary.h"

namespace
{
    juce::MemoryBlock smallGroove (int note)
    {
        juce::MidiMessageSequence track;
        for (int e = 0; e < 8; ++e)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (e * 240));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (e * 240 + 60));
        }
        track.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100).withTimeStamp (0));
        track.addEvent (juce::MidiMessage::noteOff (10, 36).withTimeStamp (60));
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        file.addTrack (track);
        juce::MemoryOutputStream out;
        file.writeTo (out);
        return out.getMemoryBlock();
    }

    grooves::Entry entry (const juce::String& path, bool fromDatabase = false)
    {
        grooves::Entry e;
        e.file = juce::File (path);
        e.name = e.file.getFileNameWithoutExtension();
        e.family = e.file.getParentDirectory().getFileName();
        e.fromDatabase = fromDatabase;
        e.bpmKnown = true;
        e.bpm = 100.0;
        return e;
    }
}

TEST_CASE ("GROOVE CATALOGUE: a path inside a folder, by its text", "[grooves][catalogue]")
{
    using grooves::pathIsIn;
    using grooves::pathIsDirectlyIn;
    CHECK (pathIsIn ("/a/b/c.mid", "/a/b"));
    CHECK (pathIsIn ("/a/b/c.mid", "/a"));
    CHECK (pathIsIn ("/a/b", "/a/b"));
    CHECK_FALSE (pathIsIn ("/a/bc/d.mid", "/a/b"));   // not just the same start
    CHECK_FALSE (pathIsIn ("/a/b/c.mid", ""));
    CHECK (pathIsDirectlyIn ("/a/b/c.mid", "/a/b"));
    CHECK_FALSE (pathIsDirectlyIn ("/a/b/c/d.mid", "/a/b"));
    CHECK_FALSE (pathIsDirectlyIn ("/a/b", "/a/b"));
}

TEST_CASE ("GROOVE CATALOGUE: every column's values, each groove's as numbers into them", "[grooves][catalogue]")
{
    std::vector<grooves::Entry> list;
    auto a = entry ("/g/Rock/Verse 01.mid");
    a.tags = { "Genre:Rock", "Type:Beat", "Type:Straight" };
    auto b = entry ("/g/Jazz/Fill 02.mid");
    b.tags = { "Genre:Jazz", "Type:Fill", "Type:Swing" };
    b.bpm = 60.0;
    auto c = entry ("/g/Rock/Chorus 01.mid");
    c.bpmKnown = false;
    for (auto* e : { &a, &b, &c })
        list.push_back (*e);
    grooves::cacheEntries (list);
    const auto made = grooves::makeCatalogue (std::move (list));
    REQUIRE (made->entries.size() == 3);
    REQUIRE (made->paths.size() == 3);
    CHECK (made->paths[1] == "/g/Jazz/Fill 02.mid");
    for (int col = 0; col < grooves::numColumns; ++col)
    {
        INFO (grooves::columnNames[col]);
        // The column's values: all of them, once, in the column's order.
        juce::StringArray every;
        for (const auto& e : made->entries)
            for (const auto& v : grooves::valuesOf (e, (grooves::Column) col))
                every.addIfNotAlreadyThere (v);
        CHECK (made->values[(size_t) col] == grooves::sortedValues ((grooves::Column) col, every));
        // Each groove's numbers name exactly its values.
        for (size_t i = 0; i < made->entries.size(); ++i)
        {
            juce::StringArray named;
            made->forEachValueId (i, col, [&] (int id) { named.add (made->values[(size_t) col][id]); });
            CHECK (named == grooves::valuesOf (made->entries[i], (grooves::Column) col));
        }
    }
    CHECK (made->values[(size_t) grooves::Column::genre] == juce::StringArray { "Jazz", "Rock" });
    CHECK (made->values[(size_t) grooves::Column::tempo].contains ("Unknown"));
}

TEST_CASE ("GROOVE CATALOGUE: the folders inside groove databases, from the grooves' paths; the packs", "[grooves][catalogue]")
{
    std::vector<grooves::Entry> list;
    list.push_back (entry ("/lib/Packs/midiDB/Pack B/Straight/Song 1/Verse.mid", true));
    list.push_back (entry ("/lib/Packs/midiDB/Pack B/Straight/Song 2/Fill.mid", true));
    list.push_back (entry ("/lib/Packs/midiDB/Pack A/Waltz/Intro.mid", true));
    list.push_back (entry ("/lib/Files/Song/Verse.mid"));   // a plain file: no virtual folders for it
    grooves::cacheEntries (list);
    const auto made = grooves::makeCatalogue (std::move (list));
    const auto kids = [&] (const juce::String& folder)
    {
        juce::StringArray out;
        if (const auto f = made->databaseFolders.find (folder); f != made->databaseFolders.end())
            for (const auto& k : f->second)
                out.add (k.getFullPathName());
        return out;
    };
    // The packs: under the database, and under the folder the database file is in (where the browser shows them).
    CHECK (kids ("/lib/Packs/midiDB") == juce::StringArray { "/lib/Packs/midiDB/Pack A", "/lib/Packs/midiDB/Pack B" });
    CHECK (kids ("/lib/Packs") == juce::StringArray { "/lib/Packs/midiDB/Pack A", "/lib/Packs/midiDB/Pack B" });
    CHECK (kids ("/lib/Packs/midiDB/Pack B") == juce::StringArray { "/lib/Packs/midiDB/Pack B/Straight" });
    CHECK (kids ("/lib/Packs/midiDB/Pack B/Straight") == juce::StringArray { "/lib/Packs/midiDB/Pack B/Straight/Song 1",
                                                                             "/lib/Packs/midiDB/Pack B/Straight/Song 2" });
    CHECK (kids ("/lib/Files").isEmpty());
    REQUIRE (made->packs.size() == 2);
    CHECK (made->packs[0].getFileName() == "Pack A");
    CHECK (made->packs[1].getFileName() == "Pack B");

    // Let go of: freed (on a thread of its own when it's big and the last hold), the caller's pointer emptied.
    auto held = made;
    grooves::letGo (std::move (held));
    CHECK (held == nullptr);
    CHECK (made->entries.size() == 4);   // (still here: `made` holds it)
}

TEST_CASE ("GROOVE CATALOGUE: a library's listing, its index written whole (300 grooves), read back", "[grooves][catalogue]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonCatalogueTests");
    root.deleteRecursively();
    const auto pack = root.getChildFile ("Pack");
    const auto data = smallGroove (42);
    for (int s = 0; s < 10; ++s)
        for (int g = 0; g < 30; ++g)
        {
            const auto f = pack.getChildFile ("Song " + juce::String (s).paddedLeft ('0', 2)).getChildFile ("Groove " + juce::String (g).paddedLeft ('0', 2) + ".mid");
            f.getParentDirectory().createDirectory();
            REQUIRE (f.replaceWithData (data.getData(), data.getSize()));
        }
    const auto settings = root.getChildFile ("Grooves.xml"), index = root.getChildFile ("Groove Index.xml");
    {
        grooves::Library library (settings, index);
        REQUIRE (library.addFolder (pack));
        library.scanNow();
        const auto now = library.getCatalogue();
        CHECK (now->entries.size() == 300);
        CHECK (library.getNumEntries() == 300);
        CHECK (library.getEntries().size() == 300);
        // find: rows of the catalogue, the same as query's copies.
        const auto found = library.find (pack.getChildFile ("Song 03"), "groove", false, {}, 120.0, 4, 4);
        const auto copies = library.query (pack.getChildFile ("Song 03"), "groove", false, {}, 120.0, 4, 4);
        REQUIRE (found.rows.size() == 30);   // that song's, by name
        REQUIRE (copies.size() == 30);
        for (size_t k = 0; k < 30; ++k)
            CHECK (found.catalogue->entries[(size_t) found.rows[k]].file == copies[k].file);
        CHECK (copies.front().name == "Groove 00");
        CHECK (copies.back().name == "Groove 29");
        CHECK (library.find (pack.getChildFile ("Song 03"), "groove 2", false, {}, 120.0, 4, 4).rows.size() == 12);   // (02, 12, 20-29)
        CHECK (found.match.empty());
    }
    // The index: every groove once.
    const auto xml = juce::XmlDocument::parse (index);
    REQUIRE (xml != nullptr);
    juce::StringArray paths;
    for (const auto* x : xml->getChildWithTagNameIterator ("G"))
        paths.add (x->getStringAttribute ("path"));
    CHECK (paths.size() == 300);
    auto once = paths;
    once.removeDuplicates (false);
    CHECK (once.size() == 300);
    // A new library (another PR7-D) lists them from it.
    {
        grooves::Library library (settings, index);
        library.loadIndex();
        CHECK (library.getNumEntries() == 300);
    }
    root.deleteRecursively();
}
