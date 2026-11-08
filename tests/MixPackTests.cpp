// MIXED SAMPLE PACKS (PR7-D 0.69.0): a pack made here, laid out as the owner's (piece / version / articulation /
// mix folders, "<NAME>_V<layer>_<mix>_RR (<round>).wav", a Mac's "._" companion beside every file); the sounds
// invented: each mix folder a sine of its own pitch, so which one a drum took can be heard.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>

#include "dsp/synth/Instrument.h"
#include "dsp/synth/MixPack.h"
#include "dsp/synth/NoteMap.h"

namespace
{
    using namespace synth::drums::notemap;
    namespace mp = synth::drums::mixpack;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-mixpack-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    void writeHit (const juce::File& file, double hz, float level)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b (2, (int) (0.4 * sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int c = 0; c < 2; ++c)
                b.setSample (c, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, 2, 24, {}, 0));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        w.reset();
        // macOS's companion: not audio, named as a WAV.
        file.getSiblingFile ("._" + file.getFileName()).replaceWithText (juce::String::repeatedString ("x", 4000));
    }

    // A mix folder of `layers` x 3 rounds at `hz` (each layer louder).
    void mixFolder (const juce::File& dir, const juce::String& name, const juce::String& mixTag, double hz, int layers = 2)
    {
        for (int v = 1; v <= layers; ++v)
            for (int r = 1; r <= 3; ++r)
                writeHit (dir.getChildFile (name + "_V" + juce::String (v) + "_" + mixTag + "_RR (" + juce::String (r) + ").wav"), hz, 0.2f * (float) v);
        dir.getChildFile (".DS_Store").replaceWithText ("x");
    }

    juce::File makePack (const juce::File& at)
    {
        const auto top = at.getChildFile ("TEST ERA WAV files");
        // Kicks: two, mixes, a parallel; the boosted version at its own pitch.
        const auto k1 = top.getChildFile ("KICK/ORIGINAL/KICK 1");
        mixFolder (k1.getChildFile ("00 - STEREO PRESET - MIX 1"), "KICK1", "Mix1", 300.0);
        mixFolder (k1.getChildFile ("01 - STEREO PRESET - MIX 1 PUNCHY"), "KICK1", "Mix1Punchy", 330.0);
        mixFolder (k1.getChildFile ("00 - STEREO PRESET - MIX 2 - CANON"), "KICK1", "Mix2b_Canon", 360.0);
        mixFolder (k1.getChildFile ("04 - PARALLEL - Blend to taste with Presets"), "KICK1", "ParCompMixed", 500.0);
        mixFolder (top.getChildFile ("KICK/ORIGINAL/KICK 2/00 - STEREO PRESET - MIX 1"), "KICK2", "Mix1", 320.0);
        mixFolder (top.getChildFile ("KICK/GAIN BOOSTED/KICK 1/00 - STEREO PRESET - MIX 1"), "KICK1", "Mix1", 700.0);
        // The snare: center (two mixes + parallel), edge (no slot), rimshot, sidestick, the extras (no slot).
        const auto sn = top.getChildFile ("SNARE/6.5x14 Brass Snare - ORIGINAL");
        mixFolder (sn.getChildFile ("1 - CENTER/01 - MIX PRESET 1"), "SNARE_CENTER", "Mix1Natural", 400.0, 3);
        mixFolder (sn.getChildFile ("1 - CENTER/02 - MIX PRESET 1 PUNCHY"), "SNARE_CENTER", "Mix1Punchy", 430.0);
        mixFolder (sn.getChildFile ("1 - CENTER/07 - PARALLEL - Blend to taste with Presets"), "SNARE_CENTER", "ParaCompNatural", 520.0);
        mixFolder (sn.getChildFile ("2 - EDGE/01 - MIX PRESET 1"), "SNARE_EDGE", "Mix1Natural", 440.0);
        mixFolder (sn.getChildFile ("3 - RIMSHOT/01 - MIX PRESET 1"), "SNARE_RIMSHOT", "Mix1Natural", 450.0);
        mixFolder (sn.getChildFile ("4 - SIDESTICK/01 - MIX PRESET 1"), "SNARE_SIDESTICK", "Mix1Natural", 460.0);
        mixFolder (sn.getChildFile ("5 - ALT CENTER HITS - EXTRAS/01 - MIX PRESET 1"), "SNARE_CENTER ALT", "Mix1Natural", 470.0);
        // Toms 1 and 4 (tuned and untuned mixes).
        for (const auto n : { 1, 4 })
        {
            const auto t = top.getChildFile ("TOMS/ORIGINAL/TOM " + juce::String (n) + "/CENTER");
            mixFolder (t.getChildFile ("01 - MIX PRESET 1"), "TOM " + juce::String (n) + "_CENTER", "Mix1Natural", 200.0 + 10 * n);
            mixFolder (t.getChildFile ("03 - MIX PRESET 2 (TUNED)"), "TOM " + juce::String (n) + "_CENTER", "Mix2_Tuned", 250.0 + 10 * n);
            mixFolder (t.getChildFile ("08 - MIX PRESET 3 - UNTUNED NO BLEED"), "TOM " + juce::String (n) + "_CENTER", "Mix3_UntunedNoBleed", 290.0);
        }
        // The hats: closed, 25 / 50 / 100% open, pedal, a tighter pedal (no slot).
        const auto hats = top.getChildFile ("HATS/ORIGINAL");
        const char* steps[] { "01 - CLOSED SHANK EDGE", "02 - 25% OPEN SHANK EDGE", "03 - 50% OPEN SHANK EDGE", "04 - 100% OPEN SHANK EDGE",
                              "05 - PEDAL STOMP", "06 - TIGHTER STOMP" };
        for (int i = 0; i < 6; ++i)
            mixFolder (hats.getChildFile (steps[i]).getChildFile ("01 - MIX PRESET"), "HI HAT_" + juce::String (steps[i]).substring (5), "BDMixNatural", 800.0 + 20 * i);
        // Crashes 17 / 18 / 20, center / edge / choke; the ride's bell / center / edge.
        for (const auto size : { 17, 18, 20 })
            for (const auto* art : { "CENTER", "EDGE", "CHOKE" })
                mixFolder (top.getChildFile ("CYMBALS/" + juce::String (size) + " inch/" + juce::String (size) + " CRASH - ORIGINAL/" + art + "/01 - MIX PRESET"),
                           juce::String (size) + " CRASH_" + art, "BDMixNatural", 1000.0 + size);
        for (const auto* art : { "BELL", "CENTER", "EDGE" })
            mixFolder (top.getChildFile ("RIDE/ORIGINAL/" + juce::String (art) + "/01 - MIX PRESET"), "RIDE_" + juce::String (art), "BDMixNatural", 1200.0);
        return top;
    }

    struct Plugin
    {
        std::unique_ptr<ParagonX1AudioProcessor> processor = std::make_unique<ParagonX1AudioProcessor>();
        TempFolder temp;
        Plugin()
        {
            processor->prepareToPlay (sampleRate, blockSize);
            processor->useRigFolder (temp.folder.getChildFile ("Presets"));
            testkit::useRootKeys (*processor);
        }
        float get (const juce::String& id) const
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor->getParameters().getParameter (id));
            return p->convertFrom0to1 (p->getValue());
        }
        std::vector<float> play (int slot, int velocity, double seconds)
        {
            processor->prepareToPlay (sampleRate, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (slot), (juce::uint8) velocity), 0);
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                processor->processBlock (buffer, midi);
                midi.clear();
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return out;
        }
    };
}

TEST_CASE ("MIXED SAMPLE PACK: a mix folder's name read as its mix", "[mixpack]")
{
    const auto key = [] (const char* s) { return mp::keyOf (s); };
    CHECK (key ("00 - STEREO PRESET - MIX 1") == mp::keyOfChoice (0));
    CHECK (key ("01 - STEREO PRESET - MIX 1 PUNCHY") == mp::keyOfChoice (1));
    CHECK (key ("02 - STEREO PRESET - MIX 2") == mp::keyOfChoice (2));
    CHECK (key ("00 - STEREO PRESET - MIX 2 - CANON") == mp::keyOfChoice (3));
    CHECK (key ("04 - MIX PRESET 2B - NO BLEED CANON WITH VERB") == mp::keyOfChoice (4));
    CHECK (key ("05 - MIX PRESET 2B - NO BLEED GUNFIRE") == mp::keyOfChoice (5));
    CHECK (key ("03 - STEREO PRESET - MIX 2 - GUNFIRE WITH VERB") == mp::keyOfChoice (6));
    CHECK (key ("01 - MIX PRESET 1") == mp::keyOfChoice (0));
    CHECK (key ("03 - MIX PRESET 2 (TUNED)").words == juce::StringArray { "TUNED" });
    CHECK (key ("09 - MIX PRESET 3B - NO BLEED UNTUNED CANON").number == 3);
    CHECK (key ("01 - MIX PRESET").number == 0);
    CHECK (mp::isParallel ("02 - PARALLEL - Blend to taste with Preset"));
    CHECK_FALSE (mp::isParallel ("01 - MIX PRESET"));
}

TEST_CASE ("MIXED SAMPLE PACK: the pack's folders onto the kit's drums, the version and mix asked for (else the nearest)", "[mixpack]")
{
    TempFolder t;
    const auto top = makePack (t.folder);
    const auto plan = mp::plan (top, mp::Version::original, 0);
    REQUIRE (plan.valid());
    CHECK (plan.name == "TEST ERA WAV files");
    std::set<int> slots;
    for (const auto& [slot, drum] : plan.slots)
        slots.insert (slot);
    CHECK (slots == std::set<int> { BD, SD, SD2, RIM, HT, T4, CH, HO1, HH, OH, PH, CY, CR2, C3, C1C, C2C, RB, RD, RE });
    CHECK (plan.slots.at (BD).mixName == "00 - STEREO PRESET - MIX 1");
    CHECK (plan.slots.at (BD).mix.getFullPathName().contains ("ORIGINAL"));
    CHECK (plan.slots.at (BD).parallel.getFileName().startsWith ("04 - PARALLEL"));
    CHECK (plan.slots.at (SD).parallel.getFileName().startsWith ("07 - PARALLEL"));
    CHECK (plan.slots.at (CH).parallel == juce::File());
    CHECK (plan.secondKick.getFullPathName().contains ("KICK 2"));
    // The crashes by size: 17" CRASH, 18" CRASH 2, 20" CRASH 3; their edges, the first two's chokes.
    CHECK (plan.slots.at (CY).mix.getFullPathName().contains ("17 CRASH - ORIGINAL" + juce::File::getSeparatorString() + "EDGE"));
    CHECK (plan.slots.at (CR2).mix.getFullPathName().contains ("18 CRASH"));
    CHECK (plan.slots.at (C3).mix.getFullPathName().contains ("20 CRASH"));
    CHECK (plan.slots.at (C2C).mix.getFullPathName().contains ("18 CRASH - ORIGINAL" + juce::File::getSeparatorString() + "CHOKE"));
    // What had no drum, named so the owner can find it.
    for (const auto* left : { "SNARE 2 - EDGE", "SNARE 5 - ALT CENTER HITS - EXTRAS", "HATS 06 - TIGHTER STOMP", "CYMBALS 17 CRASH CENTER",
                              "CYMBALS 20 CRASH CHOKE" })
        CHECK (plan.unused.contains (left));

    // GAIN BOOSTED: its own folders.
    CHECK (mp::plan (top, mp::Version::boosted, 0).slots.at (BD).mix.getFullPathName().contains ("GAIN BOOSTED"));
    // MIX 2 CANON: the kick's; the snare has none (MIX 1 instead); the toms' nearest MIX 2 is the tuned one.
    const auto canon = mp::plan (top, mp::Version::original, 3);
    CHECK (canon.slots.at (BD).mixName == "00 - STEREO PRESET - MIX 2 - CANON");
    CHECK (canon.slots.at (SD).mixName == "01 - MIX PRESET 1");
    CHECK (mp::plan (top, mp::Version::original, 2).slots.at (HT).mixName == "03 - MIX PRESET 2 (TUNED)");
    CHECK (mp::plan (top, mp::Version::original, 1).slots.at (SD).mixName == "02 - MIX PRESET 1 PUNCHY");
}

TEST_CASE ("MIXED SAMPLE PACK: a mix folder read as layers and rounds, a Mac's \"._\" files left out", "[mixpack]")
{
    TempFolder t;
    const auto dir = t.folder.getChildFile ("01 - MIX PRESET 1");
    mixFolder (dir, "SNARE_CENTER", "Mix1Natural", 400.0, 3);
    for (int r = 4; r <= 10; ++r)   // rounds past 9 (RR (10) after RR (9), not after RR (1))
        writeHit (dir.getChildFile ("SNARE_CENTER_V1_Mix1Natural_RR (" + juce::String (r) + ").wav"), 400.0, 0.2f);
    const auto inst = synth::drums::instruments::fromFolder (dir);
    REQUIRE (inst != nullptr);
    REQUIRE (inst->layers.size() == 3);
    CHECK (inst->layers[0].level < inst->layers[2].level);
    REQUIRE (inst->layers[0].hits.size() == 10);   // (no "._" companions among them)
    for (int r = 0; r < 10; ++r)
        CHECK (inst->layers[0].hits[(size_t) r].files[0].contains ("RR (" + juce::String (r + 1) + ")"));
    CHECK (inst->layers[1].hits.size() == 3);
}

TEST_CASE ("MIXED SAMPLE PACK: imported onto the kit and played: each drum its mix, the parallel mix its layer, one undo step", "[mixpack]")
{
    TempFolder t;
    const auto top = makePack (t.folder);
    Plugin p;
    const auto result = p.processor->importMixPack (top, false, 0);
    REQUIRE (result.slots.size() == 19);
    CHECK (result.secondKick);
    REQUIRE (p.processor->waitForInstruments());

    // Every drum its instrument (2 layers of 3 rounds; the snare's 3), on, playing a SAMPLE.
    for (const auto& [slot, mixName] : result.slots)
    {
        INFO (synth::drums::slotDefaults[(size_t) slot].key);
        const auto inst = p.processor->getDrumInstrument (slot);
        REQUIRE (inst != nullptr);
        CHECK (inst->layers.size() == (slot == SD ? 3u : 2u));
        CHECK (inst->layers[0].hits.size() == 3);
        CHECK (p.get (ParamIDs::drum (slot, "On")) == 1.0f);
    }
    // Layers: the parallel mixes at -10 dB, the second kick off.
    CHECK (p.processor->getLayer (BD, 0) != nullptr);
    CHECK (p.processor->getLayer (SD, 0) != nullptr);
    CHECK (p.processor->getLayer (CH, 0) == nullptr);
    CHECK (p.processor->getLayer (BD, 1) != nullptr);
    CHECK (p.get (ParamIDs::drum (BD, ParamIDs::layerLevel (0))) == Catch::Approx (-10.0f).margin (0.01));
    CHECK (p.get (ParamIDs::drum (BD, ParamIDs::layerLevel (1))) == Catch::Approx (-60.0f).margin (0.01));
    // The steps it has none of: off (they play the nearest it has).
    CHECK (p.get (ParamIDs::drum (HO3, "On")) == 0.0f);
    CHECK (p.get (ParamIDs::drum (C4, "On")) == 0.0f);

    // Heard: the kick is MIX 1 (300 Hz), the snare MIX 1 (400 Hz), the rimshot its own (450 Hz); with the parallel off.
    for (const auto slot : { BD, SD })
        testkit::set (*p.processor, ParamIDs::drum (slot, ParamIDs::layerLevel (0)), -60.0f);
    for (const auto& [slot, hz] : std::vector<std::pair<int, double>> { { BD, 300.0 }, { SD, 400.0 }, { SD2, 450.0 }, { RIM, 460.0 }, { OH, 860.0 } })
    {
        INFO (synth::drums::slotDefaults[(size_t) slot].key);
        const auto out = p.play (slot, 120, 0.3);
        CHECK (rms (out, 0, 8192) > 0.01f);
        CHECK (frequencyOf (out, 1024, 8192) == Catch::Approx (hz).margin (3.0));
    }
    // The parallel mix (500 Hz): silent at -60 dB, heard beside the kick's mix when raised to 0 dB.
    const auto off = p.play (BD, 120, 0.3);
    CHECK (levelAt (off, 500.0, 1024, 8192) < 0.01 * levelAt (off, 300.0, 1024, 8192));
    testkit::set (*p.processor, ParamIDs::drum (BD, ParamIDs::layerLevel (0)), 0.0f);
    const auto on = p.play (BD, 120, 0.3);
    CHECK (levelAt (on, 500.0, 1024, 8192) > 0.5 * levelAt (on, 300.0, 1024, 8192));
}

TEST_CASE ("MIXED SAMPLE PACK: the import is one undo step", "[mixpack]")
{
    TempFolder t;
    const auto top = makePack (t.folder);
    Plugin p;
    p.processor->flushUndoStep();
    CHECK (p.get (ParamIDs::drum (HO3, "On")) == 1.0f);
    p.processor->importMixPack (top, false, 0);
    p.processor->flushUndoStep();
    CHECK (p.get (ParamIDs::drum (HO3, "On")) == 0.0f);
    CHECK (p.get (ParamIDs::drum (BD, ParamIDs::layerLevel (1))) == Catch::Approx (-60.0f).margin (0.01));
    p.processor->undo();
    CHECK (p.get (ParamIDs::drum (HO3, "On")) == 1.0f);
    CHECK (p.get (ParamIDs::drum (BD, ParamIDs::layerLevel (1))) != Catch::Approx (-60.0f).margin (0.01));
    p.processor->waitForInstruments();
}

TEST_CASE ("MIXED SAMPLE PACK: the parallel mixes left out when asked", "[mixpack]")
{
    TempFolder t;
    const auto top = makePack (t.folder);
    Plugin p;
    p.processor->importMixPack (top, false, 0, false);
    REQUIRE (p.processor->waitForInstruments());
    CHECK (p.processor->getDrumInstrument (BD) != nullptr);
    CHECK (p.processor->getLayer (BD, 0) == nullptr);
    CHECK (p.processor->getLayer (SD, 0) == nullptr);
    CHECK (p.processor->getLayer (BD, 1) != nullptr);   // (the second kick still offered)
}
