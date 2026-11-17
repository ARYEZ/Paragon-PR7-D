// THE STUDIO LINK (PR7-D 0.83.0): PR7-S, the family's studio, lays a song out for this kit, sends "load this song" as
// SysEx on its track; the song swaps in (FOLLOW DAW on, the kit untouched) and plays with the studio's playhead; PAUSE /
// RESUME silence it at once; edits made here go back to the file once they stop.
#include <catch2/catch_test_macros.hpp>

#include "PluginTestUtils.h"

#include "song/StudioLink.h"

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    using namespace synth::drums::notemap;

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
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonStudioLinkTests");
        ParagonX1AudioProcessor p;
        Host host;
        Plugin()
        {
            folder.deleteRecursively();
            folder.createDirectory();
            p.useRigFolder (folder.getChildFile ("Rigs"));
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            testkit::useRootKeys (p);
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

        // `seconds` of output; `first` goes in with the first block (at its first sample).
        std::vector<float> play (double seconds, const juce::MidiMessage* first = nullptr)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
            {
                juce::MidiBuffer midi;
                if (done == 0 && first != nullptr)
                    midi.addEvent (*first, 0);
                buffer.clear();
                p.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
                if (host.playing)
                    host.ppq += blockSize / sampleRate * host.bpm / 60.0;
            }
            return out;
        }
    };

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

    // A song as PR7-S writes it: a 4-bar verse block with a kick on 0 and a snare on 1.5.
    juce::File writeSong (const juce::File& file)
    {
        song::Song s;
        song::Block b;
        b.name = "Verse: My library: Pocket";
        b.part = grooves::Part::verse;
        b.start = 0.0;
        b.length = b.loopLength = 16.0;
        b.hits = { { 0.0, BD, 1.0f }, { 1.5, SD, 1.0f } };
        s.blocks.push_back (b);
        auto xml = s.toXml();
        xml->setAttribute ("studioLink", file.getFullPathName());
        xml->writeTo (file);
        return file;
    }
}

TEST_CASE ("STUDIO LINK: the messages read back as written (any letters in the path), others ignored", "[studio-link]")
{
    const auto path = juce::String (juce::CharPointer_UTF8 ("C:\\Users\\Ar\xc3\xa9z\\Link\\song.xml"));
    const auto m = song::link::make (song::link::Command::load, path);
    REQUIRE (m.isSysEx());
    for (int i = 0; i < m.getSysExDataSize(); ++i)
        CHECK (m.getSysExData()[i] < 0x80);
    const auto parsed = song::link::parse (m.getSysExData(), m.getSysExDataSize());
    REQUIRE (parsed.has_value());
    CHECK (parsed->command == song::link::Command::load);
    CHECK (parsed->path == path);
    const auto pause = song::link::make (song::link::Command::pause);
    CHECK (song::link::parse (pause.getSysExData(), pause.getSysExDataSize())->command == song::link::Command::pause);
    // Someone else's SysEx, an unknown command, a broken path: nothing.
    const juce::uint8 other[] { 0x43, 0x10, 0x4C, 0x00 };
    CHECK (! song::link::parse (other, 4).has_value());
    const juce::uint8 unknown[] { 0x7D, 'P', 'R', '7', 'S', 9 };
    CHECK (! song::link::parse (unknown, 6).has_value());
    const juce::uint8 broken[] { 0x7D, 'P', 'R', '7', 'S', 1, 0x41, 0x05 };
    CHECK (! song::link::parse (broken, 8).has_value());

    // The mailbox: one at a time, taken once.
    song::link::Mailbox box;
    CHECK (box.offer (m.getSysExData(), m.getSysExDataSize()));
    CHECK (! box.offer (pause.getSysExData(), pause.getSysExDataSize()));   // (busy until taken)
    CHECK (box.take()->path == path);
    CHECK (! box.take().has_value());
}

TEST_CASE ("STUDIO LINK: PR7-S's song loads from its file and plays with the studio's playhead; pause and resume at once", "[studio-link][song-player]")
{
    Plugin t;
    const auto file = writeSong (t.folder.getChildFile ("link.xml"));
    auto loop = t.p.getSong();
    loop.loopOn = true;
    loop.loopStart = 4.0;
    loop.loopEnd = 8.0;
    loop.followDaw = false;
    t.p.setSong (loop);   // (the song track's own settings: kept)

    // The message on the track, a block played (the DAW stopped), then the timer's turn.
    t.host.playing = false;
    const auto load = song::link::make (song::link::Command::load, file.getFullPathName());
    t.play (0.01, &load);
    CHECK (t.p.getSong().blocks.empty());   // (not yet: the timer loads it)
    REQUIRE (t.p.applyStudioLink());
    CHECK (! t.p.applyStudioLink());        // (once)
    REQUIRE (t.p.getSong().blocks.size() == 1);
    CHECK (t.p.getSong().blocks[0].name == "Verse: My library: Pocket");
    CHECK (t.p.getSong().studioLink == file.getFullPathName());
    CHECK (t.p.getSong().followDaw);
    CHECK (t.p.getSong().loopOn);
    CHECK (t.p.getSong().loopEnd == 8.0);
    CHECK (t.p.getStudioLoads() == 1);
    // Loaded, not an edit: nothing written back.
    CHECK (! t.p.writeStudioLinkIfDue (1.0e9));

    // With the studio playing from 0: the kick on 0, the snare 1.5 beats on (at 120 BPM: 36000 samples).
    t.host.playing = true;
    t.host.ppq = 0.0;
    auto o = onsets (t.play (1.0));
    REQUIRE (o.size() == 2);
    CHECK (o[0] == 0);
    CHECK (o[1] == 36000);

    // PAUSE (the composer playing its own idea through the kit): silent, at once; RESUME: heard again.
    t.host.ppq = 0.0;
    const auto pause = song::link::make (song::link::Command::pause);
    CHECK (onsets (t.play (1.0, &pause)).empty());
    CHECK (t.p.isStudioPaused());
    t.host.ppq = 0.0;
    const auto resume = song::link::make (song::link::Command::resume);
    o = onsets (t.play (1.0, &resume));
    REQUIRE (o.size() == 2);
    CHECK (o[0] == 0);
}

TEST_CASE ("STUDIO LINK: edits made here go back to the file once they stop; a project's song isn't written back", "[studio-link]")
{
    Plugin t;
    const auto file = writeSong (t.folder.getChildFile ("link.xml"));
    t.host.playing = false;
    const auto load = song::link::make (song::link::Command::load, file.getFullPathName());
    t.play (0.01, &load);
    REQUIRE (t.p.applyStudioLink());

    // An edit (the snare moved to beat 3): written once it has stood still for the wait.
    auto edited = t.p.getSong();
    edited.blocks[0].hits[1].beat = 3.0;
    t.p.setSong (edited);
    CHECK (! t.p.writeStudioLinkIfDue (1000.0));   // (just seen: waits)
    CHECK (! t.p.writeStudioLinkIfDue (1000.0 + ParagonX1AudioProcessor::studioWriteDelayMs - 10.0));
    REQUIRE (t.p.writeStudioLinkIfDue (1000.0 + ParagonX1AudioProcessor::studioWriteDelayMs + 10.0));
    CHECK (! t.p.writeStudioLinkIfDue (5000.0));   // (once)
    const auto back = juce::XmlDocument::parse (file);
    REQUIRE (back != nullptr);
    const auto read = song::Song::fromXml (*back);
    REQUIRE (read.blocks.size() == 1);
    CHECK (read.blocks[0].hits[1].beat == 3.0);
    CHECK (read.studioLink == file.getFullPathName());

    // Saved with the project and opened again: the link kept, nothing written back by the opening.
    juce::MemoryBlock state;
    t.p.getStateInformation (state);
    file.deleteFile();
    ParagonX1AudioProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (other.getSong().studioLink == t.p.getSong().studioLink);
    CHECK (! other.writeStudioLinkIfDue (0.0));
    CHECK (! other.writeStudioLinkIfDue (1.0e9));
    CHECK (! file.existsAsFile());
}
