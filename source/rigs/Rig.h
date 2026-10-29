#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/Drums.h"
#include "dsp/synth/Freeform.h"
#include "dsp/synth/Spectrum.h"
#include "dsp/synth/Wavetable.h"
#include "rigs/ModMatrix.h"

#include <map>
#include <optional>

// A preset ("rig" in the code, as across the PR7 family): one complete
// sound. Every synth setting, and how it's filed (name, bank, number, tags).
//
// The OUTPUT knob and TUNE aren't part of a preset: they suit the player's
// monitoring and band, not a particular sound, so switching presets leaves
// them alone. Each preset has its own LEVEL for balancing presets instead.
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

    // Wavetables loaded from files, by oscillator (0 to 7), saved inside the
    // preset so it plays the same on another computer. (A factory table needs
    // nothing here: its oscillator's TABLE parameter names it.)
    std::map<int, synth::WavetableSource> wavetables;

    // The FFT editor's curves, by oscillator (only the ones drawn on).
    std::map<int, synth::SpectrumEdit> spectra;

    // FREEFORM drawings, by oscillator (only the ones drawn on).
    std::map<int, synth::FreeformDrawing> drawings;

    // THE DRUMS' samples, by slot (0 to 11), saved inside the preset (stage 6a). Shared, not copied: a preset copied
    // for undo keeps pointing at the same audio, and two presets are the same kit if they point at the same samples.
    std::map<int, std::shared_ptr<const synth::drums::Sample>> drumSamples;

    // The MOD MATRIX's routes and the macros' names (stage 4a).
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
