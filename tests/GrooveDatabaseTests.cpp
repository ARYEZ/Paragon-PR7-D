// GROOVE DATABASES (PR7-D 0.15.0): grooves kept inside a database file ("midiDB") listed, read, previewed and dragged
// like MIDI files. tests/data/midiDB is made by tests/data/make_groove_db.py: laid out like a real one, its grooves
// invented there (see that script for what each holds).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"
#include "grooves/GrooveDatabase.h"
#include "grooves/SqliteReader.h"
#include "ui/GroovesPage.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    juce::File fixture() { return juce::File (PARAGON_TEST_DATA).getChildFile ("midiDB"); }

    struct Folder
    {
        juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("pr7d-groovedb-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        juce::File packs = root.getChildFile ("Packs");
        juce::File database = packs.getChildFile ("Demo").getChildFile ("midiDB");
        Folder()
        {
            database.getParentDirectory().createDirectory();
            fixture().copyFileTo (database);
        }
        ~Folder() { root.deleteRecursively(); }
        // A groove's path inside the database.
        juce::File groove (const juce::String& inside) const { return database.getChildFile ("Demo Grooves").getChildFile (inside); }
    };

    struct Plugin
    {
        juce::File presets = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("pr7d-groovedb-presets-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        ParagonX1AudioProcessor p;
        Plugin()
        {
            p.useRigFolder (presets);
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
        }
        ~Plugin() { presets.deleteRecursively(); }
    };

    const auto& grooveLibraryMap()
    {
        static const auto map = synth::drums::notemap::grooveLibrary();
        return map;
    }
}

TEST_CASE ("GROOVE DATABASE: the file reader reads every table, across interior and overflow pages", "[groove-db]")
{
    REQUIRE (fixture().existsAsFile());
    grooves::sqlite::Database d;
    REQUIRE (d.open (fixture()));
    const auto columns = d.columnsOf ("MIDIENTITY");
    CHECK (columns.size() == 17);
    CHECK (columns[0] == "ID");
    CHECK (columns[11] == "Length");
    CHECK (d.columnsOf ("NO_SUCH_TABLE").isEmpty());

    // ID is the table's INTEGER PRIMARY KEY (set by a constraint at the end): SQLite keeps it as the rowid.
    std::vector<juce::int64> ids;
    juce::StringArray names;
    CHECK (d.forEachRow ("MIDIENTITY", [&] (juce::int64 rowid, const std::vector<grooves::sqlite::Value>& row)
    {
        CHECK (row[0].asInt() == rowid);
        ids.push_back (row[0].asInt());
        names.add (row[1].text);
    }));
    REQUIRE (ids.size() == 43);
    for (size_t i = 0; i < ids.size(); ++i)
        CHECK (ids[i] == (juce::int64) i + 1);
    CHECK (names[0] == "Grooves 01");
    CHECK (names[40] == "Fills 01");

    // The long groove's notes (2880 bytes) run over several 512-byte overflow pages.
    std::map<juce::int64, size_t> sizes;
    d.forEachRow ("MIDI_INFO", [&] (juce::int64, const std::vector<grooves::sqlite::Value>& row) { sizes[row[0].asInt()] = row[1].blob.getSize(); });
    CHECK (sizes.size() == 43);
    CHECK (sizes[42] == 576 * 5);
    CHECK (sizes[1] == 26 * 5);
    CHECK_FALSE (d.forEachRow ("NO_SUCH_TABLE", [] (juce::int64, const std::vector<grooves::sqlite::Value>&) {}));
}

TEST_CASE ("GROOVE DATABASE: a hit's velocity from the accent marks", "[groove-db]")
{
    using grooves::db::velocityFor;
    const auto strong = 112.0f / 127.0f, soft = 80.0f / 127.0f;
    CHECK (velocityFor (0.0, 4.0, 0x01010101, 0x01010101) == strong);   // step 0
    CHECK (velocityFor (0.5, 4.0, 0x01010101, 0x01010101) == soft);     // step 4
    CHECK (velocityFor (1.0, 4.0, 0x01010101, 0x01010101) == strong);   // step 8
    CHECK (velocityFor (4.5, 4.0, 0x00000001, 0x00000010) == strong);   // bar 2: the second mark, step 4
    CHECK (velocityFor (8.5, 4.0, 0x00000001, 0x00000010) == soft);     // bar 3: the first again
    CHECK (velocityFor (4.5, 4.0, 0x00000010, 0) == strong);            // no second mark: the first for every bar
    CHECK (velocityFor (3.999, 4.0, 0x00000001, 0) == strong);          // a hair before the bar line: the next bar's step 0
    CHECK (velocityFor (2.0, 4.0, 0, 0) == 100.0f / 127.0f);            // no marks: one level
    CHECK (velocityFor (1.0, 3.0, 0x00000800, 0) == strong);            // 3/4: beat 2 is step 11 of 32 (10.67, rounded)
}

TEST_CASE ("GROOVE DATABASE: its grooves list in GROOVES, in its folders", "[groove-db]")
{
    Folder f;
    Plugin t;
    auto& library = t.p.getGrooveLibrary();
    REQUIRE (library.addFolder (f.packs));
    library.scanNow();
    const auto entries = library.getEntries();
    REQUIRE (entries.size() == 43);

    const auto find = [&entries] (const juce::File& file) -> const grooves::Entry*
    {
        for (const auto& e : entries)
            if (e.file == file)
                return &e;
        return nullptr;
    };
    // The pack's name, its folders with their codes off, the song's name before the last folder.
    const auto* first = find (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves/Grooves 01.mid"));
    REQUIRE (first != nullptr);
    CHECK (first->name == "Grooves 01");
    CHECK (first->family == "Grooves");
    CHECK (first->bpm == 100.0);
    CHECK (first->bpmKnown);
    CHECK (first->bars == 2);
    CHECK (first->signature() == "4/4");
    CHECK (first->hand == grooves::PowerHand::closedHat);
    CHECK (first->part == grooves::Part::beat);

    const auto* fill = find (f.groove ("Straight 4-4/Rock Song 100 BPM/Fills/Fills 01.mid"));
    REQUIRE (fill != nullptr);
    CHECK (fill->part == grooves::Part::fill);
    CHECK (fill->bars == 1);   // the closing crash on the next bar line ends it

    const auto* waltz = find (f.groove ("Waltz 3-4/Waltz 90 BPM/Grooves/Grooves 01.mid"));
    REQUIRE (waltz != nullptr);
    CHECK (waltz->signature() == "3/4");
    CHECK (waltz->bpm == 90.0);
    CHECK (waltz->bars == 2);

    // A second scan takes them from the index (the database unchanged, so not even opened): the same list, none read.
    library.scanNow();
    CHECK (library.getEntries().size() == 43);
    CHECK (library.getNumScanned() == 0);
    CHECK (library.query (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves"), "", false, grooves::Sort::name, 120.0, 4, 4).size() >= 41);
}

TEST_CASE ("GROOVE DATABASE: a groove reads with its notes, tempo, length and velocities", "[groove-db]")
{
    Folder f;
    const auto g = grooves::read (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves/Grooves 01.mid"), grooveLibraryMap());
    REQUIRE (g.has_value());
    CHECK (g->name == "Grooves 01");
    CHECK (g->bpm == 100.0);
    CHECK (g->bars == 2);
    REQUIRE (g->hits.size() == 26);   // per bar: 2 kicks + the extra one, 2 snares, 8 hats
    CHECK (g->hits.front().beat == 0.0);
    for (const auto& h : g->hits)
    {
        INFO ("beat " << h.beat << " note " << h.note);
        const auto step = (int) std::lround (std::fmod (h.beat, 4.0) * 8.0);
        CHECK_THAT (h.velocity, WithinAbs (step % 8 == 0 ? 112.0 / 127.0 : 80.0 / 127.0, 0.005));
    }
    const auto kicks = std::count_if (g->hits.begin(), g->hits.end(), [] (const grooves::Hit& h) { return h.note == 36; });
    CHECK (kicks == 6);

    const auto even = grooves::read (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves/Grooves 02.mid"), grooveLibraryMap());
    REQUIRE (even.has_value());
    CHECK (even->hits.size() == 24);

    const auto longOne = grooves::read (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves/Long Groove.mid"), grooveLibraryMap());
    REQUIRE (longOne.has_value());
    CHECK (longOne->hits.size() == 576);
    CHECK (longOne->bars == 64);
    CHECK_THAT (longOne->hits.back().beat, WithinAbs (255.5, 1.0e-6));
    CHECK_THAT (longOne->hits.front().velocity, WithinAbs (100.0 / 127.0, 0.005));   // no accent marks: one level

    CHECK_FALSE (grooves::read (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves/No Such Groove.mid"), grooveLibraryMap()).has_value());
}

TEST_CASE ("GROOVE DATABASE: the folder tree shows the folders inside it", "[groove-db]")
{
    Folder f;
    using grooves::db::foldersIn;
    CHECK (foldersIn (f.packs).empty());   // the database is a level down
    CHECK (foldersIn (f.database.getParentDirectory()) == std::vector<juce::File> { f.database.getChildFile ("Demo Grooves") });
    CHECK (foldersIn (f.database.getChildFile ("Demo Grooves"))
           == std::vector<juce::File> { f.groove ("Straight 4-4"), f.groove ("Waltz 3-4") });
    CHECK (foldersIn (f.groove ("Straight 4-4/Rock Song 100 BPM"))
           == std::vector<juce::File> { f.groove ("Straight 4-4/Rock Song 100 BPM/Fills"), f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves") });
    CHECK (foldersIn (f.groove ("Straight 4-4/Rock Song 100 BPM/Fills")).empty());
}

TEST_CASE ("GROOVE DATABASE: a groove previews through the kit and drags out as a MIDI file", "[groove-db]")
{
    Folder f;
    Plugin t;
    REQUIRE (t.p.getGrooveLibrary().addFolder (f.packs));
    const auto g = grooves::read (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves/Grooves 01.mid"), grooveLibraryMap());
    REQUIRE (g.has_value());

    t.p.playGroove (*g, 0, false);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer none;
    t.p.processBlock (buffer, none);
    CHECK (t.p.getDrumKit().isSounding (synth::drums::notemap::BD));   // the kick and the hat on beat 1
    CHECK (t.p.getDrumKit().isSounding (synth::drums::notemap::CH));
    CHECK_FALSE (t.p.getDrumKit().isSounding (synth::drums::notemap::SD));

    const auto dragged = t.p.grooveDragFile (*g, 0);
    REQUIRE (dragged.existsAsFile());   // a real file, for the DAW
    juce::FileInputStream in (dragged);
    juce::MidiFile midi;
    REQUIRE (midi.readFrom (in));
    auto notes = 0;
    for (int track = 0; track < midi.getNumTracks(); ++track)
        for (const auto* e : *midi.getTrack (track))
            notes += e->message.isNoteOn() ? 1 : 0;
    CHECK (notes == 26);
}

TEST_CASE ("GROOVE DATABASE: a file that isn't one, or a damaged one, lists nothing and crashes nothing", "[groove-db]")
{
    Folder f;
    Plugin t;
    auto& library = t.p.getGrooveLibrary();

    // Not SQLite: ignored.
    f.database.replaceWithText ("not a database");
    CHECK_FALSE (grooves::db::isDatabase (f.database));
    REQUIRE (library.addFolder (f.packs));
    library.scanNow();
    CHECK (library.getEntries().empty());

    // Cut short, then scrambled past its first page: fewer grooves or none, never a crash.
    juce::MemoryBlock data;
    REQUIRE (fixture().loadFileAsData (data));
    for (const auto keep : { (size_t) 100, (size_t) 600, (size_t) 3000, (size_t) 12000 })
    {
        f.database.replaceWithData (data.getData(), keep);
        library.scanNow();
        CHECK (library.getEntries().size() < 43);
    }
    juce::Random random (7);
    for (int round = 0; round < 20; ++round)
    {
        auto scrambled = data;
        auto* bytes = static_cast<juce::uint8*> (scrambled.getData());
        for (int k = 0; k < 200; ++k)
            bytes[512 + (size_t) random.nextInt ((int) scrambled.getSize() - 512)] = (juce::uint8) random.nextInt (256);
        grooves::sqlite::Database d;
        if (d.open (scrambled))
            d.forEachRow ("MIDI_INFO", [] (juce::int64, const std::vector<grooves::sqlite::Value>&) {});
        f.database.replaceWithData (scrambled.getData(), scrambled.getSize());
        library.scanNow();
        CHECK (library.getEntries().size() <= 43);
    }
}

TEST_CASE ("GROOVE DATABASE: its own labels fill the filter columns; the packs' order sorts the quiet ones first", "[groove-db]")
{
    // 0.16.0: genre, play style, type and power hand as the database labels them (the owner's choice).
    Folder f;
    Plugin t;
    auto& library = t.p.getGrooveLibrary();
    REQUIRE (library.addFolder (f.packs));
    library.scanNow();
    const auto entries = library.getEntries();
    const auto entry = [&entries] (const juce::String& family, const juce::String& name) -> const grooves::Entry*
    {
        for (const auto& e : entries)
            if (e.family == family && e.name == name)
                return &e;
        return nullptr;
    };
    using grooves::Column;
    const auto* first = entry ("Grooves", "Grooves 01");
    const auto* fill = entry ("Fills", "Fills 01");
    REQUIRE (first != nullptr);
    REQUIRE (fill != nullptr);
    CHECK (grooves::valuesOf (*first, Column::genre) == juce::StringArray { "Rock" });
    CHECK (grooves::valuesOf (*first, Column::playStyle) == juce::StringArray { "Standard" });
    CHECK (grooves::valuesOf (*first, Column::type) == juce::StringArray { "Beat", "Straight", "Medium Hits" });
    CHECK (grooves::valuesOf (*first, Column::hand) == juce::StringArray { "Hi-Hat Closed" });
    CHECK (grooves::valuesOf (*fill, Column::type) == juce::StringArray { "Fill", "Straight", "Hard Hits" });
    CHECK (grooves::valuesOf (*fill, Column::hand) == juce::StringArray { "Snare" });   // the database's word, not ours

    // Kept with what was read: a new library lists them from the index, labels and all.
    {
        grooves::Library again (t.presets.getChildFile ("Grooves.xml"), t.presets.getChildFile ("Groove Index.xml"));
        again.loadIndex();
        bool found = false;
        for (const auto& e : again.getEntries())
            if (e.name == "Grooves 40")
            {
                found = true;
                CHECK (grooves::valuesOf (e, Column::playStyle) == juce::StringArray { "Double Kick" });
                CHECK (e.priority == 9);
            }
        CHECK (found);
    }

    // Filtered by a label; the pack's own order puts its esoteric ones (Grooves 39, 40) last.
    grooves::Filters doubleKick;
    doubleKick.include[(size_t) Column::playStyle] = { "Double Kick" };
    CHECK (library.query ({}, {}, false, {}, 120.0, 4, 4, doubleKick).size() == 2);
    grooves::SortOrder order;
    order.priority = true;
    const auto sorted = library.query (f.groove ("Straight 4-4/Rock Song 100 BPM/Grooves"), {}, false, order, 120.0, 4, 4);
    REQUIRE (sorted.size() == 41);
    CHECK (sorted[sorted.size() - 1].priority == 9);
    CHECK (sorted[sorted.size() - 2].priority == 9);
    CHECK (sorted[0].priority == 0);
    // Song tempo and time signature first, together: the waltz (3/4, 90) first in a 3/4 song at 90.
    grooves::SortOrder song;
    song.songTempo = song.songSignature = true;
    CHECK (library.query ({}, {}, false, song, 90.0, 3, 4)[0].signature() == "3/4");

    // The browser's first column: the pack, under GROOVE PACKS.
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    const auto columns = page.getBrowserColumns();
    REQUIRE (! columns.empty());
    CHECK (columns[0][0].label == "GROOVE PACKS");
    CHECK (columns[0][1].label == "Demo Grooves");
    page.chooseFolder (f.groove ("Straight 4-4/Rock Song 100 BPM/Fills"));
    CHECK (page.getResults().size() == 1);
    CHECK (page.getBrowserColumns().size() == 3);   // the pack's styles, then the song's folders
}
