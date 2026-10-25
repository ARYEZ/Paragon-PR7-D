#include "FactoryRigs.h"

#include "Parameters.h"
#include "dsp/eq/EqDesign.h"

// PR7-E's factory rigs (0.6.0): starting points for the EQ stomp, filed by
// what they're for. Bank A: vocals and voice; B: drums; C: bass, guitars
// and keys; D: the full mix; E: mastering.
//
// They're common mixing moves (cut the mud, add presence and air, tame
// sibilance and boom), not copies of anyone's presets, and every track is
// different: they're meant to be tweaked by ear. Problems that come and go
// (sibilance, boomy notes, harsh hits) use dynamic bands with AUTO, so they
// only act when the problem is there. Mastering rigs move a dB or two at
// most, and end with the LIMITER as a -1 dB safety ceiling. None of them
// touch PRESENCE or RIG LEVEL.
namespace
{
    using eq::Shape;
    using eq::Placement;

    // A band, as a rig sets it. Slope in dB/oct; a range makes it dynamic (AUTO threshold).
    struct BandSpec
    {
        Shape shape;
        double frequency, gainDb = 0.0, q = 1.0;
        int slopeDb = 12;
        Placement placement = Placement::stereo;
        double dynamicRangeDb = 0.0;
        double attack = 0.5, release = 0.5;
    };

    BandSpec cut (Shape shape, double hz, int slopeDb, Placement placement = Placement::stereo)
    {
        return { shape, hz, 0.0, 1.0, slopeDb, placement };
    }

    BandSpec bell (double hz, double gainDb, double q, Placement placement = Placement::stereo)
    {
        return { Shape::bell, hz, gainDb, q, 12, placement };
    }

    BandSpec shelf (Shape shape, double hz, double gainDb, double q = 0.7, Placement placement = Placement::stereo)
    {
        return { shape, hz, gainDb, q, 12, placement };
    }

    BandSpec tilt (double gainDb) { return { Shape::flatTilt, 1000.0, gainDb }; }   // gainDb / 10 dB per octave round 1 kHz

    // A band that moves by `rangeDb` when its part gets loud (and `gainDb` otherwise).
    BandSpec dynamic (BandSpec band, double rangeDb, double attack = 0.5, double release = 0.5)
    {
        band.dynamicRangeDb = rangeDb;
        band.attack = attack;
        band.release = release;
        return band;
    }

    int slopeIndex (int dbPerOctave)
    {
        for (int i = 0; i < eq::numSlopes; ++i)
            if (eq::slopes[i] == dbPerOctave)
                return i;
        jassertfalse;
        return eq::defaultSlope;
    }

    Rig makeRig (const juce::String& name, int bank, int number, const juce::StringArray& tags, const juce::String& notes,
                 const std::vector<BandSpec>& bands)
    {
        Rig rig;
        rig.name = name;
        rig.bank = bank;
        rig.number = number;
        rig.tags = tags;
        rig.notes = notes;
        rig.parameters[ParamIDs::eqOn] = 1.0f;
        for (int i = 0; i < (int) bands.size() && i < ParamIDs::eqNumBands; ++i)
        {
            const auto& b = bands[(size_t) i];
            const auto set = [&] (const char* what, double value) { rig.parameters[ParamIDs::eqBand (i, what)] = (float) value; };
            set ("Used", 1.0);
            set ("On", 1.0);
            set ("Shape", (double) b.shape);
            set ("Freq", b.frequency);
            set ("Gain", b.gainDb);
            set ("Q", b.q);
            set ("Slope", slopeIndex (b.slopeDb));
            set ("Place", (double) b.placement);
            if (std::abs (b.dynamicRangeDb) > 0.0)
            {
                set ("Dyn", b.dynamicRangeDb);
                set ("DynAuto", 1.0);
                set ("Attack", b.attack);
                set ("Release", b.release);
            }
        }
        return rig;
    }

    constexpr int vocals = 0, drums = 1, instruments = 2, mix = 3, mastering = 4;
}

std::vector<Rig> makeFactoryRigs()
{
    std::vector<Rig> rigs;

    Rig init;
    init.name = "Init";
    init.bank = vocals;   // A01
    init.number = 1;
    init.tags = { "Init" };
    init.notes = "Everything off: a starting point.";
    rigs.push_back (init);

    // A: vocals and voice
    rigs.push_back (makeRig ("Lead Vocal", vocals, 2, { "Vocal", "Lead" },
        "Clears rumble and boxiness, adds presence and air, and tames sibilance when it's there (a dynamic band at 6.5 kHz).",
        { cut (Shape::lowCut, 80.0, 18), bell (300.0, -2.0, 1.2), bell (3000.0, 2.0, 0.8), shelf (Shape::highShelf, 10000.0, 2.5),
          dynamic (bell (6500.0, 0.0, 3.0), -6.0) }));
    rigs.push_back (makeRig ("Vocal De-Esser", vocals, 3, { "Vocal", "De-ess", "Dynamic" },
        "Only the S and T sounds: a dynamic band at 6.5 kHz pulls down sibilance as it happens and leaves the rest alone. "
        "If the S sits higher or lower in your singer's voice, solo the band and move it there.",
        { dynamic (bell (6500.0, 0.0, 2.0), -8.0, 0.25, 0.4) }));
    rigs.push_back (makeRig ("Rap Vocal", vocals, 4, { "Vocal", "Rap", "Lead" },
        "Tight and forward: a steeper low cut, less low-mid weight, more bite at 4 kHz, sibilance kept in check.",
        { cut (Shape::lowCut, 100.0, 24), bell (220.0, -2.5, 1.0), bell (4000.0, 3.0, 1.0), shelf (Shape::highShelf, 12000.0, 2.0),
          dynamic (bell (6000.0, 0.0, 2.5), -6.0) }));
    rigs.push_back (makeRig ("Backing Vocals", vocals, 5, { "Vocal", "Backing" },
        "Thinner and a little further back than the lead, so they support it rather than crowd it, with an airy top.",
        { cut (Shape::lowCut, 150.0, 24), bell (400.0, -2.0, 0.8), bell (3000.0, -1.5, 1.0), shelf (Shape::highShelf, 10000.0, 2.0),
          dynamic (bell (6500.0, 0.0, 2.5), -6.0) }));
    rigs.push_back (makeRig ("Spoken Voice", vocals, 6, { "Voice", "Podcast", "Voice-over" },
        "For speech: clear, without boom or harsh S's. Dynamic bands catch popped P's and B's (150 Hz) and sibilance (6 kHz).",
        { cut (Shape::lowCut, 70.0, 24), dynamic (bell (150.0, 0.0, 1.0), -6.0), bell (300.0, -2.0, 1.0), bell (2500.0, 2.0, 1.0),
          dynamic (bell (6000.0, 0.0, 2.5), -6.0) }));
    rigs.push_back (makeRig ("Vocal Mud Control", vocals, 7, { "Vocal", "Dynamic", "Mud" },
        "Turns down the low mids (300 Hz) only on the notes where they build up, so the voice keeps its body elsewhere.",
        { cut (Shape::lowCut, 90.0, 18), dynamic (bell (300.0, 0.0, 1.2), -5.0) }));

    // B: drums
    rigs.push_back (makeRig ("Kick Punch", drums, 1, { "Drums", "Kick" },
        "Weight at 60 Hz, less cardboard box at 350 Hz, more beater click at 4 kHz; rumble below 30 Hz cut.",
        { cut (Shape::lowCut, 30.0, 24), bell (60.0, 3.0, 1.2), bell (350.0, -4.0, 1.5), bell (4000.0, 3.0, 1.5) }));
    rigs.push_back (makeRig ("Kick Boom Control", drums, 2, { "Drums", "Kick", "Dynamic" },
        "For a kick whose boom swamps the mix: the lows come down when they bloom, and a touch of click helps it cut through.",
        { cut (Shape::lowCut, 30.0, 24), dynamic (shelf (Shape::lowShelf, 80.0, 0.0), -5.0), bell (4000.0, 2.0, 1.2) }));
    rigs.push_back (makeRig ("Snare Crack", drums, 3, { "Drums", "Snare" },
        "Body at 200 Hz, less ring at 900 Hz, crack at 5 kHz and a little sizzle on top.",
        { cut (Shape::lowCut, 80.0, 18), bell (200.0, 2.0, 1.5), bell (900.0, -2.0, 2.0), bell (5000.0, 3.0, 1.0),
          shelf (Shape::highShelf, 10000.0, 1.5) }));
    rigs.push_back (makeRig ("Hi-Hat Tame", drums, 4, { "Drums", "Hi-hat", "Cymbals", "Dynamic" },
        "Hats without the spit: the lows (kick and snare bleed) cut, harsh hits around 8 kHz pulled down as they come.",
        { cut (Shape::lowCut, 250.0, 18), dynamic (bell (8000.0, 0.0, 1.5), -5.0), shelf (Shape::highShelf, 12000.0, 1.0) }));
    rigs.push_back (makeRig ("Overheads", drums, 5, { "Drums", "Overheads", "Cymbals" },
        "Cleaner cymbals and room: less low mid, harsh crashes tamed around 3.5 kHz, and air on top.",
        { cut (Shape::lowCut, 120.0, 18), bell (400.0, -2.0, 1.0), dynamic (bell (3500.0, 0.0, 1.5), -4.0),
          shelf (Shape::highShelf, 12000.0, 2.0) }));
    rigs.push_back (makeRig ("Drum Bus", drums, 6, { "Drums", "Bus" },
        "For the whole kit: a little weight and snap, less boxiness, and a clean bottom.",
        { cut (Shape::lowCut, 25.0, 24), bell (60.0, 1.5, 0.9), bell (400.0, -1.5, 1.0), bell (5000.0, 1.5, 0.8),
          shelf (Shape::highShelf, 12000.0, 1.5) }));

    // C: bass, guitars and keys
    rigs.push_back (makeRig ("Bass DI", instruments, 1, { "Bass" },
        "A direct bass: an even bottom (boomy notes come down, at 70 Hz), less mud at 250 Hz, a little growl at 800 Hz.",
        { cut (Shape::lowCut, 35.0, 24), dynamic (bell (70.0, 1.5, 1.4), -4.0), bell (250.0, -3.0, 1.2), bell (800.0, 1.5, 1.0) }));
    rigs.push_back (makeRig ("808 Control", instruments, 2, { "Bass", "808", "Dynamic" },
        "Evens out an 808 whose notes boom unevenly, and lifts its upper harmonics (1.2 kHz) so it's heard on small speakers.",
        { cut (Shape::lowCut, 25.0, 24), dynamic (bell (55.0, 2.0, 1.0), -5.0), bell (1200.0, 2.0, 1.0),
          cut (Shape::highCut, 8000.0, 12) }));
    rigs.push_back (makeRig ("Electric Guitar", instruments, 3, { "Guitar", "Electric" },
        "Makes room in a mix: no low end below 90 Hz, less mud, a little bite at 2.5 kHz, and the fizz above 11 kHz rolled off.",
        { cut (Shape::lowCut, 90.0, 18), bell (400.0, -2.0, 1.2), bell (2500.0, 1.5, 1.0), cut (Shape::highCut, 11000.0, 12) }));
    rigs.push_back (makeRig ("Acoustic Guitar", instruments, 4, { "Guitar", "Acoustic" },
        "Body boom (200 Hz) comes down when it builds up, a less honky middle, more sparkle.",
        { cut (Shape::lowCut, 80.0, 18), dynamic (bell (200.0, 0.0, 1.2), -5.0), bell (1000.0, -1.0, 1.0), bell (5000.0, 2.0, 0.9),
          shelf (Shape::highShelf, 11000.0, 2.0) }));
    rigs.push_back (makeRig ("Piano", instruments, 5, { "Keys", "Piano" },
        "Clearer in a mix: less low-mid muddiness, a little presence and air.",
        { cut (Shape::lowCut, 40.0, 18), bell (300.0, -2.0, 1.0), bell (3000.0, 1.5, 0.8), shelf (Shape::highShelf, 10000.0, 1.5) }));
    rigs.push_back (makeRig ("Pad, Make Room", instruments, 6, { "Keys", "Synth", "Pad" },
        "Pushes a pad or keys behind the song: the lows cut, and a dip around 2.5 kHz leaves room for a vocal.",
        { cut (Shape::lowCut, 150.0, 18), bell (2500.0, -2.5, 0.8), shelf (Shape::highShelf, 9000.0, -1.5) }));

    // D: the full mix
    rigs.push_back (makeRig ("Mix Bus Polish", mix, 1, { "Mix", "Bus" },
        "Gentle moves for a whole mix: a touch of weight, a little less low-mid, a little air, and nothing below 25 Hz.",
        { cut (Shape::lowCut, 25.0, 12), shelf (Shape::lowShelf, 80.0, 1.0), bell (300.0, -1.0, 0.8),
          shelf (Shape::highShelf, 10000.0, 1.5) }));
    rigs.push_back (makeRig ("Mix Mud Control", mix, 2, { "Mix", "Dynamic", "Mud" },
        "Turns down the low mids (280 Hz) only where they build up, so the mix stays full but clear.",
        { cut (Shape::lowCut, 25.0, 12), dynamic (bell (280.0, 0.0, 1.0), -4.0) }));
    rigs.push_back (makeRig ("Mix Harshness Control", mix, 3, { "Mix", "Dynamic", "Harsh" },
        "For a mix that gets harsh when it's busy: 3 kHz and 7 kHz come down a little only in those moments.",
        { dynamic (bell (3000.0, 0.0, 1.2), -3.0), dynamic (bell (7000.0, 0.0, 1.5), -3.0) }));
    rigs.push_back (makeRig ("Mix Wider Top", mix, 4, { "Mix", "Stereo", "Width" },
        "Wider without touching the middle: the sides' top end lifted, and their lows below 120 Hz cut so the bass stays centred.",
        { cut (Shape::lowCut, 120.0, 12, Placement::side), shelf (Shape::highShelf, 6000.0, 2.0, 0.7, Placement::side) }));
    rigs.push_back (makeRig ("Mix Warmer", mix, 5, { "Mix", "Tilt", "Warm" },
        "Tilts the whole mix darker: 0.6 dB an octave, the lows up and the highs down round 1 kHz.", { tilt (-6.0) }));
    rigs.push_back (makeRig ("Mix Brighter", mix, 6, { "Mix", "Tilt", "Bright" },
        "Tilts the whole mix brighter: 0.6 dB an octave, the highs up and the lows down round 1 kHz.", { tilt (6.0) }));

    // E: mastering (a dB or two at most), each finished by the LIMITER (below)
    rigs.push_back (makeRig ("Master Gentle Polish", mastering, 1, { "Mastering" },
        "Small, broad moves: a touch of weight and air, a little less low-mid, and sub-sonic rumble (below 18 Hz) cut.",
        { cut (Shape::lowCut, 18.0, 24), shelf (Shape::lowShelf, 60.0, 0.8), bell (250.0, -0.8, 0.7),
          shelf (Shape::highShelf, 12000.0, 1.0) }));
    rigs.push_back (makeRig ("Master Low-End Control", mastering, 2, { "Mastering", "Dynamic", "Low end" },
        "Keeps the bass solid: the sides' lows (below 150 Hz) cut so the bass is centred, and boomy notes below 100 Hz "
        "come down a little when they bloom.",
        { cut (Shape::lowCut, 20.0, 24), cut (Shape::lowCut, 150.0, 12, Placement::side),
          dynamic (shelf (Shape::lowShelf, 100.0, 0.0), -3.0) }));
    rigs.push_back (makeRig ("Master De-Harsh", mastering, 3, { "Mastering", "Dynamic", "Harsh" },
        "Softens harsh moments in a finished mix (around 3.2 and 6.5 kHz), by at most 2.5 dB, only when they happen.",
        { dynamic (bell (3200.0, 0.0, 1.0), -2.5), dynamic (bell (6500.0, 0.0, 1.5), -2.5) }));
    rigs.push_back (makeRig ("Master Width", mastering, 4, { "Mastering", "Stereo", "Width" },
        "A little wider at the top (the sides above 8 kHz up 1.5 dB) with the bass kept in the middle.",
        { cut (Shape::lowCut, 120.0, 12, Placement::side), shelf (Shape::highShelf, 8000.0, 1.5, 0.7, Placement::side) }));
    rigs.push_back (makeRig ("Master Warm Tilt", mastering, 5, { "Mastering", "Tilt", "Warm" },
        "A gentle darker tilt for a bright master: 0.3 dB an octave.", { tilt (-3.0) }));
    rigs.push_back (makeRig ("Master Bright Tilt", mastering, 6, { "Mastering", "Tilt", "Bright" },
        "A gentle brighter tilt for a dull master: 0.3 dB an octave.", { tilt (3.0) }));

    // COMP (0.9.0) on the rigs that would usually have one: gentle settings,
    // thresholds for recordings peaking around -10 dBFS (a guess: louder or
    // quieter tracks want the THRESHOLD moved).
    struct CompSpec { const char* rig; int character; float threshold, ratio, attack, release; bool autoRelease; const char* note; };
    const CompSpec comps[] = {
        { "Lead Vocal",     1, -20.0f, 3.0f, 10.0f, 300.0f, false, " COMP (OPTO) evens out the level smoothly." },
        { "Rap Vocal",      0, -18.0f, 4.0f,  5.0f, 150.0f, false, " COMP (VCA) keeps the delivery tight and upfront." },
        { "Drum Bus",       0, -20.0f, 4.0f, 30.0f, 300.0f, true,  " COMP (VCA, slow attack so hits keep their punch, AUTO release) glues the kit." },
        { "Mix Bus Polish", 0, -16.0f, 1.5f, 30.0f, 300.0f, true,  " COMP (VCA, 1.5:1) adds a light glue." },
    };
    for (const auto& c : comps)
        for (auto& rig : rigs)
            if (rig.name == c.rig)
            {
                rig.parameters[ParamIDs::compOn] = 1.0f;
                rig.parameters[ParamIDs::compCharacter] = (float) c.character;
                rig.parameters[ParamIDs::compThreshold] = c.threshold;
                rig.parameters[ParamIDs::compRatio] = c.ratio;
                rig.parameters[ParamIDs::compAttack] = c.attack;
                rig.parameters[ParamIDs::compRelease] = c.release;
                rig.parameters[ParamIDs::compAuto] = c.autoRelease ? 1.0f : 0.0f;
                rig.notes << c.note;
            }

    // The mastering rigs end with the LIMITER (0.8.0): BRICK, a -1 dB ceiling,
    // as a safety net against overs rather than to make it louder.
    for (auto& rig : rigs)
        if (rig.bank == mastering)
        {
            rig.parameters[ParamIDs::limitOn] = 1.0f;
            rig.parameters[ParamIDs::limitThreshold] = -1.0f;
            rig.parameters[ParamIDs::limitMode] = 0.0f;   // Brick
            rig.notes << " The LIMITER (stomp 7) holds a -1 dB ceiling.";
        }

    return rigs;
}
