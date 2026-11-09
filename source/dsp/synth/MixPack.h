#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

// MIXED SAMPLE PACKS (0.69.0): a drum library whose hits come already mixed to stereo, several finished mixes of each
// drum in folders of their own, laid out
//
//   <PIECE>\<VERSION>\<ARTICULATION>\<NN - MIX ...>\<NAME>_V<layer>_<mix>_RR (<round>).wav
//
// (pieces KICK / SNARE / TOMS / HATS / CYMBALS / RIDE in any order of folders; the VERSION a folder saying ORIGINAL or
// GAIN BOOSTED somewhere on the way; one mix folder may say PARALLEL: the drum compressed in parallel, to blend in).
// Our own reading of such a layout (no product named): each drum takes one mix folder (as a folder of hits:
// instruments::fromFolder reads its V / RR numbers) and its PARALLEL folder as a layer.
namespace synth::drums::mixpack
{
    enum class Version { original, boosted };

    // The mixes offered (only append: an index may be remembered), and how a mix folder's name reads as one.
    inline const juce::StringArray mixNames { "Mix 1", "Mix 1 Punchy", "Mix 2", "Mix 2 Canon", "Mix 2 Canon + Verb", "Mix 2 Gunfire",
                                              "Mix 2 Gunfire + Verb" };
    struct MixKey
    {
        int number = 0;              // 0: none in the name (a drum with one mix)
        juce::StringArray words;     // PUNCHY, CANON, GUNFIRE, VERB, TUNED, UNTUNED (sorted)
        bool operator== (const MixKey& o) const { return number == o.number && words == o.words; }
    };
    MixKey keyOf (const juce::String& folderName);
    MixKey keyOfChoice (int mix);
    bool isParallel (const juce::String& folderName);

    // What a pack gives: per slot its mix folder (the one chosen, else the nearest) and its PARALLEL folder, the
    // second kick (offered as the kick's layer), and what had no slot.
    struct Drum
    {
        juce::File mix, parallel;
        juce::String mixName;   // the mix folder's name (the summary says which was taken)
    };
    struct Plan
    {
        juce::String name;                 // the pack's folder
        std::map<int, Drum> slots;
        juce::File secondKick;             // KICK 2's mix folder (empty: none)
        juce::StringArray unused;          // e.g. "SNARE EDGE", "HATS TIGHTER STOMP"
        bool valid() const noexcept { return ! slots.empty(); }
    };
    Plan plan (const juce::File& folder, Version, int mix);

    // The slot a leaf folder's path (from the pack's folder, its parts upper-cased) plays (-1: none), the crashes by
    // their size in order (`crashSizes`, smallest first); `kick` the kick's number.
    int slotFor (const juce::StringArray& parts, const juce::Array<int>& crashSizes, int& kick);

    // Audio files of a folder, without macOS's "._" companions (a Mac's zip keeps one beside every file).
    bool isAudioFile (const juce::File&);
}
