// Stage 4f: the destinations added in 0.14.0 (SKEW, PINCH, the shapers' EQ,
// the PITCH module's RANGE and KEYTRACK, TRAVEL's STRENGTH and PHASE, the
// step sequencer's VOL MIX), the mod-wheel controller, MIDI learn, the
// knobs' modulation rings and drag-and-drop routing.
#include "SynthTestUtils.h"

#include "ModDestinations.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "rigs/Rig.h"
#include "ui/MainView.h"

namespace
{
    // How far apart a sound with a setting simply set to `to` and the same sound with it
    // left at `from` and a route (MACRO 1, at full) carrying it to `to` are: the difference's
    // level in dB below the first's. A destination the engine really follows comes out the same.
    double routedVersusSet (const std::function<void (Player&)>& setup, const std::function<void (synth::Settings&, float)>& apply,
                            synth::mod::Target target, juce::NormalisableRange<float> range, float from, float to, int note = 57)
    {
        Player set, routed;
        setup (set);
        setup (routed);
        apply (set.settings, to);
        apply (routed.settings, from);
        synth::mod::Route route;
        route.source = synth::mod::Source::macro1;
        route.target = target;
        route.range = &range;
        route.base = range.convertTo0to1 (from);
        route.amount = range.convertTo0to1 (to) - route.base;
        routed.settings.modulation.routes = &route;
        routed.settings.modulation.numRoutes = 1;
        routed.settings.modulation.macros[0] = 1.0f;
        routed.settings.modulation.usedSources = 1u << (int) synth::mod::Source::macro1;
        set.on (note);
        routed.on (note);
        const auto a = set.render (0.5)[0], b = routed.render (0.5)[0];
        double difference = 0.0, level = 0.0;
        for (size_t i = 2400; i < a.size(); ++i)
        {
            difference += (double) (a[i] - b[i]) * (a[i] - b[i]);
            level += (double) a[i] * a[i];
        }
        return 10.0 * std::log10 (juce::jmax (1.0e-30, difference) / juce::jmax (1.0e-30, level));
    }

    void sine (Player& p) { p.osc().wave = synth::OscWave::sine; p.settings.amp.envelope.sustain = 1.0f; p.settings.amp.envelope.attackMs = 0.0f; }
    void saw (Player& p) { p.settings.amp.envelope.sustain = 1.0f; p.settings.amp.envelope.attackMs = 0.0f; }
}

TEST_CASE ("New destinations: the matrix moves SKEW and PINCH (a note's own curve, within a hair of the exact one)", "[mod][routing]")
{
    using synth::mod::Field;
    const auto skew = routedVersusSet (saw, [] (synth::Settings& s, float v) { s.oscillators[0].skew = v; },
                                       { Field::oscSkew, 0 }, { -1.0f, 1.0f }, 0.0f, 0.6f);
    const auto pinch = routedVersusSet (saw, [] (synth::Settings& s, float v) { s.oscillators[0].pinch = v; },
                                        { Field::oscPinch, 0 }, { -1.0f, 1.0f }, -0.2f, -0.7f);
    WARN ("routed against set: SKEW " << skew << " dB, PINCH " << pinch << " dB");
    CHECK (skew < -55.0);
    CHECK (pinch < -55.0);
    // A route of no amount leaves it exactly as set; and SKEW really does change the sound.
    CHECK (routedVersusSet (saw, [] (synth::Settings& s, float v) { s.oscillators[0].skew = v; },
                            { Field::oscSkew, 0 }, { -1.0f, 1.0f }, 0.6f, 0.6f) < -200.0);
    Player plain, bent;
    saw (plain);
    saw (bent);
    bent.osc().skew = 0.6f;
    plain.on (57);
    bent.on (57);
    const auto a = plain.render (0.2)[0], b = bent.render (0.2)[0];
    CHECK (rms (a, 2400, 9600) > 0.0);
    double apart = 0.0;
    for (size_t i = 2400; i < a.size(); ++i)
        apart = std::max (apart, (double) std::abs (a[i] - b[i]));
    CHECK (apart > 0.1);
}

TEST_CASE ("New destinations: the shapers' EQ, PITCH RANGE and KEYTRACK, TRAVEL STRENGTH and PHASE, STEP SEQ VOL MIX", "[mod][routing]")
{
    using synth::mod::Field;
    const auto shaped = [] (Player& p)
    {
        saw (p);
        auto& sh = p.settings.shapers[0];
        sh.on = true;
        sh.drive = 0.5f;
    };
    CHECK (routedVersusSet (shaped, [] (synth::Settings& s, float v) { s.shapers[0].eqGainDb[0] = v; },
                            { Field::shaperEq1Gain, 0 }, { -24.0f, 24.0f }, 0.0f, 12.0f) < -80.0);
    CHECK (routedVersusSet (shaped, [] (synth::Settings& s, float v) { s.shapers[0].eqFreqHz[1] = v; s.shapers[0].eqGainDb[1] = 9.0f; },
                            { Field::shaperEq2Freq, 0 }, { 20.0f, 20000.0f }, 500.0f, 3000.0f) < -80.0);
    CHECK (routedVersusSet (shaped, [] (synth::Settings& s, float v) { s.shapers[0].eqQ[0] = v; s.shapers[0].eqGainDb[0] = -9.0f; },
                            { Field::shaperEq1Q, 0 }, { 0.1f, 10.0f }, 0.7f, 4.0f) < -80.0);

    // PITCH: the envelope held at its top, RANGE moved; KEYTRACK moved.
    synth::mod::Shape top;
    top.points[0] = { 0.0f, 1.0f, 0.0f };
    top.points[1] = { 2.0f, 1.0f, 0.0f };
    top.count = 2;
    const auto pitched = [&top] (Player& p) { sine (p); p.settings.pitch.envelopeOn = true; p.settings.pitch.shape = &top; };
    CHECK (routedVersusSet (pitched, [] (synth::Settings& s, float v) { s.pitch.rangeSemitones = v; },
                            { Field::pitchEnvRange, 0 }, { 0.0f, 24.0f }, 0.0f, 7.0f) < -80.0);
    CHECK (routedVersusSet (sine, [] (synth::Settings& s, float v) { s.pitch.keytrack = v; },
                            { Field::pitchKeytrack, 0 }, { -1.0f, 1.0f }, 1.0f, 0.5f) < -80.0);

    // TRAVEL on a two-frame table.
    constexpr int size = 2048;
    auto samples = std::make_shared<std::vector<float>>();
    for (const auto h : { 1, 2 })
        for (int i = 0; i < size; ++i)
            samples->push_back ((float) std::sin (juce::MathConstants<double>::twoPi * h * i / size));
    const auto table = synth::Wavetable::build ({ "Two", size, samples });
    const auto travelling = [&table] (Player& p)
    {
        saw (p);
        p.osc().wave = synth::OscWave::wavetable;
        p.osc().table = table.get();
        p.osc().travel.on = true;
        p.settings.modulation.bpm = 120.0;
    };
    CHECK (routedVersusSet (travelling, [] (synth::Settings& s, float v) { s.oscillators[0].travel.strength = v; },
                            { Field::oscTravelStrength, 0 }, { -1.0f, 1.0f }, 1.0f, 0.4f) < -80.0);
    CHECK (routedVersusSet (travelling, [] (synth::Settings& s, float v) { s.oscillators[0].travel.phase = v; },
                            { Field::oscTravelPhase, 0 }, { 0.0f, 1.0f }, 0.0f, 0.3f) < -80.0);

    // The step sequencer's VOL MIX.
    const auto gated = [] (Player& p) { sine (p); p.settings.stepSeq.on = true; p.settings.modulation.bpm = 120.0; };
    CHECK (routedVersusSet (gated, [] (synth::Settings& s, float v) { s.stepSeq.volMix = v; },
                            { Field::stepSeqVolMix, 0 }, { 0.0f, 1.0f }, 1.0f, 0.3f) < -80.0);
}

TEST_CASE ("New destinations are in the matrix's list, each a real parameter", "[mod][routing]")
{
    ParagonX1AudioProcessor processor;
    for (const auto* id : { "osc1Skew", "osc3Pinch", "osc2TrStrength", "osc8TrPhase", "shp1Eq1Gain", "shp4Eq2Q",
                            ParamIDs::pitchEnvRange, ParamIDs::pitchKeytrack, ParamIDs::seqVolMix })
    {
        INFO (id);
        const auto* d = ModDestinations::find (id);
        REQUIRE (d != nullptr);
        CHECK (processor.getParameters().getParameter (id) != nullptr);
    }
}

TEST_CASE ("CONTROLLER TO MOD WHEEL: a switched-on controller drives the mod wheel's routes", "[mod][routing]")
{
    // The mod wheel opening the cutoff of a filtered saw; the expression pedal pushed down, the wheel not.
    const auto brightness = [] (bool expressionToWheel)
    {
        Player p;
        p.settings.amp.envelope.sustain = 1.0f;
        p.settings.filter.on = true;
        p.settings.filter.cutoffHz = 200.0f;
        const juce::NormalisableRange<float> cutoff { 20.0f, 20000.0f, 0.0f, 0.25f };
        synth::mod::Route route;
        route.source = synth::mod::Source::modWheel;
        route.target = { synth::mod::Field::filterCutoff, 0 };
        route.range = &cutoff;
        route.base = cutoff.convertTo0to1 (200.0f);
        route.amount = 0.6f;
        p.settings.modulation.routes = &route;
        p.settings.modulation.numRoutes = 1;
        p.settings.modulation.toModWheel[1] = expressionToWheel;
        p.midi (juce::MidiMessage::controllerEvent (1, 11, 127));
        p.on (45);
        const auto out = p.render (0.4)[0];
        return levelAt (out, 110.0 * 12, 9600, 9600);   // the 12th harmonic: dark below the cutoff
    };
    const auto wheelOnly = brightness (false), withPedal = brightness (true);
    CHECK (db (withPedal) > db (wheelOnly) + 20.0);
}

TEST_CASE ("MIDI LEARN: a knob learns the next controller moved; the controller then turns it; links are kept", "[mod][midi]")
{
    const auto folder = juce::File::createTempFile ("pr7k-midi");
    folder.createDirectory();
    {
        ParagonX1AudioProcessor processor;
        processor.useRigFolder (folder);
        processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        auto* cutoff = processor.getParameters().getParameter (ParamIDs::fltCutoff);

        // A controller moved before learning isn't linked.
        processor.controllerForTest (20, 0.3f);
        CHECK (processor.getLastController() == 20);
        processor.startMidiLearn (ParamIDs::fltCutoff);
        processor.finishMidiLearn();
        CHECK (processor.getMidiLinks().isEmpty());
        // The next one is.
        processor.controllerForTest (74, 0.5f);
        processor.finishMidiLearn();
        CHECK (processor.getMidiLinks().controllerFor (ParamIDs::fltCutoff) == 74);
        CHECK (processor.getMidiLearnTarget().isEmpty());

        // Turning it turns the knob, at once (on the audio thread).
        processor.controllerForTest (74, 1.0f);
        CHECK_THAT (cutoff->getValue(), WithinAbs (1.0, 1.0e-6));
        processor.controllerForTest (74, 0.0f);
        CHECK_THAT (cutoff->getValue(), WithinAbs (0.0, 1.0e-6));
        // Others don't.
        processor.controllerForTest (75, 1.0f);
        CHECK_THAT (cutoff->getValue(), WithinAbs (0.0, 1.0e-6));

        // Linking the same controller elsewhere moves the link.
        auto links = processor.getMidiLinks();
        links.link (74, ParamIDs::fltRes);
        processor.setMidiLinks (links);
        CHECK (processor.getMidiLinks().controllerFor (ParamIDs::fltCutoff) == -1);
        processor.controllerForTest (74, 1.0f);
        CHECK_THAT (cutoff->getValue(), WithinAbs (0.0, 1.0e-6));

        // Kept with the project; saved as the default, a new instance starts with them.
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        {
            ParagonX1AudioProcessor reopened;
            reopened.setStateInformation (state.getData(), (int) state.getSize());
            CHECK (reopened.getMidiLinks() == processor.getMidiLinks());
        }
        REQUIRE (processor.saveMidiLinksAsDefault());
        CHECK (processor.midiLinksDefaultFile().isAChildOf (folder));
        MidiLinks fromFile;
        if (const auto xml = juce::XmlDocument::parse (processor.midiLinksDefaultFile()))
            fromFile = MidiLinks::fromXml (*xml);
        CHECK (fromFile == processor.getMidiLinks());

        // Forgetting.
        links = processor.getMidiLinks();
        links.forget (ParamIDs::fltRes);
        processor.setMidiLinks (links);
        CHECK (processor.getMidiLinks().isEmpty());
    }
    folder.deleteRecursively();
}

namespace
{
    ParamControl* findControl (juce::Component& in, const juce::String& id)
    {
        for (auto* child : in.getChildren())
        {
            if (auto* c = dynamic_cast<ParamControl*> (child); c != nullptr && c->getParameterId() == id)
                return c;
            if (auto* found = findControl (*child, id))
                return found;
        }
        return nullptr;
    }
}

TEST_CASE ("Drag and drop: a source dropped on a knob routes it, a controller links it; the ring shows the routes", "[mod][midi][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    auto* cutoff = findControl (*view, ParamIDs::fltCutoff);
    REQUIRE (cutoff != nullptr);
    CHECK (cutoff->getModulationRange().isEmpty());

    // The strip's LFO 1 chip, dropped on the cutoff: a route at +50%.
    auto& strip = view->getSourceStrip();
    auto* lfo = strip.chipFor (ControlHost::sourceDrag ("lfo1"));
    REQUIRE (lfo != nullptr);
    const juce::DragAndDropTarget::SourceDetails dropLfo (lfo->description, lfo, {});
    REQUIRE (cutoff->isInterestedInDragSource (dropLfo));
    cutoff->itemDropped (dropLfo);
    REQUIRE (processor.getModMatrix().routes.size() == 1);
    const auto& route = processor.getModMatrix().routes[0];
    CHECK (route.source == "lfo1");
    CHECK (route.destination == ParamIDs::fltCutoff);
    CHECK_THAT (route.amount, WithinAbs (0.5, 1.0e-6));

    // The ring: from where the knob is, up by half the travel (to the end, at most).
    auto* parameter = processor.getParameters().getParameter (ParamIDs::fltCutoff);
    parameter->setValueNotifyingHost (0.3f);
    CHECK_THAT (cutoff->getModulationRange().getStart(), WithinAbs (0.3, 1.0e-4));
    CHECK_THAT (cutoff->getModulationRange().getEnd(), WithinAbs (0.8, 1.0e-4));
    parameter->setValueNotifyingHost (0.7f);
    CHECK_THAT (cutoff->getModulationRange().getEnd(), WithinAbs (1.0, 1.0e-4));

    // Removing it (as the knob's menu does) takes the ring away; undo brings it back.
    processor.flushUndoStep();
    view->removeRoute (0);
    CHECK (processor.getModMatrix().routes.empty());
    CHECK (cutoff->getModulationRange().isEmpty());

    // A choice (a menu) can't be modulated, so it doesn't take a source; a controller it does.
    auto* filterType = findControl (*view, ParamIDs::fltType);
    REQUIRE (filterType != nullptr);
    CHECK_FALSE (filterType->isInterestedInDragSource (dropLfo));

    // The controller chip (after CC 74 moves), dropped on RESONANCE: linked.
    processor.controllerForTest (74, 0.5f);
    strip.refresh();
    auto* controller = strip.chipFor (ControlHost::controllerDrag (74));
    REQUIRE (controller != nullptr);
    auto* resonance = findControl (*view, ParamIDs::fltRes);
    REQUIRE (resonance != nullptr);
    const juce::DragAndDropTarget::SourceDetails dropCc (controller->description, controller, {});
    REQUIRE (resonance->isInterestedInDragSource (dropCc));
    resonance->itemDropped (dropCc);
    CHECK (processor.getMidiLinks().controllerFor (ParamIDs::fltRes) == 74);

    // MIDI learn from the knob's menu: on, then off again.
    view->learn (ParamIDs::fltDrive);
    CHECK (view->isLearning (ParamIDs::fltDrive));
    view->learn (ParamIDs::fltDrive);
    CHECK_FALSE (view->isLearning (ParamIDs::fltDrive));

    // The strip: every chip inside it, none overlapping, above the keyboard.
    std::vector<juce::Component*> chips;
    for (auto* child : strip.getChildren())
        chips.push_back (child);
    CHECK (chips.size() == 29);   // 28 sources and the controller
    for (size_t i = 0; i < chips.size(); ++i)
    {
        INFO (chips[i]->getBounds().toString());
        CHECK (strip.getLocalBounds().contains (chips[i]->getBounds()));
        CHECK (chips[i]->getWidth() >= 20);
        for (size_t j = i + 1; j < chips.size(); ++j)
            CHECK_FALSE (chips[i]->getBounds().intersects (chips[j]->getBounds()));
    }
}

TEST_CASE ("The SETTINGS page lists the MIDI links, each with a button to remove it", "[midi][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    MidiLinks links;
    links.link (74, ParamIDs::fltCutoff);
    links.link (71, ParamIDs::fltRes);
    processor.setMidiLinks (links);
    view->showPage (MainView::Page::settings);
    auto& page = view->getSettingsPage();
    REQUIRE (page.getNumLinkRows() == 2);
    page.getRemoveButton (0)->onClick();   // CC 71, sorted first
    CHECK (processor.getMidiLinks().controllerFor (ParamIDs::fltRes) == -1);
    CHECK (processor.getMidiLinks().controllerFor (ParamIDs::fltCutoff) == 74);
}

TEST_CASE ("CPU: a supersaw chord with SKEW moved by an LFO on every note", "[routing][performance]")
{
    const auto once = [] (bool modulated)
    {
        Player p;
        p.settings.amp.envelope.sustain = 1.0f;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
            o.skew = 0.4f;
        }
        const juce::NormalisableRange<float> range { -1.0f, 1.0f };
        std::array<synth::mod::Route, 2> routes {};
        for (int i = 0; i < 2; ++i)
        {
            routes[(size_t) i].source = synth::mod::Source::lfo1;
            routes[(size_t) i].target = { synth::mod::Field::oscSkew, i };
            routes[(size_t) i].range = &range;
            routes[(size_t) i].base = range.convertTo0to1 (0.4f);
            routes[(size_t) i].amount = 0.3f;
        }
        if (modulated)
        {
            p.settings.modulation.routes = routes.data();
            p.settings.modulation.numRoutes = 2;
            p.settings.modulation.usedSources = 1u << (int) synth::mod::Source::lfo1;
            p.settings.modulation.lfos[0].rateHz = 3.0f;
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (2.0);
        return (juce::Time::getMillisecondCounterHiRes() - started) / 2000.0 * 100.0;
    };
    const auto fixed = juce::jmin (once (false), once (false), once (false));
    const auto moving = juce::jmin (once (true), once (true), once (true));
    WARN ("2 saws x 7 copies x 8 keys with SKEW: " << fixed << "% of one core; moved by an LFO per note: " << moving << "% (the fastest of three)");
    CHECK (moving < fixed + 3.0);
}
