#pragma once

#include "SynthSettings.h"
#include "WaveTables.h"

#include <cmath>
#include <vector>

// SYNC, FM and AM (stage 3c), modelled on Avenger's (our own design and code;
// the manual says what they do, not how). The numbers shared by the engine
// and the displays:
//
//   SYNC  a copy of the wave, restarted at the start of every cycle of the
//         note, running faster: from the note's own pitch (0%) to 5 octaves
//         up (100%). Its envelope adds to that from each note's start,
//         falling to nothing over its LENGTH. The restart is an edge, smoothed
//         as SHAPE's are; the WIN modes fade the copy in and out each cycle
//         instead, so there's no edge at all.
//   FM    the wave read further ahead or behind as a modulator swings: phase
//         modulation, as in most "FM" synths. The modulator is one of four
//         shapes at RATE times the note, or another oscillator (which then
//         isn't heard by itself, as on Avenger).
//   AM    the wave's level swung by the modulator; at 100%, multiplied by it
//         ("ring modulation").
namespace synth::modulation
{
    // RATE's knob (0 to 1) as how many times the note's frequency the
    // modulator runs. FREE: 1/16 to 16 times, 1 in the middle. The grids
    // start at 1 in the middle (left of it, still 1, as Avenger's do) and step
    // up to 16 through their intervals.
    inline const std::vector<float>& gridRatios (ModGrid grid)
    {
        static const auto build = [] (ModGrid g)
        {
            std::vector<float> ratios;
            const auto withInterval = [&ratios] (double semitones)
            {
                for (int octave = 0; octave <= 4; ++octave)
                {
                    ratios.push_back ((float) std::exp2 (octave));
                    const auto above = std::exp2 (octave + semitones / 12.0);
                    if (above <= 16.0)
                        ratios.push_back ((float) above);
                }
            };
            switch (g)
            {
                case ModGrid::free:         break;
                case ModGrid::harmonic:     ratios = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f }; break;   // octaves and fifths, exactly
                case ModGrid::semitone:     for (int k = 0; k <= 48; ++k) ratios.push_back ((float) std::exp2 (k / 12.0)); break;
                case ModGrid::octave:       ratios = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f }; break;
                case ModGrid::octaveMinor:  withInterval (3.0); break;
                case ModGrid::octaveMajor:  withInterval (4.0); break;
                case ModGrid::octaveFourth: withInterval (5.0); break;
                case ModGrid::octaveFifth:  withInterval (7.0); break;
            }
            return ratios;
        };
        static const std::vector<float> all[] = { build (ModGrid::free), build (ModGrid::harmonic), build (ModGrid::semitone), build (ModGrid::octave),
                                                  build (ModGrid::octaveMinor), build (ModGrid::octaveMajor), build (ModGrid::octaveFourth),
                                                  build (ModGrid::octaveFifth) };
        return all[juce::jlimit (0, 7, (int) grid)];
    }

    inline float ratio (float rate, ModGrid grid)
    {
        rate = juce::jlimit (0.0f, 1.0f, rate);
        if (grid == ModGrid::free)
            return std::exp2 ((rate - 0.5f) * 8.0f);
        const auto& ratios = gridRatios (grid);
        if (rate <= 0.5f)
            return ratios.front();
        return ratios[(size_t) juce::roundToInt ((rate - 0.5f) * 2.0f * (float) (ratios.size() - 1))];
    }

    // SYNC (0 to 1, after its envelope) as how many times faster the copy runs.
    inline float syncRatio (float amount) noexcept { return std::exp2 (5.0f * juce::jlimit (0.0f, 1.0f, amount)); }

    // FM's AMOUNT (0 to 1) as how far the wave is read ahead or behind, in
    // cycles, at the modulator's peak: squared, so the low end is fine.
    // 1 = 2 cycles (a modulation index of 4 pi, very bright).
    inline float fmDepth (float amount) noexcept { return 2.0f * amount * amount; }

    // How much faster than the note FM can make the wave run, at most (for
    // choosing a table level): 1 + depth x the modulator's rate x its steepest slope.
    inline float fmSpeed (float depth, float ratio, ModShape shape) noexcept
    {
        const auto steepest = shape == ModShape::sine ? juce::MathConstants<float>::twoPi : shape == ModShape::triangle ? 4.0f : 8.0f;
        return 1.0f + depth * ratio * steepest;
    }

    // A modulator shape at phase p (0 to 1), from band-limited tables at `level`.
    inline float shapeAt (ModShape shape, float p, int level) noexcept
    {
        switch (shape)
        {
            case ModShape::sine:     return tables::read (tables::get (tables::Wave::sine, level), p);
            case ModShape::triangle: return tables::read (tables::get (tables::Wave::triangle, level), p);
            case ModShape::saw:      return tables::read (tables::get (tables::Wave::saw, level), p);
            case ModShape::square:
            {
                auto other = p + 0.5f;
                other -= (float) (other >= 1.0f);
                const auto* saw = tables::get (tables::Wave::saw, level);
                return tables::read (saw, p) - tables::read (saw, other);
            }
        }
        return 0.0f;
    }

    // SYNC's envelope at `ms` after the note started: 1 falling to 0 over
    // `lengthMs`, bent by `curve` (-1 to 1) as the AMP's curves are.
    inline float syncEnvelope (float ms, float lengthMs, float curve) noexcept
    {
        const auto progress = juce::jlimit (0.0f, 1.0f, ms / juce::jmax (1.0f, lengthMs));
        return 1.0f - synth::bend (progress, curve);
    }

    // The WIN modes' fade: a raised cosine across the cycle, 0 at its ends.
    inline float window (float y) noexcept
    {
        static const float* sine = tables::get (tables::Wave::sine, tables::numLevels - 1);
        return 0.5f - 0.5f * tables::read (sine, y + 0.25f - (float) (y + 0.25f >= 1.0f));
    }
}
