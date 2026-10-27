#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Noise.h"
#include "SynthSettings.h"

#include <array>

// PR7-K's synth engine (stage 1): MIDI in, stereo out. Our own design,
// laid out like Avenger's: up to 8 oscillators per note, each through the
// FILTER (or not), then the AMP.
//
// Where the CPU goes, and how little of it this uses:
//   - Every oscillator reads band-limited tables (WaveTables.h), so nothing
//     is oversampled.
//   - An oscillator that's switched off costs nothing; so does a voice with no
//     note, and a sub, noise or drive turned to zero.
//   - A note ends the moment it can't be heard: when its release has fallen
//     below -90 dB (Avenger's manual warns that its barely audible tails
//     "still use cpu"), when it has decayed to a SUSTAIN of zero, or when
//     every oscillator in it has been taken by a newer note.
//   - Slow things (the filter's envelope and cutoff, glide, pitch bend, the
//     tables' choice) are worked out once per 16 samples (a third of a
//     millisecond at 48 kHz) and glided between, rather than every sample.
//
// Audio thread only, apart from prepare().
namespace synth
{
    class Engine
    {
    public:
        Engine();

        void prepare (double sampleRate);
        void reset() noexcept;   // silence at once, every note gone

        // Writes `numSamples` of sound into `left` and `right`, playing the MIDI
        // in `midi` whose positions fall in [midiStart, midiStart + numSamples),
        // each at its own sample.
        void process (float* left, float* right, int numSamples,
                      const juce::MidiBuffer& midi, int midiStart, const Settings&) noexcept;

        // Notes sounding now (including releasing ones), for the display and tests.
        int getNumActiveVoices() const noexcept;

        // For tests: play a note now / let it go.
        void noteOn (int note, float velocity, const Settings&) noexcept;
        void noteOff (int note, const Settings&) noexcept;

        static constexpr int controlInterval = 16;   // samples between updates of the slow things

        // A voice ends once its release is below this (-90 dB).
        static constexpr float silence = 3.1623e-5f;

    private:
        struct OscState
        {
            bool active = false;        // sounding in this voice (an oscillator can be taken by a newer note)
            float phase = 0.0f, subPhase = 0.0f;
            float randomOffset = 0.0f;  // semitones, chosen at the note's start (RANDOM)
            float fade = 1.0f, fadeStep = 0.0f;   // FADE IN, or the quick fade when a newer note takes it
            bool fadingOut = false;
            Noise noiseLeft, noiseRight;
        };

        struct Voice
        {
            bool playing = false;       // has a note (sounding or releasing)
            int note = -1;
            float velocity = 1.0f;
            bool held = false, sustained = false;
            std::uint64_t age = 0;      // when it started: older notes are taken first
            bool killing = false;       // stolen: fading out fast
            float killGain = 1.0f;

            Envelope amp, filter;
            FilterChannel filterLeft, filterRight;
            Saturator driveLeft, driveRight;
            FilterChannel::Coefficients coefficients;   // the filter's now (glided to each update)
            bool coefficientsFresh = true;

            float pitch = 60.0f, targetPitch = 60.0f, glideRate = 0.0f;   // semitones; per sample
            float panLeft = 1.0f, panRight = 1.0f;      // the AMP's PAN, KEYTRACK and SPREAD for this note
            float spikePosition = 1.0f;                 // 0 to 1 through the spike (1: done)

            std::array<OscState, numOscillators> oscillators;
        };

        void handle (const juce::MidiMessage&, const Settings&) noexcept;
        void render (float* left, float* right, int numSamples, const Settings&) noexcept;
        void renderVoice (Voice&, float* left, float* right, int numSamples, const Settings&) noexcept;
        void renderOscillators (Voice&, int numSamples, const Settings&, float bendSemitones) noexcept;

        void startVoice (Voice&, int note, float velocity, float fromPitch, const Settings&) noexcept;
        void retrigger (Voice&, int note, float velocity, bool restartEnvelopes, bool glide, const Settings&) noexcept;
        void releaseVoice (Voice&) noexcept;
        void kill (Voice&) noexcept;
        void free (Voice&) noexcept;
        Voice* findFreeVoice() noexcept;
        void applyOscillatorLimits (const Voice& newest, const Settings&) noexcept;
        void setGlide (Voice&, float toPitch, bool glide, const Settings&) noexcept;
        void setNotePan (Voice&, const Settings&) noexcept;

        void polyNoteOn (int note, float velocity, const Settings&) noexcept;
        void monoNoteOn (int note, float velocity, const Settings&) noexcept;
        void monoNoteOff (int note, const Settings&) noexcept;
        int monoTarget (const Settings&) const noexcept;   // which held note the mono voice plays, or -1
        void allNotesOff() noexcept;
        void followMode (const Settings&) noexcept;   // POLY to MONO or back: every note lets go
        void sustainPedal (bool down) noexcept;

        double sampleRate = 48000.0;
        // Pool: up to maxVoices notes, plus room for stolen ones finishing their fade.
        std::array<Voice, maxVoices + 16> voices;
        Voice* monoVoice = nullptr;
        std::uint64_t noteCounter = 0;
        float lastPitch = -1.0f;        // the last note started, for glide (-1: none yet)
        float bend = 0.0f;              // the pitch wheel, -1 to 1
        bool pedalDown = false;
        PlayMode lastMode = PlayMode::poly;

        // Mono: the keys held down, in the order they were pressed.
        std::array<int, 128> heldNotes {};
        std::array<float, 128> heldVelocities {};
        int numHeld = 0;

        std::array<bool, numOscillators> alternate {};   // RANDOM's alternating direction, per oscillator
        juce::Random random;

        // Scratch for one control interval: what goes through the filter and what doesn't, per channel.
        std::array<float, controlInterval> filteredLeft {}, filteredRight {}, dryLeft {}, dryRight {}, scratch {};

        float killStep = 1.0f / 144.0f;   // 3 ms at 48 kHz
    };
}
