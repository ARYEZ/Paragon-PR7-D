#include "Parameters.h"

#include "dsp/synth/Drums.h"
#include "dsp/synth/SynthSettings.h"
#include "dsp/synth/OscModulation.h"
#include "dsp/synth/Wavetable.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    // Version hint: the PR7-K version that introduced a parameter (1 for 0.1.0, 2 for 0.2.0, 3 for 0.3.0, 4 for 0.4.0, 5 for 0.5.0, 6 for 0.6.0, 7 for 0.8.0, 8 for 0.9.0, 9 for 0.10.0, 10 for 0.11.0).
    constexpr int firstVersion = 1;
    constexpr int stage2 = 2;
    constexpr int stage3 = 3;
    constexpr int stage3b = 4;   // 0.4.0
    constexpr int stage3c = 5;   // 0.5.0
    constexpr int stage3d = 6;   // 0.6.0
    constexpr int stage3e = 7;   // 0.8.0 (0.7.0 added no parameters)
    constexpr int stage4a = 8;   // 0.9.0
    constexpr int stage4b = 9;   // 0.10.0
    constexpr int stage4c = 10;  // 0.11.0
    constexpr int stage4d = 11;  // 0.12.0
    constexpr int stage4e = 12;  // 0.13.0
    constexpr int stage4f = 13;  // 0.14.0
    constexpr int stage5a = 14;  // 0.15.0
    constexpr int stage5b = 15;  // 0.16.0 (16, 0.17.0: the rack, in RackParameters.cpp)
    constexpr int stage7b = 17;  // 0.18.0 (18, 0.19.0: MOD / LO-FI, in RackParameters.cpp)
    constexpr int stage7d = 19;  // 0.20.0
    constexpr int stage6a = 20;  // 0.21.0
    constexpr int stage6aStart = 21;   // 0.21.1: SAMPLE's START and END
    constexpr int stage6b = 22;  // 0.22.0
    constexpr int stage6c = 23;  // 0.23.0

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asSignedPercent = [] (float value, int)
    {
        const auto percent = juce::roundToInt (value * 100.0f);
        return (percent > 0 ? "+" : "") + juce::String (percent) + "%";
    };
    // A level as a gain (0 to 2), shown in dB.
    const ToText asGain = [] (float value, int)
    {
        if (value <= 0.00001f)
            return juce::String ("-inf");
        const auto db = juce::Decibels::gainToDecibels (value);
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    };
    const ToText asFrequency = [] (float hz, int)
    {
        return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    };
    const ToText asMs = [] (float ms, int)
    {
        return ms < 1000.0f ? juce::String (ms, ms < 10.0f ? 1 : 0) + " ms" : juce::String (ms / 1000.0f, 2) + " s";
    };
    const ToText asPan = [] (float pan, int)
    {
        const auto amount = juce::roundToInt (std::abs (pan) * 100.0f);
        return amount == 0 ? juce::String ("C") : (pan < 0.0f ? "L" : "R") + juce::String (amount);
    };
    const ToText asSemitones = [] (float value, int)
    {
        const auto semis = juce::roundToInt (value);
        return (semis > 0 ? "+" : "") + juce::String (semis) + " st";
    };
    const ToText asCents = [] (float value, int)
    {
        const auto cents = juce::roundToInt (value);
        return (cents > 0 ? "+" : "") + juce::String (cents) + " ct";
    };
    // RANDOM: below 0 random, above 0 alternating, up to a semitone.
    const ToText asRandom = [] (float value, int)
    {
        if (std::abs (value) < 0.005f)
            return juce::String ("Off");
        return (value < 0.0f ? "Rnd " : "Alt ") + juce::String (juce::roundToInt (std::abs (value) * 100.0f)) + " ct";
    };
    const ToText asColour = [] (float value, int)
    {
        if (value < 0.03f) return juce::String ("White");
        if (std::abs (value - 0.5f) < 0.03f) return juce::String ("Pink");
        if (value > 0.97f) return juce::String ("Brown");
        return juce::String (juce::roundToInt (value * 100.0f)) + "%";
    };
    const ToText asCount = [] (float value, int) { return juce::String (juce::roundToInt (value)); };
    // FORMANT: how many times faster (left) or slower (right) the wave plays, as SHAPE does it.
    const ToText asFormant = [] (float value, int)
    {
        if (std::abs (value) < 0.005f)
            return juce::String ("Off");
        const auto k = value < 0.0f ? std::exp2 (-3.0f * value) : std::exp2 (-2.0f * value);
        return k >= 1.0f ? "x" + juce::String (k, 2) : "/" + juce::String (1.0f / k, 2);
    };
    // SYNC: how far above the note its copy runs.
    const ToText asSync = [] (float value, int)
    {
        if (value < 0.005f)
            return juce::String ("Off");
        const auto semitones = 60.0f * value;
        return semitones < 12.0f ? "+" + juce::String (semitones, 1) + " st" : "+" + juce::String (semitones / 12.0f, 2) + " oct";
    };
    // FFT LP / HP: a harmonic number (Off at the ends).
    const ToText asHarmonicLp = [] (float value, int) { return value >= 511.5f ? juce::String ("Off") : "H" + juce::String (juce::roundToInt (value)); };
    const ToText asHarmonicHp = [] (float value, int) { return value <= 1.01f ? juce::String ("Off") : "H" + juce::String (juce::roundToInt (value)); };
    const ToText asRange = [] (float value, int) { return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (value)) + " st"; };
    const ToText asSplit = [] (float value, int)
    {
        const auto note = juce::roundToInt (value);
        return note <= 0 ? juce::String ("Off") : juce::MidiMessage::getMidiNoteName (note, true, true, 4);
    };
    const ToText asNote = [] (float value, int) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (value), true, true, 4); };
    const ToText asDecay = [] (float value, int) { return value < 0.5f ? juce::String ("Off") : value < 1000.0f ? juce::String (juce::roundToInt (value)) + " ms"
                                                                                                                 : juce::String (value / 1000.0f, 2) + " s"; };
    const ToText asTimes = [] (float value, int) { return "x" + juce::String (value, value < 1.0f ? 2 : 1); };
    const ToText asSteps = [] (float value, int) { return value < 0.5f ? juce::String ("Smooth") : juce::String (juce::roundToInt (value)); };
    // The SHAPER's DRIVE, as the gain into its curve.
    const ToText asShaperDrive = [] (float value, int) { return "+" + juce::String (value * synth::shaper::maxDriveDb, 1) + " dB"; };
    const ToText asEqGain = [] (float value, int)
    {
        return std::abs (value) < 0.05f ? juce::String ("Off") : (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
    };
    const ToText asQ = [] (float value, int) { return juce::String (value, value < 10.0f ? 2 : 1); };
    const ToText asSweep = [] (float value, int)
    {
        const auto octaves = value * 4.0f;
        return std::abs (octaves) < 0.01f ? juce::String ("0") : (octaves > 0.0f ? "+" : "") + juce::String (octaves, 2) + " oct";
    };
    const ToText asCrush = [] (float value, int) { return value < 0.005f ? juce::String ("Off") : juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    // WAVETABLE FRAMES: at the top, every frame the table has (however many that is).
    const ToText asFrames = [] (float value, int)
    {
        return juce::roundToInt (value) >= synth::maxWavetableFrames ? juce::String ("All") : juce::String (juce::roundToInt (value));
    };
    const ToText asHz = [] (float hz, int) { return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz"; };
    const ToText asCentsDepth = [] (float cents, int) { return cents < 0.05f ? juce::String ("Off") : juce::String (cents, cents < 10.0f ? 1 : 0) + " ct"; };
    // UNISON DETUNE: how far the outer copies are from the note (squared, as the engine does).
    const ToText asDetune = [] (float detune, int)
    {
        return juce::String (detune * detune * synth::maxDetuneCents, 1) + " ct";
    };
    const ToText asOctaves = [] (float value, int)
    {
        const auto octaves = juce::roundToInt (value);
        return octaves == 0 ? juce::String ("Off") : "+" + juce::String (octaves) + " Oct";
    };

    // A range whose knob travel is logarithmic: each octave takes the same distance.
    Range logRange (double min, double max)
    {
        return { (float) min, (float) max,
                 [] (float start, float end, float normalised) { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
    }

    // Times from 0: most of the knob for short times, as on any synth's envelope.
    Range timeRange (float maxMs)
    {
        Range range { 0.0f, maxMs };
        range.setSkewForCentre (maxMs / 40.0f);   // halfway round: 500 ms of 20 s
        return range;
    }

    void add (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name,
              Range range, float defaultValue, const ToText& toText, int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, version }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)));
    }

    void addSwitch (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name, bool defaultValue,
                    int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, version }, name, defaultValue));
    }

    void addChoice (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& id, const juce::String& name,
                    const juce::StringArray& choices, int defaultIndex, int version = firstVersion)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, version }, name, choices, defaultIndex));
    }

    // An envelope's eight: ATTACK HOLD DECAY SUSTAIN RELEASE and the three curves.
    void addEnvelope (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& prefix,
                      const char* attack, const char* hold, const char* decay, const char* sustain, const char* release,
                      const char* attackCurve, const char* decayCurve, const char* releaseCurve,
                      const synth::EnvelopeSettings& defaults)
    {
        add (layout, attack, prefix + " Attack", timeRange (20000.0f), defaults.attackMs, asMs);
        add (layout, hold, prefix + " Hold", timeRange (10000.0f), defaults.holdMs, asMs);
        add (layout, decay, prefix + " Decay", timeRange (20000.0f), defaults.decayMs, asMs);
        add (layout, sustain, prefix + " Sustain", { 0.0f, 1.0f }, defaults.sustain, asPercent);
        add (layout, release, prefix + " Release", timeRange (20000.0f), defaults.releaseMs, asMs);
        add (layout, attackCurve, prefix + " Attack Curve", { -1.0f, 1.0f }, defaults.attackCurve, asSignedPercent);
        add (layout, decayCurve, prefix + " Decay Curve", { -1.0f, 1.0f }, defaults.decayCurve, asSignedPercent);
        add (layout, releaseCurve, prefix + " Release Curve", { -1.0f, 1.0f }, defaults.releaseCurve, asSignedPercent);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using namespace ParamIDs;

    add (layout, outputGain, "Output", { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, ParamRanges::outputDefaultDb, asDb);
    addSwitch (layout, bypass, "Bypass", false);
    add (layout, tune, "Tune", { -100.0f, 100.0f, 1.0f }, 0.0f, asCents);
    add (layout, patchLevel, "Preset Level", { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, 0.0f, asDb);

    // The oscillators. Only the first is on in a new sound, as on Avenger.
    const synth::OscSettings defaults;
    for (int i = 0; i < numOscillators; ++i)
    {
        const auto name = "Osc " + juce::String (i + 1) + " ";
        addSwitch (layout, osc (i, "On"), name + "On", i == 0);
        addChoice (layout, osc (i, "Wave"), name + "Wave", synth::oscWaveNames, (int) defaults.wave);
        add (layout, osc (i, "Volume"), name + "Volume", { 0.0f, 2.0f }, defaults.volume, asGain);
        add (layout, osc (i, "Pan"), name + "Pan", { -1.0f, 1.0f }, defaults.pan, asPan);
        add (layout, osc (i, "Transpose"), name + "Transpose", { -48.0f, 48.0f, 1.0f }, defaults.transpose, asSemitones);
        add (layout, osc (i, "Fine"), name + "Fine", { -100.0f, 100.0f, 1.0f }, defaults.fineCents, asCents);
        add (layout, osc (i, "Random"), name + "Random Pitch", { -1.0f, 1.0f }, defaults.randomPitch, asRandom);
        addSwitch (layout, osc (i, "PhaseRand"), name + "Random Phase", defaults.randomPhase);
        add (layout, osc (i, "Phase"), name + "Phase", { 0.0f, 1.0f }, defaults.phase, asPercent);
        add (layout, osc (i, "Gain"), name + "Wave Gain", { 0.0f, 2.0f }, defaults.gain, asGain);
        add (layout, osc (i, "Width"), name + "Pulse Width", { 0.01f, 0.99f }, defaults.pulseWidth, asPercent);
        add (layout, osc (i, "FadeIn"), name + "Fade In", { 0.0f, 5.0f }, defaults.fadeInMs, asMs);
        addSwitch (layout, osc (i, "Invert"), name + "Invert", defaults.invert);
        add (layout, osc (i, "Noise"), name + "Noise", { 0.0f, 2.0f }, defaults.noise, asGain);
        add (layout, osc (i, "NoiseColour"), name + "Noise Colour", { 0.0f, 1.0f }, defaults.noiseColour, asColour);
        addChoice (layout, osc (i, "NoiseWidth"), name + "Noise Width", synth::noiseWidthNames, (int) defaults.noiseWidth);
        add (layout, osc (i, "Sub"), name + "Sub", { 0.0f, 2.0f }, defaults.sub, asGain);
        addChoice (layout, osc (i, "SubWave"), name + "Sub Wave", synth::subWaveNames, (int) defaults.subWave);
        addChoice (layout, osc (i, "SubOct"), name + "Sub Octave", synth::subOctaveNames, defaults.subOctave);
        addSwitch (layout, osc (i, "SubInvert"), name + "Sub Invert", defaults.subInvert);
        addSwitch (layout, osc (i, "Filter"), name + "To Filter", defaults.toFilter);
        add (layout, osc (i, "Voices"), name + "Voices", { 1.0f, (float) synth::maxVoices, 1.0f }, (float) defaults.voiceLimit, asCount);

        // Stage 2: UNISON, VIBRATO and the CHORDER.
        add (layout, osc (i, "UniVoices"), name + "Unison Voices", { 2.0f, (float) synth::maxUnison, 1.0f }, (float) defaults.unisonVoices, asCount, stage2);
        add (layout, osc (i, "UniMix"), name + "Unison Mix", { 0.0f, 1.0f }, defaults.unisonMix, asPercent, stage2);
        add (layout, osc (i, "UniDetune"), name + "Unison Detune", { 0.0f, 1.0f }, defaults.unisonDetune, asDetune, stage2);
        add (layout, osc (i, "UniSpread"), name + "Unison Spread", { 0.0f, 1.0f }, defaults.unisonSpread, asPercent, stage2);
        add (layout, osc (i, "UniOctaves"), name + "Unison Octaves", { 0.0f, 3.0f, 1.0f }, (float) defaults.unisonOctaves, asOctaves, stage2);
        addChoice (layout, osc (i, "VibMode"), name + "Vibrato Mode", synth::vibratoModeNames, (int) defaults.vibratoMode, stage2);
        addChoice (layout, osc (i, "VibWave"), name + "Vibrato Wave", synth::vibratoWaveNames, (int) defaults.vibratoWave, stage2);
        add (layout, osc (i, "VibRate"), name + "Vibrato Rate", logRange (0.1, 20.0), defaults.vibratoRateHz, asHz, stage2);
        add (layout, osc (i, "VibAmount"), name + "Vibrato Amount", { 0.0f, 100.0f, 0.1f }, defaults.vibratoCents, asCentsDepth, stage2);
        add (layout, osc (i, "VibFade"), name + "Vibrato Fade In", timeRange (10000.0f), defaults.vibratoFadeMs, asMs, stage2);
        add (layout, osc (i, "VibCurve"), name + "Vibrato Fade Curve", { -1.0f, 1.0f }, defaults.vibratoCurve, asSignedPercent, stage2);
        for (int k = 0; k < synth::numChordVoices; ++k)
        {
            const auto& chord = defaults.chord[(size_t) k];
            const auto prefix = "Ch" + juce::String (k + 1);
            const auto chordName = name + "Chord " + juce::String (k + 1) + " ";
            addSwitch (layout, osc (i, (prefix + "On").toRawUTF8()), chordName + "On", chord.on, stage2);
            add (layout, osc (i, (prefix + "Coarse").toRawUTF8()), chordName + "Coarse", { -24.0f, 24.0f, 1.0f }, chord.coarse, asSemitones, stage2);
            add (layout, osc (i, (prefix + "Fine").toRawUTF8()), chordName + "Fine", { -100.0f, 100.0f, 1.0f }, chord.fineCents, asCents, stage2);
            add (layout, osc (i, (prefix + "Pan").toRawUTF8()), chordName + "Pan", { -1.0f, 1.0f }, chord.pan, asPan, stage2);
            add (layout, osc (i, (prefix + "Vol").toRawUTF8()), chordName + "Volume", { 0.0f, 2.0f }, chord.volume, asGain, stage2);
        }

        // Stage 3: the WAVETABLE. TABLE names a factory table (a table loaded
        // from a file is kept with the preset instead, and takes its place).
        addChoice (layout, osc (i, "Table"), name + "Wavetable", synth::factoryTables::names, 0, stage3);
        add (layout, osc (i, "WtIndex"), name + "Wavetable Index", { 0.0f, 1.0f }, defaults.tableIndex, asPercent, stage3);
        addChoice (layout, osc (i, "WtMode"), name + "Wavetable Mode", synth::wavetableModeNames, 0, stage3);   // SMOOTH
        add (layout, osc (i, "WtFrames"), name + "Wavetable Frames", { 2.0f, (float) synth::maxWavetableFrames, 1.0f },
             (float) defaults.tableFrames, asFrames, stage3);

        // Stage 3b: SHAPE.
        add (layout, osc (i, "Skew"), name + "Skew", { -1.0f, 1.0f }, defaults.skew, asSignedPercent, stage3b);
        add (layout, osc (i, "Pinch"), name + "Pinch", { -1.0f, 1.0f }, defaults.pinch, asSignedPercent, stage3b);
        add (layout, osc (i, "Formant"), name + "Formant", { -1.0f, 1.0f }, defaults.formant, asFormant, stage3b);
        addChoice (layout, osc (i, "FormantMode"), name + "Formant Mode", synth::formantModeNames, (int) defaults.formantMode, stage3b);
        add (layout, osc (i, "Crush"), name + "Crush", { 0.0f, 1.0f }, defaults.crush, asCrush, stage3b);
        addChoice (layout, osc (i, "CrushMode"), name + "Crush Mode", synth::crushModeNames, (int) defaults.crushMode, stage3b);

        // Stage 3c: SYNC, FM and AM.
        add (layout, osc (i, "SyncAmount"), name + "Sync", { 0.0f, 1.0f }, defaults.syncAmount, asSync, stage3c);
        addChoice (layout, osc (i, "SyncMode"), name + "Sync Mode", synth::syncModeNames, (int) defaults.syncMode, stage3c);
        add (layout, osc (i, "SyncEnv"), name + "Sync Envelope", { -1.0f, 1.0f }, defaults.syncEnvAmount, asSignedPercent, stage3c);
        add (layout, osc (i, "SyncEnvTime"), name + "Sync Envelope Length", logRange (50.0, 3500.0), defaults.syncEnvMs, asMs, stage3c);
        add (layout, osc (i, "SyncEnvCurve"), name + "Sync Envelope Curve", { -1.0f, 1.0f }, defaults.syncEnvCurve, asSignedPercent, stage3c);
        add (layout, osc (i, "FmAmount"), name + "FM", { 0.0f, 1.0f }, defaults.fmAmount, asCrush, stage3c);
        addChoice (layout, osc (i, "FmSource"), name + "FM Source", synth::fmSourceNames, defaults.fmSource, stage3c);
        add (layout, osc (i, "AmAmount"), name + "AM", { 0.0f, 1.0f }, defaults.amAmount, asCrush, stage3c);
        addChoice (layout, osc (i, "AmShape"), name + "AM Shape", synth::modShapeNames, (int) defaults.amShape, stage3c);
        // RATE's text depends on GRID, which it can't see: shown as FREE would
        // read; the oscillator's FM page shows the real ratio beside it.
        add (layout, osc (i, "ModRate"), name + "Mod Rate", { 0.0f, 1.0f }, defaults.modRate, asPercent, stage3c);
        addChoice (layout, osc (i, "ModGrid"), name + "Mod Grid", synth::modGridNames, (int) defaults.modGrid, stage3c);
        addSwitch (layout, osc (i, "ModLock"), name + "Mod Phase Lock", defaults.modPhaseLock, stage3c);

        // Stage 3d: the FFT editor's controls (its drawn curve is kept with the preset, not as parameters).
        add (layout, osc (i, "FftSweep"), name + "FFT Sweep", { -1.0f, 1.0f }, defaults.fftSweep, asSweep, stage3d);
        addSwitch (layout, osc (i, "FftFlip"), name + "FFT Flip", defaults.fftFlip, stage3d);
        add (layout, osc (i, "FftLp"), name + "FFT LP", logRange (1.0, 512.0), defaults.fftLp, asHarmonicLp, stage3d);
        add (layout, osc (i, "FftHp"), name + "FFT HP", logRange (1.0, 512.0), defaults.fftHp, asHarmonicHp, stage3d);
        add (layout, osc (i, "FftSlope"), name + "FFT Slope", { 0.0f, 1.0f }, defaults.fftSlope, asPercent, stage3d);
        add (layout, osc (i, "FftVSpeed"), name + "FFT VSpeed", logRange (0.05, 20.0), defaults.fftVSpeedHz, asHz, stage3d);
        add (layout, osc (i, "FftVStrength"), name + "FFT VStrength", { 0.0f, 1.0f }, defaults.fftVStrength, asPercent, stage3d);

        // Which SHAPERs it goes through (stage 3e): all of them unless unticked, so switching one on is heard at once.
        for (int k = 0; k < numShapers; ++k)
            addSwitch (layout, osc (i, ParamIDs::oscSettings[ParamIDs::oscSetting ("Shp1") + k]),
                       name + "To Shaper " + juce::String (k + 1), defaults.toShaper[(size_t) k], stage3e);
        // TRAVEL (stage 4e): its own movement through its wavetable.
        const auto& tr = defaults.travel;
        addSwitch (layout, osc (i, "TrOn"), name + "Travel On", tr.on, stage4e);
        addChoice (layout, osc (i, "TrEditors"), name + "Travel Editors", synth::travel::editorNames, (int) tr.editors, stage4e);
        addChoice (layout, osc (i, "TrTrigger"), name + "Travel Trigger", synth::travel::triggerNames, (int) tr.trigger, stage4e);
        addChoice (layout, osc (i, "TrMode"), name + "Travel Mode", synth::travel::modeNames, (int) tr.mode, stage4e);
        addChoice (layout, osc (i, "TrPatternMode"), name + "Travel Pattern Mode", synth::travel::modeNames, (int) tr.patternMode, stage4e);
        add (layout, osc (i, "TrSteps"), name + "Travel Steps", { 1.0f, (float) synth::travel::maxSteps, 1.0f }, (float) tr.steps, asCount, stage4e);
        add (layout, osc (i, "TrStrength"), name + "Travel Strength", { -1.0f, 1.0f }, tr.strength, asSignedPercent, stage4e);
        add (layout, osc (i, "TrVelocity"), name + "Travel Strength Velocity", { 0.0f, 1.0f }, tr.strengthVelocity, asPercent, stage4e);
        add (layout, osc (i, "TrVelCurve"), name + "Travel Velocity Curve", { -1.0f, 1.0f }, tr.velocityCurve, asSignedPercent, stage4e);
        add (layout, osc (i, "TrSmooth"), name + "Travel Smooth", { 0.0f, 1.0f }, tr.smooth, asPercent, stage4e);
        add (layout, osc (i, "TrPhase"), name + "Travel Phase", { 0.0f, 1.0f }, tr.phase, asPercent, stage4e);
        addSwitch (layout, osc (i, "TrFlip"), name + "Travel Flip", tr.flip, stage4e);
        add (layout, osc (i, "TrRate"), name + "Travel Speed", logRange (0.02, 40.0), tr.rateHz, asHz, stage4e);
        addChoice (layout, osc (i, "TrSync"), name + "Travel Sync", synth::mod::lfoSyncNames, (int) tr.sync, stage4e);
        addChoice (layout, osc (i, "TrDivision"), name + "Travel Sync Length", synth::mod::lfoDivisionNames, tr.division, stage4e);

        // Which ARP its notes come from (stage 5a).
        addChoice (layout, osc (i, "Arp"), name + "Arp", synth::arp::routeNames, defaults.arp, stage5a);

        // Its SENDS (stage 7b).
        add (layout, osc (i, "SendDelay"), name + "Send Delay", { 0.0f, 1.0f }, defaults.send[0], asPercent, stage7b);
        add (layout, osc (i, "SendReverb"), name + "Send Reverb", { 0.0f, 1.0f }, defaults.send[1], asPercent, stage7b);
        // Its INSERT bus (stage 7d).
        addChoice (layout, osc (i, "Insert"), name + "Insert", ParamIDs::insertBusNames, defaults.insertBus, stage7d);

        // Whether it follows the PITCH envelope (stage 4c).
        addSwitch (layout, osc (i, "PitchEnv"), name + "Pitch Envelope", defaults.pitchEnvelope, stage4c);
    }

    // The FILTER: off in a new sound (Avenger's starts without one), ready at a 24 dB low-pass at 2 kHz.
    const synth::FilterSettings filter;
    addSwitch (layout, fltOn, "Filter On", filter.on);
    addChoice (layout, fltType, "Filter Type", synth::filterTypeNames, (int) filter.type);
    add (layout, fltCutoff, "Filter Cutoff", logRange (20.0, 20000.0), filter.cutoffHz, asFrequency);
    add (layout, fltRes, "Filter Resonance", { 0.0f, 1.0f }, filter.resonance, asPercent);
    add (layout, fltDrive, "Filter Drive", { 0.0f, 1.0f }, filter.drive, asPercent);
    add (layout, fltKey, "Filter Keytrack", { 0.0f, 1.0f }, filter.keytrack, asPercent);
    add (layout, fltVel, "Filter Velocity", { 0.0f, 1.0f }, filter.velocity, asPercent);
    add (layout, fltResVel, "Filter Resonance Velocity", { 0.0f, 1.0f }, filter.resonanceVelocity, asPercent);
    add (layout, fltEnv, "Filter Envelope", { -1.0f, 1.0f }, filter.envelopeAmount, asSignedPercent);
    add (layout, fltEnvVel, "Filter Envelope Velocity", { 0.0f, 1.0f }, filter.envelopeVelocity, asPercent);
    addEnvelope (layout, "Filter", fltAttack, fltHold, fltDecay, fltSustain, fltRelease,
                 fltAttackCurve, fltDecayCurve, fltReleaseCurve, filter.envelope);

    // The SHAPERs (stage 3e): four, off in a new sound.
    const synth::ShaperSettings shaper;
    for (int k = 0; k < numShapers; ++k)
    {
        const auto name = "Shaper " + juce::String (k + 1) + " ";
        addSwitch (layout, shp (k, "On"), name + "On", shaper.on, stage3e);
        addChoice (layout, shp (k, "Model"), name + "Model", synth::shaperModelNames, (int) shaper.model, stage3e);
        add (layout, shp (k, "Drive"), name + "Drive", { 0.0f, 1.0f }, shaper.drive, asShaperDrive, stage3e);
        add (layout, shp (k, "DriveVel"), name + "Drive Velocity", { 0.0f, 1.0f }, shaper.driveVelocity, asPercent, stage3e);
        for (int b = 0; b < 2; ++b)
        {
            const auto band = juce::String (b + 1);
            add (layout, shp (k, ("Eq" + band + "Gain").toRawUTF8()), name + "EQ " + band + " Gain", { -18.0f, 18.0f, 0.1f },
                 shaper.eqGainDb[(size_t) b], asEqGain, stage3e);
            add (layout, shp (k, ("Eq" + band + "Freq").toRawUTF8()), name + "EQ " + band + " Frequency", logRange (20.0, 20000.0),
                 shaper.eqFreqHz[(size_t) b], asFrequency, stage3e);
            add (layout, shp (k, ("Eq" + band + "Q").toRawUTF8()), name + "EQ " + band + " Q", logRange (0.2, 12.0),
                 shaper.eqQ[(size_t) b], asQ, stage3e);
        }
        addSwitch (layout, shp (k, "EqSplit"), name + "EQ Split", shaper.eqSplit, stage3e);
        addChoice (layout, shp (k, "Place"), name + "Place", synth::shaperPlaceNames, shaper.afterFilter ? 1 : 0, stage3e);
        add (layout, shp (k, "Mix"), name + "Mix", { 0.0f, 1.0f }, shaper.mix, asPercent, stage3e);
        add (layout, shp (k, "Output"), name + "Output", { -24.0f, 12.0f, 0.1f }, shaper.outputDb, asDb, stage3e);
    }

    // MODULATION (stage 4a): the LFOs and macros.
    const synth::mod::LfoSettings lfoDefaults;
    for (int k = 0; k < numLfos; ++k)
    {
        const auto name = "LFO " + juce::String (k + 1) + " ";
        addChoice (layout, lfo (k, "Wave"), name + "Wave", synth::mod::lfoWaveNames, (int) lfoDefaults.wave, stage4a);
        add (layout, lfo (k, "Rate"), name + "Rate", logRange (0.02, 40.0), lfoDefaults.rateHz, asHz, stage4a);
        addChoice (layout, lfo (k, "Sync"), name + "Sync", synth::mod::lfoSyncNames, (int) lfoDefaults.sync, stage4a);
        addChoice (layout, lfo (k, "Division"), name + "Sync Length", synth::mod::lfoDivisionNames, lfoDefaults.division, stage4a);
        addChoice (layout, lfo (k, "Trigger"), name + "Trigger", synth::mod::lfoTriggerNames, (int) lfoDefaults.trigger, stage4a);
        add (layout, lfo (k, "Phase"), name + "Phase", { 0.0f, 1.0f }, lfoDefaults.phase, asPercent, stage4a);
        add (layout, lfo (k, "Offset"), name + "Offset", { -1.0f, 1.0f }, lfoDefaults.offset, asSignedPercent, stage4a);
        add (layout, lfo (k, "Delay"), name + "Delay", timeRange (10000.0f), lfoDefaults.delayMs, asMs, stage4a);
        add (layout, lfo (k, "FadeIn"), name + "Fade In", timeRange (10000.0f), lfoDefaults.fadeInMs, asMs, stage4a);
        addSwitch (layout, lfo (k, "OneShot"), name + "One Shot", lfoDefaults.oneShot, stage4a);
    }
    const synth::mod::EnvelopeSettings envelopeDefaults;
    for (int k = 0; k < numEnvelopes; ++k)
    {
        const auto name = "Mod Env " + juce::String (k + 1) + " ";
        addChoice (layout, env (k, "Mode"), name + "Mode", synth::mod::envelopeModeNames, (int) envelopeDefaults.mode, stage4b);
        addChoice (layout, env (k, "Trigger"), name + "Trigger", synth::mod::envelopeTriggerNames, (int) envelopeDefaults.trigger, stage4b);
        addSwitch (layout, env (k, "Sync"), name + "Sync", envelopeDefaults.sync, stage4b);
        add (layout, env (k, "Speed"), name + "Speed", logRange (0.1, 10.0), envelopeDefaults.speed, asTimes, stage4b);
        add (layout, env (k, "Steps"), name + "Steps", { 0.0f, 32.0f, 1.0f }, (float) envelopeDefaults.steps, asSteps, stage4b);
        addSwitch (layout, env (k, "Flip"), name + "Flip", envelopeDefaults.flip, stage4b);
        add (layout, env (k, "Offset"), name + "Offset", { 0.0f, 1.0f }, envelopeDefaults.offset, asPercent, stage4b);
    }
    // The PITCH module (stage 4c).
    const synth::PitchSettings pitch;
    addSwitch (layout, pitchEnvOn, "Pitch Envelope On", pitch.envelopeOn, stage4c);
    addChoice (layout, pitchEnvMode, "Pitch Envelope Mode", synth::mod::envelopeModeNames, (int) pitch.envelope.mode, stage4c);
    add (layout, pitchEnvSpeed, "Pitch Envelope Speed", logRange (0.1, 10.0), pitch.envelope.speed, asTimes, stage4c);
    add (layout, pitchEnvRange, "Pitch Envelope Range", { 0.0f, 24.0f, 1.0f }, pitch.rangeSemitones, asRange, stage4c);
    add (layout, pitchEnvSteps, "Pitch Envelope Steps", { 0.0f, 32.0f, 1.0f }, (float) pitch.envelope.steps, asSteps, stage4c);
    addSwitch (layout, pitchEnvFlip, "Pitch Envelope Flip", pitch.envelope.flip, stage4c);
    add (layout, pitchKeytrack, "Pitch Keytrack", { -1.0f, 1.0f }, pitch.keytrack, asSignedPercent, stage4c);
    Range lagRange { 0.0f, 1000.0f };
    lagRange.setSkewForCentre (100.0f);
    add (layout, bendLag, "Pitch Bend Lag", lagRange, pitch.bendLagMs, asMs, stage4c);
    add (layout, glideCurve, "Glide Curve", { -1.0f, 1.0f }, pitch.glideCurve, asSignedPercent, stage4c);
    add (layout, glideSplit, "Glide Split", { 0.0f, 127.0f, 1.0f }, (float) pitch.glideSplit, asSplit, stage4c);

    // The STEP SEQUENCER (stage 4d). Its steps are kept with the preset (ModMatrix).
    const synth::StepSeqSettings stepSeq;
    addSwitch (layout, seqOn, "Step Seq On", stepSeq.on, stage4d);
    addChoice (layout, seqTrigger, "Step Seq Trigger", synth::seq::triggerNames, (int) stepSeq.trigger, stage4d);
    addChoice (layout, seqDivision, "Step Seq Speed", synth::seq::divisionNames, stepSeq.division, stage4d);
    addChoice (layout, seqTiming, "Step Seq Timing", synth::seq::timingNames, (int) stepSeq.timing, stage4d);
    addChoice (layout, seqMode, "Step Seq Mode", synth::seq::modeNames, (int) stepSeq.mode, stage4d);
    add (layout, seqLength, "Step Seq Length", { 2.0f, (float) synth::seq::maxSteps, 1.0f }, (float) stepSeq.length, asCount, stage4d);
    add (layout, seqContour, "Step Seq Contour", { 0.0f, 1.0f }, stepSeq.contour, asPercent, stage4d);
    add (layout, seqGate, "Step Seq Gate", { 0.01f, 1.0f }, stepSeq.gate, asPercent, stage4d);
    add (layout, seqVolMix, "Step Seq Vol Mix", { 0.0f, 1.0f }, stepSeq.volMix, asPercent, stage4d);
    add (layout, seqWidth, "Step Seq Stereo Width", { 0.0f, 1.0f }, stepSeq.width, asPercent, stage4d);
    add (layout, seqDecay, "Step Seq Decay", timeRange (4000.0f), stepSeq.decayMs, asDecay, stage4d);
    add (layout, seqFadeIn, "Step Seq Fade In", timeRange (10000.0f), stepSeq.fadeInMs, asMs, stage4d);
    add (layout, seqDelay, "Step Seq Delay", timeRange (10000.0f), stepSeq.delayMs, asMs, stage4d);
    add (layout, seqLowKey, "Step Seq Start Note", { 0.0f, 127.0f, 1.0f }, (float) stepSeq.lowKey, asNote, stage4d);
    add (layout, seqHighKey, "Step Seq End Note", { 0.0f, 127.0f, 1.0f }, (float) stepSeq.highKey, asNote, stage4d);
    add (layout, shuffle, "Shuffle", { 0.0f, 1.0f }, stepSeq.shuffle, asPercent, stage4d);

    for (int k = 0; k < 3; ++k)
        add (layout, macros[k], "Macro " + juce::String (k + 1), { 0.0f, 1.0f }, 0.0f, asPercent, stage4a);
    // The ARPEGGIATORS (stage 5a).
    const synth::ArpSettings arpDefaults;
    for (int k = 0; k < numArps; ++k)
    {
        const auto name = "Arp " + juce::String (k + 1) + " ";
        addSwitch (layout, arp (k, "On"), name + "On", arpDefaults.on, stage5a);
        addChoice (layout, arp (k, "Mode"), name + "Mode", synth::arp::modeNames, (int) arpDefaults.mode, stage5a);
        addChoice (layout, arp (k, "Division"), name + "Speed", synth::seq::divisionNames, arpDefaults.division, stage5a);
        addChoice (layout, arp (k, "Timing"), name + "Timing", synth::seq::timingNames, (int) arpDefaults.timing, stage5a);
        add (layout, arp (k, "Octaves"), name + "Octaves", { 1.0f, 4.0f, 1.0f }, (float) arpDefaults.octaves, asCount, stage5a);
        add (layout, arp (k, "Gate"), name + "Gate", { 0.05f, 1.0f }, arpDefaults.gate, asPercent, stage5a);
        add (layout, arp (k, "Length"), name + "Length", { 2.0f, (float) synth::arp::maxSteps, 1.0f }, (float) arpDefaults.length, asCount, stage5a);
        addChoice (layout, arp (k, "Velocity"), name + "Velocity", synth::arp::velocityModeNames, (int) arpDefaults.velocityMode, stage5a);
        add (layout, arp (k, "LowKey"), name + "Start Note", { 0.0f, 127.0f, 1.0f }, (float) arpDefaults.lowKey, asNote, stage5a);
        add (layout, arp (k, "HighKey"), name + "End Note", { 0.0f, 127.0f, 1.0f }, (float) arpDefaults.highKey, asNote, stage5a);
        add (layout, arp (k, "Humanize"), name + "Humanize", { 0.0f, 1.0f }, arpDefaults.humanize, asPercent, stage5a);
        add (layout, arp (k, "Strum"), name + "Strum", { 0.0f, 1.0f }, arpDefaults.strum, asPercent, stage5a);
        addChoice (layout, arp (k, "Pattern"), name + "Pattern", synth::arp::patternNames, arpDefaults.pattern, stage5a);
        addSwitch (layout, arp (k, "Chain"), name + "Chain", arpDefaults.chain, stage5a);
        addChoice (layout, arp (k, "Trigger"), name + "Trigger", synth::arp::triggerNames, (int) arpDefaults.trigger, stage5a);
        addChoice (layout, arp (k, "NoteSource"), name + "Note Source", synth::arp::noteSourceNames, (int) arpDefaults.noteSource, stage5b);
    }
    addSwitch (layout, arpsTogether, "Arps Together", synth::Settings{}.arpsTogether, stage5b);

    // THE DRUMS (stage 6a). Off by default, so the keys play the synth until the kit is switched on.
    {
        const ToText asPitch = [] (float value, int)
        {
            const auto rounded = std::round (value);
            const auto text = std::abs (value - rounded) < 0.005f ? juce::String ((int) rounded) : juce::String (value, 2);
            return (value > 0.004f ? "+" : "") + text + " st";
        };
        const ToText asLevel = [] (float db, int) { return db <= -47.95f ? juce::String ("-inf") : juce::String (db, 1) + " dB"; };
        const ToText asShare = [] (float value, int) { return juce::String (value * 100.0f, 2) + "%"; };   // of the sample
        // DELAY: later to the right; to the left, the anchor (how far into the hit lands on the step), ms either way.
        juce::NormalisableRange<float> delayRange (-1000.0f, 1000.0f, 0.1f, 0.35f, true);
        const ToText asDrumDelay = [] (float ms, int)
        {
            if (std::abs (ms) < 0.05f)
                return juce::String ("0 ms");
            const auto text = juce::String (std::abs (ms), std::abs (ms) < 10.0f ? 1 : 0) + " ms";
            return ms > 0.0f ? "+" + text : "Anchor " + text;
        };
        const ToText asLowPass = [] (float hz, int) { return hz >= 19999.0f ? juce::String ("Off") : asFrequency (hz, 0); };
        const ToText asHighPass = [] (float hz, int) { return hz <= 20.01f ? juce::String ("Off") : asFrequency (hz, 0); };
        addSwitch (layout, drumsOn, "Drums On", false, stage6a);
        add (layout, drumsRoot, "Drums Root Key", { 0.0f, 116.0f, 1.0f }, 36.0f, asNote, stage6a);
        add (layout, drumsLevel, "Drums Level", { -24.0f, 12.0f, 0.1f }, 0.0f, asDb, stage6a);
        add (layout, drumsVelocity, "Drums Velocity", { 0.0f, 1.0f }, 1.0f, asPercent, stage6a);
        for (int k = 0; k < numDrums; ++k)
        {
            const auto& d = synth::drums::slotDefaults[(size_t) k];
            const auto name = "Drum " + juce::String (k + 1) + " ";
            addChoice (layout, drum (k, "Model"), name + "Model", synth::drums::modelNames, (int) d.model, stage6a);
            add (layout, drum (k, "Level"), name + "Volume", { -48.0f, 12.0f, 0.1f }, 0.0f, asLevel, stage6a);
            add (layout, drum (k, "Pan"), name + "Pan", { -1.0f, 1.0f }, 0.0f, asPan, stage6a);
            add (layout, drum (k, "Pitch"), name + "Pitch", { -24.0f, 24.0f, 0.01f }, d.pitch, asPitch, stage6a);
            add (layout, drum (k, "Decay"), name + "Decay", { 0.0f, 1.0f }, d.decay, asPercent, stage6a);
            add (layout, drum (k, "Tone"), name + "Tone", { 0.0f, 1.0f }, d.tone, asPercent, stage6a);
            add (layout, drum (k, "Snap"), name + "Snap", { 0.0f, 1.0f }, d.snap, asPercent, stage6a);
            addSwitch (layout, drum (k, "Mute"), name + "Mute", false, stage6a);
            addSwitch (layout, drum (k, "Solo"), name + "Solo", false, stage6a);
            addChoice (layout, drum (k, "Choke"), name + "Choke", synth::drums::chokeNames, d.choke, stage6a);
            addChoice (layout, drum (k, "Insert"), name + "Insert", insertBusNames, 0, stage6a);
            add (layout, drum (k, "SendDelay"), name + "Send Delay", { 0.0f, 1.0f }, 1.0f, asPercent, stage6a);
            add (layout, drum (k, "SendReverb"), name + "Send Reverb", { 0.0f, 1.0f }, 1.0f, asPercent, stage6a);
            add (layout, drum (k, "SampleStart"), name + "Sample Start", { 0.0f, 1.0f }, 0.0f, asShare, stage6aStart);
            add (layout, drum (k, "SampleEnd"), name + "Sample End", { 0.0f, 1.0f }, 1.0f, asShare, stage6aStart);
            // THE SAMPLE EDITOR (6c).
            add (layout, drum (k, "Gain"), name + "Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, asDb, stage6c);
            addSwitch (layout, drum (k, "Mono"), name + "Mono", false, stage6c);
            add (layout, drum (k, "Delay"), name + "Delay", delayRange, 0.0f, asDrumDelay, stage6c);
            add (layout, drum (k, "LowPass"), name + "Low Pass", logRange (20.0, 20000.0), 20000.0f, asLowPass, stage6c);
            add (layout, drum (k, "HighPass"), name + "High Pass", logRange (20.0, 20000.0), 20.0f, asHighPass, stage6c);
        }

        // THE DRUM SEQUENCER (stage 6b).
        const synth::drumseq::Settings sequencer;
        addSwitch (layout, dseqOn, "Drum Seq On", sequencer.on, stage6b);
        addChoice (layout, dseqTrigger, "Drum Seq Trigger", synth::drumseq::triggerNames, (int) sequencer.trigger, stage6b);
        addChoice (layout, dseqPattern, "Drum Seq Pattern", synth::drumseq::patternNames, sequencer.pattern, stage6b);
        addChoice (layout, dseqDivision, "Drum Seq Speed", synth::seq::divisionNames, sequencer.division, stage6b);
        addChoice (layout, dseqTiming, "Drum Seq Timing", synth::seq::timingNames, (int) sequencer.timing, stage6b);
        add (layout, dseqLength, "Drum Seq Length", { 1.0f, (float) synth::drumseq::maxBeats, 1.0f }, (float) sequencer.lengthBeats,
             [] (float value, int) { return juce::String (juce::roundToInt (value)) + (juce::roundToInt (value) == 1 ? " beat" : " beats"); },
             stage6b);
        add (layout, dseqVelocity, "Drum Seq Velocity Amount", { 0.0f, 1.0f }, sequencer.velocityAmount, asPercent, stage6b);
    }

    // CONTROLLER TO MOD WHEEL (stage 4f): global, like OUTPUT.
    addSwitch (layout, mwBreath, "Breath To Mod Wheel", false, stage4f);
    addSwitch (layout, mwExpression, "Expression To Mod Wheel", false, stage4f);
    addSwitch (layout, mwAftertouch, "Aftertouch To Mod Wheel", false, stage4f);
    addSwitch (layout, mwSustain, "Sustain To Mod Wheel", false, stage4f);

    for (int k = 0; k < 2; ++k)
        addSwitch (layout, macroButtons[k], "Macro Button " + juce::String (k + 1), false, stage4a);

    // The AMP.
    const synth::AmpSettings amp;
    add (layout, ampVolume, "Amp Volume", { 0.0f, 2.0f }, amp.volume, asGain);
    add (layout, ampVel, "Amp Velocity", { 0.0f, 1.0f }, amp.velocity, asPercent);
    add (layout, ampPan, "Amp Pan", { -1.0f, 1.0f }, amp.pan, asPan);
    add (layout, ampPanKey, "Amp Pan Keytrack", { -1.0f, 1.0f }, amp.panKeytrack, asSignedPercent);
    add (layout, ampSpread, "Amp Spread", { -1.0f, 1.0f }, amp.spread, asSignedPercent);
    add (layout, ampSpike, "Amp Spike", { 0.0f, 1.0f }, amp.spike, asPercent);
    add (layout, ampSpikeCurve, "Amp Spike Curve", { -1.0f, 1.0f }, amp.spikeCurve, asSignedPercent);
    add (layout, ampSpikeVel, "Amp Spike Velocity", { 0.0f, 1.0f }, amp.spikeVelocity, asPercent);
    addEnvelope (layout, "Amp", ampAttack, ampHold, ampDecay, ampSustain, ampRelease,
                 ampAttackCurve, ampDecayCurve, ampReleaseCurve, amp.envelope);

    // How it plays.
    const synth::PlaySettings play;
    addChoice (layout, playMode, "Play Mode", synth::playModeNames, (int) play.mode);
    addSwitch (layout, legato, "Legato", play.legato);
    Range glideRange { 0.0f, 5000.0f };
    glideRange.setSkewForCentre (250.0f);
    add (layout, glide, "Glide", glideRange, play.glideMs, asMs);
    addChoice (layout, glideMode, "Glide Mode", synth::glideModeNames, (int) play.glideMode);
    add (layout, bendUp, "Bend Up", { 0.0f, 48.0f, 1.0f }, play.bendUp, asSemitones);
    add (layout, bendDown, "Bend Down", { 0.0f, 48.0f, 1.0f }, play.bendDown, asSemitones);
    add (layout, voices, "Voices", { 1.0f, (float) synth::maxVoices, 1.0f }, (float) play.voices, asCount);

    addRackParameters (layout);   // the rack (stage 7a): RackParameters.cpp
    addSendParameters (layout);   // the sends (stage 7b): RackParameters.cpp
    addModFxParameters (layout);  // MOD, MOD 2, LO-FI (stage 7c): RackParameters.cpp
    addInsertParameters (layout); // the insert buses (stage 7d): RackParameters.cpp
    return layout;
}
