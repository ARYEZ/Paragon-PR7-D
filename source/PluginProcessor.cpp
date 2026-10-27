#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

namespace
{
    constexpr double gainRampSeconds = 0.02;

    // Extra settings kept in the saved state (DAW project).
    const juce::Identifier rigNameKey { "rigName" };
    const juce::Identifier rigFileKey { "rigFile" };
    const juce::Identifier rigFactoryKey { "rigFactory" };
    const juce::Identifier rigModifiedKey { "rigModified" };
    // Which version's parameters a project was saved with, as for presets (see Rig.cpp).
    const juce::Identifier parameterVersionKey { "parameterVersion" };
    // Wavetables loaded from files, kept in the project as presets keep them.
    const juce::Identifier wavetablesKey { "Wavetables" };
    const juce::Identifier wavetableKey { "Wavetable" };
    constexpr double retiredTableMs = 2000.0;   // how long a replaced table is kept, in case a block is still playing it
    constexpr int parameterVersion = 1;
}

ParagonX1AudioProcessor::ParagonX1AudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ParagonX1", createParameterLayout())
{
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        for (size_t s = 0; s < std::size (ParamIDs::oscSettings); ++s)
        {
            oscParameters[(size_t) i].values[s] = parameters.getRawParameterValue (ParamIDs::osc (i, ParamIDs::oscSettings[s]));
            jassert (oscParameters[(size_t) i].values[s] != nullptr);
        }

    playingFactoryTable.fill (-1);
    for (auto& table : tablesForAudio)
        table = nullptr;

    rigLibrary = std::make_unique<RigLibrary>();
    // A new PR7-K is the Init preset (every parameter at its default), so the
    // header names it and < > step on from there.
    currentRig = { "Init", {}, "Init" };

    // Any change to a parameter that presets store marks the preset as
    // modified, and finishing an edit to one records an undo step.
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.addParameterListener (withId->paramID, this);
                parameter->addListener (this);
            }

    // Housekeeping off the audio thread: record undo steps.
    startTimerHz (30);

    // The starting point, so the first edit can be undone.
    history.record (takeSnapshot());
    checkpointWanted = false;
}

ParagonX1AudioProcessor::~ParagonX1AudioProcessor()
{
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            parameters.removeParameterListener (withId->paramID, this);
            parameter->removeListener (this);
        }

    stopTimer();
}

void ParagonX1AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    engine.prepare (sampleRate);
    if (juce::MessageManager::existsAndIsCurrentThread())
        updateWavetables();
    keyboardMidi.ensureSize (2048);
    keyboardState.reset();

    outputGain.reset (sampleRate, gainRampSeconds);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    bypassAmount.reset (sampleRate, gainRampSeconds);
    bypassAmount.setCurrentAndTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
}

juce::AudioProcessorParameter* ParagonX1AudioProcessor::getBypassParameter() const
{
    return parameters.getParameter (ParamIDs::bypass);
}

float ParagonX1AudioProcessor::param (const char* id) const
{
    // Looked up by ID each block. APVTS keeps these in a sorted map, so the
    // lookup is a handful of string compares and never allocates.
    return parameters.getRawParameterValue (id)->load();
}

synth::Settings ParagonX1AudioProcessor::getSynthSettings() const noexcept
{
    using namespace ParamIDs;
    synth::Settings s;
    const auto choice = [] (float value, int count) { return juce::jlimit (0, count - 1, juce::roundToInt (value)); };

    for (size_t i = 0; i < s.oscillators.size(); ++i)
    {
        const auto& v = oscParameters[i].values;   // in oscSettings' order
        const auto get = [&v] (int index) { return v[(size_t) index]->load(); };
        using ParamIDs::oscSetting;
        auto& o = s.oscillators[i];
        o.on = get (oscSetting ("On")) > 0.5f;
        o.wave = (synth::OscWave) choice (get (oscSetting ("Wave")), synth::oscWaveNames.size());
        o.volume = get (oscSetting ("Volume"));
        o.pan = get (oscSetting ("Pan"));
        o.transpose = get (oscSetting ("Transpose"));
        o.fineCents = get (oscSetting ("Fine"));
        o.randomPitch = get (oscSetting ("Random"));
        o.randomPhase = get (oscSetting ("PhaseRand")) > 0.5f;
        o.phase = get (oscSetting ("Phase"));
        o.gain = get (oscSetting ("Gain"));
        o.pulseWidth = get (oscSetting ("Width"));
        o.fadeInMs = get (oscSetting ("FadeIn"));
        o.invert = get (oscSetting ("Invert")) > 0.5f;
        o.noise = get (oscSetting ("Noise"));
        o.noiseColour = get (oscSetting ("NoiseColour"));
        o.noiseWidth = (synth::NoiseWidth) choice (get (oscSetting ("NoiseWidth")), synth::noiseWidthNames.size());
        o.sub = get (oscSetting ("Sub"));
        o.subWave = (synth::SubWave) choice (get (oscSetting ("SubWave")), synth::subWaveNames.size());
        o.subOctave = choice (get (oscSetting ("SubOct")), synth::subOctaveNames.size());
        o.subInvert = get (oscSetting ("SubInvert")) > 0.5f;
        o.toFilter = get (oscSetting ("Filter")) > 0.5f;
        o.voiceLimit = juce::jlimit (1, synth::maxVoices, juce::roundToInt (get (oscSetting ("Voices"))));

        o.table = tablesForAudio[i].load();
        o.tableIndex = get (oscSetting ("WtIndex"));
        o.tableSteppy = choice (get (oscSetting ("WtMode")), synth::wavetableModeNames.size()) == 1;
        o.tableFrames = juce::jlimit (2, synth::maxWavetableFrames, juce::roundToInt (get (oscSetting ("WtFrames"))));

        o.skew = get (oscSetting ("Skew"));
        o.pinch = get (oscSetting ("Pinch"));
        o.formant = get (oscSetting ("Formant"));
        o.formantMode = (synth::FormantMode) choice (get (oscSetting ("FormantMode")), synth::formantModeNames.size());
        o.crush = get (oscSetting ("Crush"));
        o.crushMode = (synth::CrushMode) choice (get (oscSetting ("CrushMode")), synth::crushModeNames.size());

        o.unisonVoices = juce::jlimit (2, synth::maxUnison, juce::roundToInt (get (oscSetting ("UniVoices"))));
        o.unisonMix = get (oscSetting ("UniMix"));
        o.unisonDetune = get (oscSetting ("UniDetune"));
        o.unisonSpread = get (oscSetting ("UniSpread"));
        o.unisonOctaves = juce::jlimit (0, 3, juce::roundToInt (get (oscSetting ("UniOctaves"))));
        o.vibratoMode = (synth::VibratoMode) choice (get (oscSetting ("VibMode")), synth::vibratoModeNames.size());
        o.vibratoWave = (synth::VibratoWave) choice (get (oscSetting ("VibWave")), synth::vibratoWaveNames.size());
        o.vibratoRateHz = get (oscSetting ("VibRate"));
        o.vibratoCents = get (oscSetting ("VibAmount"));
        o.vibratoFadeMs = get (oscSetting ("VibFade"));
        o.vibratoCurve = get (oscSetting ("VibCurve"));
        constexpr int firstChord = oscSetting ("Ch1On");
        for (int k = 0; k < synth::numChordVoices; ++k)
        {
            auto& chord = o.chord[(size_t) k];
            const auto at = firstChord + k * 5;   // On, Coarse, Fine, Pan, Vol
            chord.on = get (at) > 0.5f;
            chord.coarse = get (at + 1);
            chord.fineCents = get (at + 2);
            chord.pan = get (at + 3);
            chord.volume = get (at + 4);
        }
    }
    static_assert (ParamIDs::oscSetting ("Ch4Vol") == ParamIDs::oscSetting ("Ch1On") + 19);


    const auto envelope = [this] (const char* a, const char* h, const char* d, const char* su, const char* r,
                                  const char* ac, const char* dc, const char* rc)
    {
        return synth::EnvelopeSettings { param (a), param (h), param (d), param (su), param (r), param (ac), param (dc), param (rc) };
    };

    auto& f = s.filter;
    f.on = isOn (fltOn);
    f.type = (synth::FilterType) choice (param (fltType), synth::filterTypeNames.size());
    f.cutoffHz = param (fltCutoff);
    f.resonance = param (fltRes);
    f.drive = param (fltDrive);
    f.keytrack = param (fltKey);
    f.velocity = param (fltVel);
    f.resonanceVelocity = param (fltResVel);
    f.envelopeAmount = param (fltEnv);
    f.envelopeVelocity = param (fltEnvVel);
    f.envelope = envelope (fltAttack, fltHold, fltDecay, fltSustain, fltRelease, fltAttackCurve, fltDecayCurve, fltReleaseCurve);

    auto& a = s.amp;
    a.volume = param (ampVolume);
    a.velocity = param (ampVel);
    a.pan = param (ampPan);
    a.panKeytrack = param (ampPanKey);
    a.spread = param (ampSpread);
    a.spike = param (ampSpike);
    a.spikeCurve = param (ampSpikeCurve);
    a.spikeVelocity = param (ampSpikeVel);
    a.envelope = envelope (ampAttack, ampHold, ampDecay, ampSustain, ampRelease, ampAttackCurve, ampDecayCurve, ampReleaseCurve);

    auto& p = s.play;
    p.mode = (synth::PlayMode) choice (param (playMode), synth::playModeNames.size());
    p.legato = isOn (legato);
    p.glideMs = param (glide);
    p.glideMode = (synth::GlideMode) choice (param (glideMode), synth::glideModeNames.size());
    p.bendUp = param (bendUp);
    p.bendDown = param (bendDown);
    p.voices = juce::jlimit (1, synth::maxVoices, juce::roundToInt (param (voices)));
    p.tuneCents = param (tune);
    return s;
}

bool ParagonX1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no audio in, mono or stereo out.
    const auto output = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void ParagonX1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;

    // The on-screen keyboard's notes join the DAW's.
    keyboardMidi.clear();
    keyboardMidi.addEvents (midi, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (keyboardMidi, 0, numSamples, true);

    // Some hosts occasionally send more samples than they promised in
    // prepareToPlay; work through an oversized block in promised-size pieces.
    for (int start = 0; start < numSamples; start += preparedBlockSize)
        processChunk (buffer, keyboardMidi, start, juce::jmin (preparedBlockSize, numSamples - start));

    activeVoices = engine.getNumActiveVoices();
    midi.clear();   // an instrument: nothing goes on down the MIDI chain
}

void ParagonX1AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int start, int numSamples)
{
    const auto numChannels = buffer.getNumChannels();
    auto* left = buffer.getWritePointer (0, start);
    // Mono out: render the right side into the scratch end of the buffer's
    // second channel if there is one, else fold it in afterwards.
    float scratchRight[512];
    for (int done = 0; done < numSamples; done += (int) std::size (scratchRight))
    {
        const auto n = juce::jmin ((int) std::size (scratchRight), numSamples - done);
        auto* right = numChannels > 1 ? buffer.getWritePointer (1, start + done) : scratchRight;
        const auto settings = getSynthSettings();
        engine.process (left + done, right, n, midi, start + done, settings);
        if (numChannels == 1)
            for (int i = 0; i < n; ++i)
                left[done + i] = 0.5f * (left[done + i] + scratchRight[i]);
    }

    // The preset's own level and OUTPUT, then BYPASS (to silence: there's
    // nothing "clean" to pass through on an instrument).
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    bypassAmount.setTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue() * (1.0f - bypassAmount.getNextValue());
        for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
            buffer.getWritePointer (channel, start)[i] *= gain;
    }
    for (int channel = 2; channel < numChannels; ++channel)
        buffer.clear (channel, start, numSamples);

    for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
        outputMeter.pushSamples (buffer.getReadPointer (channel, start), numSamples);
}

//==============================================================================
void ParagonX1AudioProcessor::timerCallback()
{
    updateWavetables();
    recordCheckpointIfReady();
}

//==============================================================================
void ParagonX1AudioProcessor::updateWavetables()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        auto wanted = loadedTables[(size_t) i];
        auto factory = -1;
        if (wanted == nullptr)
        {
            // The factory table TABLE names: built the first time an oscillator
            // plays it as a WAVETABLE, and kept while it's chosen.
            const auto index = juce::jlimit (0, synth::factoryTables::names.size() - 1,
                                             juce::roundToInt (param (ParamIDs::osc (i, "Table").toRawUTF8())));
            const auto isWavetable = juce::roundToInt (param (ParamIDs::osc (i, "Wave").toRawUTF8())) == (int) synth::OscWave::wavetable;
            if (isWavetable || playingFactoryTable[(size_t) i] == index)
            {
                wanted = isWavetable ? synth::factoryTables::get (index) : playingTables[(size_t) i];
                factory = index;
            }
        }
        if (wanted != playingTables[(size_t) i])
        {
            if (playingTables[(size_t) i] != nullptr)
                retiredTables.emplace_back (playingTables[(size_t) i], now);
            playingTables[(size_t) i] = wanted;
            tablesForAudio[(size_t) i] = wanted.get();
        }
        playingFactoryTable[(size_t) i] = factory;
    }
    std::erase_if (retiredTables, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
}

std::shared_ptr<const synth::Wavetable> ParagonX1AudioProcessor::tableFor (const synth::WavetableSource& source)
{
    for (const auto* list : { &loadedTables, &playingTables })
        for (const auto& table : *list)
            if (table != nullptr && table->getSource() == source)
                return table;
    for (const auto& [table, when] : retiredTables)
        if (table->getSource() == source)
            return table;
    return synth::Wavetable::build (source);
}

std::shared_ptr<const synth::Wavetable> ParagonX1AudioProcessor::getWavetable (int osc)
{
    osc = juce::jlimit (0, ParamIDs::numOscillators - 1, osc);
    if (loadedTables[(size_t) osc] != nullptr)
        return loadedTables[(size_t) osc];
    return synth::factoryTables::get (juce::roundToInt (param (ParamIDs::osc (osc, "Table").toRawUTF8())));
}

bool ParagonX1AudioProcessor::loadWavetableFile (int osc, const juce::File& file)
{
    auto table = synth::Wavetable::build (synth::wavetableFiles::load (file));
    if (table == nullptr)
        return false;
    osc = juce::jlimit (0, ParamIDs::numOscillators - 1, osc);
    loadedTables[(size_t) osc] = std::move (table);
    if (auto* wave = parameters.getParameter (ParamIDs::osc (osc, "Wave")))
        wave->setValueNotifyingHost (wave->convertTo0to1 ((float) synth::OscWave::wavetable));
    markModified();
    updateWavetables();
    requestCheckpoint();
    return true;
}

void ParagonX1AudioProcessor::chooseFactoryWavetable (int osc, int index)
{
    osc = juce::jlimit (0, ParamIDs::numOscillators - 1, osc);
    loadedTables[(size_t) osc] = nullptr;
    for (const auto& [what, value] : { std::pair { "Table", (float) index }, std::pair { "Wave", (float) synth::OscWave::wavetable } })
        if (auto* parameter = parameters.getParameter (ParamIDs::osc (osc, what)))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    markModified();
    updateWavetables();
    requestCheckpoint();
}

void ParagonX1AudioProcessor::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (gestureIsStarting)
    {
        ++activeGestures;
    }
    else
    {
        // Never below zero, even if a host sends an unmatched "end".
        auto active = activeGestures.load();
        while (active > 0 && ! activeGestures.compare_exchange_weak (active, active - 1)) {}
        requestCheckpoint();
    }
}

ParagonX1AudioProcessor::Snapshot ParagonX1AudioProcessor::takeSnapshot() const
{
    return { captureRig(), currentRig, rigModified.load() };
}

void ParagonX1AudioProcessor::recordCheckpointIfReady()
{
    // Wait until the edit is really finished: no knob still held.
    if (! checkpointWanted.load() || activeGestures.load() > 0)
        return;

    checkpointWanted = false;
    history.record (takeSnapshot());
}

void ParagonX1AudioProcessor::restoreSnapshot (const Snapshot& snapshot)
{
    applyRig (snapshot.rig);
    currentRig = snapshot.current;
    rigModified = snapshot.modified;
    rigBroadcaster.sendChangeMessage();
}

void ParagonX1AudioProcessor::undo()
{
    // Record anything not yet recorded first, so undo takes back the latest edit rather than the one before it.
    recordCheckpointIfReady();
    if (const auto snapshot = history.undo())
        restoreSnapshot (*snapshot);
}

void ParagonX1AudioProcessor::redo()
{
    recordCheckpointIfReady();
    if (const auto snapshot = history.redo())
        restoreSnapshot (*snapshot);
}

//==============================================================================
void ParagonX1AudioProcessor::markModified() noexcept
{
    if (! applyingRig.load())
        rigModified = true;
}

void ParagonX1AudioProcessor::parameterChanged (const juce::String&, float)
{
    // Can be called on the audio thread (automation), so only touch the atomic.
    markModified();
}

void ParagonX1AudioProcessor::useRigFolder (const juce::File& folder)
{
    rigLibrary = std::make_unique<RigLibrary> (folder);
    rigBroadcaster.sendChangeMessage();
}

Rig ParagonX1AudioProcessor::captureRig() const
{
    Rig rig;
    rig.name = currentRig.name;
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! ParamIDs::notInRigs.contains (ranged->paramID))
                rig.parameters[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        if (const auto& table = loadedTables[(size_t) i])
            rig.wavetables[i] = table->getSource();
    return rig;
}

void ParagonX1AudioProcessor::applyRig (const Rig& rig)
{
    applyingRig = true;

    for (auto* parameter : AudioProcessor::getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr || ParamIDs::notInRigs.contains (ranged->paramID))
            continue;

        // Anything the preset doesn't mention goes back to its default.
        const auto found = rig.parameters.find (ranged->paramID);
        const auto normalised = found != rig.parameters.end()
                                  ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                  : ranged->getDefaultValue();
        ranged->setValueNotifyingHost (normalised);
    }

    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        const auto found = rig.wavetables.find (i);
        loadedTables[(size_t) i] = found != rig.wavetables.end() ? tableFor (found->second) : nullptr;
    }
    updateWavetables();

    applyingRig = false;
    rigModified = false;
}

void ParagonX1AudioProcessor::loadRig (const RigLibrary::Entry& entry)
{
    // Copy first: applying parameters can't invalidate it, but be safe.
    const auto rig = entry.rig;
    currentRig = { rig.name, entry.file, entry.isFactory ? rig.name : juce::String() };
    applyRig (rig);
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

const RigLibrary::Entry* ParagonX1AudioProcessor::findCurrentRigEntry() const
{
    if (currentRig.isUserRig())
        return rigLibrary->find (currentRig.file);
    if (currentRig.factoryName.isNotEmpty())
        return rigLibrary->findFactory (currentRig.factoryName);
    return nullptr;
}

bool ParagonX1AudioProcessor::saveCurrentRig()
{
    const auto* entry = findCurrentRigEntry();
    if (entry == nullptr || entry->isFactory)
        return false;

    // Keep how it's filed; replace the sound.
    auto rig = captureRig();
    rig.name = entry->rig.name;
    rig.bank = entry->rig.bank;
    rig.number = entry->rig.number;
    rig.tags = entry->rig.tags;
    rig.favourite = entry->rig.favourite;
    rig.notes = entry->rig.notes;

    const auto file = entry->file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    rigModified = false;
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::saveCurrentRigAs (const juce::String& name)
{
    auto rig = captureRig();
    rig.name = name;

    // File it next to the preset it came from, in the first free slot.
    if (const auto* entry = findCurrentRigEntry())
    {
        rig.bank = entry->rig.bank;
        rig.tags = entry->rig.tags;
        rig.notes = entry->rig.notes;
    }
    rig.number = rigLibrary->nextFreeNumber (rig.bank);

    const auto file = rigLibrary->saveAsNew (rig);
    if (file == juce::File())
        return false;

    const auto* saved = rigLibrary->find (file);
    currentRig = { saved != nullptr ? saved->rig.name : name, file, {} };
    rigModified = false;
    // Saving isn't an edit to undo, but undo shouldn't bring back the old name either.
    history.replaceCurrent (takeSnapshot());
    rigBroadcaster.sendChangeMessage();
    return true;
}

void ParagonX1AudioProcessor::stepRig (int direction)
{
    if (const auto* next = rigLibrary->step (findCurrentRigEntry(), direction))
        loadRig (*next);
}

bool ParagonX1AudioProcessor::updateRigDetails (const RigLibrary::Entry& entry, const Rig& newDetails)
{
    if (entry.isFactory)
        return false;

    auto rig = entry.rig;
    rig.name = newDetails.name.trim().isEmpty() ? rig.name : newDetails.name.trim();
    rig.bank = newDetails.bank;
    rig.number = newDetails.number;
    rig.tags = newDetails.tags;

    const auto file = entry.file;
    const auto isCurrent = currentRig.file == file;
    if (! rigLibrary->overwrite (file, rig))
        return false;

    if (isCurrent)
    {
        currentRig.name = rig.name;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

bool ParagonX1AudioProcessor::deleteRig (const RigLibrary::Entry& entry)
{
    if (entry.isFactory)
        return false;

    const auto file = entry.file;
    if (! rigLibrary->remove (file))
        return false;

    // The sound is still loaded; it just isn't saved anywhere any more.
    if (currentRig.file == file)
    {
        currentRig.file = juce::File();
        rigModified = true;
        history.replaceCurrent (takeSnapshot());
    }
    rigBroadcaster.sendChangeMessage();
    return true;
}

//==============================================================================
juce::AudioProcessorEditor* ParagonX1AudioProcessor::createEditor()
{
    return new ParagonX1AudioProcessorEditor (*this);
}

void ParagonX1AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (parameterVersionKey, parameterVersion, nullptr);
    state.setProperty (rigNameKey, currentRig.name, nullptr);
    state.setProperty (rigFileKey, currentRig.file.getFullPathName(), nullptr);
    state.setProperty (rigFactoryKey, currentRig.factoryName, nullptr);
    state.setProperty (rigModifiedKey, rigModified.load(), nullptr);

    juce::ValueTree tables (wavetablesKey);
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        if (const auto& table = loadedTables[(size_t) i])
        {
            const auto& source = table->getSource();
            tables.appendChild (juce::ValueTree (wavetableKey, { { "osc", i + 1 }, { "name", source.name }, { "frameSize", source.frameSize },
                                                                 { "data", synth::wavetableFiles::encode (source) } }), nullptr);
        }
    state.appendChild (tables, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParagonX1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // The wavetables loaded from files come out first: they aren't parameters.
    std::array<std::shared_ptr<const synth::Wavetable>, ParamIDs::numOscillators> tables {};
    if (auto saved = state.getChildWithName (wavetablesKey); saved.isValid())
    {
        for (const auto& table : saved)
        {
            const auto osc = (int) table["osc"] - 1;
            if (osc >= 0 && osc < ParamIDs::numOscillators)
                tables[(size_t) osc] = tableFor (synth::wavetableFiles::decode (table["name"].toString(), table["frameSize"], table["data"].toString()));
        }
        state.removeChild (saved, nullptr);
    }
    parameters.replaceState (state);
    loadedTables = tables;
    updateWavetables();

    // Restoring the project changed parameters, which marked the preset as
    // modified; put back what was saved instead.
    const auto rigFile = state[rigFileKey].toString();
    currentRig = { state.getProperty (rigNameKey, "Init").toString(),
                   rigFile.isNotEmpty() ? juce::File (rigFile) : juce::File(),
                   state[rigFactoryKey].toString() };
    rigModified = (bool) state.getProperty (rigModifiedKey, false);

    // A different project or preset: start the undo history afresh from it.
    history.clear();
    requestCheckpoint();
    rigBroadcaster.sendChangeMessage();
}

// JUCE calls this to create new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParagonX1AudioProcessor();
}
