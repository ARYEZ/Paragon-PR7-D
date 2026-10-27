#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>

#include "LevelMeterSource.h"
#include "Parameters.h"
#include "dsp/synth/Engine.h"
#include "dsp/synth/Freeform.h"
#include "dsp/synth/Spectrum.h"
#include "dsp/synth/Wavetable.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"

// Which preset is loaded, for the header and the Save button. (Presets are
// "rigs" in the code, as in the rest of the PR7 family, whose rig browser,
// files and undo this shares.)
struct CurrentRig
{
    juce::String name { "Init" };
    juce::File file;              // set for the user's own presets
    juce::String factoryName;     // set for factory presets
    bool isUserRig() const { return file != juce::File(); }

    bool operator== (const CurrentRig& other) const
    {
        return name == other.name && file == other.file && factoryName == other.factoryName;
    }
};

// PR7-K: an instrument. MIDI in (from the DAW and the on-screen keyboard),
// the synth engine, the preset's level and OUTPUT, stereo out.
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
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // The DAW's own bypass button drives our BYPASS parameter, so the two can't disagree.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Presets live in the preset browser, so the host's program list is a single entry.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Presets (message thread only)
    RigLibrary& getRigLibrary() noexcept { return *rigLibrary; }

    // Points the library at another folder. For tests.
    void useRigFolder (const juce::File& folder);

    // The current sound as a preset (not saved anywhere yet).
    Rig captureRig() const;

    void loadRig (const RigLibrary::Entry& entry);

    // Saves over the current preset if it's one of the user's own and returns
    // true; returns false if it isn't (a factory preset or never saved), in
    // which case the caller should ask for a name and use saveCurrentRigAs.
    bool saveCurrentRig();
    bool saveCurrentRigAs (const juce::String& name);

    // Loads the next (+1) or previous (-1) preset in the library.
    void stepRig (int direction);

    // Rename, re-file or re-tag a user preset from the browser.
    bool updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails);
    bool deleteRig (const RigLibrary::Entry& entry);

    CurrentRig getCurrentRig() const { return currentRig; }
    const RigLibrary::Entry* findCurrentRigEntry() const;

    // True once anything in the preset has changed since it was loaded or saved.
    bool isRigModified() const noexcept { return rigModified.load(); }

    // Tells listeners (the editor) that the preset list or current preset changed.
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
    // The synth

    // The sound as set now (any thread): what the engine plays.
    synth::Settings getSynthSettings() const noexcept;

    // The on-screen keyboard's keys, played into the synth with the DAW's MIDI.
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    // Notes sounding (including releasing ones), as of the last block.
    int getActiveVoices() const noexcept { return activeVoices.load(); }

    //==========================================================================
    // WAVETABLES (message thread)

    // The table oscillator `osc` plays when its WAVE is WAVETABLE: one loaded
    // from a file, or the factory table its TABLE parameter names.
    std::shared_ptr<const synth::Wavetable> getWavetable (int osc);
    bool hasLoadedWavetable (int osc) const { return loadedTables[(size_t) osc] != nullptr; }

    // Loads a WAV (or AIFF, FLAC...) file into oscillator `osc` and switches
    // its WAVE to WAVETABLE, cut into frames as `options` say (the default:
    // as the file says, or as it's laid out). False if the file couldn't be read as audio.
    bool loadWavetableFile (int osc, const juce::File&, const synth::wavetableFiles::CutOptions& options = {});
    // Chooses a factory table instead (forgetting a loaded one), and WAVETABLE.
    void chooseFactoryWavetable (int osc, int index);

    //==========================================================================
    // The FFT editor (message thread)

    const synth::SpectrumEdit& getSpectrum (int osc) const { return spectra[(size_t) juce::jlimit (0, ParamIDs::numOscillators - 1, osc)]; }
    // A new curve for oscillator `osc`: its wave is rebuilt in the background.
    void setSpectrum (int osc, const synth::SpectrumEdit&);
    // A drawing stroke finished: worth an undo step.
    void spectrumEditFinished() { requestCheckpoint(); }
    // The edited copy oscillator `osc` plays (null: not edited, or not built yet).
    std::shared_ptr<const synth::Wavetable> getEditedTable (int osc) const { return spectralTables[(size_t) juce::jlimit (0, ParamIDs::numOscillators - 1, osc)]; }
    // Oscillator `osc`'s wave before the editor's curve (for the editor's picture).
    synth::WavetableSource getPlainSource (int osc);
    // Waits for the background builds and puts them in place. For tests.
    void finishTableBuilds();

    //==========================================================================
    // FREEFORM (message thread): each oscillator's drawing, played when its WAVE is FREEFORM.
    const synth::FreeformDrawing& getDrawing (int osc) const { return drawings[(size_t) juce::jlimit (0, ParamIDs::numOscillators - 1, osc)]; }
    void setDrawing (int osc, const synth::FreeformDrawing&);
    void drawingEditFinished() { requestCheckpoint(); }

    // The DAW's tempo, as last seen by the audio thread (120 if it hasn't said).
    double getHostBpm() const noexcept { return hostBpm.load(); }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int start, int numSamples);
    float param (const char* id) const;
    bool isOn (const char* id) const { return param (id) > 0.5f; }

    void timerCallback() override;
    void parameterChanged (const juce::String& parameterId, float newValue) override;
    void markModified() noexcept;
    void applyRig (const Rig& rig);
    // Points each oscillator at the table it should play, building factory
    // tables as they're first wanted, and lets go of replaced ones once the
    // audio thread can no longer be using them.
    void updateWavetables();
    // A table for a loaded source: an existing one if it's the same (undo, or
    // the same preset again), else built.
    std::shared_ptr<const synth::Wavetable> tableFor (const synth::WavetableSource&);

    // Undo snapshots: the sound and which preset it is.
    struct Snapshot
    {
        Rig rig;
        CurrentRig current;
        bool modified = false;   // restored too, but doesn't count as a change

        bool operator== (const Snapshot& other) const
        {
            return rig.parameters == other.rig.parameters && rig.wavetables == other.rig.wavetables && rig.spectra == other.rig.spectra
                && rig.drawings == other.rig.drawings && current == other.current;
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

    juce::AudioProcessorValueTreeState parameters;

    // Each oscillator's parameters, in ParamIDs::oscSettings' order, and
    // everything else by ID: read once per block into synth::Settings.
    struct OscParameters { std::atomic<float>* values[std::size (ParamIDs::oscSettings)] {}; };
    std::array<OscParameters, ParamIDs::numOscillators> oscParameters {};

    // WAVETABLES: tables loaded from files (null: the factory's), what each
    // oscillator plays now, the same for the audio thread (a plain pointer it
    // can read without waiting), and replaced tables kept a little while so a
    // block still playing one never reads freed memory.
    std::array<std::shared_ptr<const synth::Wavetable>, ParamIDs::numOscillators> loadedTables, playingTables;
    std::array<std::atomic<const synth::Wavetable*>, ParamIDs::numOscillators> tablesForAudio {};
    std::array<int, ParamIDs::numOscillators> playingFactoryTable {};   // which factory table each plays (-1: none, or a loaded one)
    std::vector<std::pair<std::shared_ptr<const synth::Wavetable>, double>> retiredTables;

    // FFT: each oscillator's curve, and the edited copies of its wave built
    // from it (in the background; then handed to the audio thread as the
    // wavetables are). A key says what a copy was built from.
    struct SpectralKey
    {
        synth::SpectrumEdit edit;
        float sweep = 0.0f, vStrength = 0.0f, pulseWidth = 0.5f;
        bool flip = false;
        int wave = 0;
        const void* samples = nullptr;   // a wavetable's frames (null: a basic wave)
        bool operator== (const SpectralKey& o) const noexcept
        {
            return edit == o.edit && juce::exactlyEqual (sweep, o.sweep) && juce::exactlyEqual (vStrength, o.vStrength)
                && juce::exactlyEqual (pulseWidth, o.pulseWidth) && flip == o.flip && wave == o.wave && samples == o.samples;
        }
    };
    class SpectralBuilder;
    std::unique_ptr<SpectralBuilder> spectralBuilder;
    std::array<synth::SpectrumEdit, ParamIDs::numOscillators> spectra;
    // FREEFORM: the drawings, and the tables built from them.
    std::array<synth::FreeformDrawing, ParamIDs::numOscillators> drawings;
    std::array<std::shared_ptr<const synth::Wavetable>, ParamIDs::numOscillators> freeformTables;
    void buildFreeform (int osc);
    std::array<std::optional<SpectralKey>, ParamIDs::numOscillators> spectralKeys;   // what's built or being built (none: unedited)
    std::array<std::shared_ptr<const synth::Wavetable>, ParamIDs::numOscillators> spectralTables, spectralPulseTables;
    std::array<std::atomic<const synth::Wavetable*>, ParamIDs::numOscillators> spectralForAudio {}, spectralPulseForAudio {};
    void updateSpectra();
    void installSpectralResults();

    synth::Engine engine;
    juce::MidiKeyboardState keyboardState;
    juce::MidiBuffer keyboardMidi;   // the DAW's MIDI plus the on-screen keyboard's, per block
    std::atomic<int> activeVoices { 0 };

    // Smoothing stops zipper noise when OUTPUT or the preset's level moves.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
    juce::SmoothedValue<float> bypassAmount;   // 1: silent

    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    // Atomic because the DAW's automation changes parameters on the audio thread.
    std::atomic<bool> rigModified { false }, applyingRig { false };

    UndoHistory<Snapshot> history;
    std::atomic<int> activeGestures { 0 };
    std::atomic<bool> checkpointWanted { true };

    int preparedBlockSize = 512;
    std::atomic<double> hostBpm { 120.0 };

    LevelMeterSource outputMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
