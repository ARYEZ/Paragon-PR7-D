#include "Abyss.h"

#include <algorithm>

namespace effects
{
    namespace
    {
        constexpr double distPotOhms = 250.0e3;     // VR01, 250kA
        constexpr double tonePotOhms = 100.0e3;     // VR03a / VR03b / VR02b
        constexpr double midFreqPotOhms = 50.0e3;   // VR02a, 50kC (two sections)
        constexpr double swingVolts = 3.0;          // the M5218s on 9 V
        constexpr double diodeSaturation = 2.52e-9, diodeThermal = 1.752 * 25.85e-3;   // 1SS133 (silicon)
        constexpr double clipFarads = 10.0e-6, clipOhms = 2.2e3, diodeOutOhms = 10.0e3;   // C027, R033, R032

        // A pot's taper: the share of its track at a knob position, passing
        // `half` at half rotation (0.15 for audio "A", 0.85 for "C").
        double taper (double knob, double half) noexcept
        {
            const auto base = ((1.0 - half) / half) * ((1.0 - half) / half);
            return (std::pow (base, std::clamp (knob, 0.0, 1.0)) - 1.0) / (base - 1.0);
        }

        double swing (double volts) noexcept { return opAmpClip (volts, swingVolts); }

        // One of the drawing's bootstrapped emitter followers on an op-amp's
        // (-) leg: C1 into n1, n1 through rE to the emitter, n1 through C2
        // to the base (rBase to the bias); the emitter follows the base.
        void bootstrap (Circuit& c, int minus, double c1, double rE, double c2, double rBase)
        {
            const auto n1 = c.node(), base = c.node(), emitter = c.node();
            c.capacitor (minus, n1, c1);
            c.resistor (n1, emitter, rE);
            c.capacitor (n1, base, c2);
            c.resistor (base, 0, rBase);
            c.opAmp (base, emitter, emitter);
            c.resistor (emitter, 0, 10.0e3);
        }
    }

    void Abyss::prepare (double oversampledRate, double baseRate) noexcept
    {
        period = 1.0 / oversampledRate;

        // IC3b: C033 0.015 uF and R043 100K into (+); feedback R044 220K ||
        // C032 100 pF; Q010's branch (C034 0.027 uF, R046 2K2, C035 0.01 uF, R053 47K) on (-).
        gainHump = {};
        {
            auto& c = gainHump;
            const auto in = c.node(), plus = c.node(), minus = c.node(), out = c.node();
            c.capacitor (in, plus, 0.015e-6);
            c.resistor (plus, 0, 100.0e3);
            c.opAmp (plus, minus, out);
            c.resistor (minus, out, 220.0e3);
            c.capacitor (minus, out, 100.0e-12);
            bootstrap (c, minus, 0.027e-6, 2.2e3, 0.01e-6, 47.0e3);
            c.setInput (in);
            c.setOutput (out);
            c.prepare (oversampledRate);
        }

        // IC3a, DIST: R045 10K, R042 10K || C031 0.047 uF to the bias, C029
        // 0.033 uF and R040 100K into (+); (-) leg R041 1K + C030 10 uF;
        // feedback C028 47 pF || (DIST + R051 1K).
        dist = {};
        {
            auto& c = dist;
            const auto in = c.node(), p = c.node(), plus = c.node(), minus = c.node(), out = c.node(), leg = c.node(), mid = c.node();
            c.resistor (in, p, 10.0e3);
            c.resistor (p, 0, 10.0e3);
            c.capacitor (p, 0, 0.047e-6);
            c.capacitor (p, plus, 0.033e-6);
            c.resistor (plus, 0, 100.0e3);
            c.opAmp (plus, minus, out);
            c.resistor (minus, leg, 1.0e3);
            c.capacitor (leg, 0, 10.0e-6);
            c.capacitor (minus, out, 47.0e-12);
            distPot = c.resistor (minus, mid, distPotOhms * 0.15);
            c.resistor (mid, out, 1.0e3);
            c.setInput (in);
            c.setOutput (out);
            c.prepare (oversampledRate);
        }

        // After the diodes: R032 10K, R031 4K7 + C023 0.015 uF to ground,
        // C021 1 uF and R029 100K into IC4b's (+). The first Circuit is the
        // diodes' load (the voltage after R032); the second, IC4b itself.
        diodeLoad = {};
        {
            auto& c = diodeLoad;
            const auto k = c.node(), l = c.node(), n31 = c.node(), plus = c.node();
            c.resistor (k, l, diodeOutOhms);
            c.resistor (l, n31, 4.7e3);
            c.capacitor (n31, 0, 0.015e-6);
            c.capacitor (l, plus, 1.0e-6);
            c.resistor (plus, 0, 100.0e3);
            c.setInput (k);
            c.setOutput (l);
            c.prepare (oversampledRate);
        }
        afterDiodes = {};
        {
            // IC4b: feedback R030 3K3 || C022 47 pF; Q008's branch (C024
            // 0.015 uF, R034 1K, C025 0.0015 uF, R036 47K) and Q007's (C020
            // 0.22 uF, R027 470, C017 0.047 uF, R025 470K) on (-).
            auto& c = afterDiodes;
            const auto l = c.node(), plus = c.node(), minus = c.node(), out = c.node();
            c.capacitor (l, plus, 1.0e-6);
            c.resistor (plus, 0, 100.0e3);
            c.opAmp (plus, minus, out);
            c.resistor (minus, out, 3.3e3);
            c.capacitor (minus, out, 47.0e-12);
            bootstrap (c, minus, 0.015e-6, 1.0e3, 0.0015e-6, 47.0e3);
            bootstrap (c, minus, 0.22e-6, 470.0, 0.047e-6, 470.0e3);
            c.setInput (l);
            c.setOutput (out);
            c.prepare (oversampledRate);
        }

        // IC1a with HIGH and LOW: R014 22K into (+) (node 7); feedback R015
        // 22K || C010 10 pF to (-) (node 8). HIGH between them, its wiper to
        // R061 2K2 + C044 0.01 uF; LOW between them, its wiper into C008
        // 0.22 uF and IC1b's bootstrapped high-pass (R012 2K2, C009 0.047 uF, R013 100K).
        highLow = {};
        {
            auto& c = highLow;
            const auto in = c.node(), n7 = c.node(), n8 = c.node(), out = c.node();
            c.resistor (in, n7, 22.0e3);
            c.opAmp (n7, n8, out);
            c.resistor (n8, out, 22.0e3);
            c.capacitor (n8, out, 10.0e-12);
            const auto wH = c.node(), tH = c.node();
            highA = c.resistor (n7, wH, tonePotOhms * 0.5);
            highB = c.resistor (wH, n8, tonePotOhms * 0.5);
            c.resistor (wH, tH, 2.2e3);
            c.capacitor (tH, 0, 0.01e-6);
            const auto wL = c.node(), x = c.node(), p5 = c.node(), follower = c.node();
            lowA = c.resistor (n7, wL, tonePotOhms * 0.5);
            lowB = c.resistor (wL, n8, tonePotOhms * 0.5);
            c.capacitor (wL, x, 0.22e-6);
            c.resistor (x, follower, 2.2e3);
            c.capacitor (x, p5, 0.047e-6);
            c.resistor (p5, 0, 100.0e3);
            c.opAmp (p5, follower, follower);
            c.setInput (in);
            c.setOutput (out);
            c.prepare (baseRate);
        }

        // IC2a with MIDDLE and MID FREQ: C011 1 uF into node Y: R050 330 to
        // node 12, R038 47K into (-); feedback R035 47K || C026 100 pF; (+)
        // at Z (R039 1M). The output through C037 1 uF and R049 330 to node
        // 11; MIDDLE between nodes 12 and 11, its wiper into IC2b (a
        // follower), C036 0.022 uF and R048 2K2 to node 9; MID FREQ's two
        // sections from node 10 to node 9 and to R062 2K2 to ground; C043
        // 0.0082 uF from node 10 to ground; C038 0.1 uF from node 10 to Z.
        middle = {};
        {
            auto& c = middle;
            const auto in = c.node(), y = c.node(), n12 = c.node(), minus = c.node(), z = c.node(), out = c.node();
            c.capacitor (in, y, 1.0e-6);
            c.resistor (y, n12, 330.0);
            c.resistor (y, minus, 47.0e3);
            c.resistor (z, 0, 1.0e6);
            c.opAmp (z, minus, out);
            c.resistor (minus, out, 47.0e3);
            c.capacitor (minus, out, 100.0e-12);
            const auto n37 = c.node(), n11 = c.node(), wM = c.node(), fb = c.node();
            c.capacitor (out, n37, 1.0e-6);
            c.resistor (n37, n11, 330.0);
            midA = c.resistor (n12, wM, tonePotOhms * 0.5);
            midB = c.resistor (wM, n11, tonePotOhms * 0.5);
            c.opAmp (wM, fb, fb);
            const auto n36 = c.node(), n9 = c.node(), n10 = c.node(), n62 = c.node();
            c.capacitor (fb, n36, 0.022e-6);
            c.resistor (n36, n9, 2.2e3);
            freqA = c.resistor (n10, n9, midFreqPotOhms * 0.5);
            freqB = c.resistor (n10, n62, midFreqPotOhms * 0.5);
            c.resistor (n62, 0, 2.2e3);
            c.capacitor (n10, 0, 0.0082e-6);
            c.capacitor (n10, z, 0.1e-6);
            c.setInput (in);
            c.setOutput (out);
            c.prepare (baseRate);
        }

        gCoupling = 2.0 * clipFarads / period;
        distKnob = highKnob = lowKnob = middleKnob = midFreqKnob = -1.0;
        setDistortion (0.5);
        setEq (0.5, 0.5, 0.5, 0.5);
        reset();
    }

    void Abyss::reset() noexcept
    {
        for (auto* c : { &gainHump, &dist, &diodeLoad, &afterDiodes, &highLow, &middle })
            c->reset();
        jCoupling = diodeNode = 0.0;
    }

    void Abyss::setDistortion (double knob) noexcept
    {
        const auto k = std::clamp (knob, 0.0, 1.0);
        if (std::abs (k - distKnob) < 1.0e-4)
            return;
        distKnob = k;
        dist.setResistor (distPot, distPotOhms * taper (k, 0.15));   // audio taper: clockwise, more gain
        dist.update();
    }

    void Abyss::setEq (double high, double low, double mid, double midFreq) noexcept
    {
        high = std::clamp (high, 0.0, 1.0);
        low = std::clamp (low, 0.0, 1.0);
        mid = std::clamp (mid, 0.0, 1.0);
        midFreq = std::clamp (midFreq, 0.0, 1.0);

        // HIGH, LOW and MIDDLE: the wiper towards the op-amp's (-) side (node 8, node 11) is the boost.
        if (std::abs (high - highKnob) > 1.0e-4 || std::abs (low - lowKnob) > 1.0e-4)
        {
            highKnob = high;
            lowKnob = low;
            highLow.setResistor (highA, tonePotOhms * high);
            highLow.setResistor (highB, tonePotOhms * (1.0 - high));
            highLow.setResistor (lowA, tonePotOhms * low);
            highLow.setResistor (lowB, tonePotOhms * (1.0 - low));
            highLow.update();
        }
        if (std::abs (mid - middleKnob) > 1.0e-4 || std::abs (midFreq - midFreqKnob) > 1.0e-4)
        {
            middleKnob = mid;
            midFreqKnob = midFreq;
            middle.setResistor (midA, tonePotOhms * mid);
            middle.setResistor (midB, tonePotOhms * (1.0 - mid));
            // MID FREQ: a "C" taper; clockwise is less resistance, a higher frequency.
            const auto r = midFreqPotOhms * taper (1.0 - midFreq, 0.85);
            middle.setResistor (freqA, r);
            middle.setResistor (freqB, r);
            middle.update();
        }
    }

    double Abyss::clip (double volts) noexcept
    {
        const auto a = swing (gainHump.process (volts));
        const auto b = swing (dist.process (a));

        // C027 and R033 into the diodes; their load (R032 onwards) is
        // feedthrough x node + pending, from the load's own Circuit.
        const auto sourceR = clipOhms + 1.0 / gCoupling;
        const auto source = b - jCoupling / gCoupling;
        const auto through = diodeLoad.feedthrough(), pending = diodeLoad.pending();
        auto v = diodeNode;
        for (int iteration = 0; iteration < 20; ++iteration)
        {
            const auto x = std::clamp (v / diodeThermal, -40.0, 40.0);
            const auto load = (v * (1.0 - through) - pending) / diodeOutOhms;
            const auto f = (source - v) / sourceR - 2.0 * diodeSaturation * std::sinh (x) - load;
            const auto d = -1.0 / sourceR - 2.0 * diodeSaturation / diodeThermal * std::cosh (x) - (1.0 - through) / diodeOutOhms;
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
        jCoupling = 2.0 * (source - v) / sourceR + jCoupling;
        diodeNode = v;

        const auto l = diodeLoad.process (v);
        return -swing (afterDiodes.process (l));   // IC4a: a unity-gain inverter
    }

    double Abyss::equalise (double volts) noexcept
    {
        return swing (middle.process (swing (highLow.process (volts))));
    }
}
