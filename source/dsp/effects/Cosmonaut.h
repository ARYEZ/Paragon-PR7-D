#pragma once

#include "Circuit.h"

namespace effects
{
    // Cosmonaut: OD / DISTORTION's fuzz, modelled on the green "Civil War" /
    // Tall Font Russian Big Muff (Sovtek, mid-'90s), from Kit Rae's trace of
    // a real unit. Four transistor stages:
    //
    //   input stage -> SUSTAIN -> clipping stage -> clipping stage -> tone stack (TONE) -> output stage
    //
    // Each of the first three is a common-emitter stage held in bias by a
    // 470k from collector to base, with 500 pF across it. In the two
    // clipping stages, a pair of silicon diodes also bridges collector to
    // base, in series with a 0.047 uF capacitor. That small capacitor is the
    // Green Russian's sound: the diodes can only clip what gets through it,
    // so the bass is clipped far less than the mids and highs (the "fat
    // bottom end"); the US versions use 1 uF there and clip everything.
    //
    // Each stage is solved sample by sample (Newton's method on its base
    // node), with the transistor as an inverting amplifier of about Rc / Re
    // = 12k / 390 = 31, swinging up to about 4 V either way of its bias.
    class Cosmonaut
    {
    public:
        void prepare (double oversampledRate, double baseRate) noexcept;
        void reset() noexcept;

        // SUSTAIN, 0 to 1. Call once per block.
        void setSustain (double sustain) noexcept;
        // The clipping, at the oversampled rate: guitar volts in, volts out.
        double clip (double volts) noexcept;

        // TONE, 0 (bass) to 1 (treble). Re-solves the tone stack when it moves.
        void setTone (double tone) noexcept;
        // The tone stack and output stage, at the base rate.
        double finish (double volts) noexcept;

    private:
        struct Stage
        {
            void setup (double rate, double sourceOhms, double couplingFarads, double baseOhms, double seriesDiodeFarads, bool hasDiodes) noexcept;
            void reset() noexcept;
            double step (double sourceVolts, double extraSourceOhms) noexcept;

            double rs = 10.0e3, gCoupling = 0.0, rBase = 100.0e3, gFeedbackCap = 0.0, gDiodeCap = 0.0;
            bool diodes = false;
            double jCoupling = 0.0, jFeedbackCap = 0.0, jDiodeCap = 0.0, vBase = 0.0, iDiode = 0.0;
        };

        Stage input, clip1, clip2;
        double sustainVolts = 1.0, sustainOhms = 0.0;   // the SUSTAIN pot as a source: its share and its resistance

        Circuit toneStack;
        int bassHalf = 0, trebleHalf = 0;
        double tone = -1.0;
    };
}
