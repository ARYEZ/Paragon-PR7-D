#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/oracle/Oracle.h"

using Catch::Matchers::WithinAbs;

namespace
{
    double hzOf (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

    // A DAW's transport: where the song is, its tempo, and whether it's playing.
    struct Transport final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setTimeInSamples (sample);
            info.setTimeInSeconds ((double) sample / sampleRate);
            info.setBpm (bpm);
            info.setPpqPosition ((double) sample / sampleRate * bpm / 60.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { numerator, 4 });
            info.setIsPlaying (playing);
            return info;
        }

        double sampleRate = 48000.0, bpm = 120.0;
        juce::int64 sample = 0;
        int numerator = 4;
        bool playing = true;
    };

    // One sung (or played) note: a fundamental and a few harmonics, with
    // 5 ms fades in and out, and optional vibrato.
    struct Sung
    {
        double start, end, midi, level = 0.25, vibratoCents = 0.0;
    };

    // ORACLE on, in the plugin, played a part the way a DAW does
    // (512-sample blocks at 48 kHz, ORACLE stepping as its timer would).
    struct OracleRig
    {
        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 512;

        OracleRig()
        {
            processor.setPlayConfigDetails (1, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            processor.setPlayHead (&transport);
            setOn (true);
        }

        ~OracleRig() { processor.setPlayHead (nullptr); }

        void setOn (bool on)
        {
            processor.getParameters().getParameter (ParamIDs::tunerOn)->setValueNotifyingHost (on ? 1.0f : 0.0f);
        }

        // Plays the song from `from` to `to` (seconds), with the notes in `part` sounding.
        void play (double from, double to, const std::vector<Sung>& part)
        {
            transport.sample = (juce::int64) std::llround (from * sampleRate);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            std::vector<double> phases (part.size(), 0.0);
            for (int block = 0; transport.sample < (juce::int64) std::llround (to * sampleRate); ++block)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto t = (double) (transport.sample + i) / sampleRate;
                    double value = 0.0;
                    for (size_t n = 0; n < part.size(); ++n)
                    {
                        const auto& s = part[n];
                        if (t < s.start || t >= s.end)
                            continue;
                        const auto vibrato = s.vibratoCents / 100.0 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * (t - s.start));
                        phases[n] += juce::MathConstants<double>::twoPi * hzOf (s.midi + vibrato) / sampleRate;
                        const auto fade = juce::jmin (1.0, (t - s.start) / 0.005, (s.end - t) / 0.005);
                        value += s.level * fade * (std::sin (phases[n]) + 0.5 * std::sin (2.0 * phases[n]) + 0.2 * std::sin (3.0 * phases[n]));
                    }
                    buffer.setSample (0, i, (float) value);
                    buffer.setSample (1, i, 0.0f);
                }
                processor.processBlock (buffer, midi);
                if (transport.playing)
                    transport.sample += blockSize;
                else
                    stoppedFor += blockSize;
                if (block % 3 == 2)
                    processor.updateOracle();
                if (! transport.playing && stoppedFor > (juce::int64) ((to - from) * sampleRate))
                    break;
            }
            processor.updateOracle();
        }

        const std::vector<oracle::Note>& notes() { return processor.getOracle().getNotes(); }

        Transport transport;
        ParagonX1AudioProcessor processor;
        juce::int64 stoppedFor = 0;
    };

    std::vector<int> pitches (const std::vector<oracle::Note>& notes)
    {
        std::vector<int> result;
        for (const auto& n : notes)
            result.push_back (n.pitch);
        return result;
    }

    // A phrase with the things that make transcription hard.
    const std::vector<Sung> phrase {
        { 2.00, 2.50, 60 },                     // C4
        { 2.50, 3.00, 64 },                     // E4, legato from the C (no gap)
        { 3.10, 3.60, 67 },                     // G4, after a short gap
        { 3.60, 4.60, 69, 0.25, 50.0 },         // A4, legato, with wide vibrato (+-50 cents, 5.5 a second)
        { 4.80, 5.05, 62 },                     // D4 twice, with a 50 ms gap
        { 5.10, 5.35, 62 },
        { 5.50, 5.80, 62, 0.04 },               // D4 again: quietly, then a fresh attack with no gap
        { 5.80, 6.10, 62, 0.30 },
    };
}

TEST_CASE ("ORACLE: finds a sung phrase's notes, where they start and end, as the DAW plays it", "[oracle]")
{
    OracleRig rig;
    rig.play (1.5, 6.6, phrase);

    const auto& notes = rig.notes();
    REQUIRE (pitches (notes) == std::vector<int> { 60, 64, 67, 69, 62, 62, 62, 62 });
    for (size_t i = 0; i < notes.size(); ++i)
    {
        INFO ("note " << i << ": " << notes[i].start << " to " << notes[i].end << " s");
        // Legato changes are judged on the pitch alone, so they're allowed a little more.
        const auto legatoStart = i == 1 || i == 3 || i == 7;
        const auto legatoEnd = i == 0 || i == 2 || i == 6;
        CHECK_THAT (notes[i].start, WithinAbs (phrase[i].start, legatoStart ? 0.04 : 0.025));
        CHECK_THAT (notes[i].end, WithinAbs (phrase[i].end, legatoEnd ? 0.04 : 0.025));
        CHECK (std::abs (notes[i].cents) < 10.0f);   // the vibrato note too: its middle, not its swings
    }

    // Velocity follows how loud each note was.
    CHECK (notes[6].velocity < notes[7].velocity - 30);
    CHECK (notes[0].velocity > 80);
}

TEST_CASE ("ORACLE: how far from the note it was sung, low notes, and the MIN NOTE length", "[oracle]")
{
    OracleRig rig;

    SECTION ("A note sung 30 cents sharp is still that note, and says so")
    {
        rig.play (0.0, 1.5, { { 0.2, 1.2, 64.3 } });
        REQUIRE (pitches (rig.notes()) == std::vector<int> { 64 });
        CHECK_THAT (rig.notes()[0].cents, WithinAbs (30.0, 3.0));
    }

    SECTION ("A bass line's low notes")
    {
        rig.play (0.0, 2.6, { { 0.2, 1.2, 28 }, { 1.2, 2.2, 33 } });   // E1 (41 Hz), then A1
        CHECK (pitches (rig.notes()) == std::vector<int> { 28, 33 });
    }

    SECTION ("A blip shorter than MIN NOTE isn't a note")
    {
        rig.play (0.0, 1.6, { { 0.2, 0.6, 67 }, { 0.8, 0.84, 72 }, { 1.0, 1.4, 67 } });
        CHECK (pitches (rig.notes()) == std::vector<int> { 67, 67 });

        // With MIN NOTE down to 30 ms it is (and that's one undo step).
        auto settings = rig.processor.getOracle().getSettings();
        settings.minNoteMs = 30.0f;
        rig.processor.getOracle().setSettings (settings);
        CHECK (pitches (rig.notes()) == std::vector<int> { 67, 72, 67 });
        rig.processor.getOracle().undo();
        CHECK (pitches (rig.notes()) == std::vector<int> { 67, 67 });
    }
}

TEST_CASE ("ORACLE: hears only while it's on and the DAW plays; playing a passage again replaces it", "[oracle]")
{
    OracleRig rig;

    SECTION ("Stopped, or off: nothing")
    {
        rig.transport.playing = false;
        rig.play (0.0, 1.5, { { 0.0, 1.5, 60 } });
        CHECK (rig.processor.getOracle().getFrames().empty());

        rig.transport.playing = true;
        rig.setOn (false);
        rig.play (0.0, 1.5, { { 0.2, 1.2, 60 } });
        CHECK (rig.notes().empty());
    }

    SECTION ("A second take of 2 to 4 s replaces just that")
    {
        rig.play (0.0, 6.0, { { 0.5, 1.5, 60 }, { 2.5, 3.5, 62 }, { 4.5, 5.5, 64 } });
        REQUIRE (pitches (rig.notes()) == std::vector<int> { 60, 62, 64 });

        rig.play (2.0, 4.0, { { 2.2, 2.8, 67 }, { 3.0, 3.8, 65 } });
        CHECK (pitches (rig.notes()) == std::vector<int> { 60, 67, 65, 64 });

        // And one undo takes the second take's notes back.
        rig.processor.getOracle().undo();
        CHECK (pitches (rig.notes()) == std::vector<int> { 60, 62, 64 });
    }
}

TEST_CASE ("ORACLE: the MIDI file lines up with the song from bar 1, at its tempo", "[oracle]")
{
    OracleRig rig;
    rig.transport.bpm = 90.0;
    rig.transport.numerator = 3;
    rig.play (1.0, 6.6, phrase);
    REQUIRE (rig.notes().size() == 8);

    const auto quarter = 60.0 / 90.0;   // seconds
    const auto events = [] (const juce::MidiFile& file)
    {
        std::vector<const juce::MidiMessage*> on;
        for (const auto* holder : *file.getTrack (0))
            if (holder->message.isNoteOn())
                on.push_back (&holder->message);
        return on;
    };

    SECTION ("As sung")
    {
        const auto file = rig.processor.getOracle().toMidi();
        CHECK (file.getTimeFormat() == 960);
        const auto* track = file.getTrack (0);
        bool tempo = false, signature = false;
        for (const auto* holder : *track)
        {
            if (holder->message.isTempoMetaEvent())
                tempo = tempo || (juce::exactlyEqual (holder->message.getTimeStamp(), 0.0)
                                  && std::abs (holder->message.getTempoSecondsPerQuarterNote() - quarter) < 1.0e-4);
            int numerator = 0, denominator = 0;
            if (holder->message.isTimeSignatureMetaEvent())
            {
                holder->message.getTimeSignatureInfo (numerator, denominator);
                signature = numerator == 3 && denominator == 4;
            }
        }
        CHECK (tempo);
        CHECK (signature);

        const auto on = events (file);
        REQUIRE (on.size() == 8);
        for (size_t i = 0; i < on.size(); ++i)
        {
            INFO ("note " << i);
            CHECK (on[i]->getNoteNumber() == rig.notes()[i].pitch);
            CHECK_THAT (on[i]->getTimeStamp() / 960.0, WithinAbs (phrase[i].start / quarter, 0.07));   // within 40 ms
        }
        // One note at a time: each ends by the time the next starts.
        juce::MidiMessageSequence sequence (*track);
        sequence.updateMatchedPairs();
        for (int i = 0; i < sequence.getNumEvents(); ++i)
            if (sequence.getEventPointer (i)->message.isNoteOn())
                for (int j = i + 1; j < sequence.getNumEvents(); ++j)
                    if (sequence.getEventPointer (j)->message.isNoteOn())
                    {
                        CHECK (sequence.getTimeOfMatchingKeyUp (i) <= sequence.getEventPointer (j)->message.getTimeStamp());
                        break;
                    }
    }

    SECTION ("Quantized to 1/16: every note starts and ends on the grid")
    {
        rig.processor.getOracle().setQuantize (2);
        const auto file = rig.processor.getOracle().toMidi();
        for (const auto* holder : *file.getTrack (0))
            if (holder->message.isNoteOnOrOff())
                CHECK (std::fmod (holder->message.getTimeStamp(), 240.0) < 0.5);
        const auto on = events (file);
        REQUIRE (on.size() == 8);
        CHECK_THAT (on[0]->getTimeStamp(), WithinAbs (std::round (2.0 / quarter * 4.0) * 240.0, 0.5));
    }

    SECTION ("Written to a file a DAW can read back")
    {
        const auto folder = juce::File::createTempFile ("oracle");
        const auto written = rig.processor.getOracle().writeMidiFile (folder);
        REQUIRE (written.existsAsFile());
        CHECK (written.hasFileExtension ("mid"));
        juce::FileInputStream in (written);
        juce::MidiFile read;
        REQUIRE (read.readFrom (in));
        CHECK (events (read).size() == 8);
        folder.deleteRecursively();
    }
}

TEST_CASE ("ORACLE: fixing the notes, with undo and redo", "[oracle]")
{
    oracle::Transcriber transcriber;
    transcriber.setNotes ({ { 0.0, 1.0, 60, 90 }, { 1.0, 2.0, 62, 90 }, { 2.0, 3.0, 64, 90 } }, false);
    const auto id = [&] (size_t i) { return transcriber.getNotes()[i].id; };
    CHECK_FALSE (transcriber.canUndo());

    transcriber.transpose ({ id (1) }, 3);
    CHECK (pitches (transcriber.getNotes()) == std::vector<int> { 60, 65, 64 });

    const auto right = transcriber.split (id (0), 0.4);
    REQUIRE (right != 0);
    CHECK (pitches (transcriber.getNotes()) == std::vector<int> { 60, 60, 65, 64 });
    CHECK_THAT (transcriber.getNotes()[0].end, WithinAbs (0.4, 1.0e-9));
    CHECK_THAT (transcriber.find (right)->start, WithinAbs (0.4, 1.0e-9));

    transcriber.join ({ id (2), id (3) });   // 65 and 64: one note, at the first's pitch
    CHECK (pitches (transcriber.getNotes()) == std::vector<int> { 60, 60, 65 });
    CHECK_THAT (transcriber.getNotes()[2].end, WithinAbs (3.0, 1.0e-9));

    transcriber.remove ({ id (0) });
    CHECK (pitches (transcriber.getNotes()) == std::vector<int> { 60, 65 });
    transcriber.add (3.5, 4.0, 67);
    CHECK (pitches (transcriber.getNotes()) == std::vector<int> { 60, 65, 67 });

    for (int i = 0; i < 5; ++i)
        transcriber.undo();
    CHECK (pitches (transcriber.getNotes()) == std::vector<int> { 60, 62, 64 });
    CHECK_FALSE (transcriber.canUndo());
    transcriber.redo();
    transcriber.redo();
    CHECK (pitches (transcriber.getNotes()) == std::vector<int> { 60, 60, 65, 64 });
}

TEST_CASE ("ORACLE: the transcription is saved with the DAW project", "[oracle]")
{
    OracleRig rig;
    rig.play (1.5, 6.6, phrase);
    rig.processor.getOracle().transpose ({ rig.notes()[0].id }, 12);   // an edit
    rig.processor.getOracle().setQuantize (4);
    const auto saved = rig.notes();
    REQUIRE (saved.size() == 8);

    juce::MemoryBlock state;
    rig.processor.getStateInformation (state);

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    const auto& notes = restored.getOracle().getNotes();
    REQUIRE (notes.size() == saved.size());
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CHECK (notes[i].pitch == saved[i].pitch);
        CHECK_THAT (notes[i].start, WithinAbs (saved[i].start, 1.0e-9));
        CHECK_THAT (notes[i].end, WithinAbs (saved[i].end, 1.0e-9));
        CHECK (notes[i].velocity == saved[i].velocity);
    }
    CHECK (notes[0].pitch == 72);
    CHECK (restored.getOracle().getQuantize() == 4);
    CHECK (restored.getOracle().getFrames().size() == rig.processor.getOracle().getFrames().size());
    CHECK_THAT (restored.getOracle().getTempo().ppqAt (3.0), WithinAbs (6.0, 1.0e-6));
    // ...and it didn't get into the parameters.
    CHECK_FALSE (restored.getParameters().state.getChildWithName ("Oracle").isValid());
}

TEST_CASE ("ORACLE: a long line sung without a breath keeps every note", "[oracle]")
{
    // 100 legato notes (no gaps), 0.4 s each: ORACLE reads only the newest
    // part of a long phrase again as it plays, so check nothing is lost or
    // doubled where it starts reading.
    std::vector<Sung> line;
    std::vector<int> expected;
    for (int i = 0; i < 100; ++i)
    {
        const auto pitch = 57 + (i * 5) % 12;
        line.push_back ({ 1.0 + i * 0.4, 1.4 + i * 0.4, (double) pitch, 0.2, i % 4 == 0 ? 30.0 : 0.0 });
        expected.push_back (pitch);
    }
    OracleRig rig;
    rig.play (0.5, 41.5, line);
    CHECK (pitches (rig.notes()) == expected);
}
