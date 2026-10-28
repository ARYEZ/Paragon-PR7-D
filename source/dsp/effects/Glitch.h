#pragma once

#include "PitchShifter.h"

namespace effects
{
    // The GLITCH block: granular glitch, stutter and texture effects. It keeps
    // the last few seconds of the guitar and plays pieces of it back ("grains"
    // or "slices") in five ways (MODE):
    //
    //   pitchDelay  echoes, each one shifted by PITCH from the last, so the
    //               repeats climb or fall; DENSITY is how many repeats
    //   sequence    tempo-locked stutters: at each step (LENGTH) it may grab
    //               the slice just played and repeat it instead, like a
    //               beat-repeat edit; DENSITY is how often, and repeats are
    //               shifted by PITCH
    //   interfere   a failing signal: the guitar cut into segments, some of
    //               them dropped out, played backwards or caught in a short
    //               buzzing loop; DENSITY is how much of it is broken up
    //   texturizer  a cloud of overlapping grains from the last second or so,
    //               smeared into a pad; DENSITY is how many grains per second
    //   killswitch  a guitar's toggle switch flicked in time (Tom Morello,
    //               Buckethead): the end of each step is cut to silence;
    //               DENSITY is how much of the step is cut, and CHAOS skips
    //               cuts at random
    //
    // CHAOS is the macro: at 0 everything is regular and predictable; turning
    // it up randomises the lengths, pitches, directions and positions of the
    // pieces together. MIX blends the result with the dry guitar. SYNC sets
    // LENGTH in note values at the tapped (or host) tempo.
    class Glitch
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        enum class Mode { pitchDelay, sequence, interfere, texturizer, killswitch };
        static inline const juce::StringArray modeNames { "Pitch Delay", "Sequence", "Interfere", "Texturizer", "Killswitch" };

        // Where it sits: after the amp and cab (the default: clean, exact
        // chops), or before the amp (the chops hit the drive: wilder).
        enum class Position { post, pre };
        static inline const juce::StringArray positionNames { "Post", "Pre" };

        // LENGTH when synced, in beats (a quarter note is one beat).
        static constexpr float syncBeats[] = { 1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.5f, 1.0f, 2.0f, 4.0f };
        static inline const juce::StringArray syncNames { "1/64", "1/32", "1/16 triplet", "1/16", "1/8 triplet", "1/8", "1/4", "1/2", "1 bar" };

        struct Settings
        {
            bool on = false;
            Mode mode = Mode::sequence;
            bool sync = true;
            float lengthMs = 120.0f;   // LENGTH when not synced
            int division = 3;          // LENGTH when synced: an index into syncBeats (1/16)
            float density = 5.0f;      // 0 to 10
            float pitch = 0.0f;        // semitones, -12 to +12
            float chaos = 3.0f;        // 0 to 10
            float mix = 0.5f;          // 0 to 1
        };

        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        // `songBeats`: where the block's first sample is in the song, in beats
        // (quarter notes), while the DAW's transport is playing; negative when
        // it isn't (or the tempo is tapped, so the DAW's grid doesn't apply).
        // With SYNC on, Sequence and Interfere then start their steps on the
        // song's grid lines (every 1/16, every bar...) rather than counting
        // from when GLITCH was switched on.
        void process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings, double bpm, double songBeats = -1.0) noexcept;

        // LENGTH in samples for these settings (at least a few milliseconds,
        // at most two seconds).
        int lengthInSamples (const Settings& settings, double bpm) const noexcept;

    private:
        struct Grain
        {
            bool active = false;
            double position = 0.0;   // where it reads, in samples written since prepare()
            double step = 1.0;       // how far it moves each sample (negative: backwards)
            int length = 0, played = 0;
            float gain = 1.0f;
        };

        float readHistory (double position) const noexcept;
        float pitchDelaySample (float input, const Settings& settings, int length) noexcept;
        float sequenceSample (float input, const Settings& settings, int length) noexcept;
        float interfereSample (float input, const Settings& settings, int length) noexcept;
        float killswitchSample (float input, const Settings& settings, int length) noexcept;
        bool startsStep() noexcept;   // whether this sample begins a new step or segment
        void moveBlend() noexcept;    // one sample further through the fade between the live guitar and the action
        float texturizerSample (const Settings& settings, int length) noexcept;
        float chance() noexcept { return random.nextFloat(); }
        float semitonesWithChaos (float semitones, float chaos) noexcept;

        double sampleRate = 48000.0;
        BypassFade bypass;
        juce::SmoothedValue<float> mix;
        juce::Random random { 0x9117c4 };   // a fixed seed: the same playing glitches the same way

        // Everything played, kept for the last few seconds.
        std::vector<float> history;
        int historyMask = 0;
        juce::int64 written = 0;
        Mode lastMode = Mode::sequence;

        // Pitch Delay: its own echo line (the repeats feed back through the shifter).
        std::vector<float> echoes;
        PitchShifter shifter;
        juce::SmoothedValue<float> echoDelay;
        float echoStepSemitones = 0.0f;
        int echoCountdown = 0;

        // Sequence and Interfere: what the current step or segment is doing,
        // and a short cross-fade between that and the live guitar.
        enum class Action { live, repeat, reverse, drop, buzz };
        Action action = Action::live;
        int stepRemaining = 0, stepPlayed = 0, loopLength = 0;
        juce::int64 sliceStart = 0;
        double loopStep = 1.0;
        float blend = 0.0f;   // 0: live, 1: the action

        // Grid alignment: while the song plays, steps start on its grid lines.
        bool onGrid = false, gridLine = false;
        juce::int64 lastGridStep = std::numeric_limits<juce::int64>::min();

        // Texturizer
        std::array<Grain, 32> grains;
        int spawnCountdown = 0;
    };
}
