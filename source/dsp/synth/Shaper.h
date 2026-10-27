#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

// The SHAPER (stage 3e): a distortion on each note, modelled on Avenger's
// shaper section (our own design and code). As the manual describes it: a
// distortion MODEL and DRIVE (with DRIVE VELOCITY), two parametric EQ bands
// before the distortion to colour it (EQ SPLIT puts band 1 on the left and
// band 2 on the right, for width), up to 4 shapers one after another, each
// before or after the FILTER, and any oscillators routed to them. The manual
// doesn't list Avenger's distortion models: these are our own. MIX and
// OUTPUT are ours too (the manual has neither).
//
// Against aliasing (the harsh, out-of-tune tones a distortion's harmonics
// make when they pass half the sample rate and fold back down), each shaper
// runs at twice the sample rate (a 47-tap half-band filter each way, flat to
// 18 kHz, -80 dB on anything that would fold below 18 kHz), and each model
// takes the first-order antiderivative method on top (as the FILTER's DRIVE):
// the curve's average between one sample and the next rather than its value
// at each. Measured: a hard-driven 1.76 kHz tone's folded noise goes from
// about -30 dB (a plain distortion) to about -60 dB. The cost: 23 samples of
// delay (half a millisecond) through a shaper; MIX's dry sound is delayed to
// match. The FOLD models, pushed hard on high notes, still make more
// harmonics than this can hold (measured in the tests).
namespace synth
{
    inline constexpr int numShapers = 4;

    // Saved by index (shp*Model): only ever append.
    enum class ShaperModel { soft, hard, tube, diode, smooth, cubic, sineFold, triangleFold, rectify, halfRectify };
    inline const juce::StringArray shaperModelNames { "Soft", "Hard", "Tube", "Diode", "Smooth", "Cubic",
                                                      "Sine Fold", "Tri Fold", "Rectify", "Half Rect" };
    // Saved by index (shp*Place): only ever append.
    inline const juce::StringArray shaperPlaceNames { "Pre Filter", "Post Filter" };

    struct ShaperSettings
    {
        bool on = false;
        ShaperModel model = ShaperModel::soft;
        float drive = 0.3f;               // 0 to 1: 0 to +36 dB into the curve
        float driveVelocity = 0.0f;       // 0 to 1: how much softer notes are driven less
        std::array<float, 2> eqGainDb { 0.0f, 0.0f };          // -18 to 18 (0: that band off)
        std::array<float, 2> eqFreqHz { 300.0f, 3000.0f };
        std::array<float, 2> eqQ { 0.7f, 0.7f };
        bool eqSplit = false;             // band 1 on the left only, band 2 on the right only
        bool afterFilter = false;         // PLACE: before the FILTER (as most synths distort) or after it
        float mix = 1.0f;                 // 0 to 1: the distorted sound against the sound coming in
        float outputDb = 0.0f;            // -24 to 12
    };

    namespace shaper
    {
        inline constexpr float maxDriveDb = 36.0f;

        // DRIVE (0 to 1) as the gain into the curve.
        inline float driveGain (float drive) noexcept { return std::exp (drive * maxDriveDb * 0.11512925f); }   // ln 10 / 20

        // log (cosh x) without overflowing.
        inline double logCosh (double x) noexcept
        {
            const auto a = std::abs (x);
            return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
        }

        inline constexpr double tubeBias = 0.35;   // TUBE's lean: even harmonics
        inline constexpr double diodeKnee = 0.5;   // DIODE: the negative side flattens at -0.5

        // Each model's curve...
        inline double curve (ShaperModel model, double x) noexcept
        {
            switch (model)
            {
                case ShaperModel::soft: return std::tanh (x);
                case ShaperModel::hard: return juce::jlimit (-1.0, 1.0, x);
                case ShaperModel::tube: return std::tanh (x + tubeBias) - std::tanh (tubeBias);
                case ShaperModel::diode: return x >= 0.0 ? 1.0 - std::exp (-x) : -diodeKnee * (1.0 - std::exp (x / diodeKnee));
                case ShaperModel::smooth: return x / (1.0 + std::abs (x));
                case ShaperModel::cubic: return std::abs (x) <= 1.0 ? 1.5 * (x - x * x * x / 3.0) : (x > 0.0 ? 1.0 : -1.0);
                case ShaperModel::sineFold: return std::sin (juce::MathConstants<double>::halfPi * x);
                case ShaperModel::triangleFold:
                {
                    auto u = std::fmod (x + 1.0, 4.0);
                    if (u < 0.0)
                        u += 4.0;
                    return 1.0 - std::abs (u - 2.0);
                }
                case ShaperModel::rectify: return std::abs (std::tanh (x));
                case ShaperModel::halfRectify: return x > 0.0 ? std::tanh (x) : 0.0;
            }
            return x;
        }

        // ...and its antiderivative (any constant: only differences are used).
        inline double antiderivative (ShaperModel model, double x) noexcept
        {
            switch (model)
            {
                case ShaperModel::soft: return logCosh (x);
                case ShaperModel::hard: return std::abs (x) <= 1.0 ? 0.5 * x * x : std::abs (x) - 0.5;
                case ShaperModel::tube: return logCosh (x + tubeBias) - x * std::tanh (tubeBias);
                case ShaperModel::diode:
                    return x >= 0.0 ? x + std::exp (-x) - 1.0 : -diodeKnee * x + diodeKnee * diodeKnee * (std::exp (x / diodeKnee) - 1.0);
                case ShaperModel::smooth: return std::abs (x) - std::log1p (std::abs (x));
                case ShaperModel::cubic:
                {
                    const auto a = std::abs (x);
                    return a <= 1.0 ? 1.5 * (0.5 * x * x - x * x * x * x / 12.0) : a - 0.375;
                }
                case ShaperModel::sineFold: return -std::cos (juce::MathConstants<double>::halfPi * x) / juce::MathConstants<double>::halfPi;
                case ShaperModel::triangleFold:
                {
                    // The triangle's integral repeats every period (it averages zero).
                    auto u = std::fmod (x + 1.0, 4.0);
                    if (u < 0.0)
                        u += 4.0;
                    return u <= 2.0 ? 0.5 * u * u - u : 3.0 * u - 0.5 * u * u - 4.0;
                }
                case ShaperModel::rectify: return x >= 0.0 ? logCosh (x) : -logCosh (x);
                case ShaperModel::halfRectify: return x > 0.0 ? logCosh (x) : 0.0;
            }
            return 0.5 * x * x;
        }

        // The level back up (or down), so a full-scale wave comes out near full
        // scale at any DRIVE: louder drive, the same loudness, more grit.
        inline float makeUp (ShaperModel model, float gain) noexcept
        {
            switch (model)
            {
                case ShaperModel::sineFold:
                case ShaperModel::triangleFold: return 1.0f;   // they never pass 1
                case ShaperModel::hard: return 1.0f / juce::jmin (gain, 1.0f);
                default: break;
            }
            const auto peak = juce::jmax (std::abs (curve (model, gain)), std::abs (curve (model, -gain)));
            return peak > 1.0e-6 ? (float) (1.0 / peak) : 1.0f;
        }

        // The ones that lean to one side, so make a DC offset (taken away after them).
        inline bool isAsymmetric (ShaperModel model) noexcept
        {
            return model == ShaperModel::tube || model == ShaperModel::diode || model == ShaperModel::rectify || model == ShaperModel::halfRectify;
        }

        // An EQ band: a bell (Andrew Simper's published state-variable form,
        // which stays smooth when it's moved). 0 dB: no band.
        struct Bell
        {
            float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f, boost = 0.0f;   // boost = k (A^2 - 1)
            bool active = false;

            static Bell make (float gainDb, float freqHz, float q, double sampleRate) noexcept
            {
                Bell b;
                b.active = std::abs (gainDb) >= 0.05f;
                if (! b.active)
                    return b;
                const auto a = std::pow (10.0, gainDb / 40.0);
                const auto g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, 0.45 * sampleRate, (double) freqHz) / sampleRate);
                const auto k = 1.0 / (juce::jmax (0.05, (double) q) * a);
                b.a1 = (float) (1.0 / (1.0 + g * (g + k)));
                b.a2 = (float) (g * b.a1);
                b.a3 = (float) (g * b.a2);
                b.k = (float) k;
                b.boost = (float) (k * (a * a - 1.0));
                return b;
            }
        };

        // The 2x oversampling's half-band filter: its odd taps either side of
        // the centre (a 47-tap Kaiser-windowed sinc, beta 7.9; every other tap
        // of a half-band filter is zero, so 12 multiplies each way).
        inline constexpr int halfBandTaps = 12;
        inline constexpr float halfBandCentre = 0.4999953710f;
        inline constexpr std::array<float, halfBandTaps> halfBand {
            3.1609289040e-01f, -9.9619882028e-02f, 5.3367431087e-02f, -3.2058368641e-02f, 1.9668359286e-02f, -1.1829020736e-02f,
            6.7890652212e-03f, -3.6264820730e-03f, 1.7470107469e-03f, -7.2097213840e-04f, 2.2782318545e-04f, -3.5539827520e-05f };
        // Samples of delay through the oversampling: 12 going up, 11 coming down.
        inline constexpr int latency = 2 * halfBandTaps - 1;

        // What one shaper works out once per block: its EQ bands.
        struct Coefficients
        {
            std::array<Bell, 2> bells;
        };

        // One channel of one shaper in one note.
        class Channel
        {
        public:
            void reset() noexcept
            {
                for (auto& s : states)
                    s = {};
                previous = previousF = 0.0;
                dcIn = dcOut = 0.0f;
                started = false;
                upHistory.fill (0.0f);
                downA.fill (0.0f);
                downB.fill (0.0f);
                dryHistory.fill (0.0f);
                position = 0;
            }

            // The curve at twice the sample rate: up (one sample between each
            // two), shaped, and back down. `latency` samples late.
            float distort (float input, ShaperModel model, float gain) noexcept
            {
                // Up: the sample from `halfBandTaps` ago, and the one halfway after it.
                const auto at = (size_t) position;
                upHistory[at] = upHistory[at + window] = input;
                const auto* x = upHistory.data() + at + 1;   // x[0] the oldest of the last 24, x[23] the newest
                float between = 0.0f;
                for (int i = 0; i < halfBandTaps; ++i)
                    between += halfBand[(size_t) i] * (x[halfBandTaps + i] + x[halfBandTaps - 1 - i]);
                const auto a = x[halfBandTaps - 1];
                const auto b = 2.0f * between;

                // Shaped at the higher rate...
                const auto shapedA = shape (a, model, gain), shapedB = shape (b, model, gain);

                // ...and down: the half-band again, keeping every other sample.
                downA[at] = downA[at + window] = shapedA;
                downB[at] = downB[at + window] = shapedB;
                const auto* sa = downA.data() + at + 1;
                const auto* sb = downB.data() + at + 1;
                auto out = halfBandCentre * sa[halfBandTaps];
                for (int i = 0; i < halfBandTaps; ++i)
                    out += halfBand[(size_t) i] * (sb[halfBandTaps + i] + sb[halfBandTaps - 1 - i]);

                position = position + 1 == window ? 0 : position + 1;
                return out;
            }

            // The sound coming in, `latency` samples late (MIX's dry sound, in step with what's distorted).
            float delayedDry (float input) noexcept
            {
                const auto out = dryHistory[(size_t) dryPosition];
                dryHistory[(size_t) dryPosition] = input;
                dryPosition = dryPosition + 1 == latency ? 0 : dryPosition + 1;
                return out;
            }

            float eq (float x, const Bell& bell, int band) noexcept
            {
                if (! bell.active)
                    return x;
                auto& s = states[(size_t) band];
                const auto v3 = x - s.ic2;
                const auto v1 = bell.a1 * s.ic1 + bell.a2 * v3;
                const auto v2 = s.ic2 + bell.a2 * s.ic1 + bell.a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1;
                s.ic2 = 2.0f * v2 - s.ic2;
                return x + bell.boost * v1;
            }

            // The curve, anti-aliased (in double: a small difference divided by a small difference).
            float shape (float input, ShaperModel model, float gain) noexcept
            {
                const auto x = (double) input * gain;
                const auto f = antiderivative (model, x);
                if (model != lastModel)
                {
                    // A new curve: the last sample's antiderivative was the old curve's, so start afresh (no click).
                    lastModel = model;
                    started = false;
                }
                if (! started)
                {
                    // The first sample: no previous one to average from.
                    started = true;
                    previous = x;
                    previousF = f;
                    return (float) curve (model, x);
                }
                const auto difference = x - previous;
                const auto y = std::abs (difference) < 1.0e-5 ? curve (model, 0.5 * (x + previous)) : (f - previousF) / difference;
                previous = x;
                previousF = f;
                return (float) y;
            }

            // A gentle high-pass (5 Hz) after the lopsided models, so their DC offset doesn't build up.
            float blockDc (float x, float pole) noexcept
            {
                const auto y = x - dcIn + pole * dcOut;
                dcIn = x;
                dcOut = y;
                return y;
            }

        private:
            // Ring buffers written twice (at i and i + window), so the last 24 are always in one run.
            static constexpr int window = 2 * halfBandTaps;
            std::array<float, 2 * window> upHistory {}, downA {}, downB {};
            int position = 0;
            std::array<float, latency> dryHistory {};
            int dryPosition = 0;

            struct State { float ic1 = 0.0f, ic2 = 0.0f; };
            std::array<State, 2> states {};
            double previous = 0.0, previousF = 0.0;
            float dcIn = 0.0f, dcOut = 0.0f;
            bool started = false;
            ShaperModel lastModel = ShaperModel::soft;
        };
    }
}
