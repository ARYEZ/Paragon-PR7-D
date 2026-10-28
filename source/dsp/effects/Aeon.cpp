#include "Aeon.h"

namespace effects
{
    namespace
    {
        constexpr int chunkSize = 32;   // RES is re-applied this often while it glides

        constexpr double k = 1.0e3, n = 1.0e-9, p = 1.0e-12, u = 1.0e-6;

        // The uPC4558s on 9 V stop about 0.7 V short of each rail of the
        // 4.5 V bias (typical datasheet figures).
        constexpr double opAmpRail = 3.8;

        // IC2b's limiter: R12 (47k) across it, and R11 (4k7) in series with
        // a pair of silicon diodes (D3, D4; 1S2473, a 1N4148 type: typical
        // datasheet figures).
        constexpr double limiterR12 = 47 * k, limiterR11 = 4.7 * k;
        constexpr double diodeIs = 2.52e-9, diodeNVt = 1.752 * 0.02585;

        // The sweep circuit (sheet values) and our estimates where the sheet
        // gives none (marked).
        constexpr double controlSupply = 5.0;        // IC6, the 78L05: the control side runs on 5 V (our reading of the sheet)
        constexpr double manualPot = 50 * k, manualR42 = 68 * k, manualR43 = 150 * k, manualR44 = 470 * k;
        constexpr double depthPot = 50 * k, depthR48 = 220 * k;
        constexpr double baseEmitter = 0.6;
        constexpr double lfoCentre = 2.5;            // R40 / R41 halve the 5 V
        constexpr double lfoSwing = 1.25;            // estimate: IC5 (TL022) swings about 1.5 V either side of its 2.5 V
                                                     // reference, and R36 / R37 (180k / 220k) set the triangle at 0.82 of that
        constexpr double c32 = 22 * u, r47 = 4.7 * k;
        constexpr double q6Resistance = 2.7 * k;     // estimate: Q6's own resistance at the current VR7 (CL) sets, mid-trim

        // RES: VR4 (50k, C taper) loaded by R10 (39k) and VR5, a 22k trim
        // the factory sets so full RES rings without howling. By the sheet's
        // values the loop would pass x1 (and howl) around 11 kHz, where the
        // anti-alias filter's peak sits, even with VR5 at its end, so the
        // delay path must lose a little the sheet doesn't show: the BBD's
        // gain varies from chip to chip (MN3207: +-4 dB) and its output
        // stage, the followers and the FET switch each lose some. Our
        // estimate: 0.85 (-1.4 dB) through the delay path, with VR5 at its
        // end, which peaks the loop at 0.95 at full RES.
        constexpr double resTrack = 50 * k, resR10 = 39 * k, resVR5 = 22 * k;
        constexpr double delayPathGain = 0.85;

        double knob (float value) noexcept { return juce::jlimit (0.0, 1.0, (double) value / 10.0); }

        // Pot tapers, as fractions of the track from pin 1.
        double audioTaper (double x) noexcept { return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0; }
        double reverseAudioTaper (double x) noexcept { return 1.0 - audioTaper (1.0 - x); }

        // The exponential converter's input: the current through R48 into
        // Q7's base, times its resistance (R48 plus DEPTH's own), as volts.
        double converterVolts (double controlVolts, double depthKnob) noexcept
        {
            const auto depthResistance = depthKnob * (1.0 - depthKnob) * depthPot;
            return (controlVolts - baseEmitter) * depthR48 / (depthR48 + depthResistance);
        }

        // Volts per doubling of the clock, set so MANUAL's travel (DEPTH at
        // 0) covers the pedal's specified 13 ms to 1 ms: a 13 to 1 range.
        double clockScale() noexcept
        {
            static const double scale = std::log (Aeon::longestDelayMs / Aeon::shortestDelayMs)
                                      / (converterVolts (Aeon::manualVolts (10.0f), 0.0) - converterVolts (Aeon::manualVolts (0.0f), 0.0));
            return scale;
        }

        double delayMsForConverter (double volts) noexcept
        {
            static const double longestVolts = converterVolts (Aeon::manualVolts (0.0f), 0.0);   // (PR7-K: worked out once)
            const auto fromLongest = volts - longestVolts;
            return juce::jlimit (0.7, 16.0, Aeon::longestDelayMs * std::exp (-clockScale() * fromLongest));
        }

        // A 4-point Hermite read from a circular buffer, `delay` samples
        // before `write` (the slot the next sample goes into).
        float readDelayed (const std::vector<float>& buffer, int write, double delay) noexcept
        {
            const auto size = (int) buffer.size();
            const auto position = (double) write - delay;
            const auto whole = (int) std::floor (position);
            const auto t = (float) (position - whole);
            // (PR7-K: wrapped without dividing; the same slots.)
            auto first = (whole - 1) % size;
            if (first < 0)
                first += size;
            const auto at = [&] (int i) { auto index = first + i + 1; if (index >= size) index -= size; return buffer[(size_t) index]; };
            const auto y0 = at (-1), y1 = at (0), y2 = at (1), y3 = at (2);
            const auto c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        }
    }

    double Aeon::manualVolts (float manualKnob) noexcept
    {
        // VR1 (50k linear) across the 5 V, its wiper through R42 into Q8's
        // base, which R43 and R44 also bias; Q8's emitter follows, 0.6 V down.
        const auto position = knob (manualKnob);
        const auto wiperResistance = position * (1.0 - position) * manualPot;
        const auto g42 = 1.0 / (manualR42 + wiperResistance), g43 = 1.0 / manualR43, g44 = 1.0 / manualR44;
        const auto base = (position * controlSupply * g42 + controlSupply * g43) / (g42 + g43 + g44);
        return base - baseEmitter;
    }

    double Aeon::lfoVolts (double triangle) noexcept { return lfoCentre + lfoSwing * triangle; }

    double Aeon::delayMsForControl (double controlVolts, double depthKnob) noexcept
    {
        return delayMsForConverter (converterVolts (controlVolts, depthKnob));
    }

    Aeon::Aeon()
        : oversampling (1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true)   // 2x: the circuit's filters reach 12 kHz
    {
        // Parts from the BF-2 schematic (its part numbers). The transistor
        // followers (Q1-Q3) are written as ideal followers; each op-amp
        // output is low impedance, so stages are separate circuits.

        // Q1 (input buffer), then IC1a: pre-emphasis. Inverting, x1 in the
        // lows, rising to about x5.7 (+15 dB) above 2.3 kHz.
        {
            auto& c = inputStage;
            const auto in = c.node(), a = c.node(), q = c.node(), e = c.node(), b = c.node(), r = c.node(), minus = c.node(), out = c.node();
            c.resistor (in, a, 1 * k);  c.capacitor (a, q, 47 * n);  c.resistor (q, 0, 470 * k);    // R1, C1, R2
            c.opAmp (q, e, e);                                                                       // Q1
            c.capacitor (e, b, 1 * u);                                                               // C2
            c.resistor (b, minus, 47 * k);                                                           // R4
            c.capacitor (b, r, 6.8 * n);  c.resistor (r, minus, 10 * k);                             // C3, R5
            c.resistor (minus, out, 47 * k);  c.capacitor (minus, out, 100 * p);                     // R6, C4
            c.opAmp (0, minus, out);
            c.setInput (in);
            c.setOutput (out);
        }

        // IC2b's inputs, as the current into its summing node (a 1k
        // "meter" across an ideal op-amp reads it, as -1 V per mA).
        // The dry signal: R9 (82k), with R8 (220k) and C5 (220 pF) across it, then C6.
        {
            auto& c = mixerDry;
            const auto in = c.node(), j = c.node(), b = c.node(), minus = c.node(), out = c.node();
            c.resistor (in, j, 82 * k);                                  // R9
            c.resistor (in, b, 220 * k);  c.capacitor (b, j, 220 * p);   // R8, C5
            c.capacitor (j, minus, 47 * n);                              // C6
            c.resistor (minus, out, 1 * k);
            c.opAmp (0, minus, out);
            c.setInput (in);
            c.setOutput (out);
        }
        // RES: the wet signal through C21 into VR4, its wiper through C7,
        // VR5 and R10.
        {
            auto& c = mixerFeedback;
            const auto in = c.node(), top = c.node(), w = c.node(), r = c.node(), minus = c.node(), out = c.node();
            c.capacitor (in, top, 47 * n);                                                   // C21
            resPot = { c.resistor (top, w, resTrack / 2), c.resistor (w, 0, resTrack / 2) };     // VR4 (RES)
            c.capacitor (w, r, 47 * n);                                                      // C7
            c.resistor (r, minus, resR10 + resVR5);                                          // R10, VR5
            c.resistor (minus, out, 1 * k);
            c.opAmp (0, minus, out);
            c.setInput (in);
            c.setOutput (out);
        }

        // Q2: the BBD's anti-alias filter (R14, R15, C8, C9), a Sallen-Key
        // low-pass peaking at 12 kHz: with the output filters' droop, the
        // whole wet path is flat to about 12 kHz, then falls steeply.
        {
            auto& c = antiAlias;
            const auto in = c.node(), a = c.node(), b = c.node(), out = c.node();
            c.resistor (in, a, 10 * k);  c.resistor (a, b, 10 * k);       // R14, R15
            c.capacitor (a, out, 12 * n);  c.capacitor (b, 0, 150 * p);   // C8, C9
            c.opAmp (b, out, out);                                       // Q2
            c.setInput (in);
            c.setOutput (out);
        }

        // After the BBD: C13 and R19, Q3's three-pole low-pass (R20-R22,
        // C14-C16), then C17 into IC2a, a two-pole low-pass (inverting).
        {
            auto& c = outputFilters;
            const auto in = c.node(), a = c.node(), na = c.node(), nb = c.node(), base = c.node(), q3 = c.node(),
                       d = c.node(), m = c.node(), minus = c.node(), out = c.node();
            c.capacitor (in, a, 33 * n);  c.resistor (a, 0, 330 * k);                                  // C13, R19
            c.resistor (a, na, 10 * k);  c.capacitor (na, 0, 3.9 * n);                                 // R20, C14
            c.resistor (na, nb, 10 * k);  c.capacitor (nb, q3, 8.2 * n);                               // R21, C15
            c.resistor (nb, base, 10 * k);  c.capacitor (base, 0, 330 * p);                            // R22, C16
            c.opAmp (base, q3, q3);                                                                     // Q3
            c.capacitor (q3, d, 220 * n);  c.resistor (d, m, 47 * k);  c.capacitor (m, 0, 1 * n);      // C17, R24, C18
            c.resistor (m, out, 47 * k);  c.resistor (m, minus, 22 * k);  c.capacitor (minus, out, 150 * p);   // R26, R25, C19
            c.opAmp (0, minus, out);
            c.setInput (in);
            c.setOutput (out);
        }

        // IC1b: the output mixer, with the de-emphasis (C23, R30) across it:
        // dry through R28 (47k), wet through C20 and R29 (27k). Q4, the
        // effect switch, is a closed JFET while the effect is on.
        const auto addDeEmphasis = [] (Circuit& c, int minus, int out)
        {
            const auto r = c.node();
            c.resistor (minus, out, 47 * k);  c.capacitor (minus, out, 100 * p);    // R31, C22
            c.capacitor (minus, r, 6.8 * n);  c.resistor (r, out, 10 * k);          // C23, R30
            c.opAmp (0, minus, out);
        };
        {
            auto& c = outputDry;
            const auto in = c.node(), minus = c.node(), out = c.node();
            c.resistor (in, minus, 47 * k);   // R28
            addDeEmphasis (c, minus, out);
            c.setInput (in);
            c.setOutput (out);
        }
        {
            auto& c = outputWet;
            const auto in = c.node(), f = c.node(), minus = c.node(), out = c.node();
            c.capacitor (in, f, 2.2 * u);  c.resistor (f, minus, 27 * k);   // C20, R29
            addDeEmphasis (c, minus, out);
            c.setInput (in);
            c.setOutput (out);
        }
    }

    void Aeon::prepare (double hostSampleRate, int maxBlockSize)
    {
        oversampling.initProcessing ((size_t) maxBlockSize);
        sampleRate = hostSampleRate * 2.0;
        mono.setSize (1, maxBlockSize);

        for (auto* c : { &inputStage, &mixerDry, &mixerFeedback, &antiAlias, &outputFilters, &outputDry, &outputWet })
            c->prepare (sampleRate);

        bbd.assign ((size_t) std::ceil (0.020 * sampleRate) + 8, 0.0f);
        manual.reset (sampleRate, 0.05);
        depth.reset (sampleRate, 0.05);
        appliedResonance = -1.0f;
        reset();
    }

    void Aeon::reset() noexcept
    {
        oversampling.reset();
        for (auto* c : { &inputStage, &mixerDry, &mixerFeedback, &antiAlias, &outputFilters, &outputDry, &outputWet })
            c->reset();
        std::fill (bbd.begin(), bbd.end(), 0.0f);
        bbdWrite = 0;
        lfoPhase = 0.0;
        clockControl = std::numeric_limits<double>::quiet_NaN();   // starts at the knobs' setting
        limiterVolts = 0.0;
    }

    void Aeon::updateResonance (float resonance) noexcept
    {
        // VR4's wiper from its grounded end, a C (reverse audio) taper: RES
        // rises quickly at first, then finely towards full.
        const auto position = reverseAudioTaper (knob (resonance));
        mixerFeedback.setResistor (resPot[0], resTrack * (1.0 - position));
        mixerFeedback.setResistor (resPot[1], resTrack * position);
        mixerFeedback.update();
        appliedResonance = resonance;
    }

    void Aeon::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto numSamples = block.getNumSamples();
        const auto numChannels = block.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return;

        // A mono pedal: the channels in, summed; the same out of each.
        auto* monoSamples = mono.getWritePointer (0);
        for (size_t i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (size_t ch = 0; ch < numChannels; ++ch)
                sum += block.getSample ((int) ch, (int) i);
            monoSamples[i] = sum / (float) numChannels;
        }

        // Just reset: start at the knobs' settings rather than gliding to them.
        if (std::isnan (clockControl))
        {
            manual.setCurrentAndTargetValue (settings.manual);
            depth.setCurrentAndTargetValue (settings.depth);
        }
        manual.setTargetValue (settings.manual);
        depth.setTargetValue (settings.depth);
        if (! juce::exactlyEqual (settings.resonance, appliedResonance))
            updateResonance (settings.resonance);

        const auto rateHz = (double) juce::jlimit (slowestRateHz, fastestRateHz, settings.rateHz);

        juce::dsp::AudioBlock<float> monoBlock (mono.getArrayOfWritePointers(), 1, numSamples);
        auto up = oversampling.processSamplesUp (monoBlock);
        auto* samples = up.getChannelPointer (0);

        // C32's time constant: it charges through R47 and Q6 in parallel
        // with R48 (the DEPTH pot's own resistance is small beside 220k).
        const auto c32Coefficient = 1.0 - std::exp (-1.0 / (c32 * (1.0 / (1.0 / (r47 + q6Resistance) + 1.0 / depthR48)) * sampleRate));

        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            // The LFO: IC5's triangle. DEPTH's wiper picks between MANUAL's
            // voltage (one end) and the triangle (the other): at full DEPTH
            // MANUAL does nothing, as the pedal's manual says.
            lfoPhase += rateHz / sampleRate;
            lfoPhase -= std::floor (lfoPhase);
            const auto triangle = 4.0 * std::abs (lfoPhase - 0.5) - 1.0;
            const auto d = (double) depth.getNextValue();
            const auto manualNow = manualVolts (manual.getNextValue());
            const auto control = converterVolts (manualNow + d * (lfoVolts (triangle) - manualNow), d);

            if (std::isnan (clockControl))
                clockControl = control;
            clockControl += c32Coefficient * (control - clockControl);

            const auto delaySamples = delayMsForConverter (clockControl) * 0.001 * sampleRate;

            // The wet signal: the BBD's output now, through the output filters.
            const auto wet = opAmpClip (outputFilters.process (delayPathGain * readDelayed (bbd, bbdWrite, delaySamples)), opAmpRail);

            // The dry signal after the pre-emphasis.
            const auto dry = opAmpClip (inputStage.process (samples[i] * voltsPerUnit), opAmpRail);

            // IC2b: the dry signal and RES's feedback, as currents into its
            // summing node; its output is whatever voltage drives that
            // current back through R12 and the R11 + diode branch. Solved
            // for the diodes' voltage (Newton's method, from last sample's).
            const auto current = -(mixerDry.process (dry) + mixerFeedback.process (wet)) / (1 * k);
            // (PR7-K: sinh and cosh from one exp, as they were most of AEON's cost.)
            auto vd = limiterVolts;
            for (int iteration = 0; iteration < 4; ++iteration)
            {
                const auto e = std::exp (juce::jlimit (-40.0, 40.0, vd / diodeNVt)), inverse = 1.0 / e;
                const auto id = diodeIs * (e - inverse);                    // 2 Is sinh (x)
                const auto slope = diodeIs * (e + inverse) / diodeNVt;      // 2 Is cosh (x) / nVt
                const auto error = (vd + limiterR11 * id) / limiterR12 + id - current;
                vd -= error / ((1.0 + limiterR11 * slope) / limiterR12 + slope);
            }
            limiterVolts = vd;
            const auto e = std::exp (juce::jlimit (-40.0, 40.0, vd / diodeNVt));
            const auto limited = -(vd + limiterR11 * diodeIs * (e - 1.0 / e));

            // Into the BBD through the anti-alias filter.
            bbd[(size_t) bbdWrite] = (float) antiAlias.process (opAmpClip (limited, opAmpRail));
            bbdWrite = (bbdWrite + 1) % (int) bbd.size();

            // IC1b: dry and wet, de-emphasised.
            const auto out = opAmpClip (outputDry.process (dry) + outputWet.process (wet), opAmpRail);
            samples[i] = (float) (out / voltsPerUnit);
        }

        oversampling.processSamplesDown (monoBlock);

        for (size_t ch = 0; ch < numChannels; ++ch)
            block.getSingleChannelBlock (ch).copyFrom (monoBlock);
    }
}
