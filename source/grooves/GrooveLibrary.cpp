#include "GrooveLibrary.h"
#include "grooves/GrooveDatabase.h"

#include <map>

namespace grooves
{
    namespace
    {
        using synth::drums::notemap::Layout;

        // What was read from a file, kept with what it was read as (so a changed file, or folder map, is read again),
        // and the index's format ("2": with FEEL, 0.5.0; "3": with the rhythm, 0.6.0; anything older is read again).
        juce::String stamp (const juce::File& f, Layout layout)
        {
            return "3:" + juce::String (f.getSize()) + ":" + juce::String (f.getLastModificationTime().toMilliseconds()) + ":"
                   + juce::String ((int) layout);
        }

        std::unique_ptr<juce::XmlElement> toXml (const Entry& e, const juce::String& key)
        {
            auto x = std::make_unique<juce::XmlElement> ("G");
            x->setAttribute ("path", e.file.getFullPathName());
            x->setAttribute ("stamp", key);
            x->setAttribute ("bpm", e.bpm);
            x->setAttribute ("bpmKnown", e.bpmKnown ? 1 : 0);
            x->setAttribute ("num", e.numerator);
            x->setAttribute ("den", e.denominator);
            x->setAttribute ("bars", e.bars);
            x->setAttribute ("hand", (int) e.hand);
            x->setAttribute ("intensity", (double) e.intensity);
            x->setAttribute ("hits", e.numHits);
            x->setAttribute ("feel", (int) e.feel);
            if (e.rhythm != nullptr)
                x->setAttribute ("rhythm", e.rhythm->encode());
            return x;
        }

        Entry fromXml (const juce::XmlElement& x)
        {
            Entry e;
            e.file = juce::File (x.getStringAttribute ("path"));
            e.bpm = x.getDoubleAttribute ("bpm", 120.0);
            e.bpmKnown = x.getIntAttribute ("bpmKnown") != 0;
            e.numerator = x.getIntAttribute ("num", 4);
            e.denominator = x.getIntAttribute ("den", 4);
            e.bars = x.getIntAttribute ("bars", 1);
            e.hand = (PowerHand) juce::jlimit (0, powerHandNames.size() - 1, x.getIntAttribute ("hand"));
            e.intensity = (float) x.getDoubleAttribute ("intensity");
            e.numHits = x.getIntAttribute ("hits");
            e.feel = (Feel) juce::jlimit (0, feelNames.size() - 1, x.getIntAttribute ("feel"));
            e.rhythm = std::make_shared<const Rhythm> (Rhythm::decode (x.getStringAttribute ("rhythm")));
            return e;
        }

        Entry entryOf (const Groove& g, int folder, const synth::drums::notemap::Map& layout)
        {
            Entry e;
            e.file = g.file;
            e.folder = folder;
            e.name = g.name;
            e.family = g.file.getParentDirectory().getFileName();
            e.bpm = g.bpm;
            e.bpmKnown = g.bpmKnown;
            e.numerator = g.numerator;
            e.denominator = g.denominator;
            e.bars = g.bars;
            e.hand = g.hand;
            e.intensity = g.intensity;
            e.numHits = (int) g.hits.size();
            e.feel = g.feel;
            e.rhythm = std::make_shared<const Rhythm> (rhythmOf (g, layout));
            return e;
        }
    }

    class Library::Scanner final : public juce::Thread
    {
    public:
        explicit Scanner (Library& l) : juce::Thread ("PR7-D grooves"), library (l) {}
        ~Scanner() override { stopThread (10000); }
        void run() override
        {
            library.read (&stop);
            library.scanning = false;
            library.sendChangeMessage();
        }
        void signalStop() { stop = true; signalThreadShouldExit(); }

    private:
        Library& library;
        std::atomic<bool> stop { false };
    };

    Library::Library (juce::File settings, juce::File index) : settingsFile (std::move (settings)), indexFile (std::move (index))
    {
        load();
    }

    void Library::loadIndex()
    {
        if (indexLoaded)
            return;
        indexLoaded = true;
        if (const auto xml = juce::XmlDocument::parse (indexFile))
        {
            std::vector<Entry> known;
            for (const auto* x : xml->getChildWithTagNameIterator ("G"))
            {
                auto e = fromXml (*x);
                for (int f = 0; f < (int) folders.size(); ++f)
                    if (e.file.isAChildOf (folders[(size_t) f].folder))
                    {
                        e.folder = f;
                        e.name = e.file.getFileNameWithoutExtension();
                        e.family = e.file.getParentDirectory().getFileName();
                        e.part = partOf (e.file);
                        known.push_back (e);
                        break;
                    }
            }
            std::lock_guard<std::mutex> guard (lock);
            entries = std::move (known);
        }
    }

    Library::~Library()
    {
        stopScan();
    }

    void Library::load()
    {
        const auto xml = juce::XmlDocument::parse (settingsFile);
        if (xml == nullptr)
            return;
        subfolders = xml->getBoolAttribute ("subfolders", true);
        for (const auto* f : xml->getChildWithTagNameIterator ("Folder"))
            folders.push_back ({ juce::File (f->getStringAttribute ("path")),
                                 (Layout) juce::jlimit (0, synth::drums::notemap::layoutNames.size() - 1, f->getIntAttribute ("map", (int) Layout::grooveLibrary)) });
        for (const auto* f : xml->getChildWithTagNameIterator ("Favourite"))
            favourites.addIfNotAlreadyThere (f->getStringAttribute ("path"));
        favourites.sort (false);
        if (const auto* p = xml->getChildByName ("Preview"))
        {
            preview.songTempo = p->getBoolAttribute ("songTempo", true);
            preview.factor = (TempoFactor) juce::jlimit (0, tempoFactorNames.size() - 1, p->getIntAttribute ("factor", 1));
            preview.velocity = (float) juce::jlimit (0.25, 1.75, p->getDoubleAttribute ("velocity", 1.0));
            preview.forThisKit = p->getBoolAttribute ("forThisKit", true);
            preview.sort = (Sort) juce::jlimit (0, sortNames.size() - 1, p->getIntAttribute ("sort", 0));
        }
    }

    void Library::save() const
    {
        juce::XmlElement xml ("Grooves");
        xml.setAttribute ("subfolders", subfolders);
        for (const auto& f : folders)
        {
            auto* e = xml.createNewChildElement ("Folder");
            e->setAttribute ("path", f.folder.getFullPathName());
            e->setAttribute ("map", (int) f.layout);
        }
        for (const auto& path : favourites)
            xml.createNewChildElement ("Favourite")->setAttribute ("path", path);
        auto* p = xml.createNewChildElement ("Preview");
        p->setAttribute ("songTempo", preview.songTempo);
        p->setAttribute ("factor", (int) preview.factor);
        p->setAttribute ("velocity", (double) preview.velocity);
        p->setAttribute ("forThisKit", preview.forThisKit);
        p->setAttribute ("sort", (int) preview.sort);
        settingsFile.getParentDirectory().createDirectory();
        xml.writeTo (settingsFile);
    }

    bool Library::addFolder (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return false;
        for (const auto& f : folders)
            if (f.folder == folder || folder.isAChildOf (f.folder))
                return false;   // already there (inside another, its files are listed already)
        stopScan();   // the folders change under it
        folders.push_back ({ folder, Layout::grooveLibrary });
        save();
        scan();
        return true;
    }

    void Library::removeFolder (int index)
    {
        if (index < 0 || index >= (int) folders.size())
            return;
        stopScan();
        folders.erase (folders.begin() + index);
        save();
        scan();
    }

    void Library::setLayout (int folder, Layout layout)
    {
        if (folder < 0 || folder >= (int) folders.size() || folders[(size_t) folder].layout == layout)
            return;
        stopScan();
        folders[(size_t) folder].layout = layout;
        save();
        scan();   // its power hands change
    }

    void Library::setSubfolders (bool on)
    {
        if (on == subfolders)
            return;
        subfolders = on;
        save();
        sendChangeMessage();
    }

    synth::drums::notemap::Map Library::layoutFor (int folder, const synth::drums::notemap::Map& custom, int root) const
    {
        using namespace synth::drums::notemap;
        switch (folder >= 0 && folder < (int) folders.size() ? folders[(size_t) folder].layout : Layout::grooveLibrary)
        {
            case Layout::root: return rootLayout (root);
            case Layout::generalMidi: return generalMidi();
            case Layout::custom: return custom;
            case Layout::grooveLibrary: break;
        }
        return grooveLibrary();
    }

    void Library::stopScan()
    {
        if (scanner != nullptr)
            scanner->signalStop();
        scanner.reset();   // waits for it to stop
        scanning = false;
    }

    void Library::scan()
    {
        stopScan();
        scanning = true;
        scanned = 0;
        scanner = std::make_unique<Scanner> (*this);
        scanner->startThread (juce::Thread::Priority::low);
    }

    void Library::scanNow()
    {
        stopScan();
        scanning = true;
        scanned = 0;
        read (nullptr);
        scanning = false;
        sendChangeMessage();
    }

    void Library::read (const std::atomic<bool>* stop)
    {
        // What was read before, by path: a file whose size, date and folder map haven't changed isn't read again.
        std::map<juce::String, std::pair<juce::String, Entry>> before;
        if (const auto xml = juce::XmlDocument::parse (indexFile))
            for (const auto* x : xml->getChildWithTagNameIterator ("G"))
                before[x->getStringAttribute ("path")] = { x->getStringAttribute ("stamp"), fromXml (*x) };

        std::vector<Entry> found;
        juce::XmlElement index ("GrooveIndex");
        for (int f = 0; f < (int) folders.size(); ++f)
        {
            const auto& linked = folders[(size_t) f];
            // CUSTOM and ROOT files: their power hand from the factory layout (the plugin's own map can change any time).
            const auto map = layoutFor (f, synth::drums::notemap::grooveLibrary(), 36);
            const auto add = [&] (const juce::File& file, const juce::String& key, auto readIt)
            {
                Entry e;
                if (const auto known = before.find (file.getFullPathName()); known != before.end() && known->second.first == key)
                    e = known->second.second;
                else if (const auto g = readIt())
                    e = entryOf (*g, f, map);
                else
                    return;   // not a groove: no notes, or not a MIDI file
                e.folder = f;
                e.name = file.getFileNameWithoutExtension();
                e.family = file.getParentDirectory().getFileName();
                e.part = partOf (file);
                index.addChildElement (toXml (e, key).release());
                found.push_back (std::move (e));
                if ((++scanned % 200) == 0)
                    sendChangeMessage();
            };
            for (const auto& item : juce::RangedDirectoryIterator (linked.folder, true, juce::String ("*.mid;*.midi;") + db::wildcard, juce::File::findFiles))
            {
                if ((stop != nullptr && stop->load()) || (int) found.size() >= maxFiles)
                    break;
                const auto file = item.getFile();
                if (! file.hasFileExtension ("mid;midi"))
                {
                    // A groove database (0.15.0): each groove in it, as if a file inside it; known ones by the database's
                    // own size and date.
                    if (! db::isDatabase (file))
                        continue;
                    const auto contents = db::load (file);
                    const auto key = "db:" + stamp (file, linked.layout);
                    for (const auto& groove : contents->items)
                    {
                        if ((stop != nullptr && stop->load()) || (int) found.size() >= maxFiles)
                            break;
                        add (groove.file, key, [&]
                        {
                            juce::MemoryInputStream in (groove.midi, false);
                            return grooves::read (in, groove.file, map);
                        });
                    }
                    continue;
                }
                add (file, stamp (file, linked.layout), [&] { return grooves::read (file, map); });
            }
        }
        if (stop != nullptr && stop->load())
            return;   // stopped for a newer scan: it'll list them
        {
            std::lock_guard<std::mutex> guard (lock);
            entries = std::move (found);
            indexLoaded = true;   // what's listed is newer than the index
        }
        indexFile.getParentDirectory().createDirectory();
        index.writeTo (indexFile);
    }

    std::vector<Entry> Library::getEntries() const
    {
        std::lock_guard<std::mutex> guard (lock);
        return entries;
    }

    int Library::getNumEntries() const
    {
        std::lock_guard<std::mutex> guard (lock);
        return (int) entries.size();
    }

    juce::String valueOf (const Entry& e, Column c)
    {
        switch (c)
        {
            case Column::type: return partNames[(int) e.part];
            case Column::feel: return feelNames[(int) e.feel];
            case Column::time: return e.signature();
            case Column::hand: return powerHandNames[(int) e.hand];
            case Column::tempo:
            {
                if (! e.bpmKnown)
                    return "Unknown";
                const auto bpm = juce::roundToInt (e.bpm);
                if (bpm < 80) return "Below 80";
                if (bpm >= 160) return "160 and up";
                const auto low = bpm / 20 * 20;
                return juce::String (low) + "-" + juce::String (low + 19);
            }
        }
        return {};
    }

    juce::StringArray valuesOf (Column c)
    {
        switch (c)
        {
            case Column::type: return partNames;
            case Column::feel: return feelNames;
            case Column::time: return {};
            case Column::hand: return powerHandNames;
            case Column::tempo: return { "Below 80", "80-99", "100-119", "120-139", "140-159", "160 and up", "Unknown" };
        }
        return {};
    }

    std::vector<Entry> Library::query (const juce::File& within, const juce::String& text, bool favouritesOnly, Sort sort,
                                       double songBpm, int songNumerator, int songDenominator, const Filters& filters,
                                       const TapSearch* tap) const
    {
        juce::StringArray words;
        words.addTokens (text.toLowerCase(), " ", "\"");
        words.removeEmptyStrings();
        std::vector<Entry> result;
        {
            std::lock_guard<std::mutex> guard (lock);
            for (const auto& e : entries)
            {
                if (within != juce::File() && ! (subfolders ? e.file.isAChildOf (within) : e.file.getParentDirectory() == within))
                    continue;
                if (favouritesOnly && ! favourites.contains (e.file.getFullPathName()))
                    continue;
                const auto haystack = (e.name + " " + e.family).toLowerCase();
                if (! std::all_of (words.begin(), words.end(), [&] (const juce::String& w) { return haystack.contains (w); }))
                    continue;
                if (! filters.passes (e))
                    continue;
                result.push_back (e);
                if (tap != nullptr)
                    result.back().match = e.rhythm != nullptr ? (float) match (tap->rhythm, *e.rhythm, tap->grid) : 0.0f;
            }
        }
        const auto byName = [] (const Entry& a, const Entry& b)
        {
            if (const auto c = a.family.compareNatural (b.family); c != 0)
                return c < 0;
            return a.name.compareNatural (b.name) < 0;
        };
        if (tap != nullptr)
        {
            std::stable_sort (result.begin(), result.end(), [&] (const Entry& a, const Entry& b)
            {
                return a.match != b.match ? a.match > b.match : byName (a, b);
            });
            return result;
        }
        switch (sort)
        {
            case Sort::name:
                std::sort (result.begin(), result.end(), byName);
                break;
            case Sort::songTempo:
                std::sort (result.begin(), result.end(), [&] (const Entry& a, const Entry& b)
                {
                    const auto da = std::abs (a.bpm - songBpm), db = std::abs (b.bpm - songBpm);
                    return da != db ? da < db : byName (a, b);
                });
                break;
            case Sort::songSignature:
                std::sort (result.begin(), result.end(), [&] (const Entry& a, const Entry& b)
                {
                    const auto ma = a.numerator == songNumerator && a.denominator == songDenominator;
                    const auto mb = b.numerator == songNumerator && b.denominator == songDenominator;
                    return ma != mb ? ma : byName (a, b);
                });
                break;
        }
        return result;
    }

    bool Library::isFavourite (const juce::File& f) const
    {
        return favourites.contains (f.getFullPathName());
    }

    void Library::setFavourite (const juce::File& f, bool on)
    {
        if (on)
            favourites.addIfNotAlreadyThere (f.getFullPathName());
        else
            favourites.removeString (f.getFullPathName());
        save();
        sendChangeMessage();
    }

    void Library::setPreview (const Preview& p)
    {
        preview = p;
        save();
    }
}
