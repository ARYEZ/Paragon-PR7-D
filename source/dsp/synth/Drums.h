#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/synth/DrumSequencer.h"
#include "dsp/synth/MultiPoint.h"
#include "dsp/synth/Noise.h"

#include <array>
#include <atomic>
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
        juce::String path;                          // the file it came from, if known (6c: < > step through its folder)
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
        // REVERSE (the sequencer, 6b): what a reversed hit plays backwards. A SAMPLE slot's own sample; a drum synth's
        // hit rendered ahead at these settings ("baked": its pitch and shaping already in it), or nothing yet.
        const Sample* reverseSource = nullptr;
        bool reverseBaked = false;
        // THE SAMPLE EDITOR (6c), for every slot:
        float trim = 1.0f;                 // GAIN, as a gain (for samples recorded too loud or too quiet)
        bool mono = false;                 // a hit fades the slot's earlier ones (POLY: they overlap)
        float delay = 0.0f;                // seconds: above 0 the hit comes later; below, the sequencer starts it
                                           // that much early so that point in it (its anchor) lands on the step
        float lowPass = 20000.0f, highPass = 20.0f;   // 6 dB/oct, Hz (20 kHz / 20 Hz: off)
        const mod::Shape* envelope = nullptr;         // the drawn envelope across the hit (null: none)
        float envelopeSeconds = 0.0f;      // a drum synth's hit length, which its envelope spans (SAMPLE: START to END)

        bool filtered() const noexcept { return lowPass < 19999.0f || highPass > 20.01f; }
        // The sequencer's lead for the anchor (seconds early).
        double anchorSeconds() const noexcept { return delay < 0.0f ? -(double) delay : 0.0; }
    };

    // An envelope that changes nothing (none, or flat at the top) isn't worked out at all.
    inline bool envelopeShapes (const mod::Shape* shape) noexcept
    {
        if (shape == nullptr || shape->count <= 0)
            return false;
        for (int i = 0; i < shape->count; ++i)
            if (shape->points[(size_t) i].y < 0.9999f)
                return true;
        return false;
    }

    // A hit's own touches (the sequencer's notes): added to the slot's pan and pitch, cut after a gate, played
    // backwards; velocityGain, if set (0 or more), replaces the keys' velocity curve.
    struct HitOptions
    {
        float pan = 0.0f, pitch = 0.0f;
        int gateSamples = -1;             // -1: it rings out
        drumseq::Reverse reverse = drumseq::Reverse::off;
        float velocityGain = -1.0f;
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
        bool reversed = false;
        double startPosition = 0.0;
        // The note's own pan (a balance on top of the slot's) and gate.
        float panLeft = 1.0f, panRight = 1.0f;
        int gateLeft = -1;
        // The drawn envelope (6c): where it is across the hit (0 to 1), gliding between points worked out every
        // 8 samples; DELAY's wait before the hit starts.
        const mod::Shape* shape = nullptr;
        double shapeX = 0.0, shapeDx = 0.0;
        float shapeGain = 1.0f, shapeStep = 0.0f;
        int shapeCountdown = 0, waitLeft = 0;

        void start (const SlotSettings&, int slotIndex, float velocityGain, double sampleRate, std::uint32_t seed,
                    const HitOptions& = {}) noexcept;
        // Adds this hit's next `numSamples` into left / right (the same for the mono models).
        void render (float* left, float* right, int numSamples) noexcept;
        void fadeOut (double seconds) noexcept;

    private:
        void setTone (double tone, double rate) noexcept;   // SAMPLE's TONE: a low-pass below the middle, a high-pass above
        void startShape (const SlotSettings&, double regionSamples) noexcept;
        bool advanceShape() noexcept;      // the envelope's next sample; false once it has ended at nothing
        float next() noexcept;             // the mono models
        bool finished() const noexcept;
    };

    class Kit
    {
    public:
        static constexpr int maxVoices = 48, maxPerSlot = 6;

        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        // Plays the kit's notes in `midi` (positions from midiStart) over the next numSamples, adding into `outputs`,
        // and THE DRUM SEQUENCER's beat (6b): SONG from the song's position `ppq` at the first sample (below 0: the
        // song isn't playing), KEY while one of the four keys under the kit is held (they choose pattern A-D).
        void process (const Outputs&, int numSamples, const juce::MidiBuffer& midi, int midiStart, const KitSettings&,
                      const drumseq::Settings& sequencer = {}, double ppq = -1.0, double bpm = 120.0) noexcept;

        // The sequencer now (any thread): where it is in its pattern, in steps (-1: stopped), and which pattern.
        double getSequencerPosition() const noexcept { return sequencerPosition.load(); }
        int getSequencerPattern() const noexcept { return sequencerPattern.load(); }
        // KEY: the four keys under ROOT start pattern A to D (lowest A).
        static int patternForKey (int note, int rootKey) noexcept
        {
            const auto k = note - (rootKey - drumseq::numPatterns);
            return k >= 0 && k < drumseq::numPatterns ? k : -1;
        }
        // A reversed hit's length, in seconds (0: nothing to reverse).
        static double reverseSeconds (const SlotSettings&, float notePitch, double sampleRate) noexcept;
        // A hit straight away (for tests and the page's preview): velocity 0 to 1.
        void trigger (int slot, float velocity, const KitSettings&) noexcept;

        int getNumActiveVoices() const noexcept;
        bool isSounding (int slot) const noexcept;
        // Where a slot's last hit is, 0 to 1 of its first second (for the page), or -1.
        float getHitAge (int slot) const noexcept;

        // One hit rendered alone, `seconds` long (for the page's display and tests): its envelope, GAIN and LP / HP
        // in it, not DELAY.
        static juce::AudioBuffer<float> renderHit (const SlotSettings&, float velocity, double sampleRate, double seconds);
        // How long a slot's hit lasts as it is (to -60 dB under its peak, at most 4 s), without its envelope: what a
        // drum synth's envelope spans (SAMPLE: exactly START to END at its pitch).
        static double hitSeconds (const SlotSettings&);

        // Seeds the hits' noise (tests), so a hit sounds the same each run.
        void seedRandom (std::uint32_t seed) noexcept { noiseSeed = seed == 0 ? 1u : seed; }

    private:
        void render (const Outputs&, int offset, int numSamples, const KitSettings&) noexcept;
        static void filterSlot (const SlotSettings&, float* left, float* right, int numSamples, std::array<float, 2>& low,
                                std::array<float, 2>& high, double sampleRate) noexcept;
        void startHit (int slot, float velocity, const KitSettings&, const HitOptions& = {}) noexcept;
        void runSequencer (const Outputs&, int from, int to, const KitSettings&, const drumseq::Settings&, double ppq, double bpm) noexcept;
        void patternKey (int pattern, bool down, float velocity) noexcept;

        struct SequencedHit
        {
            int offset = 0;
            drumseq::Note note;
        };
        std::vector<SequencedHit> sequenced = std::vector<SequencedHit> (512);
        bool keyRunning = false;
        double keyClock = 0.0;                       // KEY: steps since the key went down
        int keyPattern = 0;
        float keyVelocity = 1.0f;
        std::vector<int> heldPatterns = std::vector<int> (8);
        int numHeld = 0;
        std::atomic<double> sequencerPosition { -1.0 };
        std::atomic<int> sequencerPattern { 0 };
        static bool audible (const KitSettings&, int slot) noexcept;   // MUTE / SOLO

        double sampleRate = 48000.0;
        std::vector<Voice> voices = std::vector<Voice> (maxVoices);
        std::vector<float> scratch;                     // 2 x numSlots x the block
        std::array<float, numSlots> slotGain {};        // MUTE / SOLO, glided
        std::array<std::array<float, 2>, numSlots> lowState {}, highState {};   // LP / HP (6c)
        std::array<double, numSlots> lastHit {};        // seconds since each slot's last hit (-1: none)
        std::uint64_t ages = 0;
        std::uint32_t noiseSeed = 0x2545f491u;
        int blockSize = 0;
    };
}
