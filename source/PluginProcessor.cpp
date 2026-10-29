#include "PluginProcessor.h"

#include "ModDestinations.h"

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
    const juce::Identifier drumSamplesKey { "DrumSamples" }, drumSampleKey { "DrumSample" };
    const juce::Identifier wavetableKey { "Wavetable" };
    constexpr double retiredTableMs = 2000.0;   // how long a replaced table is kept, in case a block is still playing it
    const juce::Identifier spectraKey { "Spectra" };
    const juce::Identifier spectrumKey { "Spectrum" };
    const juce::Identifier drawingsKey { "Drawings" };
    const juce::Identifier drawingKey { "Drawing" };
    const juce::Identifier modulationKey { "Modulation" };
    const juce::Identifier midiLinksKey { "MidiLinks" };
    constexpr int parameterVersion = 1;
}

//==============================================================================
// FFT: builds edited copies of the oscillators' waves on a thread of its own
// (a wavetable's take tens of milliseconds; the window shouldn't wait).
// Only the latest request for each oscillator is built.
class ParagonX1AudioProcessor::SpectralBuilder final : private juce::Thread
{
public:
    struct Job
    {
        int osc = 0;
        SpectralKey key;
        synth::WavetableSource source;
        bool normalise = true;
    };
    struct Result
    {
        int osc = 0;
        SpectralKey key;
        std::shared_ptr<const synth::Wavetable> table, pulse;
    };

    SpectralBuilder() : juce::Thread ("PR7-K table builder") { startThread (juce::Thread::Priority::low); }
    ~SpectralBuilder() override { stopThread (4000); }

    void submit (Job job)
    {
        {
            const std::scoped_lock lock (mutex);
            auto& slot = jobs[(size_t) job.osc];
            if (! slot.has_value())
                ++pending;   // a waiting request for the same oscillator is just replaced
            slot = std::move (job);
        }
        notify();
    }

    std::vector<Result> collect()
    {
        const std::scoped_lock lock (mutex);
        return std::exchange (results, {});
    }

    bool isIdle()
    {
        const std::scoped_lock lock (mutex);
        return pending == 0;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::optional<Job> job;
            {
                const std::scoped_lock lock (mutex);
                for (auto& slot : jobs)
                    if (slot.has_value())
                    {
                        job = std::move (slot);
                        slot.reset();
                        break;
                    }
            }
            if (! job.has_value())
            {
                wait (200);
                continue;
            }
            Result result { job->osc, job->key, nullptr, nullptr };
            const auto& key = job->key;
            const auto strength = juce::jlimit (0.0f, 1.0f, key.vStrength);
            // VSTRENGTH: this copy with each band pushed one way, the other copy the other way.
            result.table = synth::Wavetable::build (job->source, [&key, strength] (int h)
            {
                return synth::spectrum::gainFor (key.edit, h, key.sweep, key.flip) * (1.0f + strength * synth::spectrum::pulsePattern (h));
            }, job->normalise);
            if (strength > 0.0f)
                result.pulse = synth::Wavetable::build (job->source, [&key, strength] (int h)
                {
                    return synth::spectrum::gainFor (key.edit, h, key.sweep, key.flip) * (1.0f - strength * synth::spectrum::pulsePattern (h));
                }, job->normalise);
            const std::scoped_lock lock (mutex);
            results.push_back (std::move (result));
            --pending;
        }
    }

    std::mutex mutex;
    std::array<std::optional<Job>, ParamIDs::numOscillators> jobs;
    std::vector<Result> results;
    int pending = 0;
};

//==============================================================================
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
    for (int b = 0; b < ParamIDs::numInsertBuses; ++b)
    {
        auto& bus = rack->buses[(size_t) b];
        const auto find = [this, b] (const char* what)
        {
            auto* value = parameters.getRawParameterValue (ParamIDs::ins (b, what));
            jassert (value != nullptr);
            return value;
        };
        for (size_t s = 0; s < bus.drive_.size(); ++s) bus.drive_[s] = find (ParamIDs::insDriveSettings[s]);
        for (size_t s = 0; s < bus.glitch_.size(); ++s) bus.glitch_[s] = find (ParamIDs::insGlitchSettings[s]);
        for (size_t s = 0; s < bus.lofi_.size(); ++s) bus.lofi_[s] = find (ParamIDs::insLofiSettings[s]);
        bus.sends_ = { find ("SendDelay"), find ("SendReverb") };
    }
    for (int k = 0; k < ParamIDs::numLfos; ++k)
        for (size_t s = 0; s < std::size (ParamIDs::lfoSettings); ++s)
        {
            lfoParameters[(size_t) k].values[s] = parameters.getRawParameterValue (ParamIDs::lfo (k, ParamIDs::lfoSettings[s]));
            jassert (lfoParameters[(size_t) k].values[s] != nullptr);
        }
    for (int k = 0; k < ParamIDs::numArps; ++k)
        for (size_t s = 0; s < std::size (ParamIDs::arpSettings); ++s)
        {
            arpParameters[(size_t) k].values[s] = parameters.getRawParameterValue (ParamIDs::arp (k, ParamIDs::arpSettings[s]));
            jassert (arpParameters[(size_t) k].values[s] != nullptr);
        }
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        for (size_t s = 0; s < std::size (ParamIDs::drumSettings); ++s)
        {
            drumParameters[(size_t) k].values[s] = parameters.getRawParameterValue (ParamIDs::drum (k, ParamIDs::drumSettings[s]));
            jassert (drumParameters[(size_t) k].values[s] != nullptr);
        }
    for (int k = 0; k < ParamIDs::numEnvelopes; ++k)
        for (size_t s = 0; s < std::size (ParamIDs::envelopeSettings); ++s)
        {
            envelopeParameters[(size_t) k].values[s] = parameters.getRawParameterValue (ParamIDs::env (k, ParamIDs::envelopeSettings[s]));
            jassert (envelopeParameters[(size_t) k].values[s] != nullptr);
        }
    for (int k = 0; k < ParamIDs::numShapers; ++k)
        for (size_t s = 0; s < std::size (ParamIDs::shaperSettings); ++s)
        {
            shaperParameters[(size_t) k].values[s] = parameters.getRawParameterValue (ParamIDs::shp (k, ParamIDs::shaperSettings[s]));
            jassert (shaperParameters[(size_t) k].values[s] != nullptr);
        }
    compileMatrix();   // the PITCH envelope's points, ready from the start

    playingFactoryTable.fill (-1);
    for (auto& table : tablesForAudio)
        table = nullptr;
    for (auto* list : { &spectralForAudio, &spectralPulseForAudio })
        for (auto& table : *list)
            table = nullptr;
    spectralBuilder = std::make_unique<SpectralBuilder>();
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        buildFreeform (i);

    // THE RACK: each band's parameters, read every block.
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::eqBandSettings); ++i)
            rack->eqBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::eqBand (band, ParamIDs::eqBandSettings[i]));
    for (int band = 0; band < ParamIDs::domNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::domBandSettings); ++i)
            rack->dominionBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::domBand (band, ParamIDs::domBandSettings[i]));
    for (int band = 0; band < ParamIDs::mbNumBands; ++band)
        for (size_t i = 0; i < std::size (ParamIDs::mbBandSettings); ++i)
            rack->multibandBandParameters[(size_t) band].values[i] = parameters.getRawParameterValue (ParamIDs::mbBand (band, ParamIDs::mbBandSettings[i]));

    rigLibrary = std::make_unique<RigLibrary>();
    rack->setFolder (rigLibrary->getFolder());
    // MIDI LEARN: a new instance starts with the default links, if some were saved.
    if (midiLinksDefaultFile().existsAsFile())
        importMidiLinks (midiLinksDefaultFile());
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
    spectralBuilder.reset();
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
    synthMidi.ensureSize (2048);
    drumMidi.ensureSize (1024);
    keyboardState.reset();
    drumKit.prepare (sampleRate, 512);   // the pieces processChunk renders in

    outputGain.reset (sampleRate, gainRampSeconds);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    bypassAmount.reset (sampleRate, gainRampSeconds);
    bypassAmount.setCurrentAndTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);

    // THE RACK.
    rack->sampleRate = sampleRate;
    rack->parametricEq.prepare (sampleRate, preparedBlockSize);
    rack->dominionProcessor.prepare (sampleRate, preparedBlockSize);
    rack->compressor.prepare (sampleRate, preparedBlockSize);
    rack->multiband.prepare (sampleRate, preparedBlockSize);
    rack->compSidechain.setSize (2, preparedBlockSize);
    rack->limiter.prepare (sampleRate, preparedBlockSize);
    rack->eqInputTap.setSampleRate (sampleRate);
    rack->delay.prepare (sampleRate, preparedBlockSize);
    rack->reverb.prepare (sampleRate, preparedBlockSize);
    for (auto& block : rack->modulation)
        block.prepare (sampleRate, preparedBlockSize);
    for (auto& bus : rack->buses)
    {
        for (int c = 0; c < 2; ++c)
        {
            bus.drive[(size_t) c].prepare (sampleRate, preparedBlockSize);
            bus.glitch[(size_t) c].prepare (sampleRate, preparedBlockSize);
        }
        bus.modulation.prepare (sampleRate, preparedBlockSize);
        bus.loFi.prepare (sampleRate, preparedBlockSize);
        bus.buffer.setSize (2, preparedBlockSize);
    }
    rack->loFi.prepare (sampleRate, preparedBlockSize);
    for (auto& send : rack->sends)
        send.setSize (2, preparedBlockSize);
    rack->eqOutputTap.setSampleRate (sampleRate);
    updateLatency();
}

effects::MasterLimiter::Settings ParagonX1AudioProcessor::getLimiterSettings() const
{
    effects::MasterLimiter::Settings s;
    s.on = isOn (ParamIDs::limitOn);
    s.style = (effects::limiter::Style) juce::jlimit (0, ParamRanges::limitModes.size() - 1, juce::roundToInt (param (ParamIDs::limitMode)));
    s.gainDb = param (ParamIDs::limitGain);
    s.ceilingDb = param (ParamIDs::limitCeiling);
    s.lookaheadMs = param (ParamIDs::limitLookahead);
    s.attackMs = param (ParamIDs::limitAttack);
    s.releaseMs = param (ParamIDs::limitRelease);
    s.transientLink = param (ParamIDs::limitLinkTransient) / 100.0f;
    s.releaseLink = param (ParamIDs::limitLinkRelease) / 100.0f;
    s.truePeak = isOn (ParamIDs::limitTruePeak);
    s.oversampling = juce::roundToInt (param (ParamIDs::limitOversampling));
    s.dither = juce::roundToInt (param (ParamIDs::limitDither));
    s.shaping = juce::roundToInt (param (ParamIDs::limitShaping));
    s.unity = isOn (ParamIDs::limitUnity);
    s.delta = isOn (ParamIDs::limitDelta);
    s.dcFilter = isOn (ParamIDs::limitDcFilter);
    s.trimDb = param (ParamIDs::limitTrim);
    s.mix = param (ParamIDs::limitMix) / 100.0f;
    s.link = isOn (ParamIDs::limitLink);
    return s;
}

void ParagonX1AudioProcessor::updateLatency()
{
    // CONSTRICTOR's LOOK-AHEAD, and the LIMITER's (with its oversampling and true peak): the synth itself adds none.
    const auto latency = (isOn (ParamIDs::mbLookahead) ? (int) std::lround (effects::multiband::lookaheadSeconds * rack->sampleRate) : 0)
                         + rack->limiter.getLatencySamples (getLimiterSettings());
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

effects::multiband::Settings ParagonX1AudioProcessor::getMultibandSettings() const noexcept
{
    namespace mb = effects::multiband;
    mb::Settings settings;
    settings.lookahead = isOn (ParamIDs::mbLookahead);
    settings.mix = param (ParamIDs::mbMix) / 100.0f;
    settings.soloBand = rack->multibandSolo.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& v = rack->multibandBandParameters[i].values;   // in mbBandSettings' order
        auto& band = settings.bands[i];
        band.used = v[0]->load() > 0.5f;
        band.on = v[1]->load() > 0.5f;
        band.lowHz = v[2]->load();
        band.highHz = v[3]->load();
        band.slope = juce::jlimit (0, mb::numSlopes - 1, juce::roundToInt (v[4]->load()));
        band.thresholdDb = v[5]->load();
        band.rangeDb = v[6]->load();
        band.mode = (mb::Mode) juce::jlimit (0, 1, juce::roundToInt (v[7]->load()));
        band.ratio = v[8]->load();
        band.kneeDb = v[9]->load();
        band.attackMs = v[10]->load();
        band.releaseMs = v[11]->load();
        band.character = (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (v[12]->load()));
        band.stereo = (mb::Stereo) juce::jlimit (0, 2, juce::roundToInt (v[13]->load()));
        band.link = v[14]->load() / 100.0f;
        band.outputDb = v[15]->load();
        band.lookaheadMs = v[16]->load();
    }
    return settings;
}

eq::ParametricEq::Settings ParagonX1AudioProcessor::getEqSettings() const noexcept
{
    eq::ParametricEq::Settings settings;
    settings.on = isOn (ParamIDs::eqOn);
    settings.outputDb = param (ParamIDs::eqOutput);
    settings.gainScale = param (ParamIDs::eqGainScale);
    settings.autoGain = isOn (ParamIDs::eqAutoGain);
    settings.soloBand = rack->eqSoloBand.load();
    for (size_t i = 0; i < settings.bands.size(); ++i)
    {
        const auto& values = rack->eqBandParameters[i].values;   // in eqBandSettings' order
        auto& band = settings.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (eq::Shape) juce::roundToInt (values[2]->load());
        band.frequency = values[3]->load();
        band.gainDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.placement = (eq::Placement) juce::roundToInt (values[7]->load());
        band.dynamicRangeDb = values[8]->load();
        band.autoThreshold = values[9]->load() > 0.5f;
        band.thresholdDb = values[10]->load();
        band.attack = values[11]->load();
        band.release = values[12]->load();
    }
    return settings;
}

dominion::Settings ParagonX1AudioProcessor::getDominionSettings() const noexcept
{
    dominion::Settings s;
    s.on = isOn (ParamIDs::domOn);
    s.mode = (dominion::Mode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domMode)));
    s.depth = param (ParamIDs::domDepth);
    s.detail = param (ParamIDs::domDetail);
    s.attack = param (ParamIDs::domAttack);
    s.release = param (ParamIDs::domRelease);
    s.stereo = (dominion::StereoMode) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::domStereo)));
    s.link = param (ParamIDs::domLink);
    s.focus = param (ParamIDs::domFocus);
    s.detailTiltLow = param (ParamIDs::domDetailLow);
    s.detailTiltHigh = param (ParamIDs::domDetailHigh);
    s.attackTiltLow = param (ParamIDs::domAttackLow);
    s.attackTiltHigh = param (ParamIDs::domAttackHigh);
    s.releaseTiltLow = param (ParamIDs::domReleaseLow);
    s.releaseTiltHigh = param (ParamIDs::domReleaseHigh);
    s.maxCutDb = param (ParamIDs::domMaxCut);
    s.wetTrimDb = param (ParamIDs::domWetTrim);
    s.mix = param (ParamIDs::domMix);
    s.outDb = param (ParamIDs::domOut);
    s.delta = isOn (ParamIDs::domDelta);
    for (size_t i = 0; i < s.bands.size(); ++i)
    {
        const auto& values = rack->dominionBandParameters[i].values;   // in domBandSettings' order
        auto& band = s.bands[i];
        band.used = values[0]->load() > 0.5f;
        band.on = values[1]->load() > 0.5f;
        band.shape = (dominion::Shape) juce::jlimit (0, dominion::shapeNames.size() - 1, juce::roundToInt (values[2]->load()));
        band.frequency = values[3]->load();
        band.depthDb = values[4]->load();
        band.q = values[5]->load();
        band.slope = juce::roundToInt (values[6]->load());
        band.focus = values[7]->load();
    }
    s.listenBand = rack->dominionListen.load();
    return s;
}

effects::Delay::Settings ParagonX1AudioProcessor::getDelaySettings() const noexcept
{
    effects::Delay::Settings s;
    s.on = isOn (ParamIDs::delayOn);
    s.timeMs = param (ParamIDs::delayTime);
    s.syncIndex = juce::jlimit (0, effects::Delay::syncNames.size() - 1, juce::roundToInt (param (ParamIDs::delaySync)));
    s.feedback = param (ParamIDs::delayFeedback);
    s.tone = param (ParamIDs::delayTone);
    s.mix = param (ParamIDs::delayMix);
    s.pingPong = isOn (ParamIDs::delayPingPong);
    s.type = (effects::Delay::Type) juce::jlimit (0, effects::Delay::typeNames.size() - 1, juce::roundToInt (param (ParamIDs::delayType)));
    s.duck = param (ParamIDs::delayDuck);
    s.returnOnly = true;
    return s;
}

effects::Reverb::Settings ParagonX1AudioProcessor::getReverbSettings() const noexcept
{
    effects::Reverb::Settings s;
    s.on = isOn (ParamIDs::reverbOn);
    s.type = (effects::Reverb::Type) juce::jlimit (0, effects::Reverb::typeNames.size() - 1, juce::roundToInt (param (ParamIDs::reverbType)));
    s.size = param (ParamIDs::reverbSize);
    s.tone = param (ParamIDs::reverbTone);
    s.mix = param (ParamIDs::reverbMix);
    s.preDelayMs = param (ParamIDs::reverbPreDelay);
    s.duck = param (ParamIDs::reverbDuck);
    s.returnOnly = true;
    return s;
}

void ParagonX1AudioProcessor::processSends (juce::dsp::AudioBlock<float> block)
{
    // DELAY on its send (giving back just its echoes), then REVERB on its own send plus DELAY > REVERB's share of
    // those echoes; both returns join the synth before ECHELON. Each does nothing once it's off and its tail is gone.
    auto& r = *rack;
    const auto numSamples = (int) block.getNumSamples();
    juce::dsp::AudioBlock<float> delayReturn (r.sends[0].getArrayOfWritePointers(), 2, (size_t) numSamples);
    juce::dsp::AudioBlock<float> reverbReturn (r.sends[1].getArrayOfWritePointers(), 2, (size_t) numSamples);
    r.delay.process (delayReturn, getDelaySettings(), juce::jmax (1.0, hostBpm.load()));
    if (const auto feed = param (ParamIDs::delayToReverb); feed > 0.0f)
        for (int c = 0; c < 2; ++c)
            r.sends[1].addFrom (c, 0, r.sends[0], c, 0, numSamples, feed);
    r.reverb.process (reverbReturn, getReverbSettings());
    for (int s = 0; s < synth::numSends; ++s)
        r.returnPeak[(size_t) s] = juce::jmax (r.sends[(size_t) s].getMagnitude (0, 0, numSamples), r.sends[(size_t) s].getMagnitude (1, 0, numSamples));
    const auto numChannels = (int) block.getNumChannels();
    for (int c = 0; c < numChannels; ++c)
    {
        auto* out = block.getChannelPointer ((size_t) c);
        for (int i = 0; i < numSamples; ++i)
            out[i] += numChannels == 1 ? 0.5f * (r.sends[0].getSample (0, i) + r.sends[0].getSample (1, i) + r.sends[1].getSample (0, i) + r.sends[1].getSample (1, i))
                                       : r.sends[0].getSample (c, i) + r.sends[1].getSample (c, i);
    }
}

effects::Modulation::Settings ParagonX1AudioProcessor::getModSettings (int which) const noexcept
{
    return modSettingsFor (which == 0 ? ParamIDs::modIds : ParamIDs::mod2Ids);
}

effects::Modulation::Settings ParagonX1AudioProcessor::modSettingsFor (const ParamIDs::ModIds& ids) const noexcept
{
    // As PR7-G's modSettingsFor, without its expression pedal and POSITION.
    effects::Modulation::Settings s;
    s.on = isOn (ids.on);
    s.type = (effects::Modulation::Type) juce::jlimit (0, effects::Modulation::typeNames.size() - 1, juce::roundToInt (param (ids.type)));
    // SYNC: one LFO cycle per note length at the song's tempo.
    s.rateHz = param (ids.rate);
    if (const auto sync = juce::roundToInt (param (ids.sync)); sync > 0 && sync < (int) std::size (effects::Delay::syncBeats))
        s.rateHz = juce::jlimit (0.02f, 40.0f, (float) (juce::jmax (1.0, hostBpm.load()) / 60.0) / effects::Delay::syncBeats[sync]);   // 1/32 at 300 BPM
    s.depth = param (ids.depth);
    s.mix = param (ids.mix);
    s.eq = param (ids.eq);
    s.stereoOutputs = isOn (ids.stereo);
    s.manual = param (ids.manual);
    s.resonance = param (ids.res);
    s.hadronBlock = isOn (ids.hadron);
    s.wave = param (ids.wave);
    return s;
}

effects::LoFi::Settings ParagonX1AudioProcessor::getLoFiSettings() const noexcept { return loFiSettingsFor (-1); }

effects::LoFi::Settings ParagonX1AudioProcessor::getInsertLoFiSettings (int bus) const noexcept { return loFiSettingsFor (bus); }

effects::LoFi::Settings ParagonX1AudioProcessor::loFiSettingsFor (int bus) const noexcept
{
    // The whole sound's lofi* (bus -1), or a bus's "ins<n>Lofi*": the same settings in the same order.
    const auto value = [this, bus] (const char* whole, int index)
    {
        return bus < 0 ? param (whole) : rack->buses[(size_t) bus].lofi_[(size_t) index]->load();
    };
    effects::LoFi::Settings s;
    s.on = value (ParamIDs::lofiOn, 0) > 0.5f;
    s.type = (effects::LoFi::Type) juce::jlimit (0, effects::LoFi::typeNames.size() - 1, juce::roundToInt (value (ParamIDs::lofiType, 1)));
    s.amount = value (ParamIDs::lofiAmount, 2);
    s.noise = value (ParamIDs::lofiNoise, 3);
    s.mix = value (ParamIDs::lofiMix, 4);
    s.levelDb = value (ParamIDs::lofiLevel, 5);
    return s;
}

effects::Drive::Settings ParagonX1AudioProcessor::getInsertDriveSettings (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].drive_;   // insDriveSettings' order
    const auto value = [&v] (int index) { return v[(size_t) index]->load(); };
    effects::Drive::Settings s;
    s.on = value (0) > 0.5f;
    s.model = (effects::Drive::Model) juce::jlimit (0, effects::Drive::modelNames.size() - 1, juce::roundToInt (value (1)));
    s.drive = value (2);
    s.tone = value (3);
    s.levelDb = value (4);
    s.attack = juce::jlimit (0, effects::Drive::skylineAttackNames.size() - 1, juce::roundToInt (value (5)));
    s.gate = value (6);
    s.high = value (7);
    s.low = value (8);
    s.middle = value (9);
    s.midFreq = value (10);
    return s;
}

effects::Glitch::Settings ParagonX1AudioProcessor::getInsertGlitchSettings (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].glitch_;   // insGlitchSettings' order
    const auto value = [&v] (int index) { return v[(size_t) index]->load(); };
    effects::Glitch::Settings s;
    s.on = value (0) > 0.5f;
    s.mode = (effects::Glitch::Mode) juce::jlimit (0, effects::Glitch::modeNames.size() - 1, juce::roundToInt (value (1)));
    s.sync = value (2) > 0.5f;
    s.lengthMs = value (3);
    s.division = juce::jlimit (0, effects::Glitch::syncNames.size() - 1, juce::roundToInt (value (4)));
    s.density = value (5);
    s.pitch = value (6);
    s.chaos = value (7);
    s.mix = value (8);
    return s;
}

std::array<float, synth::numSends> ParagonX1AudioProcessor::getInsertSends (int bus) const noexcept
{
    const auto& v = rack->buses[(size_t) juce::jlimit (0, synth::numInsertBuses - 1, bus)].sends_;
    return { v[0]->load(), v[1]->load() };
}

void ParagonX1AudioProcessor::processInsertBuses (juce::dsp::AudioBlock<float> block, bool sendsReady)
{
    // Each bus an oscillator plays into: DRIVE and GLITCH (one a side: PR7-G's are mono), MOD, LO-FI, as on PR7-G's
    // chain (GLITCH POST, after the drive); then into the mix, and into the sends at the bus's amounts.
    const auto numSamples = (int) block.getNumSamples();
    const auto bpm = juce::jmax (1.0, hostBpm.load());
    for (int b = 0; b < synth::numInsertBuses; ++b)
    {
        auto& bus = rack->buses[(size_t) b];
        if (! bus.used)
        {
            bus.peak = 0.0f;
            continue;
        }
        auto busBlock = juce::dsp::AudioBlock<float> (bus.buffer).getSubBlock (0, (size_t) numSamples);
        const auto drive = getInsertDriveSettings (b);
        const auto glitch = getInsertGlitchSettings (b);
        // After the DRIVE, a DC blocker (5 Hz): RABID's circuit can leave its coupling capacitor charged, a steady
        // offset after loud playing. On PR7-G the amp after the pedal blocks it; here nothing would. Only while the
        // drive is on; switched off, what it was taking off fades out over about 50 ms, then the bus is untouched.
        const auto dcCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) getSampleRate());
        const auto dcFade = std::exp (-1.0f / (0.01f * (float) getSampleRate()));
        for (size_t c = 0; c < 2; ++c)
        {
            bus.drive[c].process (busBlock.getSingleChannelBlock (c), drive);
            auto& dc = bus.driveDc[c];
            auto* x = busBlock.getChannelPointer (c);
            if (drive.on)
                for (int i = 0; i < numSamples; ++i)
                {
                    dc += (x[i] - dc) * dcCoefficient;
                    x[i] -= dc;
                }
            else if (dc != 0.0f)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    dc *= dcFade;
                    x[i] -= dc;
                }
                if (std::abs (dc) < 1.0e-9f)
                    dc = 0.0f;
            }
            bus.glitch[c].process (busBlock.getSingleChannelBlock (c), glitch, bpm, hostPpq);
        }
        bus.modulation.process (busBlock, getInsertModSettings (b));
        bus.loFi.process (busBlock, getInsertLoFiSettings (b));
        bus.peak = juce::jmax (bus.buffer.getMagnitude (0, 0, numSamples), bus.buffer.getMagnitude (1, 0, numSamples));

        const auto numChannels = (int) block.getNumChannels();
        for (int c = 0; c < numChannels; ++c)
        {
            auto* out = block.getChannelPointer ((size_t) c);
            if (numChannels == 1)
                for (int i = 0; i < numSamples; ++i)
                    out[i] += 0.5f * (bus.buffer.getSample (0, i) + bus.buffer.getSample (1, i));
            else
                juce::FloatVectorOperations::add (out, bus.buffer.getReadPointer (c), numSamples);
        }
        if (sendsReady)
        {
            const auto sends = getInsertSends (b);
            const auto on = std::array<bool, synth::numSends> { isOn (ParamIDs::delayOn), isOn (ParamIDs::reverbOn) };
            for (int s = 0; s < synth::numSends; ++s)
                if (on[(size_t) s] && sends[(size_t) s] > 0.0f)
                    for (int c = 0; c < 2; ++c)
                        rack->sends[(size_t) s].addFrom (c, 0, bus.buffer, c, 0, numSamples, sends[(size_t) s]);
        }
    }
}

void ParagonX1AudioProcessor::processModFx (juce::dsp::AudioBlock<float> block)
{
    // MOD, then MOD 2, then LO-FI, as on PR7-G (where LO-FI comes after the reverb too, so the echoes go "through
    // the phone" with the rest). Each passes the sound untouched while off.
    rack->modulation[0].process (block, getModSettings (0));
    rack->modulation[1].process (block, getModSettings (1));
    const auto loFi = getLoFiSettings();
    rack->loFi.process (block, loFi);
    auto peak = 0.0f;
    if (loFi.on)
        for (size_t c = 0; c < block.getNumChannels(); ++c)
            peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (block.getChannelPointer (c), (int) block.getNumSamples()),
                               -juce::FloatVectorOperations::findMinimum (block.getChannelPointer (c), (int) block.getNumSamples()));
    rack->loFiPeak = peak;
}

void ParagonX1AudioProcessor::processRack (juce::dsp::AudioBlock<float> block)
{
    // As PR7-E 0.18.1's processChunk, from its EQ to its COMP: ECHELON, with what goes in and comes out kept for its
    // analyser (and EQ Match); DOMINION after it (so it hears what the EQ made); CONSTRICTOR's bands, then its full band
    // (PR7-C's compressor, which takes whole buffers and the same audio to listen to). Each does nothing while off.
    auto& r = *rack;
    const auto numChannels = (int) block.getNumChannels();
    const auto numSamples = (int) block.getNumSamples();
    if (r.compSidechain.getNumSamples() < numSamples)
        return;   // not prepared (prepareToPlay sizes everything): pass the synth through rather than read past a buffer
    const auto writeTap = [&] (InputTap& tap)
    {
        if (numChannels > 1)
            tap.write (block.getChannelPointer (0), block.getChannelPointer (1), numSamples);
        else
            tap.write (block.getChannelPointer (0), numSamples);
    };
    writeTap (r.eqInputTap);
    r.parametricEq.process (block, getEqSettings());
    writeTap (r.eqOutputTap);

    r.dominionProcessor.process (block, getDominionSettings());

    r.multiband.process (block, getMultibandSettings(), isOn (ParamIDs::compOn));
    float* channels[effects::maxChannels] {};
    for (int channel = 0; channel < numChannels; ++channel)
    {
        channels[channel] = block.getChannelPointer ((size_t) channel);
        r.compSidechain.copyFrom (channel, 0, channels[channel], numSamples);
    }
    juce::AudioBuffer<float> audio (channels, numChannels, numSamples);
    const juce::AudioBuffer<float> detector (r.compSidechain.getArrayOfWritePointers(), numChannels, numSamples);
    r.compressor.process (audio, detector, numSamples,
                          { isOn (ParamIDs::compOn) && isOn (ParamIDs::compWide), param (ParamIDs::compThreshold), param (ParamIDs::compRatio),
                            param (ParamIDs::compAttack), param (ParamIDs::compRelease), isOn (ParamIDs::compAuto),
                            param (ParamIDs::compMakeUp),
                            (effects::Compressor::Character) juce::jlimit (0, 2, juce::roundToInt (param (ParamIDs::compCharacter))),
                            isOn (ParamIDs::compLink) });
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
        for (int k = 0; k < synth::numShapers; ++k)
            o.toShaper[(size_t) k] = get (oscSetting ("Shp1") + k) > 0.5f;
        o.pitchEnvelope = get (oscSetting ("PitchEnv")) > 0.5f;
        auto& tr = o.travel;   // TRAVEL (its envelope and steps are added per block)
        tr.on = get (oscSetting ("TrOn")) > 0.5f;
        tr.editors = (synth::travel::Editors) choice (get (oscSetting ("TrEditors")), synth::travel::editorNames.size());
        tr.trigger = (synth::travel::Trigger) choice (get (oscSetting ("TrTrigger")), synth::travel::triggerNames.size());
        tr.mode = (synth::travel::Mode) choice (get (oscSetting ("TrMode")), synth::travel::modeNames.size());
        tr.patternMode = (synth::travel::Mode) choice (get (oscSetting ("TrPatternMode")), synth::travel::modeNames.size());
        tr.steps = juce::roundToInt (get (oscSetting ("TrSteps")));
        tr.strength = get (oscSetting ("TrStrength"));
        tr.strengthVelocity = get (oscSetting ("TrVelocity"));
        tr.velocityCurve = get (oscSetting ("TrVelCurve"));
        tr.smooth = get (oscSetting ("TrSmooth"));
        tr.phase = get (oscSetting ("TrPhase"));
        tr.flip = get (oscSetting ("TrFlip")) > 0.5f;
        tr.rateHz = get (oscSetting ("TrRate"));
        tr.sync = (synth::mod::LfoSync) choice (get (oscSetting ("TrSync")), synth::mod::lfoSyncNames.size());
        tr.division = choice (get (oscSetting ("TrDivision")), synth::mod::lfoDivisionNames.size());
        o.arp = choice (get (oscSetting ("Arp")), synth::arp::routeNames.size());
        o.send = { get (oscSetting ("SendDelay")), get (oscSetting ("SendReverb")) };
        o.insertBus = choice (get (oscSetting ("Insert")), ParamIDs::insertBusNames.size());

        o.table = tablesForAudio[i].load();
        o.spectral = spectralForAudio[i].load();
        o.spectralPulse = spectralPulseForAudio[i].load();
        o.fftSweep = get (oscSetting ("FftSweep"));
        o.fftFlip = get (oscSetting ("FftFlip")) > 0.5f;
        o.fftVSpeedHz = get (oscSetting ("FftVSpeed"));
        o.fftVStrength = get (oscSetting ("FftVStrength"));
        o.fftLp = get (oscSetting ("FftLp"));
        o.fftHp = get (oscSetting ("FftHp"));
        o.fftSlope = get (oscSetting ("FftSlope"));
        o.tableIndex = get (oscSetting ("WtIndex"));
        o.tableSteppy = choice (get (oscSetting ("WtMode")), synth::wavetableModeNames.size()) == 1;
        o.tableFrames = juce::jlimit (2, synth::maxWavetableFrames, juce::roundToInt (get (oscSetting ("WtFrames"))));

        o.skew = get (oscSetting ("Skew"));
        o.pinch = get (oscSetting ("Pinch"));
        o.formant = get (oscSetting ("Formant"));
        o.formantMode = (synth::FormantMode) choice (get (oscSetting ("FormantMode")), synth::formantModeNames.size());
        o.crush = get (oscSetting ("Crush"));
        o.crushMode = (synth::CrushMode) choice (get (oscSetting ("CrushMode")), synth::crushModeNames.size());

        o.syncAmount = get (oscSetting ("SyncAmount"));
        o.syncMode = (synth::SyncMode) choice (get (oscSetting ("SyncMode")), synth::syncModeNames.size());
        o.syncEnvAmount = get (oscSetting ("SyncEnv"));
        o.syncEnvMs = get (oscSetting ("SyncEnvTime"));
        o.syncEnvCurve = get (oscSetting ("SyncEnvCurve"));
        o.fmAmount = get (oscSetting ("FmAmount"));
        o.fmSource = choice (get (oscSetting ("FmSource")), synth::fmSourceNames.size());
        o.amAmount = get (oscSetting ("AmAmount"));
        o.amShape = (synth::ModShape) choice (get (oscSetting ("AmShape")), synth::modShapeNames.size());
        o.modRate = get (oscSetting ("ModRate"));
        o.modGrid = (synth::ModGrid) choice (get (oscSetting ("ModGrid")), synth::modGridNames.size());
        o.modPhaseLock = get (oscSetting ("ModLock")) > 0.5f;

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

    static_assert (synth::numShapers == ParamIDs::numShapers);
    for (size_t k = 0; k < s.shapers.size(); ++k)
    {
        const auto& v = shaperParameters[k].values;   // in shaperSettings' order
        const auto get = [&v] (int index) { return v[(size_t) index]->load(); };
        using ParamIDs::shaperSetting;
        auto& sh = s.shapers[k];
        sh.on = get (shaperSetting ("On")) > 0.5f;
        sh.model = (synth::ShaperModel) choice (get (shaperSetting ("Model")), synth::shaperModelNames.size());
        sh.drive = get (shaperSetting ("Drive"));
        sh.driveVelocity = get (shaperSetting ("DriveVel"));
        sh.eqGainDb = { get (shaperSetting ("Eq1Gain")), get (shaperSetting ("Eq2Gain")) };
        sh.eqFreqHz = { get (shaperSetting ("Eq1Freq")), get (shaperSetting ("Eq2Freq")) };
        sh.eqQ = { get (shaperSetting ("Eq1Q")), get (shaperSetting ("Eq2Q")) };
        sh.eqSplit = get (shaperSetting ("EqSplit")) > 0.5f;
        sh.afterFilter = choice (get (shaperSetting ("Place")), synth::shaperPlaceNames.size()) == 1;
        sh.mix = get (shaperSetting ("Mix"));
        sh.outputDb = get (shaperSetting ("Output"));
    }

    // MODULATION: the LFOs and macros (the routes are added per block).
    for (size_t k = 0; k < s.modulation.lfos.size(); ++k)
    {
        const auto& v = lfoParameters[k].values;   // in lfoSettings' order
        const auto get = [&v] (int index) { return v[(size_t) index]->load(); };
        using ParamIDs::lfoSetting;
        auto& l = s.modulation.lfos[k];
        l.wave = (synth::mod::LfoWave) choice (get (lfoSetting ("Wave")), synth::mod::lfoWaveNames.size());
        l.rateHz = get (lfoSetting ("Rate"));
        l.sync = (synth::mod::LfoSync) choice (get (lfoSetting ("Sync")), synth::mod::lfoSyncNames.size());
        l.division = choice (get (lfoSetting ("Division")), synth::mod::lfoDivisionNames.size());
        l.trigger = (synth::mod::LfoTrigger) choice (get (lfoSetting ("Trigger")), synth::mod::lfoTriggerNames.size());
        l.phase = get (lfoSetting ("Phase"));
        l.offset = get (lfoSetting ("Offset"));
        l.delayMs = get (lfoSetting ("Delay"));
        l.fadeInMs = get (lfoSetting ("FadeIn"));
        l.oneShot = get (lfoSetting ("OneShot")) > 0.5f;
    }
    for (size_t k = 0; k < s.modulation.envelopes.size(); ++k)
    {
        const auto& v = envelopeParameters[k].values;   // in envelopeSettings' order
        const auto get = [&v] (int index) { return v[(size_t) index]->load(); };
        using ParamIDs::envelopeSetting;
        auto& e = s.modulation.envelopes[k];
        e.mode = (synth::mod::EnvelopeMode) choice (get (envelopeSetting ("Mode")), synth::mod::envelopeModeNames.size());
        e.trigger = (synth::mod::EnvelopeTrigger) choice (get (envelopeSetting ("Trigger")), synth::mod::envelopeTriggerNames.size());
        e.sync = get (envelopeSetting ("Sync")) > 0.5f;
        e.speed = get (envelopeSetting ("Speed"));
        e.steps = juce::roundToInt (get (envelopeSetting ("Steps")));
        e.flip = get (envelopeSetting ("Flip")) > 0.5f;
        e.offset = get (envelopeSetting ("Offset"));
    }
    for (int k = 0; k < 3; ++k)
        s.modulation.macros[(size_t) k] = param (ParamIDs::macros[k]);
    for (int k = 0; k < 2; ++k)
        s.modulation.buttons[(size_t) k] = isOn (ParamIDs::macroButtons[k]);
    s.modulation.bpm = hostBpm.load();
    s.arpsTogether = isOn (ParamIDs::arpsTogether);
    s.sendOn = { isOn (ParamIDs::delayOn), isOn (ParamIDs::reverbOn) };
    s.modulation.toModWheel = { isOn (ParamIDs::mwBreath), isOn (ParamIDs::mwExpression), isOn (ParamIDs::mwAftertouch), isOn (ParamIDs::mwSustain) };

    // The PITCH module (its envelope's points are added per block).
    auto& pm = s.pitch;
    pm.envelopeOn = isOn (ParamIDs::pitchEnvOn);
    pm.envelope.mode = (synth::mod::EnvelopeMode) choice (param (ParamIDs::pitchEnvMode), synth::mod::envelopeModeNames.size());
    pm.envelope.speed = param (ParamIDs::pitchEnvSpeed);
    pm.envelope.steps = juce::roundToInt (param (ParamIDs::pitchEnvSteps));
    pm.envelope.flip = isOn (ParamIDs::pitchEnvFlip);
    pm.rangeSemitones = param (ParamIDs::pitchEnvRange);
    pm.keytrack = param (ParamIDs::pitchKeytrack);
    pm.bendLagMs = param (ParamIDs::bendLag);
    pm.glideCurve = param (ParamIDs::glideCurve);
    pm.glideSplit = juce::roundToInt (param (ParamIDs::glideSplit));

    // The ARPEGGIATORS (their patterns are added per block).
    for (size_t k = 0; k < s.arps.size(); ++k)
    {
        const auto& v = arpParameters[k].values;
        const auto get = [&v] (int index) { return v[(size_t) index]->load(); };
        using ParamIDs::arpSetting;
        auto& a = s.arps[k];
        a.on = get (arpSetting ("On")) > 0.5f;
        a.mode = (synth::arp::Mode) choice (get (arpSetting ("Mode")), synth::arp::modeNames.size());
        a.division = choice (get (arpSetting ("Division")), synth::seq::divisionNames.size());
        a.timing = (synth::seq::Timing) choice (get (arpSetting ("Timing")), synth::seq::timingNames.size());
        a.octaves = juce::roundToInt (get (arpSetting ("Octaves")));
        a.gate = get (arpSetting ("Gate"));
        a.length = juce::roundToInt (get (arpSetting ("Length")));
        a.velocityMode = (synth::arp::VelocityMode) choice (get (arpSetting ("Velocity")), synth::arp::velocityModeNames.size());
        a.lowKey = juce::roundToInt (get (arpSetting ("LowKey")));
        a.highKey = juce::roundToInt (get (arpSetting ("HighKey")));
        a.humanize = get (arpSetting ("Humanize"));
        a.strum = get (arpSetting ("Strum"));
        a.pattern = choice (get (arpSetting ("Pattern")), synth::arp::patternNames.size());
        a.chain = get (arpSetting ("Chain")) > 0.5f;
        a.trigger = (synth::arp::Trigger) choice (get (arpSetting ("Trigger")), synth::arp::triggerNames.size());
        a.noteSource = (synth::arp::NoteSource) choice (get (arpSetting ("NoteSource")), synth::arp::noteSourceNames.size());
    }

    // The STEP SEQUENCER (its steps are added per block).
    auto& sq = s.stepSeq;
    sq.on = isOn (ParamIDs::seqOn);
    sq.trigger = (synth::seq::Trigger) choice (param (ParamIDs::seqTrigger), synth::seq::triggerNames.size());
    sq.division = choice (param (ParamIDs::seqDivision), synth::seq::divisionNames.size());
    sq.timing = (synth::seq::Timing) choice (param (ParamIDs::seqTiming), synth::seq::timingNames.size());
    sq.mode = (synth::seq::Mode) choice (param (ParamIDs::seqMode), synth::seq::modeNames.size());
    sq.length = juce::roundToInt (param (ParamIDs::seqLength));
    sq.contour = param (ParamIDs::seqContour);
    sq.gate = param (ParamIDs::seqGate);
    sq.volMix = param (ParamIDs::seqVolMix);
    sq.width = param (ParamIDs::seqWidth);
    sq.decayMs = param (ParamIDs::seqDecay);
    sq.fadeInMs = param (ParamIDs::seqFadeIn);
    sq.delayMs = param (ParamIDs::seqDelay);
    sq.lowKey = juce::roundToInt (param (ParamIDs::seqLowKey));
    sq.highKey = juce::roundToInt (param (ParamIDs::seqHighKey));
    sq.shuffle = param (ParamIDs::shuffle);

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
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;
            const auto ppq = position->getPpqPosition();
            hostPpq = position->getIsPlaying() && ppq.hasValue() ? *ppq : -1.0;
        }

    // MIDI LEARN: the last controller moved (for the strip and for learning), and the knobs linked to controllers.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;
        const auto cc = message.getControllerNumber();
        if (cc < 0 || cc >= 120)
            continue;   // 120-127: channel mode messages (all notes off...)
        const auto value = (float) message.getControllerValue() / 127.0f;
        lastController = cc;
        lastControllerValue = value;
        ++controllerCount;
        if (auto* target = controllerTargets[(size_t) cc].load())
            target->setValueNotifyingHost (value);
    }

    // The on-screen keyboard's notes join the DAW's.
    keyboardMidi.clear();
    keyboardMidi.addEvents (midi, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (keyboardMidi, 0, numSamples, true);
    if (const auto wheel = pendingModWheel.exchange (-1.0f); wheel >= 0.0f)
        keyboardMidi.addEvent (juce::MidiMessage::controllerEvent (1, 1, juce::roundToInt (wheel * 127.0f)), 0);

    // THE DRUMS: while they're on, a note-on in the kit's 12 keys plays the kit, not the synth. Note-offs go to the
    // synth either way (the kit's hits are one-shots), so a note held as DRUMS was switched on still ends. With the
    // DRUM SEQUENCER on KEY, the four keys under the kit start its patterns: their note-ons go to the kit only, their
    // note-offs to both (the kit stops the pattern on them).
    const auto kit = getKitSettings();
    const auto sequencer = getDrumSeqSettings();
    const auto patternKeys = kit.on && sequencer.on && sequencer.trigger == synth::drumseq::Trigger::key;
    synthMidi.clear();
    drumMidi.clear();
    for (const auto metadata : keyboardMidi)
    {
        const auto message = metadata.getMessage();
        const auto patternKey = patternKeys && message.isNoteOnOrOff()
                             && synth::drums::Kit::patternForKey (message.getNoteNumber(), kit.rootKey) >= 0;
        if (patternKey)
        {
            drumMidi.addEvent (message, metadata.samplePosition);
            if (message.isNoteOff())
                synthMidi.addEvent (message, metadata.samplePosition);
        }
        else if (kit.on && message.isNoteOn() && kit.slotFor (message.getNoteNumber()) >= 0)
            drumMidi.addEvent (message, metadata.samplePosition);
        else
            synthMidi.addEvent (message, metadata.samplePosition);
    }
    for (int slot = 0; slot < ParamIDs::numDrums; ++slot)
        if (pendingPreview[(size_t) slot].exchange (false))
            drumKit.trigger (slot, 0.8f, kit);

    // Some hosts occasionally send more samples than they promised in
    // prepareToPlay; work through an oversized block in promised-size pieces.
    for (int start = 0; start < numSamples; start += preparedBlockSize)
        processChunk (buffer, synthMidi, drumMidi, kit, start, juce::jmin (preparedBlockSize, numSamples - start));

    activeVoices = engine.getNumActiveVoices();
    midi.clear();   // an instrument: nothing goes on down the MIDI chain
}

void ParagonX1AudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const juce::MidiBuffer& drums,
                                            const synth::drums::KitSettings& kit, int start, int numSamples)
{
    const auto numChannels = buffer.getNumChannels();
    auto* left = buffer.getWritePointer (0, start);
    // Mono out: render the right side into the scratch end of the buffer's
    // second channel if there is one, else fold it in afterwards.
    float scratchRight[512];
    // THE SENDS (stage 7b): the engine fills each send's mix while its effect is on; silence otherwise.
    const auto sendsReady = rack->sends[0].getNumSamples() >= numSamples;
    // THE INSERT BUSES (stage 7d): which ones an oscillator that's on plays into this block.
    for (auto& bus : rack->buses)
        bus.used = false;
    for (const auto& osc : oscParameters)
        if (osc.values[(size_t) ParamIDs::oscSetting ("On")]->load() > 0.5f)
            if (const auto b = juce::roundToInt (osc.values[(size_t) ParamIDs::oscSetting ("Insert")]->load()); b >= 1 && b <= synth::numInsertBuses)
                rack->buses[(size_t) b - 1].used = true;
    if (kit.on)
        for (const auto& slot : kit.slots)
            if (slot.insertBus >= 1 && slot.insertBus <= synth::numInsertBuses)
                rack->buses[(size_t) slot.insertBus - 1].used = true;
    if (sendsReady)
        for (auto& send : rack->sends)
            send.clear (0, numSamples);
    for (int done = 0; done < numSamples; done += (int) std::size (scratchRight))
    {
        const auto n = juce::jmin ((int) std::size (scratchRight), numSamples - done);
        auto* right = numChannels > 1 ? buffer.getWritePointer (1, start + done) : scratchRight;
        if (sendsReady)
        {
            engine.setSendOutputs ({ rack->sends[0].getWritePointer (0, done), rack->sends[1].getWritePointer (0, done) },
                                   { rack->sends[0].getWritePointer (1, done), rack->sends[1].getWritePointer (1, done) });
            // THE INSERT BUSES (stage 7d): only the ones an oscillator plays into.
            std::array<float*, synth::numInsertBuses> busLeft {}, busRight {};
            for (size_t b = 0; b < rack->buses.size(); ++b)
                if (rack->buses[b].used)
                {
                    busLeft[b] = rack->buses[b].buffer.getWritePointer (0, done);
                    busRight[b] = rack->buses[b].buffer.getWritePointer (1, done);
                }
            engine.setInsertOutputs (busLeft, busRight);
        }
        else
        {
            engine.setSendOutputs ({}, {});
            engine.setInsertOutputs ({}, {});
        }
        auto settings = getSynthSettings();
        // The MOD MATRIX: each route with its knob's position now.
        if (const auto* compiled = matrixForAudio.load())
        {
            settings.pitch.shape = &compiled->pitchEnvelope;
            settings.stepSeq.pattern = &compiled->stepPattern;
            for (size_t i = 0; i < settings.oscillators.size(); ++i)
            {
                settings.oscillators[i].travel.shape = &compiled->travelShapes[i];
                settings.oscillators[i].travel.pattern = &compiled->travelPatterns[i];
            }
            for (size_t k = 0; k < settings.arps.size(); ++k)
            {
                settings.arps[k].patterns = &compiled->arpPatterns[k];
                settings.arps[k].randomizer = &compiled->arpRandomizers[k];
            }
            int count = 0;
            for (const auto& c : compiled->routes)
            {
                auto& r = routeBuffer[(size_t) count++];
                r.source = c.source;
                r.target = c.target;
                r.base = c.parameter->getValue();
                r.range = &c.parameter->getNormalisableRange();
                r.amount = c.amount;
                r.percent = c.percent;
                r.lagMs = c.lagMs;
                r.curve = c.curve;
            }
            settings.modulation.routes = count > 0 ? routeBuffer.data() : nullptr;
            settings.modulation.numRoutes = count;
            settings.modulation.envelopeShapes = compiled->envelopes.data();
            settings.modulation.lfoShapes = compiled->lfoShapes.data();
            settings.modulation.usedSources = compiled->usedSources;
        }
        settings.modulation.ppq = hostPpq >= 0.0 ? hostPpq + (start + done) / getSampleRate() * settings.modulation.bpm / 60.0 : -1.0;
        engine.process (left + done, right, n, midi, start + done, settings);
        // THE DRUMS, added after the synth (which fills the mix, sends and buses afresh).
        if (kit.on || drumKit.getNumActiveVoices() > 0)
        {
            synth::drums::Outputs out;
            out.left = left + done;
            out.right = right;
            if (sendsReady)
            {
                for (size_t s = 0; s < out.sendLeft.size(); ++s)
                {
                    out.sendLeft[s] = rack->sends[s].getWritePointer (0, done);
                    out.sendRight[s] = rack->sends[s].getWritePointer (1, done);
                }
                for (size_t b = 0; b < rack->buses.size(); ++b)
                    if (rack->buses[b].used)
                    {
                        out.busLeft[b] = rack->buses[b].buffer.getWritePointer (0, done);
                        out.busRight[b] = rack->buses[b].buffer.getWritePointer (1, done);
                    }
            }
            auto sequencer = getDrumSeqSettings();
            if (const auto* compiled = matrixForAudio.load())
                sequencer.patterns = &compiled->drumPatterns;
            drumKit.process (out, n, drums, start + done, kit, sequencer, settings.modulation.ppq, juce::jmax (1.0, settings.modulation.bpm));
        }
        if (numChannels == 1)
            for (int i = 0; i < n; ++i)
                left[done + i] = 0.5f * (left[done + i] + scratchRight[i]);
    }

    // THE SENDS' returns, then MOD, MOD 2 and LO-FI (stages 7b, 7c); THE RACK (stage 7a): ECHELON, DOMINION, CONSTRICTOR; then the preset's own level and OUTPUT; then the LIMITER,
    // last, so nothing after it can push a peak past its ceiling (OUTPUT drives into it, as into a mastering
    // limiter); then BYPASS (to silence: there's nothing "clean" to pass through on an instrument).
    const auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock ((size_t) start, (size_t) numSamples)
                                                            .getSubsetChannelBlock (0, (size_t) juce::jmin (numChannels, 2));
    if (sendsReady)
    {
        processInsertBuses (block, true);
        processSends (block);
        processModFx (block);   // stage 7c: after the returns, so the echoes and reverb are part of what they shape
    }
    processRack (block);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::outputGain) + param (ParamIDs::patchLevel)));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue();
        for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
            buffer.getWritePointer (channel, start)[i] *= gain;
    }
    if (rack->compSidechain.getNumSamples() >= numSamples)
        rack->limiter.process (block, getLimiterSettings());
    bypassAmount.setTargetValue (isOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    if (bypassAmount.isSmoothing() || bypassAmount.getTargetValue() > 0.5f)
        for (int i = 0; i < numSamples; ++i)
        {
            const auto gain = 1.0f - bypassAmount.getNextValue();
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
    finishMidiLearn();
    updateWavetables();
    updateDrumReverses();
    updateLatency();
    rack->eqMatch.update (rack->eqInputTap);   // EQ Match listens with its window closed too
    recordCheckpointIfReady();
}

//==============================================================================
void ParagonX1AudioProcessor::updateWavetables()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        const auto isFreeform = juce::roundToInt (param (ParamIDs::osc (i, "Wave").toRawUTF8())) == (int) synth::OscWave::freeform;
        auto wanted = isFreeform ? freeformTables[(size_t) i] : loadedTables[(size_t) i];
        auto factory = -1;
        if (wanted == nullptr && ! isFreeform)
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
    updateSpectra();
    std::erase_if (retiredTables, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    std::erase_if (retiredMatrices, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
    std::erase_if (retiredSamples, [now] (const auto& retired) { return now - retired.second > retiredTableMs; });
}

void ParagonX1AudioProcessor::compileMatrix()
{
    // The routes the audio thread can play: on, with a source, to a knob this version knows;
    // sorted by destination (so routes to one knob add up), otherwise in order.
    auto compiled = std::make_shared<CompiledMatrix>();
    for (const auto& r : matrix.routes)
    {
        const auto source = synth::mod::sourceFromId (r.source);
        const auto* destination = ModDestinations::find (r.destination);
        auto* parameter = destination != nullptr ? parameters.getParameter (destination->parameterId) : nullptr;
        if (! r.on || source == synth::mod::Source::none || parameter == nullptr || juce::exactlyEqual (r.amount, 0.0f))
            continue;
        compiled->routes.push_back ({ source, destination->target, parameter, r.amount, r.lagMs, r.curve, r.percent });
        compiled->usedSources |= 1u << (int) source;
        if ((int) compiled->routes.size() == synth::mod::maxRoutes)
            break;
    }
    for (int k = 0; k < synth::mod::numEnvelopes; ++k)
        compiled->envelopes[(size_t) k] = matrix.envelopes[(size_t) k].toShape();
    for (int k = 0; k < synth::mod::numLfoShapes; ++k)
        compiled->lfoShapes[(size_t) k] = matrix.lfoShapes[(size_t) k].toShape();
    compiled->pitchEnvelope = matrix.pitchEnvelope.toShape();
    compiled->stepPattern = matrix.stepPattern;
    for (size_t i = 0; i < matrix.travel.size(); ++i)
    {
        compiled->travelShapes[i] = matrix.travel[i].envelope.toShape();
        compiled->travelPatterns[i] = matrix.travel[i].pattern;
    }
    std::copy (matrix.arps.begin(), matrix.arps.end(), compiled->arpPatterns.begin());
    compiled->arpRandomizers = matrix.arpRandomizers;
    for (size_t p = 0; p < compiled->drumPatterns.size() && p < matrix.drumPatterns.size(); ++p)
        compiled->drumPatterns[p] = matrix.drumPatterns[p];
    static_assert (ModMatrix::numDrumSlots == synth::drums::numSlots);
    for (size_t k = 0; k < compiled->drumEnvelopes.size() && k < matrix.drumEnvelopes.size(); ++k)
        compiled->drumEnvelopes[k] = matrix.drumEnvelopes[k].toShape();
    std::stable_sort (compiled->routes.begin(), compiled->routes.end(), [] (const CompiledRoute& a, const CompiledRoute& b)
    {
        return std::pair ((int) a.target.field, a.target.index) < std::pair ((int) b.target.field, b.target.index);
    });
    if (compiledMatrix != nullptr)
        retiredMatrices.emplace_back (compiledMatrix, juce::Time::getMillisecondCounterHiRes());
    compiledMatrix = compiled;   // always (the PITCH envelope and the STEP SEQUENCER play without routes)
    matrixForAudio = compiledMatrix.get();
}

void ParagonX1AudioProcessor::setModMatrix (const ModMatrix& newMatrix)
{
    if (matrix == newMatrix)
        return;
    matrix = newMatrix;
    if ((int) matrix.routes.size() > synth::mod::maxRoutes)
        matrix.routes.resize ((size_t) synth::mod::maxRoutes);
    compileMatrix();
    markModified();
}

int ParagonX1AudioProcessor::getNumPlayingRoutes() const noexcept
{
    const auto* compiled = matrixForAudio.load();
    return compiled != nullptr ? (int) compiled->routes.size() : 0;
}

void ParagonX1AudioProcessor::updateSpectra()
{
    // Put finished builds in place first, then ask for any that are out of date.
    installSpectralResults();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        const auto value = [this, i] (const char* what) { return param (ParamIDs::osc (i, what).toRawUTF8()); };
        SpectralKey key;
        key.edit = spectra[(size_t) i];
        key.sweep = value ("FftSweep");
        key.flip = value ("FftFlip") > 0.5f;
        key.vStrength = value ("FftVStrength");
        key.wave = juce::roundToInt (value ("Wave"));
        const auto edited = ! key.edit.isFlat() || key.vStrength > 0.0f;
        const auto isTable = key.wave == (int) synth::OscWave::wavetable || key.wave == (int) synth::OscWave::freeform;
        if (key.wave == (int) synth::OscWave::pulse)
            key.pulseWidth = juce::jlimit (0.01f, 0.99f, value ("Width"));
        const auto& table = playingTables[(size_t) i];
        if (isTable)
            key.samples = table != nullptr ? table->getSource().samples.get() : nullptr;

        if (! edited || (isTable && table == nullptr))
        {
            // Unedited: back to the plain wave.
            spectralKeys[(size_t) i].reset();
            for (auto* list : { &spectralTables, &spectralPulseTables })
                if ((*list)[(size_t) i] != nullptr)
                    retiredTables.emplace_back (std::exchange ((*list)[(size_t) i], nullptr), now);
            spectralForAudio[(size_t) i] = nullptr;
            spectralPulseForAudio[(size_t) i] = nullptr;
            continue;
        }
        if (spectralKeys[(size_t) i].has_value() && *spectralKeys[(size_t) i] == key)
            continue;   // built, or being built
        spectralKeys[(size_t) i] = key;
        SpectralBuilder::Job job;
        job.osc = i;
        job.key = key;
        job.source = isTable ? table->getSource() : synth::spectrum::basicWave ((synth::OscWave) key.wave, key.pulseWidth);
        // Basic waves keep their own level, as their plain tables do; a drawing as its NORMALIZE says.
        job.normalise = key.wave == (int) synth::OscWave::wavetable || (key.wave == (int) synth::OscWave::freeform && drawings[(size_t) i].normalise);
        spectralBuilder->submit (std::move (job));
    }
}

void ParagonX1AudioProcessor::installSpectralResults()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (auto& result : spectralBuilder->collect())
    {
        const auto i = (size_t) result.osc;
        if (! spectralKeys[i].has_value() || ! (*spectralKeys[i] == result.key))
            continue;   // out of date already
        for (auto* list : { &spectralTables, &spectralPulseTables })
            if ((*list)[i] != nullptr)
                retiredTables.emplace_back ((*list)[i], now);
        spectralTables[i] = result.table;
        spectralPulseTables[i] = result.pulse;
        spectralForAudio[i] = result.table.get();
        spectralPulseForAudio[i] = result.pulse.get();
    }
}

void ParagonX1AudioProcessor::finishTableBuilds()
{
    updateWavetables();
    while (! spectralBuilder->isIdle())
        juce::Thread::sleep (2);
    updateWavetables();
}

void ParagonX1AudioProcessor::buildFreeform (int osc)
{
    // A one-frame table from the drawing (about a millisecond: built here, not in the background).
    const auto& drawing = drawings[(size_t) osc];
    freeformTables[(size_t) osc] = synth::Wavetable::build (drawing.source(), nullptr, drawing.normalise);
}

void ParagonX1AudioProcessor::setDrawing (int osc, const synth::FreeformDrawing& drawing)
{
    osc = juce::jlimit (0, ParamIDs::numOscillators - 1, osc);
    if (drawings[(size_t) osc] == drawing)
        return;
    drawings[(size_t) osc] = drawing;
    buildFreeform (osc);
    markModified();
    updateWavetables();
}

void ParagonX1AudioProcessor::setSpectrum (int osc, const synth::SpectrumEdit& edit)
{
    osc = juce::jlimit (0, ParamIDs::numOscillators - 1, osc);
    if (spectra[(size_t) osc] == edit)
        return;
    spectra[(size_t) osc] = edit;
    markModified();
    updateSpectra();
}

synth::WavetableSource ParagonX1AudioProcessor::getPlainSource (int osc)
{
    osc = juce::jlimit (0, ParamIDs::numOscillators - 1, osc);
    const auto wave = (synth::OscWave) juce::roundToInt (param (ParamIDs::osc (osc, "Wave").toRawUTF8()));
    if (wave == synth::OscWave::wavetable || wave == synth::OscWave::freeform)
    {
        const auto table = getWavetable (osc);
        return table != nullptr ? table->getSource() : synth::WavetableSource {};
    }
    return synth::spectrum::basicWave (wave, param (ParamIDs::osc (osc, "Width").toRawUTF8()));
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
    if (juce::roundToInt (param (ParamIDs::osc (osc, "Wave").toRawUTF8())) == (int) synth::OscWave::freeform)
        return freeformTables[(size_t) osc];
    if (loadedTables[(size_t) osc] != nullptr)
        return loadedTables[(size_t) osc];
    return synth::factoryTables::get (juce::roundToInt (param (ParamIDs::osc (osc, "Table").toRawUTF8())));
}

bool ParagonX1AudioProcessor::loadWavetableFile (int osc, const juce::File& file, const synth::wavetableFiles::CutOptions& options)
{
    auto table = synth::Wavetable::build (synth::wavetableFiles::load (file, options));
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

//==============================================================================
// THE DRUMS (stage 6a).
void ParagonX1AudioProcessor::setDrumSample (int slot, std::shared_ptr<const synth::drums::Sample> sample)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    if (drumSamples[(size_t) slot] == sample)
        return;
    if (drumSamples[(size_t) slot] != nullptr)
        retiredSamples.emplace_back (drumSamples[(size_t) slot], juce::Time::getMillisecondCounterHiRes());
    drumSamples[(size_t) slot] = std::move (sample);
    samplesForAudio[(size_t) slot] = drumSamples[(size_t) slot].get();
}

bool ParagonX1AudioProcessor::loadDrumSample (int slot, const juce::File& file)
{
    auto sample = synth::drums::sampleFiles::load (file);
    if (sample == nullptr)
        return false;
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    setDrumSample (slot, std::move (sample));
    // A new file plays whole: START and END go back to its ends.
    for (const auto& [what, value] : { std::pair { "SampleStart", 0.0f }, std::pair { "SampleEnd", 1.0f } })
        if (auto* parameter = parameters.getParameter (ParamIDs::drum (slot, what)))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    // A slot that becomes SAMPLE plays it as recorded: its shaping goes to neutral (a drum synth's DECAY, TONE and SNAP
    // would cut or tilt it). A slot already on SAMPLE keeps the shaping the player gave it.
    auto* model = parameters.getParameter (ParamIDs::drum (slot, "Model"));
    if (model != nullptr && juce::roundToInt (model->convertFrom0to1 (model->getValue())) != (int) synth::drums::Model::sample)
    {
        for (const auto& [what, value] : { std::pair { "Pitch", 0.0f }, std::pair { "Decay", 0.5f }, std::pair { "Tone", 0.5f }, std::pair { "Snap", 0.5f } })
            if (auto* parameter = parameters.getParameter (ParamIDs::drum (slot, what)))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        model->setValueNotifyingHost (model->convertTo0to1 ((float) synth::drums::Model::sample));
    }
    markModified();
    requestCheckpoint();
    return true;
}

void ParagonX1AudioProcessor::clearDrumSample (int slot)
{
    setDrumSample (slot, nullptr);
    markModified();
    requestCheckpoint();
}

std::shared_ptr<const synth::drums::Sample> ParagonX1AudioProcessor::getDrumSample (int slot) const
{
    return drumSamples[(size_t) juce::jlimit (0, ParamIDs::numDrums - 1, slot)];
}

void ParagonX1AudioProcessor::previewDrum (int slot) noexcept
{
    if (slot >= 0 && slot < ParamIDs::numDrums)
        pendingPreview[(size_t) slot] = true;
}

synth::drums::KitSettings ParagonX1AudioProcessor::getKitSettings() const noexcept
{
    synth::drums::KitSettings kit;
    kit.on = isOn (ParamIDs::drumsOn);
    kit.rootKey = juce::roundToInt (param (ParamIDs::drumsRoot));
    kit.velocity = param (ParamIDs::drumsVelocity);
    kit.gain = juce::Decibels::decibelsToGain (param (ParamIDs::drumsLevel));
    kit.sendOn = { isOn (ParamIDs::delayOn), isOn (ParamIDs::reverbOn) };
    for (size_t k = 0; k < kit.slots.size(); ++k)
    {
        const auto& v = drumParameters[k].values;
        const auto get = [&v] (int index) { return v[(size_t) index]->load(); };
        using ParamIDs::drumSetting;
        auto& s = kit.slots[k];
        s.model = (synth::drums::Model) juce::jlimit (0, synth::drums::modelNames.size() - 1, juce::roundToInt (get (drumSetting ("Model"))));
        const auto level = get (drumSetting ("Level"));
        s.gain = level <= -47.95f ? 0.0f : juce::Decibels::decibelsToGain (level);
        s.pan = get (drumSetting ("Pan"));
        s.pitch = get (drumSetting ("Pitch"));
        s.decay = get (drumSetting ("Decay"));
        s.tone = get (drumSetting ("Tone"));
        s.snap = get (drumSetting ("Snap"));
        s.mute = get (drumSetting ("Mute")) > 0.5f;
        s.solo = get (drumSetting ("Solo")) > 0.5f;
        s.choke = juce::roundToInt (get (drumSetting ("Choke")));
        s.insertBus = juce::roundToInt (get (drumSetting ("Insert")));
        s.send = { get (drumSetting ("SendDelay")), get (drumSetting ("SendReverb")) };
        s.sample = samplesForAudio[k].load();
        s.start = get (drumSetting ("SampleStart"));
        s.end = get (drumSetting ("SampleEnd"));
        // REVERSE: a SAMPLE slot plays its own file backwards; a synth drum its rendered hit.
        s.reverseBaked = s.model != synth::drums::Model::sample;
        s.reverseSource = s.reverseBaked ? reversesForAudio[k].load() : s.sample;
        // THE SAMPLE EDITOR (6c).
        s.trim = juce::Decibels::decibelsToGain (get (drumSetting ("Gain")));
        s.mono = get (drumSetting ("Mono")) > 0.5f;
        s.delay = get (drumSetting ("Delay")) * 0.001f;
        s.lowPass = get (drumSetting ("LowPass"));
        s.highPass = get (drumSetting ("HighPass"));
        if (const auto* compiled = matrixForAudio.load())
            s.envelope = &compiled->drumEnvelopes[k];
        s.envelopeSeconds = hitLengths[k].load();
    }
    return kit;
}

synth::drumseq::Settings ParagonX1AudioProcessor::getDrumSeqSettings() const noexcept
{
    synth::drumseq::Settings s;
    s.on = isOn (ParamIDs::dseqOn);
    s.trigger = (synth::drumseq::Trigger) juce::jlimit (0, 1, juce::roundToInt (param (ParamIDs::dseqTrigger)));
    s.pattern = juce::jlimit (0, synth::drumseq::numPatterns - 1, juce::roundToInt (param (ParamIDs::dseqPattern)));
    s.division = juce::jlimit (0, synth::seq::divisionNames.size() - 1, juce::roundToInt (param (ParamIDs::dseqDivision)));
    s.timing = (synth::seq::Timing) juce::jlimit (0, synth::seq::timingNames.size() - 1, juce::roundToInt (param (ParamIDs::dseqTiming)));
    s.lengthBeats = juce::jlimit (1, synth::drumseq::maxBeats, juce::roundToInt (param (ParamIDs::dseqLength)));
    s.shuffle = param (ParamIDs::shuffle);
    s.velocityAmount = param (ParamIDs::dseqVelocity);
    return s;
}

void ParagonX1AudioProcessor::updateDrumReverses()
{
    // Which synth drums a reversed note plays.
    std::array<bool, ParamIDs::numDrums> wanted {};
    for (const auto& pattern : matrix.drumPatterns)
        for (const auto& n : pattern.notes)
            if (n.reverse != synth::drumseq::Reverse::off)
                wanted[(size_t) juce::jlimit (0, ParamIDs::numDrums - 1, n.row)] = true;
    const auto kit = getKitSettings();
    const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (size_t k = 0; k < (size_t) ParamIDs::numDrums; ++k)
    {
        // The hit alone: VOLUME, GAIN, LP / HP and the envelope are put on as it plays (the envelope by where it is
        // in the hit), so none of them go into the recording (before 6c, VOLUME did, and counted twice).
        auto s = kit.slots[k];
        s.gain = s.trim = 1.0f;
        s.envelope = nullptr;
        s.lowPass = 20000.0f;
        s.highPass = 20.0f;
        s.delay = 0.0f;

        // The drum synths' hit lengths, which their drawn envelopes span (only measured for one that's drawn).
        juce::String lengthKey;
        if (s.model != synth::drums::Model::sample && synth::drums::envelopeShapes (kit.slots[k].envelope))
            lengthKey << (int) s.model << ":" << s.pitch << ":" << s.decay << ":" << s.tone << ":" << s.snap;
        if (lengthKey != hitLengthKeys[k])
        {
            hitLengthKeys[k] = lengthKey;
            hitLengths[k] = lengthKey.isNotEmpty() ? (float) synth::drums::Kit::hitSeconds (s) : 0.0f;
        }

        juce::String key;
        if (wanted[k] && s.model != synth::drums::Model::sample)
            key << (int) s.model << ":" << s.pitch << ":" << s.decay << ":" << s.tone << ":" << s.snap << ":" << rate;
        if (key == reverseKeys[k])
            continue;
        reverseKeys[k] = key;
        std::shared_ptr<synth::drums::Sample> hit;
        if (key.isNotEmpty())
        {
            // The hit at full velocity (its level, pitch and shaping in it), to where it has died away (-60 dB).
            const auto rendered = synth::drums::Kit::renderHit (s, 1.0f, rate, 4.0);
            const auto* x = rendered.getReadPointer (0);
            auto peak = 0.0f;
            for (int i = 0; i < rendered.getNumSamples(); ++i)
                peak = juce::jmax (peak, std::abs (x[i]));
            auto length = rendered.getNumSamples();
            while (length > 3 && std::abs (x[length - 1]) < peak * 0.001f)
                --length;
            hit = std::make_shared<synth::drums::Sample>();
            hit->name = "reversed";
            hit->sampleRate = rate;
            hit->channels = { std::vector<float> (x, x + juce::jmax (3, length)) };
        }
        if (reverseHits[k] != nullptr)
            retiredSamples.emplace_back (reverseHits[k], now);
        reverseHits[k] = hit;
        reversesForAudio[k] = hit.get();
    }
}

//==============================================================================
// KITS AND SEQUENCES (6c part 2).
namespace
{
    // What each file holds: which parameters (the rest of the sound is untouched).
    bool isKitParameter (const juce::String& id)
    {
        // The kit's LEVEL and VELOCITY, and every slot's "drum<n>..." setting (not DRUMS ON or ROOT: how it's played).
        return id == ParamIDs::drumsLevel || id == ParamIDs::drumsVelocity
            || (id.startsWith ("drum") && juce::CharacterFunctions::isDigit (id[4]));
    }
    const juce::StringArray sequenceParameters { ParamIDs::dseqDivision, ParamIDs::dseqTiming, ParamIDs::dseqLength, ParamIDs::dseqVelocity };

    juce::String cleanName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim()).substring (0, 64);
    }

    juce::Array<juce::File> filesIn (const juce::File& folder, const juce::String& pattern)
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, pattern);
        files.sort();
        return files;
    }
}

juce::File ParagonX1AudioProcessor::drumKitsFolder() const { return rigLibrary->getFolder().getChildFile ("Drum Kits"); }
juce::File ParagonX1AudioProcessor::drumSequencesFolder() const { return rigLibrary->getFolder().getChildFile ("Drum Sequences"); }
juce::Array<juce::File> ParagonX1AudioProcessor::listDrumKits() const { return filesIn (drumKitsFolder(), "*.pr7kit"); }
juce::Array<juce::File> ParagonX1AudioProcessor::listDrumSequences() const { return filesIn (drumSequencesFolder(), "*.pr7seq"); }

std::unique_ptr<juce::XmlElement> ParagonX1AudioProcessor::captureDrumKit() const
{
    auto xml = std::make_unique<juce::XmlElement> ("DrumKit");
    xml->setAttribute ("version", 1);
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && isKitParameter (ranged->paramID))
        {
            auto* e = xml->createNewChildElement ("Param");
            e->setAttribute ("id", ranged->paramID);
            e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
        }
    for (int k = 0; k < ParamIDs::numDrums; ++k)
    {
        if (const auto& sample = drumSamples[(size_t) k])
        {
            auto* e = xml->createNewChildElement ("DrumSample");
            e->setAttribute ("slot", k + 1);
            e->setAttribute ("name", sample->name);
            if (sample->path.isNotEmpty())
                e->setAttribute ("path", sample->path);
            e->addTextElement (synth::drums::sampleFiles::encode (*sample));
        }
        const auto& envelope = matrix.drumEnvelopes[(size_t) k];
        if (! (envelope == ModShapeData::flatDrumEnvelope()))
        {
            auto* e = xml->createNewChildElement ("DrumEnvelope");
            e->setAttribute ("index", k + 1);
            e->setAttribute ("points", envelope.encode());
        }
    }
    return xml;
}

std::unique_ptr<juce::XmlElement> ParagonX1AudioProcessor::captureDrumSequence() const
{
    auto xml = std::make_unique<juce::XmlElement> ("DrumSequence");
    xml->setAttribute ("version", 1);
    for (const auto& id : sequenceParameters)
        if (auto* ranged = parameters.getParameter (id))
        {
            auto* e = xml->createNewChildElement ("Param");
            e->setAttribute ("id", id);
            e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
        }
    for (int p = 0; p < (int) matrix.drumPatterns.size(); ++p)
    {
        auto* e = xml->createNewChildElement ("DrumPattern");
        e->setAttribute ("pattern", synth::drumseq::patternNames[p]);
        e->setAttribute ("notes", matrix.drumPatterns[(size_t) p].encode());
    }
    return xml;
}

bool ParagonX1AudioProcessor::applyDrumKit (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("DrumKit"))
        return false;
    // Every kit parameter: what the file says, the rest of the kit's to its default (as a preset does).
    std::map<juce::String, float> values;
    for (const auto* e : xml.getChildWithTagNameIterator ("Param"))
        values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
    for (auto* parameter : AudioProcessor::getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && isKitParameter (ranged->paramID))
        {
            const auto found = values.find (ranged->paramID);
            ranged->setValueNotifyingHost (found != values.end() ? ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue (found->second))
                                                                 : ranged->getDefaultValue());
        }
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> samples {};
    for (const auto* e : xml.getChildWithTagNameIterator ("DrumSample"))
        if (const auto slot = e->getIntAttribute ("slot") - 1; slot >= 0 && slot < ParamIDs::numDrums)
            if (auto sample = synth::drums::sampleFiles::decode (e->getStringAttribute ("name"), e->getAllSubText()))
            {
                std::const_pointer_cast<synth::drums::Sample> (sample)->path = e->getStringAttribute ("path");
                samples[(size_t) slot] = std::move (sample);
            }
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        setDrumSample (k, samples[(size_t) k]);
    auto m = matrix;
    for (auto& envelope : m.drumEnvelopes)
        envelope = ModShapeData::flatDrumEnvelope();
    for (const auto* e : xml.getChildWithTagNameIterator ("DrumEnvelope"))
        if (const auto index = e->getIntAttribute ("index") - 1; index >= 0 && index < ModMatrix::numDrumSlots)
            if (auto d = ModShapeData::decode (e->getStringAttribute ("points"), -1, -1, -1, true); d.points.size() >= 2)
                m.drumEnvelopes[(size_t) index] = d;
    setModMatrix (m);
    markModified();
    requestCheckpoint();
    return true;
}

bool ParagonX1AudioProcessor::applyDrumSequence (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("DrumSequence"))
        return false;
    for (const auto* e : xml.getChildWithTagNameIterator ("Param"))
        if (const auto id = e->getStringAttribute ("id"); sequenceParameters.contains (id))
            if (auto* ranged = parameters.getParameter (id))
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (ranged->getNormalisableRange().snapToLegalValue ((float) e->getDoubleAttribute ("value"))));
    auto m = matrix;
    for (auto& pattern : m.drumPatterns)
        pattern = {};
    for (const auto* e : xml.getChildWithTagNameIterator ("DrumPattern"))
        if (const auto p = synth::drumseq::patternNames.indexOf (e->getStringAttribute ("pattern")); p >= 0)
            m.drumPatterns[(size_t) p] = synth::drumseq::Pattern::decode (e->getStringAttribute ("notes"));
    setModMatrix (m);
    markModified();
    requestCheckpoint();
    return true;
}

bool ParagonX1AudioProcessor::saveDrumKit (const juce::String& name)
{
    const auto clean = cleanName (name);
    if (clean.isEmpty())
        return false;
    drumKitsFolder().createDirectory();
    if (! captureDrumKit()->writeTo (drumKitsFolder().getChildFile (clean + ".pr7kit")))
        return false;
    drumKitName = clean;
    if (drumLink && ! saveDrumSequenceOnly (clean))
        return false;
    return true;
}

bool ParagonX1AudioProcessor::saveDrumSequence (const juce::String& name)
{
    const auto clean = cleanName (name);
    if (clean.isEmpty() || ! saveDrumSequenceOnly (clean))
        return false;
    if (drumLink)
    {
        drumKitsFolder().createDirectory();
        if (! captureDrumKit()->writeTo (drumKitsFolder().getChildFile (clean + ".pr7kit")))
            return false;
        drumKitName = clean;
    }
    return true;
}

bool ParagonX1AudioProcessor::saveDrumSequenceOnly (const juce::String& clean)
{
    drumSequencesFolder().createDirectory();
    if (! captureDrumSequence()->writeTo (drumSequencesFolder().getChildFile (clean + ".pr7seq")))
        return false;
    drumSequenceName = clean;
    return true;
}

bool ParagonX1AudioProcessor::loadDrumKit (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! applyDrumKit (*xml))
        return false;
    drumKitName = file.getFileNameWithoutExtension();
    if (drumLink)
        if (const auto partner = drumSequencesFolder().getChildFile (drumKitName + ".pr7seq"); partner.existsAsFile())
            if (const auto seq = juce::XmlDocument::parse (partner); seq != nullptr && applyDrumSequence (*seq))
                drumSequenceName = drumKitName;
    return true;
}

bool ParagonX1AudioProcessor::loadDrumSequence (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! applyDrumSequence (*xml))
        return false;
    drumSequenceName = file.getFileNameWithoutExtension();
    if (drumLink)
        if (const auto partner = drumKitsFolder().getChildFile (drumSequenceName + ".pr7kit"); partner.existsAsFile())
            if (const auto kit = juce::XmlDocument::parse (partner); kit != nullptr && applyDrumKit (*kit))
                drumKitName = drumSequenceName;
    return true;
}

bool ParagonX1AudioProcessor::loadNeighbourSample (int slot, int direction)
{
    slot = juce::jlimit (0, ParamIDs::numDrums - 1, slot);
    const auto& sample = drumSamples[(size_t) slot];
    if (sample == nullptr || sample->path.isEmpty())
        return false;
    const juce::File current (sample->path);
    auto files = current.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    if (files.isEmpty())
        return false;
    // In natural order ("Kick 2" before "Kick 10"), as sample packs number their files.
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    auto at = files.indexOf (current);
    if (at < 0)
    {
        // The file's gone or renamed: the first by name after where it was.
        at = 0;
        while (at < files.size() && files[at].getFileName().compareNatural (current.getFileName()) < 0)
            ++at;
        at -= direction > 0 ? 1 : 0;
    }
    const auto next = ((at + (direction > 0 ? 1 : -1)) % files.size() + files.size()) % files.size();
    return loadDrumSample (slot, files[next]);
}

juce::MidiFile ParagonX1AudioProcessor::drumPatternToMidi (int pattern) const
{
    const auto p = (size_t) juce::jlimit (0, synth::drumseq::numPatterns - 1, pattern);
    // Drums with an anchor (6c) written early, as the sequencer plays them.
    const auto kit = getKitSettings();
    std::array<double, synth::drumseq::numRows> lead {};
    for (size_t k = 0; k < lead.size(); ++k)
        lead[k] = kit.slots[k].anchorSeconds();
    return synth::drumseq::toMidi (matrix.drumPatterns[p], getDrumSeqSettings(), juce::roundToInt (param (ParamIDs::drumsRoot)),
                                   juce::jmax (1.0, getHostBpm()), lead);
}

bool ParagonX1AudioProcessor::exportDrumMidi (int pattern, const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;
    out.setPosition (0);
    out.truncate();
    return drumPatternToMidi (pattern).writeTo (out);
}

juce::File ParagonX1AudioProcessor::drumMidiDragFile (int pattern) const
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Paragon PR7-K")
                    .getChildFile ("Drums " + synth::drumseq::patternNames[juce::jlimit (0, 3, pattern)] + ".mid");
    exportDrumMidi (pattern, file);
    return file;
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
    rack->setFolder (folder);
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
    {
        if (const auto& table = loadedTables[(size_t) i])
            rig.wavetables[i] = table->getSource();
        if (! spectra[(size_t) i].isFlat())
            rig.spectra[i] = spectra[(size_t) i];
        if (! (drawings[(size_t) i] == synth::FreeformDrawing {}))
            rig.drawings[i] = drawings[(size_t) i];
    }
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        if (drumSamples[(size_t) k] != nullptr)
            rig.drumSamples[k] = drumSamples[(size_t) k];
    rig.modulation = matrix;
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

    for (int k = 0; k < ParamIDs::numDrums; ++k)
    {
        const auto found = rig.drumSamples.find (k);
        setDrumSample (k, found != rig.drumSamples.end() ? found->second : nullptr);
    }
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        const auto found = rig.wavetables.find (i);
        loadedTables[(size_t) i] = found != rig.wavetables.end() ? tableFor (found->second) : nullptr;
        const auto spectrum = rig.spectra.find (i);
        spectra[(size_t) i] = spectrum != rig.spectra.end() ? spectrum->second : synth::SpectrumEdit {};
        const auto drawing = rig.drawings.find (i);
        const auto newDrawing = drawing != rig.drawings.end() ? drawing->second : synth::FreeformDrawing {};
        if (! (drawings[(size_t) i] == newDrawing) || freeformTables[(size_t) i] == nullptr)
        {
            drawings[(size_t) i] = newDrawing;
            buildFreeform (i);
        }
    }
    updateWavetables();
    if (! (matrix == rig.modulation))
    {
        matrix = rig.modulation;
        compileMatrix();
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

    juce::ValueTree kitSamples (drumSamplesKey);
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        if (const auto& sample = drumSamples[(size_t) k])
            kitSamples.appendChild (juce::ValueTree (drumSampleKey, { { "slot", k + 1 }, { "name", sample->name }, { "path", sample->path },
                                                                    { "data", synth::drums::sampleFiles::encode (*sample) } }), nullptr);
    state.appendChild (kitSamples, nullptr);

    juce::ValueTree curves (spectraKey);
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        if (! spectra[(size_t) i].isFlat())
            curves.appendChild (juce::ValueTree (spectrumKey, { { "osc", i + 1 }, { "gains", spectra[(size_t) i].encode() } }), nullptr);
    state.appendChild (curves, nullptr);

    juce::ValueTree drawn (drawingsKey);
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        if (! (drawings[(size_t) i] == synth::FreeformDrawing {}))
            drawn.appendChild (juce::ValueTree (drawingKey, { { "osc", i + 1 }, { "drawing", drawings[(size_t) i].encode() } }), nullptr);
    state.appendChild (drawn, nullptr);
    if (! matrix.isEmpty())
        state.appendChild (juce::ValueTree::fromXml (*matrix.toXml()), nullptr);
    if (! midiLinks.isEmpty())
        state.appendChild (juce::ValueTree::fromXml (*midiLinks.toXml()), nullptr);

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
    std::array<std::shared_ptr<const synth::drums::Sample>, ParamIDs::numDrums> kitSamples {};
    if (auto saved = state.getChildWithName (drumSamplesKey); saved.isValid())
    {
        for (const auto& sample : saved)
        {
            const auto slot = (int) sample["slot"] - 1;
            if (slot >= 0 && slot < ParamIDs::numDrums)
            {
                kitSamples[(size_t) slot] = synth::drums::sampleFiles::decode (sample["name"].toString(), sample["data"].toString());
                if (kitSamples[(size_t) slot] != nullptr)
                    std::const_pointer_cast<synth::drums::Sample> (kitSamples[(size_t) slot])->path = sample["path"].toString();
            }
        }
        state.removeChild (saved, nullptr);
    }
    std::array<synth::SpectrumEdit, ParamIDs::numOscillators> curves {};
    if (auto saved = state.getChildWithName (spectraKey); saved.isValid())
    {
        for (const auto& curve : saved)
        {
            const auto osc = (int) curve["osc"] - 1;
            if (osc >= 0 && osc < ParamIDs::numOscillators)
                curves[(size_t) osc] = synth::SpectrumEdit::decode (curve["gains"].toString());
        }
        state.removeChild (saved, nullptr);
    }
    std::array<synth::FreeformDrawing, ParamIDs::numOscillators> newDrawings {};
    if (auto saved = state.getChildWithName (drawingsKey); saved.isValid())
    {
        for (const auto& drawing : saved)
        {
            const auto osc = (int) drawing["osc"] - 1;
            if (osc >= 0 && osc < ParamIDs::numOscillators)
                newDrawings[(size_t) osc] = synth::FreeformDrawing::decode (drawing["drawing"].toString());
        }
        state.removeChild (saved, nullptr);
    }
    ModMatrix newMatrix;
    if (auto saved = state.getChildWithName (modulationKey); saved.isValid())
    {
        if (const auto xml = saved.createXml())
            newMatrix = ModMatrix::fromXml (*xml);
        state.removeChild (saved, nullptr);
    }
    // MIDI LEARN's links: the project's, if it has any (else the ones this instance has: its default).
    if (auto saved = state.getChildWithName (midiLinksKey); saved.isValid())
    {
        if (const auto links = saved.createXml())
            setMidiLinks (MidiLinks::fromXml (*links));
        state.removeChild (saved, nullptr);
    }
    parameters.replaceState (state);
    matrix = newMatrix;
    compileMatrix();
    spectra = curves;
    drawings = newDrawings;
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        buildFreeform (i);
    loadedTables = tables;
    updateWavetables();
    for (int k = 0; k < ParamIDs::numDrums; ++k)
        setDrumSample (k, kitSamples[(size_t) k]);

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

//==============================================================================
// MIDI LEARN (stage 4f)
void ParagonX1AudioProcessor::setMidiLinks (const MidiLinks& links)
{
    midiLinks = links;
    std::array<juce::RangedAudioParameter*, 128> targets {};
    for (const auto& l : midiLinks.links)
        if (auto* p = parameters.getParameter (l.parameterId))
            targets[(size_t) l.controller] = p;
    for (size_t cc = 0; cc < targets.size(); ++cc)
        controllerTargets[cc] = targets[cc];
}

void ParagonX1AudioProcessor::finishMidiLearn()
{
    // A controller moved since a knob asked to learn: link them.
    if (learning.isNotEmpty() && controllerCount.load() != learnFromCount && lastController.load() >= 0)
    {
        auto links = midiLinks;
        links.link (lastController.load(), learning);
        learning = {};
        setMidiLinks (links);
    }
}

void ParagonX1AudioProcessor::startMidiLearn (const juce::String& parameterId)
{
    learning = parameterId;
    learnFromCount = controllerCount.load();
}

juce::File ParagonX1AudioProcessor::midiLinksDefaultFile() const
{
    return rigLibrary->getFolder().getChildFile ("MIDI Links.xml");
}

bool ParagonX1AudioProcessor::saveMidiLinksAsDefault() const
{
    return exportMidiLinks (midiLinksDefaultFile());
}

bool ParagonX1AudioProcessor::exportMidiLinks (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    return midiLinks.toXml()->writeTo (file);
}

juce::MidiFile ParagonX1AudioProcessor::arpToMidi (int arp, juce::Array<int> keys) const
{
    constexpr int ticksPerBeat = 960;
    arp = juce::jlimit (0, synth::arp::numArps - 1, arp);
    auto settings = getSynthSettings();
    auto s = settings.arps[(size_t) arp];
    const auto& patterns = matrix.arps[(size_t) arp];
    const auto& randomizer = matrix.arpRandomizers[(size_t) arp];

    if (keys.isEmpty())
        for (int n = 0; n < 128; ++n)
            if (keyboardState.isNoteOnForChannels (0xffff, n))
                keys.add (n);
    if (keys.isEmpty())
        keys.add (60);
    keys.sort();
    std::array<synth::arp::Held, synth::arp::maxHeld> held {};
    const auto numHeld = juce::jmin (keys.size(), synth::arp::maxHeld);
    for (int i = 0; i < numHeld; ++i)
        held[(size_t) i] = { keys[i], 0.8f, i };
    int noteCounter = 0;
    synth::arp::KeysState state { held.data(), numHeld, &noteCounter, 0.8f, 0.8f };

    const auto length = juce::jlimit (2, synth::arp::maxSteps, s.length);
    auto cycles = 1;
    if (s.chain)
    {
        cycles = 0;
        for (const auto& p : patterns.patterns)
            cycles += p.isEmpty() ? 0 : 1;
        cycles = juce::jmax (1, cycles);
    }
    const auto dice = randomizer.autoDice && ! s.chain;
    if (dice)
        cycles = 4;

    juce::MidiMessageSequence track;
    const auto bpm = juce::jmax (1.0, getHostBpm());
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0.0);
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "Arp " + juce::String (arp + 1)), 0.0);
    const auto ticksPerStep = s.beatsPerStep() * ticksPerBeat;
    const auto shift = 0.5 * juce::jlimit (0.0, 1.0, (double) settings.stepSeq.shuffle);
    juce::Random random (1);
    synth::arp::Pattern live;
    std::array<synth::arp::Played, synth::arp::maxPlayed> played {};
    for (std::int64_t step = 0; step < (std::int64_t) length * cycles; ++step)
    {
        if (dice && step % length == 0)
            live = synth::arp::roll (randomizer, length, patterns.patterns[(size_t) juce::jlimit (0, 3, s.pattern)],
                                     (juce::int64) randomizer.seed * 7919 + step / length + 1);
        const auto& block = synth::arp::blockFor (s, patterns, step, dice ? &live : nullptr);
        const auto at = ((double) step + ((step & 1) != 0 ? shift : 0.0)) * ticksPerStep;
        const auto count = synth::arp::playBlock (s, block, ticksPerStep, state, random, played.data(), (int) played.size());
        for (int i = 0; i < count; ++i)
        {
            const auto& p = played[(size_t) i];
            const auto start = at + p.start;
            // A slide: in MIDI, the note held on to overlap the next one (most synths glide on overlapping notes).
            const auto end = p.slide ? at + p.end + 0.25 * ticksPerStep : at + p.end;
            track.addEvent (juce::MidiMessage::noteOn (1, p.note, juce::jlimit (0.01f, 1.0f, p.velocity)), std::round (start));
            track.addEvent (juce::MidiMessage::noteOff (1, p.note), juce::jmax (std::round (start) + 1.0, std::round (end)));
        }
    }
    track.addEvent (juce::MidiMessage::endOfTrack(), std::round ((double) length * cycles * ticksPerStep));
    track.updateMatchedPairs();
    track.sort();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerBeat);
    file.addTrack (track);
    return file;
}

bool ParagonX1AudioProcessor::exportArpMidi (int arp, const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;
    out.setPosition (0);
    out.truncate();
    return arpToMidi (arp).writeTo (out);
}

juce::File ParagonX1AudioProcessor::arpMidiDragFile (int arp) const
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("Paragon PR7-K").getChildFile ("Arp " + juce::String (arp + 1) + ".mid");
    exportArpMidi (arp, file);
    return file;
}

bool ParagonX1AudioProcessor::importMidiLinks (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("MidiLinks"))
        return false;
    setMidiLinks (MidiLinks::fromXml (*xml));
    return true;
}

void ParagonX1AudioProcessor::controllerForTest (int controller, float value)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, controller, juce::roundToInt (value * 127.0f)), 0);
    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    processBlock (buffer, midi);
}
