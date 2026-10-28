#pragma once

#include "Circuit.h"

namespace effects
{
    // Abyss: OD / DISTORTION's high-gain model, modelled on the Boss MT-2
    // Metal Zone from the circuit diagram in its service manual (April
    // 1991), every value printed on it. In order:
    //
    //   IC3b, a gain stage with a "bootstrapped" transistor on its (-) leg:
    //        a hump of gain around 800 Hz (the mids that cut);
    //   IC3a, DIST: up to about 250 times more gain;
    //   two silicon diodes to ground, then IC4b with two more transistor
    //        branches shaping what's clipped, and IC4a, an inverter;
    //   the EQ: HIGH and LOW around IC1a, then MIDDLE with MID FREQ (a
    //        semi-parametric mid) around IC2a.
    //
    // Each transistor branch is an emitter follower: modelled as a perfect
    // follower (its emitter tracks its base). Each op-amp can swing about
    // 3 V either way of its bias, as on 9 V. The stages that can clip run at
    // the oversampled rate; the EQ (linear) at the base rate.
    class Abyss
    {
    public:
        void prepare (double oversampledRate, double baseRate) noexcept;
        void reset() noexcept;

        // The pedal's knobs, 0 to 1. Each re-solves its stage only when it moves.
        void setDistortion (double knob) noexcept;
        void setEq (double high, double low, double middle, double midFreq) noexcept;

        // The gain stages and diodes at the oversampled rate (volts in and out),
        double clip (double volts) noexcept;
        // and the EQ at the base rate.
        double equalise (double volts) noexcept;

    private:
        Circuit gainHump, dist, diodeLoad, afterDiodes, highLow, middle;
        int distPot = 0, highA = 0, highB = 0, lowA = 0, lowB = 0, midA = 0, midB = 0, freqA = 0, freqB = 0;
        double distKnob = -1.0, highKnob = -1.0, lowKnob = -1.0, middleKnob = -1.0, midFreqKnob = -1.0;
        double period = 1.0 / 384000.0;
        double gCoupling = 0.0, jCoupling = 0.0, diodeNode = 0.0;   // C027 into the diodes
    };
}
