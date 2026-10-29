// THE DRUM SEQUENCER (stage 6b) in the plugin: SONG following the DAW, KEY's pattern keys taken from the synth, the
// patterns kept by presets, the project and undo, reversed synth drums, and MIDI export. (The sequencer itself:
// DrumSeqTests.cpp.)
#include "SynthTestUtils.h"

#include "PluginProcessor.h"

namespace
{
    struct Song final : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            return info;
        }
    };

    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        Song song;
        Plugin()
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            processor.setPlayHead (&song);
            processor.seedRandomForTest (1);
            set (ParamIDs::drumsOn, 1.0f);
            set (ParamIDs::dseqOn, 1.0f);
        }
        void set (const juce::String& id, float value)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
        void addNote (int pattern, int row, double start, synth::drumseq::Reverse reverse = synth::drumseq::Reverse::off)
        {
            auto m = processor.getModMatrix();
            synth::drumseq::Note n;
            n.row = row;
            n.start = start;
            n.reverse = reverse;
            m.drumPatterns[(size_t) pattern].notes.push_back (n);
            processor.setModMatrix (m);
        }
        // Plays `seconds` with these keys (note, on at, off at: seconds), the song moving on if it's playing.
        std::vector<float> play (double seconds, std::vector<std::tuple<int, double, double>> keys = {})
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            const auto blocks = (int) (seconds * sampleRate) / blockSize;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                for (const auto& [note, on, off] : keys)
                {
                    const auto onAt = (int) (on * sampleRate) - b * blockSize, offAt = (int) (off * sampleRate) - b * blockSize;
                    if (onAt >= 0 && onAt < blockSize)
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), onAt);
                    if (offAt >= 0 && offAt < blockSize)
                        midi.addEvent (juce::MidiMessage::noteOff (1, note), offAt);
                }
                buffer.clear();
                processor.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
                if (song.playing)
                    song.ppq += blockSize / sampleRate * song.bpm / 60.0;
            }
            return out;
        }
    };

    double peakOf (const std::vector<float>& x, double from = 0.0, double to = 1.0e9)
    {
        auto peak = 0.0f;
        for (auto i = (size_t) (from * sampleRate); i < std::min (x.size(), (size_t) (to * sampleRate)); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }

    int firstSound (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != 0.0f)
                return (int) i;
        return -1;
    }
}

TEST_CASE ("DRUM SEQUENCER in the plugin: SONG plays the pattern with the DAW, on its beat; stopped, nothing", "[drumseqplugin]")
{
    Plugin p;
    p.addNote (0, 2, 4.0);   // the snare on beat 2 (step 4 of 1/16s)
    const auto played = p.play (1.0);
    CHECK (firstSound (played) == 24000);   // half a second in, at 120 BPM
    CHECK (peakOf (played, 0.5, 0.6) > 0.1);

    // From beat 1 on: it's right there.
    Plugin later;
    later.addNote (0, 2, 4.0);
    later.song.ppq = 1.0;
    CHECK (firstSound (later.play (0.2)) == 0);

    // At 90 BPM, beat 2 is 2/3 s in.
    Plugin slower;
    slower.addNote (0, 2, 4.0);
    slower.song.bpm = 90.0;
    CHECK (std::abs (firstSound (slower.play (1.0)) - 32000) <= 1);   // (the test's song position adds up a hair short)

    // Stopped, or the sequencer or the kit off: silence.
    Plugin stopped;
    stopped.addNote (0, 2, 0.0);
    stopped.song.playing = false;
    CHECK (peakOf (stopped.play (0.5)) == 0.0);
    Plugin off;
    off.addNote (0, 2, 0.0);
    off.set (ParamIDs::dseqOn, 0.0f);
    CHECK (peakOf (off.play (0.5)) == 0.0);
    off.set (ParamIDs::dseqOn, 1.0f);
    off.set (ParamIDs::drumsOn, 0.0f);
    CHECK (peakOf (off.play (0.5)) == 0.0);

    // PATTERN chooses which plays.
    Plugin b;
    b.addNote (1, 2, 0.0);
    CHECK (peakOf (b.play (0.3)) == 0.0);
    b.set (ParamIDs::dseqPattern, 1.0f);
    CHECK (peakOf (b.play (2.0), 1.7, 1.8) > 0.1);   // B at the next bar (beat 4: 1.7 s on from beat 0.6)
}

TEST_CASE ("DRUM SEQUENCER in the plugin: KEY: the four keys under the kit play patterns, not the synth", "[drumseqplugin]")
{
    Plugin p;
    p.set (ParamIDs::dseqTrigger, 1.0f);
    p.song.playing = false;
    p.addNote (1, 2, 0.0);   // B: the snare at once
    // A (key 32) is empty: silence, so the synth didn't get the key either.
    CHECK (peakOf (p.play (0.4, { { 32, 0.1, 0.3 } })) == 0.0);
    // B (key 33): the snare from the key, each bar while held.
    const auto played = p.play (4.5, { { 33, 0.1, 2.2 } });
    CHECK (firstSound (played) == (int) (0.1 * sampleRate));
    CHECK (peakOf (played, 2.1, 2.15) > 0.05);   // the next bar, 2 s on
    CHECK (peakOf (played, 4.05) < 1.0e-4);      // let go at 2.2 s: no bar after (the last hit rang out by then)

    // On SONG, those keys play the synth as ever.
    Plugin song;
    song.song.playing = false;
    CHECK (peakOf (song.play (0.3, { { 33, 0.0, 0.2 } })) > 0.01);
}

TEST_CASE ("DRUM SEQUENCER in the plugin: the patterns are kept by presets, the project and undo", "[drumseqplugin]")
{
    Plugin p;
    p.processor.flushUndoStep();
    p.addNote (0, 0, 0.0);
    p.addNote (3, 11, 6.5, synth::drumseq::Reverse::endSync);
    p.processor.modMatrixEditFinished();
    p.processor.flushUndoStep();
    const auto rig = p.processor.captureRig();
    const auto reread = Rig::fromXml (*rig.toXml());
    REQUIRE (reread.has_value());
    CHECK (reread->modulation.drumPatterns == p.processor.getModMatrix().drumPatterns);
    CHECK (reread->modulation.drumPatterns[3].notes.size() == 1);

    juce::MemoryBlock state;
    p.processor.getStateInformation (state);
    Plugin other;
    other.processor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.processor.getModMatrix().drumPatterns == p.processor.getModMatrix().drumPatterns);

    auto cleared = p.processor.getModMatrix();
    cleared.drumPatterns[3].notes.clear();
    p.processor.setModMatrix (cleared);
    p.processor.modMatrixEditFinished();
    p.processor.flushUndoStep();
    CHECK (p.processor.getModMatrix().drumPatterns[3].isEmpty());
    p.processor.undo();
    CHECK (p.processor.getModMatrix().drumPatterns[3].notes.size() == 1);
    // A new preset has none.
    CHECK (ModMatrix {}.drumPatterns.size() == 4);
    CHECK (ModMatrix {}.isEmpty());
}

TEST_CASE ("DRUM SEQUENCER in the plugin: a reversed synth drum plays its hit backwards, swelling to its end", "[drumseqplugin]")
{
    Plugin p;
    p.addNote (0, 2, 0.0, synth::drumseq::Reverse::reverse);   // the snare
    CHECK (p.processor.getKitSettings().slots[2].reverseSource == nullptr);   // made by the timer
    p.processor.updateDrumReverses();
    const auto* source = p.processor.getKitSettings().slots[2].reverseSource;
    REQUIRE (source != nullptr);
    const auto seconds = source->length() / sampleRate;
    CHECK (seconds > 0.1);
    CHECK (seconds < 1.5);
    // Only reversed rows get one.
    CHECK (p.processor.getKitSettings().slots[0].reverseSource == nullptr);

    const auto played = p.play (seconds + 0.2);
    CHECK (peakOf (played, 0.0, 0.03) < 0.2 * peakOf (played, seconds - 0.03, seconds));
    CHECK (peakOf (played, seconds + 0.01) < 1.0e-6);

    // Changing the drum makes it again.
    p.set (ParamIDs::drum (2, "Decay"), 0.1f);
    p.processor.updateDrumReverses();
    CHECK (p.processor.getKitSettings().slots[2].reverseSource->length() < source->length());
}

TEST_CASE ("DRUM SEQUENCER in the plugin: MIDI export puts the pattern on the kit's keys", "[drumseqplugin]")
{
    Plugin p;
    p.set (ParamIDs::drumsRoot, 48.0f);
    p.addNote (2, 0, 0.0);
    p.addNote (2, 3, 2.0);
    const auto file = p.processor.drumPatternToMidi (2);
    std::vector<std::pair<int, double>> ons;
    for (const auto* e : *file.getTrack (0))
        if (e->message.isNoteOn())
            ons.emplace_back (e->message.getNoteNumber(), e->message.getTimeStamp());
    CHECK (ons == std::vector<std::pair<int, double>> { { 48, 0.0 }, { 51, 480.0 } });
    const auto drag = p.processor.drumMidiDragFile (2);
    CHECK (drag.existsAsFile());
    CHECK (drag.getFileName() == "Drums C.mid");
}
