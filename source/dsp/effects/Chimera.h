#pragma once

#include "Circuit.h"

namespace effects
{
    // Chimera: OD / DISTORTION's "transparent" overdrive, modelled on the
    // Klon Centaur from a clean redraw of its schematic (cross-checked with
    // ElectroSmash's analysis). Three paths meet at an inverting summing amp:
    //
    //   - the gain stage (an op-amp, GAIN's first gang setting its gain) into
    //     two germanium diodes to ground: the drive;
    //   - a clean feed from the input, through GAIN's second gang: as GAIN
    //     goes up the clean part goes down, the "transparent" blend;
    //   - a fixed low feed (1.5k and 1 uF, under about 106 Hz) for the bass.
    //
    // Then the active TREBLE stage, and a clean bleed straight to the output.
    //
    // The gain stage and the diodes run at the oversampled rate (they clip);
    // everything after is linear, so it runs at the base rate, the summing
    // amp's output worked out as the clean feed's share plus the diodes'.
    class Chimera
    {
    public:
        void prepare (double oversampledRate, double baseRate) noexcept;
        void reset() noexcept;

        // GAIN (both gangs, audio taper) and TREBLE, 0 to 1. Re-solve only when they move.
        void setGain (double knob) noexcept;
        void setTreble (double knob) noexcept;

        // The gain stage and the germanium diodes, at the oversampled rate:
        // guitar volts in, the diodes' node out.
        double clip (double volts) noexcept;
        // The rest, at the base rate: the guitar and the (downsampled) diode node in, output volts out.
        double finish (double guitar, double diodes) noexcept;

    private:
        Circuit gainStage;     // guitar -> gain stage op-amp output
        Circuit cleanFeed;     // guitar -> summing amp output (with the diode node quiet)
        Circuit diodeFeed;     // diode node -> summing amp output (with the guitar quiet)
        Circuit treble;        // summing amp output -> treble stage output
        Circuit toOutput;      // treble stage output -> pedal output
        Circuit bleed;         // guitar -> pedal output (the clean bleed)
        int gang1Pin1 = 0, gang1Pin3 = 0, cleanGang2Pin1 = 0, cleanGang2Pin3 = 0, diodeGang2Pin1 = 0, diodeGang2Pin3 = 0;
        int trebleTop = 0, trebleBottom = 0;
        double gain = -1.0, trebleKnob = -1.0, period = 1.0 / 384000.0;

        // The clipper: C9 (1 uF) in series with R13 (1k) into the diodes.
        double gCoupling = 0.0, jCoupling = 0.0, diodeNode = 0.0;
    };
}
