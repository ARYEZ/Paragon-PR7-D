#include "dsp/synth/Drums.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace synth::drums
{
    namespace
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        // Each model's level, so a hit at the defaults peaks near -3 dBFS (measured in the tests).
        constexpr std::array<float, 10> modelLevel { 0.651f, 0.716f, 1.373f, 2.314f, 2.006f, 0.550f, 0.824f, 0.770f, 1.119f, 1.0f };

        // The six metal oscillators (hats, cymbal): unrelated pitches, so their harmonics clash like metal. The
        // classic analog-machine idea; these are our own numbers.
        constexpr std::array<double, 6> metalHz { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        constexpr std::array<double, 6> metalStart { 0.0, 0.37, 0.71, 0.13, 0.52, 0.89 };
        // The clap's bursts (seconds, at PITCH 0): three quick slaps, then the one that rings.
        constexpr std::array<double, 4> clapBursts { 0.0, 0.010, 0.020, 0.031 };

        float decayCoefficient (double seconds, double sampleRate) noexcept
        {
            return (float) std::exp (-1.0 / (juce::jmax (1.0e-4, seconds) * sampleRate));
        }

        // A square wave with its jumps smoothed (polyBLEP), so the metal doesn't fold back down as aliasing.
        float blep (double t, double dt) noexcept
        {
            if (t < dt)
            {
                t /= dt;
                return (float) (t + t - t * t - 1.0);
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return (float) (t * t + t + t + 1.0);
            }
            return 0.0f;
        }

        float square (double phase, double increment) noexcept
        {
            auto value = phase < 0.5 ? 1.0f : -1.0f;
            value += blep (phase, increment);
            auto half = phase + 0.5;
            if (half >= 1.0)
                half -= 1.0;
            return value - blep (half, increment);
        }

        void advance (double& phase, double increment) noexcept
        {
            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        // A sine from a 4096-point table, read with straight interpolation (within about -130 dB of std::sin, at a
        // fraction of its cost: the kit's voices read one or two a sample each).
        float sine (double phase) noexcept
        {
            static const auto table = []
            {
                std::array<float, 4097> t {};
                for (size_t i = 0; i < t.size(); ++i)
                    t[i] = (float) std::sin (twoPi * (double) i / 4096.0);
                return t;
            }();
            const auto position = phase * 4096.0;
            const auto index = (int) position;
            const auto frac = (float) (position - index);
            return table[(size_t) index] + frac * (table[(size_t) index + 1] - table[(size_t) index]);
        }

        // 4-point cubic (Hermite) between x1 and x2.
        float cubic (float x0, float x1, float x2, float x3, float t) noexcept
        {
            const auto c1 = 0.5f * (x2 - x0);
            const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
            const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            return ((c3 * t + c2) * t + c1) * t + x1;
        }
    }

    //==============================================================================
    namespace sampleFiles
    {
        std::shared_ptr<const Sample> load (const juce::File& file)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
                return nullptr;
            const auto length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
            const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
            juce::AudioBuffer<float> buffer (channels, length);
            reader->read (&buffer, 0, length, 0, true, channels > 1);
            auto sample = std::make_shared<Sample>();
            sample->name = file.getFileNameWithoutExtension();
            sample->sampleRate = reader->sampleRate;
            for (int c = 0; c < channels; ++c)
                sample->channels.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);
            return sample;
        }

        juce::String encode (const Sample& sample)
        {
            if (sample.length() < 2)
                return {};
            auto peak = 0.0f;
            for (const auto& channel : sample.channels)
                for (const auto x : channel)
                    peak = juce::jmax (peak, std::abs (x));
            const auto scale = peak > 0.0f ? peak : 1.0f;
            juce::MemoryOutputStream compressed;
            {
                juce::GZIPCompressorOutputStream zip (compressed, 9);
                zip.writeInt ((int) sample.channels.size());
                zip.writeInt (sample.length());
                zip.writeDouble (sample.sampleRate);
                zip.writeFloat (scale);
                for (const auto& channel : sample.channels)
                    for (const auto x : channel)
                        zip.writeShort ((short) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, x / scale) * 32767.0f));
            }
            return juce::Base64::toBase64 (compressed.getData(), compressed.getDataSize());
        }

        std::shared_ptr<const Sample> decode (const juce::String& name, const juce::String& text)
        {
            juce::MemoryOutputStream compressed;
            if (! juce::Base64::convertFromBase64 (compressed, text))
                return nullptr;
            juce::MemoryInputStream input (compressed.getData(), compressed.getDataSize(), false);
            juce::GZIPDecompressorInputStream zip (input);
            const auto channels = zip.readInt(), length = zip.readInt();
            const auto rate = zip.readDouble();
            const auto scale = zip.readFloat();
            if (channels < 1 || channels > 2 || length < 2 || length > (int) (maxSeconds * 192000.0) || ! (rate > 0.0 && rate <= 768000.0))
                return nullptr;
            auto sample = std::make_shared<Sample>();
            sample->name = name;
            sample->sampleRate = rate;
            sample->channels.assign ((size_t) channels, std::vector<float> ((size_t) length));
            for (auto& channel : sample->channels)
                for (auto& x : channel)
                {
                    if (zip.isExhausted())
                        return nullptr;
                    x = (float) zip.readShort() / 32767.0f * scale;
                }
            return sample;
        }
    }

    void Svf::set (double hz, double q, double sampleRate) noexcept
    {
        const auto fc = juce::jlimit (10.0, 0.45 * sampleRate, hz);
        g = (float) std::tan (juce::MathConstants<double>::pi * fc / sampleRate);
        k = (float) (1.0 / q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        ic1 = ic2 = 0.0f;
    }

    //==============================================================================
    void Voice::start (const SlotSettings& s, int slotIndex, float velocityGain, double rate, std::uint32_t seed) noexcept
    {
        active = true;
        slot = slotIndex;
        model = s.model;
        choke = s.choke;
        sampleRate = rate;
        t = 0.0;
        fade = 1.0f;
        fadeStep = 0.0f;
        noise = Noise (seed);
        phase = {};
        increment = {};
        burst = 0;
        env = env2 = pitchEnv = click = 1.0f;
        envCoef = env2Coef = pitchCoef = clickCoef = 0.0f;
        sample = nullptr;
        level = velocityGain * modelLevel[(size_t) model];

        const auto ratio = std::exp2 (s.pitch / 12.0);
        const auto d = (double) juce::jlimit (0.0f, 1.0f, s.decay);
        const auto tone = (double) juce::jlimit (0.0f, 1.0f, s.tone);
        const auto snap = juce::jlimit (0.0f, 1.0f, s.snap);
        const auto tau = [rate] (double seconds) { return decayCoefficient (seconds, rate); };

        switch (model)
        {
            case Model::kick:
                // A sine falling from (1 + BEND) times its pitch to 52 Hz; TONE sets how far it falls; SNAP the
                // beater's click (high-passed noise, 1.5 ms).
                baseHz = 52.0 * ratio;
                bend = 0.5 + 6.0 * tone * tone;
                pitchCoef = tau (0.016);
                envCoef = tau (0.03 + 0.5 * d * d);
                clickCoef = tau (0.0015);
                noiseLevel = 0.7f * snap;
                f1.set (2500.0, 0.7, rate);
                break;

            case Model::snare:
                // Two drumhead modes (the second 1.59 times the first) that drop a little at the hit, and the wires:
                // noise, high-passed at 700 Hz and low-passed at TONE's 3-12 kHz; SNAP sets the wires' level.
                baseHz = 180.0 * ratio;
                bend = 0.2;
                pitchCoef = tau (0.008);
                envCoef = tau (0.03 + 0.15 * d * d);
                env2Coef = tau (0.03 + 0.25 * d * d);
                noiseLevel = 0.15f + 0.85f * snap;
                f1.set (700.0, 0.7, rate);
                f2.set (3000.0 + 9000.0 * tone, 0.7, rate);
                break;

            case Model::clap:
                // Noise through a band-pass (TONE moves it), in three slaps 10 ms apart and a fourth that rings for
                // DECAY. SNAP: the slaps' level against the ring.
                baseHz = ratio;
                env = env2 = 0.0f;
                envCoef = tau (0.0035);
                env2Coef = tau (0.04 + 0.25 * d * d);
                noiseLevel = 0.6f + 0.8f * snap;
                f1.set (1100.0 * ratio * (0.7 + 0.6 * tone), 1.6, rate);
                f2.set (400.0, 0.7, rate);
                break;

            case Model::closedHat:
            case Model::openHat:
            case Model::cymbal:
            {
                // Six squares at unrelated pitches, plus noise (SNAP: the sizzle), through two high-passes at TONE's
                // cutoff (hats around 7 kHz, the cymbal around 3.5 kHz).
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    increment[i] = juce::jmin (0.45, metalHz[i] * ratio / rate);
                    phase[i] = metalStart[i];
                }
                const auto cutoff = (model == Model::cymbal ? 3500.0 : 7000.0) * ratio * (0.7 + 0.6 * tone);
                f1.set (cutoff, 0.7, rate);
                f2.set (cutoff, 1.0, rate);
                f3.set (cutoff * (model == Model::cymbal ? 3.0 : 1.8), 0.7, rate);   // the very top tamed
                noiseLevel = 0.15f + 0.7f * snap;
                envCoef = tau (model == Model::closedHat ? 0.008 + 0.08 * d * d
                                                         : model == Model::openHat ? 0.05 + 0.4 * d * d : 0.25 + 2.8 * d * d);
                break;
            }

            case Model::tom:
                // A sine falling to its pitch (TONE: from how high), a second mode at 1.5 times that dies sooner, and
                // SNAP's stick click (band-passed noise, 4 ms).
                baseHz = 100.0 * ratio;
                bend = 0.15 + 1.2 * tone;
                pitchCoef = tau (0.05);
                envCoef = tau (0.05 + 0.5 * d * d);
                env2Coef = tau ((0.05 + 0.5 * d * d) * 0.45);
                clickCoef = tau (0.004);
                noiseLevel = 2.0f * snap;
                f1.set (2000.0 * ratio, 1.0, rate);
                break;

            case Model::rim:
                // A high and a low ring (TONE balances them), very short, with SNAP's click.
                increment[0] = 1700.0 * ratio / rate;
                increment[1] = 480.0 * ratio / rate;
                envCoef = tau (0.004 + 0.03 * d * d);
                env2Coef = tau ((0.004 + 0.03 * d * d) * 1.5);
                clickCoef = tau (0.002);
                mix = (float) (0.4 + 0.6 * tone);
                noiseLevel = 0.6f * snap;
                f1.set (2500.0 * ratio, 1.2, rate);
                f2.set (200.0, 0.7, rate);
                break;

            case Model::cowbell:
                // Two squares through a band-pass (TONE moves it); a quick bright strike (SNAP) and a ring (DECAY).
                increment[0] = 587.0 * ratio / rate;
                increment[1] = 845.0 * ratio / rate;
                f1.set (1800.0 * ratio * (0.7 + 0.6 * tone), 1.2, rate);
                envCoef = tau (0.012);
                env2Coef = tau (0.04 + 0.3 * d * d);
                noiseLevel = 0.3f + 0.7f * snap;
                break;

            case Model::sample:
            {
                sample = s.sample;
                if (sample == nullptr || sample->length() < 2)
                {
                    active = false;
                    return;
                }
                position = 0.0;
                step = ratio * sample->sampleRate / rate;
                // DECAY: the whole sample from the middle up; below it, a fade that shortens it.
                envCoef = d >= 0.5 ? 1.0f : tau (0.005 + 1.5 * std::pow (d / 0.5, 3.0));
                // TONE: darker (a low-pass down to 500 Hz) or thinner (a high-pass up to 2 kHz), flat at the middle.
                lowPole = highPole = 0.0f;
                lowState = highState = {};
                if (tone < 0.499)
                    lowPole = (float) (1.0 - std::exp (-twoPi * 20000.0 * std::pow (40.0, -(0.5 - tone) * 2.0) / rate));
                else if (tone > 0.501)
                    highPole = (float) (1.0 - std::exp (-twoPi * 20.0 * std::pow (100.0, (tone - 0.5) * 2.0) / rate));
                // SNAP: a softer attack (up to a 10 ms fade-in) or a spike (up to +9.5 dB, gone in a few ms).
                attackTime = snap < 0.5f ? (0.5f - snap) * 2.0f * 0.01f : 0.0f;
                spike = snap > 0.5f ? (snap - 0.5f) * 2.0f * 2.0f : 0.0f;
                clickCoef = tau (0.0015);
                break;
            }
        }
    }

    float Voice::next() noexcept
    {
        float out = 0.0f;
        switch (model)
        {
            case Model::kick:
            {
                pitchEnv *= pitchCoef;
                advance (phase[0], baseHz * (1.0 + bend * pitchEnv) / sampleRate);
                f1.process (noise.white());
                out = sine (phase[0]) * env + f1.hp * click * noiseLevel;
                env *= envCoef;
                click *= clickCoef;
                break;
            }
            case Model::snare:
            {
                pitchEnv *= pitchCoef;
                const auto hz = baseHz * (1.0 + bend * pitchEnv);
                advance (phase[0], hz / sampleRate);
                advance (phase[1], 1.59 * hz / sampleRate);
                const auto body = (0.65f * sine (phase[0]) + 0.35f * sine (phase[1])) * env;
                f1.process (noise.white());
                f2.process (f1.hp);
                out = 0.8f * body + f2.lp * env2 * noiseLevel;
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::clap:
            {
                if (burst < (int) clapBursts.size() && t >= clapBursts[(size_t) burst] / baseHz)
                {
                    env = 1.0f;
                    if (burst == (int) clapBursts.size() - 1)
                        env2 = 1.0f;
                    ++burst;
                }
                f2.process (noise.white());
                f1.process (f2.hp);
                out = f1.bp * (env * noiseLevel + env2 * 0.7f);
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::closedHat:
            case Model::openHat:
            case Model::cymbal:
            {
                auto metal = 0.0f;
                for (size_t i = 0; i < metalHz.size(); ++i)
                {
                    metal += square (phase[i], increment[i]);
                    advance (phase[i], increment[i]);
                }
                f1.process (metal * (1.0f / 6.0f) + noiseLevel * noise.white());
                f2.process (f1.hp);
                f3.process (f2.hp);
                out = f3.lp * env;
                env *= envCoef;
                break;
            }
            case Model::tom:
            {
                pitchEnv *= pitchCoef;
                const auto hz = baseHz * (1.0 + bend * pitchEnv);
                advance (phase[0], hz / sampleRate);
                advance (phase[1], 1.5 * hz / sampleRate);
                f1.process (noise.white());
                out = sine (phase[0]) * env + 0.2f * sine (phase[1]) * env2 + f1.bp * click * noiseLevel;
                env *= envCoef;
                env2 *= env2Coef;
                click *= clickCoef;
                break;
            }
            case Model::rim:
            {
                advance (phase[0], increment[0]);
                advance (phase[1], increment[1]);
                f1.process (noise.white());
                f2.process (mix * sine (phase[0]) * env + (1.0f - mix) * sine (phase[1]) * env2 + f1.bp * click * noiseLevel);
                out = f2.hp;
                env *= envCoef;
                env2 *= env2Coef;
                click *= clickCoef;
                break;
            }
            case Model::cowbell:
            {
                const auto bell = square (phase[0], increment[0]) + square (phase[1], increment[1]);
                advance (phase[0], increment[0]);
                advance (phase[1], increment[1]);
                f1.process (0.5f * bell);
                out = f1.bp * (noiseLevel * env + 0.5f * env2);
                env *= envCoef;
                env2 *= env2Coef;
                break;
            }
            case Model::sample:
                break;
        }
        t += 1.0 / sampleRate;
        return out * level;
    }

    bool Voice::finished() const noexcept
    {
        constexpr auto quiet = 1.0e-4f;   // -80 dB
        switch (model)
        {
            case Model::kick:    return env < quiet && click < quiet;
            case Model::snare:   return env < quiet && env2 < quiet;
            case Model::clap:    return burst >= (int) clapBursts.size() && env2 < quiet && env < quiet;
            case Model::tom:     return env < quiet && click < quiet;
            case Model::rim:     return env < quiet && env2 < quiet && click < quiet;
            case Model::cowbell: return env < quiet && env2 < quiet;
            case Model::sample:  return env < quiet;
            case Model::closedHat:
            case Model::openHat:
            case Model::cymbal:  return env < quiet;
        }
        return true;
    }

    void Voice::fadeOut (double seconds) noexcept
    {
        if (active)
            fadeStep = juce::jmax (fadeStep, (float) (1.0 / juce::jmax (1.0, seconds * sampleRate)));
    }

    void Voice::render (float* left, float* right, int numSamples) noexcept
    {
        if (model == Model::sample)
        {
            const auto length = sample->length();
            const auto channels = (int) sample->channels.size();
            for (int i = 0; i < numSamples; ++i)
            {
                const auto index = (int) position;
                if (index >= length - 1 || env < 1.0e-4f)
                {
                    active = false;
                    return;
                }
                const auto frac = (float) (position - index);
                auto gain = env * level * fade;
                if (attackTime > 0.0f && t < attackTime)
                    gain *= (float) t / attackTime;
                gain *= 1.0f + spike * click;
                click *= clickCoef;
                env *= envCoef;

                std::array<float, 2> out {};
                for (int c = 0; c < juce::jmin (2, channels); ++c)
                {
                    const auto* x = sample->channels[(size_t) c].data();
                    auto y = cubic (x[juce::jmax (0, index - 1)], x[index], x[index + 1], x[juce::jmin (length - 1, index + 2)], frac);
                    if (lowPole > 0.0f)
                        y = (lowState[(size_t) c] += lowPole * (y - lowState[(size_t) c]));
                    if (highPole > 0.0f)
                        y -= (highState[(size_t) c] += highPole * (y - highState[(size_t) c]));
                    out[(size_t) c] = y * gain;
                }
                left[i] += out[0];
                right[i] += channels > 1 ? out[1] : out[0];
                position += step;
                t += 1.0 / sampleRate;
                if (fadeStep > 0.0f && (fade -= fadeStep) <= 0.0f)
                {
                    active = false;
                    return;
                }
            }
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const auto x = next() * fade;
            left[i] += x;
            right[i] += x;
            if (fadeStep > 0.0f && (fade -= fadeStep) <= 0.0f)
            {
                active = false;
                return;
            }
            if ((i & 31) == 31)
            {
                if (finished())
                {
                    active = false;
                    return;
                }
                // Envelopes that have died away go to zero, not on into denormal numbers (very slow on most
                // processors, where nothing has turned them off: the page's preview, for one).
                for (auto* e : { &env, &env2, &pitchEnv, &click })
                    if (*e < 1.0e-12f)
                        *e = 0.0f;
            }
        }
        if (finished())
            active = false;
    }

    //==============================================================================
    void Kit::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        blockSize = juce::jmax (1, maxBlockSize);
        scratch.assign ((size_t) (2 * numSlots * blockSize), 0.0f);
        reset();
    }

    void Kit::reset() noexcept
    {
        for (auto& v : voices)
            v.active = false;
        slotGain.fill (1.0f);
        lastHit.fill (-1.0);
    }

    int Kit::getNumActiveVoices() const noexcept
    {
        int count = 0;
        for (const auto& v : voices)
            count += v.active ? 1 : 0;
        return count;
    }

    bool Kit::isSounding (int slot) const noexcept
    {
        for (const auto& v : voices)
            if (v.active && v.slot == slot)
                return true;
        return false;
    }

    float Kit::getHitAge (int slot) const noexcept
    {
        const auto age = lastHit[(size_t) juce::jlimit (0, numSlots - 1, slot)];
        return age >= 0.0 && age < 1.0 ? (float) age : -1.0f;
    }

    void Kit::trigger (int slot, float velocity, const KitSettings& settings) noexcept
    {
        if (slot >= 0 && slot < numSlots)
            startHit (slot, velocity, settings);
    }

    void Kit::startHit (int slot, float velocity, const KitSettings& settings) noexcept
    {
        const auto& s = settings.slots[(size_t) slot];
        lastHit[(size_t) slot] = 0.0;
        // A slot that's silent starts at its MUTE / SOLO level (it glides only while it sounds).
        if (! isSounding (slot))
            slotGain[(size_t) slot] = audible (settings, slot) ? 1.0f : 0.0f;
        if (s.model == Model::sample && (s.sample == nullptr || s.sample->length() < 2))
            return;

        // CHOKE: this hit stops the other slots in its group, quickly but without a click.
        if (s.choke > 0)
            for (auto& v : voices)
                if (v.active && v.choke == s.choke && v.slot != slot)
                    v.fadeOut (0.005);

        // Up to maxPerSlot hits of one slot overlap (a roll); past that, the oldest fades.
        Voice* oldestOfSlot = nullptr;
        int count = 0;
        for (auto& v : voices)
            if (v.active && v.slot == slot && v.fadeStep <= 0.0f)
            {
                ++count;
                if (oldestOfSlot == nullptr || v.age < oldestOfSlot->age)
                    oldestOfSlot = &v;
            }
        if (count >= maxPerSlot && oldestOfSlot != nullptr)
            oldestOfSlot->fadeOut (0.002);

        // A free voice, or (rarely: 48 hits at once) the oldest one, cut.
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (! v.active)
            {
                chosen = &v;
                break;
            }
        if (chosen == nullptr)
            for (auto& v : voices)
                if (chosen == nullptr || v.age < chosen->age)
                    chosen = &v;

        const auto v = juce::jlimit (0.0f, 1.0f, velocity);
        const auto velocityGain = 1.0f - settings.velocity + settings.velocity * v * v;
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        chosen->start (s, slot, velocityGain, sampleRate, noiseSeed);
        chosen->age = ++ages;
    }

    void Kit::process (const Outputs& outputs, int numSamples, const juce::MidiBuffer& midi, int midiStart, const KitSettings& settings) noexcept
    {
        int done = 0;
        const auto renderTo = [&] (int until)
        {
            while (done < until)
            {
                const auto n = juce::jmin (blockSize, until - done);
                render (outputs, done, n, settings);
                done += n;
            }
        };
        if (settings.on)
            for (const auto metadata : midi)
            {
                const auto position = metadata.samplePosition - midiStart;
                if (position < 0)
                    continue;
                if (position >= numSamples)
                    break;
                const auto message = metadata.getMessage();
                if (! message.isNoteOn())
                    continue;
                const auto slot = settings.slotFor (message.getNoteNumber());
                if (slot < 0)
                    continue;
                renderTo (position);
                startHit (slot, message.getFloatVelocity(), settings);
            }
        renderTo (numSamples);
    }

    bool Kit::audible (const KitSettings& settings, int slot) noexcept
    {
        auto anySolo = false;
        for (const auto& s : settings.slots)
            anySolo = anySolo || s.solo;
        const auto& s = settings.slots[(size_t) slot];
        return ! s.mute && (! anySolo || s.solo);
    }

    void Kit::render (const Outputs& outputs, int offset, int numSamples, const KitSettings& settings) noexcept
    {
        const auto seconds = numSamples / sampleRate;
        for (auto& age : lastHit)
            if (age >= 0.0)
                age += seconds;

        std::array<bool, numSlots> used {};
        for (const auto& v : voices)
            if (v.active)
                used[(size_t) v.slot] = true;

        const auto glide = (float) (1.0 / (0.005 * sampleRate));   // MUTE / SOLO: 5 ms

        for (int slot = 0; slot < numSlots; ++slot)
        {
            const auto& s = settings.slots[(size_t) slot];
            const auto target = audible (settings, slot) ? 1.0f : 0.0f;
            if (! used[(size_t) slot])
            {
                slotGain[(size_t) slot] = target;
                continue;
            }

            auto* left = scratch.data() + (size_t) (2 * slot * blockSize);
            auto* right = left + blockSize;
            std::fill (left, left + numSamples, 0.0f);
            std::fill (right, right + numSamples, 0.0f);
            for (auto& v : voices)
                if (v.active && v.slot == slot)
                    v.render (left, right, numSamples);

            // Into the slot's bus, or the mix and (while each effect is on) the sends.
            const auto gain = settings.gain * s.gain;
            const auto panLeft = s.pan > 0.0f ? 1.0f - s.pan : 1.0f;
            const auto panRight = s.pan < 0.0f ? 1.0f + s.pan : 1.0f;
            const auto bus = s.insertBus - 1;
            const auto toBus = bus >= 0 && bus < (int) outputs.busLeft.size() && outputs.busLeft[(size_t) bus] != nullptr;
            auto* outLeft = (toBus ? outputs.busLeft[(size_t) bus] : outputs.left) + offset;
            auto* outRight = (toBus ? outputs.busRight[(size_t) bus] : outputs.right) + offset;
            std::array<float*, 2> sendLeft {}, sendRight {};
            for (size_t k = 0; k < sendLeft.size(); ++k)
                if (! toBus && settings.sendOn[k] && outputs.sendLeft[k] != nullptr && s.send[k] > 0.0f)
                {
                    sendLeft[k] = outputs.sendLeft[k] + offset;
                    sendRight[k] = outputs.sendRight[k] + offset;
                }

            auto& current = slotGain[(size_t) slot];
            for (int i = 0; i < numSamples; ++i)
            {
                if (current < target)
                    current = juce::jmin (target, current + glide);
                else if (current > target)
                    current = juce::jmax (target, current - glide);
                const auto g = current * gain;
                const auto l = left[i] * g * panLeft, r = right[i] * g * panRight;
                outLeft[i] += l;
                outRight[i] += r;
                for (size_t k = 0; k < sendLeft.size(); ++k)
                    if (sendLeft[k] != nullptr)
                    {
                        sendLeft[k][i] += l * s.send[k];
                        sendRight[k][i] += r * s.send[k];
                    }
            }
        }
    }

    juce::AudioBuffer<float> Kit::renderHit (const SlotSettings& settings, float velocity, double sampleRate, double seconds)
    {
        juce::AudioBuffer<float> buffer (2, juce::jmax (1, (int) (seconds * sampleRate)));
        buffer.clear();
        Voice voice;
        voice.start (settings, 0, velocity * velocity, sampleRate, 0x2545f491u);
        for (int done = 0; done < buffer.getNumSamples() && voice.active; done += 256)
            voice.render (buffer.getWritePointer (0, done), buffer.getWritePointer (1, done),
                          juce::jmin (256, buffer.getNumSamples() - done));
        buffer.applyGain (settings.gain);
        return buffer;
    }
}
