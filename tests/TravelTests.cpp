// TRAVEL (stage 4e): an oscillator's own movement through its wavetable,
// measured on a two-frame table (a sine, then its octave): the octave's share
// of the two is where in the table the note is (0: the first frame, 1: the last).
#include "SynthTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "rigs/Rig.h"
#include "ui/MainView.h"

namespace
{
    using synth::travel::StepMode;

    std::shared_ptr<const synth::Wavetable> sineAndOctave()
    {
        constexpr int size = 2048;
        auto samples = std::make_shared<std::vector<float>>();
        for (const auto h : { 1, 2 })
            for (int i = 0; i < size; ++i)
                samples->push_back ((float) std::sin (juce::MathConstants<double>::twoPi * h * i / size));
        return synth::Wavetable::build ({ "Sine and octave", size, samples });
    }

    // At 120 BPM a 1/4 is half a second: one SPEED period.
    constexpr double period = 0.5;

    struct TravelPlayer : Player
    {
        std::shared_ptr<const synth::Wavetable> table = sineAndOctave();
        synth::travel::Pattern pattern;
        TravelPlayer()
        {
            osc().wave = synth::OscWave::wavetable;
            osc().table = table.get();
            settings.amp.envelope.attackMs = 0.0f;
            settings.amp.envelope.sustain = 1.0f;
            settings.amp.envelope.releaseMs = 1.0f;
            settings.modulation.bpm = 120.0;
            tr().on = true;
            tr().pattern = &pattern;
        }
        synth::TravelSettings& tr() { return osc().travel; }
        synth::travel::Step& step (int k) { return pattern.steps[(size_t) k]; }
    };

    // Where in the table a note of `note` is at `seconds` (20 ms around it).
    double placeAt (const std::vector<float>& out, double seconds, int note = 57)
    {
        const auto from = (size_t) ((seconds - 0.01) * sampleRate);
        const auto f = noteHz (note);
        const auto low = levelAt (out, f, from, 960), high = levelAt (out, 2.0 * f, from, 960);
        return high / juce::jmax (1.0e-9, low + high);
    }
    double loudnessAt (const std::vector<float>& out, double seconds)
    {
        return rms (out, (size_t) ((seconds - 0.005) * sampleRate), (size_t) ((seconds + 0.005) * sampleRate));
    }

    std::vector<float> play (const std::function<void (TravelPlayer&)>& setup, double seconds = 1.2, float velocity = 1.0f)
    {
        TravelPlayer p;
        setup (p);
        p.on (57, velocity);
        return p.render (seconds)[0];
    }
}

TEST_CASE ("TRAVEL: the envelope moves the note through the table, a cycle each SPEED period", "[travel]")
{
    const auto out = play ([] (TravelPlayer&) {});   // a new envelope: a ramp from the first frame to the last, a 1/4 long
    CHECK_THAT (placeAt (out, 0.125), WithinAbs (0.25, 0.03));
    CHECK_THAT (placeAt (out, 0.375), WithinAbs (0.75, 0.03));
    CHECK_THAT (placeAt (out, period + 0.125), WithinAbs (0.25, 0.03));   // LOOP: round again

    // Off: INDEX alone.
    const auto off = play ([] (TravelPlayer& p) { p.tr().on = false; p.osc().tableIndex = 0.3f; });
    CHECK_THAT (placeAt (off, 0.375), WithinAbs (0.3, 0.02));

    // FREE: 4 cycles a second.
    const auto free = play ([] (TravelPlayer& p) { p.tr().sync = synth::mod::LfoSync::free; p.tr().rateHz = 4.0f; });
    CHECK_THAT (placeAt (free, 0.25 + 0.125), WithinAbs (0.5, 0.04));

    // A drawn envelope: up to the top in the first quarter, down to the middle after.
    synth::mod::Shape shape;
    shape.points[0] = { 0.0f, 0.0f, 0.0f };
    shape.points[1] = { 0.25f, 1.0f, 0.0f };
    shape.points[2] = { 1.0f, 0.5f, 0.0f };
    shape.count = 3;
    const auto drawn = play ([&shape] (TravelPlayer& p) { p.tr().shape = &shape; });
    CHECK_THAT (placeAt (drawn, 0.25 * period * 0.5), WithinAbs (0.5, 0.05));
    CHECK_THAT (placeAt (drawn, 0.6 * period), WithinAbs (0.75, 0.03));
}

TEST_CASE ("TRAVEL: STRENGTH and INDEX, FLIP, PHASE, and LOOP / PING PONG / ONE SHOT", "[travel]")
{
    const auto at = [] (double seconds, const std::function<void (TravelPlayer&)>& setup) { return placeAt (play (setup), seconds); };
    CHECK_THAT (at (0.375, [] (TravelPlayer& p) { p.tr().strength = 0.5f; }), WithinAbs (0.375, 0.03));
    CHECK_THAT (at (0.375, [] (TravelPlayer& p) { p.tr().strength = 0.5f; p.osc().tableIndex = 0.2f; }), WithinAbs (0.575, 0.03));
    CHECK_THAT (at (0.125, [] (TravelPlayer& p) { p.tr().strength = -1.0f; p.osc().tableIndex = 1.0f; }), WithinAbs (0.75, 0.03));
    CHECK_THAT (at (0.125, [] (TravelPlayer& p) { p.tr().flip = true; }), WithinAbs (0.75, 0.03));
    CHECK_THAT (at (0.125, [] (TravelPlayer& p) { p.tr().phase = 0.5f; }), WithinAbs (0.75, 0.03));
    CHECK_THAT (at (period + 0.125, [] (TravelPlayer& p) { p.tr().mode = synth::travel::Mode::pingPong; }), WithinAbs (0.75, 0.03));
    CHECK_THAT (at (period + 0.375, [] (TravelPlayer& p) { p.tr().mode = synth::travel::Mode::oneShot; }), WithinAbs (1.0, 0.02));
}

TEST_CASE ("TRAVEL SEQ: each step plays the envelope its own way", "[travel]")
{
    // Two steps; what the second (from 0.5 s) does, a quarter and three quarters in.
    const auto second = [] (StepMode mode, float iterations, double u)
    {
        const auto out = play ([mode, iterations] (TravelPlayer& p)
        {
            p.tr().editors = synth::travel::Editors::envSeq;
            p.tr().steps = 2;
            p.step (1).mode = mode;
            p.step (1).iterations = iterations;
        });
        return placeAt (out, period + u * period);
    };
    CHECK_THAT (second (StepMode::normal, 1.0f, 0.25), WithinAbs (0.25, 0.03));
    CHECK_THAT (second (StepMode::normal, 2.0f, 0.25), WithinAbs (0.5, 0.03));     // twice through in the step
    CHECK_THAT (second (StepMode::normal, 0.5f, 0.5), WithinAbs (0.25, 0.03));     // half of it over the step
    CHECK_THAT (second (StepMode::trigger, 2.0f, 0.75), WithinAbs (0.25, 0.03));   // its first half, twice
    CHECK_THAT (second (StepMode::pingPongHalf, 1.0f, 0.75), WithinAbs (0.25, 0.03));   // up the first half, back down
    CHECK_THAT (second (StepMode::reverse, 1.0f, 0.25), WithinAbs (0.75, 0.03));
    CHECK_THAT (second (StepMode::flip, 1.0f, 0.25), WithinAbs (0.75, 0.03));
    CHECK_THAT (second (StepMode::high, 1.0f, 0.5), WithinAbs (1.0, 0.01));
    CHECK_THAT (second (StepMode::mid, 1.0f, 0.5), WithinAbs (0.5, 0.01));
    CHECK_THAT (second (StepMode::low, 1.0f, 0.5), WithinAbs (0.0, 0.01));

    // SILENCE: the oscillator quiet for the step, back for the next.
    const auto silent = play ([] (TravelPlayer& p)
    {
        p.tr().editors = synth::travel::Editors::envSeq;
        p.tr().steps = 3;
        p.step (1).mode = StepMode::silence;
    }, 1.6);
    CHECK (loudnessAt (silent, 0.25) > 0.1);
    CHECK (loudnessAt (silent, period + 0.25) < 1.0e-4);
    CHECK (loudnessAt (silent, 2.0 * period + 0.25) > 0.1);

    // A step two periods long: the next starts a period later.
    const auto longer = play ([] (TravelPlayer& p)
    {
        p.tr().editors = synth::travel::Editors::envSeq;
        p.tr().steps = 2;
        p.step (0).length = 2;
        p.step (1).mode = StepMode::high;
    }, 1.6);
    CHECK_THAT (placeAt (longer, period + 0.25), WithinAbs (0.75, 0.03));   // still the first step, 3/4 through
    CHECK_THAT (placeAt (longer, 2.0 * period + 0.25), WithinAbs (1.0, 0.01));
}

TEST_CASE ("TRAVEL MOD: each step's strength curve; velocity and SMOOTH", "[travel]")
{
    // MOD: the first step's strength rising from 0 to 1 across it.
    const auto mod = play ([] (TravelPlayer& p)
    {
        p.tr().editors = synth::travel::Editors::envSeqMod;
        p.tr().steps = 1;
        p.pattern.strength[0].start = 0.0f;
        p.osc().tableIndex = 0.0f;
    });
    CHECK_THAT (placeAt (mod, 0.25), WithinAbs (0.25, 0.03));   // halfway: the ramp at 0.5, strength 0.5
    // ...not heard without MOD in EDITORS.
    const auto seqOnly = play ([] (TravelPlayer& p) { p.tr().editors = synth::travel::Editors::envSeq; p.tr().steps = 1; p.pattern.strength[0].start = 0.0f; });
    CHECK_THAT (placeAt (seqOnly, 0.25), WithinAbs (0.5, 0.03));

    // STRENGTH VELOCITY 100%: a note at half velocity goes half as far.
    const auto soft = play ([] (TravelPlayer& p) { p.tr().strengthVelocity = 1.0f; }, 1.2, 0.5f);
    CHECK_THAT (placeAt (soft, 0.375), WithinAbs (0.375, 0.03));

    // SMOOTH: HIGH then LOW steps; smoothed, it's still on its way down 50 ms after the step changes.
    const auto jumps = [] (float smooth)
    {
        return placeAt (play ([smooth] (TravelPlayer& p)
        {
            p.tr().editors = synth::travel::Editors::envSeq;
            p.tr().steps = 2;
            p.step (0).mode = StepMode::high;
            p.step (1).mode = StepMode::low;
            p.tr().smooth = smooth;
        }), period + 0.05);
    };
    CHECK (jumps (0.0f) < 0.02);
    CHECK (jumps (0.5f) > 0.5);   // 125 ms to settle: about 2/3 of the way still to go at 50 ms
}

TEST_CASE ("TRAVEL: VOICE, FIRST NOTE and LAST NOTE", "[travel]")
{
    // Note A from the start, note B from a quarter period. Where A is at 3/8 of a period.
    const auto aAt = [] (synth::travel::Trigger trigger)
    {
        TravelPlayer p;
        p.tr().trigger = trigger;
        p.on (57);
        auto out = p.render (0.125)[0];
        p.on (64);   // E4: its harmonics clear of A3's
        const auto rest = p.render (0.4)[0];
        out.insert (out.end(), rest.begin(), rest.end());
        return std::pair (placeAt (out, 0.25, 57), placeAt (out, 0.25, 64));
    };
    const auto [voiceA, voiceB] = aAt (synth::travel::Trigger::voice);
    CHECK_THAT (voiceA, WithinAbs (0.5, 0.04));    // its own: 1/2 through
    CHECK_THAT (voiceB, WithinAbs (0.25, 0.04));   // its own: 1/4 through
    const auto [firstA, firstB] = aAt (synth::travel::Trigger::firstNote);
    CHECK_THAT (firstA, WithinAbs (0.5, 0.04));
    CHECK_THAT (firstB, WithinAbs (0.5, 0.04));    // B follows A's
    const auto [lastA, lastB] = aAt (synth::travel::Trigger::lastNote);
    CHECK_THAT (lastA, WithinAbs (0.25, 0.04));    // B started it over, for A too
    CHECK_THAT (lastB, WithinAbs (0.25, 0.04));
}

TEST_CASE ("TRAVEL in the plugin: settings reach the engine, it moves from a fresh start, and its editors' data is kept", "[travel][processor]")
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
    set (ParamIDs::osc (2, "TrOn"), 1.0f);
    set (ParamIDs::osc (2, "TrEditors"), (float) synth::travel::Editors::envSeqMod);
    set (ParamIDs::osc (2, "TrSteps"), 8.0f);
    set (ParamIDs::osc (2, "TrStrength"), -0.5f);
    set (ParamIDs::osc (2, "TrSmooth"), 0.3f);
    set (ParamIDs::osc (2, "TrTrigger"), (float) synth::travel::Trigger::lastNote);
    {
        const auto s = processor.getSynthSettings();
        const auto& t = s.oscillators[2].travel;
        CHECK (t.on);
        CHECK (t.editors == synth::travel::Editors::envSeqMod);
        CHECK (t.steps == 8);
        CHECK_THAT (t.strength, WithinAbs (-0.5, 1.0e-3));
        CHECK_THAT (t.smooth, WithinAbs (0.3, 1.0e-3));
        CHECK (t.trigger == synth::travel::Trigger::lastNote);
        CHECK_FALSE (s.oscillators[1].travel.on);
    }

    // A fresh plugin: oscillator 1 on a factory wavetable with TRAVEL on moves through it (its sound changes).
    set (ParamIDs::osc (0, "Wave"), (float) synth::OscWave::wavetable);
    set (ParamIDs::osc (0, "Table"), 1.0f);
    set (ParamIDs::osc (0, "TrOn"), 1.0f);
    processor.finishTableBuilds();
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
    std::vector<float> out;
    for (int block = 0; block < 100; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        midi.clear();
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    // Each of the first 8 harmonics' level early on (20-40 ms) and later (240-260 ms): the frame has moved, so they've changed.
    double change = 0.0;
    for (int h = 1; h <= 8; ++h)
        change = std::max (change, std::abs (db (levelAt (out, 220.0 * h, 960, 960)) - db (levelAt (out, 220.0 * h, 11520, 960))));
    CHECK (change > 3.0);

    // Presets, projects and undo keep its envelope, steps and strengths.
    auto matrix = processor.getModMatrix();
    auto& t = matrix.travel[2];
    t.envelope.points.push_back ({ 1.0f, 0.3f, 0.2f });
    t.envelope.points[1].x = 0.5f;
    t.pattern.steps[3] = { StepMode::trigger, 4.0f, 2 };
    t.pattern.steps[5] = { StepMode::high, 1.0f, 1 };
    t.pattern.strength[1] = { 0.2f, 0.9f, 0.4f, true, -0.5f };
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    const auto rig = Rig::fromXml (*processor.captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->modulation.travel == matrix.travel);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (reopened.getModMatrix().travel == matrix.travel);
    }
    processor.flushUndoStep();
    auto edited = processor.getModMatrix();
    edited.travel[2] = {};
    processor.setModMatrix (edited);
    processor.modMatrixEditFinished();
    processor.flushUndoStep();
    processor.undo();
    CHECK (processor.getModMatrix().travel == matrix.travel);
}

TEST_CASE ("CPU: a chord of two wavetables, each travelling with its pattern", "[travel][performance]")
{
    const auto once = [] (bool travel)
    {
        TravelPlayer p;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.wave = synth::OscWave::wavetable;
            o.table = p.table.get();
            o.unisonVoices = 5;
            o.unisonMix = 0.7f;
            o.travel.on = travel;
            o.travel.editors = synth::travel::Editors::envSeqMod;
            o.travel.steps = 8;
            o.travel.smooth = 0.2f;
            o.travel.pattern = &p.pattern;
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (2.0);
        return (juce::Time::getMillisecondCounterHiRes() - started) / 2000.0 * 100.0;
    };
    const auto plain = juce::jmin (once (false), once (false), once (false));
    const auto travelling = juce::jmin (once (true), once (true), once (true));
    WARN ("2 wavetables x 5 copies x 8 keys: " << plain << "% of one core; travelling: " << travelling << "% (the fastest of three)");
    CHECK (travelling < plain + 1.5);
}

TEST_CASE ("The TRAVEL page: laid out cleanly; dragging and clicking edit the steps and strengths", "[travel][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto& panel = view->getOscillatorPanel();
    panel.selectOscillator (1);
    panel.showPage (OscillatorPanel::Page::travel);
    CHECK (panel.getTravelEnvelope().getKind() == MultiPointEditor::Kind::travel);
    CHECK (panel.getTravelEnvelope().getIndex() == 1);
    CHECK (panel.getTravelEnvelope().getShape() == TravelData::defaultEnvelope());

    std::vector<juce::Component*> shown;
    for (auto* child : panel.getChildren())
        if (child->isVisible())
            shown.push_back (child);
    CHECK (shown.size() > 20);
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

    auto& pattern = panel.getTravelPattern();
    CHECK (pattern.getNumSteps() == 4);
    const auto event = [&pattern] (juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &pattern, &pattern, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    };
    const auto travel = [&processor] { return processor.getModMatrix().travel[1].pattern; };

    // Drag step 2 up 30 pixels: three more iterations.
    const auto step = pattern.stepArea (1).getCentre();
    pattern.mouseDown (event (step));
    pattern.mouseDrag (event (step.translated (0.0f, -30.0f)));
    pattern.mouseUp (event (step.translated (0.0f, -30.0f)));
    CHECK_THAT (travel().steps[1].iterations, WithinAbs (4.0, 1.0e-6));
    // ...down past 1: half.
    pattern.mouseDown (event (step));
    pattern.mouseDrag (event (step.translated (0.0f, 60.0f)));
    pattern.mouseUp (event (step.translated (0.0f, 60.0f)));
    CHECK_THAT (travel().steps[1].iterations, WithinAbs (0.5, 1.0e-6));

    // The menu's choices, and double-clicking for two periods long.
    pattern.setStepMode (2, StepMode::reverse);
    CHECK (travel().steps[2].mode == StepMode::reverse);
    pattern.mouseDoubleClick (event (pattern.stepArea (3).getCentre()));
    CHECK (travel().steps[3].length == 2);
    CHECK (pattern.stepArea (3).getWidth() > 1.9f * pattern.stepArea (0).getWidth());

    // Strength: drag the first step's start down to the bottom; double-click adds a middle point.
    const auto box = pattern.strengthArea (0);
    const juce::Point<float> start (box.getX() + 3.0f, box.getCentreY());
    pattern.mouseDown (event (start));
    pattern.mouseDrag (event ({ start.x, box.getBottom() }));
    pattern.mouseUp (event ({ start.x, box.getBottom() }));
    CHECK_THAT (travel().strength[0].start, WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (travel().strength[0].end, WithinAbs (1.0, 1.0e-6));
    pattern.mouseDoubleClick (event (box.getCentre()));
    CHECK (travel().strength[0].hasMiddle);

    // Each edit is an undo step.
    processor.flushUndoStep();
    processor.undo();
    CHECK_FALSE (travel().strength[0].hasMiddle);
}
