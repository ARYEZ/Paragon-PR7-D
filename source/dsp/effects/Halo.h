#pragma once

#include <array>

namespace effects
{
    // Halo: OD / DISTORTION's germanium fuzz, modelled on the original
    // two-transistor fuzz of 1966-68 (germanium PNP transistors, positive
    // ground), from the published circuit (General Guitar Gadgets' drawing,
    // checked against R.G. Keen's analysis, "The Technology of the Fuzz
    // Face"). Every part value is the original's:
    //
    //   guitar --6k-- 2.2 uF --+-- Q1 base          Q1 collector = Q2 base (33k to the supply)
    //                          |                    Q2 collector: 8.2k + 470 to the supply,
    //                          +--100k-- Q2 emitter     the output taken between them
    //                                       |
    //                                  FUZZ (1k), its wiper to ground through 22 uF
    //
    // There's no clipper: the sound is the two transistors and the one 100k
    // that both biases them and feeds back. FUZZ takes the emitter
    // resistance out of Q2 for the signal (its whole 1k stays there for the
    // bias), so its gain goes from about 8 to everything the transistor has.
    // The pedal's input impedance is very low, so the guitar is part of the
    // circuit: a 6k source resistance (a single coil's) is modelled in front
    // of it. Picking softer, or a quieter guitar, cleans it up.
    //
    // The circuit is solved sample by sample at the oversampled rate, all six
    // of its nodes at once (Newton's method, the capacitors as their
    // trapezoidal equivalents), with the transistors as Ebers-Moll junctions:
    // AC128-class germanium, a gain (beta) of 100, 2.2 Mohm of collector-base
    // leakage, and their limited speed as junction capacitance (fixed at the
    // working point). It's built as the mirror image of the PNP original
    // (every voltage the other way up), which is the same circuit with the
    // same polarity and the same uneven clipping.
    //
    // Not modelled: the pickup's inductance (its rising impedance with
    // frequency), temperature (germanium drifts with it), a tired battery,
    // and the guitar's own volume knob (which raises the source resistance:
    // here the volume is already in the recording).
    class Halo
    {
    public:
        void prepare (double oversampledRate) noexcept;
        void reset() noexcept;

        // FUZZ, 0 to 1 (a linear pot, fully up at the emitter end).
        void setFuzz (double fuzz) noexcept { fuzzTarget = fuzz < 0.0 ? 0.0 : fuzz > 1.0 ? 1.0 : fuzz; }

        // One oversampled sample: guitar volts in, the pedal's output out
        // (after its output capacitor, into a 500k volume pot at full and a
        // 1M amp input).
        double process (double volts) noexcept;

        // The working point, for tests: volts from ground (their size; the
        // original's are all negative), with no signal.
        double q1CollectorVolts() const noexcept { return dc[c1]; }
        double q2EmitterVolts() const noexcept { return dc[e2]; }
        double q2CollectorVolts() const noexcept { return dc[c2]; }

    private:
        // The nodes solved for: Q1's base, Q1's collector (= Q2's base), Q2's
        // emitter and collector. The input (between the guitar's resistance
        // and C1) and the FUZZ pot's wiper hold only resistors and
        // capacitors, so they're worked out from these (fewer to solve).
        enum Node { b1, c1, e2, c2, numNodes };
        using Volts = std::array<double, numNodes>;

        struct Capacitor
        {
            int p, q;            // nodes (-1 = ground)
            double farads, g = 0.0, history = 0.0;
        };

        bool solve (double sourceVolts, bool dcOnly, int maxIterations) noexcept;
        void settleCapacitors() noexcept;
        double fuzzTopOhms() const noexcept;
        double fuzzBottomOhms() const noexcept;

        double rate = 384000.0;
        Volts v {}, dc {}, previous {};
        std::array<Capacitor, 4> junctions {};   // each transistor's collector-base and base-emitter
        double inputG = 0.0, inputHistory = 0.0;   // C1, from the input to Q1's base
        double bypassG = 0.0, bypassHistory = 0.0; // C2, from the FUZZ pot's wiper to ground
        double fuzz = 0.5, fuzzTarget = 0.5, fuzzStep = 0.0;
        double outputK = 1.0, lastX = 0.0, lastY = 0.0;   // the output capacitor and load, as a one-pole high-pass
    };
}
