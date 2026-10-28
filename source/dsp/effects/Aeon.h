#pragma once

#include "Circuit.h"
#include "EffectUtils.h"

namespace effects
{
    // AEON: a flanger modelled on the Boss BF-2 (1980s), built from its
    // schematic with the real component values. Our model of that circuit,
    // not Boss's design files:
    //
    //   x -> buffer -> pre-emphasis (IC1a) -+--------------------------------------------+
    //                                       |                                            | dry
    //                                       +-> mixer + RES (IC2b, diode limiter)        |
    //                                             -> anti-alias filter (Q2)              |
    //                                             -> BBD, 1024 stages (1 to 13 ms)       |
    //                                             -> output filters (Q3, IC2a) -+-> wet -+-> de-emphasis mixer (IC1b) -> out
    //                                                  ^                        |
    //                                                  +-------- RES -----------+
    //
    // The delay is a bucket-brigade chip (MN3207) clocked by a voltage-
    // controlled oscillator; its delay is 512 / clock frequency. A triangle
    // LFO (IC5) and MANUAL set that clock through an exponential converter
    // (Q6, Q7), so each volt multiplies the clock by the same amount and the
    // notches sweep evenly by ear. A 22 uF capacitor (C32) smooths the
    // control voltage, so at fast RATE settings the sweep gets narrower.
    // Dry and wet are mixed 50/50 and in phase, which makes the notches
    // deep; RES feeds the wet signal back in phase, for the ringing peaks.
    // The pre- and de-emphasis around the BBD (a treble lift before it, the
    // matching cut after) was Boss's noise reduction; it cancels out except
    // where the limiter catches loud, bright notes.
    class Aeon
    {
    public:
        struct Settings
        {
            float manual = 5.0f, depth = 0.5f, rateHz = 0.5f, resonance = 5.0f;   // MANUAL and RES 0 to 10, DEPTH 0 to 1
        };

        // Volts at the pedal's input for a full-scale sample: 1 V, a strong pickup or line signal.
        static constexpr double voltsPerUnit = 1.0;

        // The pedal's specified delay range (manual: 1 ms to 13 ms) and LFO
        // speeds (one cycle every 100 ms to every 16 s).
        static constexpr double shortestDelayMs = 1.0, longestDelayMs = 13.0;
        static constexpr float slowestRateHz = 1.0f / 16.0f, fastestRateHz = 10.0f;

        Aeon();
        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;
        void process (juce::dsp::AudioBlock<float> block, const Settings&) noexcept;
        double getLfoPhase() const noexcept { return lfoPhase; }   // 0 to 1 (PR7-K: for the MOD page's display)

        // The delay, in ms, for the voltage the sweep circuit sends the
        // clock (DEPTH's wiper), before C32's smoothing. For tests.
        static double delayMsForControl (double controlVolts, double depthKnob) noexcept;
        // MANUAL's voltage (Q8's emitter), knob 0 to 10.
        static double manualVolts (float manual) noexcept;
        // The LFO's triangle, -1 to 1, as volts (IC5's output).
        static double lfoVolts (double triangle) noexcept;

    private:
        void processSample (double& sample) noexcept;
        void updateResonance (float resonance) noexcept;

        double sampleRate = 96000.0;   // the oversampled rate
        juce::dsp::Oversampling<float> oversampling;
        juce::AudioBuffer<float> mono;

        // Linear stages, as circuits. The two summing amps are one circuit
        // per input (the op-amp holds the summing node at 0 V, so the inputs
        // add): IC2b's are written as the current they push into its
        // summing node, as IC2b's diode limiter needs it.
        Circuit inputStage, mixerDry, mixerFeedback, antiAlias, outputFilters, outputDry, outputWet;
        std::array<int, 2> resPot {};

        std::vector<float> bbd;   // the bucket-brigade delay, as a circular buffer
        int bbdWrite = 0;

        double lfoPhase = 0.0;        // 0 to 1
        double clockControl = 0.0;    // the exponential converter's input after C32, in volts
        double limiterVolts = 0.0;    // IC2b's diodes: last solution, Newton's starting point
        juce::SmoothedValue<float> manual, depth;
        float appliedResonance = -1.0f;
    };
}
