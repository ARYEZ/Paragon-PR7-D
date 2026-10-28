#include "Halo.h"

#include <algorithm>
#include <cmath>

namespace effects
{
    namespace
    {
        constexpr double railVolts = 9.0;

        // The original's parts (the same on every published drawing of the germanium version).
        constexpr double inputFarads = 2.2e-6;          // C1
        constexpr double q1CollectorOhms = 33.0e3;      // R2, the supply to Q1's collector
        constexpr double feedbackOhms = 100.0e3;        // R3, Q2's emitter back to Q1's base
        constexpr double supplySideOhms = 470.0;        // R4, the supply to the output point
        constexpr double collectorSideOhms = 8.2e3;     // R5, the output point to Q2's collector
        constexpr double fuzzPotOhms = 1.0e3;           // FUZZ, 1k linear
        constexpr double fuzzBypassFarads = 22.0e-6;    // C2, on its wiper
        constexpr double outputFarads = 0.01e-6;        // C3
        constexpr double outputLoadOhms = 500.0e3 * 1.0e6 / (500.0e3 + 1.0e6);   // the volume pot at full, and the amp

        // The guitar seen from the pedal: a single coil's resistance (no inductance).
        constexpr double guitarOhms = 6.0e3;

        // AC128-class germanium PNP: about 0.22 V across the base-emitter
        // junction at 1 mA, a gain of 100 (the pedal used hand-picked parts of
        // about 80 to 120), 4 backwards, 2.2 Mohm of collector-base leakage
        // (about 2 uA at the 4.5 V Q2 sees), a 1 MHz transition frequency and
        // 30 pF from collector to base.
        constexpr double saturationAmps = 2.0e-7, thermalVolts = 25.85e-3;
        constexpr double forwardGain = 100.0, reverseGain = 4.0;
        constexpr double leakageOhms = 2.2e6;
        constexpr double transitSeconds = 1.0 / (2.0 * 3.14159265358979 * 1.0e6);
        constexpr double emitterJunctionFarads = 100.0e-12, collectorJunctionFarads = 30.0e-12;

        constexpr double fuzzSweepSeconds = 0.02;   // FUZZ glides, so turning it doesn't click
    }

    void Halo::prepare (double oversampledRate) noexcept
    {
        rate = oversampledRate;
        fuzzStep = 1.0 / (fuzzSweepSeconds * rate);
        outputK = 2.0 * rate * outputFarads * outputLoadOhms;
        fuzz = fuzzTarget;

        // The working point: the capacitors open. The FUZZ pot's whole 1k is
        // the DC path whatever its setting, so this holds for every setting.
        v = { 0.2, 0.6, 0.4, 5.4 };
        solve (0.0, true, 400);
        dc = v;

        // Each transistor's speed: its junction capacitances, the base-emitter
        // one including the charge its current stores (fixed at the working point).
        const auto collectorAmps = [] (double vbe, double vbc)
        {
            const auto forward = saturationAmps * (std::exp (vbe / thermalVolts) - 1.0);
            const auto reverse = saturationAmps * (std::exp (vbc / thermalVolts) - 1.0);
            return forward - reverse * (1.0 + 1.0 / reverseGain);
        };
        const auto q1Amps = collectorAmps (dc[b1], dc[b1] - dc[c1]);
        const auto q2Amps = collectorAmps (dc[c1] - dc[e2], dc[c1] - dc[c2]);
        const auto emitterFarads = [] (double amps) { return emitterJunctionFarads + transitSeconds * std::max (amps, 0.0) / thermalVolts; };
        junctions = { { { c1, b1, collectorJunctionFarads },     // Q1
                        { b1, -1, emitterFarads (q1Amps) },
                        { c2, c1, collectorJunctionFarads },     // Q2
                        { c1, e2, emitterFarads (q2Amps) } } };
        for (auto& cap : junctions)
            cap.g = 2.0 * cap.farads * rate;
        inputG = 2.0 * inputFarads * rate;
        bypassG = 2.0 * fuzzBypassFarads * rate;

        reset();
    }

    void Halo::reset() noexcept
    {
        v = previous = dc;
        settleCapacitors();
        const auto x = railVolts - (railVolts - dc[c2]) * supplySideOhms / (supplySideOhms + collectorSideOhms);
        lastX = -x;
        lastY = 0.0;
    }

    void Halo::settleCapacitors() noexcept
    {
        // No current through any of them: each holds the voltage across it.
        for (auto& cap : junctions)
            cap.history = cap.g * (v[(size_t) cap.p] - (cap.q < 0 ? 0.0 : v[(size_t) cap.q]));
        inputHistory = inputG * (0.0 - v[b1]);   // the input sits at 0 V, Q1's base above it
        bypassHistory = bypassG * v[e2] * fuzzBottomOhms() / (fuzzTopOhms() + fuzzBottomOhms());   // the pot divides the emitter's DC
    }

    double Halo::fuzzTopOhms() const noexcept { return fuzzPotOhms * (1.0 - fuzz) + 0.5; }   // emitter to wiper: what FUZZ leaves in
    double Halo::fuzzBottomOhms() const noexcept { return fuzzPotOhms * fuzz + 0.5; }        // wiper to ground, bypassed by C2

    bool Halo::solve (double sourceVolts, bool dcOnly, int maxIterations) noexcept
    {
        // The two linear branches, each folded into one equivalent:
        //  - the guitar's 6k and C1 in series: a resistance from a source, into Q1's base;
        //  - the FUZZ pot and its bypass: a conductance (and a current) from Q2's emitter to ground.
        const auto inputOhms = guitarOhms + 1.0 / inputG;
        const auto inputSource = sourceVolts - inputHistory / inputG;
        const auto gTop = 1.0 / fuzzTopOhms(), gBottom = 1.0 / fuzzBottomOhms();
        const auto wiperG = gTop + gBottom + (dcOnly ? 0.0 : bypassG);
        const auto emitterG = gTop * (wiperG - gTop) / wiperG;
        const auto emitterExtra = dcOnly ? 0.0 : -gTop * bypassHistory / wiperG;

        for (int iteration = 0; iteration < maxIterations; ++iteration)
        {
            double jacobian[numNodes][numNodes] = {};
            double residual[numNodes] = {};   // the current leaving each node
            const auto volts = [this] (int node) { return node < 0 ? 0.0 : v[(size_t) node]; };

            // A conductance between two nodes (-1 = ground), plus a fixed current from p to q.
            const auto branch = [&] (int p, int q, double g, double extraAmps)
            {
                const auto amps = g * (volts (p) - volts (q)) + extraAmps;
                if (p >= 0)
                {
                    residual[p] += amps;
                    jacobian[p][p] += g;
                    if (q >= 0)
                        jacobian[p][q] -= g;
                }
                if (q >= 0)
                {
                    residual[q] -= amps;
                    jacobian[q][q] += g;
                    if (p >= 0)
                        jacobian[q][p] -= g;
                }
            };
            // A resistor from a node to a fixed voltage (the supply, the input).
            const auto toFixed = [&] (int node, double ohms, double fixedVolts)
            {
                residual[node] += (v[(size_t) node] - fixedVolts) / ohms;
                jacobian[node][node] += 1.0 / ohms;
            };
            // An Ebers-Moll transistor: the currents into its collector, base and emitter.
            const auto transistor = [&] (int c, int b, int e)
            {
                const auto vbe = volts (b) - volts (e), vbc = volts (b) - volts (c);
                const auto ef = std::exp (std::min (vbe / thermalVolts, 40.0)), er = std::exp (std::min (vbc / thermalVolts, 40.0));
                const auto forward = saturationAmps * (ef - 1.0), reverse = saturationAmps * (er - 1.0);
                const auto gf = saturationAmps / thermalVolts * ef, gr = saturationAmps / thermalVolts * er;
                const auto kr = 1.0 + 1.0 / reverseGain;

                const double currents[2] = { forward - reverse * kr,                          // collector
                                             forward / forwardGain + reverse / reverseGain }; // base
                // d(current) / d(collector, base, emitter volts)
                const double collector[3] = { gr * kr, gf - gr * kr, -gf };
                const double base[3] = { -gr / reverseGain, gf / forwardGain + gr / reverseGain, -gf / forwardGain };
                const int nodes[3] = { c, b, e };
                for (int row = 0; row < 3; ++row)
                {
                    if (nodes[row] < 0)
                        continue;
                    residual[nodes[row]] += row < 2 ? currents[row] : -(currents[0] + currents[1]);
                    for (int col = 0; col < 3; ++col)
                        if (nodes[col] >= 0)
                            jacobian[nodes[row]][nodes[col]] += row == 0 ? collector[col]
                                                              : row == 1 ? base[col]
                                                                         : -(collector[col] + base[col]);
                }
            };

            if (! dcOnly)
                toFixed (b1, inputOhms, inputSource);   // (C1 blocks DC: no input path at rest)
            toFixed (c1, q1CollectorOhms, railVolts);
            toFixed (c2, supplySideOhms + collectorSideOhms, railVolts);
            branch (e2, b1, 1.0 / feedbackOhms, 0.0);
            branch (e2, -1, emitterG, emitterExtra);
            branch (c1, b1, 1.0 / leakageOhms, 0.0);
            branch (c2, c1, 1.0 / leakageOhms, 0.0);
            if (! dcOnly)
                for (const auto& cap : junctions)
                    branch (cap.p, cap.q, cap.g, -cap.history);
            transistor (c1, b1, -1);   // Q1, emitter grounded
            transistor (c2, c1, e2);   // Q2, its base on Q1's collector

            // Solve jacobian x step = -residual (Gaussian elimination, partial pivoting).
            double step[numNodes];
            for (int i = 0; i < numNodes; ++i)
                step[i] = -residual[i];
            for (int col = 0; col < numNodes; ++col)
            {
                auto pivot = col;
                for (int row = col + 1; row < numNodes; ++row)
                    if (std::abs (jacobian[row][col]) > std::abs (jacobian[pivot][col]))
                        pivot = row;
                if (std::abs (jacobian[pivot][col]) < 1.0e-30)
                    return false;
                if (pivot != col)
                {
                    for (int k = 0; k < numNodes; ++k)
                        std::swap (jacobian[col][k], jacobian[pivot][k]);
                    std::swap (step[col], step[pivot]);
                }
                for (int row = col + 1; row < numNodes; ++row)
                {
                    const auto factor = jacobian[row][col] / jacobian[col][col];
                    for (int k = col; k < numNodes; ++k)
                        jacobian[row][k] -= factor * jacobian[col][k];
                    step[row] -= factor * step[col];
                }
            }
            for (int row = numNodes - 1; row >= 0; --row)
            {
                auto sum = step[row];
                for (int k = row + 1; k < numNodes; ++k)
                    sum -= jacobian[row][k] * step[k];
                step[row] = sum / jacobian[row][row];
            }

            // Small steps only: a junction's current changes e-fold every 26 mV.
            auto largest = 0.0;
            for (int i = 0; i < numNodes; ++i)
            {
                v[(size_t) i] += std::clamp (step[i], -0.1, 0.1);
                largest = std::max (largest, std::abs (step[i]));
            }
            if (largest < (dcOnly ? 1.0e-9 : 1.0e-6))   // a microvolt: far below anything audible
                return true;
        }
        return false;
    }

    double Halo::process (double volts) noexcept
    {
        fuzz += std::clamp (fuzzTarget - fuzz, -fuzzStep, fuzzStep);

        // Start from where the last two samples were heading: fewer steps to the answer.
        const auto now = v;
        for (size_t i = 0; i < v.size(); ++i)
            v[i] += v[i] - previous[i];
        previous = now;

        // The mirror image of the PNP original: the guitar goes in upside down
        // and the output comes out upside down, so it all matches the original.
        const auto sourceVolts = -volts;
        solve (sourceVolts, false, 12);

        // Each capacitor's history, for the next sample.
        for (auto& cap : junctions)
        {
            const auto across = v[(size_t) cap.p] - (cap.q < 0 ? 0.0 : v[(size_t) cap.q]);
            const auto amps = cap.g * across - cap.history;
            cap.history = cap.g * across + amps;
        }
        const auto inputAmps = (sourceVolts - inputHistory / inputG - v[b1]) / (guitarOhms + 1.0 / inputG);
        inputHistory += 2.0 * inputAmps;   // the trapezoidal update, g x across + i, with across = (i + history) / g
        const auto gTop = 1.0 / fuzzTopOhms(), gBottom = 1.0 / fuzzBottomOhms();
        const auto wiper = (gTop * v[e2] + bypassHistory) / (gTop + gBottom + bypassG);
        bypassHistory = 2.0 * bypassG * wiper - bypassHistory;

        // The output point between the 470 and the 8.2k, through C3 into the load.
        const auto x = -(railVolts - (railVolts - v[c2]) * supplySideOhms / (supplySideOhms + collectorSideOhms));
        const auto y = (outputK * (x - lastX) + (outputK - 1.0) * lastY) / (outputK + 1.0);
        lastX = x;
        lastY = y;
        return y;
    }
}
