// The ARPEGGIATOR, part 2 (stage 5b): CHORDS, FIXED NOTE, the RANDOMIZER and
// AUTODICE, ARP NOTES triggers, ARPS TOGETHER, MIDI export and saving. Measured
// from the sound (a sine per note) where it plays, from the data where it doesn't.
#include "SynthTestUtils.h"

#include <set>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "rigs/ModMatrix.h"
#include "ui/ArpPage.h"
#include "ui/MainView.h"

namespace
{
    using synth::arp::Mode;
    using synth::arp::NoteSource;
    constexpr double step = 0.125;   // a 1/16 at 120 BPM

    struct ArpPlayer : Player
    {
        synth::arp::Patterns patterns = synth::arp::Patterns::makeDefault();
        std::vector<synth::mod::Route> routes;
        std::array<synth::mod::Shape, synth::mod::numEnvelopes> envelopes {};
        std::array<synth::mod::Shape, synth::mod::numLfoShapes> lfoShapes {};
        synth::arp::Randomizer randomizer;
        ArpPlayer()
        {
            osc().wave = synth::OscWave::sine;
            osc().arp = 1;
            settings.amp.envelope.attackMs = 0.0f;
            settings.amp.envelope.sustain = 1.0f;
            settings.amp.envelope.releaseMs = 1.0f;
            settings.modulation.bpm = 120.0;
            settings.modulation.envelopeShapes = envelopes.data();
            settings.modulation.lfoShapes = lfoShapes.data();
            arp().on = true;
            arp().patterns = &patterns;
        }
        synth::ArpSettings& arp (int k = 0) { return settings.arps[(size_t) k]; }
        synth::arp::Block& block (int k, int pattern = 0) { return patterns.patterns[(size_t) pattern].blocks[(size_t) k]; }
        // One block a step for `length` steps, each with `edit` applied.
        void everyStep (int length, const std::function<void (synth::arp::Block&)>& edit = {})
        {
            patterns.patterns[0] = {};
            arp().length = length;
            for (int k = 0; k < length; ++k)
            {
                block (k) = { true };
                if (edit)
                    edit (block (k));
            }
        }
    };

    double noteAt (const std::vector<float>& out, double seconds)
    {
        const auto hz = frequencyOf (out, (size_t) ((seconds - 0.015) * sampleRate), (size_t) ((seconds + 0.015) * sampleRate));
        return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : -1.0;
    }
    double loudnessAt (const std::vector<float>& out, double seconds, double halfWidth = 0.005)
    {
        return rms (out, (size_t) ((seconds - halfWidth) * sampleRate), (size_t) ((seconds + halfWidth) * sampleRate));
    }
    // How loud `note` is in step k of slow steps (1/4s: half a second), over 300 ms of it: long enough
    // that a note a semitone away doesn't show (the measure has no window, so short stretches leak).
    constexpr double slowStep = 0.5;
    double noteLevel (const std::vector<float>& out, int note, int k = 0)
    {
        return levelAt (out, noteHz (note), (size_t) ((k * slowStep + 0.05) * sampleRate), (size_t) (0.3 * sampleRate));
    }
    void slow (ArpPlayer& p) { p.arp().division = synth::seq::divisionNames.indexOf ("1/4"); }
    std::vector<int> stepNotes (const std::vector<float>& out, int steps)
    {
        std::vector<int> notes;
        for (int k = 0; k < steps; ++k)
            notes.push_back ((int) std::lround (noteAt (out, k * step + 0.05)));
        return notes;
    }
    std::vector<float> play (const std::function<void (ArpPlayer&)>& setup, std::initializer_list<int> keys, double seconds)
    {
        ArpPlayer p;
        setup (p);
        for (const auto key : keys)
            p.on (key);
        return p.render (seconds)[0];
    }
    // A note sounding reads about 1; one a semitone from a sounding one about 0.04 (leaking into the measure).
    bool sounds (const std::vector<float>& out, int note, int k = 0) { return noteLevel (out, note, k) > 0.3; }
}

TEST_CASE ("ARP CHORDS: each block a chord on the next note, its tones switched, a sub note under it", "[arp]")
{
    // C4 held, a minor chord: C, Eb, G (not E).
    const auto minor = play ([] (ArpPlayer& p)
    {
        slow (p);
        p.arp().mode = Mode::chords;
        p.everyStep (4, [] (synth::arp::Block& b) { b.chord = synth::arp::chordNames.indexOf ("Minor"); });
    }, { 60 }, 0.5);
    CHECK (sounds (minor, 60));
    CHECK (sounds (minor, 63));
    CHECK (sounds (minor, 67));
    CHECK_FALSE (sounds (minor, 64));

    // A 7th without its fifth (tone 3 off), a sub an octave below; on the next held note in turn (E4, then C4 again).
    const auto seventh = play ([] (ArpPlayer& p)
    {
        slow (p);
        p.arp().mode = Mode::chords;
        p.everyStep (4, [] (synth::arp::Block& b)
        {
            b.chord = synth::arp::chordNames.indexOf ("7");
            b.chordMask = 0x1f & ~0x4;
            b.sub = true;
            b.subTranspose = -12;
        });
    }, { 60, 64 }, 1.0);
    for (const auto note : { 48, 60, 64, 70 })
        CHECK (sounds (seventh, note, 0));
    CHECK_FALSE (sounds (seventh, 67, 0));
    for (const auto note : { 52, 64, 68, 74 })   // step 1: on E4
        CHECK (sounds (seventh, note, 1));
    CHECK_FALSE (sounds (seventh, 60, 1));

    // Every chord's tones are 0 first and rising, and no more than five.
    for (int c = 0; c < synth::arp::chordNames.size(); ++c)
    {
        const auto& shape = synth::arp::chordShape (c);
        REQUIRE (shape.count >= 1);
        REQUIRE (shape.count <= 5);
        CHECK (shape.intervals[0] == 0);
        for (int t = 1; t < shape.count; ++t)
            CHECK (shape.intervals[(size_t) t] > shape.intervals[(size_t) t - 1]);
    }
}

TEST_CASE ("ARP FIXED NOTE: a held chord's root alone; FIXED NOTE + moves the pattern's minor intervals over a major chord", "[arp]")
{
    // C major in any inversion: C. E G C (64 67 72): the root in the lowest key's octave, 72.
    const auto fixed = [] (std::initializer_list<int> keys, int transpose, NoteSource source)
    {
        return stepNotes (play ([=] (ArpPlayer& p)
        {
            p.arp().noteSource = source;
            p.everyStep (4, [=] (synth::arp::Block& b) { b.transpose = transpose; });
        }, keys, 0.5), 3);
    };
    CHECK (fixed ({ 60, 64, 67 }, 0, NoteSource::fixedNote) == std::vector<int> { 60, 60, 60 });
    CHECK (fixed ({ 64, 67, 72 }, 0, NoteSource::fixedNote) == std::vector<int> { 72, 72, 72 });
    CHECK (fixed ({ 62, 65, 69 }, 0, NoteSource::fixedNote) == std::vector<int> { 62, 62, 62 });   // D minor
    // A block 3 up (written as a minor third): over C major +, E; over C minor +, Eb; plain FIXED NOTE leaves it.
    CHECK (fixed ({ 60, 64, 67 }, 3, NoteSource::fixedNotePlus) == std::vector<int> { 64, 64, 64 });
    CHECK (fixed ({ 60, 63, 67 }, 3, NoteSource::fixedNotePlus) == std::vector<int> { 63, 63, 63 });
    CHECK (fixed ({ 60, 64, 67 }, 3, NoteSource::fixedNote) == std::vector<int> { 63, 63, 63 });
    // ...an octave up too, and the sixth and seventh: 8 to 9, 10 to 11; fifths stay.
    CHECK (synth::arp::plusInterval (15, true) == 16);
    CHECK (synth::arp::plusInterval (8, true) == 9);
    CHECK (synth::arp::plusInterval (-2, true) == -1);
    CHECK (synth::arp::plusInterval (7, true) == 7);
    CHECK (synth::arp::plusInterval (10, false) == 10);

    // Chord detection on its own.
    const auto detect = [] (std::initializer_list<int> notes)
    {
        std::vector<synth::arp::Held> held;
        for (const auto n : notes)
            held.push_back ({ n, 1.0f, (int) held.size() });
        return synth::arp::detectChord (held.data(), (int) held.size());
    };
    CHECK (detect ({ 57, 60, 64 }).root == 57);    // A minor
    CHECK_FALSE (detect ({ 57, 60, 64 }).major);
    CHECK (detect ({ 55, 59, 62, 65 }).root == 55); // G7
    CHECK (detect ({ 55, 59, 62, 65 }).major);
    CHECK (detect ({ 48 }).root == 48);              // one key: itself
}

TEST_CASE ("ARP RANDOMIZER: a roll keeps to the scale and the chances, the same seed the same pattern; SYMMETRY", "[arp]")
{
    synth::arp::Randomizer r;
    r.scale = synth::arp::scaleNames.indexOf ("Minor Pentatonic");
    r.restChance = 0.0f;
    r.lengthChances = { 1.0f, 0.0f, 0.0f, 0.0f };
    const synth::arp::Pattern empty {};
    const auto a = synth::arp::roll (r, 16, empty, 5);
    CHECK (a == synth::arp::roll (r, 16, empty, 5));
    CHECK_FALSE (a == synth::arp::roll (r, 16, empty, 6));
    std::set<int> semitones;
    for (int k = 0; k < 16; ++k)
    {
        const auto& b = a.blocks[(size_t) k];
        REQUIRE (b.on);   // no rests asked for
        CHECK (b.length == 1);
        CHECK (synth::arp::inScale (r.scale, b.transpose));
        CHECK (b.transpose >= -24);
        CHECK (b.transpose <= 36);
        CHECK (b.velocity >= r.velocityLow - 1.0e-6f);
        CHECK (b.velocity <= r.velocityHigh + 1.0e-6f);
        semitones.insert (((b.transpose % 12) + 12) % 12);
    }
    CHECK (semitones.size() >= 3);   // not one note over and over
    for (int k = 16; k < synth::arp::maxSteps; ++k)
        CHECK_FALSE (a.blocks[(size_t) k].on);

    // Rests at 100%: nothing; lengths of 4 only: four blocks of four.
    r.restChance = 1.0f;
    CHECK (synth::arp::roll (r, 16, empty, 1).isEmpty());
    r.restChance = 0.0f;
    r.lengthChances = { 0.0f, 0.0f, 0.0f, 1.0f };
    const auto longs = synth::arp::roll (r, 16, empty, 1);
    for (int k = 0; k < 16; ++k)
        CHECK (longs.blocks[(size_t) k].on == (k % 4 == 0));

    // Only the notes rolled: the lengths, velocities and rests already there stay.
    r.lengthChances = { 1.0f, 0.0f, 0.0f, 0.0f };
    r.rollLengths = r.rollVelocity = r.rollRests = r.rollOctaves = false;
    auto current = synth::arp::Pattern {};
    for (int k = 0; k < 8; k += 2)
        current.blocks[(size_t) k] = { true, 1, 12, 0.3f };
    const auto notesOnly = synth::arp::roll (r, 8, current, 3);
    for (int k = 0; k < 8; ++k)
    {
        CHECK (notesOnly.blocks[(size_t) k].on == (k % 2 == 0));
        if (k % 2 == 0)
        {
            CHECK (notesOnly.blocks[(size_t) k].velocity == 0.3f);
            CHECK (notesOnly.blocks[(size_t) k].transpose >= 12);   // the octave kept
            CHECK (notesOnly.blocks[(size_t) k].transpose < 24);
        }
    }

    // SYMMETRY: the second half is the first backwards.
    synth::arp::Randomizer s;
    s.symmetry = true;
    s.restChance = 0.0f;
    s.lengthChances = { 1.0f, 0.0f, 0.0f, 0.0f };
    const auto mirrored = synth::arp::roll (s, 16, empty, 9);
    for (int k = 0; k < 8; ++k)
        CHECK (mirrored.blocks[(size_t) k] == mirrored.blocks[(size_t) (15 - k)]);
}

TEST_CASE ("ARP AUTODICE: a new pattern each cycle, the same on every run", "[arp]")
{
    const auto run = [] (bool dice)
    {
        synth::arp::Randomizer r;
        r.scale = 0;   // chromatic: plenty to choose from
        r.restChance = 0.0f;
        r.lengthChances = { 1.0f, 0.0f, 0.0f, 0.0f };
        r.octaveChances = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
        r.noteChances.fill (1.0f);
        r.autoDice = dice;
        return stepNotes (play ([r] (ArpPlayer& p)
        {
            p.randomizer = r;
            p.arp().randomizer = &p.randomizer;
            p.everyStep (8);
        }, { 60 }, 16 * step + 0.1), 16);
    };
    const auto first = run (true);
    CHECK (first == run (true));   // repeatable
    const std::vector<int> cycle1 (first.begin(), first.begin() + 8), cycle2 (first.begin() + 8, first.end());
    CHECK (cycle1 != cycle2);
    for (const auto note : first)
    {
        CHECK (note >= 60);
        CHECK (note < 72);
    }
    // Off: the pattern as written (every step C4).
    CHECK (run (false) == std::vector<int> (16, 60));
}

TEST_CASE ("ARP NOTES trigger: an envelope restarted by every note the arp plays", "[arp]")
{
    // Envelope 1: full, falling to nothing over 100 ms, on the AMP's volume. The arp plays a note each step (125 ms).
    const auto run = [] (synth::mod::EnvelopeTrigger trigger)
    {
        ArpPlayer p;
        p.envelopes[0].points[0] = { 0.0f, 1.0f, 0.0f };
        p.envelopes[0].points[1] = { 0.1f, 0.0f, 0.0f };
        p.envelopes[0].count = 2;
        p.settings.modulation.envelopes[0].trigger = trigger;
        synth::mod::Route r;
        r.source = synth::mod::Source::env1;
        r.target = { synth::mod::Field::ampVolume, 0 };
        static const juce::NormalisableRange<float> volume { 0.0f, 2.0f };
        r.range = &volume;
        r.base = 0.0f;
        r.amount = 0.5f;
        p.routes.push_back (r);
        p.settings.modulation.routes = p.routes.data();
        p.settings.modulation.numRoutes = 1;
        p.everyStep (8, [] (synth::arp::Block& b) { b.length = 1; });
        p.arp().gate = 1.0f;
        p.on (60);
        return p.render (0.6)[0];
    };
    const auto arpNotes = run (synth::mod::EnvelopeTrigger::arpNotes);
    const auto firstNote = run (synth::mod::EnvelopeTrigger::firstNote);
    // Just after each step starts: loud with ARP NOTES; with FIRST NOTE only the first step is.
    for (int k = 0; k < 4; ++k)
        CHECK (loudnessAt (arpNotes, k * step + 0.01) > 0.3);
    CHECK (loudnessAt (firstNote, 0.01) > 0.3);
    CHECK (loudnessAt (firstNote, 2 * step + 0.01) < 0.01);
    // ...and quiet just before the next.
    CHECK (loudnessAt (arpNotes, 2 * step - 0.012, 0.002) < 0.05);
    CHECK (synth::mod::envelopeTriggerNames.indexOf ("Arp Notes") == (int) synth::mod::EnvelopeTrigger::arpNotes);
    CHECK (synth::mod::lfoTriggerNames.indexOf ("Arp Notes") == (int) synth::mod::LfoTrigger::arpNotes);
}

TEST_CASE ("ARPS TOGETHER: an arp started part way through a step waits for the next one, in step with the others", "[arp]")
{
    // ARP 1 takes keys below 64 (oscillator 1), ARP 2 those above (oscillator 2). C4 at 0 s, C5 at 60 ms.
    const auto run = [] (bool together)
    {
        ArpPlayer p;
        p.settings.arpsTogether = together;
        p.everyStep (8);
        p.arp().highKey = 63;
        p.arp (1) = p.arp();
        p.arp (1).lowKey = 64;
        p.arp (1).highKey = 127;
        p.settings.oscillators[1] = p.osc();
        p.settings.oscillators[1].on = true;
        p.settings.oscillators[1].arp = 2;
        p.on (60);
        auto out = p.render (0.06)[0];
        p.on (72);
        const auto rest = p.render (0.5)[0];
        out.insert (out.end(), rest.begin(), rest.end());
        return out;
    };
    const auto apart = run (false), together = run (true);
    const auto c5At = [] (const std::vector<float>& out, double from, double length)
    {
        return levelAt (out, noteHz (72), (size_t) (from * sampleRate), (size_t) (length * sampleRate));
    };
    // Apart: C5 from 60 ms (its own clock). Together: not until step 1 (125 ms), then on the steps.
    CHECK (c5At (apart, 0.07, 0.04) > 0.03);
    CHECK (c5At (together, 0.07, 0.04) < 0.3 * c5At (apart, 0.07, 0.04));   // what's left is C4 leaking into the measure
    CHECK (c5At (together, step + 0.01, 0.04) > 0.03);
    // Together, each of C5's notes starts with C4's: silent just before step 2's start, sounding just after.
    CHECK (c5At (together, 2 * step - 0.008, 0.006) < 0.3 * c5At (together, 2 * step + 0.004, 0.02));
    CHECK (c5At (together, 2 * step + 0.004, 0.02) > 0.03);
}

TEST_CASE ("ARP MIDI export: the notes, times and lengths the arp plays, as a MIDI file", "[arp][processor]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    auto& state = processor.getParameters();
    const auto set = [&] (const juce::String& id, float value)
    {
        auto* p = state.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ParamIDs::arp (0, "On"), 1.0f);
    set (ParamIDs::arp (0, "Length"), 8.0f);
    set (ParamIDs::arp (0, "Gate"), 0.5f);
    auto matrix = processor.getModMatrix();
    auto& pattern = matrix.arps[0].patterns[0];
    pattern = {};
    for (int k = 0; k < 8; ++k)
        pattern.blocks[(size_t) k] = { true, 1, 0, 1.0f };
    pattern.blocks[3] = { true, 1, 12, 0.5f };   // an octave up at half velocity
    pattern.blocks[5].on = false;                  // a rest
    processor.setModMatrix (matrix);

    const auto file = processor.arpToMidi (0, { 60, 64, 67 });
    REQUIRE (file.getNumTracks() == 1);
    auto track = *file.getTrack (0);
    track.updateMatchedPairs();
    std::vector<std::tuple<int, int, int, int>> notes;   // start, length, note, velocity
    for (const auto* e : track)
        if (e->message.isNoteOn())
            notes.emplace_back ((int) e->message.getTimeStamp(), (int) (track.getTimeOfMatchingKeyUp (track.getIndexOf (e)) - e->message.getTimeStamp()),
                                e->message.getNoteNumber(), (int) e->message.getVelocity());
    // 1/16s at 960 a beat: 240 ticks each, sounding half (GATE 50%).
    REQUIRE (notes.size() == 7);
    // UP over C E G; step 3 an octave up; step 5 a rest (which doesn't move on to the next note).
    const std::vector<int> pitches { 60, 64, 67, 72, 64, 67, 60 };
    const std::vector<int> starts { 0, 240, 480, 720, 960, 1440, 1680 };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CHECK (std::get<0> (notes[i]) == starts[i]);
        CHECK (std::get<1> (notes[i]) == 120);
        CHECK (std::get<2> (notes[i]) == pitches[i]);
    }
    CHECK (std::get<3> (notes[3]) < std::get<3> (notes[0]));   // half velocity
    // A file: written and read back the same.
    const auto out = juce::File::createTempFile (".mid");
    REQUIRE (processor.exportArpMidi (0, out));
    juce::FileInputStream in (out);
    juce::MidiFile back;
    REQUIRE (back.readFrom (in));
    CHECK (back.getTrack (0)->getNumEvents() == file.getTrack (0)->getNumEvents());
    out.deleteFile();

    // CHORDS: the chord's notes together.
    set (ParamIDs::arp (0, "Mode"), (float) (int) Mode::chords);
    auto chords = processor.arpToMidi (0, { 60 });
    int atZero = 0;
    for (const auto* e : *chords.getTrack (0))
        if (e->message.isNoteOn() && e->message.getTimeStamp() == 0.0)
            ++atZero;
    CHECK (atZero == 3);   // a major chord (the block's default)
}

TEST_CASE ("ARP 5b saving: chords, randomizers, NOTE SOURCE and ARPS TOGETHER kept; 0.15.0's blocks still read", "[arp][processor]")
{
    ModMatrix m;
    auto& b = m.arps[2].patterns[1].blocks[4];
    b = { true, 2, -5, 0.75f, false, 1, synth::arp::chordNames.indexOf ("Min7"), 0x0b, true, -24 };
    auto& r = m.arpRandomizers[5];
    r.scale = synth::arp::scaleNames.indexOf ("Dorian");
    r.noteChances[3] = 0.25f;
    r.octaveChances[4] = 0.5f;
    r.restChance = 0.4f;
    r.rollOctaves = false;
    r.symmetry = r.autoDice = true;
    r.seed = 1234;
    const auto back = ModMatrix::fromXml (*m.toXml());
    CHECK (back == m);
    CHECK (back.arpRandomizers[5].seed == 1234);
    CHECK (back.arps[2].patterns[1].blocks[4].chordMask == 0x0b);
    CHECK (ModMatrix {}.toXml()->getNumChildElements() == 0);   // nothing saved when nothing changed

    // A block as 0.15.0 wrote it: the chord settings at their defaults.
    juce::XmlElement old ("Modulation");
    auto* e = old.createNewChildElement ("ArpPattern");
    e->setAttribute ("arp", 1);
    e->setAttribute ("pattern", "A");
    e->setAttribute ("blocks", "0:2:7:0.500:1:3");
    const auto read = ModMatrix::fromXml (old).arps[0].patterns[0].blocks[0];
    CHECK (read == synth::arp::Block { true, 2, 7, 0.5f, true, 3 });

    // The new parameters reach the engine.
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    state.getParameter (ParamIDs::arp (3, "NoteSource"))->setValueNotifyingHost (1.0f);
    state.getParameter (ParamIDs::arpsTogether)->setValueNotifyingHost (1.0f);
    const auto s = processor.getSynthSettings();
    CHECK (s.arps[3].noteSource == NoteSource::fixedNotePlus);
    CHECK (s.arpsTogether);
}

TEST_CASE ("The ARP page, 5b: CHORDS lanes, the NOTES column, ROLL (undoable)", "[arp][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::arp);
    auto& page = view->getArpPage();
    page.selectArp (1);
    auto& grid = page.getEditor();
    auto& notes = page.getNotesPanel();
    const auto pattern = [&processor] { return processor.getModMatrix().arps[1].patterns[0]; };
    const auto at = [] (juce::Component& c, juce::Point<float> where, bool right = false)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), where,
                                 right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, juce::Time::getCurrentTime(), where, juce::Time::getCurrentTime(), 1, false);
    };
    const auto click = [&] (juce::Component& c, juce::Point<float> where, bool right = false)
    {
        c.mouseDown (at (c, where, right));
        c.mouseUp (at (c, where, right));
        processor.flushUndoStep();
    };

    // The NOTES column beside the grid, inside the page; its own parts inside it, none on another.
    CHECK (page.getLocalBounds().contains (notes.getBounds()));
    CHECK_FALSE (notes.getBounds().intersects (grid.getBounds()));
    CHECK (notes.getWidth() >= 300);
    std::vector<juce::Component*> parts;
    for (auto* child : notes.getChildren())
        if (child->isVisible())
            parts.push_back (child);
    CHECK (parts.size() >= 20);
    for (size_t i = 0; i < parts.size(); ++i)
    {
        INFO (parts[i]->getBounds().toString());
        CHECK (notes.getLocalBounds().contains (parts[i]->getBounds()));
        for (size_t j = i + 1; j < parts.size(); ++j)
            CHECK_FALSE (parts[i]->getBounds().intersects (parts[j]->getBounds()));
    }

    // The chord lanes only in CHORDS mode, under RATCH, inside the editor; the grid shorter by them.
    const auto plainGrid = grid.gridArea();
    CHECK_FALSE (grid.showsChords());
    auto* mode = processor.getParameters().getParameter (ParamIDs::arp (1, "Mode"));
    mode->setValueNotifyingHost (mode->convertTo0to1 ((float) (int) Mode::chords));
    CHECK (grid.showsChords());
    CHECK (grid.gridArea().getHeight() < plainGrid.getHeight() - 50.0f);
    CHECK (grid.chordArea (0).getY() >= grid.ratchetArea (0).getBottom());
    CHECK (grid.subArea (0).getBottom() <= (float) grid.getHeight());

    // TONES: the third (tone 1) off and on; SUB on.
    click (grid, grid.toneArea (0, 1).getCentre());
    CHECK (pattern().blocks[0].chordMask == (0x1f & ~0x2));
    click (grid, grid.toneArea (0, 1).getCentre());
    CHECK (pattern().blocks[0].chordMask == 0x1f);
    click (grid, grid.subArea (2).getCentre());
    CHECK (pattern().blocks[2].sub);

    // A chance bar dragged: the note chance of the fifth (7 semitones) to the top.
    auto& bars = notes.getNotes();
    const auto fifth = bars.barArea (7);
    bars.mouseDown (at (bars, { fifth.getCentreX(), fifth.getBottom() - 2.0f }));
    bars.mouseDrag (at (bars, { fifth.getCentreX(), fifth.getY() - 10.0f }));
    bars.mouseUp (at (bars, { fifth.getCentreX(), fifth.getY() - 10.0f }));
    CHECK (processor.getModMatrix().arpRandomizers[1].noteChances[7] == 1.0f);
    CHECK (processor.getModMatrix().arpRandomizers[0].noteChances[7] != 1.0f);   // ARP 2's, not ARP 1's
    processor.flushUndoStep();

    // ROLL: a new seed and a new pattern in the scale; undo brings the old one back.
    const auto before = pattern();
    const auto seed = processor.getModMatrix().arpRandomizers[1].seed;
    notes.getRollButton().onClick();
    processor.flushUndoStep();
    CHECK (processor.getModMatrix().arpRandomizers[1].seed == seed + 1);
    CHECK_FALSE (pattern() == before);
    for (const auto& b : pattern().blocks)
        if (b.on)
            CHECK (synth::arp::inScale (processor.getModMatrix().arpRandomizers[1].scale, b.transpose));
    processor.undo();
    CHECK (pattern() == before);
}
