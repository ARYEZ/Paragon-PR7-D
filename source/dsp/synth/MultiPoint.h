#pragma once

#include <juce_core/juce_core.h>

#include "Envelope.h"

#include <array>
#include <cmath>

// MULTIPOINT shapes (stage 4b): the modulation envelopes and the LFOs'
// drawn shapes, modelled on Avenger's multipoint envelopes (our own design
// and code). A shape is up to 64 points, each with the curve of the line to
// the next (as the AMP envelope's curves: below 0 most of the change comes
// early). An envelope's points are in seconds (or beats, when SYNC is on),
// with a SUSTAIN point and two LOOP points; an LFO shape's run 0 to 1 across
// one cycle.
namespace synth::mod
{
    inline constexpr int maxShapePoints = 64;

    struct ShapePoint
    {
        float x = 0.0f;       // time (seconds or beats), or 0 to 1 across an LFO cycle
        float y = 0.0f;       // 0 to 1
        float curve = 0.0f;   // -1 to 1: the line to the next point
    };

    // What the audio thread reads: plain data, so it can be handed over by pointer.
    struct Shape
    {
        std::array<ShapePoint, maxShapePoints> points {};
        int count = 0;
        int sustain = -1, loopStart = -1, loopEnd = -1;   // point indices (-1: none)

        float length() const noexcept { return count > 0 ? points[(size_t) count - 1].x : 0.0f; }

        // The value at `x` (held before the first point and after the last).
        float valueAt (double x) const noexcept
        {
            if (count <= 0)
                return 0.0f;
            if (x <= points[0].x)
                return points[0].y;
            for (int i = 1; i < count; ++i)
            {
                const auto& b = points[(size_t) i];
                if (x <= b.x)
                {
                    const auto& a = points[(size_t) i - 1];
                    const auto span = b.x - a.x;
                    if (span <= 1.0e-9f)
                        return b.y;
                    const auto t = (float) ((x - a.x) / span);
                    return a.y + (b.y - a.y) * synth::bend (t, a.curve);
                }
            }
            return points[(size_t) count - 1].y;
        }

        float xOf (int index) const noexcept { return index >= 0 && index < count ? points[(size_t) index].x : 0.0f; }
    };

    // Saved by index (menv*Mode, menv*Trigger): only ever append.
    // ONE SHOT: once through, then holds the last point. SUSTAIN: holds at
    // the SUSTAIN point while the key is down (a release before it jumps
    // there). SUSTAIN JUMP: the same, but a release before it jumps to the
    // first place after it with the same level, so nothing jumps. LOOP:
    // round between the two LOOP points for ever. LOOP+REL: round them while
    // the key is down, then from the second to the end. PING PONG (+REL):
    // the same, back and forth.
    enum class EnvelopeMode { oneShot, sustain, sustainJump, loop, loopRelease, pingPong, pingPongRelease };
    inline const juce::StringArray envelopeModeNames { "One Shot", "Sustain", "Sus Jump", "Loop", "Loop+Rel", "Ping Pong", "PingPong+Rel" };
    // VOICE: each note its own; FIRST NOTE: one, started when a note starts with none held; LAST NOTE: one, restarted by every note.
    // ARP NOTES (0.16.0): one, restarted by every note an arpeggiator plays (the keys' own notes don't).
    enum class EnvelopeTrigger { voice, firstNote, lastNote, arpNotes };
    inline const juce::StringArray envelopeTriggerNames { "Voice", "First Note", "Last Note", "Arp Notes" };

    struct EnvelopeSettings
    {
        EnvelopeMode mode = EnvelopeMode::oneShot;
        EnvelopeTrigger trigger = EnvelopeTrigger::voice;
        bool sync = false;          // its times in beats rather than seconds
        float speed = 1.0f;         // 0.1 to 10: how fast it runs through
        int steps = 0;              // 0: smooth; else this many levels
        bool flip = false;          // upside down
        float offset = 0.0f;        // 0 to 1: the level counted as zero (0.5: it swings both ways)

        bool loops() const noexcept { return mode == EnvelopeMode::loop || mode == EnvelopeMode::loopRelease
                                          || mode == EnvelopeMode::pingPong || mode == EnvelopeMode::pingPongRelease; }
        bool pingPongs() const noexcept { return mode == EnvelopeMode::pingPong || mode == EnvelopeMode::pingPongRelease; }
        bool releasesFromLoop() const noexcept { return mode == EnvelopeMode::loopRelease || mode == EnvelopeMode::pingPongRelease; }

        // The shape's level made into the source's value: STEPS, FLIP, OFFSET.
        float output (float level) const noexcept
        {
            if (steps > 0)
                level = std::round (level * (float) steps) / (float) steps;
            if (flip)
                level = 1.0f - level;
            if (offset > 0.0f)
                level = (level - offset) / juce::jmax (offset, 1.0f - offset);
            return level;
        }
    };

    // One running envelope.
    struct EnvelopeState
    {
        double position = 0.0;     // along the shape's x
        int direction = 1;         // ping pong: 1 forwards, -1 back
        bool released = false, releaseHandled = true, running = false;

        void start() noexcept
        {
            position = 0.0;
            direction = 1;
            released = false;
            releaseHandled = true;
            running = true;
        }
        void release() noexcept
        {
            if (running && ! released)
            {
                released = true;
                releaseHandled = false;
            }
        }

        // Its value now (as EnvelopeSettings::output makes it), then moves on by `seconds`.
        float next (const Shape& shape, const EnvelopeSettings& s, double seconds, double bpm) noexcept
        {
            if (shape.count <= 0)
                return 0.0f;
            if (! running)
                return s.output (shape.points[0].y);
            const auto end = (double) shape.length();
            const auto hasLoop = shape.loopStart >= 0 && shape.loopEnd > shape.loopStart;
            const auto loopStart = (double) shape.xOf (shape.loopStart), loopEnd = (double) shape.xOf (shape.loopEnd);
            const auto hasSustain = shape.sustain >= 0;
            const auto sustainAt = (double) shape.xOf (shape.sustain);

            // A release just happened: where to go from here.
            if (! releaseHandled)
            {
                releaseHandled = true;
                direction = 1;
                if ((s.mode == EnvelopeMode::sustain || s.mode == EnvelopeMode::sustainJump) && hasSustain && position < sustainAt)
                {
                    const auto now = shape.valueAt (position);
                    position = sustainAt;
                    if (s.mode == EnvelopeMode::sustainJump)
                    {
                        // The first place after the sustain point at the level we're at, so nothing jumps.
                        constexpr int searchSteps = 256;
                        for (int i = 0; i < searchSteps; ++i)
                        {
                            const auto x0 = sustainAt + (end - sustainAt) * i / searchSteps;
                            const auto x1 = sustainAt + (end - sustainAt) * (i + 1) / searchSteps;
                            const auto a0 = shape.valueAt (x0) - now, a1 = shape.valueAt (x1) - now;
                            if (a0 * a1 <= 0.0f)
                            {
                                position = std::abs (a1 - a0) > 1.0e-9f ? x0 + (x1 - x0) * (double) (a0 / (a0 - a1)) : x0;
                                break;
                            }
                        }
                    }
                }
                else if (s.releasesFromLoop() && hasLoop)
                    position = std::max (position, loopEnd);
            }

            const auto level = shape.valueAt (position);

            // Move on.
            auto step = seconds * (double) s.speed;
            if (s.sync)
                step *= juce::jmax (1.0, bpm) / 60.0;   // beats
            const auto holdingAtSustain = (s.mode == EnvelopeMode::sustain || s.mode == EnvelopeMode::sustainJump) && hasSustain && ! released;
            const auto looping = s.loops() && hasLoop && ! (s.releasesFromLoop() && released);
            if (looping)
            {
                if (s.pingPongs())
                {
                    // Back and forth between the loop points (running in from the start first).
                    position += step * direction;
                    if (direction > 0 && position > loopEnd)
                    {
                        position = juce::jmax (loopStart, 2.0 * loopEnd - position);
                        direction = -1;
                    }
                    else if (direction < 0 && position < loopStart)
                    {
                        position = juce::jmin (loopEnd, 2.0 * loopStart - position);
                        direction = 1;
                    }
                }
                else
                {
                    position += step;
                    const auto span = loopEnd - loopStart;
                    if (position > loopEnd && span > 1.0e-9)
                        position = loopStart + std::fmod (position - loopStart, span);
                    else if (position > loopEnd)
                        position = loopEnd;
                }
            }
            else
            {
                position += step;
                if (holdingAtSustain && position >= sustainAt)
                    position = sustainAt;
                position = std::min (position, end);
            }
            return s.output (level);
        }
    };

    // A drawn LFO shape's level at `phase` (0 to 1), as -1 to 1.
    inline float lfoShapeValue (const Shape& shape, float phase) noexcept
    {
        return shape.count > 0 ? shape.valueAt (phase) * 2.0f - 1.0f : 0.0f;
    }
}
