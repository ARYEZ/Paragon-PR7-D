// THE SONG TRACK (PR7-D 0.8.0): blocks placed by our rules (a drop inside a block goes to its nearer edge and the rest
// move along, lengthening pushes, shortening leaves a gap), each block's groove repeated to fill it, played with the DAW's
// song position (each hit on its sample) or with PLAY, MUTE, CHANGE TEMPO, undo, kept in the project, EXPORT MIDI, and
// the strip's drops and edits.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"

#include "ui/GroovesPage.h"
#include "ui/SongTrack.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    using namespace synth::drums::notemap;

    // A block of `bars` 4/4 bars with `hits` (beat, slot).
    song::Block block (const juce::String& name, int bars, std::vector<std::pair<double, int>> hits = {})
    {
        song::Block b;
        b.name = name;
        b.length = b.loopLength = bars * 4.0;
        for (const auto& [beat, slot] : hits)
            b.hits.push_back ({ beat, slot, 1.0f });
        return b;
    }

    std::vector<double> starts (const song::Song& s)
    {
        std::vector<double> out;
        for (const auto& b : s.blocks)
            out.push_back (b.start);
        return out;
    }

    juce::StringArray names (const song::Song& s)
    {
        juce::StringArray out;
        for (const auto& b : s.blocks)
            out.add (b.name);
        return out;
    }

    struct Host final : juce::AudioPlayHead
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
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonSongTests");
        ParagonX1AudioProcessor p;
        Host host;
        Plugin()
        {
            folder.deleteRecursively();
            p.useRigFolder (folder);
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            // The kick and snare play a short steady burst (20 ms, full from its first sample): each hit shows where it starts.
            const auto burst = testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.5f, 1, sampleRate);
            testkit::useSample (p, BD, burst);
            testkit::useSample (p, SD, burst);
            p.setPlayHead (&host);
        }
        ~Plugin()
        {
            p.setPlayHead (nullptr);
            folder.deleteRecursively();
        }

        // `seconds` of the plugin's output; the DAW's position moves on with it while it plays.
        std::vector<float> play (double seconds)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer none;
            for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
            {
                buffer.clear();
                p.processBlock (buffer, none);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
                if (host.playing)
                    host.ppq += blockSize / sampleRate * host.bpm / 60.0;
            }
            return out;
        }
    };

    // Where each burst starts (a sample after at least 5 ms of silence).
    std::vector<int> onsets (const std::vector<float>& x)
    {
        std::vector<int> result;
        int quiet = 1 << 20;
        for (int i = 0; i < (int) x.size(); ++i)
        {
            if (std::abs (x[(size_t) i]) > 1.0e-6f)
            {
                if (quiet >= 240)
                    result.push_back (i);
                quiet = 0;
            }
            else
                ++quiet;
        }
        return result;
    }

    void writeMidi (const juce::File& f, std::vector<std::pair<double, int>> notes, int bars = 1)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
        for (const auto& [beat, note] : notes)
        {
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100).withTimeStamp (beat * 480.0));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480.0 + 60.0));
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp (bars * 4.0 * 480.0);
        track.addEvent (end);
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        file.addTrack (track);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    }
}

TEST_CASE ("SONG: placing blocks: after the last, inside one (to its nearer edge, the rest moved along), past the end (a gap)", "[song]")
{
    song::Song s;
    s.place (block ("A", 1), s.endBeat());
    s.place (block ("B", 1), s.endBeat());
    s.place (block ("C", 2), s.endBeat());
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0 });

    // Dropped on A's first half: in front of A, everything after moved along a bar.
    CHECK (s.place (block ("D", 1), 1.0) == 0);
    CHECK (names (s).joinIntoString (",") == "D,A,B,C");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0, 12.0 });
    // On B's second half (beat 11): after B.
    CHECK (s.place (block ("E", 1), 11.0) == 3);
    CHECK (names (s).joinIntoString (",") == "D,A,B,E,C");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0, 12.0, 16.0 });
    // Past the end (C ends at 24): to the nearest bar line (beat 29 -> 28), a gap before it.
    CHECK (s.place (block ("F", 1), 29.0) == 5);
    CHECK (s.blocks.back().start == 28.0);
    CHECK (s.endBeat() == 32.0);
}

TEST_CASE ("SONG: editing: longer pushes the blocks after it, shorter leaves a gap; move, duplicate, remove; whole bars", "[song]")
{
    song::Song s;
    for (const auto* n : { "A", "B", "C" })
        s.place (block (n, 1), s.endBeat());
    s.resize (0, 8.6);   // to whole bars: 2
    CHECK (s.blocks[0].length == 8.0);
    CHECK (starts (s) == std::vector<double> { 0.0, 8.0, 12.0 });
    s.resize (0, 4.0);   // shorter: B and C stay
    CHECK (starts (s) == std::vector<double> { 0.0, 8.0, 12.0 });
    s.resize (0, 0.5);   // at least a bar
    CHECK (s.blocks[0].length == 4.0);

    // C moved to the front: A and B move along.
    CHECK (s.move (2, 0.0) == 0);
    CHECK (names (s).joinIntoString (",") == "C,A,B");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0 });
    // Duplicated: the copy right after it, the rest along.
    CHECK (s.duplicate (1) == 2);
    CHECK (names (s).joinIntoString (",") == "C,A,A,B");
    CHECK (starts (s) == std::vector<double> { 0.0, 4.0, 8.0, 12.0 });
    s.remove (0);
    CHECK (names (s).joinIntoString (",") == "A,A,B");
    CHECK (starts (s) == std::vector<double> { 4.0, 8.0, 12.0 });   // removing leaves a gap: nothing moves

    // 3/4: three beats a bar.
    song::Song waltz;
    waltz.numerator = 3;
    CHECK (waltz.beatsPerBar() == 3.0);
    CHECK (waltz.snap (4.0) == 3.0);
    CHECK (waltz.snap (5.0) == 6.0);
}

TEST_CASE ("SONG: each block's groove repeated to fill it, cut at its end; MUTE leaves it out; CHANGE TEMPO", "[song]")
{
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD }, { 2.0, SD } }), 0.0);
    s.resize (0, 12.0);   // three times round
    s.place (block ("B", 1, { { 1.0, BD } }), s.endBeat());
    auto e = s.events();
    REQUIRE (e.size() == 7);
    for (int i = 0; i < 6; ++i)
        CHECK (e[(size_t) i].beat == i * 2.0);
    CHECK (e[6].beat == 13.0);
    CHECK (e[6].slot == BD);

    // Cut part way through the groove: the hits past its end aren't played.
    s.blocks[0].length = 6.0;
    e = s.events();
    CHECK (e.size() == 3 + 1);
    s.blocks[0].muted = true;
    e = s.events();
    REQUIRE (e.size() == 1);
    CHECK (e[0].beat == 13.0);

    // 2x: twice as fast, half as long; 1/2x: the other way.
    auto b = block ("C", 1, { { 0.0, BD }, { 3.0, SD } });
    song::changeTempo (b, grooves::TempoFactor::twice);
    CHECK (b.loopLength == 2.0);
    CHECK (b.length == 2.0);
    CHECK (b.hits[1].beat == 1.5);
    song::changeTempo (b, grooves::TempoFactor::half);
    song::changeTempo (b, grooves::TempoFactor::half);
    CHECK (b.loopLength == 8.0);
    CHECK (b.hits[1].beat == 6.0);
}

TEST_CASE ("SONG: a groove as a block (through the folder's map, the factor and velocity, its SONG PART); XML; MIDI", "[song]")
{
    grooves::Groove g;
    g.name = "Chorus 01";
    g.file = juce::File ("/grooves/Rock/Chorus 01.mid");
    g.lengthBeats = 4.0;
    g.hits = { { 0.0, 36, 1.0f }, { 1.0, 22, 0.5f }, { 2.0, 4, 1.0f } };   // 22: closed hat in the groove library map; 4: no drum
    const auto b = song::blockFrom (g, grooveLibrary(), grooves::TempoFactor::twice, 0.8f);
    CHECK (b.name == "Chorus 01");
    CHECK (b.part == grooves::Part::chorus);
    CHECK (b.loopLength == 2.0);
    REQUIRE (b.hits.size() == 2);
    CHECK (b.hits[0].slot == BD);
    CHECK (b.hits[1].slot == CH);
    CHECK (b.hits[1].beat == 0.5);
    CHECK_THAT (b.hits[1].velocity, WithinAbs (0.4, 1e-6));

    song::Song s;
    s.numerator = 6;
    s.denominator = 8;
    s.place (b, 0.0);
    s.place (block ("Fill", 1, { { 3.5, SD } }), s.endBeat());
    s.blocks[1].muted = true;
    s.blocks[1].part = grooves::Part::fill;
    const auto back = song::Song::fromXml (*s.toXml());
    CHECK (back == s);

    // MIDI: each drum on its note, at 960 ticks a beat, the tempo and signature.
    s.blocks[1].muted = false;
    const auto midi = s.toMidi (100.0, [] (int slot) { return slot == BD ? 36 : slot == SD ? 38 : -1; });   // the hat left out
    const auto* track = midi.getTrack (0);
    REQUIRE (track != nullptr);
    std::vector<std::pair<double, int>> notes;
    auto tempo = 0.0;
    auto num = 0;
    for (const auto* e : *track)
    {
        if (e->message.isNoteOn())
            notes.emplace_back (e->message.getTimeStamp(), e->message.getNoteNumber());
        if (e->message.isTempoMetaEvent())
            tempo = 60.0 / e->message.getTempoSecondsPerQuarterNote();
        if (e->message.isTimeSignatureMetaEvent())
        {
            int d = 0;
            e->message.getTimeSignatureInfo (num, d);
        }
    }
    CHECK_THAT (tempo, WithinAbs (100.0, 0.01));
    CHECK (num == 6);
    // The fill: after the first block's bar of 6/8 (3 beats, the 2-beat groove made a bar long), its snare at 3.5.
    CHECK (s.blocks[1].start == 3.0);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0] == std::make_pair (0.0, 36));
    CHECK (notes[1] == std::make_pair ((3.0 + 3.5) * 960.0, 38));
}

TEST_CASE ("SONG playing with the DAW: each hit on its sample (bar 3 at 120 BPM: 4 s in), from wherever the DAW starts", "[song][song-player]")
{
    Plugin t;
    song::Song s;
    s.place (block ("Silence", 2), 0.0);
    s.place (block ("Beat", 1, { { 0.0, BD }, { 1.5, SD } }), s.endBeat());
    REQUIRE (s.blocks[1].start == 8.0);
    t.p.setSong (s);
    auto o = onsets (t.play (5.0));
    REQUIRE (o.size() == 2);
    CHECK (o[0] == 192000);           // beat 8 at 120 BPM: 4 s
    CHECK (o[1] == 192000 + 36000);   // 1.5 beats later
    CHECK_THAT (t.p.getSongPosition(), WithinAbs (t.host.ppq, 0.02));

    // The DAW started part way in (beat 7.5) at 90 BPM: the kick half a beat in (0.5 x 60 / 90 s: 16000 samples).
    t.host.ppq = 7.5;
    t.host.bpm = 90.0;
    o = onsets (t.play (1.0));
    REQUIRE (! o.empty());
    CHECK (o[0] == 16000);

    // MUTE: nothing.
    s.blocks[1].muted = true;
    t.p.setSong (s);
    t.host.ppq = 7.5;
    CHECK (onsets (t.play (1.5)).empty());
}

TEST_CASE ("SONG PLAY with the DAW stopped: from the place chosen, at the DAW's tempo, stopping at the end", "[song][song-player]")
{
    Plugin t;
    t.host.playing = false;
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD } }), 0.0);
    s.place (block ("B", 1, { { 0.0, SD }, { 2.0, BD } }), s.endBeat());
    t.p.setSong (s);
    CHECK (onsets (t.play (0.5)).empty());   // the DAW stopped, PLAY not pressed: quiet
    CHECK (t.p.getSongPosition() < 0.0);

    t.p.playSong (4.0);   // from bar 2
    CHECK (t.p.isSongPlaying());
    const auto o = onsets (t.play (3.0));
    REQUIRE (o.size() == 2);
    CHECK (o[0] == 0);
    CHECK (o[1] == 48000);   // 2 beats at 120
    CHECK (! t.p.isSongPlaying());   // past the end (beat 8 = 2 s): stopped
    CHECK (t.p.getSongPosition() < 0.0);

    // STOP part way.
    t.p.playSong (0.0);
    t.play (0.2);
    t.p.stopSong();
    CHECK (onsets (t.play (2.0)).empty());

    // The DAW starting ends PLAY: it leads from then on, and stopping it doesn't bring PLAY back.
    t.p.playSong (0.0);
    t.host.playing = true;
    t.host.ppq = 100.0;
    t.play (0.1);
    CHECK (! t.p.isSongPlaying());
    t.host.playing = false;
    CHECK (onsets (t.play (2.0)).empty());
}

TEST_CASE ("SONG: an edit is an undo step; kept in the project (not in presets); EXPORT MIDI on the kit's notes", "[song][song-player]")
{
    Plugin t;
    t.p.flushUndoStep();
    CHECK (t.p.getSong().isEmpty());
    song::Song s;
    s.place (block ("A", 1, { { 0.0, BD }, { 1.0, SD } }), 0.0);
    t.p.setSong (s);
    t.p.flushUndoStep();
    auto longer = s;
    longer.resize (0, 8.0);
    t.p.setSong (longer);
    t.p.flushUndoStep();
    t.p.undo();
    CHECK (t.p.getSong() == s);
    t.p.undo();
    CHECK (t.p.getSong().isEmpty());
    t.p.redo();
    t.p.redo();
    CHECK (t.p.getSong() == longer);

    // The project: saved and back in another instance; a project without a song: none.
    juce::MemoryBlock state;
    t.p.getStateInformation (state);
    {
        ParagonX1AudioProcessor other;
        CHECK (other.getSong().isEmpty());
        other.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (other.getSong() == longer);
        ParagonX1AudioProcessor fresh;
        juce::MemoryBlock empty;
        fresh.getStateInformation (empty);
        other.setStateInformation (empty.getData(), (int) empty.getSize());
        CHECK (other.getSong().isEmpty());
    }
    // Not in presets: a preset loaded keeps the song.
    t.p.loadRig (*t.p.getRigLibrary().findFactory ("Room Kit"));
    CHECK (t.p.getSong() == longer);

    // EXPORT MIDI: the kick and snare on their home notes, twice round (8 beats).
    const auto midi = t.p.songToMidi();
    std::vector<int> notes;
    for (const auto* e : *midi.getTrack (0))
        if (e->message.isNoteOn())
            notes.push_back (e->message.getNoteNumber());
    CHECK (notes == std::vector<int> { t.p.homeNote (BD), t.p.homeNote (SD), t.p.homeNote (BD), t.p.homeNote (SD) });
    CHECK (t.p.songDragFile().existsAsFile());
}

TEST_CASE ("SONG strip: a MIDI file dropped is a block; DELETE; the menu's DUPLICATE, MUTE, CHANGE TEMPO; GROOVES' ADD TO SONG", "[song][ui]")
{
    Plugin t;
    SongTrack track (t.p);
    track.setBounds (0, 0, 1400, 126);
    const auto file = t.folder.getChildFile ("Dropped/Verse 02.mid");
    writeMidi (file, { { 0.0, t.p.homeNote (BD) }, { 2.0, t.p.homeNote (SD) } });
    REQUIRE (track.isInterestedInFileDrag ({ file.getFullPathName() }));
    CHECK (! track.isInterestedInFileDrag ({ "/somewhere/kick.wav" }));
    track.filesDropped ({ file.getFullPathName(), file.getFullPathName() }, (int) track.xOf (4.0) + 2, 80);
    const auto& s = t.p.getSong();
    REQUIRE (s.blocks.size() == 2);   // the second after the first
    CHECK (s.blocks[0].start == 4.0);
    CHECK (s.blocks[1].start == 8.0);
    CHECK (s.blocks[0].name == "Verse 02");
    CHECK (s.blocks[0].part == grooves::Part::verse);
    REQUIRE (s.blocks[0].hits.size() == 2);
    CHECK (s.blocks[0].hits[1].slot == SD);

    // DELETE removes the chosen block.
    track.choose (0);
    CHECK (track.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    REQUIRE (t.p.getSong().blocks.size() == 1);
    CHECK (t.p.getSong().blocks[0].start == 8.0);

    // The menu: what it has, and what each does.
    auto menu = track.menuFor (0);
    juce::StringArray items;
    std::map<juce::String, std::function<void()>> actions;
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
    {
        items.add (it.getItem().text);
        actions[it.getItem().text] = it.getItem().action;
    }
    for (const auto* item : { "Duplicate", "Remove", "Mute", "Song Part", "Chorus", "Change Tempo", "2x" })
        CHECK (items.contains (item));
    actions["Duplicate"]();
    REQUIRE (t.p.getSong().blocks.size() == 2);
    CHECK (t.p.getSong().blocks[1].start == 12.0);
    actions["Mute"]();
    CHECK (t.p.getSong().blocks[0].muted);
    actions["Chorus"]();
    CHECK (t.p.getSong().blocks[0].part == grooves::Part::chorus);
    actions["1/2x"]();   // twice as long: 2 bars, the copy moved along
    CHECK (t.p.getSong().blocks[0].length == 8.0);
    CHECK (t.p.getSong().blocks[1].start == 16.0);

    // GROOVES: right-click ADD TO SONG puts the groove after the last block.
    const auto pack = t.folder.getChildFile ("Pack");
    writeMidi (pack.getChildFile ("Fill 01.mid"), { { 3.0, 38 } });
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    REQUIRE (page.getResults().size() == 1);
    auto shown = false;
    page.onAddedToSong = [&shown] { shown = true; };
    CHECK (page.addRowToSong (0));
    CHECK (shown);
    REQUIRE (t.p.getSong().blocks.size() == 3);
    CHECK (t.p.getSong().blocks[2].name == "Fill 01");
    CHECK (t.p.getSong().blocks[2].start == 20.0);
    CHECK (t.p.getSong().blocks[2].part == grooves::Part::fill);
}
