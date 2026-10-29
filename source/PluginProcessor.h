#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <optional>

#include "LevelMeterSource.h"
#include "Parameters.h"
#include "Rack.h"
#include "dsp/synth/Engine.h"
#include "dsp/synth/Freeform.h"
#include "dsp/synth/Spectrum.h"
#include "dsp/synth/Wavetable.h"
#include "rigs/RigLibrary.h"
#include "rigs/UndoHistory.h"
#include "rigs/MidiLinks.h"

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

    //==========================================================================
    // The MOD MATRIX (message thread; stage 4a): routes from sources to knobs,
    // kept with the preset. Changing it rebuilds what the audio thread plays.
    const ModMatrix& getModMatrix() const noexcept { return matrix; }
    void setModMatrix (const ModMatrix&);
    void modMatrixEditFinished() { requestCheckpoint(); }
    // The MOD page's on-screen mod wheel (0 to 1): sent to the synth as MIDI controller 1 in the next block.
    void setScreenModWheel (float value) noexcept { pendingModWheel = juce::jlimit (0.0f, 1.0f, value); }

    // MIDI LEARN (stage 4f): controllers linked to knobs. A knob asks to learn;
    // the next controller moved is linked to it (from the timer).
    const MidiLinks& getMidiLinks() const noexcept { return midiLinks; }
    void setMidiLinks (const MidiLinks&);
    void startMidiLearn (const juce::String& parameterId);
    void cancelMidiLearn() { learning = {}; }
    void finishMidiLearn();   // from the timer (and tests): links the knob learning to a controller moved since
    const juce::String& getMidiLearnTarget() const noexcept { return learning; }
    int getLastController() const noexcept { return lastController.load(); }          // -1: none yet
    float getLastControllerValue() const noexcept { return lastControllerValue.load(); }
    juce::File midiLinksDefaultFile() const;
    bool saveMidiLinksAsDefault() const;
    bool exportMidiLinks (const juce::File&) const;
    // MIDI EXPORT (stage 5b): what ARP `arp` plays over `keys` (empty: the keys held on the keyboard, else C4),
    // one pass of its pattern (CHAIN: each with blocks; AUTODICE: four cycles), at the song's tempo.
    juce::MidiFile arpToMidi (int arp, juce::Array<int> keys = {}) const;
    bool exportArpMidi (int arp, const juce::File&) const;
    juce::File arpMidiDragFile (int arp) const;   // a file to drag onto a track
    bool importMidiLinks (const juce::File&);
    void controllerForTest (int controller, float value);   // a controller, played through a short block
    // Tests: the synth's random choices (start phases...) the same on every run, for sample-for-sample comparisons.
    void seedRandomForTest (juce::int64 seed) noexcept
    {
        engine.seedRandom (seed);
        rack->loFi.seedRandom (seed);
        drumKit.seedRandom ((std::uint32_t) seed);
    }

    //==========================================================================
    // THE DRUMS (stage 6a). While DRUMS is on, the 12 keys from its ROOT play the kit instead of the synth.
    // Samples (message thread): a slot's loaded file, kept in the preset; loading one sets the slot to SAMPLE.
    bool loadDrumSample (int slot, const juce::File&);
    void setDrumSample (int slot, std::shared_ptr<const synth::drums::Sample>);
    void clearDrumSample (int slot);
    std::shared_ptr<const synth::drums::Sample> getDrumSample (int slot) const;
    // The kit's settings now (any thread; the samples as the audio thread plays them).
    synth::drums::KitSettings getKitSettings() const noexcept;
    // The page's preview: a hit in the next block (whether DRUMS is on or not).
    void previewDrum (int slot) noexcept;
    const synth::drums::Kit& getDrumKit() const noexcept { return drumKit; }

    // THE DRUM SEQUENCER (stage 6b). Its settings now (without the patterns, which the audio thread takes from the
    // compiled matrix). Reversed synth drums play a rendered copy of their hit, made here when a reversed note needs
    // one or the drum's sound changes (the timer; tests call it).
    synth::drumseq::Settings getDrumSeqSettings() const noexcept;
    void updateDrumReverses();   // and the drum synths' hit lengths for their envelopes (6c)
    // KITS AND SEQUENCES (6c part 2): files in the presets folder's "Drum Kits" (.pr7kit: every drum's settings,
    // samples and envelope, the kit's LEVEL and VELOCITY) and "Drum Sequences" (.pr7seq: the four patterns and the
    // sequencer's SPEED, TIMING, LENGTH, VEL AMOUNT). With LINK on, loading or saving one takes the other of the same
    // name too. Loading is one undo step.
    juce::File drumKitsFolder() const;
    juce::File drumSequencesFolder() const;
    std::unique_ptr<juce::XmlElement> captureDrumKit() const;
    std::unique_ptr<juce::XmlElement> captureDrumSequence() const;
    bool applyDrumKit (const juce::XmlElement&);
    bool applyDrumSequence (const juce::XmlElement&);
    bool saveDrumKit (const juce::String& name);
    bool saveDrumSequence (const juce::String& name);
    bool loadDrumKit (const juce::File&);
    bool loadDrumSequence (const juce::File&);
    juce::Array<juce::File> listDrumKits() const;
    juce::Array<juce::File> listDrumSequences() const;
    std::atomic<bool> drumLink { true };
    juce::String getDrumKitName() const { return drumKitName; }
    juce::String getDrumSequenceName() const { return drumSequenceName; }
    // < > (6c part 2): the next or previous audio file in the slot's sample's folder (by name), loaded into the slot.
    bool loadNeighbourSample (int slot, int direction);

    // MIDI EXPORT: one pass of a pattern on the kit's keys, at the song's tempo; click saves, drag makes a file.
    juce::MidiFile drumPatternToMidi (int pattern) const;
    bool exportDrumMidi (int pattern, const juce::File&) const;
    juce::File drumMidiDragFile (int pattern) const;
    // How many routes the audio thread is playing (on, with a source and a known destination). For tests and the display.
    int getNumPlayingRoutes() const noexcept;

    // The DAW's tempo, as last seen by the audio thread (120 if it hasn't said).
    double getHostBpm() const noexcept { return hostBpm.load(); }

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    LevelMeterSource& getOutputMeter() noexcept { return outputMeter; }

    //==========================================================================
    // THE RACK (stage 7a): PR7-E 0.18.1's four effects after the synth, with PR7-E's names for what its pages read.
    // ECHELON (the EQ): its settings as they are now (any thread), the audio going into and out of it (for the
    // analyser), and SOLO, which isn't a parameter: it's held while listening.
    eq::ParametricEq::Settings getEqSettings() const noexcept;
    const InputTap& getEqInputTap() const noexcept { return rack->eqInputTap; }
    const InputTap& getEqOutputTap() const noexcept { return rack->eqOutputTap; }
    void setEqSolo (int band) noexcept { rack->eqSoloBand = band; }
    int getEqSolo() const noexcept { return rack->eqSoloBand.load(); }
    double getEqAutoGainDb() const noexcept { return rack->parametricEq.getAutoGainDb(); }
    eq::ParametricEq::DynamicReading getEqDynamics (int band) const noexcept { return rack->parametricEq.getDynamics (band); }
    eq::match::Session& getEqMatch() noexcept { return rack->eqMatch; }

    // DOMINION: its settings, what it's cutting (for its page), and listening to one of its depth-curve bands (-1: none).
    dominion::Settings getDominionSettings() const noexcept;
    const dominion::Processor& getDominion() const noexcept { return rack->dominionProcessor; }
    void setDominionListen (int band) noexcept { rack->dominionListen = band; }
    int getDominionListen() const noexcept { return rack->dominionListen.load(); }
    // Its own presets (message thread): the one loaded last (and whether it's been changed since), loading one, and yours.
    DominionPresets& getDominionPresets() noexcept { return rack->dominionPresets; }
    void loadDominionPreset (const DominionPreset& preset) { DominionPresets::apply (preset, parameters); rack->dominionPreset = preset; }
    juce::String getDominionPresetName() const { return rack->dominionPreset.name; }
    bool isDominionPresetModified() { return ! DominionPresets::matches (rack->dominionPreset, parameters); }
    void rememberDominionPreset (const DominionPreset& preset) { rack->dominionPreset = preset; }   // after saving it

    // CONSTRICTOR's presets, as DOMINION's.
    CompPresets& getCompPresets() noexcept { return rack->compPresets; }
    void loadCompPreset (const CompPreset& preset) { CompPresets::apply (preset, parameters); rack->compPreset = preset; }
    juce::String getCompPresetName() const { return rack->compPreset.name; }
    bool isCompPresetModified() { return ! CompPresets::matches (rack->compPreset, parameters); }
    void rememberCompPreset (const CompPreset& preset) { rack->compPreset = preset; }

    // CONSTRICTOR's bands as set now (any thread), and each band now (the page's meters).
    effects::multiband::Settings getMultibandSettings() const noexcept;
    effects::multiband::Processor::Reading getMultibandReading (int band) const noexcept { return rack->multiband.getReading (band); }
    void setMultibandSolo (int band) noexcept { rack->multibandSolo = band; }
    // The delay CONSTRICTOR's LOOK-AHEAD and the LIMITER add, told to the DAW (the timer calls this; tests too).
    void updateLatency();

    // CONSTRICTOR's and the LIMITER's gain reduction on a channel in the last block (dB, 0 or more).
    float getCompressorReductionDb (int channel) const noexcept { return rack->compressor.getReductionDb (channel); }
    float getLimiterReductionDb (int channel) const noexcept { return rack->limiter.getReductionDb (channel); }
    effects::MasterLimiter& getLimiter() noexcept { return rack->limiter; }
    effects::MasterLimiter::Settings getLimiterSettings() const;

    // THE SENDS (stage 7b): DELAY's and REVERB's settings as they are now (as sends' returns: MIX is the level back).
    effects::Delay::Settings getDelaySettings() const noexcept;
    effects::Reverb::Settings getReverbSettings() const noexcept;
    // MOD (0) or MOD 2's (1) settings as they are now (SYNC worked out at the song's tempo), and LO-FI's (stage 7c).
    effects::Modulation::Settings getModSettings (int which) const noexcept;
    effects::LoFi::Settings getLoFiSettings() const noexcept;
    // An insert bus's (0 to 3: A to D) effects' settings as they are now (stage 7d), its sends, and its output's peak.
    effects::Drive::Settings getInsertDriveSettings (int bus) const noexcept;
    effects::Glitch::Settings getInsertGlitchSettings (int bus) const noexcept;
    effects::Modulation::Settings getInsertModSettings (int bus) const noexcept { return modSettingsFor (ParamIDs::insModIds (bus)); }
    effects::LoFi::Settings getInsertLoFiSettings (int bus) const noexcept;
    std::array<float, synth::numSends> getInsertSends (int bus) const noexcept;
    float getInsertPeakDb (int bus) const noexcept
    {
        const auto peak = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].peak.load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }
    float getInsertModSweepPhase (int bus) const noexcept { return rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].modulation.getSweepPhase(); }
    // Where MOD's (0) or MOD 2's (1) sweep is now, 0 to 1, and LO-FI's output peak in the last block (dB, -100 while off).
    float getModSweepPhase (int which) const noexcept { return rack->modulation[(size_t) juce::jlimit (0, 1, which)].getSweepPhase(); }
    float getLoFiPeakDb() const noexcept
    {
        const auto peak = rack->loFiPeak.load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }
    // Each return's peak in the last block, dB (-100: silent): DELAY 0, REVERB 1.
    float getReturnPeakDb (int send) const noexcept
    {
        const auto peak = rack->returnPeak[(size_t) juce::jlimit (0, synth::numSends - 1, send)].load();
        return peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const juce::MidiBuffer& drums,
                       const synth::drums::KitSettings& kit, int start, int numSamples);
    void processModFx (juce::dsp::AudioBlock<float> block);
    void processInsertBuses (juce::dsp::AudioBlock<float> block, bool sendsReady);
    effects::Modulation::Settings modSettingsFor (const ParamIDs::ModIds&) const noexcept;
    effects::LoFi::Settings loFiSettingsFor (int bus) const noexcept;   // -1: the whole sound's
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
                && rig.drumSamples == other.rig.drumSamples
                && rig.drawings == other.rig.drawings && rig.modulation == other.rig.modulation && current == other.current;
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
    struct ShaperParameters { std::atomic<float>* values[std::size (ParamIDs::shaperSettings)] {}; };
    std::array<ShaperParameters, ParamIDs::numShapers> shaperParameters {};
    struct LfoParameters { std::atomic<float>* values[std::size (ParamIDs::lfoSettings)] {}; };
    std::array<LfoParameters, ParamIDs::numLfos> lfoParameters {};
    struct EnvelopeParameters { std::atomic<float>* values[std::size (ParamIDs::envelopeSettings)] {}; };
    std::array<EnvelopeParameters, ParamIDs::numEnvelopes> envelopeParameters {};
    struct ArpParameters { std::atomic<float>* values[std::size (ParamIDs::arpSettings)] {}; };
    std::array<ArpParameters, ParamIDs::numArps> arpParameters {};
    struct DrumParameters { std::atomic<float>* values[std::size (ParamIDs::drumSettings)] {}; };
    std::array<DrumParameters, ParamIDs::numDrums> drumParameters {};

    // THE DRUMS: the kit, the samples (the audio thread gets plain pointers; replaced ones are kept a while, as the
    // wavetables are), the page's preview hits, and the block's MIDI split between the synth and the kit.
    synth::drums::Kit drumKit;
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> drumSamples;
    std::array<std::atomic<const synth::drums::Sample*>, ParamIDs::numDrums> samplesForAudio {};
    std::vector<std::pair<std::shared_ptr<const synth::drums::Sample>, double>> retiredSamples;
    std::array<std::atomic<bool>, ParamIDs::numDrums> pendingPreview {};
    juce::MidiBuffer synthMidi, drumMidi;
    // Reversed synth drums' rendered hits (by slot), what each was made from, and the audio thread's pointers.
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> reverseHits;
    std::array<juce::String, ParamIDs::numDrums> reverseKeys;
    std::array<std::atomic<const synth::drums::Sample*>, ParamIDs::numDrums> reversesForAudio {};
    // A drum synth's hit length, which its drawn envelope spans (6c); what each was measured from.
    std::array<std::atomic<float>, ParamIDs::numDrums> hitLengths {};
    std::array<juce::String, ParamIDs::numDrums> hitLengthKeys;
    juce::String drumKitName, drumSequenceName;   // the last loaded or saved (shown on the page)
    bool saveDrumSequenceOnly (const juce::String& cleanName);

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

    std::unique_ptr<Rack> rack = std::make_unique<Rack>();   // made first: the rig folder sets its folders
    std::unique_ptr<RigLibrary> rigLibrary;
    CurrentRig currentRig;
    // Atomic because the DAW's automation changes parameters on the audio thread.
    std::atomic<bool> rigModified { false }, applyingRig { false };

    UndoHistory<Snapshot> history;
    std::atomic<int> activeGestures { 0 };
    std::atomic<bool> checkpointWanted { true };

    int preparedBlockSize = 512;
    std::atomic<double> hostBpm { 120.0 };
    double hostPpq = -1.0;   // the song position at this block's start (-1: stopped); audio thread
    std::atomic<float> pendingModWheel { -1.0f };   // -1: nothing new
    MidiLinks midiLinks;
    std::array<std::atomic<juce::RangedAudioParameter*>, 128> controllerTargets {};   // the audio thread's copy, by CC
    std::atomic<int> lastController { -1 }, controllerCount { 0 };
    std::atomic<float> lastControllerValue { 0.0f };
    juce::String learning;
    int learnFromCount = 0;

    // The MOD MATRIX, and the routes the audio thread plays (rebuilt from it
    // on the message thread, swapped in by pointer, the old kept 2 s).
    ModMatrix matrix;
    struct CompiledRoute
    {
        synth::mod::Source source;
        synth::mod::Target target;
        juce::RangedAudioParameter* parameter;
        float amount, lagMs, curve;
        bool percent;
    };
    struct CompiledMatrix
    {
        std::vector<CompiledRoute> routes;
        std::array<synth::mod::Shape, synth::mod::numEnvelopes> envelopes {};
        std::array<synth::mod::Shape, synth::mod::numLfoShapes> lfoShapes {};
        synth::mod::Shape pitchEnvelope {};
        synth::seq::Pattern stepPattern {};
        std::array<synth::mod::Shape, synth::numOscillators> travelShapes {};
        std::array<synth::travel::Pattern, synth::numOscillators> travelPatterns {};
        std::array<synth::arp::Patterns, synth::arp::numArps> arpPatterns {};
        std::array<synth::arp::Randomizer, synth::arp::numArps> arpRandomizers {};
        synth::drumseq::Patterns drumPatterns {};
        std::array<synth::mod::Shape, synth::drums::numSlots> drumEnvelopes {};
        std::uint32_t usedSources = 0;
    };
    std::shared_ptr<const CompiledMatrix> compiledMatrix;
    std::atomic<const CompiledMatrix*> matrixForAudio { nullptr };
    std::vector<std::pair<std::shared_ptr<const CompiledMatrix>, double>> retiredMatrices;
    std::array<synth::mod::Route, synth::mod::maxRoutes> routeBuffer {};   // audio thread
    void compileMatrix();

    LevelMeterSource outputMeter;
    void processRack (juce::dsp::AudioBlock<float> block);   // the rack's effects before OUTPUT
    void processSends (juce::dsp::AudioBlock<float> block);  // DELAY and REVERB on their sends, their returns added in


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParagonX1AudioProcessor)
};
