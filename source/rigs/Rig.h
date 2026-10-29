#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/Drums.h"
#include "rigs/ModMatrix.h"

#include <map>
#include <optional>

// A preset ("rig" in the code, as across the PR7 family): one complete
// sound. The kit, its beats and the effects, and how it's filed (name, bank, number, tags).
//
// The OUTPUT knob isn't part of a preset: it suits the player's monitoring,
// not a particular sound, so switching presets leaves it alone. Each preset
// has its own LEVEL for balancing presets instead.
struct Rig
{
    static constexpr int numBanks = 5;          // A to E
    static constexpr int maxNumber = 99;

    juce::String name { "New Rig" };
    int bank = 0;                               // 0 = A
    int number = 1;                             // 1 to maxNumber
    bool favourite = false;
    juce::StringArray tags;

    // Parameter values in their real units (dB, ms...), by parameter ID.
    // Storing real values rather than 0-1 positions means a rig still
    // loads correctly if a later version changes a knob's range. Loading a
    // rig resets any parameter it doesn't mention to its default, so rigs
    // saved before a parameter existed get that parameter's neutral setting.
    std::map<juce::String, float> parameters;

    // THE DRUMS' samples, by slot (0 to 11), saved inside the preset. Shared, not copied: a preset copied
    // for undo keeps pointing at the same audio, and two presets are the same kit if they point at the same samples.
    std::map<int, std::shared_ptr<const synth::drums::Sample>> drumSamples;

    // The drum sequencer's patterns and the drums' drawn envelopes (ModMatrix: PR7-K's name).
    ModMatrix modulation;

    // Free text shown with the rig: what it's for.
    juce::String notes;

    juce::String getSlotName() const;           // e.g. "A01"
    static juce::String bankName (int bank);    // "A".."E"

    std::unique_ptr<juce::XmlElement> toXml() const;
    // Returns nothing if the XML isn't a rig.
    static std::optional<Rig> fromXml (const juce::XmlElement&);

    static constexpr const char* fileExtension = ".pr7rig";
};
