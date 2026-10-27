#include "FactoryRigs.h"

#include "Parameters.h"
#include "dsp/synth/SynthSettings.h"

// PR7-K's first factory presets (0.1.0): a few starting points that show
// what stage 1 can do (oscillators, sub, noise, the filter and its
// envelope, mono and glide). Our own sounds, set up from common synth
// recipes, not copies of anyone's presets, and not yet tuned by ear: the
// full factory set comes in a later stage (presets), once the engine has
// unison, wavetables, modulation and effects to build them with.
namespace
{
    // A preset under construction: anything it doesn't set stays at its default.
    struct Builder
    {
        Rig rig;

        Builder (const juce::String& name, int number, const juce::StringArray& tags, const juce::String& notes)
        {
            rig.name = name;
            rig.bank = 0;
            rig.number = number;
            rig.tags = tags;
            rig.notes = notes;
        }

        Builder& set (const juce::String& id, float value) { rig.parameters[id] = value; return *this; }
        Builder& osc (int index, const char* what, float value) { return set (ParamIDs::osc (index, what), value); }

        Builder& filter (synth::FilterType type, float cutoffHz, float resonance, float envelope)
        {
            set (ParamIDs::fltOn, 1.0f);
            set (ParamIDs::fltType, (float) type);
            set (ParamIDs::fltCutoff, cutoffHz);
            set (ParamIDs::fltRes, resonance);
            return set (ParamIDs::fltEnv, envelope);
        }

        Builder& filterEnvelope (float attack, float decay, float sustain, float release)
        {
            set (ParamIDs::fltAttack, attack);
            set (ParamIDs::fltDecay, decay);
            set (ParamIDs::fltSustain, sustain);
            return set (ParamIDs::fltRelease, release);
        }

        Builder& ampEnvelope (float attack, float decay, float sustain, float release)
        {
            set (ParamIDs::ampAttack, attack);
            set (ParamIDs::ampDecay, decay);
            set (ParamIDs::ampSustain, sustain);
            return set (ParamIDs::ampRelease, release);
        }

        Builder& mono (float glideMs)
        {
            set (ParamIDs::playMode, (float) synth::PlayMode::mono);
            set (ParamIDs::legato, 1.0f);
            set (ParamIDs::glideMode, (float) synth::GlideMode::legato);
            return set (ParamIDs::glide, glideMs);
        }
    };

    constexpr auto saw = (float) synth::OscWave::saw, pulse = (float) synth::OscWave::pulse,
                   sine = (float) synth::OscWave::sine;
}

std::vector<Rig> makeFactoryRigs()
{
    using synth::FilterType;
    std::vector<Rig> rigs;
    const auto add = [&rigs] (Builder& builder) { rigs.push_back (builder.rig); };

    // A01: what a new sound starts as (the parameters' defaults): one saw, no filter.
    Builder init ("Init", 1, { "Init" }, "One saw, straight to the amp: a blank page.");
    add (init);

    Builder bass ("Classic Bass", 2, { "Bass", "Mono" },
                  "A saw with a sine sub an octave down through a 24 dB low-pass that snaps shut. Mono, gliding between overlapping notes.");
    bass.osc (0, "Sub", 0.8f).osc (0, "SubOct", 1.0f)
        .filter (FilterType::lp24, 180.0f, 0.25f, 0.45f).filterEnvelope (0.0f, 260.0f, 0.15f, 120.0f)
        .set (ParamIDs::fltKey, 0.5f).set (ParamIDs::fltVel, 0.3f)
        .ampEnvelope (1.0f, 400.0f, 0.8f, 60.0f).mono (45.0f);
    add (bass);

    Builder pluck ("Saw Pluck", 3, { "Pluck", "Keys" },
                   "Two saws a few cents apart, the filter and amp both decaying to nothing: plucked, for arps and chords.");
    pluck.osc (0, "Fine", -6.0f).osc (0, "Pan", -0.3f)
         .osc (1, "On", 1.0f).osc (1, "Wave", saw).osc (1, "Fine", 6.0f).osc (1, "Pan", 0.3f)
         .filter (FilterType::lp24, 350.0f, 0.2f, 0.6f).filterEnvelope (0.0f, 320.0f, 0.0f, 300.0f)
         .set (ParamIDs::fltKey, 0.6f).set (ParamIDs::fltEnvVel, 0.5f)
         .ampEnvelope (0.5f, 700.0f, 0.0f, 350.0f).set (ParamIDs::ampDecayCurve, -0.5f).set (ParamIDs::ampReleaseCurve, -0.5f);
    add (pluck);

    Builder lead ("Square Lead", 4, { "Lead", "Mono" },
                  "A square with a saw an octave up underneath it, a little resonance, and glide between overlapping notes.");
    lead.osc (0, "Wave", pulse).osc (1, "On", 1.0f).osc (1, "Wave", saw).osc (1, "Transpose", 12.0f).osc (1, "Volume", 0.5f)
        .filter (FilterType::lp12, 2500.0f, 0.3f, 0.25f).filterEnvelope (5.0f, 600.0f, 0.4f, 200.0f)
        .set (ParamIDs::fltKey, 0.5f)
        .ampEnvelope (3.0f, 300.0f, 0.9f, 180.0f).mono (90.0f);
    add (lead);

    Builder pad ("Warm Pad", 5, { "Pad" },
                 "Three saws spread left, centre and right and detuned a little, slow in and slow out, notes alternating across the stereo field.");
    for (int i = 0; i < 3; ++i)
        pad.osc (i, "On", 1.0f).osc (i, "Wave", saw).osc (i, "Fine", (float) (i - 1) * 9.0f).osc (i, "Pan", (float) (i - 1) * 0.7f)
           .osc (i, "Volume", 0.6f);
    pad.filter (FilterType::lp24, 1400.0f, 0.15f, 0.15f).filterEnvelope (900.0f, 2000.0f, 0.6f, 1500.0f)
       .set (ParamIDs::fltKey, 0.4f)
       .ampEnvelope (700.0f, 1000.0f, 0.85f, 1800.0f).set (ParamIDs::ampReleaseCurve, -0.4f)
       .set (ParamIDs::ampSpread, 0.4f).set (ParamIDs::ampVel, 0.4f);
    add (pad);

    Builder sub ("Sub Bass", 6, { "Bass", "Mono" },
                 "A sine with a sine sub: deep and clean, for under a busy mix. Mono, gliding between overlapping notes.");
    sub.osc (0, "Wave", sine).osc (0, "PhaseRand", 0.0f).osc (0, "Sub", 0.6f).osc (0, "SubOct", 1.0f)
       .ampEnvelope (2.0f, 300.0f, 1.0f, 80.0f).mono (30.0f);
    add (sub);

    Builder sweep ("Noise Sweep", 7, { "FX" },
                   "Pink noise through a resonant band-pass that rises slowly as the note is held: a riser for builds.");
    sweep.osc (0, "Gain", 0.0f).osc (0, "Noise", 1.0f).osc (0, "NoiseColour", 0.5f)
         .filter (FilterType::bp12, 250.0f, 0.55f, 0.8f).filterEnvelope (4000.0f, 1000.0f, 1.0f, 800.0f)
         .ampEnvelope (300.0f, 500.0f, 1.0f, 1200.0f);
    add (sweep);

    Builder brass ("Saw Brass", 8, { "Brass", "Keys" },
                   "Two saws an octave apart with a filter that opens as each note starts, as a brass section's tone swells.");
    brass.osc (1, "On", 1.0f).osc (1, "Wave", saw).osc (1, "Transpose", -12.0f).osc (1, "Volume", 0.6f)
         .filter (FilterType::lp24, 600.0f, 0.1f, 0.35f).filterEnvelope (90.0f, 700.0f, 0.5f, 250.0f)
         .set (ParamIDs::fltKey, 0.5f).set (ParamIDs::fltEnvVel, 0.6f)
         .ampEnvelope (40.0f, 500.0f, 0.85f, 250.0f);
    add (brass);

    return rigs;
}
