#pragma once

#include <juce_core/juce_core.h>

#include "MultiPoint.h"

#include <array>
#include <cmath>

// MODULATION (stage 4a): the mod matrix, LFOs, macros and the controllers,
// modelled on Avenger's (our own design and code). A route takes a SOURCE
// (an LFO, a macro, the mod wheel, velocity...) to a DESTINATION (any of the
// knobs the engine follows live) by an AMOUNT (-100% to +100% of the knob's
// travel), either added (+) or as a share of the travel left (%), with LAG
// (slowing the source down) and a CURVE (bending it).
//
// The engine works modulation out per note, once per 16 samples, on its
// own copy of the settings: so a velocity or an LFO running per note moves
// each note's knobs on its own.
namespace synth
{
    struct Settings;

    namespace mod
    {
        // Sources, saved by name (`sourceIds`): never rename one; append new ones.
        enum class Source { none, lfo1, lfo2, lfo3, lfo4, macro1, macro2, macro3, button1, button2,
                            modWheel, pitchBend, velocity, aftertouch, keytrack, breath, expression, sustain, random,
                            // 0.10.0: the modulation envelopes
                            env1, env2, env3, env4, env5, env6, env7, env8,
                            // 0.11.0: the PITCH module's envelope (-1 to 1)
                            pitchEnvelope,
                            // 0.12.0: the STEP SEQUENCER (0 to 1)
                            stepSeq };
        inline constexpr int numSources = 29;
        inline constexpr std::array<const char*, numSources> sourceIds {
            "none", "lfo1", "lfo2", "lfo3", "lfo4", "macro1", "macro2", "macro3", "button1", "button2",
            "modWheel", "pitchBend", "velocity", "aftertouch", "keytrack", "breath", "expression", "sustain", "random",
            "env1", "env2", "env3", "env4", "env5", "env6", "env7", "env8", "pitchEnv", "stepSeq" };
        inline constexpr std::array<const char*, numSources> sourceNames {
            "--", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Macro 1", "Macro 2", "Macro 3", "Button 1", "Button 2",
            "Mod Wheel", "Pitch Bend", "Velocity", "Aftertouch", "Keytrack", "Breath", "Expression", "Sustain Pedal", "Random",
            "Env 1", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6", "Env 7", "Env 8", "Pitch Env", "Step Seq" };
        inline Source sourceFromId (const juce::String& id) noexcept
        {
            for (int i = 0; i < numSources; ++i)
                if (id == sourceIds[(size_t) i])
                    return (Source) i;
            return Source::none;
        }
        // LFOs and the pitch wheel swing both ways (-1 to 1); the rest run 0 to 1.
        inline bool isBipolar (Source s) noexcept
        {
            return (s >= Source::lfo1 && s <= Source::lfo4) || s == Source::pitchBend || s == Source::pitchEnvelope;
        }

        // A destination: which of the engine's settings a route moves.
        enum class Field
        {
            oscVolume, oscPan, oscTranspose, oscFine, oscGain, oscWidth, oscNoise, oscNoiseColour, oscSub, oscTableIndex,
            oscFormant, oscCrush, oscSyncAmount, oscSyncEnv, oscFmAmount, oscAmAmount, oscModRate,
            oscUnisonMix, oscUnisonDetune, oscUnisonSpread, oscVibratoRate, oscVibratoAmount,
            oscFftLp, oscFftHp, oscFftSlope, oscFftVSpeed,
            filterCutoff, filterResonance, filterDrive, filterKeytrack, filterEnvelope,
            shaperDrive, shaperMix, shaperOutput,
            ampVolume, ampPan,
            // Stage 4f: the bends, the shapers' EQ, the PITCH module, TRAVEL and the step sequencer
            oscSkew, oscPinch, oscTravelStrength, oscTravelPhase,
            shaperEq1Gain, shaperEq1Freq, shaperEq1Q, shaperEq2Gain, shaperEq2Freq, shaperEq2Q,
            pitchEnvRange, pitchKeytrack, stepSeqVolMix
        };
        struct Target
        {
            Field field = Field::ampVolume;
            int index = 0;   // which oscillator or shaper
        };

        // Writes a destination's value (in its real units) into the settings.
        void apply (Settings&, Target, float value) noexcept;

        // A route as the engine plays it: its destination's knob position now
        // (0 to 1), and how to turn a position back into the setting's units.
        struct Route
        {
            Source source = Source::none;
            Target target;
            float base = 0.0f;          // the knob's position, 0 to 1
            float amount = 0.0f;        // -1 to 1
            bool percent = false;       // % (a share of the travel left) rather than + (added)
            float lagMs = 0.0f;
            float curve = 0.0f;         // -1 to 1: below 0 the source rises fast first, above slow first
            const juce::NormalisableRange<float>* range = nullptr;
        };
        inline constexpr int maxRoutes = 64;

        // CURVE: bends the source (keeping its sign), 4^curve as the power.
        inline float bend (float value, float curve) noexcept
        {
            if (std::abs (curve) < 1.0e-4f || juce::exactlyEqual (value, 0.0f))
                return value;
            const auto shaped = std::pow (std::abs (value), std::exp2 (2.0f * curve));
            return value < 0.0f ? -shaped : shaped;
        }

        // The knob's position with the route's push on it (`push` = amount x the source, -1 to 1).
        inline float combine (float base, float push, bool percent) noexcept
        {
            if (percent)
                return juce::jlimit (0.0f, 1.0f, push >= 0.0f ? base + push * (1.0f - base) : base + push * base);
            return juce::jlimit (0.0f, 1.0f, base + push);
        }

        //======================================================================
        // LFOs: 4, each running per note or once for the whole synth.
        inline constexpr int numLfos = 4;

        // Saved by index (lfo*Wave, lfo*Sync, lfo*Division, lfo*Trigger): only ever append.
        // CUSTOM 1 to 3 (0.10.0): the preset's drawn shapes.
        enum class LfoWave { sine, triangle, sawUp, sawDown, square, sampleHold, smoothRandom, custom1, custom2, custom3 };
        inline const juce::StringArray lfoWaveNames { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd",
                                                      "Custom 1", "Custom 2", "Custom 3" };
        inline constexpr int numLfoShapes = 3;
        inline constexpr int numEnvelopes = 8;
        enum class LfoSync { free, straight, triplet, dotted };
        inline const juce::StringArray lfoSyncNames { "Free", "Note", "Triplet", "Dotted" };
        inline const juce::StringArray lfoDivisionNames { "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        inline constexpr std::array<double, 10> lfoDivisionBeats { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
        // VOICE: each note its own (Avenger's "all oscs"); GLOBAL: one, always
        // running; FIRST NOTE: one, restarted when a note starts with none held;
        // LAST NOTE: one, restarted by every note.
        // ARP NOTES (0.16.0): one, restarted by every note an arpeggiator plays.
        enum class LfoTrigger { voice, global, firstNote, lastNote, arpNotes };
        inline const juce::StringArray lfoTriggerNames { "Voice", "Global", "First Note", "Last Note", "Arp Notes" };

        struct LfoSettings
        {
            LfoWave wave = LfoWave::sine;
            float rateHz = 2.0f;           // FREE
            LfoSync sync = LfoSync::free;
            int division = 5;              // 1/4
            LfoTrigger trigger = LfoTrigger::voice;
            float phase = 0.0f;            // 0 to 1: where it starts
            float offset = 0.0f;           // -1 to 1: moves the whole wave up or down, fitted in (at +1: 0 to 1, never clipped)
            float delayMs = 0.0f;
            float fadeInMs = 0.0f;
            bool oneShot = false;          // once through, then holds its last value

            // Cycles a second at `bpm`.
            double cyclesPerSecond (double bpm) const noexcept
            {
                if (sync == LfoSync::free)
                    return rateHz;
                auto beats = lfoDivisionBeats[(size_t) juce::jlimit (0, (int) lfoDivisionBeats.size() - 1, division)];
                if (sync == LfoSync::triplet)
                    beats *= 2.0 / 3.0;
                else if (sync == LfoSync::dotted)
                    beats *= 1.5;
                return juce::jmax (1.0, bpm) / 60.0 / beats;
            }
        };

        // The wave at `phase` (0 to 1), -1 to 1. The random waves use the
        // values chosen for this cycle and the last.
        inline float lfoWave (LfoWave wave, float phase, float held, float previousHeld, const Shape* shapes = nullptr) noexcept
        {
            switch (wave)
            {
                case LfoWave::custom1:
                case LfoWave::custom2:
                case LfoWave::custom3:
                    return shapes != nullptr ? lfoShapeValue (shapes[(int) wave - (int) LfoWave::custom1], phase) : 0.0f;
                case LfoWave::sine: return std::sin (juce::MathConstants<float>::twoPi * phase);
                case LfoWave::triangle: return phase < 0.25f ? 4.0f * phase : phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f;
                case LfoWave::sawUp: return 2.0f * phase - 1.0f;
                case LfoWave::sawDown: return 1.0f - 2.0f * phase;
                case LfoWave::square: return phase < 0.5f ? 1.0f : -1.0f;
                case LfoWave::sampleHold: return held;
                case LfoWave::smoothRandom:
                {
                    const auto t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * phase);
                    return previousHeld + (held - previousHeld) * t;
                }
            }
            return 0.0f;
        }

        // One running LFO.
        struct LfoState
        {
            double phase = 0.0;             // 0 to 1 within the cycle (more than 1 once a one-shot is done)
            float held = 0.0f, previousHeld = 0.0f;
            double age = 0.0;               // seconds since it started (for DELAY and FADE IN)

            void start (const LfoSettings& s, juce::Random& random) noexcept
            {
                phase = s.phase;
                previousHeld = random.nextFloat() * 2.0f - 1.0f;
                held = random.nextFloat() * 2.0f - 1.0f;
                age = 0.0;
            }

            // Its value now (-1 to 1, with OFFSET and the FADE IN), then moves on by `seconds`.
            float next (const LfoSettings& s, double cyclesPerSecond, double seconds, juce::Random& random, const Shape* shapes = nullptr) noexcept
            {
                const auto delay = s.delayMs * 0.001;
                float value = 0.0f;
                if (age >= delay)
                {
                    const auto shown = s.oneShot && phase >= 1.0 ? 1.0 : phase - std::floor (phase);
                    const auto wave = s.oneShot && phase >= 1.0 ? lfoWave (s.wave, 0.999999f, held, previousHeld, shapes)
                                                                : lfoWave (s.wave, (float) shown, held, previousHeld, shapes);
                    auto fade = 1.0f;
                    if (s.fadeInMs > 0.0f)
                        fade = (float) juce::jlimit (0.0, 1.0, (age - delay) / (s.fadeInMs * 0.001));
                    value = (wave * fade + s.offset) / (1.0f + std::abs (s.offset));

                    const auto before = phase;
                    phase += cyclesPerSecond * seconds;
                    if (s.oneShot)
                        phase = juce::jmin (phase, 1.0);
                    else if (phase >= 1.0)
                    {
                        // A new cycle: the random waves choose their next value.
                        if (std::floor (phase) > std::floor (before))
                        {
                            previousHeld = held;
                            held = random.nextFloat() * 2.0f - 1.0f;
                        }
                        phase -= std::floor (phase);
                    }
                }
                else
                    value = s.offset / (1.0f + std::abs (s.offset));
                age += seconds;
                return value;
            }
        };

        // Everything about modulation the engine is told each block.
        struct ModulationSettings
        {
            std::array<LfoSettings, numLfos> lfos {};
            std::array<float, 3> macros {};
            std::array<bool, 2> buttons {};
            const Route* routes = nullptr;    // the processor's (or a test's), `numRoutes` of them
            int numRoutes = 0;
            // The modulation envelopes (0.10.0): their settings and shapes, and the LFOs' drawn shapes
            // (`numEnvelopes` and `numLfoShapes` of them; null: none).
            std::array<EnvelopeSettings, numEnvelopes> envelopes {};
            const Shape* envelopeShapes = nullptr;
            const Shape* lfoShapes = nullptr;
            // Which sources any route uses (bit per Source): the others aren't worked out.
            std::uint32_t usedSources = 0xffffffffu;
            bool uses (Source source) const noexcept { return (usedSources >> (int) source) & 1u; }
            double bpm = 120.0;
            double ppq = -1.0;                // the song position at the block's start, in beats (-1: stopped)
            // CONTROLLER TO MOD WHEEL (stage 4f): breath, expression, aftertouch, sustain also drive the mod wheel
            // (the wheel reads the highest of itself and those switched on).
            std::array<bool, 4> toModWheel {};
        };
    }
}
