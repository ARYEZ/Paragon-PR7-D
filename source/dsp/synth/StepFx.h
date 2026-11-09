#pragma once

#include "dsp/synth/DrumSequencer.h"

#include <cstdint>
#include <vector>

// THE STEP FX LANE (0.73.0): beat effects on the whole drum mix, switched on and off by the pattern's FX steps in
// time with the sequencer. Our own design, after what electronic beat-effect units do; no latency (everything works
// on the sound already heard, kept in a 16-second recording).
//
// What each does over its stretch of steps (t = time since the stretch began; RATE = a slice in note values):
// - REPEAT: the first slice plays as it is, then loops (a stutter); AMOUNT is each repeat's level against the one
//   before (100%: all alike; lower: they die away).
// - CHOP: a gate opening and closing each RATE (open the first half); AMOUNT how far it closes (100%: silence).
// - TAPE STOP: the sound slows like a tape machine switched off, down over the stretch; AMOUNT how far (100%: to a
//   stop, fading out as it gets there; 50%: to half speed, an octave down).
// - REVERSE: each slice plays the slice before it backwards (the first one, the slice just before the stretch).
// - FILTER: a low-pass sweeping down over the stretch, from fully open; AMOUNT how far (100%: to 150 Hz).
// - BITCRUSH: fewer bits and a lower sample rate; AMOUNT how much (100%: 4 bits, every 16th sample held).
// Every jump in the sound (an effect starting or ending, a repeat starting over) is smoothed over 3 ms.
namespace synth::stepfx
{
    class Processor
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;

        // `left` / `right` (null: mono) in place. `startSteps`: the sequencer's step at the first sample, counted from
        // its start (< 0: not playing: the sound passes through); `stepsPerSample`, `beatsPerStep`; the pattern playing
        // (null: none) and its length in steps.
        void process (float* left, float* right, int numSamples, double startSteps, double stepsPerSample, double beatsPerStep,
                      const drumseq::Pattern* pattern, double lengthSteps) noexcept;

        bool isPlayingEffect() const noexcept { return activeKey >= 0; }
        drumseq::FxType getPlayingType() const noexcept { return activeKey >= 0 ? fx.type : drumseq::FxType::none; }
        int getPlayingIndex() const noexcept { return activeKey >= 0 ? (int) (activeKey % drumseq::maxFxSteps) : -1; }

    private:
        float read (int channel, double position) const noexcept;   // the recording at an absolute sample position

        double sampleRate = 48000.0;
        std::vector<float> ring[2];
        int64_t ringSize = 0;
        int64_t written = 0;              // samples recorded so far (the next one's absolute position)

        int64_t activeKey = -1;           // the effect playing: its pass x maxFxSteps + its index (-1: none)
        drumseq::FxStep fx;
        double origin = 0.0;              // the absolute sample position of the stretch's start
        double tapeRead = 0.0;            // TAPE STOP: how far it has read since the start, in samples
        int64_t lastSlice = -1;
        float gate = 1.0f;                // CHOP's gain, smoothed
        float lowState[2] {}, bandState[2] {};   // FILTER's state-variable filter
        float held[2] {};
        int holdLeft = 0;                 // BITCRUSH: samples left to hold
        float offset[2] {}, previous[2] {}, beforePrevious[2] {};       // the click smoothing
        float offsetDecay = 0.99f, gateCoefficient = 0.02f;
    };
}
