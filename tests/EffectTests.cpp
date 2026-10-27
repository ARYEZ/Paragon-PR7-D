// The PR7 family's shared Circuit solver (dsp/effects/Circuit.h), kept for
// later stages: it models analog stages part by part.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_core/juce_core.h>

#include "dsp/effects/Circuit.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;

    float dB (float ratio) { return 20.0f * std::log10 (ratio); }

    // The level of `hz` in `samples` from `fromSeconds` on (Goertzel), as a fraction of full scale.
    float levelAt (const std::vector<float>& samples, double hz, double fromSeconds = 0.1)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto n = samples.size() - from;
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (float) (2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) n);
    }
}

TEST_CASE ("The circuit solver behaves like the circuit: an RC low-pass", "[effects][circuit]")
{
    // 1k and 1 uF: the corner is at 159 Hz, falling 6 dB per octave.
    effects::Circuit c;
    const auto in = c.node(), out = c.node();
    c.resistor (in, out, 1.0e3);
    c.capacitor (out, 0, 1.0e-6);
    c.setInput (in);
    c.setOutput (out);
    c.prepare (sampleRate);

    const auto gainAt = [&c] (double hz)
    {
        c.reset();
        std::vector<float> output;
        for (int i = 0; i < (int) sampleRate; ++i)
            output.push_back ((float) c.process (std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate)));
        return dB (levelAt (output, hz, 0.5));
    };
    CHECK_THAT (gainAt (159.15), WithinAbs (-3.01, 0.1));
    CHECK_THAT (gainAt (1591.5), WithinAbs (-20.04, 0.2));
    CHECK_THAT (gainAt (20.0), WithinAbs (0.0, 0.1));
}
