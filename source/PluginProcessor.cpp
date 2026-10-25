#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

namespace
{
    constexpr double gainRampSeconds = 0.02;

    // Extra settings kept in the saved state (DAW project or preset).
    const juce::Identifier rigNameKey { "rigName" };
    const juce::Identifier rigFileKey { "rigFile" };
    const juce::Identifier rigFactoryKey { "rigFactory" };
    const juce::Identifier rigModifiedKey { "rigModified" };
    const juce::Identifier tappedBpmKey { "tappedBpm" };
    const juce::Identifier expressionCcKey { "expressionCc" };
    const juce::Identifier tunerReferenceKey { "tunerReferenceHz" };

}

ParagonX1AudioProcessor::ParagonX1AudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ParagonX1", createParameterLayout())
{
    inputGainDb = parameters.getRawParameterValue (ParamIDs::inputGain);
    outputGainDb = parameters.getRawParameterValue (ParamIDs::outputGain);
    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
        eqGainDb[(size_t) band] = parameters.getRawParameterValue (ParamIDs::eqBands[band]);

    rigLibrary = std::make_unique<RigLibrary>();

    // Any change to a parameter that rigs store marks the rig as modified,
    // and finishing an edit to one records an undo step.
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            if (! ParamIDs::notInRigs.contains (withId->paramID))
            {
                parameters.addParameterListener (withId->paramID, this);
                parameter->addListener (this);
            }

    // Housekeeping off the audio thread: show MIDI pedal moves and record
    // undo steps. 30 Hz so a MIDI expression pedal moves smoothly on screen.
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
    inputGain.reset (sampleRate, gainRampSeconds);
    outputGain.reset (sampleRate, gainRampSeconds);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load() + param (ParamIDs::rigLevel)));

    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    bypassDelay.prepare ({ sampleRate, (juce::uint32) preparedBlockSize, 1 });
    bypassDry.setSize (1, preparedBlockSize);
    bypassAmount.reset (sampleRate, gainRampSeconds);
    bypassAmount.setCurrentAndTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    compGate.prepare (sampleRate, preparedBlockSize);
    filterPitch.prepare (sampleRate, preparedBlockSize);
    presence.prepare (sampleRate, preparedBlockSize);
    graphicEq.prepare (sampleRate, preparedBlockSize);
    modulation.prepare (sampleRate, preparedBlockSize);
    delay.prepare (sampleRate, preparedBlockSize);
    reverb.prepare (sampleRate, preparedBlockSize);
    loFi.prepare (sampleRate, preparedBlockSize);
    glitch.prepare (sampleRate, preparedBlockSize);
    pitchBlock.prepare (sampleRate, preparedBlockSize);
    talkBox.prepare (sampleRate, preparedBlockSize);
    wah.prepare (sampleRate, preparedBlockSize);
    inputTap.setSampleRate (sampleRate);

    expressionVolume.reset (sampleRate, gainRampSeconds);
    expressionVolume.setCurrentAndTargetValue (1.0f);
    tunerOpen.reset (sampleRate, gainRampSeconds);
    tunerOpen.setCurrentAndTargetValue (isOn (ParamIDs::tunerOn) && isOn (ParamIDs::tunerMute) ? 0.0f : 1.0f);
    setLatencySamples (0);   // nothing in the chain looks ahead (the NAM amp, which did, went with PR7-B's bass blocks)
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

void ParagonX1AudioProcessor::readHostTempo()
{
    // The song position too, while the transport plays, so GLITCH can put its
    // steps on the song's grid. -1: not playing, or the host doesn't say.
    songBeatsAtBlock = -1.0;
    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;
            if (const auto beats = position->getPpqPosition(); position->getIsPlaying() && beats.hasValue())
                songBeatsAtBlock = *beats;
        }
}

bool ParagonX1AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto isMonoOrStereo = [] (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };

    return isMonoOrStereo (layouts.getMainInputChannelSet())
        && isMonoOrStereo (layouts.getMainOutputChannelSet());
}

double ParagonX1AudioProcessor::getTempoBpm() const noexcept
{
    const auto tapped = tappedBpm.load();
    return tapped > 0.0 ? tapped : hostBpm.load();
}

void ParagonX1AudioProcessor::readMidiExpression (const juce::MidiBuffer& midi) noexcept
{
    // CC 11 is "expression" in the MIDI spec, and what most MIDI expression
    // pedals send; SETTINGS can pick another. Only the latest value in the block matters.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isControllerOfType (expressionCc.load()))
            pendingMidiExpression = (float) message.getControllerValue() / 127.0f;
    }
}

float ParagonX1AudioProcessor::getExpressionPosition() const noexcept
{
    // A MIDI pedal move is used straight away, before the timer has copied it
    // into the parameter, so the pedal responds without the timer's delay.
    const auto midi = pendingMidiExpression.load();
    return midi >= 0.0f ? midi : param (ParamIDs::expression);
}

float ParagonX1AudioProcessor::expressionAmount() const noexcept
{
    // MIN and MAX set where the heel and toe land, so the pedal can, say,
    // sweep only the top half of the wah, or never quite reach silence.
    const auto heel = param (ParamIDs::expressionMin), toe = param (ParamIDs::expressionMax);
    return heel + (toe - heel) * getExpressionPosition();
}

void ParagonX1AudioProcessor::syncMidiExpressionToParameter()
{
    auto midi = pendingMidiExpression.load();
    if (midi < 0.0f)
        return;

    if (auto* pedal = parameters.getParameter (ParamIDs::expression))
    {
        pedal->beginChangeGesture();
        pedal->setValueNotifyingHost (pedal->convertTo0to1 (midi));
        pedal->endChangeGesture();
    }

    // Only clear it if no newer MIDI value arrived meanwhile; that one is
    // picked up on the next tick.
    pendingMidiExpression.compare_exchange_strong (midi, -1.0f);
}

void ParagonX1AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    readMidiExpression (midi);

    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    readHostTempo();

    // Some hosts occasionally send more samples than they promised in
    // prepareToPlay. Every stage's scratch memory is sized for the promise,
    // so work through an oversized block in promised-size pieces.
    for (int start = 0; start < buffer.getNumSamples(); start += preparedBlockSize)
        processChunk (buffer, start, juce::jmin (preparedBlockSize, buffer.getNumSamples() - start));
}

void ParagonX1AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    const auto numOutputChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    // The chain up to the graphic EQ runs on the first input channel, as in
    // PR7-B (it was built for a mono instrument). On a stereo track that is
    // the left channel. (The utility stomps will need the whole stereo
    // signal: that's for when they're planned.)
    auto* mono = buffer.getWritePointer (0, start);
    juce::dsp::AudioBlock<float> monoBlock (&mono, 1, (size_t) numSamples);

    // Keep the clean input, delayed to line up with the processed sound, for BYPASS.
    const auto latency = (float) juce::jlimit (0, bypassDelay.getMaximumDelayInSamples() - 1, getLatencySamples());
    auto* dry = bypassDry.getWritePointer (0);
    for (int i = 0; i < numSamples; ++i)
    {
        bypassDelay.pushSample (0, mono[i]);
        dry[i] = bypassDelay.popSample (0, latency);
    }

    // The tuner listens to the guitar itself, before anything changes it.
    inputTap.write (mono, numSamples);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb->load()));
    inputGain.applyGain (mono, numSamples);
    inputMeter.pushSamples (mono, numSamples);

    // PITCH: TUNING, OCTAVER and FREEZE, before anything else shapes the guitar.
    pitchBlock.process (monoBlock, { param (ParamIDs::pitchTune), isOn (ParamIDs::octOn), param (ParamIDs::octSubTwo),
                                     param (ParamIDs::octSubOne), param (ParamIDs::octDry), param (ParamIDs::octUp),
                                     isOn (ParamIDs::freezeOn), param (ParamIDs::freezeLevel),
                                     param (ParamIDs::freezeAttack) / 1000.0f, param (ParamIDs::freezeRelease) / 1000.0f });

    const auto expressionTarget = (int) param (ParamIDs::expressionTarget);
    const auto pedal = expressionAmount();
    wah.process (monoBlock, expressionTarget == ExpressionTargets::wah, pedal);

    compGate.process (monoBlock, { isOn (ParamIDs::compOn), param (ParamIDs::compThreshold), param (ParamIDs::compRatio),
                                   param (ParamIDs::compLevel), param (ParamIDs::gateThreshold),
                                   (effects::CompGate::Type) (int) param (ParamIDs::compType), param (ParamIDs::compAmount),
                                   param (ParamIDs::compDetail), param (ParamIDs::compMix), isOn (ParamIDs::compIntensity) });
    // PITCH / WHAMMY: the pedal bends the pitch shifter from no shift at
    // the heel to its INTERVAL at the toe, like a DigiTech Whammy.
    // WHAMMY-WAH: the pedal plays the Whammy-Wah type. Unassigned, it rests at the heel.
    const auto whammy = expressionTarget == ExpressionTargets::pitch ? pedal : 1.0f;
    // The Talk Box type runs after the cab (below); here FILTER / PITCH stands aside for it.
    const auto fpType = (effects::FilterPitch::Type) (int) param (ParamIDs::fpType);
    const auto talkBoxOn = isOn (ParamIDs::fpOn) && fpType == effects::FilterPitch::Type::talkBox;
    filterPitch.process (monoBlock, { isOn (ParamIDs::fpOn) && ! talkBoxOn, fpType,
                                      param (ParamIDs::fpSensitivity), param (ParamIDs::fpResonance),
                                      param (ParamIDs::fpInterval) * whammy, param (ParamIDs::fpMix),
                                      (int) param (ParamIDs::fpXpProgram),
                                      expressionTarget == ExpressionTargets::whammyWah ? pedal : 0.0f });
    // GLITCH: before the amp (PRE) or after the EQ (POST). The pedal, on
    // GLITCH, fades it in: dry at the heel, its MIX at the toe.
    const effects::Glitch::Settings glitchSettings { isOn (ParamIDs::glitchOn), (effects::Glitch::Mode) (int) param (ParamIDs::glitchMode),
                                                     isOn (ParamIDs::glitchSync), param (ParamIDs::glitchTime),
                                                     (int) param (ParamIDs::glitchDivision), param (ParamIDs::glitchDensity),
                                                     param (ParamIDs::glitchPitch), param (ParamIDs::glitchChaos),
                                                     param (ParamIDs::glitchMix) * (expressionTarget == ExpressionTargets::glitch ? pedal : 1.0f) };
    const auto glitchPre = (int) param (ParamIDs::glitchPosition) == (int) effects::Glitch::Position::pre;
    // Where this chunk starts in the song. Only while the DAW's own tempo is
    // in charge: a tapped tempo doesn't match the DAW's grid.
    const auto songBeats = songBeatsAtBlock >= 0.0 && tappedBpm.load() <= 0.0
                               ? songBeatsAtBlock + (double) start / getSampleRate() * hostBpm.load() / 60.0
                               : -1.0;
    if (glitchPre)
        glitch.process (monoBlock, glitchSettings, getTempoBpm(), songBeats);

    presence.process (monoBlock, param (ParamIDs::presence));

    // TALK BOX: the sound shaped into vowels by the pedal (its TALK BOX
    // setting; unassigned, it rests at the heel: "oo").
    talkBox.process (monoBlock, talkBoxOn, expressionTarget == ExpressionTargets::talkBox ? pedal : 0.0f,
                     param (ParamIDs::fpResonance), param (ParamIDs::fpMix));

    effects::GraphicEq::Settings eq;
    eq.on = isOn (ParamIDs::eqOn);
    for (size_t band = 0; band < eq.gainsDb.size(); ++band)
        eq.gainsDb[band] = eqGainDb[band]->load();
    eq.levelDb = param (ParamIDs::eqLevel);
    eq.ultraLo = isOn (ParamIDs::eqUltraLo);
    eq.ultraHi = isOn (ParamIDs::eqUltraHi);
    graphicEq.process (monoBlock, eq);
    if (! glitchPre)
        glitch.process (monoBlock, glitchSettings, getTempoBpm(), songBeats);

    // From here on, stereo: modulation, delay and reverb spread the sound
    // across both speakers.
    for (int channel = 1; channel < numOutputChannels; ++channel)
        buffer.copyFrom (channel, start, buffer, 0, start, numSamples);

    const auto stereoBlock = juce::dsp::AudioBlock<float> (buffer)
                                 .getSubBlock ((size_t) start, (size_t) numSamples)
                                 .getSubsetChannelBlock (0, (size_t) juce::jmin (numOutputChannels, effects::maxChannels));

    // MORPH FX: the pedal blends the time effects in, from dry at the heel
    // to each effect's own MIX setting at the toe. (The effects smooth their
    // mix, so sweeping the pedal doesn't click.)
    const auto morph = expressionTarget == ExpressionTargets::morph ? pedal : 1.0f;

    // Types with no dry/wet mix (tremolo, vibrato, auto-pan...) morph their depth instead.
    const auto modType = (effects::Modulation::Type) (int) param (ParamIDs::modType);
    const auto morphsDepth = ! effects::Modulation::usesMix (modType);

    // SYNC: one LFO cycle per note length at the tapped (or host) tempo.
    auto modRate = param (ParamIDs::modRate);
    if (const auto sync = (int) param (ParamIDs::modSync); sync > 0 && sync < (int) std::size (effects::Delay::syncBeats))
        modRate = juce::jlimit (0.02f, 20.0f, (float) (getTempoBpm() / 60.0) / effects::Delay::syncBeats[sync]);

    modulation.process (stereoBlock, { isOn (ParamIDs::modOn), modType, modRate,
                                       param (ParamIDs::modDepth) * (morphsDepth ? morph : 1.0f),
                                       param (ParamIDs::modMix) * (morphsDepth ? 1.0f : morph),
                                       param (ParamIDs::modManual), param (ParamIDs::modRes) });
    delay.process (stereoBlock, { isOn (ParamIDs::delayOn), param (ParamIDs::delayTime), (int) param (ParamIDs::delaySync),
                                  param (ParamIDs::delayFeedback), param (ParamIDs::delayTone), param (ParamIDs::delayMix) * morph,
                                  isOn (ParamIDs::delayPingPong), (effects::Delay::Type) (int) param (ParamIDs::delayType),
                                  param (ParamIDs::delayDuck) },
                   getTempoBpm());
    reverb.process (stereoBlock, { isOn (ParamIDs::reverbOn), (effects::Reverb::Type) (int) param (ParamIDs::reverbType),
                                   param (ParamIDs::reverbSize), param (ParamIDs::reverbTone), param (ParamIDs::reverbMix) * morph,
                                   param (ParamIDs::reverbPreDelay), param (ParamIDs::reverbDuck) });

    // LO-FI last, so the whole sound goes through the phone. Assigned to the
    // expression pedal, the pedal fades it in: full band at the heel, lo-fi at the toe.
    const auto loFiPedal = expressionTarget == ExpressionTargets::loFi ? pedal : 1.0f;
    loFi.process (stereoBlock, { isOn (ParamIDs::lofiOn), (effects::LoFi::Type) (int) param (ParamIDs::lofiType),
                                 param (ParamIDs::lofiAmount), param (ParamIDs::lofiNoise),
                                 param (ParamIDs::lofiMix) * loFiPedal, param (ParamIDs::lofiLevel) });

    // The master OUTPUT knob plus the rig's own RIG LEVEL, and the
    // expression pedal when it's a VOLUME pedal. Squaring the position gives
    // the "audio taper" of a real volume pedal: half way down is about
    // -12 dB rather than -6 dB, so the quiet end of the travel is usable.
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb->load() + param (ParamIDs::rigLevel)));
    expressionVolume.setTargetValue (expressionTarget == ExpressionTargets::volume ? pedal * pedal : 1.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue() * expressionVolume.getNextValue();
        for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
            stereoBlock.getChannelPointer (channel)[i] *= gain;
    }

    // BYPASS crossfades to the clean input. The chain keeps running underneath
    // so delay and reverb tails are ready when it's
    // switched back on.
    bypassAmount.setTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    if (bypassAmount.isSmoothing() || bypassAmount.getTargetValue() > 0.5f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto amount = bypassAmount.getNextValue();
            for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
            {
                auto& sample = stereoBlock.getChannelPointer (channel)[i];
                sample += (dry[i] - sample) * amount;
            }
        }
    }

    // The tuner mutes everything (bypassed or not), as a pedal tuner does,
    // so you can tune on stage silently. Faded to avoid a click.
    tunerOpen.setTargetValue (isOn (ParamIDs::tunerOn) && isOn (ParamIDs::tunerMute) ? 0.0f : 1.0f);
    if (tunerOpen.isSmoothing() || tunerOpen.getTargetValue() < 0.5f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto open = tunerOpen.getNextValue();
            for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
                stereoBlock.getChannelPointer (channel)[i] *= open;
        }
    }

    for (size_t channel = 0; channel < stereoBlock.getNumChannels(); ++channel)
        outputMeter.pushSamples (stereoBlock.getChannelPointer (channel), numSamples);
}

//==============================================================================
void ParagonX1AudioProcessor::timerCallback()
{
    syncMidiExpressionToParameter();
    recordCheckpointIfReady();
}

//==============================================================================
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
void ParagonX1AudioProcessor::tapTempo()
{
    if (const auto bpm = tapCounter.tap (juce::Time::getMillisecondCounterHiRes()))
        tappedBpm = std::round (*bpm * 10.0) / 10.0;   // to 0.1 BPM, as displayed
}

void ParagonX1AudioProcessor::clearTappedTempo()
{
    tappedBpm = 0.0;
    tapCounter = {};
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

        // Anything the rig doesn't mention goes back to its default.
        const auto found = rig.parameters.find (ranged->paramID);
        const auto normalised = found != rig.parameters.end()
                                  ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                  : ranged->getDefaultValue();
        ranged->setValueNotifyingHost (normalised);
    }

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

    // File it next to the rig it came from, in the first free slot.
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
    state.setProperty (rigNameKey, currentRig.name, nullptr);
    state.setProperty (rigFileKey, currentRig.file.getFullPathName(), nullptr);
    state.setProperty (rigFactoryKey, currentRig.factoryName, nullptr);
    state.setProperty (rigModifiedKey, rigModified.load(), nullptr);
    state.setProperty (tappedBpmKey, tappedBpm.load(), nullptr);
    state.setProperty (expressionCcKey, expressionCc.load(), nullptr);
    state.setProperty (tunerReferenceKey, tunerReferenceHz.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParagonX1AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // Parameters whose default changed: a project saved before they existed
    // gets the setting it had then, so it sounds the same.
    const auto addIfMissing = [&state] (const char* id, int value)
    {
        if (state.getChildWithProperty ("id", id).isValid())
            return;
        juce::ValueTree parameter ("PARAM");
        parameter.setProperty ("id", id, nullptr);
        parameter.setProperty ("value", value, nullptr);
        state.appendChild (parameter, nullptr);
    };
    addIfMissing (ParamIDs::reverbType, (int) effects::Reverb::Type::classic);    // before the reverb had a TYPE
    parameters.replaceState (state);

    // Restoring the project changed parameters, which marked the rig as
    // modified; put back what was saved instead.
    const auto rigFile = state[rigFileKey].toString();
    currentRig = { state.getProperty (rigNameKey, "Untitled").toString(),
                   rigFile.isNotEmpty() ? juce::File (rigFile) : juce::File(),
                   state[rigFactoryKey].toString() };
    rigModified = (bool) state.getProperty (rigModifiedKey, false);
    tappedBpm = (double) state.getProperty (tappedBpmKey, 0.0);
    setExpressionCc ((int) state.getProperty (expressionCcKey, 11));
    setTunerReferenceHz ((double) state.getProperty (tunerReferenceKey, 440.0));

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
