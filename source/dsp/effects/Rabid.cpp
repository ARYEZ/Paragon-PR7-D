#include "Rabid.h"

#include <algorithm>

namespace effects
{
    namespace
    {
        // The RAT 2's parts (Effects Layouts "RAT Project" BOM).
        constexpr double distortionPotOhms = 100.0e3;           // A100k, as a variable resistor
        constexpr double feedbackFarads = 100.0e-12;             // across it
        constexpr double leg1Ohms = 560.0, leg1Farads = 4.7e-6;  // the (-) input's two legs to ground
        constexpr double leg2Ohms = 47.0, leg2Farads = 2.2e-6;
        constexpr double clipOhms = 1.0e3, clipFarads = 4.7e-6;  // into the diodes
        constexpr double filterFixedOhms = 1.5e3, filterPotOhms = 100.0e3, filterFarads = 3.3e-9;

        // The LM308 with its 30 pF compensation: about 1 MHz gain-bandwidth,
        // 0.3 V/us slew rate, and about 3 V of swing either way of its bias.
        constexpr double gainBandwidthHz = 1.0e6, slewVoltsPerSecond = 0.3e6, swingVolts = 3.0;

        // 1N4148 silicon diodes.
        constexpr double diodeSaturation = 2.52e-9, diodeThermal = 1.752 * 25.85e-3;
    }

    void Rabid::prepare (double oversampledRate) noexcept
    {
        period = 1.0 / oversampledRate;

        // The op-amp's output (the input here) through DISTORTION || 100 pF
        // to its (-) input, and the two legs from there to ground.
        feedback = {};
        const auto out = feedback.node(), minus = feedback.node(), leg1 = feedback.node(), leg2 = feedback.node();
        distortionPot = feedback.resistor (out, minus, distortionPotOhms * 0.25);
        feedback.capacitor (out, minus, feedbackFarads);
        feedback.resistor (minus, leg1, leg1Ohms);
        feedback.capacitor (leg1, 0, leg1Farads);
        feedback.resistor (minus, leg2, leg2Ohms);
        feedback.capacitor (leg2, 0, leg2Farads);
        feedback.setInput (out);
        feedback.setOutput (minus);
        feedback.prepare (oversampledRate);
        distortion = -1.0;
        setDistortion (0.5);

        gCoupling = 2.0 * clipFarads / period;
        gFilterCap = 2.0 * filterFarads / period;
        setFilter (0.5);
        reset();
    }

    void Rabid::reset() noexcept
    {
        feedback.reset();
        opAmpOut = jCoupling = jFilterCap = diodeNode = 0.0;
    }

    void Rabid::setDistortion (double knob) noexcept
    {
        // Audio taper: more resistance, more gain.
        const auto k = std::clamp (knob, 0.0, 1.0);
        if (std::abs (k - distortion) < 1.0e-4)
            return;
        distortion = k;
        feedback.setResistor (distortionPot, distortionPotOhms * k * k);
        feedback.update();
    }

    void Rabid::setFilter (double knob) noexcept
    {
        const auto k = std::clamp (knob, 0.0, 1.0);
        filterOhms = filterFixedOhms + filterPotOhms * k * k;
    }

    double Rabid::process (double volts) noexcept
    {
        // The op-amp: its output moves at (gain-bandwidth) x (+ minus -),
        // solved implicitly against its own feedback (the (-) input is
        // feedthrough x output + pending), then held to its slew rate and swing.
        const auto rate = 6.283185307179586 * gainBandwidthHz * period;
        const auto minusPending = feedback.pending();
        auto next = (opAmpOut + rate * (volts - minusPending)) / (1.0 + rate * feedback.feedthrough());
        const auto maxStep = slewVoltsPerSecond * period;
        next = std::clamp (next, opAmpOut - maxStep, opAmpOut + maxStep);
        next = std::clamp (next, -swingVolts, swingVolts);
        opAmpOut = next;
        feedback.process (opAmpOut);

        // The clipper: the op-amp through 1k and 4.7 uF into the diodes; the
        // FILTER (1.5k + pot) into 3.3 nF hangs off the same node. Every
        // capacitor is its trapezoidal equivalent; Newton finds the diode node.
        const auto sourceR = clipOhms + 1.0 / gCoupling;
        const auto source = opAmpOut - jCoupling / gCoupling;
        const auto gFilter = 1.0 / filterOhms;
        const auto filterShare = gFilter / (gFilter + gFilterCap);   // how the filter node follows the diode node
        auto v = diodeNode;
        for (int iteration = 0; iteration < 20; ++iteration)
        {
            const auto vf = (v * gFilter + jFilterCap) / (gFilter + gFilterCap);
            const auto x = std::clamp (v / diodeThermal, -40.0, 40.0);
            const auto diode = 2.0 * diodeSaturation * std::sinh (x);
            const auto slope = 2.0 * diodeSaturation / diodeThermal * std::cosh (x);
            const auto f = (source - v) / sourceR - diode - (v - vf) * gFilter;
            const auto d = -1.0 / sourceR - slope - gFilter * (1.0 - filterShare);
            const auto step = std::clamp (-f / d, -0.1, 0.1);
            v += step;
            if (std::abs (step) < 1.0e-9)
                break;
        }
        if (! std::isfinite (v))
        {
            reset();
            return 0.0;
        }

        const auto vf = (v * gFilter + jFilterCap) / (gFilter + gFilterCap);
        const auto inputCurrent = (source - v) / sourceR;
        jCoupling = 2.0 * inputCurrent + jCoupling;
        jFilterCap = 2.0 * gFilterCap * vf - jFilterCap;
        diodeNode = v;
        return vf;
    }
}
