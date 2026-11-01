// ARTICULATIONS PART 2 (PR7-D 0.18.0): velocity curves (each drum's, and the incoming MIDI's), the velocity gate, the
// MIDI channel, CC zones, the MIDI monitor, MIDI IN presets and the project, LEARN and the hi-hat's calibration.
#include "TestUtils.h"
#include <catch2/catch_approx.hpp>
#include "PluginTestUtils.h"

#include "dsp/synth/NoteMap.h"
#include "rigs/Rig.h"
#include "ui/DrumPage.h"
#include "ui/MidiInPanel.h"

namespace
{
    using namespace synth::drums;
    using namespace synth::drums::notemap;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-midiin-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        explicit Plugin (const juce::File& presets = {})
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            processor.seedRandomForTest (1);
            if (presets != juce::File())
                processor.useRigFolder (presets);
        }
        void set (const juce::String& id, float value) { testkit::set (processor, id, value); }
        std::vector<float> play (double seconds, const juce::MidiBuffer& midi = {})
        {
            processor.prepareToPlay (sampleRate, blockSize);
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                juce::MidiBuffer chunk;
                for (const auto m : midi)
                    if (m.samplePosition >= b * blockSize && m.samplePosition < (b + 1) * blockSize)
                        chunk.addEvent (m.getMessage(), m.samplePosition - b * blockSize);
                buffer.clear();
                processor.processBlock (buffer, chunk);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
        static juce::MidiBuffer note (int n, int velocity, int channel = 10)
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (channel, n, (juce::uint8) velocity), 0);
            return midi;
        }
        std::vector<int> sounding (const juce::MidiBuffer& midi)
        {
            play (0.02, midi);
            std::vector<int> slots;
            for (int k = 0; k < numSlots; ++k)
                if (processor.getDrumKit().isSounding (k))
                    slots.push_back (k);
            return slots;
        }
    };

    float peak (const std::vector<float>& x)
    {
        auto p = 0.0f;
        for (const auto v : x)
            p = std::max (p, std::abs (v));
        return p;
    }
}

TEST_CASE ("VELOCITY CURVE: straight changes nothing; limits, points, GATE and SOFT shape it; it saves as text", "[midiin]")
{
    VelocityCurve straight;
    CHECK (straight.isStraight());
    for (int v = 0; v <= 127; ++v)
        CHECK (straight.apply ((float) v / 127.0f) == (float) v / 127.0f);

    // The limits: soft hits lifted to at least 40, nothing harder than 100.
    VelocityCurve limited;
    limited.points[0] = { 0.0f, 40.0f / 127.0f };
    limited.points[1] = { 1.0f, 100.0f / 127.0f };
    CHECK (limited.apply (0.0f) * 127.0f == Catch::Approx (40.0f));
    CHECK (limited.apply (1.0f) * 127.0f == Catch::Approx (100.0f));
    CHECK (limited.apply (0.5f) * 127.0f == Catch::Approx (70.0f));
    // The left limit moved in: below it, its height; with GATE, nothing.
    VelocityCurve gated;
    gated.points[0] = { 30.0f / 127.0f, 30.0f / 127.0f };
    CHECK (gated.apply (10.0f / 127.0f) * 127.0f == Catch::Approx (30.0f));
    gated.gate = true;
    CHECK (gated.apply (10.0f / 127.0f) < 0.0f);
    CHECK (gated.apply (40.0f / 127.0f) >= 0.0f);
    // A point: 91 in comes out 114 (the manual's example), the line through it.
    VelocityCurve bent;
    CHECK (bent.add ({ 91.0f / 127.0f, 114.0f / 127.0f }) == 1);
    CHECK (bent.apply (91.0f / 127.0f) * 127.0f == Catch::Approx (114.0f));
    // SOFT: the same at the points, rounded between them.
    auto soft = bent;
    soft.soft = true;
    CHECK (soft.apply (91.0f / 127.0f) == Catch::Approx (bent.apply (91.0f / 127.0f)));
    CHECK (soft.apply (20.0f / 127.0f) < bent.apply (20.0f / 127.0f));
    // A point can't pass its neighbours; the limits can't be removed.
    bent.move (1, { 1.5f, 0.5f });
    CHECK (bent.points[1].x == 1.0f);
    bent.remove (0);
    CHECK (bent.count == 3);
    bent.remove (1);
    CHECK (bent.count == 2);
    // As text and back.
    soft.gate = true;
    CHECK (VelocityCurve::decode (soft.encode()) == soft);
    CHECK (VelocityCurve::decode ("").isStraight());
}

TEST_CASE ("VELOCITY CURVE on a drum: every hit through it (keys, grooves); the GATE keeps out hits outside its range", "[midiin]")
{
    Plugin p;
    testkit::useSample (p.processor, SD, testkit::sine (200.0));
    const auto hard = peak (p.play (0.1, Plugin::note (38, 127)));
    const auto mid = peak (p.play (0.1, Plugin::note (38, 64)));
    REQUIRE (mid < hard * 0.5f);

    // A curve sending everything to full: a mid hit now as loud as a hard one.
    auto m = p.processor.getModMatrix();
    m.drumVelocityCurves[(size_t) SD].points[0] = { 0.0f, 1.0f };
    p.processor.setModMatrix (m);
    CHECK (peak (p.play (0.1, Plugin::note (38, 64))) == Catch::Approx (hard).epsilon (1.0e-5));
    // Only that drum: the kick still follows its velocity.
    testkit::useSample (p.processor, BD, testkit::sine (60.0));
    const auto kickHard = peak (p.play (0.1, Plugin::note (36, 127)));
    CHECK (peak (p.play (0.1, Plugin::note (36, 64))) < kickHard * 0.5f);

    // It's kept in presets, and kit files.
    const auto saved = Rig::fromXml (*[&] { Rig r; r.modulation = p.processor.getModMatrix(); return r.toXml(); }());
    REQUIRE (saved.has_value());
    CHECK (saved->modulation.drumVelocityCurves[(size_t) SD] == m.drumVelocityCurves[(size_t) SD]);

    // THE GATE: hits softer than 50 don't play the snare; 60 does.
    p.processor.setModMatrix ({});
    p.set (ParamIDs::drum (SD, "VelMin"), 50.0f);
    CHECK (peak (p.play (0.1, Plugin::note (38, 40))) == 0.0f);
    CHECK (peak (p.play (0.1, Plugin::note (38, 60))) > 0.0f);
    p.set (ParamIDs::drum (SD, "VelMax"), 100.0f);
    CHECK (peak (p.play (0.1, Plugin::note (38, 110))) == 0.0f);
}

TEST_CASE ("MIDI IN: the incoming curve, the channel, CC zones; grooves and the song don't go through them", "[midiin]")
{
    Plugin p;
    p.set (ParamIDs::drumsMap, (float) Layout::grooveLibrary);
    testkit::useSample (p.processor, SD, testkit::sine (200.0));
    const auto hard = peak (p.play (0.1, Plugin::note (38, 127)));

    // The incoming curve: everything full.
    auto in = p.processor.getMidiIn();
    in.curve.points[0] = { 0.0f, 1.0f };
    p.processor.setMidiIn (in);
    CHECK (peak (p.play (0.1, Plugin::note (38, 30))) == Catch::Approx (hard).epsilon (1.0e-5));
    // ...but a groove's hit at 30 / 127 is played as written (the kit's own hits don't go through it).
    {
        synth::drums::KitSettings settings = p.processor.getKitSettings();
        CHECK (settings.inputCurve != nullptr);
        synth::drums::Kit kit;
        kit.prepare (sampleRate, blockSize);
        std::vector<float> left ((size_t) blockSize * 8), right ((size_t) blockSize * 8);
        synth::drums::Outputs outs;
        outs.left = left.data();
        outs.right = right.data();
        const synth::drums::SlotHit hit { 0, SD, 30.0f / 127.0f };
        kit.process (outs, blockSize * 8, {}, 0, settings, {}, -1.0, 120.0, &hit, 1);
        CHECK (peak (left) < hard * 0.5f);
    }
    // GATE on the incoming curve: soft notes don't play at all.
    in.curve = {};
    in.curve.points[0] = { 20.0f / 127.0f, 20.0f / 127.0f };
    in.curve.gate = true;
    p.processor.setMidiIn (in);
    CHECK (p.sounding (Plugin::note (38, 10)).empty());
    CHECK (p.sounding (Plugin::note (38, 30)) == std::vector<int> { SD });
    p.processor.setMidiIn ({});

    // THE CHANNEL: on 10, channel 1 is ignored.
    p.set (ParamIDs::drumsMidiChannel, 10.0f);
    CHECK (p.sounding (Plugin::note (38, 100, 1)).empty());
    CHECK (p.sounding (Plugin::note (38, 100, 10)) == std::vector<int> { SD });
    p.set (ParamIDs::drumsMidiChannel, 0.0f);
    CHECK (p.sounding (Plugin::note (38, 100, 1)) == std::vector<int> { SD });

    // CC ZONES: note 38 in three zones by CC 16: centre, edge (rimshot), rim (sidestick).
    in = p.processor.getMidiIn();
    auto* z = in.zonesFor (38);
    REQUIRE (z != nullptr);
    z->cc = 16;
    z->count = 3;
    z->bounds = { 43, 86, 120 };
    z->slots = { -1, SD2, RIM, -1 };
    p.processor.setMidiIn (in);
    const auto zoned = [&p] (int cc)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (10, 16, cc), 0);
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), 1);
        return p.sounding (midi);
    };
    CHECK (zoned (10) == std::vector<int> { SD });    // zone 1, "as mapped"
    CHECK (zoned (42) == std::vector<int> { SD });
    CHECK (zoned (43) == std::vector<int> { SD2 });
    CHECK (zoned (100) == std::vector<int> { RIM });
    CHECK (zoned (127) == std::vector<int> { RIM });
    // Other notes are left alone.
    CHECK (p.sounding (Plugin::note (36, 100)) == std::vector<int> { BD });
}

TEST_CASE ("MIDI MONITOR: notes, controllers and aftertouch, the drum played and the velocity after the curve", "[midiin]")
{
    Plugin p;
    p.set (ParamIDs::drumsMap, (float) Layout::grooveLibrary);
    auto in = p.processor.getMidiIn();
    in.curve.points[0] = { 30.0f / 127.0f, 30.0f / 127.0f };
    in.curve.gate = true;
    p.processor.setMidiIn (in);
    p.set (ParamIDs::drum (HO1, "On"), 0.0f);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, 90), 0);
    midi.addEvent (juce::MidiMessage::noteOn (10, 24, (juce::uint8) 100), 1);   // open 1, off: open 2 plays
    midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 10), 2);    // gated
    midi.addEvent (juce::MidiMessage::aftertouchChange (10, 49, 80), 3);
    p.play (0.01, midi);
    std::array<Kit::MidiEvent, 16> events;
    const auto n = p.processor.popMidiEvents (events.data(), 16);
    REQUIRE (n == 4);
    CHECK (events[0].type == Kit::MidiEvent::Type::controller);
    CHECK (events[0].number == 4);
    CHECK (events[0].value == 90);
    CHECK (events[1].type == Kit::MidiEvent::Type::noteOn);
    CHECK (events[1].slot == HH);
    CHECK (events[1].valueOut == 100);
    CHECK (events[2].slot == -1);
    CHECK (events[2].valueOut == -1);
    CHECK (events[3].type == Kit::MidiEvent::Type::aftertouch);
    CHECK (p.processor.popMidiEvents (events.data(), 16) == 0);   // read once
}

TEST_CASE ("MIDI IN presets: saved and loaded (map, channel, curve, zones, pedal steps); the built-in ones; the project keeps them", "[midiin]")
{
    TempFolder temp;
    Plugin p (temp.folder);
    p.processor.mapNote (50, SD2);   // a CUSTOM map
    p.set (ParamIDs::drumsMidiChannel, 10.0f);
    p.set (ParamIDs::drumsPedalSteps[0], 90.0f);
    auto in = p.processor.getMidiIn();
    in.curve.add ({ 0.5f, 0.8f });
    auto* z = in.zonesFor (38);
    z->count = 2;
    z->slots = { -1, RIM, -1, -1 };
    p.processor.setMidiIn (in);
    REQUIRE (p.processor.saveMidiInPreset ("My E-Kit"));
    CHECK (p.processor.listMidiInPresets().size() == 1);

    // The built-in DEFAULT puts everything back.
    p.processor.applyBuiltInMidiInPreset (0);
    CHECK (p.processor.getMapLayout() == Layout::grooveLibrary);
    CHECK (p.processor.getMidiIn() == ParagonX1AudioProcessor::MidiIn {});
    CHECK (p.processor.getKitSettings().channel == 0);
    CHECK (p.processor.getKitSettings().pedalSteps[0] == 102);
    p.processor.applyBuiltInMidiInPreset (1);
    CHECK (p.processor.getMapLayout() == Layout::generalMidi);

    // Loaded back: all of it.
    REQUIRE (p.processor.loadMidiInPreset (p.processor.listMidiInPresets()[0]));
    CHECK (p.processor.getMidiInPresetName() == "My E-Kit");
    CHECK (p.processor.getMapLayout() == Layout::custom);
    CHECK (p.processor.getNoteMap()[50] == SD2);
    CHECK (p.processor.getKitSettings().channel == 10);
    CHECK (p.processor.getKitSettings().pedalSteps[0] == 90);
    CHECK (p.processor.getMidiIn() == in);

    // The project keeps the curve and zones (and the preset's name).
    juce::MemoryBlock state;
    p.processor.getStateInformation (state);
    Plugin other (temp.folder);
    other.processor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.processor.getMidiIn() == in);
    CHECK (other.processor.getMidiInPresetName() == "My E-Kit");
    // They aren't part of presets: switching preset leaves them.
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::drumsMidiChannel));
}

TEST_CASE ("MIDI IN panel: LEARN maps a hit pad's note; LEARN CC; CALIBRATE spreads the steps over the pedal's range", "[midiin]")
{
    Plugin p;
    p.set (ParamIDs::drumsMap, (float) Layout::grooveLibrary);
    MidiInPanel panel (p.processor);
    panel.setSize (340, 480);

    // LEARN on note 38 (the snare's centre): hitting note 70 makes 70 play it.
    panel.setNote (38);
    panel.startLearn();
    p.play (0.01, Plugin::note (70, 100));
    panel.pollMidi();
    CHECK_FALSE (panel.isLearning());
    CHECK (p.processor.getNoteMap()[70] == SD);
    CHECK (panel.getNote() == 70);
    CHECK (panel.getMonitorLines().size() == 1);
    CHECK (panel.getMonitorLines()[0].contains ("NOTE 70"));

    // CC ZONES with LEARN CC: the pad's controller (CC 23) becomes the zones' controller.
    panel.setNote (38);
    panel.setZoneCount (3);
    panel.startLearnController();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (10, 23, 64), 0);
    p.play (0.01, midi);
    panel.pollMidi();
    const auto zoneOf38 = [&p]
    {
        for (const auto& z : p.processor.getMidiIn().zones)
            if (z.note == 38)
                return z;
        return KitSettings::CcZones {};
    };
    CHECK (zoneOf38().cc == 23);
    CHECK (zoneOf38().count == 3);
    // Marks kept rising.
    panel.setZoneBound (0, 120);
    CHECK (zoneOf38().bounds[0] < zoneOf38().bounds[1]);
    panel.setZoneSlot (2, RIM);
    CHECK (zoneOf38().slots[2] == RIM);
    panel.setZoneCount (0);
    CHECK (zoneOf38().count == 0);

    // CALIBRATE: the pedal only reached 12 to 93; the steps spread over that.
    panel.startCalibration();
    midi.clear();
    midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, 50), 0);
    midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, 93), 1);
    midi.addEvent (juce::MidiMessage::controllerEvent (10, 4, 12), 2);
    p.play (0.01, midi);
    panel.pollMidi();
    panel.finishCalibration();
    const auto steps = p.processor.getKitSettings().pedalSteps;
    CHECK (steps[0] == 77);   // 12 + 81 x 0.8
    CHECK (steps[1] == 61);
    CHECK (steps[2] == 44);
    CHECK (steps[3] == 28);
    // A pedal that hardly moved changes nothing.
    panel.startCalibration();
    panel.finishCalibration();
    CHECK (p.processor.getKitSettings().pedalSteps == steps);
}

TEST_CASE ("DRUM view's VELOCITY: the curve goes to every chosen drum, one undo step", "[midiin]")
{
    Plugin p;
    DrumPage page (p.processor);
    page.setSize (1400, 700);
    page.showView (DrumPage::View::drum);
    page.showVelocity (true);
    CHECK (page.isShowingVelocity());
    CHECK (page.getVelocityEditor().isVisible());
    page.chooseSlots ({ SD, SD2, RIM });
    VelocityCurve c;
    c.points[0] = { 0.0f, 0.3f };
    page.setVelocityCurve (c, true);
    for (const auto k : { SD, SD2, RIM })
        CHECK (p.processor.getModMatrix().drumVelocityCurves[(size_t) k] == c);
    CHECK (p.processor.getModMatrix().drumVelocityCurves[(size_t) BD].isStraight());
    page.showVelocity (false);
    CHECK_FALSE (page.getVelocityEditor().isVisible());
}
