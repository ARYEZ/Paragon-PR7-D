#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// An attack, hold, decay, sustain, release envelope, as on Avenger's AMP and
// FILTER sections (our own code and curve maths). Attack, decay and release
// can each be bent: 0 is a straight line; negative bends move fast at first
// and slow down (the shape of most natural sounds, and of an analog
// envelope's decay); positive bends start slowly and speed up.
//
// Retriggering (a new note on a voice that's still sounding, or a mono
// legato-off note) starts the attack from wherever the envelope is, so it
// never jumps to zero and clicks.
namespace synth
{
    struct EnvelopeSettings
    {
        float attackMs = 1.0f, holdMs = 0.0f, decayMs = 300.0f, sustain = 1.0f, releaseMs = 200.0f;
        float attackCurve = 0.0f, decayCurve = 0.0f, releaseCurve = 0.0f;   // -1 to 1
    };

    // Bends 0-1 progress: (e^(k p) - 1) / (e^k - 1), k from the curve. Straight
    // when the curve is 0. At -1 a fall is (nearly) a true exponential, falling
    // at a steady rate in dB, as a plucked string or a piano note dies away
    // (e^-10: it has fallen 87 dB by the end).
    inline float bend (float progress, float curve) noexcept
    {
        const auto k = curve * 10.0f;   // negative: most of the way in the first moments (fast first)
        if (std::abs (k) < 1.0e-3f)
            return progress;
        return (std::exp (k * progress) - 1.0f) / (std::exp (k) - 1.0f);
    }

    class Envelope
    {
    public:
        enum class Stage { idle, attack, hold, decay, sustain, release };

        void setSampleRate (double rate) noexcept
        {
            sampleRate = rate;
            sustainGlide = (float) (1.0 / (0.01 * rate));
        }

        void trigger (const EnvelopeSettings& s) noexcept
        {
            settings = s;
            from = level;   // from where it is: no click on a retrigger
            enter (Stage::attack);
        }

        void release() noexcept
        {
            if (stage == Stage::idle || stage == Stage::release)
                return;
            from = level;
            enter (Stage::release);
        }

        // New settings while a note plays: the times and levels a stage hasn't reached yet follow at once.
        void update (const EnvelopeSettings& s) noexcept { settings = s; }

        // The next sample's value, 0 to 1.
        float next() noexcept { return step (1); }

        // Moves on `count` samples at once and returns the value there: the
        // filter's envelope is read this way, once per few samples. (A stage
        // that ends part-way through the step starts the next one at its
        // beginning: at most `count` samples late, a fraction of a millisecond.)
        float step (int count) noexcept
        {
            switch (stage)
            {
                case Stage::idle:
                    return 0.0f;
                case Stage::attack:
                    level = from + (1.0f - from) * bend (advance (settings.attackMs, count), settings.attackCurve);
                    if (done())
                        enter (Stage::hold);
                    break;
                case Stage::hold:
                    level = 1.0f;
                    advance (settings.holdMs, count);
                    if (done())
                        enter (Stage::decay);
                    break;
                case Stage::decay:
                    level = 1.0f + (settings.sustain - 1.0f) * bend (advance (settings.decayMs, count), settings.decayCurve);
                    if (done())
                        enter (Stage::sustain);
                    break;
                case Stage::sustain:
                    // Follows SUSTAIN smoothly (in about 10 ms) if it's turned while the note is held.
                    level += (settings.sustain - level) * juce::jmin (1.0f, (float) count * sustainGlide);
                    break;
                case Stage::release:
                    level = from * (1.0f - bend (advance (settings.releaseMs, count), settings.releaseCurve));
                    if (done())
                    {
                        level = 0.0f;
                        enter (Stage::idle);
                    }
                    break;
            }
            return level;
        }

        Stage getStage() const noexcept { return stage; }
        float getLevel() const noexcept { return level; }
        bool isActive() const noexcept { return stage != Stage::idle; }
        bool isReleasing() const noexcept { return stage == Stage::release; }
        // Reached a sustain of zero: silent from here, until it's retriggered.
        bool isSilentSustain() const noexcept { return stage == Stage::sustain && settings.sustain <= 0.0f && level < 1.0e-6f; }

        void reset() noexcept
        {
            stage = Stage::idle;
            level = from = 0.0f;
            position = 0.0;
        }

    private:
        void enter (Stage newStage) noexcept
        {
            stage = newStage;
            position = 0.0;
            if (stage == Stage::decay && settings.decayMs <= 0.0f)
                level = settings.sustain;
        }

        // Steps through the current stage (`ms` long) and returns how far through it is, 0 to 1.
        float advance (float ms, int count) noexcept
        {
            const auto length = (double) ms * 0.001 * sampleRate;
            if (length < 1.0)
            {
                position = 1.0;
                return 1.0f;
            }
            position += count / length;
            return (float) juce::jmin (1.0, position);
        }

        bool done() const noexcept { return position >= 1.0; }

        EnvelopeSettings settings;
        Stage stage = Stage::idle;
        double sampleRate = 48000.0, position = 0.0;
        float sustainGlide = 1.0f / 480.0f;
        float level = 0.0f, from = 0.0f;
    };
}
