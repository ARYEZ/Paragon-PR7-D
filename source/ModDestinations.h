#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/Modulation.h"

#include <vector>

// What the MOD MATRIX can move (stage 4a): every knob the engine follows
// live while a note plays, by its parameter ID, with the setting it moves
// and how the menu names it. Not yet: SKEW and PINCH (their curve is built
// ahead), the FFT editor's SWEEP, FLIP and VSTRENGTH (baked into its
// tables), the shapers' EQ bands (worked out once per block), and anything
// chosen from a menu or switched.
namespace ModDestinations
{
    struct Destination
    {
        juce::String parameterId;
        juce::String group;     // "Osc 1", "Filter", "Shaper 2", "Amp"
        juce::String name;      // "Volume", "Cutoff"...
        synth::mod::Target target;
    };

    const std::vector<Destination>& all();
    const Destination* find (const juce::String& parameterId);
}
