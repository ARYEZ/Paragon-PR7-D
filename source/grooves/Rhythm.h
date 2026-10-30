// TAP TO FIND (PR7-D 0.6.0, plan stage d part 3): a groove's rhythm, drum group by drum group, and how close one rhythm
// is to another. Our own design, inspired by the tap-a-rhythm search of commercial drum software: the player taps a
// rhythm over a loop (or drops a MIDI file, or picks a groove), and every groove gets a match from 0 to 1.
#pragma once

#include "Groove.h"

#include <memory>

namespace grooves
{
    // The drum groups tapped and compared (the pads). Others (pedal hat, clap, percussion) aren't compared.
    enum class Lane { kick, snare, hats, ride, toms, crash };
    inline constexpr int numLanes = 6;
    inline const juce::StringArray laneNames { "KICK", "SNARE", "HI-HAT", "RIDE", "TOMS", "CRASH" };
    int laneOf (int slot) noexcept;       // the kit's slot's group, or -1
    int slotOfLane (int lane) noexcept;   // the drum a pad plays: kick, snare, closed hat, ride, mid tom, cymbal

    // Where each group's hits are, in beats from the start; the rhythm loops every lengthBeats.
    struct Rhythm
    {
        double lengthBeats = 8.0, beatsPerBar = 4.0;
        std::array<std::vector<double>, numLanes> beats;   // each in time order

        bool isEmpty() const noexcept
        {
            return std::all_of (beats.begin(), beats.end(), [] (const auto& b) { return b.empty(); });
        }
        int numHits() const noexcept
        {
            auto n = 0;
            for (const auto& b : beats)
                n += (int) b.size();
            return n;
        }

        // As text for the index: "length;perBar;lane:twelfths,twelfths...|lane:...": beats to a twelfth of a beat
        // (sixteenths and triplets alike land on it), at most the first `maxBeats` (32: eight bars of 4/4).
        static constexpr double maxBeats = 32.0;
        juce::String encode() const;
        static Rhythm decode (const juce::String&);
    };

    // A groove's rhythm through `layout` (which drum each note is), its first maxBeats.
    Rhythm rhythmOf (const Groove&, const synth::drums::notemap::Map& layout);
    // The first `lengthBeats` of a groove as a tap: a MIDI file dropped, a groove to find more like.
    Rhythm firstBars (const Rhythm&, double lengthBeats);

    // How close `groove` is to the tapped `query`, 0 to 1, both on the `gridBeats` grid (QUANTIZE): only the groups
    // tapped count (each by how many hits it has); a group scores by the hits both have against the hits either has
    // (2 x both / (tapped + groove's)); HI-HAT and RIDE stand in for each other when the groove has only one of them
    // there; the groove (looped) is tried from each of its bars, the best kept.
    double match (const Rhythm& query, const Rhythm& groove, double gridBeats);

    // QUANTIZE's grids.
    inline const juce::StringArray quantizeNames { "1/4", "1/8", "1/16", "1/8 Triplet", "1/16 Triplet" };
    inline double gridOf (int quantize) noexcept
    {
        constexpr double grids[] = { 1.0, 0.5, 0.25, 1.0 / 3.0, 1.0 / 6.0 };
        return grids[juce::jlimit (0, 4, quantize)];
    }
}
