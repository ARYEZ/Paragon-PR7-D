#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/synth/Noise.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// THE DRUMS (stage 6a): a kit of 12 slots, one per key of an octave (C the kick, D# the clap, as on Avenger's drum tab
// and General MIDI). Each slot is one of our own drum synths or a sample the user loads; they play as one-shots (a
// note-off doesn't stop them), with VOLUME, PAN, PITCH, and three shaping knobs whose meaning suits the model:
//
//   DECAY  how long it rings;
//   TONE   its brightness (or, on KICK and TOM, how far the pitch drops);
//   SNAP   its attack (the beater's click, the snare's wires, the hats' sizzle...).
//
// The drum synths are our own designs from the classic analog ideas: a sine whose pitch falls (kick, tom), tuned modes
// plus filtered noise (snare), noise in bursts (clap), six square waves at unrelated pitches for metal (hats, cymbal,
// cowbell). Not a copy of any drum machine's circuit. SAMPLE plays the slot's sample (cubic interpolation, any rate);
// its DECAY, TONE and SNAP are neutral at the middle: DECAY under it shortens the sample, TONE tilts it darker or
// thinner, SNAP softens the attack or adds a spike.
//
// CHOKE: a hit stops the other slots in its group (the closed hat stops the open hat). MUTE and SOLO as on a mixer.
namespace synth::drums
{
    inline constexpr int numSlots = 12;

    // Saved by index (drum{k}Model): only ever append.
    enum class Model { kick, snare, clap, closedHat, openHat, tom, rim, cowbell, cymbal, sample };
    inline const juce::StringArray modelNames { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Rim", "Cowbell", "Cymbal", "Sample" };
    inline const juce::StringArray chokeNames { "None", "1", "2", "3", "4" };   // drum{k}Choke, by index: only append

    // The new kit, slot by slot (C to B): what it starts as, and short names for the keys.
    struct SlotDefault
    {
        Model model;
        float pitch;          // semitones
        float decay, tone, snap;
        int choke;
        const char* key;      // e.g. "BD"
    };
    inline constexpr std::array<SlotDefault, numSlots> slotDefaults {{
        { Model::kick,      0.0f, 0.5f, 0.5f, 0.5f, 0, "BD" },     // C
        { Model::rim,       0.0f, 0.4f, 0.5f, 0.5f, 0, "RIM" },    // C#
        { Model::snare,     0.0f, 0.5f, 0.5f, 0.5f, 0, "SD" },     // D
        { Model::clap,      0.0f, 0.5f, 0.5f, 0.5f, 0, "CLP" },    // D#
        { Model::snare,     3.0f, 0.4f, 0.7f, 0.8f, 0, "SD2" },    // E
        { Model::tom,      -5.0f, 0.5f, 0.5f, 0.5f, 0, "LT" },     // F
        { Model::closedHat, 0.0f, 0.4f, 0.5f, 0.5f, 1, "CH" },     // F#
        { Model::tom,       0.0f, 0.5f, 0.5f, 0.5f, 0, "MT" },     // G
        { Model::closedHat, -2.0f, 0.2f, 0.4f, 0.3f, 1, "PH" },    // G#: the pedal hat
        { Model::tom,       5.0f, 0.5f, 0.5f, 0.5f, 0, "HT" },     // A
        { Model::openHat,   0.0f, 0.5f, 0.5f, 0.5f, 1, "OH" },     // A#
        { Model::cymbal,    0.0f, 0.6f, 0.5f, 0.5f, 0, "CY" },     // B
    }};

    // A loaded sample: one or two channels at its own rate.
    struct Sample
    {
        juce::String name;
        double sampleRate = 48000.0;
        std::vector<std::vector<float>> channels;   // 1 or 2, all the same length

        int length() const noexcept { return channels.empty() ? 0 : (int) channels[0].size(); }
    };

    // Samples from files (WAV, AIFF, FLAC...), and as text inside presets and projects (16-bit, gzipped, base64).
    namespace sampleFiles
    {
        inline constexpr double maxSeconds = 30.0;   // longer files are cut: these are drum hits, not songs
        std::shared_ptr<const Sample> load (const juce::File&);
        juce::String encode (const Sample&);
        std::shared_ptr<const Sample> decode (const juce::String& name, const juce::String& text);

        // Where each hit in a file starts (in samples), for a file holding several (a kick played nine ways, say):
        // a jump in level well above the quiet just before it, at least 40 ms after the last, moved back to where the
        // hit's rise begins. A single hit gives one start; a file with nothing loud gives none.
        std::vector<int> findHits (const Sample&);
    }

    struct SlotSettings
    {
        Model model = Model::kick;
        float gain = 1.0f;                 // VOLUME, as a gain
        float pan = 0.0f;                  // -1 left to 1 right
        float pitch = 0.0f;                // semitones
        float decay = 0.5f, tone = 0.5f, snap = 0.5f;
        bool mute = false, solo = false;
        int choke = 0;                     // 0 none, 1-4
        int insertBus = 0;                 // 0 the mix, 1-4 bus A-D
        std::array<float, 2> send { 1.0f, 1.0f };   // DELAY, REVERB (only while that effect is on)
        const Sample* sample = nullptr;    // SAMPLE's, or nothing
        float start = 0.0f, end = 1.0f;    // SAMPLE: the part played, as shares of the file (0.21.1)
    };

    struct KitSettings
    {
        bool on = false;
        int rootKey = 36;                  // the kit's C (the keys rootKey to rootKey + 11 play the drums)
        float velocity = 1.0f;             // how much the keys' velocity changes the level, 0 to 1
        float gain = 1.0f;                 // the kit's LEVEL
        std::array<bool, 2> sendOn {};     // DELAY, REVERB switched on
        std::array<SlotSettings, numSlots> slots;

        int slotFor (int note) const noexcept { return note >= rootKey && note < rootKey + numSlots ? note - rootKey : -1; }
    };

    // Where the kit plays into: the mix, each send (null while its effect is off), each insert bus (null if none).
    struct Outputs
    {
        float* left = nullptr;
        float* right = nullptr;
        std::array<float*, 2> sendLeft {}, sendRight {};
        std::array<float*, 4> busLeft {}, busRight {};
    };

    // A zero-delay-feedback state-variable filter (Zavalishin's), set once per hit.
    struct Svf
    {
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;

        void set (double hz, double q, double sampleRate) noexcept;
        void process (float x) noexcept
        {
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2;
            bp = v1;
            hp = x - k * v1 - v2;
        }
    };

    // One hit sounding.
    struct Voice
    {
        bool active = false;
        int slot = -1;
        Model model = Model::kick;
        std::uint64_t age = 0;
        int choke = 0;

        double sampleRate = 48000.0;
        double t = 0.0;                    // seconds since the hit
        float level = 1.0f;                // velocity, and the model's own scaling
        float fade = 1.0f, fadeStep = 0.0f;   // stolen or choked: fading out

        // The models' working parts.
        std::array<double, 6> phase {}, increment {};
        double baseHz = 0.0, bend = 0.0;
        float env = 0.0f, envCoef = 0.0f, env2 = 0.0f, env2Coef = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f;
        float click = 0.0f, clickCoef = 0.0f, noiseLevel = 0.0f, mix = 0.0f;
        int burst = 0;
        Svf f1, f2, f3;
        Noise noise;

        // SAMPLE's.
        const Sample* sample = nullptr;
        double position = 0.0, step = 1.0, endPosition = 0.0, endFade = 0.0;   // END: where it stops, fading over endFade
        float lowPole = 0.0f, highPole = 0.0f;
        std::array<float, 2> lowState {}, highState {};
        float attackTime = 0.0f, spike = 0.0f;

        void start (const SlotSettings&, int slotIndex, float velocityGain, double sampleRate, std::uint32_t seed) noexcept;
        // Adds this hit's next `numSamples` into left / right (the same for the mono models).
        void render (float* left, float* right, int numSamples) noexcept;
        void fadeOut (double seconds) noexcept;

    private:
        float next() noexcept;             // the mono models
        bool finished() const noexcept;
    };

    class Kit
    {
    public:
        static constexpr int maxVoices = 48, maxPerSlot = 6;

        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        // Plays the kit's notes in `midi` (positions from midiStart) over the next numSamples, adding into `outputs`.
        void process (const Outputs&, int numSamples, const juce::MidiBuffer& midi, int midiStart, const KitSettings&) noexcept;
        // A hit straight away (for tests and the page's preview): velocity 0 to 1.
        void trigger (int slot, float velocity, const KitSettings&) noexcept;

        int getNumActiveVoices() const noexcept;
        bool isSounding (int slot) const noexcept;
        // Where a slot's last hit is, 0 to 1 of its first second (for the page), or -1.
        float getHitAge (int slot) const noexcept;

        // One hit rendered alone, `seconds` long (for the page's display and tests).
        static juce::AudioBuffer<float> renderHit (const SlotSettings&, float velocity, double sampleRate, double seconds);

        // Seeds the hits' noise (tests), so a hit sounds the same each run.
        void seedRandom (std::uint32_t seed) noexcept { noiseSeed = seed == 0 ? 1u : seed; }

    private:
        void render (const Outputs&, int offset, int numSamples, const KitSettings&) noexcept;
        void startHit (int slot, float velocity, const KitSettings&) noexcept;
        static bool audible (const KitSettings&, int slot) noexcept;   // MUTE / SOLO

        double sampleRate = 48000.0;
        std::vector<Voice> voices = std::vector<Voice> (maxVoices);
        std::vector<float> scratch;                     // 2 x numSlots x the block
        std::array<float, numSlots> slotGain {};        // MUTE / SOLO, glided
        std::array<double, numSlots> lastHit {};        // seconds since each slot's last hit (-1: none)
        std::uint64_t ages = 0;
        std::uint32_t noiseSeed = 0x2545f491u;
        int blockSize = 0;
    };
}
