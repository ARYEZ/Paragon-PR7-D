#pragma once

#include "dsp/synth/Drums.h"

#include <array>
#include <cstdint>

// THE NOTE MAP (PR7-D 0.2.0): which MIDI note plays which drum.
//
//   ROOT          the 20 keys from ROOT play the slots in order (C the kick ... the first 12 as on General MIDI kits'
//                 octave, as PR7-K played them); the four keys under ROOT start the sequencer's patterns on KEY.
//   GROOVE LIBRARY  the extended layout of popular drum software's groove libraries, so their MIDI grooves play as
//                 written: several notes per drum (its articulations), from note 1 to 127. Inspired by that layout,
//                 checked against the owner's copy of one maker's published map and an open-source drum-map table;
//                 our own reading where they're silent (which ride notes are bow, bell or edge).
//   GENERAL MIDI  the General MIDI percussion map (35 to 81).
//   CUSTOM        the player's own, edited note by note (and saved as their default).
//
// Notes a layout doesn't use play nothing.
namespace synth::drums::notemap
{
    // Saved by index (drumsMap): only ever append.
    enum class Layout { root, grooveLibrary, generalMidi, custom };
    inline const juce::StringArray layoutNames { "Root", "Groove Library", "General MIDI", "Custom" };

    using Map = std::array<std::int8_t, 128>;   // each note's slot, -1: none

    // The slots by name (slotDefaults' order).
    enum Slot : std::int8_t { BD, RIM, SD, CLP, SD2, LT, CH, MT, PH, HT, OH, CY, RD, RB, CR2, CHN, SPL, HH, TMB, SHK };
    static_assert (SHK == numSlots - 1);

    inline Map empty()
    {
        Map m;
        m.fill (-1);
        return m;
    }

    inline Map rootLayout (int root)
    {
        auto m = empty();
        for (int k = 0; k < numSlots; ++k)
            if (root + k >= 0 && root + k < 128)
                m[(size_t) (root + k)] = (std::int8_t) k;
        return m;
    }

    namespace detail
    {
        inline void set (Map& m, std::int8_t slot, std::initializer_list<int> notes)
        {
            for (const auto n : notes)
                m[(size_t) n] = slot;
        }
        inline void range (Map& m, std::int8_t slot, int from, int to)
        {
            for (int n = from; n <= to; ++n)
                m[(size_t) n] = slot;
        }
    }

    // The groove libraries' layout: the owner's map (sent from that software's manual) as drums. Each articulation goes
    // to the slot that plays it: center hits to the snare (SD), rimshots to SD2, sidesticks to RIM; the three tom
    // pieces to HT (rack), MT (floor 1) and LT (floor 2); every crash note to CY except 86, which an open-source map
    // of the same layout calls a crash too; ride notes to RD but 53, the bell (General MIDI's ride bell, kept there by
    // the same open-source map). Its hat openness levels go to CH (closed), PH (pedal) and OH (open); HH, CR2, CHN and
    // SPL are left for CUSTOM until a real groove file shows which notes they are.
    inline Map grooveLibrary()
    {
        using namespace detail;
        auto m = empty();
        set (m, SHK, { 2 });
        set (m, TMB, { 3 });
        range (m, BD, 34, 36);
        set (m, SD, { 6, 33, 38, 39, 66, 68, 69, 70, 125 });
        set (m, SD2, { 40, 71, 126 });
        set (m, RIM, { 37, 67, 76, 127 });
        set (m, CH, { 7, 8, 9, 11, 18, 19, 20, 22, 42, 61, 62, 63, 64, 65, 119, 122 });
        set (m, PH, { 10, 21, 23, 44 });
        set (m, OH, { 12, 13, 14, 15, 16, 17, 24, 25, 26, 46, 60, 120, 121, 123, 124 });
        set (m, HT, { 47, 48, 79, 80, 81, 82 });
        set (m, MT, { 43, 45, 74, 75, 77, 78 });
        set (m, LT, { 41, 72, 73 });
        set (m, RD, { 29, 30, 51, 52, 54, 59 });
        range (m, RD, 83, 93);
        range (m, RD, 96, 105);
        range (m, RD, 108, 118);
        set (m, RB, { 53 });
        set (m, CY, { 27, 28, 31, 32, 49, 50, 55, 56, 57, 58, 86, 94, 95, 106, 107 });
        return m;
    }

    // General MIDI's percussion map, on the slots nearest each sound (no slot for the cowbell, the bongos and the
    // rest of the Latin percussion: those notes play nothing).
    inline Map generalMidi()
    {
        using namespace detail;
        auto m = empty();
        set (m, BD, { 35, 36 });
        set (m, RIM, { 37 });
        set (m, SD, { 38 });
        set (m, CLP, { 39 });
        set (m, SD2, { 40 });
        set (m, LT, { 41, 43 });
        set (m, CH, { 42 });
        set (m, MT, { 45, 47 });
        set (m, PH, { 44 });
        set (m, HT, { 48, 50 });
        set (m, OH, { 46 });
        set (m, CY, { 49 });
        set (m, RD, { 51, 59 });
        set (m, CHN, { 52 });
        set (m, RB, { 53 });
        set (m, TMB, { 54 });
        set (m, SPL, { 55 });
        set (m, CR2, { 57 });
        set (m, SHK, { 69, 70 });
        return m;
    }

    // The note a slot is written on in a MIDI export (and shown beside its row): a layout's main note for it (the
    // General MIDI one where it has one), else the lowest note that plays it, else -1.
    inline int homeNote (const Map& m, Layout layout, int slot, int root)
    {
        if (layout == Layout::root)
            return root + slot >= 0 && root + slot < 128 ? root + slot : -1;
        if (layout == Layout::grooveLibrary || layout == Layout::generalMidi)
        {
            constexpr std::array<int, numSlots> preferred { 36, 37, 38, 39, 40, 41, 42, 45, 44, 48, 46, 49, 51, 53, 57, 52, 55, -1, 54, 69 };
            const auto n = preferred[(size_t) juce::jlimit (0, numSlots - 1, slot)];
            if (n >= 0 && m[(size_t) n] == slot)
                return n;
        }
        for (int n = 0; n < 128; ++n)
            if (m[(size_t) n] == slot)
                return n;
        return -1;
    }

    // As text for the project and the default file: "note:slot" for each note that plays one.
    inline juce::String encode (const Map& m)
    {
        juce::StringArray items;
        for (int n = 0; n < 128; ++n)
            if (m[(size_t) n] >= 0)
                items.add (juce::String (n) + ":" + juce::String ((int) m[(size_t) n]));
        return items.joinIntoString (" ");
    }
    inline Map decode (const juce::String& text)
    {
        auto m = empty();
        for (const auto& item : juce::StringArray::fromTokens (text, " ", {}))
        {
            const auto note = item.upToFirstOccurrenceOf (":", false, false).getIntValue();
            const auto slot = item.fromFirstOccurrenceOf (":", false, false).getIntValue();
            if (note >= 0 && note < 128 && slot >= 0 && slot < numSlots && item.containsChar (':'))
                m[(size_t) note] = (std::int8_t) slot;
        }
        return m;
    }
}
