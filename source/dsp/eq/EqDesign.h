#pragma once

#include <juce_core/juce_core.h>

#include <array>

// The EQ stomp's filters: the band shapes, their slopes, and how each band
// becomes a cascade of digital filter sections. Pure maths, no audio
// routing, so the display and the tests use exactly what the sound uses.
//
// Inspired by FabFilter Pro-Q 4's zero-latency mode, which matches analog
// EQ curves: our own design, from published methods, not FabFilter's.
//
//   - Each band is first an analog prototype: a cascade of first- and
//     second-order sections (bells, Butterworth cuts, Holters & Zolzer
//     style higher-order shelves).
//   - Each section becomes a digital biquad whose poles are the analog
//     poles mapped exactly (matched-z) and whose numerator is chosen so the
//     digital curve equals the analog one at DC, at the band's frequency
//     and at the top of the range (after M. Vicanek, "Matched Second Order
//     Digital Filters", 2016). High-pass sections keep an exact double zero
//     at DC and match at the band's frequency, as in the paper.
//   - Ordinary digital EQs (the bilinear transform) squash curves near the
//     top of the range: a +12 dB bell at 15 kHz at 44.1 kHz is up to 7.5 dB
//     off its analog shape. These stay within about 1 dB, and exact at the
//     band's own frequency. A two-pole digital filter can't do better in
//     the top octave at 44.1/48 kHz. At 96 kHz they're within 0.3 dB for
//     bands up to 20 kHz (0.5 dB for a bell at 25 kHz).
namespace eq
{
    // Saved by index: only ever add to the end.
    enum class Shape { bell, lowShelf, lowCut, highShelf, highCut, notch, bandPass, tiltShelf, flatTilt, allPass };
    inline const juce::StringArray shapeNames { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut",
                                                "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass" };

    // Slopes in dB per octave, saved by index (only ever add to the end).
    inline constexpr int slopes[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
    inline constexpr int numSlopes = (int) std::size (slopes);
    inline const juce::StringArray slopeNames { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct",
                                                "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
    inline constexpr int defaultSlope = 1;   // 12 dB/oct

    // Which part of a stereo signal a band works on. Saved by index.
    enum class Placement { stereo, left, right, mid, side };
    inline const juce::StringArray placementNames { "Stereo", "Left", "Right", "Mid", "Side" };

    inline constexpr double minFrequency = 10.0, maxFrequency = 30000.0;
    inline constexpr double maxGainDb = 30.0;
    inline constexpr double minQ = 0.025, maxQ = 40.0;

    struct Band
    {
        bool used = false;   // the band exists (a slot in use)
        bool on = true;      // its own bypass
        Shape shape = Shape::bell;
        double frequency = 1000.0;
        double gainDb = 0.0;
        double q = 1.0;
        int slope = defaultSlope;   // index into slopes
        Placement placement = Placement::stereo;

        // Dynamic EQ (see Dynamics.h), for the shapes with a gain. A range
        // of 0 is an ordinary, static band.
        double dynamicRangeDb = 0.0;   // how far the gain moves when the band's part of the sound gets loud (- cuts, + boosts)
        bool autoThreshold = true;     // the threshold follows the band's own level
        double thresholdDb = -24.0;    // when it doesn't
        double attack = 0.5, release = 0.5;   // 0 to 1: 0.5 automatic, lower faster, higher slower

        bool isActive() const noexcept { return used && on; }
        bool isDynamic() const noexcept;
    };

    bool usesGain (Shape) noexcept;     // bell, shelves, tilts (and so can be dynamic)
    bool usesSlope (Shape) noexcept;    // cuts, shelves, tilt shelf, band pass, all pass
    bool usesQ (Shape, int slope) noexcept;
    int minSlope (Shape) noexcept;      // index: shelves and cuts go down to 6 dB/oct, the rest 12
    int slopeFor (Shape, int slope) noexcept;   // the slope actually used, within the shape's range

    // One digital section: y = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2) x.
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    inline constexpr int maxSections = 16;   // a 96 dB/oct cut is 8; a flat tilt up to 12

    // A band as digital filters: the sections in series, then a gain.
    struct Design
    {
        std::array<Biquad, maxSections> sections {};
        int numSections = 0;
        double gain = 1.0;

        bool isIdentity() const noexcept { return numSections == 0 && juce::approximatelyEqual (gain, 1.0); }
    };

    // Doesn't allocate: safe on the audio thread. `gainScale` scales the
    // gain of the shapes that have one (the EQ's GAIN SCALE, 1 = as set).
    Design design (const Band&, double sampleRate, double gainScale = 1.0) noexcept;

    // The digital curve, in dB, at `hz`.
    double responseDb (const Design&, double hz, double sampleRate) noexcept;

    // SOLO: what you hear of a band's part of the spectrum. Bells, notches,
    // band and all passes: a band pass at its frequency and Q; shelves and
    // tilts: the side of the spectrum they lift or cut; cuts: what they cut away.
    Design soloDesign (const Band&, double sampleRate) noexcept;
}
