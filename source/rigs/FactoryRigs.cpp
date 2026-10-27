#include "FactoryRigs.h"

#include "Parameters.h"
#include "dsp/synth/SynthSettings.h"
#include "dsp/synth/Wavetable.h"

// PR7-K's first factory presets: a few starting points that show what each
// stage can do (0.1.0: oscillators, sub, noise, the filter and its envelope,
// mono and glide; 0.2.0: unison, vibrato and the chorder; 0.3.0: wavetables;
// 0.4.0: SHAPE; 0.5.0: SYNC, FM and AM; 0.6.0: the FFT editor). Our own sounds, set up from common synth
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

        Builder& shaper (int k, synth::ShaperModel model, float drive, bool afterFilter = false)
        {
            set (ParamIDs::shp (k, "On"), 1.0f);
            set (ParamIDs::shp (k, "Model"), (float) model);
            set (ParamIDs::shp (k, "Drive"), drive);
            return set (ParamIDs::shp (k, "Place"), afterFilter ? 1.0f : 0.0f);
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

    // Stage 2 (0.2.0): unison, vibrato and the chorder.
    Builder supersaw ("Supersaw Lead", 9, { "Lead", "Unison" },
                      "Seven detuned saws spread wide (the classic trance and EDM lead), a touch of vibrato that grows in, and a filter that opens a little with each note.");
    supersaw.osc (0, "UniVoices", 7.0f).osc (0, "UniMix", 0.8f).osc (0, "UniDetune", 0.45f).osc (0, "UniSpread", 0.8f)
            .osc (0, "VibAmount", 8.0f).osc (0, "VibRate", 5.5f).osc (0, "VibFade", 400.0f).osc (0, "Volume", 0.6f)
            .filter (FilterType::lp24, 3500.0f, 0.15f, 0.2f).filterEnvelope (5.0f, 500.0f, 0.5f, 300.0f)
            .set (ParamIDs::fltKey, 0.5f)
            .ampEnvelope (5.0f, 300.0f, 0.9f, 350.0f).set (ParamIDs::ampReleaseCurve, -0.4f);
    add (supersaw);

    Builder strings ("Unison Strings", 10, { "Pad", "Unison" },
                     "Five saws, each copy with its own slow vibrato (independent), so the section never sits still: a soft, wide string pad.");
    strings.osc (0, "UniVoices", 5.0f).osc (0, "UniMix", 1.0f).osc (0, "UniDetune", 0.3f).osc (0, "UniSpread", 1.0f)
           .osc (0, "VibMode", (float) synth::VibratoMode::independent).osc (0, "VibAmount", 12.0f).osc (0, "VibRate", 4.5f)
           .osc (0, "VibFade", 800.0f).osc (0, "Volume", 0.5f)
           .filter (FilterType::lp12, 2200.0f, 0.1f, 0.1f).filterEnvelope (600.0f, 1500.0f, 0.7f, 1200.0f)
           .set (ParamIDs::fltKey, 0.4f)
           .ampEnvelope (450.0f, 800.0f, 0.85f, 1400.0f).set (ParamIDs::ampReleaseCurve, -0.5f).set (ParamIDs::ampVel, 0.5f);
    add (strings);

    Builder stab ("Fifth Stab", 11, { "Stab", "Chord" },
                  "Each key plays itself, a fifth above and an octave above (the chorder), three saw copies each: a big, short chord stab for one finger.");
    stab.osc (0, "Ch1On", 1.0f).osc (0, "Ch1Coarse", 7.0f).osc (0, "Ch1Vol", 0.8f)
        .osc (0, "Ch2On", 1.0f).osc (0, "Ch2Coarse", 12.0f).osc (0, "Ch2Vol", 0.7f)
        .osc (0, "UniVoices", 3.0f).osc (0, "UniMix", 0.7f).osc (0, "UniDetune", 0.35f).osc (0, "UniSpread", 0.7f)
        .osc (0, "Volume", 0.45f)
        .filter (FilterType::lp24, 600.0f, 0.2f, 0.5f).filterEnvelope (0.0f, 280.0f, 0.1f, 200.0f)
        .set (ParamIDs::fltKey, 0.5f).set (ParamIDs::fltEnvVel, 0.4f)
        .ampEnvelope (1.0f, 400.0f, 0.0f, 250.0f).set (ParamIDs::ampDecayCurve, -0.4f);
    add (stab);

    // 0.3.0: wavetables. (No modulation yet to move INDEX: that's stage 4,
    // so these set it where it sounds good and leave the knob to play with.)
    const auto table = [] (const char* name) { return (float) synth::factoryTables::names.indexOf (name); };
    const auto wavetable = (float) synth::OscWave::wavetable;

    Builder vowels ("Vowel Pad", 12, { "Pad", "Wavetable" },
                    "A choir-like pad on the Vowels table, between \"ah\" and \"eh\", five copies each slowly wobbling on their own. Turn INDEX to change the vowel.");
    vowels.osc (0, "Wave", wavetable).osc (0, "Table", table ("Vowels")).osc (0, "WtIndex", 0.15f)
          .osc (0, "UniVoices", 5.0f).osc (0, "UniMix", 0.9f).osc (0, "UniDetune", 0.25f).osc (0, "UniSpread", 0.9f)
          .osc (0, "VibMode", (float) synth::VibratoMode::independent).osc (0, "VibAmount", 10.0f).osc (0, "VibRate", 4.0f)
          .osc (0, "VibFade", 900.0f).osc (0, "Volume", 0.5f)
          .ampEnvelope (600.0f, 1000.0f, 0.85f, 1600.0f).set (ParamIDs::ampReleaseCurve, -0.5f).set (ParamIDs::ampVel, 0.4f);
    add (vowels);

    Builder sync ("Sync Lead", 13, { "Lead", "Wavetable" },
                  "The Hard Sync table halfway in (the tearing sound of a synced oscillator) with a saw an octave down underneath, mono with a quick glide.");
    sync.osc (0, "Wave", wavetable).osc (0, "Table", table ("Hard Sync")).osc (0, "WtIndex", 0.5f).osc (0, "Volume", 0.6f)
        .osc (1, "On", 1.0f).osc (1, "Transpose", -12.0f).osc (1, "Volume", 0.35f).osc (1, "PhaseRand", 0.0f)
        .filter (FilterType::lp24, 5000.0f, 0.1f, 0.0f).set (ParamIDs::fltKey, 0.6f)
        .ampEnvelope (2.0f, 300.0f, 0.85f, 200.0f)
        .mono (60.0f);
    add (sync);

    Builder bits ("Bit Bass", 14, { "Bass", "Wavetable" },
                  "A lo-fi bass from the Bit Steps table (a sine in a handful of steps), clean underneath with a sine sub, and a filter that snaps shut.");
    bits.osc (0, "Wave", wavetable).osc (0, "Table", table ("Bit Steps")).osc (0, "WtIndex", 0.7f).osc (0, "WtMode", 1.0f)
        .osc (0, "Sub", 0.7f).osc (0, "SubOct", 1.0f).osc (0, "Volume", 0.6f)
        .filter (FilterType::lp24, 400.0f, 0.25f, 0.55f).filterEnvelope (0.0f, 220.0f, 0.15f, 150.0f)
        .set (ParamIDs::fltKey, 0.3f)
        .ampEnvelope (1.0f, 500.0f, 0.7f, 120.0f)
        .mono (0.0f);
    add (bits);

    // 0.4.0: SHAPE.
    Builder vox ("Formant Vox", 15, { "Lead", "Shape" },
                 "A saw played faster inside each cycle (FORMANT, X-FADE 2), which gives it a fixed, vocal resonance as you play up and down; three copies and a slow vibrato.");
    vox.osc (0, "Formant", -0.35f).osc (0, "FormantMode", (float) synth::FormantMode::crossfade2)
       .osc (0, "UniVoices", 3.0f).osc (0, "UniMix", 0.6f).osc (0, "UniDetune", 0.25f).osc (0, "UniSpread", 0.6f)
       .osc (0, "VibAmount", 12.0f).osc (0, "VibRate", 5.0f).osc (0, "VibFade", 500.0f).osc (0, "Volume", 0.6f)
       .filter (FilterType::lp12, 6000.0f, 0.1f, 0.0f)
       .ampEnvelope (40.0f, 400.0f, 0.85f, 400.0f)
       .mono (80.0f);
    add (vox);

    Builder skewed ("Phase Bass", 16, { "Bass", "Shape" },
                    "A sine bent hard to one side (SKEW) into a rounded, buzzy bass, the way phase-distortion synths make theirs, and a filter that closes after the attack.");
    skewed.osc (0, "Wave", (float) synth::OscWave::sine).osc (0, "Skew", 0.75f).osc (0, "Pinch", -0.3f).osc (0, "Volume", 0.7f)
          .osc (0, "Sub", 0.5f).osc (0, "SubOct", 1.0f)
          .filter (FilterType::lp24, 900.0f, 0.15f, 0.45f).filterEnvelope (0.0f, 350.0f, 0.2f, 150.0f)
          .set (ParamIDs::fltKey, 0.4f)
          .ampEnvelope (1.0f, 600.0f, 0.75f, 120.0f)
          .mono (0.0f);
    add (skewed);

    Builder crushed ("Crushed Keys", 17, { "Keys", "Shape" },
                     "A triangle through CRUSH (BOTH: steps and fewer levels), the gritty sound of an old sampler, with a soft attack and a plucky decay.");
    crushed.osc (0, "Wave", (float) synth::OscWave::triangle).osc (0, "Crush", 0.55f).osc (0, "CrushMode", (float) synth::CrushMode::both)
           .osc (0, "Volume", 0.6f)
           .filter (FilterType::lp12, 5000.0f, 0.0f, 0.0f)
           .ampEnvelope (3.0f, 900.0f, 0.3f, 400.0f).set (ParamIDs::ampVel, 0.6f);
    add (crushed);

    // 0.5.0: SYNC, FM and AM.
    Builder syncSweep ("Sync Sweep", 18, { "Lead", "Sync" },
                   "A saw synced an octave and a half up, its own envelope sweeping it down from three octaves higher at each note: the classic tearing sync lead.");
    syncSweep.osc (0, "SyncAmount", 0.3f).osc (0, "SyncEnv", 0.45f).osc (0, "SyncEnvTime", 450.0f).osc (0, "SyncEnvCurve", -0.4f)
         .osc (0, "UniVoices", 3.0f).osc (0, "UniMix", 0.5f).osc (0, "UniDetune", 0.2f).osc (0, "UniSpread", 0.6f).osc (0, "Volume", 0.55f)
         .filter (FilterType::lp24, 7000.0f, 0.1f, 0.0f)
         .ampEnvelope (2.0f, 300.0f, 0.85f, 250.0f)
         .mono (40.0f);
    add (syncSweep);

    Builder bell ("FM Bell", 19, { "Keys", "FM" },
                  "A sine frequency-modulated by a sine six times as fast (oscillator 2, two octaves and a fifth up): a bright, glassy bell that rings away.");
    bell.osc (0, "Wave", (float) synth::OscWave::sine).osc (0, "FmAmount", 0.45f).osc (0, "FmSource", (float) (synth::fmFirstOscillator + 1))
        .osc (0, "Volume", 0.7f)
        .osc (1, "On", 1.0f).osc (1, "Wave", (float) synth::OscWave::sine).osc (1, "Transpose", 31.0f).osc (1, "PhaseRand", 0.0f)
        .ampEnvelope (1.0f, 2500.0f, 0.0f, 1500.0f).set (ParamIDs::ampDecayCurve, -0.6f).set (ParamIDs::ampVel, 0.7f);
    add (bell);

    Builder ring ("Ring Mod", 20, { "FX", "AM" },
                  "A triangle ring-modulated (AM at full) by a sine at 1.5 times the note: clangy, metallic, robot-like.");
    ring.osc (0, "Wave", (float) synth::OscWave::triangle).osc (0, "AmAmount", 1.0f).osc (0, "ModRate", 0.5f + 0.5f / 8.0f)
        .osc (0, "Volume", 0.8f)
        .ampEnvelope (5.0f, 800.0f, 0.6f, 500.0f);
    add (ring);

    // 0.6.0: the FFT editor (curves drawn here in code).
    Builder hollow ("Hollow Reed", 21, { "Lead", "FFT" },
                    "A saw with every even harmonic drawn out (the FFT editor), which leaves a hollow, clarinet-like reed; the 3rd and 5th lifted a little.");
    for (int band = 1; band < synth::SpectrumEdit::bands; band += 2)
        hollow.rig.spectra[0].gainsDb[(size_t) band] = synth::SpectrumEdit::minDb;   // harmonics 2, 4, 6...
    hollow.rig.spectra[0].gainsDb[2] = 3.0f;
    hollow.rig.spectra[0].gainsDb[4] = 2.0f;
    hollow.osc (0, "VibAmount", 10.0f).osc (0, "VibRate", 5.0f).osc (0, "VibFade", 600.0f).osc (0, "Volume", 0.7f)
          .osc (0, "FftLp", 40.0f)
          .ampEnvelope (30.0f, 300.0f, 0.9f, 250.0f)
          .mono (50.0f);
    add (hollow);

    Builder shimmer ("Spectral Shimmer", 22, { "Pad", "FFT" },
                     "Five saw copies whose harmonics pulse slowly against each other (VSPEED / VSTRENGTH), with a bump drawn round the 6th to 12th harmonics and HP thinning the bottom: a pad that never sits still.");
    for (int band = 5; band < 12; ++band)
        shimmer.rig.spectra[0].gainsDb[(size_t) band] = 6.0f;
    shimmer.osc (0, "FftVStrength", 0.7f).osc (0, "FftVSpeed", 0.4f).osc (0, "FftHp", 2.0f).osc (0, "FftLp", 60.0f).osc (0, "FftSlope", 0.6f)
           .osc (0, "UniVoices", 5.0f).osc (0, "UniMix", 0.8f).osc (0, "UniDetune", 0.3f).osc (0, "UniSpread", 1.0f).osc (0, "Volume", 0.45f)
           .ampEnvelope (900.0f, 1000.0f, 0.85f, 2000.0f).set (ParamIDs::ampReleaseCurve, -0.5f);
    add (shimmer);

    // FREEFORM (stage 3d, part 2): a drawn cycle.
    Builder drawn ("Drawn Growl", 23, { "Bass", "Freeform" },
                   "A hand-drawn cycle: a ramp, then a jagged staircase that SMOOTH B rounds off, its ends joined (EDGE). Gritty in the low mids, through a low-pass that opens on each note.");
    {
        synth::FreeformDrawing d;
        for (int i = 0; i < synth::FreeformDrawing::points; ++i)
        {
            const auto t = (float) i / (float) synth::FreeformDrawing::points;
            d.values[(size_t) i] = t < 0.5f ? -0.9f + 3.6f * t                                   // the ramp up
                                            : 0.6f - 0.3f * (float) (int) ((t - 0.5f) * 10.0f);   // five steps down
        }
        d.smoothA = 0.05f;
        d.smoothB = 0.2f;
        d.split = 0.5f;
        d.edge = 0.3f;
        drawn.rig.drawings[0] = d;
    }
    drawn.osc (0, "Wave", (float) synth::OscWave::freeform).osc (0, "PhaseRand", 0.0f).osc (0, "Sub", 0.4f).osc (0, "Volume", 0.8f)
         .filter (FilterType::lp24, 400.0f, 0.3f, 0.5f).filterEnvelope (0.0f, 350.0f, 0.2f, 150.0f)
         .ampEnvelope (1.0f, 300.0f, 0.9f, 120.0f)
         .mono (30.0f);
    add (drawn);

    // The SHAPER (stage 3e).
    Builder tube ("Tube Bass", 24, { "Bass", "Shaper" },
                  "A saw and its sub through TUBE (warm, with even harmonics) before a low-pass, with the low mids lifted going in so they "
                  "growl first, and DRIVE following velocity: play harder for more bite.");
    tube.osc (0, "Sub", 0.5f).osc (0, "PhaseRand", 0.0f).osc (0, "Volume", 0.8f)
        .shaper (0, synth::ShaperModel::tube, 0.55f)
        .set (ParamIDs::shp (0, "DriveVel"), 0.6f)
        .set (ParamIDs::shp (0, "Eq1Gain"), 6.0f).set (ParamIDs::shp (0, "Eq1Freq"), 250.0f).set (ParamIDs::shp (0, "Eq1Q"), 0.8f)
        .filter (FilterType::lp24, 700.0f, 0.2f, 0.4f).filterEnvelope (0.0f, 400.0f, 0.3f, 150.0f)
        .ampEnvelope (1.0f, 300.0f, 0.9f, 120.0f)
        .mono (25.0f);
    add (tube);

    Builder wide ("Wide Fold Lead", 25, { "Lead", "Shaper" },
                  "Two detuned saws folded (SINE FOLD) with EQ SPLIT, so the left and right channels fold differently and the lead spreads wide; "
                  "SOFT after the filter rounds it off.");
    wide.osc (0, "Fine", -6.0f).osc (0, "Volume", 0.6f)
        .osc (1, "On", 1.0f).osc (1, "Wave", saw).osc (1, "Fine", 6.0f).osc (1, "Volume", 0.6f)
        .shaper (0, synth::ShaperModel::sineFold, 0.35f)
        .set (ParamIDs::shp (0, "Eq1Gain"), 9.0f).set (ParamIDs::shp (0, "Eq1Freq"), 900.0f)
        .set (ParamIDs::shp (0, "Eq2Gain"), 9.0f).set (ParamIDs::shp (0, "Eq2Freq"), 2400.0f)
        .set (ParamIDs::shp (0, "EqSplit"), 1.0f).set (ParamIDs::shp (0, "Mix"), 0.7f)
        .shaper (1, synth::ShaperModel::soft, 0.2f, true)
        .filter (FilterType::lp12, 4500.0f, 0.15f, 0.2f).filterEnvelope (5.0f, 600.0f, 0.5f, 300.0f)
        .ampEnvelope (5.0f, 400.0f, 0.85f, 250.0f)
        .mono (60.0f);
    add (wide);

    // MODULATION (stage 4a).
    const auto route = [] (Builder& b, const char* source, const juce::String& destination, float amount, bool percent = false)
    {
        b.rig.modulation.routes.push_back ({ true, source, destination, amount, percent, 0.0f, 0.0f });
    };
    Builder wobble ("Wobble Bass", 26, { "Bass", "Modulation" },
                    "LFO 1, synced to eighth notes, sweeps the filter: the classic wobble, in time with your song. "
                    "Try LFO 1's LENGTH at 1/16 or 1/8 T.");
    wobble.osc (0, "Sub", 0.6f).osc (0, "PhaseRand", 0.0f).osc (0, "UniVoices", 3.0f).osc (0, "UniMix", 0.5f).osc (0, "UniDetune", 0.15f)
          .filter (FilterType::lp24, 200.0f, 0.45f, 0.0f).set (ParamIDs::fltDrive, 0.3f)
          .set (ParamIDs::lfo (0, "Wave"), (float) synth::mod::LfoWave::sine)
          .set (ParamIDs::lfo (0, "Sync"), (float) synth::mod::LfoSync::straight)
          .set (ParamIDs::lfo (0, "Division"), 6.0f)   // 1/8
          .set (ParamIDs::lfo (0, "Trigger"), (float) synth::mod::LfoTrigger::global)
          .set (ParamIDs::lfo (0, "Offset"), 1.0f)     // only ever opens the filter
          .ampEnvelope (1.0f, 300.0f, 1.0f, 80.0f)
          .mono (20.0f);
    route (wobble, "lfo1", ParamIDs::fltCutoff, 0.45f);
    add (wobble);

    Builder wheelPad ("Mod Wheel Pad", 27, { "Pad", "Modulation" },
                 "A wide supersaw pad. The MOD WHEEL opens the filter and brings in vibrato; a slow global LFO keeps the unison drifting.");
    wheelPad.osc (0, "UniVoices", 7.0f).osc (0, "UniMix", 0.8f).osc (0, "UniDetune", 0.25f).osc (0, "UniSpread", 1.0f).osc (0, "Volume", 0.5f)
       .osc (0, "VibRate", 5.0f)
       .filter (FilterType::lp12, 900.0f, 0.15f, 0.0f)
       .set (ParamIDs::lfo (1, "Rate"), 0.15f).set (ParamIDs::lfo (1, "Trigger"), (float) synth::mod::LfoTrigger::global)
       .ampEnvelope (600.0f, 1000.0f, 0.9f, 1500.0f);
    route (wheelPad, "modWheel", ParamIDs::fltCutoff, 0.35f);
    route (wheelPad, "modWheel", ParamIDs::osc (0, "VibAmount"), 0.25f);
    route (wheelPad, "lfo2", ParamIDs::osc (0, "UniDetune"), 0.15f);
    add (wheelPad);

    Builder keys ("Velocity Keys", 28, { "Keys", "Modulation" },
                  "Play harder for brighter and grittier: velocity opens the filter and drives SHAPER 1. MACRO 1 (TONE) darkens or brightens everything.");
    keys.osc (0, "Wave", (float) synth::OscWave::pulse).osc (0, "Width", 0.35f).osc (0, "Volume", 0.7f)
        .osc (1, "On", 1.0f).osc (1, "Wave", (float) synth::OscWave::triangle).osc (1, "Transpose", 12.0f).osc (1, "Volume", 0.4f)
        .shaper (0, synth::ShaperModel::tube, 0.1f)
        .filter (FilterType::lp24, 500.0f, 0.1f, 0.3f).filterEnvelope (0.0f, 600.0f, 0.2f, 300.0f)
        .set (ParamIDs::macros[0], 0.5f)
        .ampEnvelope (2.0f, 1200.0f, 0.3f, 400.0f);
    route (keys, "velocity", ParamIDs::fltCutoff, 0.35f);
    route (keys, "velocity", ParamIDs::shp (0, "Drive"), 0.5f);
    route (keys, "macro1", ParamIDs::fltCutoff, 0.3f);
    keys.rig.modulation.macroNames[0] = "TONE";
    add (keys);

    // MODULATION ENVELOPES and drawn LFO shapes (stage 4b).
    using SP = synth::mod::ShapePoint;
    Builder gated ("Gated Pad", 29, { "Pad", "Modulation" },
                   "A supersaw pad chopped in time with your song by ENV 1: a looping, synced envelope (eighth-note pulses) on the volume. "
                   "Try its points, or STEPS, for other rhythms.");
    gated.osc (0, "UniVoices", 7.0f).osc (0, "UniMix", 0.8f).osc (0, "UniDetune", 0.3f).osc (0, "UniSpread", 1.0f).osc (0, "Volume", 0.5f)
         .filter (FilterType::lp12, 2500.0f, 0.1f, 0.0f)
         .set (ParamIDs::env (0, "Mode"), (float) synth::mod::EnvelopeMode::loop)
         .set (ParamIDs::env (0, "Trigger"), (float) synth::mod::EnvelopeTrigger::firstNote)
         .set (ParamIDs::env (0, "Sync"), 1.0f)
         .ampEnvelope (20.0f, 500.0f, 1.0f, 400.0f);
    gated.rig.modulation.envelopes[0] = { { SP { 0.0f, 1.0f, 0.0f }, SP { 0.3f, 1.0f, -0.5f }, SP { 0.45f, 0.0f, 0.0f }, SP { 0.5f, 0.0f, 0.0f } }, -1, 0, 3 };
    route (gated, "env1", ParamIDs::ampVolume, 0.5f);   // VOLUME at 0, pushed up half its travel (to 1, full) at the envelope's top
    gated.set (ParamIDs::ampVolume, 0.0f);
    add (gated);

    Builder swell ("Swell Lead", 30, { "Lead", "Modulation" },
                   "ENV 1 slowly opens the filter (two rises, drawn) while a drawn LFO shape (CUSTOM 1, a bounce) wobbles the pitch a little.");
    swell.osc (0, "Volume", 0.7f).osc (1, "On", 1.0f).osc (1, "Wave", (float) synth::OscWave::pulse).osc (1, "Fine", 7.0f).osc (1, "Volume", 0.5f)
         .filter (FilterType::lp24, 300.0f, 0.3f, 0.0f)
         .set (ParamIDs::env (0, "Mode"), (float) synth::mod::EnvelopeMode::sustain)
         .set (ParamIDs::lfo (0, "Wave"), (float) synth::mod::LfoWave::custom1)
         .set (ParamIDs::lfo (0, "Rate"), 5.0f)
         .ampEnvelope (10.0f, 500.0f, 0.9f, 400.0f)
         .mono (60.0f);
    swell.rig.modulation.envelopes[0] = { { SP { 0.0f, 0.0f, 0.4f }, SP { 0.6f, 0.45f, 0.0f }, SP { 0.9f, 0.35f, 0.5f }, SP { 2.0f, 1.0f, 0.0f },
                                            SP { 3.0f, 0.2f, -0.4f } }, 3, -1, -1 };
    swell.rig.modulation.lfoShapes[0] = { { SP { 0.0f, 0.0f, -0.6f }, SP { 0.5f, 1.0f, 0.6f }, SP { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
    route (swell, "env1", ParamIDs::fltCutoff, 0.55f);
    route (swell, "lfo1", ParamIDs::osc (0, "Fine"), 0.06f);
    route (swell, "lfo1", ParamIDs::osc (1, "Fine"), 0.06f);
    add (swell);

    return rigs;
}
