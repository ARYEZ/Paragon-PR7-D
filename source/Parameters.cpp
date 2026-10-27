#include "Parameters.h"

#include "dsp/synth/SynthSettings.h"
#include "dsp/synth/Wavetable.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    // Version hint: the PR7-K version that introduced a parameter (1 for 0.1.0, 2 for 0.2.0, 3 for 0.3.0, 4 for 0.4.0).
    constexpr int firstVersion = 1;
    constexpr int stage2 = 2;
    constexpr int stage3 = 3;
    constexpr int stage3b = 4;   // 0.4.0

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

    return layout;
}
