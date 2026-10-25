#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are saved in DAW projects and presets, so never rename one
// once it has shipped. Add new parameters with a new ID instead.
//
// IDs that are gone, never to be reused (a project or rig that still has them
// loads fine: they're ignored):
//   - before 0.1.0 shipped: PR7-B's ampOn, ampNormalise, cabOn, and the bass
//     drives' drive*, gotham* and dual*;
//   - in 0.2.0: tunerMute;
//   - in 0.3.0, with PR7-B's effects (they were mono, and PR7-E is a stereo
//     utility): comp*, gateThreshold, fp*, glitch*, lofi*, mod*, delay*,
//     reverb*, pitchTune, oct*, freeze*.
namespace ParamIDs
{
    inline constexpr auto inputGain  = "inputGain";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto presence   = "presence";    // the PRESENCE knob on the face
    inline constexpr auto rigLevel   = "rigLevel";    // the RIG LEVEL knob on the face
    inline constexpr auto bypass     = "bypass";

    inline constexpr auto tunerOn          = "tunerOn";            // stomp 1
    inline constexpr auto expression       = "expression";        // the pedal's position, 0 heel to 1 toe
    inline constexpr auto expressionTarget = "expressionTarget";  // what the pedal controls
    inline constexpr auto expressionMin    = "expressionMin";     // the target's setting at the heel...
    inline constexpr auto expressionMax    = "expressionMax";     // ...and at the toe

    // Stomp 4 for now: PR7-B's 9-band graphic EQ, until the new EQ replaces it.
    inline constexpr auto eqOn    = "eqOn";
    inline constexpr auto eqLevel = "eqLevel";
    inline const juce::StringArray eqBands { "eq33", "eq80", "eq150", "eq300", "eq500", "eq900", "eq2k", "eq5k", "eq9k" };
    inline const juce::StringArray eqBandNames { "33", "80", "150", "300", "500", "900", "2K", "5K", "9K" };
    inline constexpr auto eqUltraLo = "eqUltraLo";
    inline constexpr auto eqUltraHi = "eqUltraHi";

    // Global settings: kept when switching rigs. Everything else is part of a rig.
    // The expression pedal's position is global for the same reason a real
    // pedal's is: it's where your foot is, not part of the sound. What the
    // pedal is assigned to, and its range, are saved with each rig.
    inline const juce::StringArray notInRigs { inputGain, outputGain, bypass, tunerOn, expression };
}

namespace ExpressionTargets
{
    // Saved by number: only ever add to the end. Before 0.3.0 the list went
    // on with PR7-B's effects (2 to 8); projects and rigs from then that used
    // those load with the pedal unassigned (see setStateInformation and Rig::fromXml).
    enum { off, volume };
    inline const juce::StringArray names { "Off", "Volume" };
}

namespace ParamRanges
{
    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 24.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
