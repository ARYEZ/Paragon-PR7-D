#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// Dynamic EQ (PR7-E 0.5.0): how a dynamic band's gain follows its part of
// the sound. Inspired by FabFilter Pro-Q 4's dynamic EQ, as its manual
// describes it; our own design, because Pro-Q's own timing and knee aren't
// published, and the numbers below are our choices.
//
//   - The band listens to the EQ's input through the filter SOLO plays (a
//     band pass at a bell's frequency and Q; the lows below a low shelf,
//     the highs above a high shelf), on the channels it works on.
//   - Its level: the power of that over a couple of cycles of the band's
//     frequency, in dB (a full-scale sine reads 0 dB, as on the analyser).
//   - Over the threshold, each dB the level goes over moves the band's gain
//     a dB towards its RANGE, and no further: a negative range cuts when the
//     band's part gets loud (taming), a positive one boosts (bringing out).
//     The knee is soft: it starts 3 dB below the threshold.
//   - The gain gets there over the attack time (a time constant: about two
//     thirds of the way in that time) and lets go over the release time.
//   - AUTO threshold: 6 dB above the band's own typical level over the last
//     few seconds (from the start, the average of all it's heard), so it
//     acts on what stands out, whatever the track's level. Pauses and
//     silence (24 dB or more under it) barely move it.
//   - Automatic attack and release follow the band's frequency: a few cycles
//     of it (lows need longer to measure), from 2 to 50 ms, and release 15
//     times that, from 80 to 600 ms. ATTACK and RELEASE at 50% are those;
//     0% is four times faster, 100% four times slower (as in Pro-Q).
namespace eq::dynamics
{
    inline constexpr double maxRangeDb = 30.0;
    inline constexpr double minThresholdDb = -60.0, maxThresholdDb = 0.0;
    inline constexpr double kneeDb = 6.0;
    inline constexpr double autoAboveDb = 6.0;      // AUTO's threshold over the band's typical level
    inline constexpr double autoSeconds = 3.0;      // how quickly that typical level follows
    inline constexpr double autoIgnoreBelowDb = 24.0;   // quieter than this under it (pauses): barely followed
    inline constexpr double floorDb = -100.0;

    // How far the band's gain moves, in dB (with the sign of `rangeDb`).
    inline double gainChangeDb (double levelDb, double thresholdDb, double rangeDb) noexcept
    {
        const auto over = levelDb - thresholdDb, half = 0.5 * kneeDb;
        const auto amount = over >= half ? over : over > -half ? (over + half) * (over + half) / (2.0 * kneeDb) : 0.0;
        return std::copysign (std::min (amount, std::abs (rangeDb)), rangeDb);
    }

    // 0 to 1 (0.5 automatic) to a factor on the automatic times: 1/4 to 4.
    inline double speed (double knob) noexcept { return std::pow (4.0, 2.0 * (juce::jlimit (0.0, 1.0, knob) - 0.5)); }

    // The level is measured over about two cycles of the band's frequency (1 to 25 ms).
    inline double measureSeconds (double hz) noexcept { return juce::jlimit (0.001, 0.025, 2.0 / std::max (hz, 1.0)); }

    inline double autoAttackSeconds (double hz) noexcept { return juce::jlimit (0.002, 0.05, 4.0 / std::max (hz, 1.0)); }
    inline double attackSeconds (double hz, double knob) noexcept { return autoAttackSeconds (hz) * speed (knob); }
    inline double releaseSeconds (double hz, double knob) noexcept
    {
        return juce::jlimit (0.08, 0.6, 15.0 * autoAttackSeconds (hz)) * speed (knob);
    }
}
