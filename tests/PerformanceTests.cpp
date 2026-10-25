// Phase 6: the "performance" features: tap tempo, undo/redo, the tuner's
// mute and the expression pedal.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/TapTempo.h"
#include "rigs/UndoHistory.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct TestRig
    {
        TestRig()
        {
            folder.createDirectory();
            processor.useRigFolder (folder);
            processor.setPlayConfigDetails (1, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
        }
        ~TestRig() { folder.deleteRecursively(); }

        juce::RangedAudioParameter& parameter (const char* id) { return *processor.getParameters().getParameter (id); }
        float value (const char* id) { return parameter (id).convertFrom0to1 (parameter (id).getValue()); }

        // A knob turned with the mouse: a gesture around the change, as the UI does.
        void turn (const char* id, float plainValue)
        {
            auto& p = parameter (id);
            p.beginChangeGesture();
            p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
            p.endChangeGesture();
            processor.flushUndoStep();
        }

        // Plays a 220 Hz tone for `seconds`; returns the peak of the last block.
        float play (double seconds, juce::MidiBuffer midi = {})
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            float peak = 0.0f;
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (0, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (phase++) / sampleRate));
                processor.processBlock (buffer, midi);
                midi.clear();
                peak = buffer.getMagnitude (0, 0, blockSize);
            }
            return peak;
        }

        juce::File folder { juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1-perf-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())) };
        ParagonX1AudioProcessor processor;
        long phase = 0;
    };
}

TEST_CASE ("Tap tempo averages the taps and starts again after a pause", "[tap]")
{
    TapTempo taps;
    CHECK_FALSE (taps.tap (0.0).has_value());   // one tap is not a tempo

    auto bpm = taps.tap (500.0);
    REQUIRE (bpm.has_value());
    CHECK_THAT (*bpm, WithinAbs (120.0, 1.0e-9));

    // One late tap moves the average only a little.
    taps.tap (1000.0);
    taps.tap (1500.0);
    bpm = taps.tap (2060.0);
    CHECK (*bpm > 113.0);
    CHECK (*bpm < 120.0);

    // A long pause: the next tap is the first of a new count.
    CHECK_FALSE (taps.tap (6000.0).has_value());
    CHECK_THAT (*taps.tap (6600.0), WithinAbs (100.0, 1.0e-9));

    // Absurdly fast tapping is limited to something a delay can use.
    TapTempo fast;
    fast.tap (0.0);
    CHECK_THAT (*fast.tap (10.0), WithinAbs (TapTempo::maxBpm, 1.0e-9));
}

TEST_CASE ("Undo history steps back and forward and forgets redo after a new edit", "[undo]")
{
    UndoHistory<int> history;
    history.record (1);
    CHECK_FALSE (history.canUndo());
    history.record (2);
    history.record (2);   // no change: no extra step
    history.record (3);

    CHECK (*history.undo() == 2);
    CHECK (*history.undo() == 1);
    CHECK_FALSE (history.undo().has_value());
    CHECK (*history.redo() == 2);

    history.record (9);   // a new edit after undoing
    CHECK_FALSE (history.canRedo());
    CHECK (*history.undo() == 2);

    for (int i = 0; i < 500; ++i)
        history.record (100 + i);
    int steps = 0;
    while (history.undo())
        ++steps;
    CHECK (steps == (int) UndoHistory<int>::maxSteps - 1);
}

TEST_CASE ("Undo and redo take back whole knob moves", "[undo]")
{
    TestRig rig;
    rig.turn (ParamIDs::eqLevel, 8.0f);
    rig.turn (ParamIDs::presence, 6.0f);
    REQUIRE (rig.processor.canUndo());

    rig.processor.undo();
    CHECK_THAT (rig.value (ParamIDs::presence), WithinAbs (0.0, 1.0e-4));   // back to the default
    CHECK_THAT (rig.value (ParamIDs::eqLevel), WithinAbs (8.0, 1.0e-4));   // untouched

    rig.processor.undo();
    CHECK_THAT (rig.value (ParamIDs::eqLevel), WithinAbs (0.0, 1.0e-4));
    CHECK_FALSE (rig.processor.canUndo());

    rig.processor.redo();
    rig.processor.redo();
    CHECK_THAT (rig.value (ParamIDs::eqLevel), WithinAbs (8.0, 1.0e-4));
    CHECK_THAT (rig.value (ParamIDs::presence), WithinAbs (6.0, 1.0e-4));
    CHECK_FALSE (rig.processor.canRedo());
}

TEST_CASE ("Undo doesn't take back the global knobs or the pedal position", "[undo]")
{
    TestRig rig;
    rig.turn (ParamIDs::eqLevel, 7.0f);
    rig.turn (ParamIDs::outputGain, -6.0f);
    rig.turn (ParamIDs::expression, 0.3f);

    rig.processor.undo();
    CHECK_THAT (rig.value (ParamIDs::eqLevel), WithinAbs (0.0, 1.0e-4));
    CHECK_THAT (rig.value (ParamIDs::outputGain), WithinAbs (-6.0, 1.0e-4));
    CHECK_THAT (rig.value (ParamIDs::expression), WithinAbs (0.3, 1.0e-3));
}

TEST_CASE ("Undoing a rig change brings back the previous rig and its name", "[undo][rigs]")
{
    TestRig rig;
    rig.turn (ParamIDs::presence, 5.0f);

    const auto* init = rig.processor.getRigLibrary().findFactory ("Init");
    REQUIRE (init != nullptr);
    rig.processor.loadRig (*init);
    rig.processor.flushUndoStep();
    CHECK (rig.processor.getCurrentRig().name == "Init");
    CHECK_FALSE (rig.processor.isRigModified());

    rig.processor.undo();
    CHECK (rig.processor.getCurrentRig().name == "Untitled");
    CHECK_THAT (rig.value (ParamIDs::presence), WithinAbs (5.0, 1.0e-4));
    CHECK (rig.processor.isRigModified());   // it was edited before the rig change

    rig.processor.redo();
    CHECK (rig.processor.getCurrentRig().name == "Init");
    CHECK_FALSE (rig.processor.isRigModified());
}

TEST_CASE ("Tapped tempo replaces the DAW's until cleared, and is saved", "[tap]")
{
    TestRig rig;
    CHECK_THAT (rig.processor.getTempoBpm(), WithinAbs (120.0, 1.0e-9));   // no DAW tempo: 120

    rig.processor.tapTempo();
    juce::Thread::sleep (400);
    rig.processor.tapTempo();
    const auto tapped = rig.processor.getTappedBpm();
    CHECK (tapped > 100.0);    // ~150, allowing for a slow test machine
    CHECK (tapped < 160.0);
    CHECK_THAT (rig.processor.getTempoBpm(), WithinAbs (tapped, 1.0e-9));

    juce::MemoryBlock state;
    rig.processor.getStateInformation (state);
    rig.processor.clearTappedTempo();
    CHECK_THAT (rig.processor.getTempoBpm(), WithinAbs (120.0, 1.0e-9));

    rig.processor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_THAT (rig.processor.getTappedBpm(), WithinAbs (tapped, 1.0e-9));
}

TEST_CASE ("The tuner never changes the sound: no mute, so you hear the recording you're checking", "[tuner]")
{
    // The same tone, played through two plugins, one with the tuner on:
    // every sample comes out the same.
    TestRig off, on;
    on.turn (ParamIDs::tunerOn, 1.0f);

    juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 40; ++block)
    {
        for (auto* buffer : { &a, &b })
        {
            buffer->clear();
            for (int i = 0; i < blockSize; ++i)
                buffer->setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (block * blockSize + i) / sampleRate));
        }
        off.processor.processBlock (a, midi);
        on.processor.processBlock (b, midi);
        on.processor.updateTuner();
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (a.getSample (channel, i) == b.getSample (channel, i));
    }
    CHECK (b.getMagnitude (0, 0, blockSize) > 0.25f);
    CHECK (on.processor.getTuner().getReading().hasNote);   // and it was listening
}

TEST_CASE ("A 0.1.0 project saved with the tuner on and muting plays out loud", "[tuner][state]")
{
    // 0.1.0 had a MUTE for the tuner, on by default. A project saved with it
    // (and the tuner left on) must not come back silent.
    juce::MemoryBlock saved;
    {
        TestRig old;
        old.turn (ParamIDs::tunerOn, 1.0f);
        old.processor.getStateInformation (saved);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    auto* mute = xml->createNewChildElement ("PARAM");
    mute->setAttribute ("id", "tunerMute");
    mute->setAttribute ("value", 1.0);
    juce::MemoryBlock oldProject;
    juce::AudioProcessor::copyXmlToBinary (*xml, oldProject);

    TestRig rig;
    const auto normal = rig.play (0.3);
    rig.processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
    CHECK (rig.processor.getParameters().getRawParameterValue (ParamIDs::tunerOn)->load() > 0.5f);
    CHECK_THAT (rig.play (0.3), WithinAbs (normal, 0.01));
}

TEST_CASE ("Expression pedal as a volume pedal, within its MIN and MAX", "[expression]")
{
    TestRig rig;
    const auto full = rig.play (0.3);

    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::volume);
    CHECK (rig.play (0.2) < 1.0e-4f);                       // heel down by default: silent, as a real volume pedal

    rig.turn (ParamIDs::expression, 1.0f);
    CHECK_THAT (rig.play (0.2), WithinAbs (full, 0.005));   // toe down: full volume

    rig.turn (ParamIDs::expression, 0.5f);
    CHECK_THAT (rig.play (0.2), WithinAbs (full * 0.25, 0.01));   // audio taper: half way is -12 dB

    rig.turn (ParamIDs::expressionMin, 0.5f);
    rig.turn (ParamIDs::expression, 0.0f);
    CHECK_THAT (rig.play (0.2), WithinAbs (full * 0.25, 0.01));   // heel now stops at half volume
}

TEST_CASE ("MIDI CC 11 moves the expression pedal", "[expression]")
{
    TestRig rig;
    const auto full = rig.play (0.3);   // before assigning VOLUME: the pedal starts heel down, which would be silent
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::volume);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 0), 0);
    CHECK (rig.play (0.2, midi) < 1.0e-4f);
    CHECK_THAT (rig.processor.getExpressionPosition(), WithinAbs (0.0, 1.0e-6));

    // Other controllers are ignored.
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 7, 127), 0);
    CHECK (rig.play (0.1, midi) < 1.0e-4f);

    midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 127), 0);
    CHECK_THAT (rig.play (0.2, midi), WithinAbs (full, 0.005));
}

TEST_CASE ("A project from before 0.3.0 with the pedal on an effect that's gone loads unassigned, not silent", "[expression][state]")
{
    // 0.1.0 and 0.2.0 could assign the pedal to PR7-B's effects (WAH was 2,
    // TALK BOX 8). Those are gone; a project that used one must not come
    // back as a VOLUME pedal sitting at the heel, which would be silent.
    juce::MemoryBlock saved;
    {
        TestRig old;
        old.processor.getStateInformation (saved);
    }
    for (const auto oldTarget : { 2, 8 })
    {
        INFO ("Old target " << oldTarget);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        REQUIRE (xml != nullptr);
        xml->removeAttribute ("parameterVersion");   // as 0.1.0 and 0.2.0 wrote it
        for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
            if (param->getStringAttribute ("id") == ParamIDs::expressionTarget)
                param->setAttribute ("value", oldTarget);
        juce::MemoryBlock oldProject;
        juce::AudioProcessor::copyXmlToBinary (*xml, oldProject);

        TestRig rig;
        const auto normal = rig.play (0.3);
        rig.processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
        CHECK (juce::roundToInt (rig.value (ParamIDs::expressionTarget)) == ExpressionTargets::off);
        CHECK_THAT (rig.play (0.3), WithinAbs (normal, 0.01));
    }

    // A project saved now keeps VOLUME as VOLUME.
    TestRig rig;
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::volume);
    juce::MemoryBlock current;
    rig.processor.getStateInformation (current);
    TestRig restored;
    restored.processor.setStateInformation (current.getData(), (int) current.getSize());
    CHECK (juce::roundToInt (restored.value (ParamIDs::expressionTarget)) == ExpressionTargets::volume);
}

TEST_CASE ("SETTINGS: the pedal's MIDI CC and the tuner's reference are saved with the project", "[expression][settings]")
{
    TestRig rig;
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::volume);
    rig.turn (ParamIDs::expression, 1.0f);   // toe down, so there's sound until CC 4 moves it
    rig.processor.setExpressionCc (4);
    rig.processor.setTunerReferenceHz (432.0);

    // CC 4 now moves the pedal; CC 11 no longer does.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 0), 0);
    CHECK (rig.play (0.2, midi) > 0.05f);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 4, 0), 0);
    CHECK (rig.play (0.2, midi) < 1.0e-4f);

    juce::MemoryBlock state;
    rig.processor.getStateInformation (state);
    ParagonX1AudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.getExpressionCc() == 4);
    CHECK_THAT (restored.getTunerReferenceHz(), WithinAbs (432.0, 1.0e-9));

    // Out-of-range values are kept sensible.
    restored.setExpressionCc (500);
    CHECK (restored.getExpressionCc() == 127);
}
