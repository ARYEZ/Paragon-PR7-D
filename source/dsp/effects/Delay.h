#pragma once

#include "PitchShifter.h"

namespace effects
{
    // The delay half of footswitch 4, in eight types:
    //
    //   digital    clean, exact repeats (the original Paragon delay); TONE
    //              darkens each repeat a little more than the last
    //   analog     a bucket-brigade pedal (Boss DM-2, MXR Carbon Copy): warm,
    //              dark repeats that soften and blur, with a slight wobble,
    //              and that sing into oscillation with FEEDBACK up high
    //   tape       an Echoplex or Space Echo: wow and flutter, tape
    //              saturation, a little hiss, repeats that warm as they fade
    //   mod        clean repeats with chorus on them
    //   reverse    each repeat played backwards
    //   multiTap   repeats at rhythmic spacings inside each beat, accenting it
    //   shimmer    each repeat pitched up an octave, rising into a glowing wash
    //   ducking    Digital with DUCK preset high: repeats tuck under your
    //              playing and swell back in the gaps (DUCK works on every
    //              type; this keeps rigs that used the type sounding the same)
    //
    // Echo time is either free (milliseconds) or locked to the DAW's tempo.
    // Ping-pong bounces repeats between the left and right speakers.
    // Switching off lets the echoes ring out ("trails") instead of cutting
    // them dead.
    class Delay
    {
    public:
        // Note lengths, in beats (quarter notes), for tempo sync.
        // Index 0 is "Off": use the time in milliseconds instead.
        // Saved by number: only append. (1/16 dotted, 1/16 triplet and 1/32
        // added in PR7-G 0.40.1, for fast synced tremolo.)
        static constexpr float syncBeats[] = { 0.0f, 4.0f, 2.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f,
                                               0.375f, 1.0f / 6.0f, 0.125f };
        static inline const juce::StringArray syncNames { "Off", "1/1", "1/2", "1/4", "1/4 dotted", "1/4 triplet",
                                                          "1/8", "1/8 dotted", "1/8 triplet", "1/16",
                                                          "1/16 dotted", "1/16 triplet", "1/32" };

        static constexpr float maxDelaySeconds = 2.5f;

        // Saved by number in rigs and projects: only ever add to the end.
        enum class Type { digital, analog, tape, mod, reverse, multiTap, shimmer, ducking };
        static inline const juce::StringArray typeNames { "Digital", "Analog", "Tape", "Mod", "Reverse", "Multi-Tap", "Shimmer", "Ducking" };

        struct Settings
        {
            bool on = false;
            float timeMs = 375.0f;
            int syncIndex = 3;           // 1/4
            float feedback = 0.35f;      // 0 to 0.95
            float tone = 7.0f;           // 0 to 10
            float mix = 0.25f;           // 0 to 1
            bool pingPong = false;
            Type type = Type::digital;
            float duck = 0.0f;           // 0 to 1: DUCK
            // PR7-K (0.18.0): a send's return. The block becomes just the echoes (at MIX), not the input plus
            // them: the dry sound reaches the mix by its own way.
            bool returnOnly = false;
        };

        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings, double bpm) noexcept;

        // The echo time these settings give at this tempo, in seconds.
        static float delaySeconds (const Settings& settings, double bpm) noexcept;

    private:
        void resetState() noexcept;

        // A circular buffer read from behind its write position, for the
        // reverse and shimmer types.
        struct Ring
        {
            std::vector<float> data;
            int mask = 0, write = 0;
            void prepare (int minimumLength);
            void clear() { std::fill (data.begin(), data.end(), 0.0f); write = 0; }
            void push (float x) noexcept { data[(size_t) write] = x; write = (write + 1) & mask; }
            float at (float samplesAgo) const noexcept;
        };

        double sampleRate = 48000.0;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 4 };
        juce::SmoothedValue<float> delaySamples, send, feedback, mix;
        float feedbackFilter[maxChannels] {}, feedbackFilter2[maxChannels] {}, lowCutState[maxChannels] {};
        bool running = false;
        Type lastType = Type::digital;
        TailWatch tail;
        juce::AudioBuffer<float> wet;
        juce::Random random;

        // Analog, tape and mod: the drifting read position.
        float wowPhase = 0.0f, flutterPhase = 0.0f, drift = 0.0f, driftTarget = 0.0f;
        int driftCountdown = 0;

        // Reverse: what goes into the delay, recorded, and two heads
        // playing it backwards in delay-length chunks.
        std::array<Ring, maxChannels> reverseRings;
        struct Head { int start = 0, count = 0, length = 0; };   // each chunk keeps the length it started with
        std::array<Head, 2> reverseHeads {};

        // Shimmer: an octave-up pitch shifter in the feedback loop, per side.
        std::array<PitchShifter, maxChannels> shimmerShifters;

        // DUCK (and the Ducking type, which is Digital with DUCK preset).
        Ducker ducker;
    };
}
