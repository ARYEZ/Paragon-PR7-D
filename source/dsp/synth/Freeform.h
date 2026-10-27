#pragma once

#include "Wavetable.h"

#include <cmath>
#include <vector>

// FREEFORM (stage 3d, part 2): one cycle drawn by hand, modelled on Avenger's
// freeform editor (our own design and code). The drawing is 512 points; it
// plays as a one-frame wavetable built from it after:
//   SMOOTH A / B  rounding sharp corners (which are what sound bright and
//                 clicky), A before SPLIT and B after it (SPLIT at 100%:
//                 A everywhere, as Avenger's default);
//   EDGE          joining the cycle's end to its start, so it doesn't jump
//                 there: ADAPT moves both ends to meet in the middle, ZERO
//                 fades them to silence, SYMMETRY blends the wave with its
//                 mirror image (left half into right);
//   NORMALIZE     as loud as it can be without going over full scale.
// Kept with the preset and the project, as the FFT editor's curve is.
namespace synth
{
    // Saved by index: only ever append.
    enum class FreeformEdge { adapt, zero, symmetry };
    inline const juce::StringArray freeformEdgeNames { "Adapt", "Zero", "Symmetry" };

    struct FreeformDrawing
    {
        static constexpr int points = 512;
        static constexpr int cycle = 2048;   // samples the drawing is rendered to

        std::vector<float> values = std::vector<float> ((size_t) points, 0.0f);   // -1 to 1
        float smoothA = 0.0f, smoothB = 0.0f;   // 0 to 1
        float split = 1.0f;                     // 0 to 1: where B takes over from A
        bool normalise = true;
        float edge = 0.0f;                      // 0 to 1
        FreeformEdge edgeMode = FreeformEdge::adapt;

        bool operator== (const FreeformDrawing& o) const noexcept
        {
            return values == o.values && juce::exactlyEqual (smoothA, o.smoothA) && juce::exactlyEqual (smoothB, o.smoothB)
                && juce::exactlyEqual (split, o.split) && normalise == o.normalise && juce::exactlyEqual (edge, o.edge) && edgeMode == o.edgeMode;
        }
        bool isFlat() const noexcept { return std::all_of (values.begin(), values.end(), [] (float v) { return std::abs (v) < 1.0e-4f; }); }

        // The drawing to start from (a menu on the editor): flat (as Avenger's), or a basic shape.
        static inline const juce::StringArray startingShapes { "Flat", "Sine", "Saw", "Square", "Triangle" };
        static FreeformDrawing startingShape (int which)
        {
            FreeformDrawing d;
            for (int i = 0; i < points; ++i)
            {
                const auto t = (double) i / points;
                double v = 0.0;
                switch (which)
                {
                    case 1: v = std::sin (juce::MathConstants<double>::twoPi * t); break;
                    case 2: v = 2.0 * t - 1.0; break;
                    case 3: v = t < 0.5 ? 0.8 : -0.8; break;
                    case 4: v = 1.0 - 4.0 * std::abs (t - 0.25 - std::floor (t + 0.25)); break;
                    default: break;
                }
                d.values[(size_t) i] = (float) v;
            }
            return d;
        }

        // The cycle as played (before building its table): drawn, smoothed, edges joined.
        std::vector<float> render() const
        {
            std::vector<float> wave ((size_t) cycle);
            for (int i = 0; i < cycle; ++i)
            {
                const auto position = (float) i * (float) points / (float) cycle;
                const auto a = (int) position;
                const auto t = position - (float) a;
                // The last point carries on its slope rather than bending back to
                // the first: what's drawn ends at the cycle's end, so a jump back
                // to the start sits exactly there, where EDGE joins it.
                const auto next = a + 1 < points ? values[(size_t) a + 1] : 2.0f * values[(size_t) a] - values[(size_t) a - 1];
                wave[(size_t) i] = values[(size_t) a] + (next - values[(size_t) a]) * t;
            }

            // SMOOTH: three passes of a moving average (close to a gaussian), up
            // to an eighth of the cycle wide, round the cycle.
            const auto smoothed = [&wave] (float amount)
            {
                auto out = wave;
                const auto width = juce::roundToInt (amount * (float) cycle / 8.0f);
                if (width < 1)
                    return out;
                std::vector<float> next ((size_t) cycle);
                for (int pass = 0; pass < 3; ++pass)
                {
                    double sum = 0.0;
                    for (int k = -width; k <= width; ++k)
                        sum += out[(size_t) ((k + cycle) % cycle)];
                    for (int i = 0; i < cycle; ++i)
                    {
                        next[(size_t) i] = (float) (sum / (2 * width + 1));
                        sum += out[(size_t) ((i + width + 1) % cycle)] - out[(size_t) ((i - width + cycle) % cycle)];
                    }
                    std::swap (out, next);
                }
                return out;
            };
            const auto a = smoothed (smoothA);
            const auto useB = split < 0.999f;
            const auto b = useB ? smoothed (smoothB) : a;
            // A before SPLIT, B after, blended over 32 samples at SPLIT and at the cycle's start.
            const auto splitAt = split * (float) cycle;
            constexpr float blend = 32.0f;
            for (int i = 0; i < cycle; ++i)
            {
                if (! useB)
                {
                    wave[(size_t) i] = a[(size_t) i];
                    continue;
                }
                const auto x = (float) i;
                auto towardsB = juce::jlimit (0.0f, 1.0f, (x - splitAt) / blend + 0.5f);
                if (x > (float) cycle - blend * 0.5f)
                    towardsB *= juce::jlimit (0.0f, 1.0f, ((float) cycle - x) / (blend * 0.5f));   // back to A at the end, as it starts
                wave[(size_t) i] = a[(size_t) i] + (b[(size_t) i] - a[(size_t) i]) * towardsB;
            }

            // EDGE: over up to a quarter of the cycle at each end.
            const auto e = juce::jlimit (0.0f, 1.0f, edge);
            if (e > 0.0f)
            {
                const auto region = juce::jmax (1, juce::roundToInt (e * (float) cycle / 4.0f));
                const auto rise = [] (float t) { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t); };   // 0 to 1, smoothly
                switch (edgeMode)
                {
                    case FreeformEdge::zero:
                        for (int i = 0; i < region; ++i)
                        {
                            const auto g = rise ((float) i / (float) region);
                            wave[(size_t) i] *= g;
                            wave[(size_t) (cycle - 1 - i)] *= g;
                        }
                        break;
                    case FreeformEdge::adapt:
                    {
                        const auto start = wave.front(), end = wave.back();
                        const auto middle = 0.5f * (start + end);
                        for (int i = 0; i < region; ++i)
                        {
                            const auto g = 1.0f - rise ((float) i / (float) region);
                            wave[(size_t) i] += (middle - start) * g;
                            wave[(size_t) (cycle - 1 - i)] += (middle - end) * g;
                        }
                        break;
                    }
                    case FreeformEdge::symmetry:
                    {
                        const auto original = wave;
                        for (int i = 0; i < cycle; ++i)
                            wave[(size_t) i] = original[(size_t) i] + (0.5f * (original[(size_t) i] + original[(size_t) (cycle - 1 - i)]) - original[(size_t) i]) * e;
                        break;
                    }
                }
            }
            return wave;
        }

        // As a one-frame wavetable source (named "Freeform").
        WavetableSource source() const
        {
            return { "Freeform", cycle, std::make_shared<std::vector<float>> (render()) };
        }

        // Saved as text: the settings, then the points (3 decimals).
        juce::String encode() const
        {
            juce::StringArray items { juce::String (smoothA, 3), juce::String (smoothB, 3), juce::String (split, 3), normalise ? "1" : "0",
                                      juce::String (edge, 3), juce::String ((int) edgeMode) };
            for (const auto v : values)
                items.add (juce::String (v, 3));
            return items.joinIntoString (" ");
        }
        static FreeformDrawing decode (const juce::String& text)
        {
            FreeformDrawing d;
            const auto items = juce::StringArray::fromTokens (text, " ", {});
            if (items.size() != 6 + points)
                return d;
            d.smoothA = juce::jlimit (0.0f, 1.0f, items[0].getFloatValue());
            d.smoothB = juce::jlimit (0.0f, 1.0f, items[1].getFloatValue());
            d.split = juce::jlimit (0.0f, 1.0f, items[2].getFloatValue());
            d.normalise = items[3] == "1";
            d.edge = juce::jlimit (0.0f, 1.0f, items[4].getFloatValue());
            d.edgeMode = (FreeformEdge) juce::jlimit (0, freeformEdgeNames.size() - 1, items[5].getIntValue());
            for (int i = 0; i < points; ++i)
                d.values[(size_t) i] = juce::jlimit (-1.0f, 1.0f, items[6 + i].getFloatValue());
            return d;
        }
    };
}
