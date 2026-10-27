#pragma once

#include "Modulation.h"

#include <array>
#include <cmath>

// TRAVEL (stage 4e): each oscillator's own way of moving through its
// wavetable, modelled on Avenger's RS/WT envelope (the manual's pages 48-51;
// our own design and code). Up to three editors:
//   - ENV: an envelope drawn across one cycle (points, curves): where in the
//     table, 0 (the first frame) to 1 (the last), over time.
//   - SEQ: a pattern of up to 16 steps, each playing the envelope its own way
//     (the step modes below), a SPEED period long (or two).
//   - MOD: a strength curve for each step (start, middle, end, bent).
// The position moves INDEX: index = INDEX + STRENGTH x position (STRENGTH -100%
// to +100%; velocity can lessen it), except the HIGH, MID and LOW steps, which
// set the frame outright. SMOOTH slows its moves.
//
// Our readings where the manual leaves things open: an envelope cycle is one
// SPEED period; a step's iterations divide its own length; PING PONG on the
// pattern runs it backwards, steps and all; a SILENCE step fades the
// oscillator out (and in) over a millisecond; SMOOTH is up to 500 ms.
namespace synth
{
    namespace travel
    {
        inline constexpr int maxSteps = 16;

        // Saved by index (osc*Tr*): only ever append.
        enum class Editors { env, envSeq, envSeqMod };
        inline const juce::StringArray editorNames { "Env", "Env+Seq", "Env+Seq+Mod" };
        enum class Trigger { voice, firstNote, lastNote };
        inline const juce::StringArray triggerNames { "Voice", "First Note", "Last Note" };
        enum class Mode { loop, pingPong, oneShot };
        inline const juce::StringArray modeNames { "Loop", "Ping Pong", "One Shot" };

        // A pattern step's way of playing the envelope (saved by index: only ever append).
        enum class StepMode { normal, trigger, pingPongHalf, reverse, flip, silence, high, mid, low };
        inline const juce::StringArray stepModeNames { "Normal", "Trigger", "Ping Pong (half)", "Reverse", "Flip", "Silence", "High", "Mid", "Low" };
        inline const juce::StringArray stepModeMarks { "x", "t", "PP", "R", "U", "S", "H", "M", "L" };
        inline bool takesIterations (StepMode m) noexcept { return m <= StepMode::flip; }

        // Iterations: 0.5, then 1 to 16.
        inline float clampIterations (float n) noexcept { return n < 0.75f ? 0.5f : (float) juce::jlimit (1, 16, juce::roundToInt (n)); }

        struct Step
        {
            StepMode mode = StepMode::normal;
            float iterations = 1.0f;   // 0.5, 1 to 16
            int length = 1;            // SPEED periods: 1 or 2
            bool operator== (const Step& o) const noexcept { return mode == o.mode && juce::exactlyEqual (iterations, o.iterations) && length == o.length; }
        };
        // MOD: a step's strength across it, 0 to 1 (start, an optional middle, end), the lines bent by `curve`.
        struct Strength
        {
            float start = 1.0f, middle = 1.0f, end = 1.0f;
            bool hasMiddle = false;
            float curve = 0.0f;        // -1 to 1
            bool operator== (const Strength& o) const noexcept
            {
                return juce::exactlyEqual (start, o.start) && juce::exactlyEqual (middle, o.middle) && juce::exactlyEqual (end, o.end)
                    && hasMiddle == o.hasMiddle && juce::exactlyEqual (curve, o.curve);
            }
            float at (float u) const noexcept
            {
                const auto line = [this] (float from, float to, float t) { return from + (to - from) * mod::bend (juce::jlimit (0.0f, 1.0f, t), curve); };
                if (! hasMiddle)
                    return line (start, end, u);
                return u < 0.5f ? line (start, middle, 2.0f * u) : line (middle, end, 2.0f * u - 1.0f);
            }
        };
        struct Pattern
        {
            std::array<Step, maxSteps> steps {};
            std::array<Strength, maxSteps> strength {};
            bool operator== (const Pattern& o) const noexcept { return steps == o.steps && strength == o.strength; }
        };
    }

    struct TravelSettings
    {
        bool on = false;
        travel::Editors editors = travel::Editors::env;
        travel::Trigger trigger = travel::Trigger::voice;
        travel::Mode mode = travel::Mode::loop;          // the envelope's (ENV alone)
        travel::Mode patternMode = travel::Mode::loop;   // the pattern's (with SEQ)
        int steps = 4;                     // 1 to 16 (with SEQ)
        float strength = 1.0f;             // -1 to 1
        float strengthVelocity = 0.0f;     // 0 to 1: how far a soft note lessens it...
        float velocityCurve = 0.0f;        // ...and its curve, -1 to 1
        float smooth = 0.0f;               // 0 to 1: slows the moves (up to 500 ms)
        float phase = 0.0f;                // 0 to 1: where in the cycle (or the pattern) it starts
        bool flip = false;                 // the envelope upside down
        float rateHz = 1.0f;               // FREE: cycles a second
        mod::LfoSync sync = mod::LfoSync::straight;
        int division = 5;                  // 1/4
        const mod::Shape* shape = nullptr;               // the envelope (null: a ramp up)
        const travel::Pattern* pattern = nullptr;        // the steps and strengths (null: plain)

        bool usesSequence() const noexcept { return editors != travel::Editors::env; }
        bool usesStrength() const noexcept { return editors == travel::Editors::envSeqMod; }
        double periodsPerSecond (double bpm) const noexcept
        {
            mod::LfoSettings asLfo;
            asLfo.rateHz = rateHz;
            asLfo.sync = sync;
            asLfo.division = division;
            return asLfo.cyclesPerSecond (bpm);
        }
    };

    namespace travel
    {
        // Where TRAVEL is at a moment.
        struct Point
        {
            float position = 0.0f;   // 0 to 1: through the envelope's levels...
            float strength = 1.0f;   // ...times this (MOD's), times STRENGTH
            bool absolute = false;   // HIGH / MID / LOW: `position` is the frame itself
            bool silent = false;     // SILENCE
        };

        inline double wrap01 (double x) noexcept { return x - std::floor (x); }

        // `periods`: SPEED periods since it started (PHASE is added here).
        inline Point at (const TravelSettings& s, double periods) noexcept
        {
            const auto envelope = [&s] (double x) -> float
            {
                const auto y = s.shape != nullptr && s.shape->count >= 2 ? s.shape->valueAt ((float) juce::jlimit (0.0, 1.0, x)) : (float) juce::jlimit (0.0, 1.0, x);
                return s.flip ? 1.0f - y : y;
            };
            // Loop, ping pong or once through a cycle `length` long: where in it.
            const auto place = [] (Mode mode, double p, double length) -> double
            {
                switch (mode)
                {
                    case Mode::oneShot: return juce::jlimit (0.0, length * 0.999999, p);
                    case Mode::pingPong:
                    {
                        const auto q = std::fmod (juce::jmax (0.0, p), 2.0 * length);
                        return q < length ? q : 2.0 * length - q;
                    }
                    case Mode::loop: break;
                }
                return std::fmod (juce::jmax (0.0, p), length);
            };

            Point point;
            if (! s.usesSequence())
            {
                const auto x = place (s.mode, periods + s.phase, 1.0);
                point.position = envelope (x);
                return point;
            }

            static const Pattern plain {};
            const auto& pattern = s.pattern != nullptr ? *s.pattern : plain;
            const auto count = juce::jlimit (1, maxSteps, s.steps);
            double total = 0.0;
            for (int k = 0; k < count; ++k)
                total += juce::jlimit (1, 2, pattern.steps[(size_t) k].length);
            const auto p = place (s.patternMode, periods + s.phase * total, total);

            // Which step, and how far through it.
            int k = 0;
            double start = 0.0;
            for (; k < count - 1; ++k)
            {
                const auto length = (double) juce::jlimit (1, 2, pattern.steps[(size_t) k].length);
                if (p < start + length)
                    break;
                start += length;
            }
            const auto& step = pattern.steps[(size_t) k];
            const auto u = juce::jlimit (0.0, 1.0, (p - start) / juce::jlimit (1, 2, step.length));
            const auto n = (double) clampIterations (step.iterations);
            const auto w = wrap01 (u * n);
            switch (step.mode)
            {
                case StepMode::normal: point.position = envelope (n < 1.0 ? u * n : w); break;
                case StepMode::trigger: point.position = envelope (w / n); break;
                case StepMode::pingPongHalf: point.position = envelope (w < 0.5 ? w : 1.0 - w); break;
                case StepMode::reverse: point.position = envelope (1.0 - (n < 1.0 ? u * n : w)); break;
                case StepMode::flip: point.position = 1.0f - envelope (n < 1.0 ? u * n : w); break;
                case StepMode::silence: point.silent = true; point.position = envelope (0.0); break;
                case StepMode::high: point.absolute = true; point.position = 1.0f; break;
                case StepMode::mid: point.absolute = true; point.position = 0.5f; break;
                case StepMode::low: point.absolute = true; point.position = 0.0f; break;
            }
            if (s.usesStrength())
                point.strength = juce::jlimit (0.0f, 1.0f, pattern.strength[(size_t) k].at ((float) u));
            return point;
        }

        // STRENGTH for a note of `velocity` (0 to 1).
        inline float strengthFor (const TravelSettings& s, float velocity) noexcept
        {
            const auto shaped = mod::bend (juce::jlimit (0.0f, 1.0f, velocity), s.velocityCurve);
            return s.strength * (1.0f - s.strengthVelocity * (1.0f - shaped));
        }

        // The frame (INDEX 0 to 1) TRAVEL puts the oscillator at.
        inline float indexFor (const Point& p, float index, float strength) noexcept
        {
            return p.absolute ? p.position : juce::jlimit (0.0f, 1.0f, index + strength * p.strength * p.position);
        }

        // SMOOTH as a time constant, in seconds (0: none).
        inline double smoothSeconds (float smooth) noexcept { return 0.5 * (double) smooth * smooth; }
    }
}
