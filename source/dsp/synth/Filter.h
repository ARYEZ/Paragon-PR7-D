#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// The FILTER: low-pass, high-pass, band-pass and band-stop at 6, 12, 18 and
// 24 dB per octave, like the basic filter types on Avenger's filter section
// (our own design, not a copy of any of its algorithms).
//
// Built from "zero-delay feedback" state-variable filters (Vadim Zavalishin's
// published topology): they stay smooth and in tune when the cutoff is swept
// fast by an envelope, and stay stable at any setting. 12 dB is one of them;
// 24 dB is two in a row (the resonance on the first); 18 dB is one plus a
// gentle one-pole; 6 dB is the one-pole alone, which has no resonance (a
// one-pole filter can't ring; Avenger's 6 dB has a resonance we don't model).
//
// DRIVE saturates the sound going in, smoothly (tanh), before the filter.
// The saturation uses "antiderivative anti-aliasing" (a published method,
// Parker et al. 2016), which removes most of the harsh folded-back tones a
// plain clipper would add, without oversampling.
namespace synth
{
    // Saved by index (fltType): only ever append.
    enum class FilterType { lp6, lp12, lp18, lp24, hp6, hp12, hp18, hp24, bp12, bp24, bs12, bs24 };
    inline const juce::StringArray filterTypeNames { "LP 6", "LP 12", "LP 18", "LP 24", "HP 6", "HP 12", "HP 18", "HP 24",
                                                     "BP 12", "BP 24", "BS 12", "BS 24" };

    // One channel's filter. The coefficients come from the voice, which
    // works them out once per few samples and glides between them.
    class FilterChannel
    {
    public:
        struct Coefficients
        {
            float g = 0.0f;    // tan (pi * cutoff / sample rate)
            float k = 1.4142f; // damping: 2 at no resonance... 1 / Q
            float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;   // the state-variable filter's, from g and k
            float onePole = 0.0f;                    // the one-pole's g / (1 + g)
            float kFlat = 1.4142f;                   // the second stage's damping (no resonance)
            float b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;   // ...and its coefficients

            static Coefficients make (double cutoffHz, double resonance, double sampleRate) noexcept
            {
                Coefficients c;
                const auto fc = juce::jlimit (10.0, 0.45 * sampleRate, cutoffHz);
                c.g = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
                // RESONANCE 0 to 1: Q from 0.5 (none) up to 30, on a curve that
                // spends most of the knob where it's musical.
                const auto q = 0.5 + 29.5 * std::pow (juce::jlimit (0.0, 1.0, resonance), 2.5);
                c.k = (float) (1.0 / q);
                c.setFromGK();
                return c;
            }

            void setFromGK() noexcept
            {
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                kFlat = 1.4142135f;   // Butterworth: the second stage adds slope, not a second peak
                b1 = 1.0f / (1.0f + g * (g + kFlat));
                b2 = g * b1;
                b3 = g * b2;
                onePole = g / (1.0f + g);
            }

            // Straight-line glide between two sets (per sample, between control updates).
            static Coefficients lerp (const Coefficients& from, const Coefficients& to, float t) noexcept
            {
                Coefficients c;
                c.g = from.g + (to.g - from.g) * t;
                c.k = from.k + (to.k - from.k) * t;
                c.setFromGK();
                return c;
            }
        };

        void reset() noexcept { s1 = s2 = t1 = t2 = p = 0.0f; }

        float process (float x, FilterType type, const Coefficients& c) noexcept
        {
            switch (type)
            {
                case FilterType::lp6:  return onePoleLow (x, c);
                case FilterType::hp6:  return x - onePoleLow (x, c);
                case FilterType::lp12: { float lp, bp, hp; svf (x, c, lp, bp, hp); return lp; }
                case FilterType::hp12: { float lp, bp, hp; svf (x, c, lp, bp, hp); return hp; }
                case FilterType::bp12: { float lp, bp, hp; svf (x, c, lp, bp, hp); return bp * c.k; }       // unity at the peak
                case FilterType::bs12: { float lp, bp, hp; svf (x, c, lp, bp, hp); return lp + hp; }
                case FilterType::lp18: { float lp, bp, hp; svf (x, c, lp, bp, hp); return onePoleLow (lp, c); }
                case FilterType::hp18: { float lp, bp, hp; svf (x, c, lp, bp, hp); return hp - onePoleLow (hp, c); }
                case FilterType::lp24: { float lp, bp, hp; svf (x, c, lp, bp, hp); return flat (lp, c, 0); }
                case FilterType::hp24: { float lp, bp, hp; svf (x, c, lp, bp, hp); return flat (hp, c, 1); }
                case FilterType::bp24: { float lp, bp, hp; svf (x, c, lp, bp, hp); return flat (bp * c.k, c, 2); }
                case FilterType::bs24: { float lp, bp, hp; svf (x, c, lp, bp, hp); return flat (lp + hp, c, 3); }
            }
            return x;
        }

    private:
        void svf (float x, const Coefficients& c, float& lp, float& bp, float& hp) noexcept
        {
            const auto v3 = x - s2;
            const auto v1 = c.a1 * s1 + c.a2 * v3;
            const auto v2 = s2 + c.a2 * s1 + c.a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            lp = v2;
            bp = v1;
            hp = x - c.k * v1 - v2;
        }

        // The second, unresonant stage for the 24 dB types: 0 low, 1 high, 2 band (unity peak), 3 notch.
        float flat (float x, const Coefficients& c, int output) noexcept
        {
            const auto v3 = x - t2;
            const auto v1 = c.b1 * t1 + c.b2 * v3;
            const auto v2 = t2 + c.b2 * t1 + c.b3 * v3;
            t1 = 2.0f * v1 - t1;
            t2 = 2.0f * v2 - t2;
            switch (output)
            {
                case 0: return v2;
                case 1: return x - c.kFlat * v1 - v2;
                case 2: return v1 * c.kFlat;
                default: return x - c.kFlat * v1;
            }
        }

        float onePoleLow (float x, const Coefficients& c) noexcept
        {
            const auto v = (x - p) * c.onePole;
            const auto y = v + p;
            p = y + v;
            return y;
        }

        float s1 = 0.0f, s2 = 0.0f, t1 = 0.0f, t2 = 0.0f, p = 0.0f;
    };

    // DRIVE's saturation, anti-aliased: tanh, by the first-order antiderivative method.
    class Saturator
    {
    public:
        void reset() noexcept { previous = previousLogCosh = 0.0; }

        // `gain` is DRIVE's push into the curve; the result is scaled so a full-scale input still peaks near 1.
        // (In double precision: the method divides a small difference by a small difference.)
        float process (float input, float gain, float makeUp) noexcept
        {
            const auto x = (double) input * gain;
            const auto difference = x - previous;
            const auto logCoshX = logCosh (x);
            double y;
            if (std::abs (difference) < 1.0e-5)
                y = std::tanh (0.5 * (x + previous));
            else
                y = (logCoshX - previousLogCosh) / difference;
            previous = x;
            previousLogCosh = logCoshX;
            return (float) y * makeUp;
        }

    private:
        // log (cosh x), without overflowing for large x.
        static double logCosh (double x) noexcept
        {
            const auto a = std::abs (x);
            return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
        }

        double previous = 0.0, previousLogCosh = 0.0;
    };
}
