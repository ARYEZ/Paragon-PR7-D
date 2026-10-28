#pragma once

#include "Circuit.h"

namespace effects
{
    // Rabid: OD / DISTORTION's RAT, modelled on the ProCo RAT 2 from its
    // published build schematic (the Effects Layouts "RAT Project" bill of
    // materials, cross-checked against other layouts):
    //
    //   LM308 gain stage (DISTORTION) -> 1k + 4.7 uF -> two silicon diodes to ground -> FILTER -> output
    //
    // The RAT's sound is as much its op-amp as its diodes. The LM308 is slow:
    // at high gain it runs out of bandwidth (its gain-bandwidth product is
    // about 1 MHz), so the top end rolls off as DISTORTION goes up, and it
    // can only swing 0.3 V per microsecond, so a hard-driven RAT's edges are
    // sloped rather than sharp. Both are modelled: the op-amp is solved
    // sample by sample as an integrator (its gain-bandwidth) with its slew
    // and swing limits, driving its own feedback network.
    class Rabid
    {
    public:
        void prepare (double oversampledRate) noexcept;
        void reset() noexcept;

        // DISTORTION and FILTER, 0 to 1 as the pedal's knobs turn. FILTER up
        // is darker, as on the pedal.
        void setDistortion (double knob) noexcept;
        void setFilter (double knob) noexcept;

        // Guitar volts in, volts out (after the FILTER), at the oversampled rate.
        double process (double volts) noexcept;

    private:
        Circuit feedback;                 // the op-amp's output to its (-) input
        int distortionPot = 0;
        double distortion = -1.0;
        double period = 1.0 / 384000.0;
        double opAmpOut = 0.0;

        // The clipper: the 4.7 uF in series with 1k, the diodes, and the FILTER's 3.3 nF.
        double gCoupling = 0.0, jCoupling = 0.0, gFilterCap = 0.0, jFilterCap = 0.0;
        double filterOhms = 1.5e3, diodeNode = 0.0;
    };
}
