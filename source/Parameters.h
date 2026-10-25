#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are saved in DAW projects and presets, so never rename one
// once it has shipped. Add new parameters with a new ID instead.
namespace ParamIDs
{
    inline constexpr auto inputGain    = "inputGain";
    inline constexpr auto outputGain   = "outputGain";
    // (PR7-B's ampOn, ampNormalise and cabOn, and its OD / DISTORTION block's
    // drive*, gotham* and dual* IDs, went with the NAM amp, the cabinet IR and
    // the bass drives before PR7-E 0.1.0 shipped. Don't reuse those IDs.)

    // Phase 3
    inline constexpr auto presence       = "presence";

    inline constexpr auto compOn         = "compOn";
    inline constexpr auto compThreshold  = "compThreshold";
    inline constexpr auto compRatio      = "compRatio";
    inline constexpr auto compLevel      = "compLevel";
    inline constexpr auto gateThreshold  = "gateThreshold";

    inline constexpr auto modOn          = "modOn";
    inline constexpr auto modType        = "modType";
    inline constexpr auto modRate        = "modRate";
    inline constexpr auto modDepth       = "modDepth";
    inline constexpr auto modMix         = "modMix";
    // AEON's (the flanger's) MANUAL and RES; it shares RATE and DEPTH.
    inline constexpr auto modManual      = "modManual";
    inline constexpr auto modRes         = "modRes";

    inline constexpr auto delayOn        = "delayOn";
    inline constexpr auto delayTime      = "delayTime";
    inline constexpr auto delaySync      = "delaySync";
    inline constexpr auto delayFeedback  = "delayFeedback";
    inline constexpr auto delayTone      = "delayTone";
    inline constexpr auto delayMix       = "delayMix";
    inline constexpr auto delayPingPong  = "delayPingPong";

    inline constexpr auto reverbOn       = "reverbOn";
    inline constexpr auto reverbSize     = "reverbSize";
    inline constexpr auto reverbTone     = "reverbTone";
    inline constexpr auto reverbMix      = "reverbMix";

    // Phase 4
    inline constexpr auto rigLevel       = "rigLevel";

    // Phase 5
    inline constexpr auto bypass         = "bypass";

    // Phase 6
    inline constexpr auto tunerOn          = "tunerOn";
    inline constexpr auto tunerMute        = "tunerMute";
    inline constexpr auto expression       = "expression";        // the pedal's position, 0 heel to 1 toe
    inline constexpr auto expressionTarget = "expressionTarget";  // what the pedal controls
    inline constexpr auto expressionMin    = "expressionMin";     // the target's setting at the heel...
    inline constexpr auto expressionMax    = "expressionMax";     // ...and at the toe

    // Phase 8: the 9-band graphic EQ, one band per octave.
    inline constexpr auto eqOn    = "eqOn";
    inline constexpr auto eqLevel = "eqLevel";
    // The bands of PR7-B's bass EQ (PR7-G's were 63 Hz to 16 kHz, with their own IDs).
    inline const juce::StringArray eqBands { "eq33", "eq80", "eq150", "eq300", "eq500", "eq900", "eq2k", "eq5k", "eq9k" };
    inline const juce::StringArray eqBandNames { "33", "80", "150", "300", "500", "900", "2K", "5K", "9K" };
    inline constexpr auto eqUltraLo = "eqUltraLo";
    inline constexpr auto eqUltraHi = "eqUltraHi";

    // Modulation tempo sync: Off, or a note length at the tapped or host tempo.
    inline constexpr auto modSync = "modSync";

    // Version 10: the FILTER / PITCH block: auto-wah and pitch shifter.
    inline constexpr auto fpOn          = "fpOn";
    inline constexpr auto fpType        = "fpType";
    inline constexpr auto fpSensitivity = "fpSensitivity";   // auto-wah
    inline constexpr auto fpResonance   = "fpResonance";     // auto-wah
    inline constexpr auto fpInterval    = "fpInterval";      // pitch shifter, in semitones
    inline constexpr auto fpMix         = "fpMix";

    // Version 12: the LO-FI block, last in the chain.
    inline constexpr auto lofiOn     = "lofiOn";
    inline constexpr auto lofiType   = "lofiType";
    inline constexpr auto lofiAmount = "lofiAmount";
    inline constexpr auto lofiNoise  = "lofiNoise";
    inline constexpr auto lofiMix    = "lofiMix";
    inline constexpr auto lofiLevel  = "lofiLevel";

    // Version 15: the reverb's TYPE and PRE-DELAY.
    inline constexpr auto reverbType     = "reverbType";
    inline constexpr auto reverbPreDelay = "reverbPreDelay";

    // Version 16: the delay's TYPE.
    inline constexpr auto delayType = "delayType";

    // Version 17: DUCK for the delay and the reverb.
    inline constexpr auto delayDuck  = "delayDuck";
    inline constexpr auto reverbDuck = "reverbDuck";

    // Version 21: the FILTER / PITCH block's Whammy-Wah program (the XP-100's 01 to 28).
    // (Version 20's "fpWhammyWahMode" was replaced by this the same day.)
    inline constexpr auto fpXpProgram = "fpXpProgram";

    // Version 22: the Daybreak compressor (COMP / GATE's second TYPE).
    inline constexpr auto compType      = "compType";
    inline constexpr auto compAmount    = "compAmount";      // COMP
    inline constexpr auto compDetail    = "compDetail";      // DETAIL
    inline constexpr auto compMix       = "compMix";         // MIX
    inline constexpr auto compIntensity = "compIntensity";   // INTENSITY

    // Version 23: the GLITCH block.
    inline constexpr auto glitchOn       = "glitchOn";
    inline constexpr auto glitchMode     = "glitchMode";
    inline constexpr auto glitchPosition = "glitchPosition";   // Post (after the amp) or Pre (before it)
    inline constexpr auto glitchSync     = "glitchSync";
    inline constexpr auto glitchTime     = "glitchTime";       // LENGTH in ms, when not synced
    inline constexpr auto glitchDivision = "glitchDivision";   // LENGTH as a note value, when synced
    inline constexpr auto glitchDensity  = "glitchDensity";
    inline constexpr auto glitchPitch    = "glitchPitch";
    inline constexpr auto glitchChaos    = "glitchChaos";
    inline constexpr auto glitchMix      = "glitchMix";

    // Version 24: the PITCH block (first in the chain).
    inline constexpr auto pitchTune      = "pitchTune";        // TUNING, semitones; 0 is standard (off)
    inline constexpr auto octOn          = "octOn";            // the OCTAVER
    inline constexpr auto octSubTwo      = "octSubTwo";
    inline constexpr auto octSubOne      = "octSubOne";
    inline constexpr auto octDry         = "octDry";
    inline constexpr auto octUp          = "octUp";
    inline constexpr auto freezeOn       = "freezeOn";         // FREEZE
    inline constexpr auto freezeLevel    = "freezeLevel";
    inline constexpr auto freezeAttack   = "freezeAttack";     // ms
    inline constexpr auto freezeRelease  = "freezeRelease";    // ms

    // Global settings: kept when switching rigs. Everything else is part of a rig.
    // The expression pedal's position is global for the same reason a real
    // pedal's is: it's where your foot is, not part of the sound. What the
    // pedal is assigned to, and its range, are saved with each rig.
    inline const juce::StringArray notInRigs { inputGain, outputGain, bypass, tunerOn, tunerMute, expression };
}

namespace ExpressionTargets
{
    // Saved by number: only ever add to the end.
    enum { off, volume, wah, morph, pitch, loFi, whammyWah, glitch, talkBox };
    inline const juce::StringArray names { "Off", "Volume", "Wah / Filter", "Morph FX", "Pitch / Whammy", "Lo-Fi", "Whammy-Wah", "Glitch", "Talk Box" };
}

namespace ParamRanges
{
    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 24.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
