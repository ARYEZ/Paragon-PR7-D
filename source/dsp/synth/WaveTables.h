#pragma once

#include <array>
#include <vector>

// Band-limited single-cycle waves for PR7-K's oscillators: our own design.
//
// A naive saw (a ramp) has harmonics all the way up, and those above half the
// sample rate fold back down as inharmonic "aliasing" (the whistle a cheap
// synth makes high up the keyboard). Here every wave is built from its
// harmonics, and each wave is stored at 28 levels, a third of an octave apart:
// level 0 has 512 harmonics, each level after it has a third of an octave
// fewer, down to the plain sine. An oscillator reads the level with as many
// harmonics as fit under its limit (see harmonicLimitHz), so whatever note it
// plays, nothing folds back where it can be heard. That also means no
// oversampling is needed, which is a large part of why it's cheap.
//
// Tables are 4096 samples (8 times the 512 harmonics), read with straight
// interpolation: at that ratio the interpolation's own error stays far below
// hearing. Built once, shared by every voice and every instance of the plugin.
namespace synth::tables
{
    // Saved by index (osc*Wave and osc*SubWave choose from these): only append.
    enum class Wave { saw, triangle, sine };

    inline constexpr int size = 4096;           // samples per cycle (a power of two)
    inline constexpr int numLevels = 28;        // a third of an octave apart
    inline constexpr int maxHarmonics = 512;    // in level 0

    // How many harmonics a level holds: 512 at level 0, a third of an octave fewer each level, 1 at the last.
    int harmonicsAt (int level) noexcept;

    // The level to read for a note at `frequencyHz`: the one with the most
    // harmonics whose top harmonic stays under harmonicLimitHz (sampleRate).
    int levelFor (double frequencyHz, double sampleRate) noexcept;

    // The highest a harmonic may go. A harmonic above half the sample rate
    // folds back down to (sampleRate - frequency), so allowing harmonics up to
    // sampleRate - 20 kHz keeps every folded one above 20 kHz, beyond
    // hearing, while keeping the top octave bright. Never less than 45% of the
    // sample rate (for unusually low sample rates).
    double harmonicLimitHz (double sampleRate) noexcept;

    // One level of one wave: size + 1 samples (the last repeats the first, so
    // interpolating past the end needs no wrap). Peaks are about +-1 (a
    // band-limited saw overshoots a little at its jump, as every analog one does).
    const float* get (Wave, int level) noexcept;

    // Reads `table` at `phase` (0 to 1), interpolating between samples.
    inline float read (const float* table, float phase) noexcept
    {
        const auto position = phase * (float) size;
        const auto index = (int) position;
        const auto fraction = position - (float) index;
        const auto a = table[index & (size - 1)];
        const auto b = table[(index & (size - 1)) + 1];
        return a + (b - a) * fraction;
    }
}
