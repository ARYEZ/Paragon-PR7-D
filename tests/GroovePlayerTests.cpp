// THE GROOVE PLAYER's preview (PR7-D 0.4.0): a groove played through the kit, its hits on the samples its tempo puts
// them (the song's, or its own, times the factor), looping or once, its velocities scaled, its notes through the
// folder's map; and the file dragged out.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginTestUtils.h"
#include "ui/GroovesPage.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    using namespace synth::drums::notemap;

    struct Plugin
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonGroovePlayerTests");
        ParagonX1AudioProcessor p;
        Plugin()
        {
            folder.deleteRecursively();
            p.useRigFolder (folder);
            p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            // The kick's and closed hat's slots play a short, steady burst (20 ms, full from its first sample): each hit
            // shows where it starts.
            const auto burst = testkit::tone ([] (double) { return 1.0; }, 100.0, 0.02, 0.5f, 1, sampleRate);
            testkit::useSample (p, BD, burst);
            testkit::useSample (p, CH, burst);
        }
        ~Plugin() { folder.deleteRecursively(); }

        void preview (bool songTempo, grooves::TempoFactor factor, float velocity = 1.0f)
        {
            auto s = p.getGrooveLibrary().getPreview();
            s.songTempo = songTempo;
            s.factor = factor;
            s.velocity = velocity;
            p.getGrooveLibrary().setPreview (s);
            p.applyGroovePreview();
        }

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
            }
            return out;
        }
    };

    // A groove: `notes` (beat, note) at `bpm`, `bars` 4/4 bars.
    grooves::Groove groove (std::vector<std::pair<double, int>> notes, double bpm = 90.0, int bars = 1, float velocity = 1.0f)
    {
        grooves::Groove g;
        g.name = "Test";
        g.file = juce::File ("/grooves/Test.mid");
        g.bpm = g.bpmKnown ? bpm : bpm;
        g.bars = bars;
        g.lengthBeats = bars * 4.0;
        for (const auto& [beat, note] : notes)
            g.hits.push_back ({ beat, note, velocity });
        return g;
    }

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

    float peak (const std::vector<float>& x)
    {
        auto p = 0.0f;
        for (const auto v : x)
            p = std::max (p, std::abs (v));
        return p;
    }
}

TEST_CASE ("GROOVE PREVIEW: looping at the song's tempo, each hit on its sample", "[grooves][groove-player]")
{
    Plugin t;
    t.preview (true, grooves::TempoFactor::normal);
    // Kick on 1, 2 and 3.5 of a one-bar groove; the song at 120 (the host's default here): a beat is 24000 samples.
    t.p.playGroove (groove ({ { 0.0, 36 }, { 1.0, 36 }, { 2.5, 36 } }), 0, true);
    const auto out = t.play (4.5);
    // Rounded down to the sample, as the sequencer's: 2.5 beats is 60000 exactly.
    CHECK (onsets (out) == std::vector<int> { 0, 24000, 60000, 96000, 120000, 156000, 192000, 216000 });
    CHECK (t.p.getGroovePosition() >= 0.0);
}

TEST_CASE ("GROOVE PREVIEW: its own tempo, and the factors", "[grooves][groove-player]")
{
    const auto firstGap = [] (bool songTempo, grooves::TempoFactor factor)
    {
        Plugin t;
        t.preview (songTempo, factor);
        t.p.playGroove (groove ({ { 0.0, 36 }, { 1.0, 36 } }, 90.0), 0, true);
        const auto o = onsets (t.play (1.5));
        REQUIRE (o.size() >= 2);
        return o[1] - o[0];
    };
    CHECK (firstGap (true, grooves::TempoFactor::normal) == 24000);       // 120: the song's
    CHECK (firstGap (false, grooves::TempoFactor::normal) == 32000);      // 90: its own
    CHECK (firstGap (true, grooves::TempoFactor::twice) == 12000);
    CHECK (firstGap (true, grooves::TempoFactor::half) == 48000);
    CHECK (firstGap (true, grooves::TempoFactor::twoThirds) == 36000);
    CHECK (firstGap (true, grooves::TempoFactor::threeHalves) == 16000);
}

TEST_CASE ("GROOVE PREVIEW: once through (with a closing crash on the next bar line), then it stops", "[grooves][groove-player]")
{
    Plugin t;
    t.preview (true, grooves::TempoFactor::twice);   // a bar a second
    t.p.playGroove (groove ({ { 0.0, 36 }, { 3.0, 36 }, { 4.0, 36 } }), 0, false);
    const auto out = t.play (3.0);
    CHECK (onsets (out) == std::vector<int> { 0, 36000, 48000 });   // no second time round
    CHECK (t.p.getGroovePosition() < 0.0);
}

TEST_CASE ("GROOVE PREVIEW: the velocity scale, and STOP", "[grooves][groove-player]")
{
    const auto level = [] (float velocity)
    {
        Plugin t;
        t.preview (true, grooves::TempoFactor::normal, velocity);
        t.p.playGroove (groove ({ { 0.0, 36 } }), 0, true);
        return peak (t.play (0.1));
    };
    const auto full = level (1.0f), half = level (0.5f);
    INFO ("full " << full << ", half " << half);
    // Half the velocity: 12 dB down at the kit's VELOCITY 100% (as a key played half as hard).
    CHECK_THAT (juce::Decibels::gainToDecibels (half / full), WithinAbs (-12.04, 0.3));

    Plugin t;
    t.p.playGroove (groove ({ { 0.0, 36 }, { 2.0, 36 } }), 0, true);
    t.play (0.5);
    t.p.stopGroove();
    CHECK (onsets (t.play (2.0)).empty());
    CHECK (t.p.getGroovePosition() < 0.0);
}

TEST_CASE ("GROOVE PREVIEW: notes through the folder's map: 22 is the closed hat in a groove library's files, nothing in GM's", "[grooves][groove-player]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack"), gm = t.folder.getChildFile ("GM Pack");
    pack.createDirectory();
    gm.createDirectory();
    auto& library = t.p.getGrooveLibrary();
    REQUIRE (library.addFolder (pack));
    REQUIRE (library.addFolder (gm));
    library.setLayout (1, Layout::generalMidi);
    t.p.playGroove (groove ({ { 0.0, 22 } }), 0, false);
    t.p.processBlock (*std::make_unique<juce::AudioBuffer<float>> (2, blockSize), *std::make_unique<juce::MidiBuffer>());
    CHECK (t.p.getDrumKit().isSounding (CH));
    Plugin u;   // (the same temporary folder, made afresh)
    gm.createDirectory();
    REQUIRE (u.p.getGrooveLibrary().addFolder (gm));
    u.p.getGrooveLibrary().setLayout (0, Layout::generalMidi);
    u.p.playGroove (groove ({ { 0.0, 22 }, { 0.0, 42 } }), 0, false);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer none;
    u.p.processBlock (buffer, none);
    for (int k = 0; k < synth::drums::numSlots; ++k)
        CHECK (u.p.getDrumKit().isSounding (k) == (k == CH));   // 42 the hat, 22 nothing
}

TEST_CASE ("GROOVE DRAG: the file on this kit's notes, or as written; velocity and factor in it", "[grooves][groove-player]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    pack.createDirectory();
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    const auto g = groove ({ { 0.0, 36 }, { 1.0, 22 }, { 2.0, 53 }, { 3.0, 3 } });
    const auto notesIn = [&t, &g]
    {
        const auto file = t.p.grooveDragFile (g, 0);
        REQUIRE (file.existsAsFile());
        const auto read = grooves::read (file, generalMidi());
        REQUIRE (read.has_value());
        return read;
    };
    // The kit on ROOT (C2): each drum on its key there: kick 36, closed hat 42, ride bell 49, tambourine 54.
    auto r = notesIn();
    REQUIRE (r->hits.size() == 4);
    CHECK (r->hits[0].note == 36);
    CHECK (r->hits[1].note == 36 + CH);
    CHECK (r->hits[2].note == 36 + RB);
    CHECK (r->hits[3].note == 36 + TMB);
    // The kit on the groove libraries' map, as the folder: as written (every note kept as it is).
    testkit::set (t.p, ParamIDs::drumsMap, (float) Layout::grooveLibrary);
    r = notesIn();
    CHECK (r->hits[1].note == 22);
    CHECK (r->hits[3].note == 3);
    // 2x and half velocity: the notes twice as close, half as hard.
    testkit::set (t.p, ParamIDs::drumsMap, (float) Layout::root);
    t.preview (true, grooves::TempoFactor::twice, 0.5f);
    r = notesIn();
    CHECK_THAT (r->hits[1].beat, WithinAbs (0.5, 1e-9));
    CHECK_THAT (r->hits[3].beat, WithinAbs (1.5, 1e-9));
    CHECK_THAT (r->hits[0].velocity * 127.0f, WithinAbs (64.0, 0.6));
    // FOR THIS KIT off: as written, whatever the kit's map.
    auto s = t.p.getGrooveLibrary().getPreview();
    s.forThisKit = false;
    t.p.getGrooveLibrary().setPreview (s);
    CHECK (notesIn()->hits[1].note == 22);
}

TEST_CASE ("GROOVES page: the tree opens the way to the folder chosen; a folder filters the list", "[grooves][groove-player][ui]")
{
    Plugin t;
    const auto pack = t.folder.getChildFile ("Pack");
    for (const auto* path : { "Rock 96 BPM/Verse 01.mid", "Rock 96 BPM/Fill 01.mid", "Funk/Verse 01.mid" })
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100));
        track.addEvent (juce::MidiMessage::noteOff (10, 36).withTimeStamp (60));
        juce::MidiFile file;
        file.setTicksPerQuarterNote (480);
        file.addTrack (track);
        const auto f = pack.getChildFile (path);
        f.getParentDirectory().createDirectory();
        juce::FileOutputStream out (f);
        file.writeTo (out);
    }
    REQUIRE (t.p.getGrooveLibrary().addFolder (pack));
    t.p.getGrooveLibrary().scanNow();
    GroovesPage page (t.p);
    page.setSize (1476, 672);
    page.refresh();
    CHECK (page.getResults().size() == 3);
    page.chooseFolder (pack.getChildFile ("Rock 96 BPM"));
    CHECK (page.getResults().size() == 2);
    page.refresh();   // rebuilt: the same folder chosen, the way to it open
    auto* selected = page.getTree().getSelectedItem (0);
    REQUIRE (selected != nullptr);
    CHECK (selected->getUniqueName() == pack.getChildFile ("Rock 96 BPM").getFullPathName());
    CHECK (selected->getParentItem()->isOpen());
    CHECK (page.getResults().size() == 2);
    page.setSearch ("fill");
    CHECK (page.getResults().size() == 1);
    page.playRow (0, true);
    CHECK (t.p.getGrooveFile().getFileName() == "Fill 01.mid");

    // The window closing stops it.
    {
        GroovesPage another (t.p);
        another.refresh();
        another.playRow (0, true);
        CHECK (t.p.getGrooveFile() != juce::File());
    }
    CHECK (t.p.getGrooveFile() == juce::File());
}
