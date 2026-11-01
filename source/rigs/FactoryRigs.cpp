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

    // A06 (0.24.0): a djent kit built from scratch with the acoustic models, nobody's samples. The kick tuned up a
    // little, muffled (short DECAY) and struck by a hard beater (TONE) with lots of click (SNAP) near the centre; the
    // snare tuned high, tight, its wires crisp; the toms deep and open; the rimshot (SD2) the same snare struck at
    // the edge; the cymbals modelled too since 0.25.0. Our own settings: tune them by ear.
    Builder djent ("Djent Kit", 6, { "Kit", "Sequencer", "Acoustic" },
                   "Modelled drums, built from the physics with no samples: a tight, clicky kick (hard beater, heavily muffled), "
                   "a high, cracking snare, three deep toms, and modelled cymbals (0.25.0): hats at five openings and a pedal, two crashes, a "
                   "ride (bow, bell, edge), a trashy china, and a stack in the splash's slot (SPL). The master's compressor (FET) and "
                   "LIMITER hold it together. The sequencer has four grooves: A syncopated half time, B double-kick sixteenths, "
                   "C a ghost-note groove, D a tom fill. Our own settings, not tuned by ear yet.");
    {
        using synth::drums::Model;
        constexpr int bd = 0, sd = 2, sd2 = 4, lt = 5, ch = 6, mt = 7, ht = 9, oh = 10, cy = 11, rd = 12, chn = 15;
        djent.drum (bd, "Model", (float) (int) Model::acousticKick).drum (bd, "Pitch", 2.0f).drum (bd, "Decay", 0.28f)
             .drum (bd, "Tone", 0.92f).drum (bd, "Snap", 0.85f).drum (bd, "Position", 0.12f).drum (bd, "Shell", 0.2f)
             .drum (bd, "Variation", 0.15f).drum (bd, "Mono", 1.0f);
        djent.drum (sd, "Model", (float) (int) Model::acousticSnare).drum (sd, "Pitch", 5.0f).drum (sd, "Decay", 0.35f)
             .drum (sd, "Tone", 0.8f).drum (sd, "Snap", 0.55f).drum (sd, "Position", 0.25f).drum (sd, "Shell", 0.45f)
             .drum (sd, "Variation", 0.2f);
        djent.drum (sd2, "Model", (float) (int) Model::acousticSnare).drum (sd2, "Pitch", 5.0f).drum (sd2, "Decay", 0.4f)
             .drum (sd2, "Tone", 0.95f).drum (sd2, "Snap", 0.5f).drum (sd2, "Position", 0.85f).drum (sd2, "Shell", 0.7f)
             .drum (sd2, "Variation", 0.2f);
        for (const auto& [slot, pitch, pan] : { std::tuple { ht, 4.0f, 0.25f }, std::tuple { mt, 0.0f, 0.0f }, std::tuple { lt, -5.0f, -0.3f } })
            djent.drum (slot, "Model", (float) (int) Model::acousticTom).drum (slot, "Pitch", pitch).drum (slot, "Decay", 0.5f)
                 .drum (slot, "Tone", 0.7f).drum (slot, "Snap", 0.45f).drum (slot, "Position", 0.3f).drum (slot, "Shell", 0.35f)
                 .drum (slot, "Variation", 0.2f).drum (slot, "Pan", pan);
        // 0.25.0: the modelled cymbals. The hats each at their opening (the pedal's steps play them in turn), the
        // crashes either side, the ride on the right (its bow, bell and edge struck where they are), a trashy china, and
        // a stack in the splash's slot (djent's favourite).
        constexpr int ph = 8, cr2 = 14, spl = 16, hh = 17, rb = 13, re = 20, ho1 = 21, ho3 = 22;
        for (const auto& [slot, open] : { std::pair { ch, 0.0f }, std::pair { ho1, 0.25f }, std::pair { hh, 0.45f },
                                          std::pair { ho3, 0.7f }, std::pair { oh, 0.95f } })
            djent.drum (slot, "Model", (float) (int) Model::acousticHat).drum (slot, "Shell", open).drum (slot, "Pan", 0.3f)
                 .drum (slot, "Tone", 0.75f).drum (slot, "Variation", 0.25f);
        djent.drum (ph, "Model", (float) (int) Model::acousticHatPedal).drum (ph, "Pan", 0.3f);
        djent.drum (cy, "Model", (float) (int) Model::acousticCrash).drum (cy, "Pan", -0.35f).drum (cy, "Position", 0.85f)
             .drum (cy, "Tone", 0.7f).drum (cy, "Shell", 0.55f);
        djent.drum (cr2, "Model", (float) (int) Model::acousticCrash).drum (cr2, "Pan", 0.5f).drum (cr2, "Pitch", 2.0f)
             .drum (cr2, "Position", 0.85f).drum (cr2, "Decay", 0.4f);
        for (const auto& [slot, where] : { std::pair { rd, 0.5f }, std::pair { rb, 0.0f }, std::pair { re, 0.95f } })
            djent.drum (slot, "Model", (float) (int) Model::acousticRide).drum (slot, "Position", where).drum (slot, "Pan", -0.4f)
                 .drum (slot, "Tone", 0.8f).drum (slot, "Snap", 0.6f);
        djent.drum (chn, "Model", (float) (int) Model::acousticChina).drum (chn, "Pan", 0.45f).drum (chn, "Level", -3.0f)
             .drum (chn, "Snap", 0.6f).drum (chn, "Position", 0.9f);
        djent.drum (spl, "Model", (float) (int) Model::acousticStack).drum (spl, "Pan", -0.25f).drum (spl, "Level", -2.0f);
        djent.set (ParamIDs::compOn, 1.0f).set (ParamIDs::compThreshold, -16.0f).set (ParamIDs::compRatio, 4.0f)
             .set (ParamIDs::compAttack, 8.0f).set (ParamIDs::compRelease, 120.0f).set (ParamIDs::compMakeUp, 3.0f)
             .set (ParamIDs::compCharacter, 2.0f)
             .set (ParamIDs::limitOn, 1.0f).set (ParamIDs::dseqOn, 1.0f);

        using synth::drumseq::Note;
        auto& patterns = djent.rig.modulation.drumPatterns;
        const auto note = [] (synth::drumseq::Pattern& p, int row, double start, float velocity = 1.0f) -> Note&
        {
            Note n;
            n.row = row;
            n.start = start;
            n.velocity = velocity;
            p.notes.push_back (n);
            return p.notes.back();
        };
        // A: the kick following a syncopated (palm-muted guitar) rhythm, the snare on 3 (half time), the china on
        // the quarters.
        auto& a = patterns[0];
        for (const auto k : { 0.0, 1.0, 3.0, 6.0, 7.0, 10.0, 11.0, 13.0, 14.0 })
            note (a, bd, k, k == 1.0 || k == 7.0 || k == 11.0 ? 0.85f : 1.0f);
        note (a, sd, 8.0);
        for (int k = 0; k < 16; k += 4)
            note (a, chn, k, k == 0 ? 0.9f : 0.7f);
        // B: double-kick sixteenths under a backbeat, the ride on the eighths.
        auto& b = patterns[1];
        for (int k = 0; k < 16; ++k)
            note (b, bd, k, k % 2 == 0 ? 1.0f : 0.85f);
        note (b, sd, 4.0);
        note (b, sd, 12.0);
        for (int k = 0; k < 16; k += 2)
            note (b, rd, k, k % 4 == 0 ? 0.85f : 0.6f);
        // C: a groove with ghost notes, the hats on the eighths, a rimshot on the last backbeat.
        auto& c = patterns[2];
        for (const auto k : { 0.0, 3.0, 6.0, 10.0 })
            note (c, bd, k);
        note (c, sd, 4.0);
        note (c, sd2, 12.0);
        for (const auto k : { 7.0, 9.0, 14.0 })
            note (c, sd, k, 0.25f);
        for (int k = 0; k < 16; k += 2)
            note (c, ch, k, k % 4 == 0 ? 0.85f : 0.55f);
        // D: a tom fill down the kit, the china on the next downbeat's place.
        auto& d = patterns[3];
        note (d, bd, 0.0);
        note (d, sd, 4.0);
        for (const auto& [k, tom] : { std::pair { 8.0, ht }, std::pair { 9.0, ht }, std::pair { 10.0, mt }, std::pair { 11.0, mt },
                                     std::pair { 12.0, lt }, std::pair { 13.0, lt }, std::pair { 14.0, lt } })
            note (d, tom, k, k == 8.0 || k == 10.0 || k == 12.0 ? 1.0f : 0.8f);
        note (d, sd, 15.0);
        note (d, bd, 15.0);
        note (d, cy, 15.0, 0.6f);
    }
    add (djent);

    return rigs;
}
