#include "EqDesign.h"

#include <complex>

namespace eq
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;

        // An analog section: (n2 s^2 + n1 s + n0) / (s^2 + d1 s + d0), s in rad/s.
        // highPass: the numerator is n2 s^2 alone (an exact double zero at DC).
        struct Analog
        {
            double n2 = 0.0, n1 = 0.0, n0 = 1.0, d1 = 0.0, d0 = 1.0;
            bool highPass = false;
        };

        double analogMagnitude2 (const Analog& h, double w) noexcept
        {
            const std::complex<double> num (h.n0 - h.n2 * w * w, h.n1 * w), den (h.d0 - w * w, h.d1 * w);
            return std::norm (num) / std::max (std::norm (den), 1.0e-300);
        }

        // The digital poles: the analog ones mapped exactly (z = e^(sT)). A band
        // can sit above the top of the range (up to 30 kHz): its poles are kept
        // below 0.9 x Nyquist, keeping their Q, and the numerator still matches
        // its real curve at three points.
        std::pair<double, double> matchedPoles (const Analog& h, double sampleRate) noexcept
        {
            const auto T = 1.0 / sampleRate;
            const auto discriminant = h.d1 * h.d1 - 4.0 * h.d0;
            const auto limit = 0.9 * pi * sampleRate;
            if (discriminant < 0.0)
            {
                auto sigma = -0.5 * h.d1, omega = 0.5 * std::sqrt (-discriminant);
                if (const auto size = std::hypot (sigma, omega); size > limit)
                {
                    sigma *= limit / size;
                    omega *= limit / size;
                }
                return { -2.0 * std::exp (sigma * T) * std::cos (omega * T), std::exp (2.0 * sigma * T) };
            }
            auto p1 = 0.5 * (-h.d1 + std::sqrt (discriminant)), p2 = 0.5 * (-h.d1 - std::sqrt (discriminant));
            if (const auto size = std::max (std::abs (p1), std::abs (p2)); size > limit)
            {
                p1 *= limit / size;
                p2 *= limit / size;
            }
            return { -(std::exp (p1 * T) + std::exp (p2 * T)), std::exp ((p1 + p2) * T) };
        }

        // Where the curve is matched between DC and Nyquist: the band's own
        // frequency, unless that's too close to (or above) the top.
        double matchFrequency (double frequency, double sampleRate) noexcept
        {
            const auto nyquist = 0.5 * sampleRate;
            return frequency < 0.85 * nyquist ? frequency : 0.6 * nyquist;
        }

        Biquad match (const Analog& h, double sampleRate, double frequency) noexcept
        {
            const auto [a1, a2] = matchedPoles (h, sampleRate);
            const auto T = 1.0 / sampleRate;
            const auto A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2), A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2), A2 = -4.0 * a2;
            // |denominator|^2 at w, in terms of phi1 = sin^2 (wT/2), as in the paper.
            const auto phi = [T] (double w) { const auto s = std::sin (0.5 * w * T); return s * s; };
            const auto denominator2 = [&] (double p1) { const auto p0 = 1.0 - p1; return A0 * p0 + A1 * p1 + A2 * 4.0 * p0 * p1; };

            const auto wm = 2.0 * pi * matchFrequency (frequency, sampleRate);
            const auto pm = phi (wm);
            Biquad b;
            b.a1 = a1;
            b.a2 = a2;

            if (h.highPass)
            {
                // b0 (1 - z^-1)^2, with the gain matched at the band's frequency.
                const auto b0 = std::sqrt (analogMagnitude2 (h, wm) * denominator2 (pm)) / (4.0 * std::max (pm, 1.0e-12));
                b.b0 = b0;
                b.b1 = -2.0 * b0;
                b.b2 = b0;
                return b;
            }

            const auto B0 = analogMagnitude2 (h, 0.0) * A0;
            const auto B1 = analogMagnitude2 (h, pi * sampleRate) * A1;
            const auto p0 = 1.0 - pm, p2 = 4.0 * p0 * pm;
            const auto B2 = (analogMagnitude2 (h, wm) * denominator2 (pm) - B0 * p0 - B1 * pm) / std::max (p2, 1.0e-12);

            const auto W = 0.5 * (std::sqrt (B0) + std::sqrt (B1));
            b.b0 = 0.5 * (W + std::sqrt (std::max (W * W + B2, 0.0)));
            b.b1 = 0.5 * (std::sqrt (B0) - std::sqrt (B1));
            b.b2 = b.b0 > 1.0e-30 ? -B2 / (4.0 * b.b0) : 0.0;
            return b;
        }

        // A first-order analog section (n1 s + n0) / (s + p). Matched as a
        // second-order one with a spare pole/zero pair high up (it cancels in
        // the analog curve), which frees the numerator to match three points.
        Biquad matchFirstOrder (double n1, double n0, double p, double sampleRate, double frequency) noexcept
        {
            const auto far = 0.9 * pi * sampleRate;
            if (n0 <= 0.0)
            {
                // High pass s / (s + p): a zero exactly at DC, the gain matched at Nyquist.
                const auto a1 = -std::exp (-std::min (p, far) / sampleRate);
                const auto w = pi * sampleRate;
                const auto magnitude = n1 * w / std::hypot (w, p);
                Biquad b;
                b.a1 = a1;
                b.b0 = 0.5 * magnitude * (1.0 - a1);
                b.b1 = -b.b0;
                return b;
            }
            return match ({ n1, n1 * far + n0, n0 * far, p + far, p * far }, sampleRate, frequency);
        }

        // Butterworth damping for order n: the second-order sections are
        // s^2 + 2 c s + 1 (c = sin ((2m - 1) pi / 2n)); odd orders add s + 1.
        double butterworthDamping (int order, int section) noexcept
        {
            return std::sin ((2.0 * section + 1.0) * pi / (2.0 * order));
        }

        struct Builder
        {
            Design result;
            double sampleRate;
            double frequency;

            void add (const Biquad& b) noexcept
            {
                if (result.numSections < maxSections)
                    result.sections[(size_t) result.numSections++] = b;
            }
            void add (const Analog& h) noexcept { add (match (h, sampleRate, frequency)); }
            void addFirstOrder (double n1, double n0, double p) noexcept { add (matchFirstOrder (n1, n0, p, sampleRate, frequency)); }
        };

        // A shelf of `order` (1 to 16), `gain` (linear) at DC and 1 at the
        // top, half way (in dB) at w. The section with the highest Q takes the
        // band's Q: 1 is Butterworth (no overshoot), higher adds a bump.
        void addLowShelf (Builder& builder, double w, double gain, int order, double q) noexcept
        {
            const auto k = std::pow (gain, 1.0 / order);
            const auto wp = w / std::sqrt (k), wz = w * std::sqrt (k);
            for (int m = 0; m < order / 2; ++m)
            {
                const auto c = butterworthDamping (order, m) / (m == 0 ? q : 1.0);
                builder.add (Analog { 1.0, 2.0 * c * wz, wz * wz, 2.0 * c * wp, wp * wp });
            }
            if (order % 2 == 1)
                builder.addFirstOrder (1.0, wz, wp);
        }

        void addCut (Builder& builder, double w, int order, double q, bool high) noexcept
        {
            for (int m = 0; m < order / 2; ++m)
            {
                const auto c = butterworthDamping (order, m) / (m == 0 ? q : 1.0);
                if (high)
                    builder.add (Analog { 1.0, 0.0, 0.0, 2.0 * c * w, w * w, true });
                else
                    builder.add (Analog { 0.0, 0.0, w * w, 2.0 * c * w, w * w });
            }
            if (order % 2 == 1)
            {
                if (high)
                    builder.addFirstOrder (1.0, 0.0, w);
                else
                    builder.addFirstOrder (0.0, w, w);
            }
        }

        void addBandPass (Builder& builder, double w, double q, int sections) noexcept
        {
            for (int i = 0; i < sections; ++i)
                builder.add (Analog { 0.0, w / q, 0.0, w / q, w * w });
        }

        int orderOf (int slope) noexcept { return slopes[(size_t) juce::jlimit (0, numSlopes - 1, slope)] / 6; }
    }

    bool usesGain (Shape shape) noexcept
    {
        return shape == Shape::bell || shape == Shape::lowShelf || shape == Shape::highShelf
            || shape == Shape::tiltShelf || shape == Shape::flatTilt;
    }

    bool usesSlope (Shape shape) noexcept
    {
        return shape == Shape::lowCut || shape == Shape::highCut || shape == Shape::lowShelf || shape == Shape::highShelf
            || shape == Shape::tiltShelf || shape == Shape::bandPass || shape == Shape::allPass;
    }

    int minSlope (Shape shape) noexcept
    {
        return shape == Shape::lowCut || shape == Shape::highCut || shape == Shape::lowShelf || shape == Shape::highShelf
                       || shape == Shape::tiltShelf
                   ? 0 : 1;
    }

    int slopeFor (Shape shape, int slope) noexcept
    {
        return usesSlope (shape) ? juce::jlimit (minSlope (shape), numSlopes - 1, slope) : defaultSlope;
    }

    bool usesQ (Shape shape, int slope) noexcept
    {
        if (shape == Shape::flatTilt)
            return false;
        // A first-order (6 dB/oct) cut or shelf has no Q to set.
        return ! (usesSlope (shape) && orderOf (slopeFor (shape, slope)) == 1);
    }

    Design design (const Band& band, double sampleRate, double gainScale) noexcept
    {
        Builder builder { {}, sampleRate, band.frequency };
        if (! band.isActive() || sampleRate <= 0.0)
            return builder.result;

        const auto w = 2.0 * pi * juce::jlimit (minFrequency, maxFrequency, band.frequency);
        const auto q = juce::jlimit (minQ, maxQ, band.q);
        const auto gainDb = juce::jlimit (-maxGainDb, maxGainDb, band.gainDb * gainScale);
        const auto order = orderOf (slopeFor (band.shape, band.slope));
        // A band at 0 dB still runs its filters (they just pass the sound):
        // skipping them would freeze their memories, and a gain gliding
        // through 0 dB would then jump.

        switch (band.shape)
        {
            case Shape::bell:
            {
                const auto A = std::pow (10.0, gainDb / 40.0);
                builder.add (Analog { 1.0, A * w / q, w * w, w / (A * q), w * w });
                break;
            }
            case Shape::lowShelf:
                addLowShelf (builder, w, std::pow (10.0, gainDb / 20.0), order, q);
                break;
            case Shape::highShelf:
            case Shape::tiltShelf:
            {
                // A high shelf is a low shelf of the opposite gain, lifted by
                // the gain. A tilt shelf is a high shelf pivoting round 0 dB:
                // half the gain cut below, half boosted above.
                const auto gain = std::pow (10.0, gainDb / 20.0);
                addLowShelf (builder, w, 1.0 / gain, order, q);
                builder.result.gain = band.shape == Shape::tiltShelf ? std::sqrt (gain) : gain;
                break;
            }
            case Shape::lowCut:  addCut (builder, w, order, q, true); break;
            case Shape::highCut: addCut (builder, w, order, q, false); break;
            case Shape::notch:
                builder.add (Analog { 1.0, 0.0, w * w, w / q, w * w });
                break;
            case Shape::bandPass:
                // Steeper slopes stack more sections, which also narrows it.
                addBandPass (builder, w, q, std::max (1, order / 2));
                break;
            case Shape::allPass:
            {
                for (int i = 0; i < std::max (1, order / 2); ++i)
                {
                    const auto [a1, a2] = matchedPoles (Analog { 1.0, 0.0, 1.0, w / q, w * w }, sampleRate);
                    builder.add (Biquad { a2, a1, 1.0, a1, a2 });   // the numerator mirrors the poles: flat, only the phase turns
                }
                break;
            }
            case Shape::flatTilt:
            {
                // A straight tilt through the band's frequency: GAIN is the
                // tilt across the ten audible octaves (20 Hz to 20 kHz), so
                // gain / 10 dB per octave. Our own design (the manual doesn't
                // define it): a first-order shelf each octave from 10 Hz up.
                const auto perOctave = std::pow (10.0, gainDb / 10.0 / 20.0);
                for (double corner = 10.0; corner < 0.45 * sampleRate && builder.result.numSections < maxSections; corner *= 2.0)
                {
                    const auto wc = 2.0 * pi * corner;
                    // G (s + wc/sqrt G) / (s + wc sqrt G): 1 at DC, G (perOctave) at the top, half way at wc.
                    builder.addFirstOrder (perOctave, wc * std::sqrt (perOctave), wc * std::sqrt (perOctave));
                }
                builder.result.gain = std::pow (10.0, -responseDb (builder.result, band.frequency, sampleRate) / 20.0);
                break;
            }
        }
        return builder.result;
    }

    double responseDb (const Design& design, double hz, double sampleRate) noexcept
    {
        const auto w = 2.0 * pi * hz / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        double magnitude = std::abs (design.gain);
        for (int i = 0; i < design.numSections; ++i)
        {
            const auto& s = design.sections[(size_t) i];
            magnitude *= std::abs ((s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2));
        }
        return 20.0 * std::log10 (std::max (magnitude, 1.0e-15));
    }

    Design soloDesign (const Band& band, double sampleRate) noexcept
    {
        Builder builder { {}, sampleRate, band.frequency };
        const auto w = 2.0 * pi * juce::jlimit (minFrequency, maxFrequency, band.frequency);
        const auto order = orderOf (slopeFor (band.shape, band.slope));
        switch (band.shape)
        {
            case Shape::lowShelf:  addCut (builder, w, 2, 1.0, false); break;   // the lows it lifts or cuts
            case Shape::highShelf: addCut (builder, w, 2, 1.0, true); break;
            case Shape::lowCut:    addCut (builder, w, order, 1.0, false); break;   // what it cuts away
            case Shape::highCut:   addCut (builder, w, order, 1.0, true); break;
            case Shape::flatTilt:  break;   // it touches everything: hear it all
            case Shape::tiltShelf: addBandPass (builder, w, 0.5, 1); break;
            case Shape::bell:
            case Shape::notch:
            case Shape::bandPass:
            case Shape::allPass:   addBandPass (builder, w, juce::jlimit (minQ, maxQ, band.q), 1); break;
        }
        return builder.result;
    }
}
