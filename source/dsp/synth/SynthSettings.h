#pragma once

#include "Envelope.h"
#include "Filter.h"

#include <array>

// Everything the synth engine needs to know about the sound, in real units.
// The processor fills one from the parameters each block; the engine never
// reads parameters itself (so it can be tested on its own).
namespace synth
{
    inline constexpr int numOscillators = 8;   // as on Avenger: up to 8 oscillator tabs
    inline constexpr int maxVoices = 64;       // notes at once

    class Wavetable;

    // Saved by index (osc*Wave): only ever append. WAVETABLE came in stage 3 (0.3.0).
    enum class OscWave { saw, pulse, triangle, sine, wavetable };
    inline const juce::StringArray oscWaveNames { "Saw", "Pulse", "Triangle", "Sine", "Wavetable" };

    // Saved by index (osc*WtMode): only ever append. How INDEX moves between
    // frames: SMOOTH blends the two nearest, STEPPY jumps from one to the next.
    inline const juce::StringArray wavetableModeNames { "Smooth", "Steppy" };
    inline constexpr int maxWavetableFrames = 256;

    // Saved by index (osc*SubWave): only ever append.
    enum class SubWave { sine, triangle, square, saw };
    inline const juce::StringArray subWaveNames { "Sine", "Triangle", "Square", "Saw" };

    // Saved by index (osc*SubOct): only ever append. How far below the note the sub plays.
    inline const juce::StringArray subOctaveNames { "-2 Oct", "-1 Oct", "0 Oct" };

    // Saved by index (osc*NoiseWidth): only ever append.
    enum class NoiseWidth { stereo, half, mono };
    inline const juce::StringArray noiseWidthNames { "Stereo", "Half", "Mono" };

    // Saved by index (osc*VibMode): only ever append. GLOBAL: every copy of the
    // oscillator wobbles together; INDEPENDENT: each on its own (wider).
    enum class VibratoMode { global, independent };
    inline const juce::StringArray vibratoModeNames { "Global", "Indep." };   // INDEPENDENT, short to fit its menu

    // Saved by index (osc*VibWave): only ever append.
    enum class VibratoWave { sine, triangle, square, saw, random };
    inline const juce::StringArray vibratoWaveNames { "Sine", "Triangle", "Square", "Saw", "Random" };

    inline constexpr int maxUnison = 7;         // copies of each note, as Avenger's V-Saw
    inline constexpr int numChordVoices = 4;    // the chorder's extra notes
    inline constexpr float maxDetuneCents = 100.0f;   // UNISON DETUNE at 100%: the outer copies a semitone either side (our choice)

    // One of the chorder's extra notes.
    struct ChordVoice
    {
        bool on = false;
        float coarse = 0.0f;        // semitones, -24 to 24
        float fineCents = 0.0f;     // -100 to 100
        float pan = 0.0f;           // -1 to 1, added to the oscillator's
        float volume = 1.0f;        // 0 to 2
    };

    // Saved by index (playMode): only ever append.
    enum class PlayMode { poly, mono, monoLow, monoHigh };
    inline const juce::StringArray playModeNames { "Poly", "Mono", "Mono Low", "Mono High" };

    // Saved by index (glideMode): only ever append.
    enum class GlideMode { always, legato };
    inline const juce::StringArray glideModeNames { "Always", "Legato" };

    struct OscSettings
    {
        bool on = false;
        OscWave wave = OscWave::saw;
        float volume = 1.0f;        // the whole oscillator (wave, noise and sub), 0 to 2
        float pan = 0.0f;           // -1 left to 1 right
        float transpose = 0.0f;     // semitones, -48 to 48
        float fineCents = 0.0f;     // -100 to 100
        float randomPitch = 0.0f;   // -1 to 1: below 0 a random pitch each note, above 0 alternating; up to a semitone
        bool randomPhase = true;    // start each note at a random point in the wave...
        float phase = 0.0f;         // ...or here, 0 to 1
        float gain = 1.0f;          // the wave alone, 0 to 2 (0 leaves the noise and sub)
        float pulseWidth = 0.5f;    // PULSE's, 0.01 to 0.99
        float fadeInMs = 0.0f;      // a tiny fade at the start of each note against clicks, 0 to 5
        bool invert = false;        // the wave and sub upside down
        float noise = 0.0f;         // 0 to 2
        float noiseColour = 0.0f;   // 0 white, 0.5 pink, 1 brown
        NoiseWidth noiseWidth = NoiseWidth::stereo;
        float sub = 0.0f;           // 0 to 2
        SubWave subWave = SubWave::sine;
        int subOctave = 1;          // index into subOctaveNames
        bool subInvert = false;
        bool toFilter = true;       // through the FILTER (off: straight to the AMP)
        int voiceLimit = maxVoices; // this oscillator's own notes-at-once limit

        // UNISON (stage 2, as Avenger's V-Saw): copies of each note, detuned and spread.
        int unisonVoices = maxUnison;   // 2 to 7 copies in all, the note itself included...
        float unisonMix = 0.0f;         // ...the extra copies' level, 0 (off: they cost nothing) to 1 (as loud as the note)
        float unisonDetune = 0.3f;      // 0 to 1: how far apart they're tuned (1: the outer ones maxDetuneCents away)
        float unisonSpread = 0.5f;      // 0 to 1: how wide across the stereo field
        int unisonOctaves = 0;          // 0 to 3: the extra copies stacked this many octaves up, in turn

        // VIBRATO on every copy of the oscillator.
        VibratoMode vibratoMode = VibratoMode::global;
        VibratoWave vibratoWave = VibratoWave::sine;
        float vibratoRateHz = 5.0f;
        float vibratoCents = 0.0f;      // depth either side, 0 (off) to 100
        float vibratoFadeMs = 0.0f;     // time to reach full depth after the note starts
        float vibratoCurve = 0.0f;      // -1 to 1: the fade's shape

        // CHORDER: up to 4 extra notes for each key, each with its own unison copies.
        std::array<ChordVoice, numChordVoices> chord { { { false, 12.0f }, { false, 7.0f }, { false, -12.0f }, { false, 4.0f } } };

        // WAVETABLE (stage 3): the table (the factory's, or one loaded from a
        // file; owned by the processor, which keeps it alive while it plays),
        // where in it (INDEX, 0 to 1), how it moves, and how many of its frames
        // INDEX steps through (FRAMES, 2 to 256, spread evenly).
        const Wavetable* table = nullptr;
        float tableIndex = 0.0f;
        bool tableSteppy = false;
        int tableFrames = maxWavetableFrames;

        bool hasUnison() const noexcept { return unisonMix > 0.0f && unisonVoices > 1; }
    };

    struct FilterSettings
    {
        bool on = false;            // a new sound has none (as on Avenger, where a filter is added when wanted)
        FilterType type = FilterType::lp24;
        float cutoffHz = 2000.0f;
        float resonance = 0.0f;      // 0 to 1
        float drive = 0.0f;          // 0 to 1
        float keytrack = 0.0f;       // 0 to 1: 1 follows the keyboard exactly
        float velocity = 0.0f;       // 0 to 1: how much softer notes close the cutoff
        float resonanceVelocity = 0.0f;
        float envelopeAmount = 0.0f; // -1 to 1
        float envelopeVelocity = 0.0f;
        EnvelopeSettings envelope { 0.0f, 0.0f, 500.0f, 0.0f, 300.0f };
    };

    struct AmpSettings
    {
        float volume = 1.0f;        // 0 to 2
        float velocity = 1.0f;      // 0 to 1: 1 follows velocity fully
        float pan = 0.0f;
        float panKeytrack = 0.0f;   // -1 to 1: low notes left (or right), high notes the other way
        float spread = 0.0f;        // -1 random pan per note to 1 alternating left and right
        float spike = 0.0f;         // 0 to 1: an extra click of level at the start of each note
        float spikeCurve = 0.0f;    // -1 to 1
        float spikeVelocity = 0.0f; // 0 to 1
        EnvelopeSettings envelope;
    };

    struct PlaySettings
    {
        PlayMode mode = PlayMode::poly;
        bool legato = true;         // mono: an overlapping note glides on without restarting the envelopes
        float glideMs = 0.0f;
        GlideMode glideMode = GlideMode::always;
        float bendUp = 2.0f, bendDown = 2.0f;   // semitones
        int voices = 32;            // notes at once (Avenger's "global maximum voices")
        float tuneCents = 0.0f;     // the whole synth, -100 to 100
    };

    struct Settings
    {
        std::array<OscSettings, numOscillators> oscillators {};
        FilterSettings filter;
        AmpSettings amp;
        PlaySettings play;

        Settings() { oscillators[0].on = true; }
    };
}
