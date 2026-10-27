#pragma once

#include "SynthSettings.h"
#include "WaveTables.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// SHAPE (stage 3b): bending an oscillator's wave while it plays, modelled on
// Avenger's shape controls (our own design and code; the manual describes
// what each does, not how):
//
//   SKEW     squashes one side of the wave and stretches the other (right of
//            centre the left side, left of centre the right side).
//   PINCH    zooms into the middle of the wave (right) or out of it (left).
//   FORMANT  plays the wave faster and leaves a gap after it (left), or
//            slower so only its start fits in a cycle (right), the classic
//            formant / "window sync" sound; MODE says what happens at the
//            edges: CUT (a hard edge), FADE (the end fades to silence),
//            X-FADE (both ends fade) or X-FADE 2 (the end fades into the
//            start's level, so it never jumps).
//   CRUSH    RATE holds the wave in steps (down to 3 a cycle), BIT rounds it
//            to fewer levels (down to a square), BOTH does both.
//
// Kept clean, which is what costs Avenger CPU (it oversamples):
//   - SKEW and PINCH move where in the wave each sample reads, by curves
//     that are smooth all the way round the cycle (no corner where it
//     wraps), and the oscillator reads a band-limited level chosen for the
//     fastest they play any part of the wave.
//   - FORMANT and CRUSH make hard edges, jumps and corners. Each is where
//     the phase says it is, so it's smoothed over the two samples round it
//     ("polyBLEP" for a jump, "polyBLAMP" for a corner: what a band-limited
//     edge would look like), which keeps most of what would fold back from
//     being made at all.
//   - BIT's rounding is smoothed by averaging the rounded wave between
//     samples (first-order antiderivative anti-aliasing, as the filter's DRIVE does).
// At their neutral settings nothing here runs: the oscillator plays exactly as without it.
namespace synth
{
    // SKEW and PINCH's curve, worked out once when they change (a table read
    // per sample, rather than four cosines). Where in the wave a point u of
    // the cycle reads, both 0 to 1.
    struct WarpTable
    {
        static constexpr int size = 1024;
        float skew = 0.0f, pinch = 0.0f;
        bool identity = true;
        int count = size;   // points used (a note's own, modulated table uses fewer: `gridSize`)
        std::array<float, size + 1> points {};

        // Each control's two passes: SKEW u + a (1 - cos 2 pi u) / 2 pi (one
        // side fast, the other slow), PINCH u + b sin (2 pi u) / 2 pi (the
        // middle slow and the edges fast, or the other way round). Smooth
        // round the cycle, and never running backwards.
        static float skewA (float skew) noexcept { return 0.95f * juce::jlimit (-1.0f, 1.0f, skew); }
        static float pinchB (float pinch) noexcept { return 0.9f * juce::jlimit (-1.0f, 1.0f, pinch); }

        void update (float newSkew, float newPinch) noexcept
        {
            const auto newIdentity = juce::exactlyEqual (newSkew, 0.0f) && juce::exactlyEqual (newPinch, 0.0f);
            if (newIdentity == identity && juce::exactlyEqual (newSkew, skew) && juce::exactlyEqual (newPinch, pinch))
                return;
            skew = newSkew;
            pinch = newPinch;
            identity = newIdentity;
            count = size;
            if (identity)
                return;
            const auto a = (double) skewA (skew), b = (double) pinchB (pinch);
            constexpr auto twoPi = juce::MathConstants<double>::twoPi;
            for (int i = 0; i <= size; ++i)
            {
                auto u = (double) i / size;
                for (int pass = 0; pass < 2; ++pass)
                    u += a * (1.0 - std::cos (twoPi * u)) / twoPi;
                for (int pass = 0; pass < 2; ++pass)
                    u += b * std::sin (twoPi * u) / twoPi;
                points[(size_t) i] = (float) juce::jlimit (0.0, 1.0, u);
            }
            points[0] = 0.0f;
            points[size] = 1.0f;
        }

        float operator() (float u) const noexcept
        {
            if (identity)
                return u;
            const auto position = juce::jlimit (0.0f, (float) count, u * (float) count);
            const auto index = juce::jmin ((int) position, count - 1);
            const auto fraction = position - (float) index;
            return points[(size_t) index] + (points[(size_t) index + 1] - points[(size_t) index]) * fraction;
        }

        // SKEW and PINCH moved by the matrix (stage 4f): a note's own table, made
        // every 16 samples from `Grid` (the curves worked out ahead at 65 settings
        // of each, blended), at 256 points: no trigonometry, about 3,000
        // multiplies and adds an update. Within 0.001 of the exact curve.
        static constexpr int gridSize = 256;
        struct Grid
        {
            static constexpr int levels = 65;
            std::vector<float> skewRows, pinchRows;   // levels x (gridSize + 1): the first pass's curves, and the second's

            static const Grid& get()
            {
                static const Grid grid;
                return grid;
            }
            Grid() : skewRows ((size_t) levels * (gridSize + 1)), pinchRows ((size_t) levels * (gridSize + 1))
            {
                constexpr auto twoPi = juce::MathConstants<double>::twoPi;
                for (int r = 0; r < levels; ++r)
                {
                    const auto control = -1.0f + 2.0f * (float) r / (float) (levels - 1);
                    const auto a = (double) skewA (control), b = (double) pinchB (control);
                    for (int i = 0; i <= gridSize; ++i)
                    {
                        auto u = (double) i / gridSize, v = u;
                        for (int pass = 0; pass < 2; ++pass)
                            u += a * (1.0 - std::cos (twoPi * u)) / twoPi;
                        for (int pass = 0; pass < 2; ++pass)
                            v += b * std::sin (twoPi * v) / twoPi;
                        skewRows[(size_t) (r * (gridSize + 1) + i)] = (float) juce::jlimit (0.0, 1.0, u);
                        pinchRows[(size_t) (r * (gridSize + 1) + i)] = (float) juce::jlimit (0.0, 1.0, v);
                    }
                }
            }
        };

        void updateFromGrid (float newSkew, float newPinch) noexcept
        {
            const auto newIdentity = juce::exactlyEqual (newSkew, 0.0f) && juce::exactlyEqual (newPinch, 0.0f);
            if (count == gridSize && newIdentity == identity && juce::exactlyEqual (newSkew, skew) && juce::exactlyEqual (newPinch, pinch))
                return;
            skew = newSkew;
            pinch = newPinch;
            identity = newIdentity;
            count = gridSize;
            if (identity)
                return;
            const auto& grid = Grid::get();
            const auto rowOf = [] (float control, int& row, float& blend)
            {
                const auto position = (juce::jlimit (-1.0f, 1.0f, control) + 1.0f) * 0.5f * (float) (Grid::levels - 1);
                row = juce::jmin ((int) position, Grid::levels - 2);
                blend = position - (float) row;
            };
            int sRow = 0, pRow = 0;
            float sBlend = 0.0f, pBlend = 0.0f;
            rowOf (skew, sRow, sBlend);
            rowOf (pinch, pRow, pBlend);
            const auto* s0 = grid.skewRows.data() + sRow * (gridSize + 1);
            const auto* s1 = s0 + gridSize + 1;
            const auto* p0 = grid.pinchRows.data() + pRow * (gridSize + 1);
            const auto* p1 = p0 + gridSize + 1;
            // Only one of them moved (the usual case): its two rows blended, nothing more.
            if (juce::exactlyEqual (pinch, 0.0f) || juce::exactlyEqual (skew, 0.0f))
            {
                const auto* a = juce::exactlyEqual (pinch, 0.0f) ? s0 : p0;
                const auto* b = juce::exactlyEqual (pinch, 0.0f) ? s1 : p1;
                const auto blend = juce::exactlyEqual (pinch, 0.0f) ? sBlend : pBlend;
                for (int i = 0; i <= gridSize; ++i)
                    points[(size_t) i] = a[i] + (b[i] - a[i]) * blend;
                points[0] = 0.0f;
                points[(size_t) gridSize] = 1.0f;
                return;
            }
            for (int i = 0; i <= gridSize; ++i)
            {
                const auto u = s0[i] + (s1[i] - s0[i]) * sBlend;                    // SKEW's passes...
                const auto x = u * (float) gridSize;                                // ...then PINCH's, read between its points
                const auto j = juce::jmin ((int) x, gridSize - 1);
                const auto f = x - (float) j;
                const auto a = p0[j] + (p0[j + 1] - p0[j]) * f;
                const auto b = p1[j] + (p1[j + 1] - p1[j]) * f;
                points[(size_t) i] = a + (b - a) * pBlend;
            }
            points[0] = 0.0f;
            points[(size_t) gridSize] = 1.0f;
        }

        // The fastest the curve plays any part of the wave.
        float speed() const noexcept
        {
            const auto a = std::abs (skewA (skew)), b = std::abs (pinchB (pinch));
            return (1.0f + a) * (1.0f + a) * (1.0f + b) * (1.0f + b);
        }
    };

    struct WaveBend
    {
        // std::floor and std::round are library calls on a plain x86-64 build
        // (and on Windows), a few nanoseconds each, several a sample per copy:
        // most of SHAPE's cost before these. Truncating is enough for the
        // non-negative values used here.
        static float floorPositive (float x) noexcept { return (float) (int) x; }
        static double floorAny (double x) noexcept
        {
            const auto t = (double) (std::int64_t) x;
            return t > x ? t - 1.0 : t;
        }

        bool active = false;
        const WarpTable* warp = nullptr;   // SKEW and PINCH (null: neither)
        float k = 1.0f;            // FORMANT: how many times faster the wave plays
        bool hasFormant = false;
        FormantMode mode = FormantMode::cut;
        float edge = 0.25f;        // FORMANT's fades: the part of the wave they take
        int steps = 0;             // RATE: steps a cycle (0: off)
        int levels = 0;            // BIT: levels each side of zero (0: off)
        float bitGain = 1.0f;      // BIT's level made up, so the loudest step reaches full scale
        float speed = 1.0f;        // the fastest the bent wave plays any of the original (and some headroom): for choosing a table level
        bool syncEdge = false;     // SYNC (not windowed) restarts its copy at the cycle's start: an edge there too (set by the engine)
        static constexpr float headroom = 1.6f;

        // From the oscillator's settings and its SKEW / PINCH curve (once an
        // interval; then forIncrement for each copy).
        static WaveBend make (const OscSettings& s, const WarpTable& warpTable) noexcept
        {
            WaveBend b;
            b.active = s.hasBend();
            if (! b.active)
                return b;
            b.warp = warpTable.identity ? nullptr : &warpTable;
            const auto formant = juce::jlimit (-1.0f, 1.0f, s.formant);
            b.hasFormant = ! juce::exactlyEqual (formant, 0.0f);
            b.k = formant < 0.0f ? std::exp2 (-3.0f * formant) : std::exp2 (-2.0f * formant);   // up to 8x faster, or 4x slower
            b.mode = s.formantMode;
            b.edge = 0.25f * juce::jmin (1.0f, 4.0f * std::abs (formant));   // the fades grow in with FORMANT, so it starts smoothly from 0
            const auto crush = juce::jlimit (0.0f, 1.0f, s.crush);
            if (crush > 0.0f)
            {
                if (s.crushMode != CrushMode::bit)
                    b.steps = juce::roundToInt (3.0f * std::pow (256.0f / 3.0f, 1.0f - crush));
                if (s.crushMode != CrushMode::rate)
                {
                    b.levels = juce::jmax (1, juce::roundToInt (std::exp2 (7.0f * (1.0f - crush))));
                    b.bitGain = (float) b.levels / ((float) b.levels - 0.5f);
                }
            }
            b.speed = b.k * (b.warp != nullptr ? b.warp->speed() : 1.0f);
            // FORMANT's bursts (and, less, SKEW and PINCH's warping) give every
            // harmonic side tones a note's pitch either side; a level whose top
            // harmonic only just fits under the limit would fold them back
            // into hearing (measured: a compressed saw at 880 Hz, -25 dB). This
            // leaves them room.
            if (b.hasFormant || b.warp != nullptr)
                b.speed *= headroom;
            return b;
        }

        // For a copy playing at `increment` (phase per sample): RATE's steps
        // can't be shorter than two samples (finer than that, there's nothing
        // to hear, and the edges' smoothing would overlap).
        WaveBend forIncrement (float increment) const noexcept
        {
            auto b = *this;
            if (b.steps > 0 && (float) b.steps * increment > 0.5f)
                b.steps = 0;
            return b;
        }

        static float rise (float x) noexcept   // 0 to 1, smoothly (a raised cosine), as x goes 0 to 1
        {
            static const float* sine = tables::get (tables::Wave::sine, tables::numLevels - 1);
            return 0.5f - 0.5f * tables::read (sine, 0.25f + 0.5f * x);
        }

        float warped (float u) const noexcept { return warp != nullptr ? (*warp) (u) : u; }

        // The bent wave at `phase` (0 to 1) before CRUSH, reading the plain
        // wave with `read (x)`. At an edge, `fromLeft` gives the value just
        // before it rather than just after.
        template <typename Read>
        float shape (float phase, Read&& read, bool fromLeft = false) const noexcept
        {
            if (! hasFormant)
                return read (warped (phase));

            // FORMANT: the stretch of the cycle that plays the wave (u from 0 to 1 across it)...
            const auto p = phase * k;
            const auto inside = k <= 1.0f || p < 1.0f || (fromLeft && p <= 1.0f);
            if (! inside)
                return mode == FormantMode::crossfade2 ? read (0.0f) : 0.0f;   // the gap after it
            const auto u = k <= 1.0f ? phase : p;
            auto value = read (warped (u) * juce::jmin (1.0f, k));

            // ...and its edges.
            if (edge > 0.0f && mode != FormantMode::cut)
            {
                const auto fadeOut = u > 1.0f - edge ? rise ((1.0f - u) / edge) : 1.0f;
                switch (mode)
                {
                    case FormantMode::cut:        break;
                    case FormantMode::fade:       value *= fadeOut; break;
                    case FormantMode::crossfade:  value *= fadeOut * (u < 0.5f * edge ? rise (u / (0.5f * edge)) : 1.0f); break;
                    case FormantMode::crossfade2: value = read (0.0f) + (value - read (0.0f)) * fadeOut; break;
                }
            }
            return value;
        }

        // The same with RATE's steps: held at the start of each step.
        template <typename Read>
        float stepped (float phase, Read&& read) const noexcept
        {
            if (steps == 0)
                return shape (phase, read);
            return shape (floorPositive (phase * (float) steps) / (float) steps, read);
        }

        // Corrections for a jump (BLEP) and a corner (BLAMP), `t` samples from it (|t| < 1).
        static float blep (float t) noexcept
        {
            return t < 0.0f ? 0.5f * (t + 1.0f) * (t + 1.0f) : -0.5f * (1.0f - t) * (1.0f - t);
        }
        static float blamp (float t) noexcept
        {
            const auto x = t < 0.0f ? t + 1.0f : 1.0f - t;
            return x * x * x * (1.0f / 6.0f);
        }

        // One sample of the bent wave (before BIT), its edges smoothed: what the oscillator plays.
        template <typename Read>
        float sample (float phase, float increment, Read&& read) const noexcept
        {
            auto value = stepped (phase, read);
            if (increment <= 0.0f || (steps == 0 && ! hasFormant && ! syncEdge))
                return value;
            const auto near = [&] (float at)   // how many samples from `at` (wrapping round the cycle)
            {
                auto d = phase - at;   // -1 to 1
                d += d < -0.5f ? 1.0f : d > 0.5f ? -1.0f : 0.0f;
                return d / increment;
            };
            if (steps > 0)
            {
                // Every step's start is a jump (between steps the wave holds still: no corners).
                const auto j = floorPositive (phase * (float) steps + 0.5f);
                const auto t = near (j / (float) steps);
                if (std::abs (t) < 1.0f)
                {
                    const auto stepValue = [&] (float index)
                    {
                        const auto i = index < 0.0f ? index + (float) steps : index >= (float) steps ? index - (float) steps : index;
                        return shape (i / (float) steps, read);
                    };
                    value += (stepValue (j) - stepValue (j - 1.0f)) * blep (t);
                }
                return value;
            }

            // FORMANT (and SYNC): the cycle's start, and (FORMANT playing faster) the end of the
            // wave, are where the wave can jump or turn a corner. The slopes
            // either side are measured a hair away from the edge.
            const auto edgeAt = [&] (float after, float before, float t)
            {
                constexpr auto hair = 1.0e-4f;
                const auto right = shape (after, read), left = shape (before, read, true);
                const auto slopeRight = (shape (after + hair, read) - right) / hair;
                const auto slopeLeft = (left - shape (before - hair, read, true)) / hair;
                value += (right - left) * blep (t) + (slopeRight - slopeLeft) * increment * blamp (t);
            };
            if (const auto t = near (0.0f); std::abs (t) < 1.0f)
                edgeAt (0.0f, 1.0f, t);
            if (hasFormant && k > 1.0f)
                if (const auto t = near (1.0f / k); std::abs (t) < 1.0f)
                    edgeAt (1.0f / k, 1.0f / k, t);
            return value;
        }

        // BIT: the wave rounded to `levels` each side of zero (no level at zero:
        // at 1, a square), smoothed between samples. `previous` is the last
        // sample's input, kept per copy.
        float bits (float value, float& previous) const noexcept
        {
            if (levels == 0)
                return value;
            // Just inside full scale: a peak of exactly 1 would round up to a level above.
            value = juce::jlimit (-0.99999f, 0.99999f, value);
            const auto l = (double) levels;
            const auto round = [l] (double v) { return (floorAny (v * l) + 0.5) / l; };
            const auto integral = [l] (double v)   // of the rounded wave from 0 to v
            {
                v = std::abs (v);
                const auto m = (double) (std::int64_t) (v * l);
                return (m + 0.5) * v / l - m * (m + 1.0) / (2.0 * l * l);
            };
            const auto from = (double) previous, to = (double) value;
            previous = value;
            const auto out = std::abs (to - from) > 1.0e-5 ? (integral (to) - integral (from)) / (to - from)
                                                           : round (0.5 * (from + to));
            return (float) out * bitGain;
        }
    };
}
