#pragma once

#include "Parameters.h"
#include "dsp/InputTap.h"
#include "dsp/dominion/Dominion.h"
#include "dsp/effects/Compressor.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/MasterLimiter.h"
#include "dsp/effects/MultibandComp.h"
#include "dsp/effects/Reverb.h"
#include "dsp/eq/EqMatchSession.h"
#include "dsp/eq/ParametricEq.h"
#include "dsp/synth/SynthSettings.h"
#include "rigs/CompPresets.h"
#include "rigs/DominionPresets.h"

// THE RACK (stage 7a): PR7-E 0.18.1's four effects after the synth, in PR7-E's order: ECHELON (the EQ), DOMINION,
// CONSTRICTOR (its bands, then the full-band compressor), [OUTPUT], the LIMITER. Kept together on the heap: the
// effects' filter banks and buffers are far bigger than a thread's stack can hold a copy of (Windows: 1 MB), and
// the tests build the processor on the stack.
struct Rack
{
    eq::ParametricEq parametricEq;
    struct EqBandParameters { std::atomic<float>* values[std::size (ParamIDs::eqBandSettings)] {}; };
    std::array<EqBandParameters, ParamIDs::eqNumBands> eqBandParameters {};
    std::atomic<int> eqSoloBand { -1 };
    InputTap eqInputTap { 16384 }, eqOutputTap { 16384 };   // what goes into and comes out of the EQ, for its analyser
    eq::match::Session eqMatch { {} };                       // EQ Match's listening (its folder is set with the presets folder)

    dominion::Processor dominionProcessor;
    struct DominionBandParameters { std::atomic<float>* values[std::size (ParamIDs::domBandSettings)] {}; };
    std::array<DominionBandParameters, ParamIDs::domNumBands> dominionBandParameters {};
    std::atomic<int> dominionListen { -1 };
    DominionPresets dominionPresets { {} };
    DominionPreset dominionPreset = DominionPresets::factory().front();   // "Default"

    CompPresets compPresets { {} };
    CompPreset compPreset = CompPresets::factory().front();
    effects::Compressor compressor;
    effects::multiband::Processor multiband;   // CONSTRICTOR's bands, before the full band
    struct MultibandBandParameters { std::atomic<float>* values[std::size (ParamIDs::mbBandSettings)] {}; };
    std::array<MultibandBandParameters, ParamIDs::mbNumBands> multibandBandParameters {};
    std::atomic<int> multibandSolo { -1 };
    juce::AudioBuffer<float> compSidechain;   // what it listens to: the audio itself

    effects::MasterLimiter limiter;   // with its OUTPUT TRIM and DITHER
    double sampleRate = 48000.0;

    // THE SENDS (stage 7b): PR7-G's DELAY and REVERB, each fed its own mix from the engine (each oscillator at its
    // SEND amount), returning only their echoes / reverb into the mix before ECHELON.
    effects::Delay delay;
    effects::Reverb reverb;
    std::array<juce::AudioBuffer<float>, synth::numSends> sends;   // 2 channels each, the prepared block size

    void setFolder (const juce::File& presetsFolder)
    {
        eqMatch.setReferencesFolder (presetsFolder.getChildFile ("Match References"));
        dominionPresets.setFolder (presetsFolder.getChildFile ("Dominion Presets"));
        compPresets.setFolder (presetsFolder.getChildFile ("Comp Presets"));
    }
};
