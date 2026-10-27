// The PITCH module (stage 4c): the pitch envelope, keytrack, bend lag, and
// the glide's curve and split note, measured as pitch.
#include "SynthTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/MainView.h"

using Catch::Matchers::WithinRel;

namespace
{
    using P = synth::mod::ShapePoint;

    synth::mod::Shape shapeOf (std::initializer_list<P> points)
    {
        synth::mod::Shape s;
        for (const auto& p : points)
            s.points[(size_t) s.count++] = p;
        return s;
    }

    struct PitchPlayer : Player
    {
        synth::mod::Shape shape;
        PitchPlayer()
        {
            osc().wave = synth::OscWave::sine;
            settings.amp.envelope.sustain = 1.0f;
            settings.amp.envelope.attackMs = 0.0f;
            settings.amp.envelope.releaseMs = 1.0f;   // a note let go is gone at once (so it doesn't blur the next one's pitch)
            settings.pitch.shape = &shape;
        }
    };

    // The pitch over [from, to) seconds, in semitones from `note`.
    double semitonesBetween (const std::vector<float>& out, double from, double to, double note)
    {
        const auto hz = frequencyOf (out, (size_t) (from * sampleRate), (size_t) (to * sampleRate));
        return 12.0 * std::log2 (hz / noteHz (note));
    }
}

TEST_CASE ("PITCH envelope: the top is +RANGE, the middle no change; it moves the note as drawn", "[pitch]")
{
    PitchPlayer p;
    p.shape = shapeOf ({ P { 0.0f, 1.0f, 0.0f }, P { 1.0f, 0.5f, 0.0f } });   // +RANGE down to nothing over a second
    p.settings.pitch.envelopeOn = true;
    p.settings.pitch.rangeSemitones = 12.0f;
    p.on (57);
    const auto out = p.render (1.5)[0];
    CHECK_THAT (semitonesBetween (out, 0.01, 0.03, 57.0), WithinAbs (11.76, 0.2));   // near the top (12 at 0, 11.52 at 40 ms)
    CHECK_THAT (semitonesBetween (out, 0.49, 0.51, 57.0), WithinAbs (6.0, 0.15));
    CHECK_THAT (semitonesBetween (out, 1.1, 1.4, 57.0), WithinAbs (0.0, 0.02));

    // Off: nothing.
    PitchPlayer off;
    off.shape = p.shape;
    off.on (57);
    const auto plain = off.render (0.3)[0];
    CHECK_THAT (semitonesBetween (plain, 0.1, 0.3, 57.0), WithinAbs (0.0, 0.01));
}

TEST_CASE ("PITCH envelope: RANGE, FLIP, STEPS, and only the oscillators that follow it", "[pitch]")
{
    const auto at = [] (const std::function<void (PitchPlayer&)>& setup)
    {
        PitchPlayer p;
        p.shape = shapeOf ({ P { 0.0f, 0.75f, 0.0f }, P { 2.0f, 0.75f, 0.0f } });   // half way up, held
        p.settings.pitch.envelopeOn = true;
        setup (p);
        p.on (57);
        const auto out = p.render (0.4)[0];
        return semitonesBetween (out, 0.1, 0.35, 57.0);
    };
    CHECK_THAT (at ([] (PitchPlayer& p) { p.settings.pitch.rangeSemitones = 12.0f; }), WithinAbs (6.0, 0.02));
    CHECK_THAT (at ([] (PitchPlayer& p) { p.settings.pitch.rangeSemitones = 24.0f; }), WithinAbs (12.0, 0.02));
    CHECK_THAT (at ([] (PitchPlayer& p) { p.settings.pitch.rangeSemitones = 12.0f; p.settings.pitch.envelope.flip = true; }), WithinAbs (-6.0, 0.02));
    // STEPS 2: levels 0, 0.5, 1: 0.75 rounds to 1 (the top).
    CHECK_THAT (at ([] (PitchPlayer& p) { p.settings.pitch.rangeSemitones = 12.0f; p.settings.pitch.envelope.steps = 2; }), WithinAbs (12.0, 0.02));
    // An oscillator not following it plays its note.
    CHECK_THAT (at ([] (PitchPlayer& p) { p.settings.pitch.rangeSemitones = 12.0f; p.osc().pitchEnvelope = false; }), WithinAbs (0.0, 0.01));
}

TEST_CASE ("PITCH KEYTRACK: 0% plays every key as middle C, -100% turns the keyboard upside down", "[pitch]")
{
    const auto played = [] (float keytrack, int note)
    {
        PitchPlayer p;
        p.settings.pitch.keytrack = keytrack;
        p.on (note);
        const auto out = p.render (0.3)[0];
        return frequencyOf (out, 2400, 14000);
    };
    CHECK_THAT (played (1.0f, 69), WithinRel (440.0, 1.0e-4));
    CHECK_THAT (played (0.0f, 69), WithinRel (noteHz (60.0), 1.0e-4));
    CHECK_THAT (played (0.0f, 40), WithinRel (noteHz (60.0), 1.0e-4));
    CHECK_THAT (played (-1.0f, 72), WithinRel (noteHz (48.0), 1.0e-4));
    CHECK_THAT (played (0.5f, 72), WithinRel (noteHz (66.0), 1.0e-4));
}

TEST_CASE ("PITCH BEND LAG: the wheel's jump glides", "[pitch]")
{
    const auto afterBend = [] (float lagMs, double from, double to)
    {
        PitchPlayer p;
        p.settings.pitch.bendLagMs = lagMs;
        p.on (69);
        p.render (0.1);
        p.midi (juce::MidiMessage::pitchWheel (1, 16383));   // full up: +2 semitones
        const auto out = p.render (0.8)[0];
        return semitonesBetween (out, from, to, 69.0);
    };
    CHECK_THAT (afterBend (0.0f, 0.01, 0.03), WithinAbs (2.0, 0.01));
    const auto early = afterBend (200.0f, 0.01, 0.03);
    CHECK (early > 0.1);
    CHECK (early < 0.4);                                     // about 2 (1 - e^-0.1) = 0.19 after 20 ms
    CHECK_THAT (afterBend (200.0f, 0.7, 0.79), WithinAbs (2.0, 0.08));   // there after 3.5 time constants
}

TEST_CASE ("GLIDE CURVE bends the glide's path; at 0 it's the straight glide, sample for sample", "[pitch]")
{
    const auto glide = [] (float curve)
    {
        PitchPlayer p;
        p.settings.play.glideMs = 400.0f;
        p.settings.pitch.glideCurve = curve;
        p.on (57);
        p.render (0.1);
        p.off (57);
        p.on (69);
        return p.render (0.6)[0];
    };
    const auto straight = glide (0.0f);
    const auto curved = glide (-1.0f);
    const auto late = glide (1.0f);
    CHECK_THAT (semitonesBetween (straight, 0.09, 0.11, 57.0), WithinAbs (3.0, 0.25));    // a quarter of the way at 100 ms
    CHECK (semitonesBetween (curved, 0.09, 0.11, 57.0) > 9.0);                           // most of the way at once
    CHECK (semitonesBetween (late, 0.09, 0.11, 57.0) < 0.5);                             // slow to start
    CHECK_THAT (semitonesBetween (curved, 0.45, 0.58, 57.0), WithinAbs (12.0, 0.02));    // all arrive

    // CURVE 0 is the glide as it was (no change for existing sounds).
    PitchPlayer before;
    before.settings.play.glideMs = 400.0f;
    before.on (57);
    before.render (0.1);
    before.off (57);
    before.on (69);
    CHECK (before.render (0.6)[0] == straight);
}

TEST_CASE ("GLIDE SPLIT: notes below it don't glide, and aren't glided from", "[pitch]")
{
    PitchPlayer p;
    p.settings.play.glideMs = 300.0f;
    p.settings.pitch.glideSplit = 60;
    p.on (72);
    p.render (0.4);
    p.off (72);
    p.on (48);          // below the split: straight in
    auto out = p.render (0.3)[0];
    CHECK_THAT (semitonesBetween (out, 0.005, 0.03, 48.0), WithinAbs (0.0, 0.02));
    p.off (48);
    p.on (74);          // above: glides from 72 (the last note above the split), not from 48
    out = p.render (0.4)[0];
    const auto start = semitonesBetween (out, 0.003, 0.012, 74.0);
    CHECK (start > -2.2);
    CHECK (start < -1.5);
}

TEST_CASE ("PITCH ENV as a matrix source, even with the pitch envelope itself off", "[pitch][mod]")
{
    PitchPlayer p;
    p.shape = shapeOf ({ P { 0.0f, 1.0f, 0.0f }, P { 2.0f, 1.0f, 0.0f } });   // the top, held
    const juce::NormalisableRange<float> fineRange { -100.0f, 100.0f, 1.0f };
    synth::mod::Route route;
    route.source = synth::mod::Source::pitchEnvelope;
    route.target = { synth::mod::Field::oscFine, 0 };
    route.range = &fineRange;
    route.base = 0.5f;
    route.amount = 0.5f;
    p.settings.modulation.routes = &route;
    p.settings.modulation.numRoutes = 1;
    p.on (69);
    const auto out = p.render (0.4)[0];
    CHECK_THAT (1200.0 * std::log2 (frequencyOf (out, 2400, 14000) / 440.0), WithinAbs (100.0, 1.0));
}

TEST_CASE ("PITCH in the plugin: its settings reach the engine, its envelope plays from a fresh start and is kept", "[pitch][processor]")
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
    set (ParamIDs::pitchEnvOn, 1.0f);
    set (ParamIDs::pitchEnvRange, 24.0f);
    set (ParamIDs::pitchKeytrack, 0.5f);
    set (ParamIDs::glideSplit, 60.0f);
    set (ParamIDs::osc (3, "PitchEnv"), 0.0f);
    const auto settings = processor.getSynthSettings();
    CHECK (settings.pitch.envelopeOn);
    CHECK_THAT (settings.pitch.rangeSemitones, WithinAbs (24.0, 1.0e-4));
    CHECK_THAT (settings.pitch.keytrack, WithinAbs (0.5, 1.0e-3));
    CHECK (settings.pitch.glideSplit == 60);
    CHECK_FALSE (settings.oscillators[3].pitchEnvelope);
    CHECK (settings.oscillators[2].pitchEnvelope);

    // A fresh plugin's envelope (the default drop: +half the RANGE to nothing in 150 ms) plays at once.
    set (ParamIDs::pitchKeytrack, 1.0f);
    set (ParamIDs::osc (0, "Wave"), (float) synth::OscWave::sine);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
    std::vector<float> out;
    for (int block = 0; block < 60; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        midi.clear();
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    // (Its line is curved, fast first: 7 ms in it has already fallen to about +9.5.)
    CHECK (semitonesBetween (out, 0.002, 0.012, 57.0) > 9.0);
    CHECK_THAT (semitonesBetween (out, 0.2, 0.3, 57.0), WithinAbs (0.0, 0.05));

    // Presets, projects and undo keep its points.
    auto matrix = processor.getModMatrix();
    matrix.pitchEnvelope.points.push_back ({ 0.5f, 0.2f, 0.3f });
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    const auto rig = Rig::fromXml (*processor.captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->modulation.pitchEnvelope.points.size() == 3);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (reopened.getModMatrix().pitchEnvelope.points.size() == 3);
    }
    processor.flushUndoStep();
    auto edited = processor.getModMatrix();
    edited.pitchEnvelope = ModShapeData::defaultPitchEnvelope();
    processor.setModMatrix (edited);
    processor.modMatrixEditFinished();
    processor.flushUndoStep();
    processor.undo();
    CHECK (processor.getModMatrix().pitchEnvelope.points.size() == 3);
}

TEST_CASE ("The PITCH tab: its controls and the oscillators following it, laid out cleanly", "[pitch][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::modulation);
    auto& panel = view->getModPage().getModulatorPanel();
    panel.selectPitch();
    CHECK (panel.getEditor().getKind() == MultiPointEditor::Kind::pitchEnvelope);
    CHECK (panel.getEditor().getShape() == ModShapeData::defaultPitchEnvelope());

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
}
