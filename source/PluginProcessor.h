#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LevelMeterSource.h"
#include "dsp/InputTap.h"
#include "dsp/TapTempo.h"
#include "dsp/effects/CompGate.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/FilterPitch.h"
#include "dsp/effects/GraphicEq.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/PitchBlock.h"
#include "dsp/effects/TalkBox.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/Presence.h"
#include "dsp/effects/Reverb.h"
#include "dsp/effects/Wah.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"

// Which rig is loaded, for the header display and the Save button.
struct CurrentRig
{
    juce::String name { "Untitled" };
    juce::File file;              // set for the user's own rigs
    juce::String factoryName;     // set for factory rigs
    bool isUserRig() const { return file != juce::File(); }

    bool operator== (const CurrentRig& other) const
    {
        return name == other.name && file == other.file && factoryName == other.factoryName;
    }
};

class ParagonX1AudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AudioProcessorParameter::Listener
{
public:
    ParagonX1AudioProcessor();
    ~ParagonX1AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // MIDI CC 11 moves the expression pedal
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The DAW's own bypass button drives our BYPASS parameter, so the two
    // can't disagree, and bypassing keeps the plugin's latency.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Presets live in the rig browser (Phase 4), so the host's program list is a single entry.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Rigs (message thread only)
    RigLibrary& getRigLibrary() noexcept { return *rigLibrary; }

    // Points the library at another folder. For tests.
    void useRigFolder (const juce::File& folder);

    // The current sound as a rig (not saved anywhere yet).
    Rig captureRig() const;

    void loadRig (const RigLibrary::Entry& entry);

    // Saves over the current rig if it's one of the user's own and returns
    // true; returns false if it isn't (a factory rig or never saved), in
    // which case the caller should ask for a name and use saveCurrentRigAs.
    bool saveCurrentRig();
    bool saveCurrentRigAs (const juce::String& name);

    // Loads the next (+1) or previous (-1) rig in the library.
    void stepRig (int direction);

    // Rename, re-file or re-tag a user rig from the browser.
    bool updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails);
    bool deleteRig (const RigLibrary::Entry& entry);

    CurrentRig getCurrentRig() const { return currentRig; }
    const RigLibrary::Entry* findCurrentRigEntry() const;

    // True once anything in the rig has changed since it was loaded or saved.
    bool isRigModified() const noexcept { return rigModified.load(); }

    // Tells listeners (the editor) that the rig list or current rig changed.
    juce::ChangeBroadcaster rigBroadcaster;

    //==========================================================================
    // UNDO / REDO (message thread only)
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();

    // Records a finished edit now instead of on the next timer tick. For tests.
    void flushUndoStep() { recordCheckpointIfReady(); }

    //==========================================================================
    // Tempo

    // The DAW's tempo, as last seen by the audio thread (120 if it hasn't said).
    double getHostBpm() const noexcept { return hostBpm.load(); }

    // TAP: each call is one tap (message thread). Once tapped, the tapped
    // tempo is used instead of the DAW's until clearTappedTempo().
    void tapTempo();
    void clearTappedTempo();
    double getTappedBpm() const noexcept { return tappedBpm.load(); }   // 0 when following the DAW
    double getTempoBpm() const noexcept;                                 // whichever is in use

    //==========================================================================
    // Tuner: the guitar signal, for the editor's tuner to measure.
    const InputTap& getInputTap() const noexcept { return inputTap; }

    // The expression pedal's position after MIDI (0 heel to 1 toe), for the UI.
    float getExpressionPosition() const noexcept;

    //==========================================================================
    // SETTINGS page. Saved with the project.

    // Which MIDI controller moves the expression pedal (11 = "expression").
    int getExpressionCc() const noexcept { return expressionCc.load(); }
    void setExpressionCc (int cc) noexcept { expressionCc = juce::jlimit (1, 127, cc); }

    // The tuner's reference pitch for A4 (440 Hz unless the band tunes otherwise).
    double getTunerReferenceHz() const noexcept { return tunerReferenceHz.load(); }
    void setTunerReferenceHz (double hz) noexcept { tunerReferenceHz = juce::jlimit (420.0, 460.0, hz); }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getInputMeter() noexcept { return inputMeter; }
    // How hard the compressor is working (dB, 0 or less), for its meter.
    float getCompGainReductionDb() const noexcept { return compGate.getGainReductionDb(); }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples);
    void readHostTempo();
    float param (const char* id) const;
    bool isOn (const char* id) const { return param (id) > 0.5f; }

    void timerCallback() override;
    void parameterChanged (const juce::String& parameterId, float newValue) override;
    void markModified() noexcept;
    void applyRig (const Rig& rig);

    // Undo snapshots: the sound and which rig it is.
    struct Snapshot
    {
        Rig rig;
        CurrentRig current;
        bool modified = false;   // restored too, but doesn't count as a change

        bool operator== (const Snapshot& other) const
        {
            return rig.parameters == other.rig.parameters && current == other.current;
        }
    };
    Snapshot takeSnapshot() const;
    void restoreSnapshot (const Snapshot&);
    void requestCheckpoint() noexcept { checkpointWanted = true; }
    void recordCheckpointIfReady();

    // Knob drags and button clicks start and end "gestures"; an edit is
    // finished (and worth an undo step) when its gesture ends.
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;

    void readMidiExpression (const juce::MidiBuffer&) noexcept;
    void syncMidiExpressionToParameter();
    float expressionAmount() const noexcept;   // position mapped through MIN/MAX

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;

    // Smoothing stops zipper noise when a gain knob moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain, outputGain;

    // Signal chain, in order: pitch -> wah -> compGate -> filter / pitch -> presence
    // -> talk box -> graphic EQ -> modulation -> delay -> reverb -> lo-fi (glitch pre
    // or post). Up to the EQ it's mono; from modulation on it's stereo when the track is.
    effects::CompGate compGate;
    effects::FilterPitch filterPitch;   // after the compressor
    effects::Presence presence;
    effects::Modulation modulation;
    effects::Delay delay;
    effects::Reverb reverb;
    effects::LoFi loFi;   // last: after the reverb
    effects::Glitch glitch;
    effects::PitchBlock pitchBlock;
    effects::TalkBox talkBox;   // FILTER / PITCH's Talk Box type, after the cab   // first: TUNING, OCTAVER and FREEZE, before the wah   // after the EQ (POST), or after FILTER / PITCH (PRE)
    effects::Wah wah;
    effects::GraphicEq graphicEq;   // after presence, before modulation
    std::array<std::atomic<float>*, effects::GraphicEq::numBands> eqGainDb {};   // the expression pedal's WAH, first in the chain as on a pedalboard

    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    // Atomic because the DAW's automation changes parameters on the audio thread.
    std::atomic<bool> rigModified { false }, applyingRig { false };

    // BYPASS: the clean input, delayed by our latency so switching bypass
    // never shifts the guitar in time, crossfaded in over 20 ms.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypassDelay { 8192 };
    juce::AudioBuffer<float> bypassDry;
    juce::SmoothedValue<float> bypassAmount;

    // The expression pedal's VOLUME, and the tuner's mute.
    juce::SmoothedValue<float> expressionVolume, tunerOpen;

    // The last MIDI expression value not yet copied into the expression parameter (-1 = none).
    std::atomic<float> pendingMidiExpression { -1.0f };
    std::atomic<int> expressionCc { 11 };
    std::atomic<double> tunerReferenceHz { 440.0 };

    InputTap inputTap { 32768 };   // enough for the tuner's window at 192 kHz

    UndoHistory<Snapshot> history;
    std::atomic<int> activeGestures { 0 };
    std::atomic<bool> checkpointWanted { true };

    int preparedBlockSize = 512;
    std::atomic<double> hostBpm { 120.0 }, tappedBpm { 0.0 };
    double songBeatsAtBlock = -1.0;   // the song position at this block's start, in beats; -1 when not playing (audio thread only)
    TapTempo tapCounter;

    LevelMeterSource inputMeter, outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
