#pragma once

#include "Envelope.h"
#include "Filter.h"
#include "Shaper.h"
#include "Modulation.h"
#include "StepSequencer.h"
#include "WtTravel.h"
#include "Arpeggiator.h"

#include <array>

// Everything the synth engine needs to know about the sound, in real units.
// The processor fills one from the parameters each block; the engine never
// reads parameters itself (so it can be tested on its own).
namespace synth
{
    inline constexpr int numOscillators = 8;   // as on Avenger: up to 8 oscillator tabs
    inline constexpr int numSends = 2;         // stage 7b: the send effects, DELAY and REVERB
    inline constexpr int numInsertBuses = 4;   // stage 7d: the insert buses A to D
    inline constexpr int maxVoices = 64;       // notes at once

    class Wavetable;

    // Saved by index (osc*Wave): only ever append. WAVETABLE came in stage 3 (0.3.0), FREEFORM in 0.7.0.
    enum class OscWave { saw, pulse, triangle, sine, wavetable, freeform };
    inline const juce::StringArray oscWaveNames { "Saw", "Pulse", "Triangle", "Sine", "Wavetable", "Freeform" };

    // Saved by index (osc*WtMode): only ever append. How INDEX moves between
    // frames: SMOOTH blends the two nearest, STEPPY jumps from one to the next.
    inline const juce::StringArray wavetableModeNames { "Smooth", "Steppy" };
    inline constexpr int maxWavetableFrames = 256;

    // Saved by index (osc*FormantMode, osc*CrushMode): only ever append. SHAPE (stage 3b, 0.4.0).
    enum class FormantMode { cut, fade, crossfade, crossfade2 };
    inline const juce::StringArray formantModeNames { "Cut", "Fade", "X-Fade", "X-Fade 2" };
    enum class CrushMode { rate, bit, both };
    inline const juce::StringArray crushModeNames { "Rate", "Bit", "Both" };

    // Saved by index: only ever append. SYNC, FM and AM (stage 3c, 0.5.0).
    // SYNC: PURE plays the synced copy alone, ADD with the wave; WIN fades
    // the copy in and out each cycle (no edge).
    enum class SyncMode { pure, add, pureWindowed, addWindowed };
    inline const juce::StringArray syncModeNames { "Pure", "Add", "Pure Win", "Add Win" };
    // The modulator's shapes (AM), and FM's: those, or another oscillator.
    enum class ModShape { sine, triangle, saw, square };
    inline const juce::StringArray modShapeNames { "Sine", "Triangle", "Saw", "Square" };
    inline const juce::StringArray fmSourceNames { "Sine", "Triangle", "Saw", "Square",
                                                   "Osc 1", "Osc 2", "Osc 3", "Osc 4", "Osc 5", "Osc 6", "Osc 7", "Osc 8" };
    inline constexpr int fmFirstOscillator = 4;   // fmSourceNames' index of "Osc 1"
    // Which rates the modulator's RATE allows (as Avenger's): FREE any; the
    // others steps of whole intervals above the note, which sound cleaner.
    enum class ModGrid { free, harmonic, semitone, octave, octaveMinor, octaveMajor, octaveFourth, octaveFifth };
    inline const juce::StringArray modGridNames { "Free", "Harmonic", "Semi", "Octave", "Oct+Min3", "Oct+Maj3", "Oct+4th", "Oct+5th" };

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
        // Through which SHAPERs (stage 3e), when they're on: all four, unless unticked.
        std::array<bool, numShapers> toShaper { true, true, true, true };
        // Follows the PITCH module's envelope (stage 4c), when it's on.
        bool pitchEnvelope = true;
        int voiceLimit = maxVoices; // this oscillator's own notes-at-once limit
        int arp = 0;                // stage 5a: 0 straight from the keys; 1 to 8 its notes come from ARP n (when that arp is on)
        // Stage 7b: how much goes to each send (DELAY, REVERB), 0 to 1, after the AMP (and the note's pan).
        std::array<float, numSends> send { 1.0f, 1.0f };
        // Stage 7d: the insert bus it plays into, after the AMP and pan (0: none, 1 to 4: A to D). On a bus, the bus's
        // own sends feed DELAY and REVERB (after its effects), not `send`.
        int insertBus = 0;

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
        // file; or FREEFORM's drawing, a one-frame table; owned by the
        // processor, which keeps it alive while it plays),
        // where in it (INDEX, 0 to 1), how it moves, and how many of its frames
        // INDEX steps through (FRAMES, 2 to 256, spread evenly).
        const Wavetable* table = nullptr;
        float tableIndex = 0.0f;
        bool tableSteppy = false;
        int tableFrames = maxWavetableFrames;
        // TRAVEL (stage 4e): the oscillator's own movement through its wavetable.
        TravelSettings travel;

        // SHAPE (stage 3b): bending the wave as it plays (see WaveBend.h). All
        // at 0: off, costing nothing.
        float skew = 0.0f;          // -1 to 1
        float pinch = 0.0f;         // -1 to 1
        float formant = 0.0f;       // -1 (8x faster, a gap after) to 1 (4x slower, only the start plays)
        FormantMode formantMode = FormantMode::fade;
        float crush = 0.0f;         // 0 to 1
        CrushMode crushMode = CrushMode::rate;

        bool hasBend() const noexcept
        {
            return ! juce::exactlyEqual (skew, 0.0f) || ! juce::exactlyEqual (pinch, 0.0f) || ! juce::exactlyEqual (formant, 0.0f) || crush > 0.0f;
        }

        // SYNC, FM and AM (stage 3c; see OscModulation.h). All at 0: off, costing nothing.
        float syncAmount = 0.0f;        // 0 to 1: the synced copy 0 to 5 octaves above the note
        SyncMode syncMode = SyncMode::pure;
        float syncEnvAmount = 0.0f;     // -1 to 1: SYNC's own envelope, added to it
        float syncEnvMs = 500.0f;       // 50 to 3500: how long it takes to fall
        float syncEnvCurve = 0.0f;      // -1 to 1
        float fmAmount = 0.0f;          // 0 to 1
        int fmSource = 0;               // index into fmSourceNames
        float amAmount = 0.0f;          // 0 to 1
        ModShape amShape = ModShape::sine;
        float modRate = 0.5f;           // 0 to 1: the modulator's RATE knob (see modRatio)
        ModGrid modGrid = ModGrid::harmonic;
        bool modPhaseLock = true;       // the modulator starts with the wave (locked) or anywhere

        // FFT (stage 3d; see Spectrum.h). The edited copy of this oscillator's
        // wave, built by the processor (null: not edited), and VSTRENGTH's
        // other copy (null: no pulsing), which it swings to at VSPEED.
        const Wavetable* spectral = nullptr;
        const Wavetable* spectralPulse = nullptr;
        float fftSweep = 0.0f;          // -1 to 1 (baked into `spectral`)
        bool fftFlip = false;           // (baked)
        float fftVSpeedHz = 1.0f;       // 0.05 to 20
        float fftVStrength = 0.0f;      // 0 to 1 (baked into `spectralPulse`)
        // LP and HP: live, by harmonic number (512: LP off; 1: HP off); SLOPE 0 steep to 1 gentle.
        float fftLp = 512.0f;
        float fftHp = 1.0f;
        float fftSlope = 0.0f;
        bool hasFftFilter() const noexcept { return fftLp < 511.5f || fftHp > 1.01f; }

        bool hasSync() const noexcept { return syncAmount > 0.0f || ! juce::exactlyEqual (syncEnvAmount, 0.0f); }
        bool hasFm() const noexcept { return fmAmount > 0.0f; }
        bool hasAm() const noexcept { return amAmount > 0.0f; }
        bool hasModulation() const noexcept { return hasSync() || hasFm() || hasAm(); }

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

    // The PITCH module (stage 4c), modelled on Avenger's: a bipolar
    // multipoint envelope on the oscillators' pitch, KEYTRACK, the pitch
    // wheel's lag, and the glide's curve and split note.
    struct PitchSettings
    {
        bool envelopeOn = false;
        mod::EnvelopeSettings envelope;      // MODE, SPEED, STEPS, FLIP (SYNC and OFFSET unused)
        const mod::Shape* shape = nullptr;   // its points: the middle (0.5) is no change
        float rangeSemitones = 12.0f;        // 0 to 24: how far the top and bottom go
        float keytrack = 1.0f;               // -1 to 1: how far the pitch follows the keys (from middle C)
        float bendLagMs = 0.0f;              // the pitch wheel, smoothed
        float glideCurve = 0.0f;             // -1 to 1: the glide's path (0: straight)
        int glideSplit = 0;                  // notes below this one don't glide (0: off)
    };

    struct Settings
    {
        std::array<OscSettings, numOscillators> oscillators {};
        FilterSettings filter;
        std::array<ShaperSettings, numShapers> shapers {};
        AmpSettings amp;
        PlaySettings play;
        mod::ModulationSettings modulation;
        PitchSettings pitch;
        StepSeqSettings stepSeq;
        std::array<ArpSettings, arp::numArps> arps {};
        // Stage 7b: the sends in use (their effect on): only then does the engine fill their mixes.
        std::array<bool, numSends> sendOn {};
        bool arpsTogether = false;   // 0.16.0: the first key starts every arp's clock, so arps joining later are in step

        Settings() { oscillators[0].on = true; }
    };
}
