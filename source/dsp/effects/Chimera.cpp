#include "Chimera.h"

#include <algorithm>

namespace effects
{
    namespace
    {
        constexpr double gainPotOhms = 100.0e3;   // dual gang (from clone parts lists; not printed on the drawing)
        constexpr double treblePotOhms = 10.0e3;

        // The gain stage's TL072 runs on about +-9 V (a charge pump), and gets within about 1.5 V of each.
        constexpr double gainStageSwing = 7.5;

        // 1N34A germanium diodes: about 0.35 V forward.
        constexpr double diodeSaturation = 200.0e-9, diodeThermal = 1.3 * 25.85e-3;
        constexpr double clipFarads = 1.0e-6, clipOhms = 1.0e3;   // C9, R13
        constexpr double diodeLoadOhms = 47.0e3;                  // R16, into the summing amp's virtual ground

        // The output's VOLUME pot (10k) is held at noon; LEVEL sets the level after it.
        constexpr double volumeTopOhms = 560.0 + 5.0e3, volumeBottomOhms = 5.0e3;

        // An audio-taper ("A") pot: 15% of the track at half rotation.
        double audioTaper (double knob) noexcept
        {
            constexpr double base = (0.85 / 0.15) * (0.85 / 0.15);
            return (std::pow (base, std::clamp (knob, 0.0, 1.0)) - 1.0) / (base - 1.0);
        }

        // The clean feed's network, around the summing amp's (-) input s:
        // GAIN's second gang hanging off the input through R5 || C4, R17,
        // C12 + R18, and the branch from the diode side (C10 to p; R16; C11
        // + R15). `source` drives B (guitar) or d (diodes); the other is ground.
        struct Feed { int gangPin1, gangPin3; };
        Feed buildFeed (Circuit& c, bool fromGuitar)
        {
            const auto source = c.node(), s = c.node(), u = c.node();
            const auto b2 = c.node(), nc6 = c.node(), bg = c.node(), n12 = c.node(), p = c.node(), n11 = c.node();
            const auto b = fromGuitar ? source : 0, d = fromGuitar ? 0 : source;

            c.resistor (b, b2, 5.1e3);       // R5
            c.capacitor (b, b2, 68.0e-9);    // C4
            c.resistor (b2, 0, 1.5e3);       // R8
            c.capacitor (b2, nc6, 0.39e-6);  // C6
            c.resistor (nc6, 0, 1.0e3);      // R9
            const auto pin1 = c.resistor (b2, bg, gainPotOhms * 0.5);   // gang 2: pin 1 to wiper
            const auto pin3 = c.resistor (bg, 0, gainPotOhms * 0.5);    // gang 2: wiper to pin 3
            c.resistor (bg, s, 2.0e3);       // R17
            c.capacitor (s, n12, 27.0e-9);   // C12
            c.resistor (n12, bg, 2.0e3);     // R18
            c.capacitor (d, p, 1.0e-6);      // C10
            c.resistor (p, s, 47.0e3);       // R16
            c.capacitor (p, n11, 2.2e-9);    // C11
            c.resistor (n11, bg, 22.0e3);    // R15

            if (fromGuitar)
            {
                // Feed-forward 1: C3 into the gain stage's input node, R7 + C16 (a low-pass), R19 into s.
                const auto in1 = c.node(), g = c.node(), ff1 = c.node();
                c.capacitor (b, in1, 0.1e-6);    // C3
                c.resistor (in1, g, 10.0e3);     // R6
                c.capacitor (in1, g, 68.0e-9);   // C5
                c.resistor (g, 0, gainPotOhms * 0.5);   // gang 1 pin 1 to wiper: kept at noon here (the op-amp draws no current, this only loads in1 slightly)
                c.resistor (in1, ff1, 1.5e3);    // R7
                c.capacitor (ff1, 0, 1.0e-6);    // C16
                c.resistor (ff1, s, 15.0e3);     // R19
            }

            // The summing amp (IC2_A), inverting: R20 || C13.
            c.opAmp (0, s, u);
            c.resistor (s, u, 392.0e3);      // R20
            c.capacitor (s, u, 820.0e-12);   // C13
            c.setInput (source);
            c.setOutput (u);
            return { pin1, pin3 };
        }
    }

    void Chimera::prepare (double oversampledRate, double baseRate) noexcept
    {
        period = 1.0 / oversampledRate;

        // The gain stage (IC1_B), non-inverting: C3, R6 || C5 into (+), with
        // gang 1 from (+) to the 4.5 V bias; the (-) leg R10 + the rest of
        // gang 1 against R11 || C7; feedback R12 || C8. R7 + C16 + R19 load
        // its input node (R19 ends at the summing amp's virtual ground).
        gainStage = {};
        {
            auto& c = gainStage;
            const auto b = c.node(), in1 = c.node(), g = c.node(), h = c.node(), inv = c.node(), out = c.node(), ff1 = c.node();
            c.capacitor (b, in1, 0.1e-6);    // C3
            c.resistor (in1, g, 10.0e3);     // R6
            c.capacitor (in1, g, 68.0e-9);   // C5
            gang1Pin1 = c.resistor (g, 0, gainPotOhms * 0.5);
            gang1Pin3 = c.resistor (h, 0, 2.0e3 + gainPotOhms * 0.5);   // R10 + gang 1 pin 3 to wiper
            c.resistor (h, inv, 15.0e3);     // R11
            c.capacitor (h, inv, 82.0e-9);   // C7
            c.resistor (inv, out, 422.0e3);  // R12
            c.capacitor (inv, out, 390.0e-12);   // C8
            c.opAmp (g, inv, out);
            c.resistor (in1, ff1, 1.5e3);    // R7
            c.capacitor (ff1, 0, 1.0e-6);    // C16
            c.resistor (ff1, 0, 15.0e3);     // R19, to the virtual ground
            c.setInput (b);
            c.setOutput (out);
            c.prepare (oversampledRate);
        }

        cleanFeed = {};
        const auto clean = buildFeed (cleanFeed, true);
        cleanGang2Pin1 = clean.gangPin1;
        cleanGang2Pin3 = clean.gangPin3;
        cleanFeed.prepare (baseRate);
        diodeFeed = {};
        const auto fromDiodes = buildFeed (diodeFeed, false);
        diodeGang2Pin1 = fromDiodes.gangPin1;
        diodeGang2Pin3 = fromDiodes.gangPin3;
        diodeFeed.prepare (baseRate);

        // The TREBLE stage (IC2_B), inverting: R22 and R24 (100k each), with
        // R21 + pot + R23 and C14 between them.
        treble = {};
        {
            auto& c = treble;
            const auto in = c.node(), v = c.node(), wiper = c.node(), out = c.node();
            c.resistor (in, v, 100.0e3);     // R22
            c.resistor (v, out, 100.0e3);    // R24
            c.opAmp (0, v, out);
            trebleTop = c.resistor (in, wiper, 1.8e3 + treblePotOhms * 0.5);   // R21 + pot
            c.capacitor (wiper, v, 3.9e-9);  // C14
            trebleBottom = c.resistor (wiper, out, 4.7e3 + treblePotOhms * 0.5);   // pot + R23
            c.setInput (in);
            c.setOutput (out);
            c.prepare (baseRate);
        }

        // The output: C15 and R25 + VOLUME from the treble stage, and the
        // clean bleed (C2, R3, R4, R26) from the input, meeting at the VOLUME
        // pot's wiper (with R28 and the amp's input across it). Each is worked
        // out with the other source quiet.
        const auto buildOutput = [] (Circuit& c, bool fromGuitar)
        {
            const auto source = c.node(), n15 = c.node(), out = c.node(), nc2 = c.node(), nr4 = c.node();
            const auto w = fromGuitar ? 0 : source, b = fromGuitar ? source : 0;
            c.capacitor (w, n15, 4.7e-6);           // C15
            c.resistor (n15, out, volumeTopOhms);   // R25 + VOLUME
            c.resistor (out, 0, volumeBottomOhms);
            c.resistor (out, 0, 100.0e3);           // R28
            c.resistor (out, 0, 1.0e6);             // the amp's input
            c.capacitor (b, nc2, 4.7e-6);           // C2
            c.resistor (nc2, 0, 100.0e3);           // R3
            c.resistor (nc2, nr4, 560.0);           // R4
            c.resistor (nr4, out, 68.0e3);          // R26
            c.setInput (source);
            c.setOutput (out);
        };
        toOutput = {};
        buildOutput (toOutput, false);
        toOutput.prepare (baseRate);
        bleed = {};
        buildOutput (bleed, true);
        bleed.prepare (baseRate);

        gCoupling = 2.0 * clipFarads / period;
        gain = trebleKnob = -1.0;
        setGain (0.5);
        setTreble (0.5);
        reset();
    }

    void Chimera::reset() noexcept
    {
        for (auto* c : { &gainStage, &cleanFeed, &diodeFeed, &treble, &toOutput, &bleed })
            c->reset();
        jCoupling = diodeNode = 0.0;
    }

    void Chimera::setGain (double knob) noexcept
    {
        const auto k = std::clamp (knob, 0.0, 1.0);
        if (std::abs (k - gain) < 1.0e-4)
            return;
        gain = k;
        const auto f = audioTaper (k);
        const auto pin1 = std::max (0.5, gainPotOhms * f), pin3 = std::max (0.5, gainPotOhms * (1.0 - f));
        gainStage.setResistor (gang1Pin1, pin1);
        gainStage.setResistor (gang1Pin3, 2.0e3 + pin3);
        gainStage.update();
        cleanFeed.setResistor (cleanGang2Pin1, pin1);
        cleanFeed.setResistor (cleanGang2Pin3, pin3);
        cleanFeed.update();
        diodeFeed.setResistor (diodeGang2Pin1, pin1);
        diodeFeed.setResistor (diodeGang2Pin3, pin3);
        diodeFeed.update();
    }

    void Chimera::setTreble (double knob) noexcept
    {
        const auto k = std::clamp (knob, 0.0, 1.0);
        if (std::abs (k - trebleKnob) < 1.0e-4)
            return;
        trebleKnob = k;
        // Towards R21 is the most treble.
        treble.setResistor (trebleTop, 1.8e3 + treblePotOhms * (1.0 - k));
        treble.setResistor (trebleBottom, 4.7e3 + treblePotOhms * k);
        treble.update();
    }

    double Chimera::clip (double volts) noexcept
    {
        const auto stage = opAmpClip (gainStage.process (volts), gainStageSwing);

        // C9 and R13 into the diodes, loaded by R16 to the summing amp's
        // virtual ground; Newton finds the diode node.
        const auto sourceR = clipOhms + 1.0 / gCoupling;
        const auto source = stage - jCoupling / gCoupling;
        auto v = diodeNode;
        for (int iteration = 0; iteration < 20; ++iteration)
        {
            const auto x = std::clamp (v / diodeThermal, -40.0, 40.0);
            const auto [sinhX, coshX] = sinhCosh (x);   // (PR7-K: one exp for both)
            const auto f = (source - v) / sourceR - 2.0 * diodeSaturation * sinhX - v / diodeLoadOhms;
            const auto d = -1.0 / sourceR - 2.0 * diodeSaturation / diodeThermal * coshX - 1.0 / diodeLoadOhms;
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
        return v;
    }

    double Chimera::finish (double guitar, double diodes) noexcept
    {
        const auto summed = cleanFeed.process (guitar) + diodeFeed.process (diodes);
        return toOutput.process (treble.process (summed)) + bleed.process (guitar);
    }
}
