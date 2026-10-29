// THE GROOVE PLAYER (PR7-D 0.4.0): the player's own groove folders ("linked folders"), every MIDI file in them read
// once in the background (and remembered between sessions), and the browser's queries: a folder as a filter (with
// or without its subfolders), text, favourites, sorting. Kept in the presets folder's "Grooves.xml", shared by every
// PR7-D: the grooves are on the computer, not in a song.
#pragma once

#include "Groove.h"

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

        juce::String signature() const { return juce::String (numerator) + "/" + juce::String (denominator); }
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
        // `settings`: where the folders, favourites and the preview's settings are kept; `index`: what was read.
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

        // READING. scan() starts reading in the background (listeners hear when it's done, and as it goes);
        // scanNow() reads on this thread (tests).
        void scan();
        void scanNow();
        bool isScanning() const noexcept { return scanning.load(); }
        int getNumScanned() const noexcept { return scanned.load(); }

        // THE GROOVES (a copy: safe while the scan replaces them).
        std::vector<Entry> getEntries() const;
        int getNumEntries() const;

        // THE BROWSER: the grooves in `within` (nothing: everywhere; a folder: its files, and its subfolders' if
        // SUBFOLDERS is on) whose name or folder has every word of `text`, favourites only if asked, sorted.
        std::vector<Entry> query (const juce::File& within, const juce::String& text, bool favouritesOnly, Sort sort,
                                  double songBpm, int songNumerator, int songDenominator) const;

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
        void read (const std::atomic<bool>* stop);
        void stopScan();   // the folders are only changed with no scan running
        class Scanner;

        juce::File settingsFile, indexFile;
        std::vector<LinkedFolder> folders;
        bool subfolders = true;
        juce::StringArray favourites;   // full paths, sorted
        Preview preview;

        mutable std::mutex lock;        // guards entries
        std::vector<Entry> entries;
        std::atomic<bool> scanning { false };
        std::atomic<int> scanned { 0 };
        std::unique_ptr<Scanner> scanner;
    };
}
