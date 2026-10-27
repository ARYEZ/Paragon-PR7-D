// MODULATION (stage 4a): the mod matrix, LFOs, macros and controllers, measured
// by what they do to the sound (pitch, level, brightness).
#include "SynthTestUtils.h"

#include "ModDestinations.h"
#include "PluginProcessor.h"
#include "dsp/synth/Modulation.h"

using Catch::Matchers::WithinRel;

namespace
{
    using namespace synth::mod;

    // The ranges of the knobs these tests move (as the parameters have them).
    const juce::NormalisableRange<float> fineRange { -100.0f, 100.0f, 1.0f };
    const juce::NormalisableRange<float> volumeRange { 0.0f, 2.0f };

    // A player with routes.
    struct ModPlayer : Player
    {
        std::vector<Route> routes;

        Route& route (Source source, Field field, const juce::NormalisableRange<float>& range, float value, float amount, int index = 0)
        {
            Route r;
            r.source = source;
            r.target = { field, index };
            r.range = &range;
            r.base = range.convertTo0to1 (value);
            r.amount = amount;
            routes.push_back (r);
            settings.modulation.routes = routes.data();
            settings.modulation.numRoutes = (int) routes.size();
            return routes.back();
        }
        LfoSettings& lfo (int k = 0) { return settings.modulation.lfos[(size_t) k]; }
    };

    // A sine at A4 (440 Hz) with LFO 1 on its FINE by +-100 cents.
    ModPlayer pitchPlayer()
    {
        ModPlayer p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.envelope.sustain = 1.0f;
        p.route (Source::lfo1, Field::oscFine, fineRange, 0.0f, 0.5f);   // +-50% of the travel: +-100 cents
        return p;
    }

    // Its pitch in cents from A4 over [from, to) seconds.
    double centsBetween (const std::vector<float>& out, double from, double to)
    {
        const auto hz = frequencyOf (out, (size_t) (from * sampleRate), (size_t) (to * sampleRate));
        return 1200.0 * std::log2 (hz / 440.0);
    }

    double rmsBetween (const std::vector<float>& out, double from, double to)
    {
        return rms (out, (size_t) (from * sampleRate), (size_t) (to * sampleRate));
    }
}

TEST_CASE ("MOD: CURVE bends the source, + adds, % takes a share of the travel left", "[mod]")
{
    CHECK_THAT (bend (0.5f, 0.0f), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (bend (0.5f, 1.0f), WithinAbs (0.0625, 1.0e-6));    // slow first: 0.5^4
    CHECK_THAT (bend (0.5f, -1.0f), WithinAbs (0.8409, 1.0e-4));   // fast first: 0.5^0.25
    CHECK_THAT (bend (-0.5f, 1.0f), WithinAbs (-0.0625, 1.0e-6));  // the sign kept
    CHECK_THAT (combine (0.5f, 0.5f, false), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (combine (0.5f, 0.8f, false), WithinAbs (1.0, 1.0e-6));    // stops at the end
    CHECK_THAT (combine (0.2f, 1.0f, true), WithinAbs (1.0, 1.0e-6));     // %: always reaches the end at 100%
    CHECK_THAT (combine (0.2f, 0.5f, true), WithinAbs (0.6, 1.0e-6));     // half the travel left
    CHECK_THAT (combine (0.2f, -0.5f, true), WithinAbs (0.1, 1.0e-6));    // half the travel below
}

TEST_CASE ("MOD LFO waves: each shape where it should be", "[mod]")
{
    CHECK_THAT (lfoWave (LfoWave::sine, 0.25f, 0, 0), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::triangle, 0.25f, 0, 0), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::triangle, 0.75f, 0, 0), WithinAbs (-1.0, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::sawUp, 0.0f, 0, 0), WithinAbs (-1.0, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::sawDown, 0.0f, 0, 0), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::square, 0.4f, 0, 0), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::square, 0.6f, 0, 0), WithinAbs (-1.0, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::sampleHold, 0.3f, 0.7f, -0.2f), WithinAbs (0.7, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::smoothRandom, 0.0f, 0.7f, -0.2f), WithinAbs (-0.2, 1.0e-6));
    CHECK_THAT (lfoWave (LfoWave::smoothRandom, 1.0f, 0.7f, -0.2f), WithinAbs (0.7, 1.0e-6));
}

TEST_CASE ("MOD LFO on pitch: a 2 Hz square moves the note up and down 100 cents, at the right times", "[mod]")
{
    auto p = pitchPlayer();
    p.lfo().wave = LfoWave::square;
    p.lfo().rateHz = 2.0f;
    p.on (69);
    const auto out = p.render (1.0)[0];
    CHECK_THAT (centsBetween (out, 0.02, 0.24), WithinAbs (100.0, 1.0));    // the first half of each cycle up
    CHECK_THAT (centsBetween (out, 0.27, 0.49), WithinAbs (-100.0, 1.0));   // the second down
    CHECK_THAT (centsBetween (out, 0.52, 0.74), WithinAbs (100.0, 1.0));

    // Synced: a quarter note at 120 BPM is the same 2 Hz.
    auto synced = pitchPlayer();
    synced.lfo().wave = LfoWave::square;
    synced.lfo().sync = LfoSync::straight;
    synced.lfo().division = 5;   // 1/4
    synced.settings.modulation.bpm = 120.0;
    synced.on (69);
    const auto syncedOut = synced.render (1.0)[0];
    CHECK_THAT (centsBetween (syncedOut, 0.27, 0.49), WithinAbs (-100.0, 1.0));
    CHECK_THAT (centsBetween (syncedOut, 0.52, 0.74), WithinAbs (100.0, 1.0));
    // ...and dotted, at 90 BPM: 1.5 beats of 0.667 s: a cycle each second.
    synced.lfo().sync = LfoSync::dotted;
    synced.settings.modulation.bpm = 90.0;
    CHECK_THAT (synced.lfo().cyclesPerSecond (90.0), WithinAbs (1.0, 1.0e-9));
}

TEST_CASE ("MOD LFO PHASE, OFFSET, DELAY, FADE IN and ONE SHOT", "[mod]")
{
    // PHASE 50%: a square starts in its low half.
    {
        auto p = pitchPlayer();
        p.lfo().wave = LfoWave::square;
        p.lfo().rateHz = 2.0f;
        p.lfo().phase = 0.5f;
        p.on (69);
        const auto out = p.render (0.5)[0];
        CHECK_THAT (centsBetween (out, 0.02, 0.24), WithinAbs (-100.0, 1.0));
    }
    // OFFSET +50%: the wave moved up and fitted in: its top stays +100 cents, its bottom is lifted to -33.
    {
        auto p = pitchPlayer();
        p.lfo().wave = LfoWave::square;
        p.lfo().rateHz = 2.0f;
        p.lfo().offset = 0.5f;
        p.on (69);
        const auto out = p.render (0.5)[0];
        CHECK_THAT (centsBetween (out, 0.02, 0.24), WithinAbs (100.0, 1.0));
        CHECK_THAT (centsBetween (out, 0.27, 0.49), WithinAbs (-100.0 / 3.0, 1.0));
    }
    // DELAY 300 ms: nothing until then.
    {
        auto p = pitchPlayer();
        p.lfo().wave = LfoWave::square;
        p.lfo().rateHz = 2.0f;
        p.lfo().delayMs = 300.0f;
        p.on (69);
        const auto out = p.render (0.6)[0];
        CHECK_THAT (centsBetween (out, 0.02, 0.28), WithinAbs (0.0, 0.5));
        CHECK_THAT (centsBetween (out, 0.32, 0.54), WithinAbs (100.0, 1.0));
    }
    // FADE IN 1 s: a quarter of the depth after a quarter of a second.
    {
        auto p = pitchPlayer();
        p.lfo().wave = LfoWave::square;
        p.lfo().rateHz = 0.5f;    // high for the first second
        p.lfo().fadeInMs = 1000.0f;
        p.on (69);
        const auto out = p.render (0.9)[0];
        CHECK_THAT (centsBetween (out, 0.24, 0.26), WithinAbs (25.0, 2.5));
        CHECK_THAT (centsBetween (out, 0.74, 0.76), WithinAbs (75.0, 2.5));
    }
    // OFFSET +100%: only ever up, 0 to +100 cents.
    {
        auto p = pitchPlayer();
        p.lfo().wave = LfoWave::square;
        p.lfo().rateHz = 2.0f;
        p.lfo().offset = 1.0f;
        p.on (69);
        const auto out = p.render (0.5)[0];
        CHECK_THAT (centsBetween (out, 0.02, 0.24), WithinAbs (100.0, 1.0));
        CHECK_THAT (centsBetween (out, 0.27, 0.49), WithinAbs (0.0, 0.5));
    }
    // ONE SHOT: a saw rises once and stays at the top.
    {
        auto p = pitchPlayer();
        p.lfo().wave = LfoWave::sawUp;
        p.lfo().rateHz = 4.0f;
        p.lfo().oneShot = true;
        p.on (69);
        const auto out = p.render (1.0)[0];
        CHECK_THAT (centsBetween (out, 0.4, 0.95), WithinAbs (100.0, 1.0));
    }
}

TEST_CASE ("MOD LFO TRIGGER: VOICE gives each note its own, GLOBAL and FIRST NOTE one for all, LAST NOTE restarts", "[mod]")
{
    // A 2 Hz square on the AMP's volume (% by -100%: the top half silent, the
    // bottom half full); a second note 125 ms after the first.
    const auto levelAfterSecond = [] (LfoTrigger trigger)
    {
        ModPlayer p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.envelope.sustain = 1.0f;
        p.settings.amp.envelope.attackMs = 0.0f;
        p.lfo().wave = LfoWave::square;
        p.lfo().rateHz = 2.0f;
        p.lfo().trigger = trigger;
        p.route (Source::lfo1, Field::ampVolume, volumeRange, 1.0f, -1.0f).percent = true;
        p.on (57);
        p.render (0.125);
        p.on (69);
        const auto out = p.render (0.5)[0];
        // From 260 ms to 360 ms after the first note (135-235 ms into this render).
        return rmsBetween (out, 0.135, 0.235);
    };
    const auto oneNote = [] ()
    {
        ModPlayer p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.envelope.sustain = 1.0f;
        p.on (69);
        const auto out = p.render (0.3)[0];
        return rmsBetween (out, 0.1, 0.2);
    }();
    // VOICE: the first note in its loud half (it started 250 ms before), the second in its silent half.
    CHECK_THAT (levelAfterSecond (LfoTrigger::voice), WithinRel (oneNote * 2.0, 0.05));
    // FIRST NOTE: one LFO, started by the first note: both in the loud half.
    CHECK_THAT (levelAfterSecond (LfoTrigger::firstNote), WithinRel (std::sqrt (2.0 * std::pow (oneNote * 2.0, 2.0)), 0.05));
    // LAST NOTE: restarted by the second note, 260 ms after the first is 135 ms after the restart: both silent.
    CHECK (levelAfterSecond (LfoTrigger::lastNote) < 1.0e-3);
}

TEST_CASE ("MOD sources: velocity, the mod wheel, aftertouch, keytrack, macros; routes to one knob add up", "[mod]")
{
    const auto cents = [] (const std::function<void (ModPlayer&)>& setup, int note = 69, float velocity = 1.0f)
    {
        ModPlayer p;
        p.osc().wave = synth::OscWave::sine;
        p.settings.amp.envelope.sustain = 1.0f;
        setup (p);
        p.on (note, velocity);
        const auto out = p.render (0.3)[0];
        const auto hz = frequencyOf (out, 2400, 14000);
        return 1200.0 * std::log2 (hz / noteHz (note));
    };
    // VELOCITY: half velocity, half the push.
    CHECK_THAT (cents ([] (ModPlayer& p) { p.route (Source::velocity, Field::oscFine, fineRange, 0.0f, 0.5f); }, 69, 0.5f), WithinAbs (50.0, 1.0));
    // The MOD WHEEL, from MIDI controller 1.
    CHECK_THAT (cents ([] (ModPlayer& p)
    {
        p.route (Source::modWheel, Field::oscFine, fineRange, 0.0f, 0.5f);
        p.midi (juce::MidiMessage::controllerEvent (1, 1, 127));
        p.render (0.01);
    }), WithinAbs (100.0, 1.0));
    // AFTERTOUCH (channel pressure).
    CHECK_THAT (cents ([] (ModPlayer& p)
    {
        p.route (Source::aftertouch, Field::oscFine, fineRange, 0.0f, -0.5f);
        p.midi (juce::MidiMessage::channelPressureChange (1, 127));
        p.render (0.01);
    }), WithinAbs (-100.0, 1.0));
    // KEYTRACK: note / 127.
    CHECK_THAT (cents ([] (ModPlayer& p) { p.route (Source::keytrack, Field::oscFine, fineRange, 0.0f, 0.5f); }, 69), WithinAbs (100.0 * 69.0 / 127.0, 1.0));
    // MACROS, and two routes to one knob adding up.
    CHECK_THAT (cents ([] (ModPlayer& p)
    {
        p.settings.modulation.macros = { 1.0f, 0.5f, 0.0f };
        p.route (Source::macro1, Field::oscFine, fineRange, 0.0f, 0.1f);   // +20 cents
        p.route (Source::macro2, Field::oscFine, fineRange, 0.0f, 0.2f);   // +20 cents
    }), WithinAbs (40.0, 1.0));
    // BUTTONS: off, nothing; on, the whole amount.
    CHECK_THAT (cents ([] (ModPlayer& p) { p.route (Source::button1, Field::oscFine, fineRange, 0.0f, 0.3f); }), WithinAbs (0.0, 0.5));
    CHECK_THAT (cents ([] (ModPlayer& p)
    {
        p.settings.modulation.buttons = { true, false };
        p.route (Source::button1, Field::oscFine, fineRange, 0.0f, 0.3f);
    }), WithinAbs (60.0, 1.0));
}

TEST_CASE ("MOD LAG: a stepped source glides instead of jumping", "[mod]")
{
    const auto centsAfterStep = [] (float lagMs)
    {
        auto p = pitchPlayer();
        p.routes.back().lagMs = lagMs;
        p.settings.modulation.routes = p.routes.data();
        p.lfo().wave = LfoWave::square;
        p.lfo().rateHz = 2.0f;
        p.on (69);
        const auto out = p.render (0.5)[0];
        return centsBetween (out, 0.255, 0.275);   // just after the square falls at 250 ms
    };
    CHECK_THAT (centsAfterStep (0.0f), WithinAbs (-100.0, 1.0));
    const auto lagged = centsAfterStep (100.0f);
    CHECK (lagged > 0.0);       // after 15 ms of a 100 ms lag, still most of the way up
    CHECK (lagged < 90.0);      // but on its way
}

TEST_CASE ("MOD per note: velocity on the filter makes softer notes darker", "[mod]")
{
    const juce::NormalisableRange<float> cutoffRange = []
    {
        juce::NormalisableRange<float> r { 20.0f, 20000.0f };
        r.setSkewForCentre (632.0f);
        return r;
    }();
    const auto brightness = [&] (float velocity)
    {
        ModPlayer p;
        p.settings.amp.velocity = 0.0f;   // the same level: only the filter hears velocity
        p.settings.amp.envelope.sustain = 1.0f;
        p.settings.filter.on = true;
        p.settings.filter.cutoffHz = 300.0f;
        p.route (Source::velocity, Field::filterCutoff, cutoffRange, 300.0f, 0.6f);
        p.on (45, velocity);
        const auto out = p.render (0.4)[0];
        return levelAt (out, 110.0 * 30.0, 9600, 9600) / levelAt (out, 110.0, 9600, 9600);   // the 30th harmonic (3.3 kHz) against the note
    };
    // Full velocity: the cutoff at the top (20 kHz). At 0.3: about 1.7 kHz, an octave under that harmonic.
    CHECK (db (brightness (1.0f)) > db (brightness (0.3f)) + 15.0);
}

TEST_CASE ("MOD in the plugin: routes play, and presets, projects and undo keep the matrix", "[mod][processor]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    CHECK (processor.getNumPlayingRoutes() == 0);

    ModMatrix matrix;
    matrix.routes.push_back ({ true, "macro1", ParamIDs::osc (0, "Volume"), -1.0f, true, 0.0f, 0.0f });   // MACRO 1 up: silence
    matrix.routes.push_back ({ true, "lfo2", "no such knob", 0.5f, false, 0.0f, 0.0f });                 // kept, does nothing
    matrix.routes.push_back ({ false, "lfo1", ParamIDs::fltCutoff, 0.5f, false, 0.0f, 0.0f });          // off
    matrix.macroNames[0] = "FADE";
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    CHECK (processor.getNumPlayingRoutes() == 1);
    CHECK (processor.isRigModified());

    const auto play = [&processor] (float macro)
    {
        auto* parameter = processor.getParameters().getParameter (ParamIDs::macros[0]);
        parameter->setValueNotifyingHost (macro);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
        double sum = 0.0;
        for (int block = 0; block < 40; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            midi.clear();
            if (block >= 20)
                sum += buffer.getRMSLevel (0, 0, blockSize);
        }
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        processor.processBlock (buffer, midi);
        return sum;
    };
    CHECK (play (0.0f) > 0.01);
    CHECK (play (1.0f) < 1.0e-4);

    // Presets and projects keep it (the unknown route too).
    const auto rig = Rig::fromXml (*processor.captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->modulation == processor.getModMatrix());
    CHECK (rig->modulation.macroName (0) == "FADE");
    CHECK (rig->modulation.macroName (1) == "MACRO 2");
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (reopened.getModMatrix() == processor.getModMatrix());
        CHECK (reopened.getNumPlayingRoutes() == 1);
    }

    // Undo takes an edit back.
    processor.flushUndoStep();
    auto edited = processor.getModMatrix();
    edited.routes.clear();
    processor.setModMatrix (edited);
    processor.modMatrixEditFinished();
    processor.flushUndoStep();
    CHECK (processor.getNumPlayingRoutes() == 0);
    processor.undo();
    CHECK (processor.getModMatrix() == matrix);
    CHECK (processor.getNumPlayingRoutes() == 1);

    // Every destination is a real parameter with a continuous range.
    for (const auto& d : ModDestinations::all())
    {
        INFO (d.parameterId);
        auto* parameter = dynamic_cast<juce::AudioParameterFloat*> (processor.getParameters().getParameter (d.parameterId));
        CHECK (parameter != nullptr);
    }
}

TEST_CASE ("CPU: a supersaw chord with six routes per note", "[mod][performance]")
{
    const juce::NormalisableRange<float> unit { 0.0f, 1.0f };
    const auto percent = [&] (bool modulated)
    {
        ModPlayer p;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
            o.unisonDetune = 0.4f;
        }
        p.settings.filter.on = true;
        if (modulated)
        {
            p.lfo (0).rateHz = 3.0f;
            p.lfo (1).trigger = LfoTrigger::global;
            p.route (Source::lfo1, Field::oscFine, fineRange, 0.0f, 0.05f);
            p.route (Source::lfo2, Field::oscUnisonDetune, unit, 0.4f, 0.2f, 1);
            p.route (Source::lfo1, Field::filterResonance, unit, 0.2f, 0.3f);
            p.route (Source::velocity, Field::ampVolume, volumeRange, 1.0f, 0.2f);
            p.route (Source::modWheel, Field::oscVolume, volumeRange, 1.0f, -0.2f);
            p.route (Source::random, Field::ampPan, unit, 0.5f, 0.3f);
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (5.0);
        const auto cost = (juce::Time::getMillisecondCounterHiRes() - started) / 5000.0 * 100.0;
        WARN ((modulated ? "six routes per note" : "no routes") << ": 2 saws x 7 copies x 8 keys through the filter, " << cost << "% of one core");
        return cost;
    };
    const auto plain = percent (false);
    const auto modulated = percent (true);
    CHECK (modulated < plain + 5.0);
}

