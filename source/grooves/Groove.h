// THE GROOVE PLAYER (PR7-D 0.4.0, plan stage d part 1): one MIDI groove from the player's own files, and what can be
// told about it from the file alone (its tempo, time signature, length, what the riding hand plays, how busy and hard
// it is). Our own design, inspired by the groove browsers of commercial drum software: nothing is read from, or
// shipped from, anyone's groove database; the tags come from the file and its folder.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/synth/NoteMap.h"

#include <optional>
#include <vector>

namespace grooves
{
    // What the riding hand plays most (the "power hand"), worked out from the notes through the folder's layout.
    enum class PowerHand { none, closedHat, openHat, ride, crash, percussion };
    // (0.16.0: named as groove packs' databases name them, so the two list together.)
    inline const juce::StringArray powerHandNames { "-", "Hi-Hat Closed", "Hi-Hat Open", "Ride", "Crash", "Percussion" };

    // FEEL (0.5.0): how its off-beat hits fall, on the half / quarter-beat grid or the triplet grid (a swing or shuffle).
    enum class Feel { straight, swung };
    inline const juce::StringArray feelNames { "Straight", "Swung" };

    // TYPE (0.5.0): which part of a song it's for, from the words groove packs name their files and folders with
    // ("Verse 02", "Pre-Chorus", "Fills"...); BEAT when none is there.
    enum class Part { beat, intro, verse, preChorus, chorus, bridge, fill, ending, countIn };
    inline const juce::StringArray partNames { "Beat", "Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Fill", "Ending", "Count-In" };
    Part partOf (const juce::File&);   // the file's name, then its folder's, then the one above
    Part partIn (const juce::String& name);   // nothing found: Part::beat

    // GROOVE PARTS (0.7.0): a groove's drums in groups, to hear, drag or lay into a pattern on their own. Chosen as a
    // set: bit n is part n (allParts: every one).
    enum class DrumPart { kick, snare, hats, pedal, ride, toms, crash, percussion };
    inline constexpr int numDrumParts = 8;
    inline constexpr unsigned allParts = (1u << numDrumParts) - 1u;
    inline const juce::StringArray drumPartNames { "KICK", "SNARE", "HI-HAT", "HI-HAT PEDAL", "RIDE", "TOMS", "CRASH", "PERCUSSION" };
    int drumPartOf (int slot) noexcept;   // the kit's slot's part (every slot has one), -1 for none
    inline bool inParts (unsigned parts, int slot) noexcept
    {
        const auto part = drumPartOf (slot);
        return part >= 0 && (parts & (1u << part)) != 0;
    }

    struct Hit
    {
        double beat = 0.0;    // quarter notes from the groove's start
        int note = 0;         // as written in the file
        float velocity = 1.0f;
    };

    struct Groove
    {
        juce::File file;
        juce::String name;                // the file's name, without .mid
        double bpm = 120.0;               // the file's own tempo (or the one in its name or folder's; else 120)
        bool bpmKnown = false;
        int numerator = 4, denominator = 4;
        double lengthBeats = 4.0;         // whole bars, in quarter notes
        int bars = 1;
        PowerHand hand = PowerHand::none;
        float intensity = 0.0f;           // 0 to 1: how hard (velocity) and how busy (hits a beat)
        Feel feel = Feel::straight;
        std::vector<Hit> hits;            // in time order

        double beatsPerBar() const noexcept { return numerator * 4.0 / denominator; }
        juce::String signature() const { return juce::String (numerator) + "/" + juce::String (denominator); }
    };

    // A groove from a standard MIDI file (format 0 or 1, timed in ticks), every track and channel merged; `layout` says
    // which drum each note is (for the power hand). Nothing if it can't be read or has no notes.
    std::optional<Groove> read (const juce::File&, const synth::drums::notemap::Map& layout);
    std::optional<Groove> read (juce::InputStream&, const juce::File& file, const synth::drums::notemap::Map& layout);

    // "Funky 16ths 102 BPM" -> 102 (also "102bpm"); 0 if the text has none from 30 to 300.
    double tempoInName (const juce::String&);

    // The tempo changes the preview and the dragged file get (only how fast: the notes stay as played).
    enum class TempoFactor { half, normal, twice, twoThirds, threeHalves };
    inline const juce::StringArray tempoFactorNames { "1/2x", "1x", "2x", "2/3x (4/4 to 6/8)", "3/2x (6/8 to 4/4)" };
    inline double factorOf (TempoFactor f) noexcept
    {
        constexpr double factors[] = { 0.5, 1.0, 2.0, 2.0 / 3.0, 1.5 };
        return factors[juce::jlimit (0, 4, (int) f)];
    }

    // The groove as a MIDI file to drag out: one track marked `bpm`, each hit's velocity scaled by `velocityScale`
    // (1: as played), on the note `noteFor` gives it (below 0: left out), at its beat times `factor`'s inverse: a DAW
    // places a dropped file at the song's tempo (not the file's), so 2x has to put the notes twice as close. 2/3x on
    // 4/4 is marked 6/8, 3/2x on 6/8 is marked 4/4 (the bars they make).
    juce::MidiFile toMidi (const Groove&, double bpm, float velocityScale, const std::function<int (int note)>& noteFor,
                           TempoFactor factor = TempoFactor::normal);
}
