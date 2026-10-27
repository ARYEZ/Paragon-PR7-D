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
    const juce::Identifier wavetableKey { "Wavetable" };
    constexpr double retiredTableMs = 2000.0;   // how long a replaced table is kept, in case a block is still playing it
    const juce::Identifier spectraKey { "Spectra" };
    const juce::Identifier spectrumKey { "Spectrum" };
    const juce::Identifier drawingsKey { "Drawings" };
    const juce::Identifier drawingKey { "Drawing" };
    const juce::Identifier modulationKey { "Modulation" };
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
    for (int k = 0; k < ParamIDs::numLfos; ++k)
        for (size_t s = 0; s < std::size (ParamIDs::lfoSettings); ++s)
        {
            lfoParameters[(size_t) k].values[s] = parameters.getRawParameterValue (ParamIDs::lfo (k, ParamIDs::lfoSettings[s]));
            jassert (lfoParameters[(size_t) k].values[s] != nullptr);
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
        for (int k = 0; k < synth::numShapers; ++k)
            o.toShaper[(size_t) k] = get (oscSetting ("Shp1") + k) > 0.5f;
        o.pitchEnvelope = get (oscSetting ("PitchEnv")) > 0.5f;

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

    // The on-screen keyboard's notes join the DAW's.
    keyboardMidi.clear();
    keyboardMidi.addEvents (midi, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (keyboardMidi, 0, numSamples, true);
    if (const auto wheel = pendingModWheel.exchange (-1.0f); wheel >= 0.0f)
        keyboardMidi.addEvent (juce::MidiMessage::controllerEvent (1, 1, juce::roundToInt (wheel * 127.0f)), 0);

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
        auto settings = getSynthSettings();
        // The MOD MATRIX: each route with its knob's position now.
        if (const auto* compiled = matrixForAudio.load())
        {
            settings.pitch.shape = &compiled->pitchEnvelope;
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
    std::stable_sort (compiled->routes.begin(), compiled->routes.end(), [] (const CompiledRoute& a, const CompiledRoute& b)
    {
        return std::pair ((int) a.target.field, a.target.index) < std::pair ((int) b.target.field, b.target.index);
    });
    if (compiledMatrix != nullptr)
        retiredMatrices.emplace_back (compiledMatrix, juce::Time::getMillisecondCounterHiRes());
    compiledMatrix = compiled;   // always (the PITCH envelope plays without routes)
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
    {
        if (const auto& table = loadedTables[(size_t) i])
            rig.wavetables[i] = table->getSource();
        if (! spectra[(size_t) i].isFlat())
            rig.spectra[i] = spectra[(size_t) i];
        if (! (drawings[(size_t) i] == synth::FreeformDrawing {}))
            rig.drawings[i] = drawings[(size_t) i];
    }
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
    parameters.replaceState (state);
    matrix = newMatrix;
    compileMatrix();
    spectra = curves;
    drawings = newDrawings;
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
        buildFreeform (i);
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
