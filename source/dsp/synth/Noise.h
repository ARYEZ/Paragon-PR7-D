#pragma once

#include <cstdint>

// Each oscillator's NOISE: white, pink or brown, or anywhere between (COLOUR
// 0 is white, 0.5 pink, 1 brown). Pink is Paul Kellet's well-known
// "economy" filter on white noise (-3 dB per octave); brown is white noise
// integrated with a slight leak (-6 dB per octave). Each is scaled to about
// the same loudness as the white, so turning COLOUR doesn't jump in level
// (our own scaling, set by ear-free measurement in the tests).
namespace synth
{
    class Noise
    {
    public:
        explicit Noise (std::uint32_t seed = 0x9e3779b9u) noexcept : state (seed == 0 ? 1u : seed) {}

        // White noise, -1 to 1.
        float white() noexcept
        {
            // xorshift32: fast, and plenty random for noise.
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
        }

        float next (float colour) noexcept
        {
            const auto w = white();
            if (colour <= 0.0f)
                return w;

            // Pink (Kellet's economy version).
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            const auto pink = (b0 + b1 + b2 + w * 0.1848f) * 0.25f;

            if (colour <= 0.5f)
                return w + (pink - w) * (colour * 2.0f);

            brown = (brown + w * 0.02f) * 0.998f;
            const auto brownScaled = brown * 3.5f;
            return pink + (brownScaled - pink) * ((colour - 0.5f) * 2.0f);
        }

    private:
        std::uint32_t state;
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, brown = 0.0f;
    };
}
