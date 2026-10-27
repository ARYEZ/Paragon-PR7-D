#include "Engine.h"

#include "WaveTables.h"
#include "WaveBend.h"
#include "Wavetable.h"

namespace synth
{
    namespace
    {
        constexpr float killMs = 3.0f;          // a stolen note's fade
        constexpr float oscFadeOutMs = 5.0f;    // an oscillator taken by a newer note (its VOICES limit)
        constexpr float spikeMs = 20.0f;        // the AMP's SPIKE: how long the extra click lasts (our own choice)
        constexpr float spikeMaxGain = 3.0f;    // ...and how much it adds at most: up to 4x, +12 dB (our own choice)
        constexpr float filterEnvelopeOctaves = 8.0f;   // the FILTER's ENV at +-100% moves the cutoff this far
        constexpr float filterVelocityOctaves = 4.0f;   // the FILTER's VELOCITY at 100% closes the softest notes this far

        float pitchToHz (float semitones) noexcept { return 440.0f * std::exp2 ((semitones - 69.0f) / 12.0f); }

        // PAN as a balance: the centre leaves both sides at full level, so a
        // centred sound is as loud as it would be in mono.
        void balance (float pan, float& left, float& right) noexcept
        {
            pan = juce::jlimit (-1.0f, 1.0f, pan);
            left = pan > 0.0f ? 1.0f - pan : 1.0f;
            right = pan < 0.0f ? 1.0f + pan : 1.0f;
        }

        float wrap (float phase) noexcept { return phase - std::floor (phase); }

        // What an oscillator reads this interval: one of the basic waves, or
        // two frames of its wavetable and how far between them it is (moving
        // from mixFrom to mixTo across the interval, so INDEX glides smoothly).
        struct WaveSource
        {
            OscWave wave = OscWave::saw;
            float pulseWidth = 0.5f;
            const Wavetable* table = nullptr;
            int below = 0, above = 0;
            float mixFrom = 0.0f, mixTo = 0.0f;
            bool oneFrame = true;

            // FFT: VSTRENGTH's other copy, and how far towards it (start and end of the interval).
            const Wavetable* partner = nullptr;
            float partnerFrom = 0.0f, partnerTo = 0.0f;
            // FFT LP and HP: where their cutoffs sit among the table's levels
            // (fractional, 0 = 512 harmonics, 3 an octave fewer...; below 0: off),
            // and how many levels SLOPE spreads them over.
            float lpPosition = -1.0f, hpPosition = -1.0f, slopeWidth = 1.0f;
            bool needsGeneralPath() const noexcept { return partner != nullptr || lpPosition >= 0.0f || hpPosition >= 0.0f; }
        };

        // A cutoff among the levels: blend level `lo` (more harmonics) into
        // `hi` (fewer) by `t`. Never richer than `minimum` (the level that
        // keeps the note clean).
        struct LevelBlend
        {
            int lo = 0, hi = 0;
            float t = 0.0f;
        };
        LevelBlend blendFor (float position, float width, int minimum) noexcept
        {
            const auto span = juce::jmax (1, juce::roundToInt (width));
            auto lo = (int) std::floor (position - 0.5f * (float) (span - 1));
            auto hi = lo + span;
            const auto t = juce::jlimit (0.0f, 1.0f, (position - (float) lo) / (float) span);
            lo = juce::jlimit (minimum, tables::numLevels - 1, lo);
            hi = juce::jlimit (minimum, tables::numLevels - 1, hi);
            return { lo, hi, t };
        }

        // A wavetable's two frames at `level`, blended, for `n` samples: calls
        // `write (i, value)` for each.
        template <typename Write>
        void readTable (const WaveSource& source, int level, const float* positions, int n, Write write) noexcept
        {
            const auto below = source.table->get (source.below, level);
            if (source.oneFrame)
            {
                for (int i = 0; i < n; ++i)
                    write (i, Wavetable::read (below, positions[i]));
                return;
            }
            const auto above = source.table->get (source.above, level);
            const auto step = (source.mixTo - source.mixFrom) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const auto a = Wavetable::read (below, positions[i]);
                const auto b = Wavetable::read (above, positions[i]);
                write (i, a + (b - a) * (source.mixFrom + step * (float) (i + 1)));
            }
        }

        // SYNC, FM and AM for one oscillator this interval (OscModulation.h).
        struct Modulation
        {
            bool active = false;
            bool sync = false, syncAdd = false, syncWindowed = false;
            float syncFrom = 1.0f, syncTo = 1.0f;   // how many times faster SYNC's copy runs, at the interval's start and end
            float fmDepth = 0.0f;                   // cycles
            const float* fmSignal = nullptr;        // another oscillator this interval, or null: fmShape
            ModShape fmShape = ModShape::sine;
            float amDepth = 0.0f;
            ModShape amShape = ModShape::sine;
            float ratio = 1.0f;                     // the modulator's rate, times the copy's
            float speed = 1.0f;                     // how much faster than the note they can make the wave run (for the table level)
            bool usesModulator() const noexcept { return (fmDepth > 0.0f && fmSignal == nullptr) || amDepth > 0.0f; }
        };

        float floorAny (float x) noexcept   // std::floor without the library call (see WaveBend)
        {
            const auto t = (float) (int) x;
            return t > x ? t - 1.0f : t;
        }

        // SHAPE (WaveBend.h), SYNC, FM and AM, sample by sample, for `n`
        // samples: calls `out (i, value)` for each. Slower than the plain
        // reads, so only used while one of them is on.
        template <typename Out>
        void renderBent (const WaveSource& source, WaveBend bending, const Modulation& mod, int level, int modLevel, int n, float& phase,
                         float increment, float& crushPrevious, float& modPhase, float& lastX, Out&& out) noexcept
        {
            const auto basic = source.wave == OscWave::saw || source.wave == OscWave::pulse ? tables::Wave::saw
                             : source.wave == OscWave::triangle ? tables::Wave::triangle : tables::Wave::sine;
            const auto lp = source.lpPosition >= 0.0f ? blendFor (source.lpPosition, source.slopeWidth, level) : LevelBlend { level, level, 0.0f };
            const auto hp = source.hpPosition >= 0.0f ? blendFor (source.hpPosition, source.slopeWidth, level) : LevelBlend {};
            bending.syncEdge = mod.sync && ! mod.syncWindowed;
            const auto step = (source.mixTo - source.mixFrom) / (float) n;
            const auto partnerStep = (source.partnerTo - source.partnerFrom) / (float) n;
            const auto syncStep = (mod.syncTo - mod.syncFrom) / (float) n;
            const auto modIncrement = increment * mod.ratio;
            for (int i = 0; i < n; ++i)
            {
                const auto mix = source.mixFrom + step * (float) (i + 1);
                const auto partnerMix = source.partnerFrom + partnerStep * (float) (i + 1);

                // The plain wave at one level.
                const auto readAt = [&] (int at, float x) -> float
                {
                    if (source.wave == OscWave::wavetable)
                    {
                        const auto frames = [&] (const Wavetable& t)
                        {
                            const auto a = Wavetable::read (t.get (source.below, at), x);
                            return source.oneFrame ? a : a + (Wavetable::read (t.get (source.above, at), x) - a) * mix;
                        };
                        auto v = frames (*source.table);
                        if (source.partner != nullptr)
                            v += (frames (*source.partner) - v) * partnerMix;
                        return v;
                    }
                    const auto* table = tables::get (basic, at);
                    if (source.wave == OscWave::pulse)
                    {
                        auto other = x + source.pulseWidth;   // 0 to 2
                        other -= (float) (other >= 1.0f);
                        return tables::read (table, x) - tables::read (table, other);
                    }
                    return tables::read (table, x);
                };
                // ...through FFT's LP (the levels blended at its cutoff) and HP (what's below its cutoff taken away).
                const auto read = [&] (float x) -> float
                {
                    auto v = readAt (lp.lo, x);
                    if (lp.hi != lp.lo && lp.t > 0.0f)
                        v += (readAt (lp.hi, x) - v) * lp.t;
                    if (source.hpPosition >= 0.0f)
                    {
                        auto below = readAt (hp.lo, x);
                        if (hp.hi != hp.lo && hp.t > 0.0f)
                            below += (readAt (hp.hi, x) - below) * hp.t;
                        v -= below;
                    }
                    return v;
                };

                // SYNC: a copy restarted each cycle, running `ratio` times faster.
                const auto ratio = mod.syncFrom + syncStep * (float) (i + 1);
                const auto synced = [&] (float y) -> float
                {
                    if (! mod.sync)
                        return read (y);
                    auto z = y * ratio;
                    z -= (float) (int) z;
                    auto copy = read (z);
                    if (mod.syncWindowed)
                        copy *= modulation::window (y);
                    return mod.syncAdd ? 0.5f * (read (y) + copy) : copy;
                };

                // FM: where in the wave this sample reads, swung by the modulator.
                auto x = phase;
                auto speed = increment;
                if (mod.fmDepth > 0.0f)
                {
                    const auto m = mod.fmSignal != nullptr ? mod.fmSignal[i] : modulation::shapeAt (mod.fmShape, modPhase, modLevel);
                    x += mod.fmDepth * m;
                    x -= floorAny (x);
                    auto moved = x - lastX;   // how far it really moved (for smoothing edges), wrapping round
                    moved += moved < -0.5f ? 1.0f : moved > 0.5f ? -1.0f : 0.0f;
                    speed = juce::jmax (1.0e-6f, std::abs (moved));
                }
                lastX = x;

                auto value = bending.sample (x, speed, synced);
                if (mod.amDepth > 0.0f)
                    value *= 1.0f - mod.amDepth + mod.amDepth * modulation::shapeAt (mod.amShape, modPhase, modLevel);
                out (i, bending.bits (value, crushPrevious));

                if (mod.usesModulator())
                {
                    modPhase += modIncrement;
                    modPhase -= floorAny (modPhase);
                }
                phase += increment;
                phase -= (float) (phase >= 1.0f);
            }
        }

        // One oscillator's wave for `n` samples into `out` (replacing), advancing `phase`.
        void renderWave (const WaveSource& source, float* out, int n, float& phase, float increment, int level, float gain) noexcept
        {
            switch (source.wave)
            {
                case OscWave::saw:
                case OscWave::pulse:
                {
                    const auto* saw = tables::get (tables::Wave::saw, level);
                    if (source.wave == OscWave::saw)
                    {
                        for (int i = 0; i < n; ++i)
                        {
                            out[i] = tables::read (saw, phase) * gain;
                            phase += increment;
                            phase -= (float) (phase >= 1.0f);
                        }
                    }
                    else
                    {
                        // A pulse is one saw minus another a PULSE WIDTH later in the
                        // cycle: band-limited, because both saws are, and centred on zero.
                        for (int i = 0; i < n; ++i)
                        {
                            auto other = phase + source.pulseWidth;
                            other -= (float) (other >= 1.0f);
                            out[i] = (tables::read (saw, phase) - tables::read (saw, other)) * gain;
                            phase += increment;
                            phase -= (float) (phase >= 1.0f);
                        }
                    }
                    break;
                }
                case OscWave::triangle:
                case OscWave::sine:
                {
                    const auto* table = tables::get (source.wave == OscWave::triangle ? tables::Wave::triangle : tables::Wave::sine, level);
                    for (int i = 0; i < n; ++i)
                    {
                        out[i] = tables::read (table, phase) * gain;
                        phase += increment;
                        phase -= (float) (phase >= 1.0f);
                    }
                    break;
                }
                case OscWave::wavetable:
                case OscWave::freeform:   // (played as a one-frame wavetable: source.wave is wavetable by now)
                {
                    float positions[16];
                    for (int done = 0; done < n; done += 16)
                    {
                        const auto count = juce::jmin (16, n - done);
                        for (int i = 0; i < count; ++i)
                        {
                            positions[i] = phase;
                            phase += increment;
                            phase -= (float) (phase >= 1.0f);
                        }
                        auto part = source;
                        const auto span = source.mixTo - source.mixFrom;
                        part.mixFrom = source.mixFrom + span * (float) done / (float) n;
                        part.mixTo = source.mixFrom + span * (float) (done + count) / (float) n;
                        readTable (part, level, positions, count, [out, done, gain] (int i, float v) { out[done + i] = v * gain; });
                    }
                    break;
                }
            }
        }

        // Adds the SUB to `out`.
        void addSub (SubWave wave, float* out, int n, float& phase, float increment, int level, float gain) noexcept
        {
            const auto* table = tables::get (wave == SubWave::sine ? tables::Wave::sine
                                           : wave == SubWave::triangle ? tables::Wave::triangle
                                                                       : tables::Wave::saw, level);
            if (wave == SubWave::square)
            {
                for (int i = 0; i < n; ++i)
                {
                    auto other = phase + 0.5f;
                    other -= (float) (other >= 1.0f);
                    out[i] += (tables::read (table, phase) - tables::read (table, other)) * gain;
                    phase += increment;
                    phase -= (float) (phase >= 1.0f);
                }
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                out[i] += tables::read (table, phase) * gain;
                phase += increment;
                phase -= (float) (phase >= 1.0f);
            }
        }
    }

    Engine::Engine()
    {
        // Each voice's noise gets its own seeds, so no two notes hiss alike.
        std::uint32_t seed = 0x2545f491u;
        for (auto& voice : voices)
            for (auto& oscillator : voice.oscillators)
            {
                seed = seed * 1664525u + 1013904223u;
                oscillator.noiseLeft = Noise (seed);
                seed = seed * 1664525u + 1013904223u;
                oscillator.noiseRight = Noise (seed);
            }
    }

    void Engine::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        killStep = (float) (1.0 / juce::jmax (1.0, killMs * 0.001 * sampleRate));
        (void) tables::get (tables::Wave::saw, 0);   // builds the tables now, not on the first note
        for (auto& voice : voices)
        {
            voice.amp.setSampleRate (sampleRate);
            voice.filter.setSampleRate (sampleRate);
        }
        reset();
    }

    void Engine::reset() noexcept
    {
        for (auto& voice : voices)
            free (voice);
        monoVoice = nullptr;
        numHeld = 0;
        bend = 0.0f;
        pedalDown = false;
        lastPitch = -1.0f;
    }

    int Engine::getNumActiveVoices() const noexcept
    {
        return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.playing; });
    }

    //==========================================================================
    void Engine::process (float* left, float* right, int numSamples,
                          const juce::MidiBuffer& midi, int midiStart, const Settings& settings) noexcept
    {
        followMode (settings);

        std::fill (left, left + numSamples, 0.0f);
        std::fill (right, right + numSamples, 0.0f);

        int done = 0;
        for (const auto metadata : midi)
        {
            const auto position = metadata.samplePosition - midiStart;
            if (position < 0)
                continue;
            if (position >= numSamples)
                break;
            if (position > done)
            {
                render (left + done, right + done, position - done, settings);
                done = position;
            }
            handle (metadata.getMessage(), settings);
        }
        if (done < numSamples)
            render (left + done, right + done, numSamples - done, settings);
    }

    void Engine::handle (const juce::MidiMessage& message, const Settings& settings) noexcept
    {
        if (message.isNoteOn())
            noteOn (message.getNoteNumber(), message.getFloatVelocity(), settings);
        else if (message.isNoteOff())
            noteOff (message.getNoteNumber(), settings);
        else if (message.isPitchWheel())
            bend = juce::jlimit (-1.0f, 1.0f, (float) (message.getPitchWheelValue() - 8192) / 8192.0f);
        else if (message.isSustainPedalOn())
            sustainPedal (true);
        else if (message.isSustainPedalOff())
            sustainPedal (false);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            if (message.isAllSoundOff())
                reset();
            else
                allNotesOff();
        }
    }

    //==========================================================================
    void Engine::followMode (const Settings& settings) noexcept
    {
        // Switching between POLY and MONO while notes sound: start afresh.
        if ((settings.play.mode == PlayMode::poly) != (lastMode == PlayMode::poly))
            allNotesOff();
        lastMode = settings.play.mode;
    }

    void Engine::noteOn (int note, float velocity, const Settings& settings) noexcept
    {
        followMode (settings);
        if (velocity <= 0.0f)
        {
            noteOff (note, settings);
            return;
        }
        if (settings.play.mode == PlayMode::poly)
            polyNoteOn (note, velocity, settings);
        else
            monoNoteOn (note, velocity, settings);
    }

    void Engine::noteOff (int note, const Settings& settings) noexcept
    {
        followMode (settings);
        if (settings.play.mode != PlayMode::poly)
        {
            monoNoteOff (note, settings);
            return;
        }
        for (auto& voice : voices)
            if (voice.playing && voice.held && voice.note == note && ! voice.killing)
            {
                voice.held = false;
                if (pedalDown)
                    voice.sustained = true;
                else
                    releaseVoice (voice);
            }
    }

    void Engine::polyNoteOn (int note, float velocity, const Settings& settings) noexcept
    {
        // The same key again while its last note still sounds: that one lets go.
        auto anyHeld = false;
        for (auto& voice : voices)
            if (voice.playing && ! voice.killing)
            {
                if (voice.note == note && (voice.held || voice.sustained))
                {
                    voice.held = voice.sustained = false;
                    releaseVoice (voice);
                }
                anyHeld = anyHeld || voice.held;
            }

        // Over VOICES: the quietest releasing note goes, or else the oldest.
        auto sounding = (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.playing && ! v.killing; });
        while (sounding >= juce::jlimit (1, maxVoices, settings.play.voices))
        {
            Voice* victim = nullptr;
            for (auto& voice : voices)
                if (voice.playing && ! voice.killing && voice.amp.isReleasing()
                    && (victim == nullptr || voice.amp.getLevel() < victim->amp.getLevel()))
                    victim = &voice;
            if (victim == nullptr)
                for (auto& voice : voices)
                    if (voice.playing && ! voice.killing && (victim == nullptr || voice.age < victim->age))
                        victim = &voice;
            if (victim == nullptr)
                break;
            kill (*victim);
            --sounding;
        }

        auto* voice = findFreeVoice();
        const auto glide = settings.play.glideMs > 0.0f && lastPitch >= 0.0f
                        && (settings.play.glideMode == GlideMode::always || anyHeld);
        startVoice (*voice, note, velocity, glide ? lastPitch : (float) note, settings);
        applyOscillatorLimits (*voice, settings);
    }

    void Engine::monoNoteOn (int note, float velocity, const Settings& settings) noexcept
    {
        // Remember the key (moving it to the end if it's somehow already held).
        auto* end = heldNotes.data() + numHeld;
        if (auto* found = std::find (heldNotes.data(), end, note); found != end)
        {
            std::move (found + 1, end, found);
            --numHeld;
        }
        if (numHeld < (int) heldNotes.size())
        {
            heldNotes[(size_t) numHeld] = note;
            heldVelocities[(size_t) note] = velocity;
            ++numHeld;
        }

        const auto target = monoTarget (settings);
        if (target < 0)
            return;

        const auto sounding = monoVoice != nullptr && monoVoice->playing && ! monoVoice->killing;
        if (! sounding)
        {
            auto* voice = findFreeVoice();
            const auto glide = settings.play.glideMs > 0.0f && lastPitch >= 0.0f && settings.play.glideMode == GlideMode::always;
            startVoice (*voice, target, heldVelocities[(size_t) target], glide ? lastPitch : (float) target, settings);
            monoVoice = voice;   // after starting it: starting a voice clears whatever it was
            return;
        }
        if (target == monoVoice->note && monoVoice->held)
            return;   // MONO LOW / HIGH: a key that doesn't change which note plays

        // Overlapping a held note: LEGATO glides on (without restarting the
        // envelopes); otherwise the envelopes start again from where they are.
        const auto overlapping = monoVoice->held;
        const auto legato = settings.play.legato && overlapping;
        const auto glide = settings.play.glideMs > 0.0f && (settings.play.glideMode == GlideMode::always || overlapping);
        retrigger (*monoVoice, target, heldVelocities[(size_t) target], ! legato, glide, settings);
    }

    void Engine::monoNoteOff (int note, const Settings& settings) noexcept
    {
        auto* end = heldNotes.data() + numHeld;
        if (auto* found = std::find (heldNotes.data(), end, note); found != end)
        {
            std::move (found + 1, end, found);
            --numHeld;
        }

        if (monoVoice == nullptr || ! monoVoice->playing || monoVoice->killing)
            return;

        const auto target = monoTarget (settings);
        if (target >= 0)
        {
            // Back to a key still held (a trill): always legato-style glide rules.
            if (target != monoVoice->note)
                retrigger (*monoVoice, target, heldVelocities[(size_t) target], ! settings.play.legato,
                           settings.play.glideMs > 0.0f, settings);
            return;
        }
        if (monoVoice->note != note && monoVoice->held)
            return;
        monoVoice->held = false;
        if (pedalDown)
            monoVoice->sustained = true;
        else
            releaseVoice (*monoVoice);
    }

    int Engine::monoTarget (const Settings& settings) const noexcept
    {
        if (numHeld == 0)
            return -1;
        const auto* first = heldNotes.data();
        const auto* end = heldNotes.data() + numHeld;
        switch (settings.play.mode)
        {
            case PlayMode::monoLow:  return *std::min_element (first, end);
            case PlayMode::monoHigh: return *std::max_element (first, end);
            case PlayMode::mono:
            case PlayMode::poly:     break;
        }
        return heldNotes[(size_t) numHeld - 1];   // the last one pressed
    }

    void Engine::allNotesOff() noexcept
    {
        for (auto& voice : voices)
            if (voice.playing)
            {
                voice.held = voice.sustained = false;
                releaseVoice (voice);
            }
        numHeld = 0;
    }

    void Engine::sustainPedal (bool down) noexcept
    {
        pedalDown = down;
        if (down)
            return;
        for (auto& voice : voices)
            if (voice.playing && voice.sustained && ! voice.held)
            {
                voice.sustained = false;
                releaseVoice (voice);
            }
    }

    //==========================================================================
    Engine::Voice* Engine::findFreeVoice() noexcept
    {
        for (auto& voice : voices)
            if (! voice.playing)
                return &voice;

        // Every slot busy (only possible with many stolen notes still fading):
        // the one furthest through its fade goes at once.
        auto* oldest = &voices.front();
        for (auto& voice : voices)
            if (voice.killing && (! oldest->killing || voice.killGain < oldest->killGain))
                oldest = &voice;
        free (*oldest);
        return oldest;
    }

    void Engine::startVoice (Voice& voice, int note, float velocity, float fromPitch, const Settings& settings) noexcept
    {
        free (voice);
        voice.playing = true;
        voice.held = true;
        voice.note = note;
        voice.velocity = velocity;
        voice.age = ++noteCounter;
        voice.pitch = fromPitch;
        voice.coefficientsFresh = true;
        voice.filterLeft.reset();
        voice.filterRight.reset();
        voice.driveLeft.reset();
        voice.driveRight.reset();
        setGlide (voice, (float) note, ! juce::approximatelyEqual (fromPitch, (float) note), settings);
        retrigger (voice, note, velocity, true, ! juce::approximatelyEqual (fromPitch, (float) note), settings);
    }

    void Engine::retrigger (Voice& voice, int note, float velocity, bool restartEnvelopes, bool glide, const Settings& settings) noexcept
    {
        voice.note = note;
        voice.velocity = velocity;
        voice.held = true;
        voice.sustained = false;
        setGlide (voice, (float) note, glide, settings);
        setNotePan (voice, settings);
        lastPitch = (float) note;

        if (! restartEnvelopes)
            return;

        voice.amp.trigger (settings.amp.envelope);
        voice.filter.trigger (settings.filter.envelope);
        voice.spikePosition = 0.0f;

        for (size_t i = 0; i < voice.oscillators.size(); ++i)
        {
            const auto& s = settings.oscillators[i];
            auto& osc = voice.oscillators[i];
            const auto wasActive = osc.active && ! osc.fadingOut;
            osc.active = s.on;
            osc.fadingOut = false;
            if (! s.on)
                continue;

            // RANDOM: below zero a random pitch each note, above zero alternating up and down.
            if (s.randomPitch < 0.0f)
                osc.randomOffset = (random.nextFloat() * 2.0f - 1.0f) * -s.randomPitch;
            else if (s.randomPitch > 0.0f)
            {
                alternate[i] = ! alternate[i];
                osc.randomOffset = (alternate[i] ? 1.0f : -1.0f) * s.randomPitch;
            }
            else
                osc.randomOffset = 0.0f;

            // A mono retrigger keeps the wave running (no jump); a fresh note starts it where PHASE says.
            if (! wasActive)
            {
                osc.phase = s.randomPhase ? random.nextFloat() : wrap (s.phase);
                osc.subPhase = s.randomPhase ? random.nextFloat() : wrap (s.phase);
                const auto fadeSamples = s.fadeInMs * 0.001f * (float) sampleRate;
                osc.fade = fadeSamples >= 1.0f ? 0.0f : 1.0f;
                osc.fadeStep = fadeSamples >= 1.0f ? 1.0f / fadeSamples : 0.0f;

                // The copies: a chord note's own pitch starts as the key does; every
                // unison copy at a random point, which keeps them from lining up and
                // sounding hollow (what makes a "supersaw" wide rather than phasey).
                for (int copy = 1; copy < maxCopies; ++copy)
                {
                    auto& c = osc.copies[(size_t) copy];
                    c.phase = (copy % maxUnison == 0 && ! s.randomPhase) ? wrap (s.phase) : random.nextFloat();
                    c.vibratoPhase = random.nextFloat();
                    c.vibratoRate = 0.85f + 0.3f * random.nextFloat();   // INDEPENDENT: each a little faster or slower (our choice)
                    c.vibratoHeld = random.nextFloat() * 2.0f - 1.0f;
                }
                osc.vibratoPhase = 0.0f;
                osc.vibratoHeld = random.nextFloat() * 2.0f - 1.0f;
                osc.tableIndex = -1.0f;
                osc.pulsePhase = random.nextFloat();
                osc.crushPrevious = 0.0f;
                for (auto& c : osc.copies)
                    c.crushPrevious = 0.0f;

                // FM and AM's modulator: PHASE LOCK starts it with the wave (each
                // copy's at its own start), else anywhere.
                const auto ratio = modulation::ratio (s.modRate, s.modGrid);
                osc.modPhase = s.modPhaseLock ? wrap (osc.phase * ratio) : random.nextFloat();
                osc.lastX = osc.phase;
                for (auto& c : osc.copies)
                {
                    c.modPhase = s.modPhaseLock ? wrap (c.phase * ratio) : random.nextFloat();
                    c.lastX = c.phase;
                }
            }
            if (! wasActive || restartEnvelopes)
            {
                osc.vibratoAge = 0;   // the vibrato fades in again with each new note
                osc.syncAge = 0;      // ...and SYNC's envelope starts again
            }
        }
    }

    void Engine::setGlide (Voice& voice, float toPitch, bool glide, const Settings& settings) noexcept
    {
        voice.targetPitch = toPitch;
        if (! glide || settings.play.glideMs <= 0.0f)
        {
            voice.pitch = toPitch;
            voice.glideRate = 0.0f;
            return;
        }
        // GLIDE is the time to get there, however far.
        voice.glideRate = std::abs (toPitch - voice.pitch) / juce::jmax (1.0f, settings.play.glideMs * 0.001f * (float) sampleRate);
    }

    void Engine::setNotePan (Voice& voice, const Settings& settings) noexcept
    {
        const auto& amp = settings.amp;
        auto pan = amp.pan + amp.panKeytrack * juce::jlimit (-1.0f, 1.0f, ((float) voice.note - 60.0f) / 36.0f);
        if (amp.spread > 0.0f)
            pan += (voice.age % 2 == 0 ? amp.spread : -amp.spread);       // alternating left and right
        else if (amp.spread < 0.0f)
            pan += (random.nextFloat() * 2.0f - 1.0f) * -amp.spread;     // anywhere, at random
        balance (pan, voice.panLeft, voice.panRight);
    }

    void Engine::applyOscillatorLimits (const Voice& newest, const Settings& settings) noexcept
    {
        // An oscillator's own VOICES limit: over it, the oldest note's copy of
        // that oscillator fades out (the rest of that note carries on).
        for (size_t i = 0; i < settings.oscillators.size(); ++i)
        {
            const auto limit = settings.oscillators[i].voiceLimit;
            if (! settings.oscillators[i].on || limit >= maxVoices)
                continue;
            for (;;)
            {
                int count = 0;
                Voice* oldest = nullptr;
                for (auto& voice : voices)
                    if (voice.playing && ! voice.killing && voice.oscillators[i].active && ! voice.oscillators[i].fadingOut)
                    {
                        ++count;
                        if (&voice != &newest && (oldest == nullptr || voice.age < oldest->age))
                            oldest = &voice;
                    }
                if (count <= limit || oldest == nullptr)
                    break;
                auto& osc = oldest->oscillators[i];
                osc.fadingOut = true;
                osc.fadeStep = -1.0f / juce::jmax (1.0f, oscFadeOutMs * 0.001f * (float) sampleRate);
            }
        }
    }

    void Engine::releaseVoice (Voice& voice) noexcept
    {
        voice.amp.release();
        voice.filter.release();
    }

    void Engine::kill (Voice& voice) noexcept
    {
        voice.killing = true;
        voice.held = voice.sustained = false;
        if (&voice == monoVoice)
            monoVoice = nullptr;
    }

    void Engine::free (Voice& voice) noexcept
    {
        voice.playing = voice.held = voice.sustained = voice.killing = false;
        voice.killGain = 1.0f;
        voice.note = -1;
        voice.amp.reset();
        voice.filter.reset();
        for (auto& osc : voice.oscillators)
        {
            osc.active = osc.fadingOut = false;
            osc.fade = 1.0f;
            osc.fadeStep = 0.0f;
        }
        if (&voice == monoVoice)
            monoVoice = nullptr;
    }

    //==========================================================================
    void Engine::render (float* left, float* right, int numSamples, const Settings& settings) noexcept
    {
        for (auto& voice : voices)
        {
            if (! voice.playing)
                continue;
            for (int done = 0; done < numSamples && voice.playing; done += controlInterval)
                renderVoice (voice, left + done, right + done, juce::jmin (controlInterval, numSamples - done), settings);
        }
    }

    void Engine::renderVoice (Voice& voice, float* left, float* right, int n, const Settings& settings) noexcept
    {
        const auto& filterSettings = settings.filter;
        const auto& ampSettings = settings.amp;
        voice.amp.update (ampSettings.envelope);
        voice.filter.update (filterSettings.envelope);

        // Glide, once per interval.
        if (voice.glideRate > 0.0f)
        {
            const auto step = voice.glideRate * (float) n;
            if (std::abs (voice.targetPitch - voice.pitch) <= step)
            {
                voice.pitch = voice.targetPitch;
                voice.glideRate = 0.0f;
            }
            else
                voice.pitch += voice.targetPitch > voice.pitch ? step : -step;
        }
        const auto bendSemitones = bend >= 0.0f ? bend * settings.play.bendUp : bend * settings.play.bendDown;

        std::fill (filteredLeft.begin(), filteredLeft.begin() + n, 0.0f);
        std::fill (filteredRight.begin(), filteredRight.begin() + n, 0.0f);
        std::fill (dryLeft.begin(), dryLeft.begin() + n, 0.0f);
        std::fill (dryRight.begin(), dryRight.begin() + n, 0.0f);
        renderOscillators (voice, n, settings, bendSemitones);

        // Every oscillator in it taken by newer notes: the note is over.
        if (std::none_of (voice.oscillators.begin(), voice.oscillators.end(), [] (const OscState& o) { return o.active; }))
        {
            free (voice);
            return;
        }

        // The FILTER: its cutoff for the end of this interval, glided to from where it was.
        const auto velocityDown = 1.0f - voice.velocity;
        const auto filterEnvelope = voice.filter.step (n);
        if (filterSettings.on)
        {
            auto octaves = filterSettings.keytrack * (voice.pitch - 60.0f) / 12.0f
                         - filterSettings.velocity * filterVelocityOctaves * velocityDown
                         + filterSettings.envelopeAmount * filterEnvelopeOctaves * filterEnvelope
                               * (1.0f - filterSettings.envelopeVelocity * velocityDown);
            const auto cutoff = (double) filterSettings.cutoffHz * std::exp2 ((double) octaves);
            const auto resonance = filterSettings.resonance * (1.0f - filterSettings.resonanceVelocity * velocityDown);
            const auto target = FilterChannel::Coefficients::make (cutoff, resonance, sampleRate);
            const auto from = voice.coefficientsFresh ? target : voice.coefficients;
            voice.coefficientsFresh = false;

            // DRIVE: up to +24 dB into the saturation, scaled back so a full-scale
            // wave still peaks near full scale (quieter sounds come out louder,
            // as from any overdriven input).
            const auto drive = filterSettings.drive;
            const auto driveGain = 1.0f + drive * 15.0f;
            const auto makeUp = 1.0f / std::tanh (driveGain);
            const auto type = filterSettings.type;
            for (int i = 0; i < n; ++i)
            {
                const auto c = FilterChannel::Coefficients::lerp (from, target, (float) (i + 1) / (float) n);
                auto l = filteredLeft[(size_t) i], r = filteredRight[(size_t) i];
                if (drive > 0.0f)
                {
                    l = voice.driveLeft.process (l, driveGain, makeUp);
                    r = voice.driveRight.process (r, driveGain, makeUp);
                }
                filteredLeft[(size_t) i] = voice.filterLeft.process (l, type, c);
                filteredRight[(size_t) i] = voice.filterRight.process (r, type, c);
            }
            voice.coefficients = target;
        }

        // The AMP: envelope, velocity, SPIKE, VOLUME, then this note's pan.
        const auto velocityGain = 1.0f - ampSettings.velocity * velocityDown;
        const auto volume = ampSettings.volume * velocityGain;
        const auto spike = ampSettings.spike * spikeMaxGain * (1.0f - ampSettings.spikeVelocity * velocityDown);
        const auto spikeStep = 1.0f / juce::jmax (1.0f, spikeMs * 0.001f * (float) sampleRate);
        for (int i = 0; i < n; ++i)
        {
            auto gain = voice.amp.next() * volume;
            if (voice.spikePosition < 1.0f)
            {
                gain *= 1.0f + spike * (1.0f - synth::bend (voice.spikePosition, ampSettings.spikeCurve));
                voice.spikePosition += spikeStep;
            }
            if (voice.killing)
            {
                gain *= voice.killGain;
                voice.killGain -= killStep;
                if (voice.killGain <= 0.0f)
                    voice.killGain = 0.0f;
            }
            left[i] += (filteredLeft[(size_t) i] + dryLeft[(size_t) i]) * gain * voice.panLeft;
            right[i] += (filteredRight[(size_t) i] + dryRight[(size_t) i]) * gain * voice.panRight;
        }

        // Over once it can't be heard.
        const auto& amp = voice.amp;
        if (! amp.isActive()
            || (voice.killing && voice.killGain <= 0.0f)
            || (amp.isReleasing() && amp.getLevel() < silence)
            || amp.isSilentSustain())
            free (voice);
    }

    float Engine::unisonPosition (int copy, int voices) noexcept
    {
        // The note's own pitch in the middle; the others alternately above and
        // below, further out in turn: for 7, +1/3 -1/3 +2/3 -2/3 +1 -1.
        if (copy <= 0 || voices < 2)
            return 0.0f;
        const auto extras = voices - 1;
        const auto half = (extras + 1) / 2;
        const auto step = (copy + 1) / 2;
        return (copy % 2 == 1 ? 1.0f : -1.0f) * (float) step / (float) half;
    }

    int Engine::getNumActiveOscillators (const Settings& settings) const noexcept
    {
        int count = 0;
        for (const auto& voice : voices)
            if (voice.playing)
                for (size_t i = 0; i < voice.oscillators.size(); ++i)
                    if (voice.oscillators[i].active)
                    {
                        const auto& s = settings.oscillators[i];
                        const auto notes = 1 + (int) std::count_if (s.chord.begin(), s.chord.end(), [] (const ChordVoice& c) { return c.on; });
                        count += notes * (s.hasUnison() ? s.unisonVoices : 1);
                    }
        return count;
    }

    float Engine::vibratoValue (VibratoWave wave, float& phase, float& held, float advance) noexcept
    {
        float value = 0.0f;
        switch (wave)
        {
            case VibratoWave::sine:     value = std::sin (juce::MathConstants<float>::twoPi * phase); break;
            case VibratoWave::triangle: value = 1.0f - 4.0f * std::abs (phase - 0.5f); break;
            case VibratoWave::square:   value = phase < 0.5f ? 1.0f : -1.0f; break;
            case VibratoWave::saw:      value = 2.0f * phase - 1.0f; break;
            case VibratoWave::random:   value = held; break;
        }
        phase += advance;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            held = random.nextFloat() * 2.0f - 1.0f;   // RANDOM: a new value each cycle
        }
        return value;
    }

    namespace
    {
        // Adds one copy of a wave to `left` and `right`, each sample scaled by
        // `gains` (FADE IN, VOLUME, GAIN) and the copy's own pan.
        //
        // In two passes, so the compiler can work on several samples at once:
        // first every sample's place in the table (worked out from the start,
        // not each from the last, so no sample waits for the one before), then
        // the reading and mixing.
        void addCopy (const WaveSource& source, float* left, float* right, const float* gains, int n, float& phase, float increment,
                      int level, float gainLeft, float gainRight) noexcept
        {
            constexpr int maxN = Engine::controlInterval;
            float positions[maxN], others[maxN];
            const auto isTable = source.wave == OscWave::wavetable;
            const auto size = isTable ? 1.0f : (float) tables::size;   // a wavetable's levels scale the phase themselves
            for (int i = 0; i < n; ++i)
            {
                auto p = phase + increment * (float) i;
                p -= (float) (int) p;   // 0 to 1 (p is never negative)
                positions[i] = p * size;
                auto q = p + source.pulseWidth;
                q -= (float) (int) q;
                others[i] = q * size;
            }
            phase += increment * (float) n;
            phase -= (float) (int) phase;

            if (isTable)
            {
                readTable (source, level, positions, n, [&] (int i, float value)
                {
                    const auto v = value * gains[i];
                    left[i] += v * gainLeft;
                    right[i] += v * gainRight;
                });
                return;
            }

            const auto read = [] (const float* table, float position)
            {
                const auto index = (int) position;
                const auto fraction = position - (float) index;
                const auto* at = table + (index & (tables::size - 1));
                return at[0] + (at[1] - at[0]) * fraction;
            };

            if (source.wave == OscWave::pulse)
            {
                const auto* saw = tables::get (tables::Wave::saw, level);
                for (int i = 0; i < n; ++i)
                {
                    const auto v = (read (saw, positions[i]) - read (saw, others[i])) * gains[i];
                    left[i] += v * gainLeft;
                    right[i] += v * gainRight;
                }
                return;
            }
            const auto* table = tables::get (source.wave == OscWave::saw ? tables::Wave::saw
                                           : source.wave == OscWave::triangle ? tables::Wave::triangle : tables::Wave::sine, level);
            for (int i = 0; i < n; ++i)
            {
                const auto v = read (table, positions[i]) * gains[i];
                left[i] += v * gainLeft;
                right[i] += v * gainRight;
            }
        }
    }

    void Engine::renderOscillators (Voice& voice, int n, const Settings& settings, float bendSemitones) noexcept
    {
        const auto filterOn = settings.filter.on;
        std::array<float, controlInterval> gains {};

        // FM from other oscillators: an oscillator chosen as another's
        // modulator is rendered first, into fmSignals, and isn't heard itself.
        std::array<bool, numOscillators> isModulator {};
        for (size_t i = 0; i < settings.oscillators.size(); ++i)
        {
            const auto& s = settings.oscillators[i];
            if (s.on && s.hasFm() && s.fmSource >= fmFirstOscillator && (size_t) (s.fmSource - fmFirstOscillator) != i)
                isModulator[(size_t) (s.fmSource - fmFirstOscillator)] = true;
        }
        std::array<size_t, numOscillators> order {};
        size_t count = 0;
        for (const auto modulators : { true, false })
            for (size_t i = 0; i < order.size(); ++i)
                if (isModulator[i] == modulators)
                    order[count++] = i;
        for (size_t i = 0; i < order.size(); ++i)
            if (isModulator[i])
                std::fill (fmSignals[i].begin(), fmSignals[i].end(), 0.0f);

        for (const auto index : order)
        {
            auto& osc = voice.oscillators[index];
            const auto& s = settings.oscillators[index];
            if (! osc.active)
                continue;
            if (! s.on)
            {
                // Switched off while the note plays: gone from this note.
                osc.active = false;
                continue;
            }

            // VIBRATO: its depth now (fading in after the note starts), and the
            // global wobble for this interval.
            auto vibratoDepth = 0.0f;
            if (s.vibratoCents > 0.0f)
            {
                const auto fadeSamples = s.vibratoFadeMs * 0.001f * (float) sampleRate;
                const auto progress = fadeSamples < 1.0f ? 1.0f : juce::jmin (1.0f, (float) osc.vibratoAge / fadeSamples);
                vibratoDepth = s.vibratoCents * 0.01f * synth::bend (progress, s.vibratoCurve);
                osc.vibratoAge += n;
            }
            const auto vibratoAdvance = s.vibratoRateHz * (float) n / (float) sampleRate;
            const auto globalVibrato = vibratoDepth > 0.0f && s.vibratoMode == VibratoMode::global
                                     ? vibratoValue (s.vibratoWave, osc.vibratoPhase, osc.vibratoHeld, vibratoAdvance) * vibratoDepth : 0.0f;

            // Its pitch for this interval, and the tables to read for it.
            const auto basePitch = voice.pitch + bendSemitones + s.transpose + s.fineCents * 0.01f + osc.randomOffset
                                 + settings.play.tuneCents * 0.01f;
            const auto independent = vibratoDepth > 0.0f && s.vibratoMode == VibratoMode::independent;
            const auto ownVibrato = independent ? vibratoValue (s.vibratoWave, osc.copies[0].vibratoPhase, osc.copies[0].vibratoHeld,
                                                                vibratoAdvance * osc.copies[0].vibratoRate) * vibratoDepth
                                                : globalVibrato;
            const auto hz = pitchToHz (basePitch + ownVibrato);
            const auto increment = juce::jlimit (0.0f, 0.5f, hz / (float) sampleRate);
            const auto level = tables::levelFor (hz, sampleRate);
            WaveSource source;
            source.wave = s.wave;
            source.pulseWidth = juce::jlimit (0.01f, 0.99f, s.pulseWidth);
            auto audible = s.gain > 0.0f;

            // What it reads: FFT's edited copy if there is one (for a
            // wavetable, with the same frames), else its wavetable, its
            // FREEFORM drawing (a one-frame table) or its wave.
            const auto drawn = s.wave == OscWave::wavetable || s.wave == OscWave::freeform;
            const auto* table = s.spectral != nullptr ? s.spectral : drawn ? s.table : nullptr;
            if (drawn || s.spectral != nullptr)
            {
                source.wave = OscWave::wavetable;
                audible = audible && table != nullptr;
            }
            if (table != nullptr)
            {
                source.table = table;
                if (s.wave == OscWave::wavetable)
                {
                    // INDEX: from where it was at the end of the last interval to where
                    // it is now, between the same two frames (if it crossed a frame,
                    // from the new pair's start: a step too small to hear).
                    const auto from = osc.tableIndex < 0.0f ? s.tableIndex : osc.tableIndex;
                    osc.tableIndex = s.tableIndex;
                    const auto now = table->locate (s.tableIndex, s.tableFrames, s.tableSteppy);
                    const auto before = table->locate (from, s.tableFrames, s.tableSteppy);
                    source.below = now.below;
                    source.above = now.above;
                    source.mixTo = now.between;
                    source.mixFrom = before.below == now.below && before.above == now.above ? before.between : now.between;
                    source.oneFrame = now.below == now.above;
                }
                // FFT VSPEED: swinging between the two edited copies.
                if (s.spectralPulse != nullptr && s.spectralPulse->getNumFrames() == table->getNumFrames())
                {
                    const auto swing = [] (float p) { return 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * p); };
                    source.partner = s.spectralPulse;
                    source.partnerFrom = swing (osc.pulsePhase);
                    osc.pulsePhase = wrap (osc.pulsePhase + s.fftVSpeedHz * (float) n / (float) sampleRate);
                    source.partnerTo = swing (osc.pulsePhase);
                }
            }
            // FFT LP and HP: their cutoffs among the levels (3 an octave), live.
            if (s.hasFftFilter())
            {
                const auto position = [] (float harmonics) { return 3.0f * std::log2 ((float) tables::maxHarmonics / juce::jlimit (1.0f, 512.0f, harmonics)); };
                source.lpPosition = s.fftLp < 511.5f ? position (s.fftLp) : -1.0f;
                source.hpPosition = s.fftHp > 1.01f ? position (s.fftHp) : -1.0f;
                source.slopeWidth = 1.0f + 5.0f * juce::jlimit (0.0f, 1.0f, s.fftSlope);
            }

            // SHAPE: bent waves read a level with fewer harmonics, as many as the
            // fastest the bend plays any part of the wave leaves room for.
            warpTables[index].update (s.skew, s.pinch);
            const auto bending = WaveBend::make (s, warpTables[index]);

            // SYNC, FM and AM: the same, with the copy's or modulator's rate.
            Modulation mod;
            if (s.hasModulation())
            {
                mod.active = true;
                mod.ratio = modulation::ratio (s.modRate, s.modGrid);
                if (s.hasSync())
                {
                    // SYNC's envelope: from the note's start, falling over LENGTH.
                    const auto msPerSample = 1000.0f / (float) sampleRate;
                    const auto from = modulation::syncEnvelope ((float) osc.syncAge * msPerSample, s.syncEnvMs, s.syncEnvCurve);
                    const auto to = modulation::syncEnvelope ((float) (osc.syncAge + n) * msPerSample, s.syncEnvMs, s.syncEnvCurve);
                    osc.syncAge += n;
                    mod.sync = true;
                    mod.syncAdd = s.syncMode == SyncMode::add || s.syncMode == SyncMode::addWindowed;
                    mod.syncWindowed = s.syncMode == SyncMode::pureWindowed || s.syncMode == SyncMode::addWindowed;
                    mod.syncFrom = modulation::syncRatio (s.syncAmount + s.syncEnvAmount * from);
                    mod.syncTo = modulation::syncRatio (s.syncAmount + s.syncEnvAmount * to);
                    mod.speed *= juce::jmax (mod.syncFrom, mod.syncTo);
                }
                if (s.hasFm() && s.fmSource != fmFirstOscillator + (int) index)   // an oscillator can't modulate itself
                {
                    mod.fmDepth = modulation::fmDepth (s.fmAmount);
                    if (s.fmSource >= fmFirstOscillator)
                    {
                        // Another oscillator: rendered first (see the loop's order),
                        // its speed estimated from its pitch against this one's.
                        const auto modulator = (size_t) (s.fmSource - fmFirstOscillator);
                        mod.fmSignal = fmSignals[modulator].data();
                        const auto& other = settings.oscillators[modulator];
                        const auto pitchRatio = std::exp2 ((other.transpose - s.transpose + (other.fineCents - s.fineCents) * 0.01f) / 12.0f);
                        mod.speed *= modulation::fmSpeed (mod.fmDepth, pitchRatio, ModShape::saw);
                    }
                    else
                    {
                        mod.fmShape = (ModShape) s.fmSource;
                        mod.speed *= modulation::fmSpeed (mod.fmDepth, mod.ratio, mod.fmShape);
                    }
                }
                if (s.hasAm())
                {
                    mod.amDepth = juce::jlimit (0.0f, 1.0f, s.amAmount);
                    mod.amShape = s.amShape;
                    mod.speed *= 1.0f + mod.ratio;   // each harmonic gains side tones the modulator's rate away
                }
                if ((mod.sync || mod.fmDepth > 0.0f) && ! (bending.hasFormant || bending.warp != nullptr))
                    mod.speed *= WaveBend::headroom;   // room for side tones, as SHAPE's
            }
            const auto modLevel = tables::levelFor (hz * mod.ratio * 2.0f, sampleRate);

            auto* mono = scratch.data();
            const auto general = bending.active || mod.active || source.needsGeneralPath();
            if (audible && general)
                renderBent (source, bending.forIncrement (increment), mod, tables::levelFor (hz * bending.speed * mod.speed, sampleRate), modLevel,
                            n, osc.phase, increment, osc.crushPrevious, osc.modPhase, osc.lastX,
                            [mono, gain = s.gain] (int i, float v) { mono[i] = v * gain; });
            else if (audible)
                renderWave (source, mono, n, osc.phase, increment, level, s.gain);
            else
            {
                std::fill (mono, mono + n, 0.0f);
                osc.phase = wrap (osc.phase + increment * (float) n);   // keeps running, silent
            }

            if (s.sub > 0.0f)
            {
                const auto octaves = (float) (s.subOctave - 2);   // -2, -1 or 0 octaves
                const auto subHz = hz * std::exp2 (octaves);
                addSub (s.subWave, mono, n, osc.subPhase, juce::jlimit (0.0f, 0.5f, subHz / (float) sampleRate),
                        tables::levelFor (subHz, sampleRate), s.sub * (s.subInvert ? -1.0f : 1.0f));
            }
            const auto sign = s.invert ? -1.0f : 1.0f;
            if (s.invert)
                for (int i = 0; i < n; ++i)
                    mono[i] = -mono[i];

            // FADE IN (or fading out after a newer note took this oscillator) and
            // VOLUME, once for every copy.
            for (int i = 0; i < n; ++i)
            {
                gains[(size_t) i] = osc.fade * s.volume;
                if (! juce::exactlyEqual (osc.fadeStep, 0.0f))
                {
                    osc.fade = juce::jlimit (0.0f, 1.0f, osc.fade + osc.fadeStep);
                    if (osc.fade >= 1.0f && osc.fadeStep > 0.0f)
                        osc.fadeStep = 0.0f;
                }
            }

            if (isModulator[index])
            {
                // FM's modulator for another oscillator (with its VOLUME and FADE IN): kept for it, not heard.
                for (int i = 0; i < n; ++i)
                    fmSignals[index][(size_t) i] = mono[i] * gains[(size_t) i];
            }
            else
            {
                float panLeft, panRight;
                balance (s.pan, panLeft, panRight);
                auto* outLeft = (s.toFilter && filterOn ? filteredLeft : dryLeft).data();
                auto* outRight = (s.toFilter && filterOn ? filteredRight : dryRight).data();

                const auto noise = s.noise;
                const auto colour = s.noiseColour;
                for (int i = 0; i < n; ++i)
                {
                    auto l = mono[i], r = mono[i];
                    if (noise > 0.0f)
                    {
                        const auto noiseLeft = osc.noiseLeft.next (colour);
                        auto noiseRight = noiseLeft;
                        if (s.noiseWidth != NoiseWidth::mono)
                        {
                            const auto other = osc.noiseRight.next (colour);
                            noiseRight = s.noiseWidth == NoiseWidth::stereo ? other : 0.5f * (noiseLeft + other);
                        }
                        l += noiseLeft * noise;
                        r += noiseRight * noise;
                    }
                    outLeft[i] += l * gains[(size_t) i] * panLeft;
                    outRight[i] += r * gains[(size_t) i] * panRight;
                }

                // The other copies: the key's unison copies, then each chord note and
                // its unison copies. The wave only (the sub and noise stay with the key),
                // each with its own pitch, pan and level. None at all unless asked for.
                const auto unison = s.hasUnison() ? s.unisonVoices : 1;
                const auto detuneCents = s.unisonDetune * s.unisonDetune * maxDetuneCents;   // squared: fine control at the low end
                if (audible && (unison > 1 || std::any_of (s.chord.begin(), s.chord.end(), [] (const ChordVoice& c) { return c.on; })))
                {
                    for (int note = 0; note <= numChordVoices; ++note)
                    {
                        const auto* chord = note > 0 ? &s.chord[(size_t) note - 1] : nullptr;
                        if (chord != nullptr && ! chord->on)
                            continue;
                        const auto notePitch = chord != nullptr ? chord->coarse + chord->fineCents * 0.01f : 0.0f;
                        const auto noteVolume = chord != nullptr ? chord->volume : 1.0f;
                        const auto notePan = chord != nullptr ? chord->pan : 0.0f;
                        for (int u = note == 0 ? 1 : 0; u < unison; ++u)
                        {
                            auto& c = osc.copies[(size_t) (note * maxUnison + u)];
                            const auto position = unisonPosition (u, unison);
                            const auto octave = u > 0 && s.unisonOctaves > 0 ? (float) ((u - 1) % (s.unisonOctaves + 1)) * 12.0f : 0.0f;
                            const auto copyVibrato = independent ? vibratoValue (s.vibratoWave, c.vibratoPhase, c.vibratoHeld, vibratoAdvance * c.vibratoRate) * vibratoDepth
                                                                 : globalVibrato;
                            const auto copyHz = pitchToHz (basePitch + notePitch + octave + position * detuneCents * 0.01f + copyVibrato);
                            float left, right;
                            balance (s.pan + notePan + position * s.unisonSpread, left, right);
                            const auto copyLevel = (u == 0 ? 1.0f : s.unisonMix) * noteVolume * s.gain * sign;
                            const auto copyIncrement = juce::jlimit (0.0f, 0.5f, copyHz / (float) sampleRate);
                            if (general)
                            {
                                const auto gainLeft = left * copyLevel, gainRight = right * copyLevel;
                                renderBent (source, bending.forIncrement (copyIncrement), mod, tables::levelFor (copyHz * bending.speed * mod.speed, sampleRate),
                                            tables::levelFor (copyHz * mod.ratio * 2.0f, sampleRate), n, c.phase,
                                            copyIncrement, c.crushPrevious, c.modPhase, c.lastX, [&] (int i, float v)
                                            {
                                                v *= gains[(size_t) i];
                                                outLeft[i] += v * gainLeft;
                                                outRight[i] += v * gainRight;
                                            });
                            }
                            else
                                addCopy (source, outLeft, outRight, gains.data(), n, c.phase, copyIncrement,
                                         tables::levelFor (copyHz, sampleRate), left * copyLevel, right * copyLevel);
                        }
                    }
                }
            }

            if (osc.fadingOut && osc.fade <= 0.0f)
                osc.active = false;
        }
    }
}
