#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

// The STEP SEQUENCER (stage 4d), modelled on Avenger's (the manual's page
// 83; our own design and code): a row of up to 32 steps, each a block of a
// set height (its depth), linked to the next or not, or deleted (a rest).
// It starts as a classic trance gate: on the note's volume, by VOL MIX. It's
// also a source in the matrix (STEP SEQ, 0 to 1), so it can move any knob.
//
// Each step lasts SPEED (a note length at the song's tempo). The gate opens
// at a step's start and closes GATE of the way through it; linked steps keep
// it open from one to the next (closing GATE of the way through the last),
// moving to each one's height. CONTOUR softens the edges (up to half a step),
// DECAY lets each block fall away after it opens, SHUFFLE starts every second
// step late (a swing). In STEREO there are two rows: the left channel's and
// the right's, WIDTH from the same (0) to fully apart (1).
//
// The manual doesn't give the curves and times: ours are a raised cosine for
// CONTOUR (and a 0.5 ms edge at CONTOUR 0, so a hard gate doesn't click),
// DECAY as the time to fall 60 dB, and SHUFFLE moving the second step of each
// pair up to half a step late (50% to 75% swing).
namespace synth
{
    namespace seq
    {
        inline constexpr int maxSteps = 32;

        struct Step
        {
            bool on = true;       // false: deleted (a rest)
            float level = 1.0f;   // 0 to 1: its height
            bool tied = false;    // linked to the next step

            bool operator== (const Step& o) const noexcept { return on == o.on && juce::exactlyEqual (level, o.level) && tied == o.tied; }
        };
        using Row = std::array<Step, maxSteps>;

        // The steps: row 0 (the only one in MONO; the left in STEREO) and row 1 (the right).
        struct Pattern
        {
            std::array<Row, 2> rows {};
            bool operator== (const Pattern& o) const noexcept { return rows == o.rows; }
        };

        // Saved by index (seqMode, seqTrigger, seqTiming): only ever append.
        enum class Mode { mono, stereo };
        inline const juce::StringArray modeNames { "Mono", "Stereo" };
        // SONG: in step with the song while it plays, otherwise from the first
        // note held; VOICE: each note its own, from its start; FIRST NOTE: one,
        // restarted when a note starts with none held; LAST NOTE: by every note.
        enum class Trigger { song, voice, firstNote, lastNote };
        inline const juce::StringArray triggerNames { "Song", "Voice", "First Note", "Last Note" };
        enum class Timing { straight, triplet, dotted };
        inline const juce::StringArray timingNames { "Straight", "Triplet", "Dotted" };
        // SPEED: a step's length (the LFOs' list, from 8 bars to 1/64).
        inline const juce::StringArray divisionNames { "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        inline constexpr std::array<double, 10> divisionBeats { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };

        inline constexpr double edgeSeconds = 0.0005;   // the shortest edge (CONTOUR 0)
        inline constexpr double decayLn = 6.907755;     // ln 1000: DECAY falls 60 dB

        inline float raisedCosine (double x) noexcept { return (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x)); }
    }

    struct StepSeqSettings
    {
        bool on = false;                 // the trance gate on the volume (the matrix source runs either way)
        seq::Trigger trigger = seq::Trigger::song;
        int division = 7;                // 1/16
        seq::Timing timing = seq::Timing::straight;
        seq::Mode mode = seq::Mode::mono;
        int length = 16;                 // 2 to 32 steps
        float contour = 0.0f;            // 0 to 1: the edges, hard to half a step
        float gate = 0.5f;               // 0.01 to 1: how much of a step it's open
        float volMix = 1.0f;             // 0 to 1: how deep it cuts the volume
        float width = 1.0f;              // 0 to 1: STEREO's rows the same (0) to fully apart
        float decayMs = 0.0f;            // 0: off; otherwise each block falls 60 dB in this time
        float fadeInMs = 0.0f, delayMs = 0.0f;   // from each note's start
        int lowKey = 0, highKey = 127;   // the notes it plays on (the others are left alone)
        float shuffle = 0.0f;            // 0 to 1: every second step late, up to half a step
        const seq::Pattern* pattern = nullptr;

        double beatsPerStep() const noexcept
        {
            auto beats = seq::divisionBeats[(size_t) juce::jlimit (0, (int) seq::divisionBeats.size() - 1, division)];
            if (timing == seq::Timing::triplet)
                beats *= 2.0 / 3.0;
            else if (timing == seq::Timing::dotted)
                beats *= 1.5;
            return beats;
        }
        bool plays (int note) const noexcept { return note >= lowKey && note <= highKey; }
    };

    namespace seq
    {
        // The pattern worked out for playing (once a block): where each step
        // starts, and for each step where its run of linked steps starts and
        // where the gate closes. Then its value at any position is quick.
        class Prepared
        {
        public:
            void prepare (const StepSeqSettings& s, double secondsPerStep) noexcept
            {
                length = juce::jlimit (2, maxSteps, s.length);
                inverseLength = 1.0 / length;
                stereo = s.mode == Mode::stereo;
                width = juce::jlimit (0.0f, 1.0f, s.width);
                const auto shift = 0.5 * juce::jlimit (0.0, 1.0, (double) s.shuffle);
                for (int k = 0; k < length; ++k)
                    bounds[(size_t) k] = k + ((k & 1) != 0 ? shift : 0.0);
                bounds[(size_t) length] = length;

                const auto steps = juce::jmax (1.0e-6, secondsPerStep);
                minRamp = edgeSeconds / steps;
                ramp = juce::jmax (minRamp, 0.5 * juce::jlimit (0.0, 1.0, (double) s.contour));
                decaySteps = s.decayMs > 0.0f ? s.decayMs * 0.001 / steps : 0.0;
                const auto gate = juce::jlimit (0.01, 1.0, (double) s.gate);

                static const Pattern empty {};
                const auto& pattern = s.pattern != nullptr ? *s.pattern : empty;
                for (int row = 0; row < 2; ++row)
                {
                    const auto& r = pattern.rows[(size_t) row];
                    const auto at = [&r] (int k) -> const Step& { return r[(size_t) k]; };
                    for (int k = 0; k < length; ++k)
                    {
                        auto& info = infos[(size_t) row][(size_t) k];
                        info.on = at (k).on;
                        info.level = juce::jlimit (0.0f, 1.0f, at (k).level);
                        if (! info.on)
                            continue;
                        // Back to the run's first step, and on to its last (round the loop's end too).
                        int first = k, count = 0;
                        while (count < length - 1)
                        {
                            const auto before = (first + length - 1) % length;
                            if (! at (before).on || ! at (before).tied)
                                break;
                            first = before;
                            ++count;
                        }
                        const auto whole = count == length - 1 && at ((first + length - 1) % length).tied;   // every step on and linked
                        int last = k;
                        for (int c = 0; c < length - 1 && at (last).tied && at ((last + 1) % length).on; ++c)
                            last = (last + 1) % length;
                        info.runStartsHere = first == k;
                        info.runStart = bounds[(size_t) first];
                        info.previous = first == k ? 0.0f : juce::jlimit (0.0f, 1.0f, at ((k + length - 1) % length).level);
                        info.closeAt = whole ? 1.0e12 : distance (first, last) + gate * stepLength (last);
                    }
                }
            }

            // The gate's value (0 to 1) at `position` steps from the pattern's start, for `row`.
            float value (int row, double position) const noexcept
            {
                // Where in the loop (positions from the engine are never negative; no library
                // remainder or floor per sample: they're slow calls on some compilers).
                auto q = position - (double) (juce::int64) (juce::jmax (0.0, position) * inverseLength) * length;
                if (q >= length)
                    q -= length;
                if (q < 0.0)
                    q += length;
                auto k = juce::jmin (length - 1, (int) q);
                if ((k & 1) != 0 && q < bounds[(size_t) k])
                    --k;   // still in the step before (the second of a pair starts late)
                const auto& info = infos[(size_t) row][(size_t) k];
                if (! info.on)
                    return 0.0f;
                auto t = q - info.runStart;
                if (t < 0.0)
                    t += length;
                if (t >= info.closeAt)
                    return 0.0f;

                // The edges: in at the run's start, out where the gate closes.
                const auto edge = juce::jmin (ramp, 0.5 * info.closeAt);
                auto open = 1.0f;
                if (t < edge)
                    open = raisedCosine (t / edge);
                const auto left = info.closeAt - t;
                if (left < edge)
                    open = juce::jmin (open, raisedCosine (left / edge));

                // A linked step moves from the last one's height to its own.
                auto level = info.level;
                if (! info.runStartsHere)
                {
                    const auto into = q - bounds[(size_t) k];
                    const auto move = juce::jmin (ramp, stepLength (k));
                    if (into < move)
                        level = info.previous + (info.level - info.previous) * raisedCosine (into / move);
                }
                if (decaySteps > 0.0)
                    open *= std::exp ((float) (-decayLn * t / decaySteps));
                return level * open;
            }

            // Both channels' values at `position` (MONO: the same; STEREO: the rows, WIDTH apart).
            std::pair<float, float> values (double position) const noexcept
            {
                const auto l = value (0, position);
                if (! stereo)
                    return { l, l };
                const auto r = value (1, position);
                const auto same = 0.5f * (1.0f - width);
                return { l + (r - l) * same, r + (l - r) * same };
            }

            int getLength() const noexcept { return length; }

        private:
            double stepLength (int k) const noexcept { return bounds[(size_t) k + 1] - bounds[(size_t) k]; }
            // From step a's start to step b's, forward round the loop.
            double distance (int a, int b) const noexcept
            {
                auto d = bounds[(size_t) b] - bounds[(size_t) a];
                return d < 0.0 ? d + length : d;
            }

            struct Info
            {
                bool on = true, runStartsHere = true;
                float level = 1.0f, previous = 0.0f;
                double runStart = 0.0, closeAt = 0.5;   // in steps: where its run starts (in the loop), and how long after that the gate closes
            };
            int length = 16;
            double inverseLength = 1.0 / 16.0;
            bool stereo = false;
            float width = 1.0f;
            std::array<double, maxSteps + 1> bounds {};
            std::array<std::array<Info, maxSteps>, 2> infos {};
            double ramp = 0.0, minRamp = 0.0, decaySteps = 0.0;
        };
    }
}
