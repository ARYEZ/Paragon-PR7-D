#include "Cosmonaut.h"

#include <algorithm>

namespace effects
{
    namespace
    {
        // Kit Rae's trace of the green Civil War / Tall Font Russian (V7B/C),
        // with Tonepad's Green Big Muff schematic agreeing on these values.
        constexpr double feedbackOhms = 470.0e3;        // R9 / R17 / R15, collector to base
        constexpr double feedbackFarads = 500.0e-12;    // two 1 nF in series across each
        constexpr double openLoopGain = 12.0e3 / 390.0; // collector / emitter resistors
        constexpr double collectorSwing = 4.0;          // volts either way of the bias (9 V supply)

        // Stage 1: R2 39k and C1 0.1 uF in, R14 100k base to ground.
        constexpr double inputOhms = 39.0e3, inputFarads = 0.1e-6, inputBaseOhms = 100.0e3;
        // Clipping stages: 10k and 0.1 uF in, 100k base to ground, 0.047 uF in series with the diodes.
        constexpr double clipInputOhms = 10.0e3, clipInputFarads = 0.1e-6, clipBaseOhms = 100.0e3, diodeSeriesFarads = 0.047e-6;
        // SUSTAIN: a 100k pot with 1k under it.
        constexpr double sustainPotOhms = 100.0e3, sustainBottomOhms = 1.0e3;

        // Tone stack: C9 3.9 nF into R5 22k (treble), R8 20k into C8 10 nF
        // (bass), the 100k TONE pot between them, C3 0.1 uF from its wiper
        // into the output stage (R7 470k and R3 100k: about 82k).
        constexpr double toneTrebleFarads = 3.9e-9, toneTrebleOhms = 22.0e3, toneBassOhms = 20.0e3, toneBassFarads = 10.0e-9;
        constexpr double tonePotOhms = 100.0e3, toneOutFarads = 0.1e-6, outputStageInOhms = 82.0e3;
        // Output stage: 10k collector, 2.7k emitter, inverting.
        constexpr double outputGain = -10.0e3 / 2.7e3;

        // Silicon diodes (1N914 class).
        constexpr double diodeSaturation = 2.52e-9, diodeThermal = 1.752 * 25.85e-3;

        double transistorSwing (double volts) noexcept { return opAmpClip (volts, collectorSwing); }
    }

    void Cosmonaut::Stage::setup (double rate, double sourceOhms, double couplingFarads, double baseOhms,
                                  double seriesDiodeFarads, bool hasDiodes) noexcept
    {
        const auto period = 1.0 / rate;
        rs = sourceOhms;
        gCoupling = 2.0 * couplingFarads / period;
        rBase = baseOhms;
        gFeedbackCap = 2.0 * feedbackFarads / period;
        gDiodeCap = 2.0 * seriesDiodeFarads / period;
        diodes = hasDiodes;
        reset();
    }

    void Cosmonaut::Stage::reset() noexcept
    {
        jCoupling = jFeedbackCap = jDiodeCap = vBase = iDiode = 0.0;
    }

    double Cosmonaut::Stage::step (double sourceVolts, double extraSourceOhms) noexcept
    {
        // Every capacitor is its trapezoidal-rule equivalent: a conductance g
        // and a history current j. The coupling capacitor in series with the
        // input resistor folds into one source with a larger resistance.
        const auto sourceR = rs + extraSourceOhms + 1.0 / gCoupling;
        const auto source = sourceVolts - jCoupling / gCoupling;
        const auto across = 1.0 / feedbackOhms + gFeedbackCap;   // feedback R and C, collector to base
        const auto loop = openLoopGain + 1.0;                      // collector - base = -(A + 1) x base

        auto vb = vBase, id = diodes ? iDiode : 0.0;
        for (int iteration = 0; iteration < 16; ++iteration)
        {
            // Kirchhoff at the base: the input, the feedback R and C, the diodes and the base resistor.
            const auto u = -loop * vb;
            const auto f1 = (source - vb) / sourceR + u * across - jFeedbackCap + id - vb / rBase;
            const auto d11 = -1.0 / sourceR - loop * across - 1.0 / rBase;
            if (! diodes)
            {
                vb -= f1 / d11;   // linear: one step is exact
                break;
            }

            // The diodes, in series with their capacitor, carry the same current.
            const auto vd = std::clamp (u - (id + jDiodeCap) / gDiodeCap, -1.5, 1.5);
            const auto current = 2.0 * diodeSaturation * std::sinh (vd / diodeThermal);
            const auto slope = 2.0 * diodeSaturation / diodeThermal * std::cosh (vd / diodeThermal);
            const auto f2 = id - current;
            const auto d21 = slope * loop, d22 = 1.0 + slope / gDiodeCap;
            const auto determinant = d11 * d22 - d21;
            auto dvb = (-f1 * d22 + f2) / determinant;
            const auto did = (-f2 * d11 + f1 * d21) / determinant;
            dvb = std::clamp (dvb, -0.05, 0.05);   // small steps: the diodes' curve is steep
            vb += dvb;
            id += did;
            if (std::abs (dvb) < 1.0e-9 && std::abs (f2) < 1.0e-9)
                break;
        }

        if (! std::isfinite (vb) || ! std::isfinite (id))
        {
            reset();
            return 0.0;
        }

        // Move the capacitors' histories on.
        const auto u = -loop * vb;
        const auto inputCurrent = (source - vb) / sourceR;
        jCoupling = 2.0 * (inputCurrent + jCoupling) - jCoupling;   // g v + i, with v = (i + j) / g
        jFeedbackCap = 2.0 * gFeedbackCap * u - jFeedbackCap;
        if (diodes)
            jDiodeCap = 2.0 * (id + jDiodeCap) - jDiodeCap;
        vBase = vb;
        iDiode = id;
        return -openLoopGain * vb;
    }

    void Cosmonaut::prepare (double oversampledRate, double baseRate) noexcept
    {
        input.setup (oversampledRate, inputOhms, inputFarads, inputBaseOhms, 1.0e-6, false);
        clip1.setup (oversampledRate, clipInputOhms, clipInputFarads, clipBaseOhms, diodeSeriesFarads, true);
        clip2.setup (oversampledRate, clipInputOhms, clipInputFarads, clipBaseOhms, diodeSeriesFarads, true);

        toneStack = {};
        const auto in = toneStack.node(), treble = toneStack.node(), bass = toneStack.node(), wiper = toneStack.node(), out = toneStack.node();
        toneStack.capacitor (in, treble, toneTrebleFarads);
        toneStack.resistor (treble, 0, toneTrebleOhms);
        toneStack.resistor (in, bass, toneBassOhms);
        toneStack.capacitor (bass, 0, toneBassFarads);
        bassHalf = toneStack.resistor (bass, wiper, tonePotOhms * 0.5);
        trebleHalf = toneStack.resistor (wiper, treble, tonePotOhms * 0.5);
        toneStack.capacitor (wiper, out, toneOutFarads);
        toneStack.resistor (out, 0, outputStageInOhms);
        toneStack.setInput (in);
        toneStack.setOutput (out);
        toneStack.prepare (baseRate);
        tone = -1.0;
        setTone (0.5);
        setSustain (0.5);
    }

    void Cosmonaut::reset() noexcept
    {
        input.reset();
        clip1.reset();
        clip2.reset();
        toneStack.reset();
    }

    void Cosmonaut::setSustain (double sustain) noexcept
    {
        // The pot as a source: the share of the input stage it passes on,
        // and the resistance it puts in front of the first clipping stage.
        const auto s = std::clamp (sustain, 0.0, 1.0);
        const auto upper = (1.0 - s) * sustainPotOhms, lower = s * sustainPotOhms + sustainBottomOhms;
        sustainVolts = lower / (upper + lower);
        sustainOhms = upper * lower / (upper + lower);
    }

    double Cosmonaut::clip (double volts) noexcept
    {
        const auto a = transistorSwing (input.step (volts, 0.0));
        const auto b = transistorSwing (clip1.step (a * sustainVolts, sustainOhms));
        return transistorSwing (clip2.step (b, 0.0));
    }

    void Cosmonaut::setTone (double newTone) noexcept
    {
        const auto t = std::clamp (newTone, 0.0, 1.0);
        if (std::abs (t - tone) < 1.0e-3)
            return;
        tone = t;
        toneStack.setResistor (bassHalf, t * tonePotOhms);
        toneStack.setResistor (trebleHalf, (1.0 - t) * tonePotOhms);
        toneStack.update();
    }

    double Cosmonaut::finish (double volts) noexcept
    {
        return transistorSwing (outputGain * toneStack.process (volts));
    }
}
