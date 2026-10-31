// THE GROOVE PLAYER (PR7-D 0.4.0): the player's own groove folders ("linked folders"), every MIDI file in them read
// once in the background (and remembered between sessions), and the browser's queries: a folder as a filter (with
// or without its subfolders), text, favourites, sorting. Kept in the presets folder's "Grooves.xml", shared by every
// PR7-D: the grooves are on the computer, not in a song.
#pragma once

#include "Groove.h"
#include "Rhythm.h"

#include <juce_events/juce_events.h>

#include <mutex>

namespace grooves
{
    // What the browser lists for a groove (its hits are read again when it's played or dragged).
    struct Entry
    {
        juce::File file;
        int folder = 0;              // which linked folder it's in
        juce::String name;
        juce::String family;         // the folder it's in, by name ("Funky 16ths 102 BPM")
        double bpm = 120.0;
        bool bpmKnown = false;
        int numerator = 4, denominator = 4;
        int bars = 1;
        PowerHand hand = PowerHand::none;
        float intensity = 0.0f;
        int numHits = 0;
        Feel feel = Feel::straight;
        Part part = Part::beat;      // from the names (worked out when listed)
        std::shared_ptr<const Rhythm> rhythm;   // TAP TO FIND (0.6.0): its rhythm (shared: entries are copied a lot)
        float match = -1.0f;         // with a tap search: how close, 0 to 1 (else -1)

        juce::String signature() const { return juce::String (numerator) + "/" + juce::String (denominator); }
        double lengthBeats() const noexcept { return bars * numerator * 4.0 / denominator; }
    };

    // THE FILTER COLUMNS (0.5.0): each groove's value in each, as text.
    enum class Column { type, feel, time, hand, tempo };
    inline constexpr int numColumns = 5;
    inline const juce::StringArray columnNames { "TYPE", "FEEL", "TIME", "POWER HAND", "TEMPO" };
    juce::String valueOf (const Entry&, Column);
    // The values a column can have, in the order it lists them (TIME: whichever signatures there are, listed as found).
    juce::StringArray valuesOf (Column);

    // Values chosen in each column (a groove passes with any of them) and values excluded (it passes with none of them);
    // the columns together (it passes them all).
    struct Filters
    {
        std::array<juce::StringArray, numColumns> include, exclude;
        bool isEmpty() const noexcept
        {
            return std::all_of (include.begin(), include.end(), [] (const auto& a) { return a.isEmpty(); })
                && std::all_of (exclude.begin(), exclude.end(), [] (const auto& a) { return a.isEmpty(); });
        }
        bool passes (const Entry& e) const
        {
            for (int c = 0; c < numColumns; ++c)
            {
                const auto v = valueOf (e, (Column) c);
                if ((! include[(size_t) c].isEmpty() && ! include[(size_t) c].contains (v)) || exclude[(size_t) c].contains (v))
                    return false;
            }
            return true;
        }
    };

    struct LinkedFolder
    {
        juce::File folder;
        // Which drum each note in its files is ("FILES' MAP"): groove packs are written for different layouts.
        synth::drums::notemap::Layout layout = synth::drums::notemap::Layout::grooveLibrary;
    };

    enum class Sort { name, songTempo, songSignature };
    inline const juce::StringArray sortNames { "Name", "Nearest Song Tempo", "Song Time Signature First" };

    class Library final : public juce::ChangeBroadcaster
    {
    public:
        // `settings`: where the folders, favourites and the preview's settings are kept; `index`: what was read (listed
        // by loadIndex, not here: every PR7-D in a song makes a Library, most never open GROOVES).
        Library (juce::File settings, juce::File index);
        ~Library() override;

        // THE FOLDERS. Adding or removing one, or changing its map, reads the folders again (in the background).
        const std::vector<LinkedFolder>& getFolders() const noexcept { return folders; }
        bool addFolder (const juce::File&);
        void removeFolder (int);
        void setLayout (int folder, synth::drums::notemap::Layout);
        bool getSubfolders() const noexcept { return subfolders; }
        void setSubfolders (bool);
        // How a folder's files' notes become drums: its map (CUSTOM: the plugin's own, `custom`; ROOT from `root`).
        synth::drums::notemap::Map layoutFor (int folder, const synth::drums::notemap::Map& custom, int root) const;

        // What was read last time, listed at once (once; a scan brings it up to date). The page calls it on opening.
        void loadIndex();

        // READING. scan() starts reading in the background (listeners hear when it's done, and as it goes): it lists
        // what was read before at once, then checks the folders for new or changed files, reading only those.
        // `onlyIfNeeded` (0.15.2, the page opening): not if this DAW session has checked these folders already, with
        // this index (every PR7-D in a song shares that: only the first checks). scanNow() checks on this thread.
        void scan (bool onlyIfNeeded = false);
        void scanNow();
        bool isScanning() const noexcept { return scanning.load(); }
        int getNumScanned() const noexcept { return scanned.load(); }   // files read anew (not known from before)

        // THE GROOVES (a copy: safe while the scan replaces them).
        std::vector<Entry> getEntries() const;
        int getNumEntries() const;

        // THE BROWSER: the grooves in `within` (nothing: everywhere; a folder: its files, and its subfolders' if
        // SUBFOLDERS is on) whose name or folder has every word of `text`, favourites only if asked, sorted.
        // TAP TO FIND (0.6.0): with `tap`, every groove gets its match to the tapped rhythm on the `grid`, and the list
        // is sorted by it, closest first (instead of `sort`).
        struct TapSearch { Rhythm rhythm; double grid = 0.5; };
        std::vector<Entry> query (const juce::File& within, const juce::String& text, bool favouritesOnly, Sort sort,
                                  double songBpm, int songNumerator, int songDenominator, const Filters& filters = {},
                                  const TapSearch* tap = nullptr) const;

        // FAVOURITES.
        bool isFavourite (const juce::File&) const;
        void setFavourite (const juce::File&, bool);

        // THE PREVIEW's settings, kept with the rest.
        struct Preview
        {
            bool songTempo = true;            // the song's tempo (else each groove's own)
            TempoFactor factor = TempoFactor::normal;
            float velocity = 1.0f;            // 0.25 to 1.75: every hit's velocity scaled
            bool forThisKit = true;           // dragged out: on this kit's notes (else as the file has them)
            Sort sort = Sort::name;
        };
        Preview getPreview() const noexcept { return preview; }
        void setPreview (const Preview&);

        static constexpr int maxFiles = 100000;   // read no more than this (a whole drive linked by mistake)

    private:
        void load();
        void save() const;
        void read (const std::atomic<bool>* stop, bool check);
        juce::String foldersChecked() const;
        bool checkedThisSessionAlready() const;
        void stopScan();   // the folders are only changed with no scan running
        class Scanner;

        juce::File settingsFile, indexFile;
        std::vector<LinkedFolder> folders;
        bool subfolders = true;
        juce::StringArray favourites;   // full paths, sorted
        Preview preview;

        std::atomic<bool> indexLoaded { false };   // set by a scan too
        mutable std::mutex lock;        // guards entries
        std::vector<Entry> entries;
        std::atomic<bool> scanning { false };
        std::atomic<int> scanned { 0 };
        std::unique_ptr<Scanner> scanner;
    };
}
