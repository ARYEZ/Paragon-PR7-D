// The STEP SEQUENCER (stage 4d): the trance gate on the volume, and the
// source in the matrix, measured as the gain it puts on a steady note (the
// sound with it, against the same sound without it).
#include "SynthTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "rigs/Rig.h"
#include "ui/MainView.h"

namespace
{
    // At 120 BPM a 1/16 step is 125 ms.
    constexpr double step = 0.125;

    struct SeqPlayer : Player
    {
        synth::seq::Pattern pattern;
        SeqPlayer()
        {
            osc().wave = synth::OscWave::sine;
            settings.amp.envelope.attackMs = 0.0f;
            settings.amp.envelope.sustain = 1.0f;
            settings.amp.envelope.releaseMs = 1.0f;
            settings.modulation.bpm = 120.0;
            settings.stepSeq.on = true;
            settings.stepSeq.pattern = &pattern;
        }
        synth::StepSeqSettings& seq() { return settings.stepSeq; }
        synth::seq::Step& at (int k, int row = 0) { return pattern.rows[(size_t) row][(size_t) k]; }
    };

    // The gain the step sequencer puts on the sound, over `seconds`, for each channel: its
    // sound divided by the same sound without it, as RMS over 1 ms around each moment asked.
    struct Gains
    {
        std::array<std::vector<float>, 2> with, without;
        double at (double seconds, int channel = 0) const
        {
            const auto centre = (size_t) (seconds * sampleRate);
            const auto from = centre - 24, to = centre + 24;
            return rms (with[(size_t) channel], from, to) / juce::jmax (1.0e-9, rms (without[(size_t) channel], from, to));
        }
    };
    Gains gains (const std::function<void (SeqPlayer&)>& setup, double seconds = 1.0, int note = 69)
    {
        SeqPlayer seq, plain;
        setup (seq);
        setup (plain);
        plain.seq().on = false;
        seq.on (note);
        plain.on (note);
        return { seq.render (seconds), plain.render (seconds) };
    }
}

TEST_CASE ("STEP SEQ: a trance gate: open for GATE of each step, shut for the rest, step after step", "[stepseq]")
{
    const auto g = gains ([] (SeqPlayer&) {});   // new: 16 steps of 1/16, all at full height, GATE 50%
    for (int k = 0; k < 6; ++k)
    {
        INFO ("step " << k);
        CHECK_THAT (g.at (k * step + 0.03), WithinAbs (1.0, 0.01));
        CHECK (g.at (k * step + 0.09) < 0.001);
    }
    // GATE 80%: open until 100 ms into the step.
    const auto longer = gains ([] (SeqPlayer& p) { p.seq().gate = 0.8f; });
    CHECK_THAT (longer.at (0.09), WithinAbs (1.0, 0.01));
    CHECK (longer.at (0.11) < 0.001);
}

TEST_CASE ("STEP SEQ: each step's height, deleted steps, and linked steps holding the gate open", "[stepseq]")
{
    const auto g = gains ([] (SeqPlayer& p)
    {
        p.at (1).level = 0.5f;
        p.at (2).on = false;
        p.at (3).tied = true;   // 3 linked to 4: open from 3's start to GATE of the way through 4
        p.at (4).level = 0.25f;
    });
    CHECK_THAT (g.at (1 * step + 0.03), WithinAbs (0.5, 0.01));
    CHECK (g.at (2 * step + 0.03) < 0.001);
    CHECK_THAT (g.at (3 * step + 0.03), WithinAbs (1.0, 0.01));
    CHECK_THAT (g.at (3 * step + 0.09), WithinAbs (1.0, 0.01));   // would be shut without the link
    CHECK_THAT (g.at (4 * step + 0.03), WithinAbs (0.25, 0.01));  // moved to 4's height
    CHECK (g.at (4 * step + 0.09) < 0.001);
}

TEST_CASE ("STEP SEQ: VOL MIX sets how deep it cuts; LENGTH where it starts over", "[stepseq]")
{
    const auto half = gains ([] (SeqPlayer& p) { p.seq().volMix = 0.5f; });
    CHECK_THAT (half.at (0.09), WithinAbs (0.5, 0.01));
    CHECK_THAT (half.at (0.03), WithinAbs (1.0, 0.01));

    // LENGTH 3, the second step deleted: steps 1, 4, 7 shut; the rest open.
    const auto three = gains ([] (SeqPlayer& p) { p.seq().length = 3; p.at (1).on = false; });
    for (int k = 0; k < 8; ++k)
    {
        INFO ("step " << k);
        if (k % 3 == 1)
            CHECK (three.at (k * step + 0.03) < 0.001);
        else
            CHECK_THAT (three.at (k * step + 0.03), WithinAbs (1.0, 0.01));
    }
}

TEST_CASE ("STEP SEQ: CONTOUR softens the edges; DECAY lets each block fall away", "[stepseq]")
{
    // CONTOUR 0: a hard edge (0.5 ms), open 1 ms in.
    const auto hard = gains ([] (SeqPlayer&) {});
    CHECK_THAT (hard.at (step + 0.0015), WithinAbs (1.0, 0.02));
    // CONTOUR 100%: the edge as long as it can be: half a step, but no more than half the time it's open
    // (GATE 50%: a quarter step, 31 ms); halfway up at 15.6 ms (a raised cosine).
    const auto soft = gains ([] (SeqPlayer& p) { p.seq().contour = 1.0f; });
    CHECK_THAT (soft.at (step + 0.015625), WithinAbs (0.5, 0.03));
    CHECK_THAT (soft.at (step + 0.0625 - 0.015625), WithinAbs (0.5, 0.03));
    CHECK (soft.at (step + 0.005) < 0.1);

    // DECAY 100 ms (60 dB in that time): -30 dB at 50 ms.
    const auto decay = gains ([] (SeqPlayer& p) { p.seq().decayMs = 100.0f; p.seq().gate = 1.0f; });
    CHECK_THAT (db (decay.at (step + 0.05)), WithinAbs (-30.0, 1.0));
    CHECK_THAT (db (decay.at (step + 0.02)), WithinAbs (-12.0, 1.0));
}

TEST_CASE ("STEP SEQ: SHUFFLE starts every second step late; TRIPLET and DOTTED steps", "[stepseq]")
{
    // Step 0 deleted, step 1 at full height, GATE 100%: when step 1 opens.
    const auto opensAt = [] (float shuffle)
    {
        const auto g = gains ([shuffle] (SeqPlayer& p) { p.at (0).on = false; p.seq().gate = 1.0f; p.seq().shuffle = shuffle; }, 0.4);
        for (double t = 0.1; t < 0.3; t += 0.0005)
            if (g.at (t) > 0.5)
                return t;
        return -1.0;
    };
    CHECK_THAT (opensAt (0.0f), WithinAbs (0.125, 0.001));
    CHECK_THAT (opensAt (0.5f), WithinAbs (0.15625, 0.001));   // a quarter step late
    CHECK_THAT (opensAt (1.0f), WithinAbs (0.1875, 0.001));    // half a step late (75% swing)

    // TRIPLET: steps of 83 ms; DOTTED: 188 ms.
    const auto triplet = gains ([] (SeqPlayer& p) { p.seq().timing = synth::seq::Timing::triplet; });
    CHECK_THAT (triplet.at (0.02), WithinAbs (1.0, 0.01));
    CHECK (triplet.at (0.06) < 0.001);
    CHECK_THAT (triplet.at (0.0833 + 0.02), WithinAbs (1.0, 0.01));
    const auto dotted = gains ([] (SeqPlayer& p) { p.seq().timing = synth::seq::Timing::dotted; });
    CHECK_THAT (dotted.at (0.08), WithinAbs (1.0, 0.01));
    CHECK (dotted.at (0.12) < 0.001);
    CHECK_THAT (dotted.at (0.1875 + 0.03), WithinAbs (1.0, 0.01));
}

TEST_CASE ("STEP SEQ: STEREO plays a row on each side; WIDTH 0 makes them the same", "[stepseq]")
{
    const auto setup = [] (float width)
    {
        return [width] (SeqPlayer& p)
        {
            p.seq().mode = synth::seq::Mode::stereo;
            p.seq().width = width;
            p.at (0, 1).on = false;   // the right's first step deleted
            p.at (1, 0).level = 0.0f; // the left's second step at no height
        };
    };
    const auto apart = gains (setup (1.0f));
    CHECK_THAT (apart.at (0.03, 0), WithinAbs (1.0, 0.01));
    CHECK (apart.at (0.03, 1) < 0.001);
    CHECK (apart.at (step + 0.03, 0) < 0.001);
    CHECK_THAT (apart.at (step + 0.03, 1), WithinAbs (1.0, 0.01));
    const auto same = gains (setup (0.0f));
    CHECK_THAT (same.at (0.03, 0), WithinAbs (0.5, 0.01));
    CHECK_THAT (same.at (0.03, 1), WithinAbs (0.5, 0.01));
    // MONO: the first row on both sides.
    const auto mono = gains ([] (SeqPlayer& p) { p.at (0, 1).on = false; });
    CHECK_THAT (mono.at (0.03, 1), WithinAbs (1.0, 0.01));
}

TEST_CASE ("STEP SEQ: DELAY and FADE IN bring it in after the note starts; the key zone", "[stepseq]")
{
    const auto g = gains ([] (SeqPlayer& p) { p.seq().delayMs = 100.0f; p.seq().fadeInMs = 200.0f; });
    CHECK_THAT (g.at (0.09), WithinAbs (1.0, 0.01));    // shut part of step 0, before DELAY: not cut
    CHECK_THAT (g.at (0.20), WithinAbs (0.5, 0.02));    // halfway through FADE IN: cut halfway
    CHECK (g.at (0.34) < 0.001);                        // all the way in

    const auto zone = [] (int note)
    {
        return gains ([] (SeqPlayer& p) { p.seq().lowKey = 60; p.seq().highKey = 72; }, 0.2, note).at (0.09);
    };
    CHECK_THAT (zone (57), WithinAbs (1.0, 0.001));     // below the zone: left alone
    CHECK (zone (64) < 0.001);
    CHECK_THAT (zone (76), WithinAbs (1.0, 0.001));     // above it
}

TEST_CASE ("STEP SEQ: SONG follows the song's beats; VOICE, FIRST NOTE and LAST NOTE start with notes", "[stepseq]")
{
    // SONG, the song playing from half a step in: the shut half of step 0 first, open at step 1 (62.5 ms).
    {
        SeqPlayer p;
        p.on (69);
        std::vector<float> out;
        std::array<float, blockSize> left {}, right {};
        const juce::MidiBuffer none;
        for (int block = 0; block < 40; ++block)
        {
            p.settings.modulation.ppq = 0.125 + block * blockSize / sampleRate * 2.0;   // 2 beats a second
            p.engine.process (left.data(), right.data(), blockSize, none, 0, p.settings);
            out.insert (out.end(), left.begin(), left.end());
        }
        CHECK (rms (out, 1200, 2400) < 0.001);           // 25 to 50 ms: shut
        CHECK (rms (out, 3600, 4800) > 0.05);             // 75 to 100 ms: step 1, open
        CHECK (rms (out, 7200, 8400) < 0.001);           // 150 to 175 ms: step 1's shut half
    }

    // Two notes: A from the start, B from 62.5 ms (when A's step is shut). How loud B is just after it starts.
    const auto secondNote = [] (synth::seq::Trigger trigger)
    {
        SeqPlayer p;
        p.seq().trigger = trigger;
        p.on (57);
        auto out = p.render (0.0625)[0];
        p.on (81);
        const auto rest = p.render (0.05)[0];
        out.insert (out.end(), rest.begin(), rest.end());
        return std::pair (levelAt (out, noteHz (81), 3200, 1200), levelAt (out, noteHz (57), 3200, 1200));
    };
    const auto [voiceB, voiceA] = secondNote (synth::seq::Trigger::voice);
    CHECK (voiceB > 0.05);                 // B's own gate, open at its start
    CHECK (voiceA < 0.001);                // A's still shut
    const auto [firstB, firstA] = secondNote (synth::seq::Trigger::firstNote);
    CHECK (firstB < 0.001);                // B follows the gate A started
    CHECK (firstA < 0.001);
    const auto [lastB, lastA] = secondNote (synth::seq::Trigger::lastNote);
    CHECK (lastB > 0.05);                  // B starts the gate over...
    CHECK (lastA > 0.05);                  // ...for A too
}

TEST_CASE ("STEP SEQ as a matrix source, with the trance gate itself off", "[stepseq][mod]")
{
    SeqPlayer p;
    p.seq().on = false;
    p.at (0).level = 0.5f;
    const juce::NormalisableRange<float> fineRange { -100.0f, 100.0f, 1.0f };
    synth::mod::Route route;
    route.source = synth::mod::Source::stepSeq;
    route.target = { synth::mod::Field::oscFine, 0 };
    route.range = &fineRange;
    route.base = 0.5f;
    route.amount = 0.5f;   // at full height: +100 cents
    p.settings.modulation.routes = &route;
    p.settings.modulation.numRoutes = 1;
    p.settings.modulation.usedSources = 1u << (int) synth::mod::Source::stepSeq;
    p.on (69);
    const auto out = p.render (0.4)[0];
    CHECK_THAT (1200.0 * std::log2 (frequencyOf (out, 480, 2880) / 440.0), WithinAbs (50.0, 1.0));     // step 0 at half height
    CHECK_THAT (1200.0 * std::log2 (frequencyOf (out, 3480, 5760) / 440.0), WithinAbs (0.0, 1.0));     // its shut half
    CHECK_THAT (1200.0 * std::log2 (frequencyOf (out, 6480, 8880) / 440.0), WithinAbs (100.0, 1.0));   // step 1
    // ...and with no route using it and the gate off, the sound is exactly as without it.
    SeqPlayer off, plain;
    off.seq().on = false;
    plain.settings.stepSeq = {};
    off.on (69);
    plain.on (69);
    CHECK (off.render (0.3)[0] == plain.render (0.3)[0]);
}

TEST_CASE ("STEP SEQ in the plugin: its settings reach the engine, it gates from a fresh start, its steps are kept", "[stepseq][processor]")
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
    set (ParamIDs::seqOn, 1.0f);
    set (ParamIDs::seqLength, 8.0f);
    set (ParamIDs::seqGate, 0.25f);
    set (ParamIDs::seqMode, 1.0f);
    set (ParamIDs::seqTrigger, (float) synth::seq::Trigger::voice);
    set (ParamIDs::seqLowKey, 36.0f);
    set (ParamIDs::shuffle, 0.4f);
    {
        const auto s = processor.getSynthSettings();
        CHECK (s.stepSeq.on);
        CHECK (s.stepSeq.length == 8);
        CHECK_THAT (s.stepSeq.gate, WithinAbs (0.25, 1.0e-3));
        CHECK (s.stepSeq.mode == synth::seq::Mode::stereo);
        CHECK (s.stepSeq.trigger == synth::seq::Trigger::voice);
        CHECK (s.stepSeq.lowKey == 36);
        CHECK_THAT (s.stepSeq.shuffle, WithinAbs (0.4, 1.0e-3));
    }

    // A fresh plugin's steps (all at full height) gate at once: GATE 25% of 1/16 at 120 BPM.
    set (ParamIDs::seqMode, 0.0f);
    set (ParamIDs::shuffle, 0.0f);
    set (ParamIDs::osc (0, "Wave"), (float) synth::OscWave::sine);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
    std::vector<float> out;
    for (int block = 0; block < 40; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        midi.clear();
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    CHECK (rms (out, 480, 960) > 0.05);         // 10 to 20 ms: open
    CHECK (rms (out, 2400, 4800) < 1.0e-4);     // 50 to 100 ms: shut
    CHECK (rms (out, 6480, 6960) > 0.05);       // step 1

    // Presets, projects and undo keep its steps.
    auto matrix = processor.getModMatrix();
    matrix.stepPattern.rows[0][3] = { false, 0.4f, false };
    matrix.stepPattern.rows[1][5] = { true, 0.3f, true };
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    const auto rig = Rig::fromXml (*processor.captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->modulation.stepPattern == matrix.stepPattern);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (reopened.getModMatrix().stepPattern == matrix.stepPattern);
    }
    processor.flushUndoStep();
    auto edited = processor.getModMatrix();
    edited.stepPattern = {};
    processor.setModMatrix (edited);
    processor.modMatrixEditFinished();
    processor.flushUndoStep();
    processor.undo();
    CHECK (processor.getModMatrix().stepPattern == matrix.stepPattern);
}

TEST_CASE ("CPU: a supersaw chord through the step sequencer, each note its own", "[stepseq][performance]")
{
    const auto once = [] (bool seq)
    {
        SeqPlayer p;
        p.seq().on = seq;
        p.seq().trigger = synth::seq::Trigger::voice;
        p.seq().contour = 0.3f;
        p.seq().decayMs = 200.0f;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.wave = synth::OscWave::saw;
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (2.0);
        return (juce::Time::getMillisecondCounterHiRes() - started) / 2000.0 * 100.0;
    };
    // Each the fastest of three, taken in turn (plain, sequenced, plain...) so a busy moment on a shared machine
    // slows both alike. The limit is relative: a slower machine costs more points for everything (0.17.0's macOS
    // runner measured 4.3% plain, 6.5% sequenced, +53%; here about 3.1% and 3.8%, +21%).
    auto plain = 1.0e9, gated = 1.0e9;
    for (int run = 0; run < 3; ++run)
    {
        plain = juce::jmin (plain, once (false));
        gated = juce::jmin (gated, once (true));
    }
    WARN ("2 saws x 7 copies x 8 keys: " << plain << "% of one core; with the step sequencer per note: " << gated << "% (the fastest of three)");
    CHECK (gated < plain * 1.75);
}

TEST_CASE ("The STEP tab: its controls laid out cleanly; clicking sets heights, deletes and links steps", "[stepseq][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::modulation);
    auto& panel = view->getModPage().getModulatorPanel();
    panel.selectStepSeq();
    CHECK (panel.getKind() == ModulatorPanel::Kind::step);

    std::vector<juce::Component*> shown;
    for (auto* child : panel.getChildren())
        if (child->isVisible())
            shown.push_back (child);
    CHECK (shown.size() > 30);
    for (size_t i = 0; i < shown.size(); ++i)
    {
        INFO (shown[i]->getBounds().toString());
        CHECK (panel.getLocalBounds().contains (shown[i]->getBounds()));
        for (size_t j = i + 1; j < shown.size(); ++j)
        {
            INFO (shown[i]->getBounds().toString() << " and " << shown[j]->getBounds().toString());
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
        }
    }

    // Clicks: a step's height where it's clicked, a right-click deletes, the arrow links.
    auto& steps = panel.getStepEditor();
    CHECK (steps.getLength() == 16);
    CHECK (steps.getNumLanes() == 1);
    const auto click = [&steps] (juce::Point<float> at, bool right = false)
    {
        const auto mods = right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier;
        const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &steps, &steps, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
        steps.mouseDown (e);
        steps.mouseUp (e);
    };
    const auto bar = steps.barArea (0, 2);
    click ({ bar.getCentreX(), bar.getY() + bar.getHeight() * 0.75f });   // a quarter of the way up
    CHECK_THAT (processor.getModMatrix().stepPattern.rows[0][2].level, WithinAbs (0.25, 0.02));
    click (steps.barArea (0, 5).getCentre(), true);
    CHECK_FALSE (processor.getModMatrix().stepPattern.rows[0][5].on);
    click (steps.linkArea (0, 7).getCentre());
    CHECK (processor.getModMatrix().stepPattern.rows[0][7].tied);
    // Each click is an undo step.
    processor.flushUndoStep();
    processor.undo();
    CHECK_FALSE (processor.getModMatrix().stepPattern.rows[0][7].tied);
}
