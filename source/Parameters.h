#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <string_view>

// Parameter IDs are saved in DAW projects and presets, so never rename one
// once it has shipped. Add new parameters with a new ID instead.
//
// PR7-K started afresh at 0.1.0: none of PR7-E's parameters carried over
// (its EQ, COMP, DOMINION and LIMITER code is kept for the effects rack, a
// later stage, which will give them new IDs of its own).
namespace ParamIDs
{
    // Global: not part of a preset.
    inline constexpr auto outputGain = "outputGain";   // the OUTPUT knob
    inline constexpr auto bypass     = "bypass";
    inline constexpr auto tune       = "tune";         // the whole synth, in cents (SETTINGS)

    // Saved with each preset: its own level, to balance presets against each other.
    inline constexpr auto patchLevel = "patchLevel";

    // The 8 oscillator tabs: "osc1On", "osc1Wave"... `what` is one of oscSettings.
    inline constexpr int numOscillators = 8;
    inline juce::String osc (int index, const char* what) { return "osc" + juce::String (index + 1) + what; }
    // Stage 2 (0.2.0) appended its unison, vibrato and chorder settings ("Ch1On"... for the chorder's 4 notes),
    // stage 3 (0.3.0) the WAVETABLE's: which factory table, INDEX, its mode and FRAMES,
    // 0.4.0 SHAPE's: SKEW, PINCH, FORMANT and its mode, CRUSH and its mode,
    // 0.5.0 SYNC (with its envelope), FM, AM and their modulator's RATE, GRID and PHASE LOCK,
    // 0.6.0 the FFT editor's SWEEP, FLIP, LP, HP, SLOPE, VSPEED and VSTRENGTH (its curve is kept with the preset),
    // 0.8.0 which SHAPERs the oscillator goes through ("Shp1" to "Shp4"),
    // 0.11.0 whether it follows the PITCH module's envelope.
    inline constexpr const char* oscSettings[] = {
        "On", "Wave", "Volume", "Pan", "Transpose", "Fine", "Random", "PhaseRand", "Phase", "Gain", "Width", "FadeIn", "Invert",
        "Noise", "NoiseColour", "NoiseWidth", "Sub", "SubWave", "SubOct", "SubInvert", "Filter", "Voices",
        "UniVoices", "UniMix", "UniDetune", "UniSpread", "UniOctaves",
        "VibMode", "VibWave", "VibRate", "VibAmount", "VibFade", "VibCurve",
        "Ch1On", "Ch1Coarse", "Ch1Fine", "Ch1Pan", "Ch1Vol", "Ch2On", "Ch2Coarse", "Ch2Fine", "Ch2Pan", "Ch2Vol",
        "Ch3On", "Ch3Coarse", "Ch3Fine", "Ch3Pan", "Ch3Vol", "Ch4On", "Ch4Coarse", "Ch4Fine", "Ch4Pan", "Ch4Vol",
        "Table", "WtIndex", "WtMode", "WtFrames",
        "Skew", "Pinch", "Formant", "FormantMode", "Crush", "CrushMode",
        "SyncAmount", "SyncMode", "SyncEnv", "SyncEnvTime", "SyncEnvCurve", "FmAmount", "FmSource", "AmAmount", "AmShape",
        "ModRate", "ModGrid", "ModLock",
        "FftSweep", "FftFlip", "FftLp", "FftHp", "FftSlope", "FftVSpeed", "FftVStrength",
        "Shp1", "Shp2", "Shp3", "Shp4",
        "PitchEnv" };

    // Where a setting is in oscSettings, worked out by the compiler (a typo won't compile).
    consteval int oscSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (oscSettings); ++i)
            if (std::string_view (oscSettings[i]) == what)
                return i;
        throw "not an oscillator setting";
    }

    // The FILTER and AMP. Numbered from 1: Avenger has four of each, and
    // more may come in a later stage (an oscillator's Filter switch, now on or
    // off for filter 1, can become a choice of filters without breaking presets).
    inline constexpr auto fltOn       = "flt1On";
    inline constexpr auto fltType     = "flt1Type";
    inline constexpr auto fltCutoff   = "flt1Cutoff";     // Hz
    inline constexpr auto fltRes      = "flt1Res";        // 0 to 1
    inline constexpr auto fltDrive    = "flt1Drive";      // 0 to 1
    inline constexpr auto fltKey      = "flt1Key";        // keytrack, 0 to 1
    inline constexpr auto fltVel      = "flt1Vel";        // cutoff velocity, 0 to 1
    inline constexpr auto fltResVel   = "flt1ResVel";
    inline constexpr auto fltEnv      = "flt1Env";        // envelope amount, -1 to 1
    inline constexpr auto fltEnvVel   = "flt1EnvVel";
    inline constexpr auto fltAttack   = "flt1Attack";     // ms
    inline constexpr auto fltHold     = "flt1Hold";
    inline constexpr auto fltDecay    = "flt1Decay";
    inline constexpr auto fltSustain  = "flt1Sustain";    // 0 to 1
    inline constexpr auto fltRelease  = "flt1Release";
    inline constexpr auto fltAttackCurve  = "flt1AttackCurve";    // -1 to 1
    inline constexpr auto fltDecayCurve   = "flt1DecayCurve";
    inline constexpr auto fltReleaseCurve = "flt1ReleaseCurve";

    inline constexpr auto ampVolume   = "amp1Volume";     // 0 to 2
    inline constexpr auto ampVel      = "amp1Vel";        // 0 to 1
    inline constexpr auto ampPan      = "amp1Pan";        // -1 to 1
    inline constexpr auto ampPanKey   = "amp1PanKey";
    inline constexpr auto ampSpread   = "amp1Spread";
    inline constexpr auto ampSpike    = "amp1Spike";      // 0 to 1
    inline constexpr auto ampSpikeCurve = "amp1SpikeCurve";
    inline constexpr auto ampSpikeVel = "amp1SpikeVel";
    inline constexpr auto ampAttack   = "amp1Attack";
    inline constexpr auto ampHold     = "amp1Hold";
    inline constexpr auto ampDecay    = "amp1Decay";
    inline constexpr auto ampSustain  = "amp1Sustain";
    inline constexpr auto ampRelease  = "amp1Release";
    inline constexpr auto ampAttackCurve  = "amp1AttackCurve";
    inline constexpr auto ampDecayCurve   = "amp1DecayCurve";
    inline constexpr auto ampReleaseCurve = "amp1ReleaseCurve";

    // The SHAPERs (stage 3e, 0.8.0): four, numbered "shp1" to "shp4", each with these settings.
    inline constexpr int numShapers = 4;
    inline constexpr const char* shaperSettings[] = {
        "On", "Model", "Drive", "DriveVel", "Eq1Gain", "Eq1Freq", "Eq1Q", "Eq2Gain", "Eq2Freq", "Eq2Q", "EqSplit", "Place", "Mix", "Output" };
    inline juce::String shp (int index, const char* what) { return "shp" + juce::String (index + 1) + what; }
    consteval int shaperSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (shaperSettings); ++i)
            if (std::string_view (shaperSettings[i]) == what)
                return i;
        throw "not a shaper setting";
    }

    // MODULATION (stage 4a, 0.9.0): four LFOs ("lfo1" to "lfo4", each with
    // these settings), three macro knobs and two macro buttons. The mod
    // matrix itself isn't parameters: it's kept with the preset (ModMatrix).
    inline constexpr int numLfos = 4;
    inline constexpr const char* lfoSettings[] = {
        "Wave", "Rate", "Sync", "Division", "Trigger", "Phase", "Offset", "Delay", "FadeIn", "OneShot" };
    inline juce::String lfo (int index, const char* what) { return "lfo" + juce::String (index + 1) + what; }
    consteval int lfoSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (lfoSettings); ++i)
            if (std::string_view (lfoSettings[i]) == what)
                return i;
        throw "not an LFO setting";
    }
    // The modulation envelopes (stage 4b, 0.10.0): "menv1" to "menv8". Their points are kept with the preset (ModMatrix).
    inline constexpr int numEnvelopes = 8;
    inline constexpr const char* envelopeSettings[] = { "Mode", "Trigger", "Sync", "Speed", "Steps", "Flip", "Offset" };
    inline juce::String env (int index, const char* what) { return "menv" + juce::String (index + 1) + what; }
    consteval int envelopeSetting (std::string_view what)
    {
        for (int i = 0; i < (int) std::size (envelopeSettings); ++i)
            if (std::string_view (envelopeSettings[i]) == what)
                return i;
        throw "not an envelope setting";
    }
    inline constexpr const char* macros[] = { "macro1", "macro2", "macro3" };
    inline constexpr const char* macroButtons[] = { "macroButton1", "macroButton2" };

    // The PITCH module (stage 4c, 0.11.0). Its envelope's points are kept with the preset (ModMatrix).
    inline constexpr auto pitchEnvOn    = "pitchEnvOn";
    inline constexpr auto pitchEnvMode  = "pitchEnvMode";
    inline constexpr auto pitchEnvSpeed = "pitchEnvSpeed";
    inline constexpr auto pitchEnvRange = "pitchEnvRange";   // semitones, 0 to 24
    inline constexpr auto pitchEnvSteps = "pitchEnvSteps";
    inline constexpr auto pitchEnvFlip  = "pitchEnvFlip";
    inline constexpr auto pitchKeytrack = "pitchKeytrack";   // -1 to 1
    inline constexpr auto bendLag       = "bendLag";         // ms
    inline constexpr auto glideCurve    = "glideCurve";      // -1 to 1
    inline constexpr auto glideSplit    = "glideSplit";      // a note (0: off)

    // How the keyboard plays it.
    inline constexpr auto playMode  = "playMode";     // choice: Poly, Mono, Mono Low, Mono High
    inline constexpr auto legato    = "legato";
    inline constexpr auto glide     = "glide";        // ms
    inline constexpr auto glideMode = "glideMode";    // choice: Always, Legato
    inline constexpr auto bendUp    = "bendUp";       // semitones
    inline constexpr auto bendDown  = "bendDown";
    inline constexpr auto voices    = "voices";       // notes at once

    // Global settings: kept when switching presets. Everything else is part of a preset.
    inline const juce::StringArray notInRigs { outputGain, bypass, tune };
}

namespace ParamRanges
{
    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 12.0f;
    inline constexpr float outputDefaultDb = -6.0f;   // headroom for chords (Avenger's master gain defaults to -3 dB)
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
