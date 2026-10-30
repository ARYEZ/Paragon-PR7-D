#include "grooves/GrooveDatabase.h"

#include "grooves/SqliteReader.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>
#include <mutex>
#include <set>

namespace grooves::db
{
    namespace
    {
        constexpr int ticksPerBeat = 480;

        // A database's folder or file name as people read it: its sorting code off the front ("405@STRAIGHT_4#4" ->
        // "Straight 4-4", "072-S261@GROOVES" -> "Grooves", "01@Alien MainBeat 1" -> "Alien MainBeat 1"), capitals
        // only where a word starts (unless it holds a digit, or is BPM).
        juce::String cleanName (juce::String s)
        {
            if (s.containsChar ('@') && s.upToFirstOccurrenceOf ("@", false, false).containsOnly ("0123456789-Ss"))
                s = s.fromFirstOccurrenceOf ("@", false, false);
            s = s.replaceCharacter ('_', ' ').replaceCharacter ('#', '-').trim();
            if (s.isNotEmpty() && s == s.toUpperCase())
            {
                auto words = juce::StringArray::fromTokens (s, " ", "");
                for (auto& w : words)
                    if (! w.containsAnyOf ("0123456789") && w != "BPM")
                        w = w.substring (0, 1) + w.substring (1).toLowerCase();
                s = words.joinIntoString (" ");
            }
            return juce::File::createLegalFileName (s);
        }

        // Column positions by name (the format isn't ours: look them up rather than count them).
        struct Columns
        {
            juce::StringArray names;
            int operator() (const char* name) const { return names.indexOf (name, true); }
        };

        const sqlite::Value& at (const std::vector<sqlite::Value>& row, int column)
        {
            static const sqlite::Value none;
            return column >= 0 && column < (int) row.size() ? row[(size_t) column] : none;
        }

        // A table of id -> text (the database keeps paths, names and time signatures this way).
        std::map<juce::int64, juce::String> names (const sqlite::Database& d, const char* table, const char* column)
        {
            std::map<juce::int64, juce::String> out;
            const Columns c { d.columnsOf (table) };
            const auto id = c ("ID"), text = c (column);
            d.forEachRow (table, [&] (juce::int64, const std::vector<sqlite::Value>& row) { out[at (row, id).asInt()] = at (row, text).text; });
            return out;
        }

        struct Cached
        {
            juce::int64 size = -1, date = 0;
            std::shared_ptr<const Contents> contents;
        };
        std::mutex cacheLock;
        std::map<juce::String, Cached> cache;

        std::shared_ptr<const Contents> read (const juce::File& file)
        {
            auto contents = std::make_shared<Contents>();
            sqlite::Database d;
            if (! d.open (file))
                return contents;

            const auto paths = names (d, "PATHS", "Path");
            const auto fileNames = names (d, "FILENAMES", "Name");
            const auto headers = names (d, "HEADER", "Name");
            const auto signatures = names (d, "TIMESIGS", "Name");
            std::map<juce::int64, std::pair<juce::String, juce::String>> libraries;   // id -> its name, its shown name
            {
                const Columns c { d.columnsOf ("LIBRARY") };
                d.forEachRow ("LIBRARY", [&] (juce::int64, const std::vector<sqlite::Value>& row)
                {
                    libraries[at (row, c ("ID")).asInt()] = { at (row, c ("Name")).text, at (row, c ("FormattedName")).text };
                });
            }
            std::map<juce::int64, std::pair<juce::uint32, juce::uint32>> accents;
            {
                const Columns c { d.columnsOf ("ACCENTS") };
                d.forEachRow ("ACCENTS", [&] (juce::int64, const std::vector<sqlite::Value>& row)
                {
                    accents[at (row, c ("EntID")).asInt()] = { (juce::uint32) (at (row, c ("AccentBar0")).asInt() & 0xffffffff),
                                                               (juce::uint32) (at (row, c ("AccentBar1")).asInt() & 0xffffffff) };
                });
            }
            std::map<juce::int64, juce::MemoryBlock> notes;
            {
                const Columns c { d.columnsOf ("MIDI_INFO") };
                d.forEachRow ("MIDI_INFO", [&] (juce::int64, const std::vector<sqlite::Value>& row)
                {
                    notes[at (row, c ("EntID")).asInt()] = at (row, c ("MIDI")).blob;
                });
            }

            struct Groove
            {
                juce::int64 id = 0, path = 0, header = 0, library = 0, signature = 0, fileName = 0;
                juce::String name;
                int tempo = 0, length = 0;
            };
            std::vector<Groove> grooves;
            {
                const Columns c { d.columnsOf ("MIDIENTITY") };
                d.forEachRow ("MIDIENTITY", [&] (juce::int64, const std::vector<sqlite::Value>& row)
                {
                    Groove g;
                    g.id = at (row, c ("ID")).asInt();
                    g.name = at (row, c ("Name")).text;
                    g.path = at (row, c ("PathID")).asInt();
                    g.header = at (row, c ("HeaderID")).asInt();
                    g.library = at (row, c ("LibraryID")).asInt();
                    g.signature = at (row, c ("TimeSigID")).asInt();
                    g.fileName = at (row, c ("FilenameID")).asInt();
                    g.tempo = (int) at (row, c ("Tempo")).asInt();
                    g.length = (int) at (row, c ("Length")).asInt();
                    grooves.push_back (std::move (g));
                });
            }
            std::set<juce::int64> headersUsed;
            for (const auto& g : grooves)
                headersUsed.insert (g.header);
            const auto headersVary = headersUsed.size() > 1;   // else it's the pack's one heading, not a song's name

            std::sort (grooves.begin(), grooves.end(), [&] (const Groove& a, const Groove& b)
            {
                const auto pa = paths.count (a.path) ? paths.at (a.path) : juce::String(), pb = paths.count (b.path) ? paths.at (b.path) : juce::String();
                return pa != pb ? pa < pb : a.name.compareNatural (b.name) < 0;
            });

            std::set<juce::String> used, folders;
            std::map<juce::String, size_t> index;
            for (const auto& g : grooves)
            {
                const auto blob = notes.find (g.id);
                if (blob == notes.end() || blob->second.getSize() < 5)
                    continue;
                const auto library = libraries.count (g.library) ? libraries.at (g.library) : std::pair<juce::String, juce::String>();
                auto pack = library.second.isNotEmpty() ? juce::File::createLegalFileName (library.second) : cleanName (library.first);
                if (pack.isEmpty())
                    pack = file.getParentDirectory().getFileName();
                if (contents->pack.isEmpty())
                    contents->pack = pack;

                // The folders: the database's own (the first, the pack's code, left out), the song's name before the last
                // where they don't already say it.
                juce::StringArray segments;
                const auto path = paths.count (g.path) ? paths.at (g.path) : juce::String();
                auto parts = juce::StringArray::fromTokens (path.replaceCharacter ('\\', '/'), "/", "");
                parts.removeEmptyStrings();
                for (int i = 0; i < parts.size(); ++i)
                {
                    if (i == 0 && (parts[0].startsWithChar ('_') || (library.first.isNotEmpty() && parts[0].containsIgnoreCase (library.first))))
                        continue;
                    if (const auto name = cleanName (parts[i]); name.isNotEmpty())
                        segments.add (name);
                }
                if (headersVary && headers.count (g.header))
                    if (const auto song = juce::File::createLegalFileName (headers.at (g.header).trim()); song.isNotEmpty() && ! segments.contains (song, true))
                        segments.insert (juce::jmax (0, segments.size() - 1), song);

                auto folder = file.getChildFile (pack);
                folders.insert (folder.getFullPathName());
                for (const auto& s : segments)
                {
                    folder = folder.getChildFile (s);
                    folders.insert (folder.getFullPathName());
                }
                auto name = juce::File::createLegalFileName (g.name.trim());
                if (name.isEmpty())
                    name = cleanName (fileNames.count (g.fileName) ? juce::File (fileNames.at (g.fileName)).getFileNameWithoutExtension() : juce::String ("Groove"));
                auto groove = folder.getChildFile (name + ".mid");
                for (int n = 2; used.count (groove.getFullPathName()) > 0; ++n)
                    groove = folder.getChildFile (name + " (" + juce::String (n) + ").mid");
                used.insert (groove.getFullPathName());

                // As a MIDI file: its tempo, time signature, notes (velocities from the accent marks), its length.
                auto numerator = 4, denominator = 4;
                if (signatures.count (g.signature))
                {
                    const auto text = signatures.at (g.signature);
                    const auto n = text.upToFirstOccurrenceOf ("/", false, false).getIntValue(), dd = text.fromFirstOccurrenceOf ("/", false, false).getIntValue();
                    if (n > 0 && n <= 32 && dd > 0 && dd <= 32 && juce::isPowerOfTwo (dd))
                    {
                        numerator = n;
                        denominator = dd;
                    }
                }
                const auto beatsPerBar = numerator * 4.0 / denominator;
                const auto marks = accents.count (g.id) ? accents.at (g.id) : std::pair<juce::uint32, juce::uint32>();
                juce::MidiMessageSequence track;
                if (g.tempo > 0 && g.tempo <= 1000)
                    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60.0e6 / g.tempo)), 0.0);
                track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (numerator, denominator), 0.0);
                const auto* bytes = static_cast<const juce::uint8*> (blob->second.getData());
                auto last = 0.0;
                for (size_t i = 0; i + 5 <= blob->second.getSize(); i += 5)
                {
                    float beat = 0.0f;
                    const auto bits = juce::ByteOrder::littleEndianInt (bytes + i);
                    std::memcpy (&beat, &bits, sizeof (float));
                    const auto note = (int) bytes[i + 4];
                    if (! std::isfinite (beat) || beat < 0.0f || beat > 100000.0f || note > 127)
                        continue;
                    const auto tick = std::round ((double) beat * ticksPerBeat);
                    const auto velocity = velocityFor (beat, beatsPerBar, marks.first, marks.second);
                    track.addEvent (juce::MidiMessage::noteOn (10, note, velocity), tick);
                    track.addEvent (juce::MidiMessage::noteOff (10, note), tick + ticksPerBeat / 8);
                    last = juce::jmax (last, tick + ticksPerBeat / 8);
                }
                track.addEvent (juce::MidiMessage::endOfTrack(), juce::jmax (last, (double) g.length * ticksPerBeat));
                track.updateMatchedPairs();
                juce::MidiFile midi;
                midi.setTicksPerQuarterNote (ticksPerBeat);
                midi.addTrack (track);
                Contents::Item item;
                item.file = groove;
                juce::MemoryOutputStream out (item.midi, false);
                midi.writeTo (out, 0);
                out.flush();
                index[groove.getFullPathName()] = contents->items.size();
                contents->items.push_back (std::move (item));
            }
            for (const auto& f : folders)
                contents->folders.push_back (juce::File (f));
            std::sort (contents->folders.begin(), contents->folders.end(), [] (const juce::File& a, const juce::File& b)
            {
                return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
            });
            return contents;
        }

        // The database a path inside one belongs to (the path itself, or a file on the way up), or an empty File.
        juce::File databaseOf (const juce::File& path)
        {
            for (auto f = path; f.getFullPathName().isNotEmpty(); f = f.getParentDirectory())
            {
                if (f.existsAsFile())
                    return isDatabase (f) ? f : juce::File();
                if (f.isDirectory() || f == f.getParentDirectory())
                    return {};   // a real folder: no database above it holds this path
            }
            return {};
        }
    }

    bool isDatabase (const juce::File& f)
    {
        const auto name = f.getFileName().toLowerCase();
        if ((name != "mididb" && ! name.endsWith (".mididb")) || ! f.existsAsFile())
            return false;
        juce::FileInputStream in (f);
        char magic[16] {};
        return in.openedOk() && in.read (magic, 16) == 16 && std::memcmp (magic, "SQLite format 3\0", 16) == 0;
    }

    const Contents::Item* Contents::find (const juce::File& f) const
    {
        for (const auto& item : items)
            if (item.file == f)
                return &item;
        return nullptr;
    }

    std::shared_ptr<const Contents> load (const juce::File& database)
    {
        const auto size = database.getSize();
        const auto date = database.getLastModificationTime().toMilliseconds();
        {
            std::lock_guard<std::mutex> guard (cacheLock);
            if (const auto known = cache.find (database.getFullPathName()); known != cache.end() && known->second.size == size && known->second.date == date)
                return known->second.contents;
        }
        auto contents = read (database);
        std::lock_guard<std::mutex> guard (cacheLock);
        cache[database.getFullPathName()] = { size, date, contents };
        return contents;
    }

    const Contents::Item* itemFor (const juce::File& path, std::shared_ptr<const Contents>& keepAlive)
    {
        const auto database = databaseOf (path.getParentDirectory());
        if (database == juce::File())
            return nullptr;
        keepAlive = load (database);
        return keepAlive->find (path);
    }

    std::vector<juce::File> foldersIn (const juce::File& folder)
    {
        std::vector<juce::File> out;
        if (folder.isDirectory())
        {
            for (const auto& entry : juce::RangedDirectoryIterator (folder, false, wildcard, juce::File::findFiles))
                if (isDatabase (entry.getFile()))
                    for (const auto& f : load (entry.getFile())->folders)
                        if (f.getParentDirectory() == entry.getFile())
                            out.push_back (f);
            return out;
        }
        if (const auto database = databaseOf (folder); database != juce::File())
            for (const auto& f : load (database)->folders)
                if (f.getParentDirectory() == folder)
                    out.push_back (f);
        return out;
    }

    float velocityFor (double beat, double beatsPerBar, juce::uint32 accentBar0, juce::uint32 accentBar1)
    {
        if (accentBar0 == 0 && accentBar1 == 0)
            return 100.0f / 127.0f;   // no marks: one level
        auto bar = (juce::int64) std::floor (beat / beatsPerBar);
        auto step = (int) std::floor ((beat - (double) bar * beatsPerBar) * 32.0 / beatsPerBar + 0.5);
        if (step >= 32)
        {
            step -= 32;
            ++bar;
        }
        const auto marks = bar % 2 == 1 && accentBar1 != 0 ? accentBar1 : accentBar0;
        return ((marks >> step) & 1u) != 0 ? 112.0f / 127.0f : 80.0f / 127.0f;
    }
}
