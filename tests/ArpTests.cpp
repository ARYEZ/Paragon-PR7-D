// The ARPEGGIATOR (stage 5a): which notes it plays, when, how long and how
// loud, measured from the sound (a sine per note: its pitch and level).
#include "SynthTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "rigs/Rig.h"
#include "ui/MainView.h"

namespace
{
    using synth::arp::Mode;
    constexpr double step = 0.125;   // a 1/16 at 120 BPM

    struct ArpPlayer : Player
    {
        synth::arp::Patterns patterns = synth::arp::Patterns::makeDefault();
        ArpPlayer()
        {
            osc().wave = synth::OscWave::sine;
            osc().arp = 1;
            settings.amp.envelope.attackMs = 0.0f;
            settings.amp.envelope.sustain = 1.0f;
            settings.amp.envelope.releaseMs = 1.0f;
            settings.modulation.bpm = 120.0;
            arp().on = true;
            arp().patterns = &patterns;
        }
        synth::ArpSettings& arp() { return settings.arps[0]; }
        synth::arp::Block& block (int k, int pattern = 0) { return patterns.patterns[(size_t) pattern].blocks[(size_t) k]; }
        void clearPattern (int pattern = 0) { patterns.patterns[(size_t) pattern] = {}; }
    };

    // The note sounding around `seconds` (its frequency over 30 ms), as a MIDI note.
    double noteAt (const std::vector<float>& out, double seconds)
    {
        const auto hz = frequencyOf (out, (size_t) ((seconds - 0.015) * sampleRate), (size_t) ((seconds + 0.015) * sampleRate));
        return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : -1.0;
    }
    double loudnessAt (const std::vector<float>& out, double seconds, double halfWidth = 0.005)
    {
        return rms (out, (size_t) ((seconds - halfWidth) * sampleRate), (size_t) ((seconds + halfWidth) * sampleRate));
    }
    // The notes in the middle of steps 0, 1, 2...
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
}

TEST_CASE ("ARP: UP plays the held notes one a step, low to high, round again; GATE leaves a gap", "[arp]")
{
    const auto out = play ([] (ArpPlayer&) {}, { 60, 64, 67 }, 0.8);
    CHECK (stepNotes (out, 6) == std::vector<int> { 60, 64, 67, 60, 64, 67 });
    // GATE 80%: sounding at 90 ms into a step, quiet at 115.
    CHECK (loudnessAt (out, 0.09) > 0.05);
    CHECK (loudnessAt (out, 0.115, 0.002) < 0.001);
    // GATE 50%.
    const auto shorter = play ([] (ArpPlayer& p) { p.arp().gate = 0.5f; }, { 60 }, 0.3);
    CHECK (loudnessAt (shorter, 0.05) > 0.05);
    CHECK (loudnessAt (shorter, 0.09) < 0.001);
}

TEST_CASE ("ARP modes: DOWN, UP/DOWN, DOWN/UP, AS PLAYED, RANDOM, POLY; OCTAVES", "[arp]")
{
    const auto with = [] (Mode mode, std::initializer_list<int> keys, int steps, int octaves = 1)
    {
        return stepNotes (play ([mode, octaves] (ArpPlayer& p) { p.arp().mode = mode; p.arp().octaves = octaves; }, keys, steps * step + 0.1), steps);
    };
    CHECK (with (Mode::down, { 60, 64, 67 }, 4) == std::vector<int> { 67, 64, 60, 67 });
    CHECK (with (Mode::upDown, { 60, 64, 67 }, 6) == std::vector<int> { 60, 64, 67, 64, 60, 64 });
    CHECK (with (Mode::downUp, { 60, 64, 67 }, 6) == std::vector<int> { 67, 64, 60, 64, 67, 64 });
    CHECK (with (Mode::asPlayed, { 64, 60, 67 }, 4) == std::vector<int> { 64, 60, 67, 64 });
    CHECK (with (Mode::up, { 60, 64 }, 5, 2) == std::vector<int> { 60, 64, 72, 76, 60 });
    for (const auto note : with (Mode::random, { 60, 64, 67 }, 8))
        CHECK ((note == 60 || note == 64 || note == 67));

    // POLY: the chord together, on every step.
    const auto poly = play ([] (ArpPlayer& p) { p.arp().mode = Mode::poly; }, { 60, 64, 67 }, 0.4);
    for (const auto note : { 60, 64, 67 })
    {
        CHECK (levelAt (poly, noteHz (note), 600, 2400) > 0.05);
        CHECK (levelAt (poly, noteHz (note), 6600, 2400) > 0.05);   // step 1 too
    }
}

TEST_CASE ("ARP blocks: rests, TRANSPOSE, BLOCKS long, VELOCITY, RATCHET, SLIDE", "[arp]")
{
    const auto out = play ([] (ArpPlayer& p)
    {
        p.clearPattern();
        p.arp().length = 8;
        p.block (0) = { true, 1, 0, 1.0f, false, 1 };
        p.block (1) = { true, 1, 12, 1.0f, false, 1 };     // an octave up
        // step 2: a rest
        p.block (3) = { true, 2, 7, 0.5f, false, 1 };      // a fifth up, two steps long, half velocity
        p.block (5) = { true, 1, 0, 1.0f, false, 2 };      // twice in the step
    }, { 60 }, 1.0);
    CHECK (std::lround (noteAt (out, 0.05)) == 60);
    CHECK (std::lround (noteAt (out, step + 0.05)) == 72);
    CHECK (loudnessAt (out, 2 * step + 0.05) < 0.001);
    CHECK (std::lround (noteAt (out, 3 * step + 0.05)) == 67);
    CHECK (std::lround (noteAt (out, 4 * step + 0.05)) == 67);                 // still sounding in its second step
    CHECK (loudnessAt (out, 4 * step + 0.05) < 0.6 * loudnessAt (out, 0.05)); // at half velocity (AMP VELOCITY 100%)
    // RATCHET 2: two notes in step 5, each 80% of half a step: quiet between them.
    CHECK (loudnessAt (out, 5 * step + 0.03) > 0.05);
    CHECK (loudnessAt (out, 5 * step + 0.0575, 0.002) < 0.001);
    CHECK (loudnessAt (out, 5 * step + 0.09) > 0.05);

    // SLIDE: the first block glides into the second (GLIDE 30 ms): no gap, no new note, the pitch moves.
    const auto slid = play ([] (ArpPlayer& p)
    {
        p.clearPattern();
        p.arp().length = 4;
        p.block (0) = { true, 1, 0, 1.0f, true, 1 };
        p.block (1) = { true, 1, 12, 1.0f, false, 1 };
        p.settings.play.glideMs = 30.0f;
    }, { 60 }, 0.4);
    CHECK (loudnessAt (slid, step - 0.004, 0.003) > 0.05);   // no gap before the boundary (it would be quiet without SLIDE)
    CHECK (std::lround (noteAt (slid, step + 0.08)) == 72);
    const auto midGlide = noteAt (slid, step + 0.012);
    CHECK (midGlide > 61.0);
    CHECK (midGlide < 71.0);
}

TEST_CASE ("ARP patterns: the one chosen; CHAIN plays those with blocks in turn; LENGTH", "[arp]")
{
    const auto setup = [] (bool chain, int chosen)
    {
        return [chain, chosen] (ArpPlayer& p)
        {
            p.arp().length = 2;
            p.patterns.patterns[1] = {};
            for (int k = 0; k < 2; ++k)
                p.block (k, 1) = { true, 1, 12, 1.0f, false, 1 };   // B: an octave up
            p.patterns.patterns[3] = {};
            p.block (0, 3) = { true, 1, 24, 1.0f, false, 1 };       // D: two octaves up, on its first step
            p.arp().chain = chain;
            p.arp().pattern = chosen;
        };
    };
    CHECK (stepNotes (play (setup (false, 1), { 60 }, 0.6), 4) == std::vector<int> { 72, 72, 72, 72 });
    // CHAIN: A (2 steps), B (2), D (2: its second a rest), A...
    const auto chained = play (setup (true, 0), { 60 }, 1.0);
    CHECK (stepNotes (chained, 5) == std::vector<int> { 60, 60, 72, 72, 84 });
    CHECK (loudnessAt (chained, 5 * step + 0.05) < 0.001);
    CHECK (std::lround (noteAt (chained, 6 * step + 0.05)) == 60);
}

TEST_CASE ("ARP: velocity modes, HUMANIZE, STRUM, SHUFFLE", "[arp]")
{
    // Two keys, the first loud and the second soft: each note's own level, or the first key's.
    const auto levels = [] (synth::arp::VelocityMode mode)
    {
        ArpPlayer p;
        p.arp().velocityMode = mode;
        p.on (60, 1.0f);
        p.on (64, 0.25f);
        const auto out = p.render (0.3)[0];
        return std::pair (loudnessAt (out, 0.05), loudnessAt (out, step + 0.05));
    };
    const auto [each60, each64] = levels (synth::arp::VelocityMode::eachNote);
    CHECK (each64 < 0.5 * each60);
    const auto [first60, first64] = levels (synth::arp::VelocityMode::firstNote);
    CHECK_THAT (first64 / first60, WithinAbs (1.0, 0.05));
    const auto [last60, last64] = levels (synth::arp::VelocityMode::lastNote);
    CHECK_THAT (last60 / last64, WithinAbs (1.0, 0.05));
    CHECK (last60 < 0.5 * first60);

    // When a note starts: the first sample above a whisper after `from`.
    const auto startOf = [] (const std::vector<float>& out, double from)
    {
        for (auto i = (size_t) (from * sampleRate); i < out.size(); ++i)
            if (std::abs (out[i]) > 0.01f)
                return (double) i / sampleRate;
        return -1.0;
    };
    // SHUFFLE 100%: the second step half a step late.
    const auto shuffled = [&startOf] (float shuffle)
    {
        const auto out = play ([shuffle] (ArpPlayer& p) { p.arp().gate = 0.5f; p.settings.stepSeq.shuffle = shuffle; }, { 60 }, 0.4);
        return startOf (out, 0.07);
    };
    CHECK_THAT (shuffled (0.0f), WithinAbs (step, 0.001));
    CHECK_THAT (shuffled (1.0f), WithinAbs (1.5 * step, 0.001));

    // HUMANIZE: each block late by up to a quarter step, never early.
    const auto human = play ([] (ArpPlayer& p) { p.arp().gate = 0.5f; p.arp().humanize = 1.0f; }, { 60 }, 1.0);
    for (int k = 1; k < 6; ++k)
    {
        const auto start = startOf (human, k * step - 0.025);   // (the last note, late too, ends 31 ms before at the latest)
        CHECK (start >= k * step - 0.0005);
        CHECK (start <= k * step + 0.25 * step + 0.001);
    }

    // STRUM 100% (POLY): the chord's notes one after another over half a step.
    const auto strum = play ([] (ArpPlayer& p) { p.arp().mode = Mode::poly; p.arp().strum = 1.0f; }, { 60, 67 }, 0.2);
    CHECK (levelAt (strum, noteHz (60), 240, 1200) > 0.05);
    CHECK (levelAt (strum, noteHz (67), 240, 1200) < 0.01);       // the top note not yet
    CHECK (levelAt (strum, noteHz (67), 3360, 1200) > 0.05);      // from half a step
}

TEST_CASE ("ARP routing: only its oscillators; the others play the keys; its key zone; the sustain pedal", "[arp]")
{
    // Oscillator 2 (two octaves up) not routed: it plays the key, held; oscillator 1 the arp.
    ArpPlayer p;
    p.arp().gate = 0.5f;
    auto& two = p.osc (1);
    two.on = true;
    two.wave = synth::OscWave::sine;
    two.transpose = 24.0f;
    two.randomPhase = false;
    p.on (57);
    auto out = p.render (0.3)[0];
    CHECK (levelAt (out, noteHz (81), 4000, 1200) > 0.05);   // the key, through the arp's gap
    CHECK (levelAt (out, noteHz (57), 4000, 1200) < 0.005);  // the arp's note, in its gap
    CHECK (levelAt (out, noteHz (57), 600, 1200) > 0.05);
    p.off (57);
    out = p.render (0.3)[0];
    CHECK (rms (out, 2400, 14000) < 0.001);                    // both stop

    // Arp off: oscillator 1 plays the key straight.
    ArpPlayer off;
    off.arp().on = false;
    off.on (57);
    const auto straight = off.render (0.3)[0];
    CHECK (loudnessAt (straight, 0.115, 0.002) > 0.05);        // no gate

    // Key zone C4 up: A3 doesn't play the arp's oscillators at all.
    const auto zoned = play ([] (ArpPlayer& q) { q.arp().lowKey = 60; }, { 57 }, 0.3);
    CHECK (rms (zoned, 0, zoned.size()) < 0.0001);

    // Sustain pedal: the arp keeps going after the key's let go.
    ArpPlayer pedal;
    pedal.midi (juce::MidiMessage::controllerEvent (1, 64, 127));
    pedal.on (60);
    pedal.render (0.1);
    pedal.off (60);
    const auto held = pedal.render (0.4)[0];
    CHECK (loudnessAt (held, 2 * step - 0.1 + 0.05) > 0.05);
}

TEST_CASE ("ARP SONG: in step with the song's beats while it plays", "[arp]")
{
    // The song half a step in when the key goes down: the arp waits for the next step (62.5 ms).
    ArpPlayer p;
    p.arp().gate = 0.5f;
    p.settings.modulation.ppq = 0.125;   // the song playing when the key goes down
    p.on (60);
    std::vector<float> out;
    std::array<float, blockSize> left {}, right {};
    const juce::MidiBuffer none;
    for (int block = 0; block < 30; ++block)
    {
        p.settings.modulation.ppq = 0.125 + block * blockSize / sampleRate * 2.0;
        p.engine.process (left.data(), right.data(), blockSize, none, 0, p.settings);
        out.insert (out.end(), left.begin(), left.end());
    }
    CHECK (rms (out, 0, 2900) < 0.001);
    CHECK (rms (out, 3100, 5800) > 0.05);   // 64.6 to 121 ms: the step from 62.5 ms (half of it, GATE 50%)
}

TEST_CASE ("ARP in the plugin: settings reach the engine, it plays from a fresh start, its patterns are kept", "[arp][processor]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    const auto set = [&] (const juce::String& id, float value)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    set (ParamIDs::arp (2, "On"), 1.0f);
    set (ParamIDs::arp (2, "Mode"), (float) Mode::downUp);
    set (ParamIDs::arp (2, "Octaves"), 3.0f);
    set (ParamIDs::arp (2, "Length"), 12.0f);
    set (ParamIDs::arp (2, "Chain"), 1.0f);
    set (ParamIDs::osc (1, "Arp"), 3.0f);
    {
        const auto s = processor.getSynthSettings();
        CHECK (s.arps[2].on);
        CHECK (s.arps[2].mode == Mode::downUp);
        CHECK (s.arps[2].octaves == 3);
        CHECK (s.arps[2].length == 12);
        CHECK (s.arps[2].chain);
        CHECK (s.oscillators[1].arp == 3);
        CHECK (s.oscillators[0].arp == 0);
    }

    // Oscillator 1 to ARP 1, on: a fresh plugin's pattern (16 blocks) arpeggiates a held chord.
    set (ParamIDs::arp (0, "On"), 1.0f);
    set (ParamIDs::osc (0, "Arp"), 1.0f);
    set (ParamIDs::osc (0, "Wave"), (float) synth::OscWave::sine);
    set (ParamIDs::ampRelease, 1.0f);   // each note gone before the next (so its pitch reads cleanly)
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 67, 1.0f), 0);
    std::vector<float> out;
    for (int block = 0; block < 80; ++block)   // 0.43 s
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        midi.clear();
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    CHECK (stepNotes (out, 3) == std::vector<int> { 60, 67, 60 });

    // Presets, projects and undo keep the patterns.
    auto matrix = processor.getModMatrix();
    matrix.arps[4].patterns[2].blocks[5] = { true, 3, -5, 0.4f, true, 3 };
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    const auto rig = Rig::fromXml (*processor.captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->modulation.arps == matrix.arps);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (reopened.getModMatrix().arps == matrix.arps);
    }
    processor.flushUndoStep();
    auto edited = processor.getModMatrix();
    edited.arps[4] = synth::arp::Patterns::makeDefault();
    processor.setModMatrix (edited);
    processor.modMatrixEditFinished();
    processor.flushUndoStep();
    processor.undo();
    CHECK (processor.getModMatrix().arps == matrix.arps);
}

TEST_CASE ("The ARP page: laid out cleanly; clicks put, move, lengthen and delete blocks; routing, shift, patterns", "[arp][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::arp);
    auto& page = view->getArpPage();
    page.selectArp (2);

    std::vector<juce::Component*> shown;
    for (auto* child : page.getChildren())
        if (child->isVisible())
            shown.push_back (child);
    CHECK (shown.size() > 40);
    for (size_t i = 0; i < shown.size(); ++i)
    {
        INFO (shown[i]->getBounds().toString());
        CHECK (page.getLocalBounds().contains (shown[i]->getBounds()));
        for (size_t j = i + 1; j < shown.size(); ++j)
        {
            INFO (shown[i]->getBounds().toString() << " and " << shown[j]->getBounds().toString());
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
        }
    }

    auto& grid = page.getEditor();
    CHECK (grid.getArp() == 2);
    const auto pattern = [&processor] (int p = 0) { return processor.getModMatrix().arps[2].patterns[(size_t) p]; };
    const auto mouse = [&grid] (juce::Point<float> at, bool right = false)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                 right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &grid, &grid, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    };
    const auto click = [&] (juce::Point<float> at, bool right = false) { grid.mouseDown (mouse (at, right)); grid.mouseUp (mouse (at, right)); };

    // A new arp: blocks on steps 0-15. Step 3's block moved up a fifth: click at +7 on step 3.
    click (grid.cell (3, 7).getCentre());
    CHECK (pattern().blocks[3].on);
    CHECK (pattern().blocks[3].transpose == 7);
    // Drag it to an octave down.
    const auto from = grid.cell (3, 7).getCentre();
    grid.mouseDown (mouse (from));
    grid.mouseDrag (mouse (grid.cell (3, -12).getCentre()));
    grid.mouseUp (mouse (grid.cell (3, -12).getCentre()));
    CHECK (pattern().blocks[3].transpose == -12);
    // Its right edge dragged to step 5: three steps long.
    const auto edge = grid.cell (3, -12).getTopRight().translated (-2.0f, 2.0f);
    grid.mouseDown (mouse (edge));
    grid.mouseDrag (mouse (grid.cell (5, -12).getCentre()));
    grid.mouseUp (mouse (grid.cell (5, -12).getCentre()));
    CHECK (pattern().blocks[3].length == 3);
    // Right-click deletes it.
    click (grid.cell (4, -12).getCentre(), true);   // (within its length)
    CHECK_FALSE (pattern().blocks[3].on);
    // Lanes: slide, ratchet up and down, velocity.
    click (grid.slideArea (0).getCentre());
    CHECK (pattern().blocks[0].slide);
    click (grid.ratchetArea (0).getCentre());
    click (grid.ratchetArea (0).getCentre());
    CHECK (pattern().blocks[0].ratchet == 3);
    click (grid.ratchetArea (0).getCentre(), true);
    CHECK (pattern().blocks[0].ratchet == 2);
    const auto lane = grid.velocityArea (1);
    click ({ lane.getCentreX(), lane.getY() + lane.getHeight() * 0.75f });
    CHECK_THAT (pattern().blocks[1].velocity, WithinAbs (0.27, 0.05));

    // +1 / -1 move every block.
    page.shift (1);
    CHECK (pattern().blocks[0].transpose == 1);
    page.shift (-1);
    CHECK (pattern().blocks[0].transpose == 0);

    // Pattern B: chosen (the arp's PATTERN) and edited; START FROM.
    page.selectPattern (1);
    CHECK (processor.getSynthSettings().arps[2].pattern == 1);
    CHECK (grid.getPattern() == 1);
    page.startFrom (2);   // octave bounce
    CHECK (pattern (1).blocks[1].transpose == 12);
    CHECK (pattern (0).blocks[1].transpose == 0);

    // Routing oscillators 1 and 3 to this arp, then 1 back to the keys.
    page.route (0, true);
    page.route (2, true);
    CHECK (processor.getSynthSettings().oscillators[0].arp == 3);
    CHECK (page.isRouted (2));
    processor.flushUndoStep();   // (the plugin's timer records a step between edits; tests have no timer running)
    page.route (0, false);
    CHECK (processor.getSynthSettings().oscillators[0].arp == 0);

    // Each edit an undo step.
    processor.flushUndoStep();
    processor.undo();
    CHECK (page.isRouted (0));
}
