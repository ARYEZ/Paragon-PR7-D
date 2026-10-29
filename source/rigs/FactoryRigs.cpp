#include "FactoryRigs.h"

#include "dsp/effects/Drive.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Reverb.h"

#include "Parameters.h"
#include "dsp/synth/Drums.h"

// PR7-D's factory presets: our own kits and beats, grown from PR7-K's drum presets (A40 Kit And Bass, A41 Beat And
// Bass, A42 Tight Kit) with the bass taken out. Our own sounds and patterns, not copies of anyone's, and not yet tuned
// by ear.
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
        Builder& drum (int slot, const char* what, float value) { return set (ParamIDs::drum (slot, what), value); }
    };
}

std::vector<Rig> makeFactoryRigs()
{
    std::vector<Rig> rigs;
    const auto add = [&rigs] (Builder& builder) { rigs.push_back (builder.rig); };

    // A01: what a new sound starts as (the parameters' defaults).
    Builder init ("Init", 1, { "Init" }, "The kit as it starts: our 12 drum synths on C to B from ROOT, dry, no beat. A blank page.");
    add (init);

    // A02: PR7-K's A40 without its bass.
    Builder kit ("Room Kit", 2, { "Kit", "Room" },
                 "The drum synths in a small room: the snare, clap, toms, hats and cymbal sent to a ROOM reverb, the kick dry. "
                 "Toms and hats spread a little left and right. Our own settings, not tuned by ear.");
    kit.drum (0, "Decay", 0.4f).drum (0, "Tone", 0.6f).drum (0, "SendReverb", 0.0f)
        .drum (2, "Tone", 0.6f).drum (2, "SendReverb", 0.6f)
        .drum (3, "SendReverb", 0.6f).drum (4, "SendReverb", 0.5f)
        .drum (1, "SendReverb", 0.3f)
        .drum (6, "SendReverb", 0.2f).drum (8, "SendReverb", 0.2f).drum (10, "SendReverb", 0.25f)
        .drum (11, "SendReverb", 0.4f).drum (6, "Pan", 0.25f).drum (10, "Pan", 0.25f)
        .drum (5, "SendReverb", 0.3f).drum (5, "Pan", -0.3f)
        .drum (7, "SendReverb", 0.3f)
        .drum (9, "SendReverb", 0.3f).drum (9, "Pan", 0.3f)
        .set (ParamIDs::reverbOn, 1.0f).set (ParamIDs::reverbType, (float) (int) effects::Reverb::Type::room)
        .set (ParamIDs::reverbSize, 0.35f).set (ParamIDs::reverbMix, 0.25f);
    add (kit);

    // A03: PR7-K's A41 without its bass: the room kit with a beat in each pattern, played with the song.
    Builder beat ("Room Beat", 3, { "Kit", "Room", "Sequencer" },
                  "The room kit with the drum sequencer on: A a straight beat, B busier (ghost snares, sixteenth hats), "
                  "C half time, D a fill (a tom roll, a reversed cymbal swelling into the next bar). It plays while your song does; "
                  "choose the pattern with PATTERN on the DRUMS page's SEQUENCER. Our own beats, not tuned by ear.");
    beat.rig.parameters = kit.rig.parameters;
    beat.set (ParamIDs::dseqOn, 1.0f).set (ParamIDs::shuffle, 0.1f);
    {
        using synth::drumseq::Note;
        auto& patterns = beat.rig.modulation.drumPatterns;
        const auto note = [] (synth::drumseq::Pattern& p, int row, double start, float velocity = 1.0f) -> Note&
        {
            Note n;
            n.row = row;
            n.start = start;
            n.velocity = velocity;
            p.notes.push_back (n);
            return p.notes.back();
        };
        constexpr int kick = 0, rim = 1, snare = 2, lowTom = 5, closedHat = 6, midTom = 7, highTom = 9, openHat = 10, cymbal = 11;
        auto& a = patterns[0];
        for (const auto k : { 0.0, 8.0, 10.0 })
            note (a, kick, k);
        note (a, snare, 4.0);
        note (a, snare, 12.0);
        for (int k = 0; k < 14; k += 2)
            note (a, closedHat, k, k % 4 == 0 ? 0.9f : 0.6f);
        note (a, openHat, 14.0, 0.7f).gate = 0.5f;

        auto& b = patterns[1];
        for (const auto k : { 0.0, 3.0, 8.0, 10.0, 11.0 })
            note (b, kick, k, k == 3.0 || k == 11.0 ? 0.7f : 1.0f);
        note (b, snare, 4.0);
        note (b, snare, 12.0);
        note (b, snare, 7.0, 0.35f);
        note (b, snare, 15.0, 0.4f);
        for (int k = 0; k < 16; ++k)
            note (b, closedHat, k, k % 4 == 0 ? 0.9f : k % 2 == 0 ? 0.6f : 0.4f);

        auto& c = patterns[2];
        note (c, kick, 0.0);
        note (c, kick, 3.0, 0.6f);
        note (c, kick, 10.0);
        note (c, snare, 8.0);
        note (c, rim, 6.0, 0.6f).pan = -0.4f;
        for (int k = 0; k < 16; k += 2)
            note (c, closedHat, k, k % 8 == 0 ? 0.9f : 0.55f);

        auto& d = patterns[3];
        note (d, kick, 0.0);
        note (d, kick, 8.0);
        note (d, snare, 4.0);
        note (d, snare, 10.0, 0.6f);
        for (int k = 0; k < 10; k += 2)
            note (d, closedHat, k, 0.6f);
        note (d, highTom, 12.0).roll = 1;   // 2x
        note (d, midTom, 13.0);
        note (d, lowTom, 14.0);
        note (d, lowTom, 15.0).pitch = -2.0f;
        note (d, cymbal, 15.0, 0.8f).reverse = synth::drumseq::Reverse::endSync;
    }
    add (beat);

    // A04: PR7-K's A42 without its bass: the beat, tightened with the sample editor.
    Builder tight ("Tight Beat", 4, { "Kit", "Sequencer", "Editor" },
                   "The room beat tightened with the sample editor: the clap's main slap on the beat (its flams start "
                   "early: DELAY's anchor), the open hat cut short by its drawn envelope, the kick MONO and a little darker, the "
                   "hats thinned out of the kick's way (HP). Our own settings, not tuned by ear.");
    tight.rig.parameters = beat.rig.parameters;
    tight.rig.modulation = beat.rig.modulation;
    tight.drum (3, "Delay", -18.0f)
        .drum (0, "Mono", 1.0f).drum (0, "LowPass", 5000.0f)
        .drum (6, "HighPass", 400.0f).drum (8, "HighPass", 400.0f).drum (10, "HighPass", 300.0f);
    tight.rig.modulation.drumEnvelopes[10] = { { { 0.0f, 1.0f, 0.0f }, { 0.3f, 0.8f, -0.4f }, { 0.45f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
    {
        auto& c = tight.rig.modulation.drumPatterns[0];   // the clap with the snare on beat 2 and 4
        for (const auto step : { 4.0, 12.0 })
        {
            synth::drumseq::Note n;
            n.row = 3;
            n.start = step;
            n.velocity = 0.7f;
            c.notes.push_back (n);
        }
    }
    add (tight);

    // A05: the tight beat with its kick, snare and clap through insert bus A: RABID's drive, then LO-FI's cassette
    // (no hiss: the presets must go silent), the hats and the rest left clean.
    Builder dirty ("Dirty Beat", 5, { "Kit", "Sequencer", "Inserts" },
                   "The tight beat with the kick, snare and clap on insert bus A: driven (RABID) and worn (LO-FI), its own small "
                   "reverb send. The hats, toms and cymbal stay clean in the room. Our own settings, not tuned by ear.");
    dirty.rig.parameters = tight.rig.parameters;
    dirty.rig.modulation = tight.rig.modulation;
    for (const auto slot : { 0, 2, 3 })
        dirty.drum (slot, "Insert", 1.0f);
    dirty.set (ParamIDs::ins (0, "DriveOn"), 1.0f).set (ParamIDs::ins (0, "DriveModel"), (float) (int) effects::Drive::Model::rabid)
        .set (ParamIDs::ins (0, "DriveGain"), 5.0f).set (ParamIDs::ins (0, "DriveTone"), 5.5f).set (ParamIDs::ins (0, "DriveLevel"), -6.0f)
        .set (ParamIDs::ins (0, "LofiOn"), 1.0f).set (ParamIDs::ins (0, "LofiType"), (float) (int) effects::LoFi::Type::cassette)
        .set (ParamIDs::ins (0, "LofiAmount"), 4.0f).set (ParamIDs::ins (0, "LofiNoise"), 0.0f)
        .set (ParamIDs::ins (0, "SendDelay"), 0.0f).set (ParamIDs::ins (0, "SendReverb"), 0.25f);
    add (dirty);

    return rigs;
}
