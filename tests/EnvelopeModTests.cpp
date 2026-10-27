// MODULATION ENVELOPES and drawn LFO shapes (stage 4b): the shapes, every
// mode, and what they do to a note, measured.
#include "SynthTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/synth/Modulation.h"
#include "ui/MainView.h"

using Catch::Matchers::WithinRel;

namespace
{
    using namespace synth::mod;
    using P = ShapePoint;

    Shape shapeOf (std::initializer_list<P> points, int sustain = -1, int loopStart = -1, int loopEnd = -1)
    {
        Shape s;
        for (const auto& p : points)
            s.points[(size_t) s.count++] = p;
        s.sustain = sustain;
        s.loopStart = loopStart;
        s.loopEnd = loopEnd;
        return s;
    }

    // Runs an envelope `seconds` in steps of 10 ms; its value at the end.
    float runFor (EnvelopeState& state, const Shape& shape, const EnvelopeSettings& s, double seconds, double bpm = 120.0)
    {
        float value = 0.0f;
        for (double t = 0.0; t < seconds - 1.0e-9; t += 0.01)
            value = state.next (shape, s, 0.01, bpm);
        return state.next (shape, s, 0.0, bpm);   // where it is now
    }

    const juce::NormalisableRange<float> fineRange { -100.0f, 100.0f, 1.0f };
    const juce::NormalisableRange<float> volumeRange { 0.0f, 2.0f };

    struct EnvPlayer : Player
    {
        std::vector<Route> routes;
        std::array<Shape, numEnvelopes> envelopes {};
        std::array<Shape, numLfoShapes> lfoShapes {};

        EnvPlayer()
        {
            osc().wave = synth::OscWave::sine;
            settings.amp.envelope.sustain = 1.0f;
            settings.amp.envelope.attackMs = 0.0f;
            settings.modulation.envelopeShapes = envelopes.data();
            settings.modulation.lfoShapes = lfoShapes.data();
        }
        Route& route (Source source, Field field, const juce::NormalisableRange<float>& range, float value, float amount)
        {
            Route r;
            r.source = source;
            r.target = { field, 0 };
            r.range = &range;
            r.base = range.convertTo0to1 (value);
            r.amount = amount;
            routes.push_back (r);
            settings.modulation.routes = routes.data();
            settings.modulation.numRoutes = (int) routes.size();
            return routes.back();
        }
    };

    double centsBetween (const std::vector<float>& out, double from, double to, double note = 69.0)
    {
        const auto hz = frequencyOf (out, (size_t) (from * sampleRate), (size_t) (to * sampleRate));
        return 1200.0 * std::log2 (hz / noteHz (note));
    }
}

TEST_CASE ("ENV shapes: straight and curved lines between points, held before the first and after the last", "[env]")
{
    // A point's curve shapes the line after it.
    const auto shape = shapeOf ({ P { 0.0f, 0.0f, -1.0f }, P { 1.0f, 1.0f, 0.0f }, P { 2.0f, 0.5f, 0.0f } });
    CHECK_THAT (shape.valueAt (-1.0), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (shape.valueAt (1.5), WithinAbs (0.75, 1.0e-6));                                  // straight
    CHECK_THAT (shape.valueAt (0.5), WithinAbs (synth::bend (0.5f, -1.0f), 1.0e-6));             // curved: fast first
    CHECK (shape.valueAt (0.5) > 0.9);
    CHECK_THAT (shape.valueAt (5.0), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (shape.length(), WithinAbs (2.0, 1.0e-6));
}

TEST_CASE ("ENV modes: one shot, sustain, sustain jump, loop, ping pong, and their releases", "[env]")
{
    // Up to 1 over a second, down to 0.5 at 2 s (SUSTAIN), to 0 at 3 s; loop between points 1 and 2.
    const auto shape = shapeOf ({ P { 0.0f, 0.0f, 0.0f }, P { 1.0f, 1.0f, 0.0f }, P { 2.0f, 0.5f, 0.0f }, P { 3.0f, 0.0f, 0.0f } }, 2, 1, 2);
    EnvelopeSettings s;

    SECTION ("ONE SHOT runs to the end and holds there")
    {
        EnvelopeState e;
        e.start();
        CHECK_THAT (runFor (e, shape, s, 0.5), WithinAbs (0.5, 0.01));
        CHECK_THAT (runFor (e, shape, s, 1.5), WithinAbs (0.5, 0.01));     // at 2 s
        CHECK_THAT (runFor (e, shape, s, 5.0), WithinAbs (0.0, 1.0e-6));
    }
    SECTION ("SUSTAIN holds at its point while the key is down, then runs on")
    {
        s.mode = EnvelopeMode::sustain;
        EnvelopeState e;
        e.start();
        CHECK_THAT (runFor (e, shape, s, 5.0), WithinAbs (0.5, 1.0e-6));   // held at S
        e.release();
        CHECK_THAT (runFor (e, shape, s, 0.5), WithinAbs (0.25, 0.01));    // half way down the last line
        // Let go before S: it jumps to S.
        EnvelopeState early;
        early.start();
        runFor (early, shape, s, 0.5);
        early.release();
        CHECK_THAT (runFor (early, shape, s, 0.0), WithinAbs (0.5, 0.01));
    }
    SECTION ("SUSTAIN JUMP lets go from where the level matches, so nothing jumps")
    {
        s.mode = EnvelopeMode::sustainJump;
        EnvelopeState e;
        e.start();
        runFor (e, shape, s, 0.3);        // 0.3 on the way up
        e.release();
        CHECK_THAT (runFor (e, shape, s, 0.0), WithinAbs (0.3, 0.01));    // the same level, after S (at 2.4 s)
        CHECK_THAT (runFor (e, shape, s, 0.2), WithinAbs (0.2, 0.01));    // and on down from there (2.6 s)
    }
    SECTION ("LOOP goes round L1 to L2 for ever; LOOP+REL then runs to the end")
    {
        s.mode = EnvelopeMode::loop;
        EnvelopeState e;
        e.start();
        CHECK_THAT (runFor (e, shape, s, 1.5), WithinAbs (0.75, 0.01));   // into the loop
        CHECK_THAT (runFor (e, shape, s, 1.0), WithinAbs (0.75, 0.01));   // once round: the same place
        e.release();
        CHECK_THAT (runFor (e, shape, s, 1.0), WithinAbs (0.75, 0.01));   // LOOP ignores the release

        s.mode = EnvelopeMode::loopRelease;
        EnvelopeState r;
        r.start();
        runFor (r, shape, s, 1.5);
        r.release();
        CHECK_THAT (runFor (r, shape, s, 0.5), WithinAbs (0.25, 0.01));   // from L2 down the last line
    }
    SECTION ("PING PONG bounces between L1 and L2")
    {
        s.mode = EnvelopeMode::pingPong;
        EnvelopeState e;
        e.start();
        CHECK_THAT (runFor (e, shape, s, 1.8), WithinAbs (0.6, 0.01));    // forwards: 1.8 s
        CHECK_THAT (runFor (e, shape, s, 0.4), WithinAbs (0.6, 0.01));    // to L2 at 2 s and back to 1.8
        CHECK_THAT (runFor (e, shape, s, 0.6), WithinAbs (0.9, 0.01));    // back to 1.2
    }
    SECTION ("SPEED, SYNC, STEPS, FLIP and OFFSET")
    {
        EnvelopeState e;
        s.speed = 2.0f;
        e.start();
        CHECK_THAT (runFor (e, shape, s, 0.25), WithinAbs (0.5, 0.01));   // twice as fast
        s.speed = 1.0f;
        s.sync = true;                                                   // beats at 120 BPM: 0.5 s each
        e.start();
        CHECK_THAT (runFor (e, shape, s, 0.25, 120.0), WithinAbs (0.5, 0.01));
        s.sync = false;
        s.steps = 4;
        CHECK_THAT (s.output (0.3f), WithinAbs (0.25, 1.0e-6));
        s.steps = 0;
        s.flip = true;
        CHECK_THAT (s.output (0.3f), WithinAbs (0.7, 1.0e-6));
        s.flip = false;
        s.offset = 0.5f;
        CHECK_THAT (s.output (0.0f), WithinAbs (-1.0, 1.0e-6));
        CHECK_THAT (s.output (1.0f), WithinAbs (1.0, 1.0e-6));
        CHECK_THAT (s.output (0.75f), WithinAbs (0.5, 1.0e-6));
    }
}

TEST_CASE ("ENV in a note: a one-second rise moves the pitch up 100 cents, as drawn", "[env]")
{
    EnvPlayer p;
    p.envelopes[0] = shapeOf ({ P { 0.0f, 0.0f, 0.0f }, P { 1.0f, 1.0f, 0.0f } });
    p.route (Source::env1, Field::oscFine, fineRange, 0.0f, 0.5f);
    p.on (69);
    const auto out = p.render (1.5)[0];
    CHECK_THAT (centsBetween (out, 0.24, 0.26), WithinAbs (25.0, 1.5));
    CHECK_THAT (centsBetween (out, 0.49, 0.51), WithinAbs (50.0, 1.5));
    CHECK_THAT (centsBetween (out, 1.1, 1.4), WithinAbs (100.0, 1.0));
}

TEST_CASE ("ENV TRIGGER: VOICE gives each note its own, LAST NOTE restarts one for all", "[env]")
{
    // A one-second fall from full to nothing on the AMP's volume; a second note at 0.5 s.
    const auto levelAt60 = [] (EnvelopeTrigger trigger)
    {
        EnvPlayer p;
        p.envelopes[0] = shapeOf ({ P { 0.0f, 1.0f, 0.0f }, P { 1.0f, 0.0f, 0.0f } });
        p.settings.modulation.envelopes[0].trigger = trigger;
        p.route (Source::env1, Field::ampVolume, volumeRange, 0.0f, 0.5f);   // volume = the envelope's level
        p.on (57);
        p.render (0.5);
        p.on (69);
        const auto out = p.render (0.2)[0];
        return rms (out, 4320, 5280);   // 590-610 ms after the first note
    };
    const auto oneNote = []
    {
        Player p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.envelope.sustain = 1.0f;
        p.on (69);
        const auto out = p.render (0.3)[0];
        return rms (out, 4800, 9600);
    }();
    // VOICE: the first note at 0.4, the second at 0.9.
    CHECK_THAT (levelAt60 (EnvelopeTrigger::voice), WithinRel (oneNote * std::sqrt (0.4 * 0.4 + 0.9 * 0.9), 0.05));
    // LAST NOTE: both restarted at 0.5 s: both at 0.9.
    CHECK_THAT (levelAt60 (EnvelopeTrigger::lastNote), WithinRel (oneNote * 0.9 * std::sqrt (2.0), 0.05));
}

TEST_CASE ("Drawn LFO shapes: CUSTOM plays the shape drawn", "[env]")
{
    EnvPlayer p;
    // A square drawn: high the first half, low the second.
    p.lfoShapes[1] = shapeOf ({ P { 0.0f, 1.0f, 0.0f }, P { 0.5f, 1.0f, 0.0f }, P { 0.5001f, 0.0f, 0.0f }, P { 1.0f, 0.0f, 0.0f } });
    p.settings.modulation.lfos[0].wave = LfoWave::custom2;
    p.settings.modulation.lfos[0].rateHz = 2.0f;
    p.route (Source::lfo1, Field::oscFine, fineRange, 0.0f, 0.5f);
    p.on (69);
    const auto out = p.render (0.5)[0];
    CHECK_THAT (centsBetween (out, 0.02, 0.24), WithinAbs (100.0, 1.0));
    CHECK_THAT (centsBetween (out, 0.27, 0.49), WithinAbs (-100.0, 1.0));
}

TEST_CASE ("ENV in the plugin: settings reach the engine, shapes are kept with presets, projects and undo", "[env][processor]")
{
    ParagonX1AudioProcessor processor;
    const auto set = [&] (const juce::String& id, float value)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    set (ParamIDs::env (3, "Mode"), (float) EnvelopeMode::pingPong);
    set (ParamIDs::env (3, "Speed"), 2.5f);
    set (ParamIDs::env (3, "Steps"), 8.0f);
    const auto settings = processor.getSynthSettings();   // (a copy each time)
    const auto& e = settings.modulation.envelopes[3];
    CHECK (e.mode == EnvelopeMode::pingPong);
    CHECK_THAT (e.speed, WithinRel (2.5, 0.01));
    CHECK (e.steps == 8);

    auto matrix = processor.getModMatrix();
    matrix.envelopes[3].points.push_back ({ 2.0f, 0.3f, 0.5f });
    matrix.envelopes[3].sustain = -1;
    matrix.lfoShapes[2].points[1].y = 0.2f;
    matrix.routes.push_back ({ true, "env4", ParamIDs::fltCutoff, 0.5f, false, 0.0f, 0.0f });
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    CHECK (processor.getNumPlayingRoutes() == 1);

    const auto rig = Rig::fromXml (*processor.captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->modulation.envelopes[3].points.size() == 5);
    CHECK_THAT (rig->modulation.envelopes[3].points[4].curve, WithinAbs (0.5, 1.0e-3));
    CHECK (rig->modulation.envelopes[3].sustain == -1);
    CHECK_THAT (rig->modulation.lfoShapes[2].points[1].y, WithinAbs (0.2, 1.0e-3));
    CHECK (rig->modulation.envelopes[0] == ModShapeData::defaultEnvelope());   // untouched ones stay new
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (reopened.getModMatrix().envelopes[3].points.size() == 5);
        CHECK_THAT (reopened.getModMatrix().lfoShapes[2].points[1].y, WithinAbs (0.2, 1.0e-3));
    }
    processor.flushUndoStep();
    auto edited = processor.getModMatrix();
    edited.envelopes[3] = ModShapeData::defaultEnvelope();
    processor.setModMatrix (edited);
    processor.modMatrixEditFinished();
    processor.flushUndoStep();
    processor.undo();
    CHECK (processor.getModMatrix().envelopes[3].points.size() == 5);
}

TEST_CASE ("The multipoint editor: double-click adds and removes points, dragging moves them and curves lines", "[env][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::modulation);
    auto& panel = view->getModPage().getModulatorPanel();
    panel.selectEnvelope (1);
    auto& multi = panel.getEditor();
    REQUIRE (multi.isVisible());
    const auto event = [&] (juce::Point<float> p, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &multi, &multi, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), clicks, false);
    };
    const auto count = [&] { return processor.getModMatrix().envelopes[1].points.size(); };
    REQUIRE (count() == 4);

    // Double-click empty space between the first two points: a new point there.
    const auto between = (multi.pointPosition (0) + multi.pointPosition (1)) * 0.5f + juce::Point<float> (0.0f, -20.0f);
    multi.mouseDoubleClick (event (between, juce::ModifierKeys::leftButtonModifier, 2));
    REQUIRE (count() == 5);
    CHECK (processor.getModMatrix().envelopes[1].sustain == 3);   // the flags follow their points
    // Double-click it: gone.
    multi.mouseDoubleClick (event (multi.pointPosition (1), juce::ModifierKeys::leftButtonModifier, 2));
    CHECK (count() == 4);

    // Drag the last point to the right and down.
    const auto from = multi.pointPosition (3);
    multi.mouseDown (event (from));
    multi.mouseDrag (event (from + juce::Point<float> (30.0f, -40.0f)));
    multi.mouseUp (event (from + juce::Point<float> (30.0f, -40.0f)));
    const auto& last = processor.getModMatrix().envelopes[1].points[3];
    CHECK (last.x > 1.2f);
    CHECK (last.y > 0.0f);

    // Drag on a line: it curves.
    const auto lineMiddle = (multi.pointPosition (2) + multi.pointPosition (3)) * 0.5f;
    multi.mouseDown (event (lineMiddle));
    multi.mouseDrag (event (lineMiddle + juce::Point<float> (0.0f, 40.0f)));
    multi.mouseUp (event (lineMiddle + juce::Point<float> (0.0f, 40.0f)));
    CHECK (std::abs (processor.getModMatrix().envelopes[1].points[2].curve) > 0.1f);

    // The SHAPE tabs: the first and last points stay at the cycle's ends, and meet.
    panel.selectShape (0);
    const auto first = multi.pointPosition (0);
    multi.mouseDown (event (first));
    multi.mouseDrag (event (first + juce::Point<float> (40.0f, -30.0f)));
    multi.mouseUp (event (first + juce::Point<float> (40.0f, -30.0f)));
    const auto& shape = processor.getModMatrix().lfoShapes[0];
    CHECK (shape.points.front().x == 0.0f);
    CHECK_THAT (shape.points.back().y, WithinAbs (shape.points.front().y, 1.0e-6));
}
