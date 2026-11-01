// THE MIXER (PR7-D 0.20.0): mic names sorted into channels, multi-mic kits (SFZ and folders) keeping each mic apart,
// and what the mixer does to the sound: each channel's fader, mute, solo, polarity, pan and output; bleed (the
// channel's and each piece's); time offset to the sample; release; synth drums left alone; presets keeping it.
// Each test mic file is a sine at its own pitch, so each channel can be measured in the output.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>

#include "dsp/synth/Instrument.h"
#include "dsp/synth/Mixer.h"
#include "dsp/synth/NoteMap.h"
#include "rigs/Rig.h"
#include "ui/MixerPage.h"

namespace
{
    using namespace synth;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-mixer-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    void writeSine (const juce::File& file, double hz, float level, double seconds = 1.0)
    {
        file.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> b (1, (int) (seconds * sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate, 1, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    // A kit: the kick (36) through KICK IN (100 Hz) and the overheads (1000 Hz); the snare (38) through SNARE TOP
    // (300 Hz), KICK IN (bleed: 500 Hz) and the room (2000 Hz). Each at 0.25.
    juce::File writeKit (const juce::File& folder)
    {
        writeSine (folder.getChildFile ("kick_in.wav"), 100.0, 0.25f);
        writeSine (folder.getChildFile ("kick_oh.wav"), 1000.0, 0.25f);
        writeSine (folder.getChildFile ("snare_top.wav"), 300.0, 0.25f);
        writeSine (folder.getChildFile ("snare_kickin.wav"), 500.0, 0.25f);
        writeSine (folder.getChildFile ("snare_room.wav"), 2000.0, 0.25f);
        const auto sfz = folder.getChildFile ("kit.sfz");
        sfz.replaceWithText ("<group> key=36\n"
                             "<region> sample=kick_in.wav group_label=Kick In\n"
                             "<region> sample=kick_oh.wav group_label=OH\n"
                             "<group> key=38\n"
                             "<region> sample=snare_top.wav group_label=Snare Top\n"
                             "<region> sample=snare_kickin.wav group_label=Kick In\n"
                             "<region> sample=snare_room.wav group_label=Room\n");
        return sfz;
    }

    struct Plugin
    {
        std::unique_ptr<ParagonX1AudioProcessor> processor = std::make_unique<ParagonX1AudioProcessor>();
        explicit Plugin (const juce::File& presets, bool out2 = false)
        {
            auto layout = processor->getBusesLayout();
            if (out2)
                layout.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            REQUIRE (processor->setBusesLayout (layout));
            processor->prepareToPlay (sampleRate, blockSize);
            processor->useRigFolder (presets);
        }
        void set (const juce::String& id, float value) { testkit::set (*processor, id, value); }
        // Every output channel, `seconds` of it, `note` hit at velocity 127 at the start.
        std::vector<std::vector<float>> play (int note, double seconds = 0.3)
        {
            processor->prepareToPlay (sampleRate, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 127), 0);
            const auto channels = processor->getTotalNumOutputChannels();
            std::vector<std::vector<float>> out ((size_t) channels);
            juce::AudioBuffer<float> buffer (channels, blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                processor->processBlock (buffer, midi);
                midi.clear();
                for (int c = 0; c < channels; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
            }
            return out;
        }
        // A pitch's level in MAIN's left channel, over 0.05 to 0.25 s.
        double level (int note, double hz)
        {
            const auto out = play (note);
            return levelAt (out[0], hz, (size_t) (0.05 * sampleRate), (size_t) (0.2 * sampleRate));
        }
    };
}

TEST_CASE ("MIXER: mic names into channels", "[mixer]")
{
    using namespace synth::mixer;
    CHECK (channelForName ("Kick In") == kickIn);
    CHECK (channelForName ("kick_out") == kickOut);
    CHECK (channelForName ("Snare Top") == snareTop);
    CHECK (channelForName ("SnareBottom") == snareBottom);
    CHECK (channelForName ("Snare Btm") == snareBottom);
    CHECK (channelForName ("HH") == hiHat);
    CHECK (channelForName ("Tom 1") == tom1);
    CHECK (channelForName ("Floor Tom") == tom2);
    CHECK (channelForName ("OH") == overheads);
    CHECK (channelForName ("Overheads") == overheads);
    CHECK (channelForName ("Room") == room);
    CHECK (channelForName ("Room Kick") == room);   // a room mic, though it says kick
    CHECK (channelForName ("Room Far") == ambience);
    CHECK (channelForName ("Samples/OH/snare_v3.wav") == overheads);
    CHECK (channelForName ("misc") == -1);
    CHECK (numPieces == drums::numPieces);
    // A drum's own channels (direct sound) and the ones it bleeds into.
    CHECK (drums::homeChannel (drums::notemap::SD) == snareTop);
    CHECK (drums::isDirect (drums::notemap::SD, snareBottom));
    CHECK (drums::isDirect (drums::notemap::SD, overheads));
    CHECK_FALSE (drums::isDirect (drums::notemap::SD, kickIn));
    CHECK (drums::homeChannel (drums::notemap::RD) == cymbals);
}

TEST_CASE ("MIXER: a multi-mic SFZ kit and a folder of mic folders keep each mic apart; a single-mic one doesn't", "[mixer]")
{
    TempFolder temp;
    const auto sfz = drums::instruments::fromSfz (writeKit (temp.folder));
    REQUIRE (sfz.notes.size() == 2);
    const auto& snare = *sfz.notes.at (38);
    REQUIRE (snare.hasMics());
    CHECK (snare.micChannels() == juce::Array<int> { mixer::kickIn, mixer::snareTop, mixer::room });
    CHECK (snare.layers[0].hits[0].mics.size() == 3);
    // Its summed hit is still there (pictures, as before).
    CHECK (snare.layers[0].hits[0].sample != nullptr);

    // A folder with a folder per mic: each hit by its name in each.
    const auto folder = temp.folder.getChildFile ("Tom");
    for (const auto* mic : { "Tom 1", "OH", "Room" })
        for (int i = 1; i <= 2; ++i)
            writeSine (folder.getChildFile (mic).getChildFile ("tom " + juce::String (i) + ".wav"), 200.0 * i, 0.2f * (float) i, 0.2);
    const auto tom = drums::instruments::fromFolder (folder);
    REQUIRE (tom != nullptr);
    CHECK (tom->numHits() == 2);
    CHECK (tom->micChannels() == juce::Array<int> { mixer::tom1, mixer::overheads, mixer::room });

    // One mic: plays as before (no channels).
    temp.folder.getChildFile ("single.sfz").replaceWithText ("<region> sample=kick_in.wav key=36\n");
    CHECK_FALSE (drums::instruments::fromSfz (temp.folder.getChildFile ("single.sfz")).notes.at (36)->hasMics());
}

TEST_CASE ("MIXER: faders, mute, solo, polarity, bleed, time offset, release, output; synth drums left alone", "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    p.set (ParamIDs::drum (0, "VelToVol"), 0.0f);

    // Each mic's sine at its level (0.25 x the kit's -6 dB... measured, then compared).
    const auto top = p.level (38, 300.0), bleed = p.level (38, 500.0), room = p.level (38, 2000.0);
    REQUIRE (top > 0.05);
    CHECK (bleed == Catch::Approx (top).epsilon (0.02));
    CHECK (room == Catch::Approx (top).epsilon (0.02));

    // A fader 6 dB down: that mic alone.
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), -6.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), 0.0f);

    // MUTE the room; SOLO the snare top (only it heard).
    p.set (ParamIDs::mix (mixer::room, "Mute"), 1.0f);
    CHECK (p.level (38, 2000.0) < room * 0.001);
    p.set (ParamIDs::mix (mixer::room, "Mute"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (38, 2000.0) < room * 0.001);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);

    // BLEED: the snare in the kick mic is bleed. The channel's BLEED off, or -12 dB, takes it out or down; the snare's
    // own sound in its own mic (direct) isn't touched. The kick's own sound in KICK IN isn't bleed either.
    p.set (ParamIDs::mix (mixer::kickIn, "BleedOn"), 0.0f);
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (36, 100.0) > 0.05);
    p.set (ParamIDs::mix (mixer::kickIn, "BleedOn"), 1.0f);
    p.set (ParamIDs::mix (mixer::kickIn, "Bleed"), -12.0f);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed * 0.251).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::kickIn, "Bleed"), 0.0f);
    // BLEED FROM INSTRUMENTS: the snare's own level in KICK IN (-6 dB), and its polarity.
    auto m = p.processor->getModMatrix();
    const auto snarePiece = drums::pieceOfSlot (drums::notemap::SD);
    m.bleed (mixer::kickIn, snarePiece).gainDb = -6.0f;
    p.processor->setModMatrix (m);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed * 0.501).epsilon (0.02));
    m.bleed (mixer::kickIn, snarePiece) = { 0.0f, true, true };
    p.processor->setModMatrix (m);
    const auto normal = [&]
    {
        m.bleed (mixer::kickIn, snarePiece).invert = false;
        p.processor->setModMatrix (m);
        return p.play (38)[0];
    }();
    m.bleed (mixer::kickIn, snarePiece).invert = true;
    p.processor->setModMatrix (m);
    const auto flipped = p.play (38)[0];
    // Flipping that one mic's snare: the sum differs by twice that mic's 500 Hz.
    CHECK (levelAt (normal, 500.0, 2400, 9600) == Catch::Approx (levelAt (flipped, 500.0, 2400, 9600)).epsilon (0.02));
    CHECK (levelAt (normal, 300.0, 2400, 9600) == Catch::Approx (levelAt (flipped, 300.0, 2400, 9600)).epsilon (0.02));
    double difference = 0.0;
    for (size_t i = 2400; i < 9600; ++i)
        difference = std::max (difference, (double) std::abs (normal[i] - flipped[i]));
    CHECK (difference > 0.1);
    p.processor->setModMatrix ({});

    // Polarity of a whole channel: the snare top's sine upside down.
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    const auto up = p.play (38)[0];
    p.set (ParamIDs::mix (mixer::snareTop, "Phase"), 1.0f);
    const auto down = p.play (38)[0];
    for (size_t i = 100; i < 4000; i += 97)
        CHECK (down[i] == Catch::Approx (-up[i]).margin (1.0e-5));
    p.set (ParamIDs::mix (mixer::snareTop, "Phase"), 0.0f);

    // TIME OFFSET 5 ms: the channel's first sample 240 samples later.
    const auto firstSound = [] (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
                return (int) i;
        return -1;
    };
    const auto at = firstSound (up);
    INFO ("first sound " << at << ", rms " << rms (up, 0, 4000));
    REQUIRE (at >= 0);
    p.set (ParamIDs::mix (mixer::snareTop, "Offset"), 5.0f);
    CHECK (firstSound (p.play (38)[0]) - at == 240);
    p.set (ParamIDs::mix (mixer::snareTop, "Offset"), 0.0f);

    // RELEASE 50 ms: the channel quiet by 100 ms (the fade takes 30), the sine still going without it.
    p.set (ParamIDs::mix (mixer::snareTop, "Release"), 50.0f);
    const auto released = p.play (38)[0];
    CHECK (rms (released, (size_t) (0.1 * sampleRate), (size_t) (0.2 * sampleRate)) < 1.0e-4);
    CHECK (rms (released, 500, 1500) > 0.02);
    p.set (ParamIDs::mix (mixer::snareTop, "Release"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);

    // OUTPUT: the room to OUT 2 (on in the DAW): there, not in MAIN.
    p.set (ParamIDs::mix (mixer::room, "Output"), 1.0f);
    const auto out = p.play (38);
    const auto out2 = p.processor->getChannelIndexInProcessBlockBuffer (false, 1, 0);
    INFO ("channels " << out.size() << " out2 " << out2 << " total " << p.processor->getTotalNumOutputChannels());
    REQUIRE (out2 >= 0);
    REQUIRE (out2 < (int) out.size());
    CHECK (levelAt (out[(size_t) out2], 2000.0, 2400, 9600) == Catch::Approx (room).epsilon (0.05));
    CHECK (levelAt (out[0], 2000.0, 2400, 9600) < room * 0.001);
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::mix (mixer::room, "Output")));
    p.set (ParamIDs::mix (mixer::room, "Output"), 0.0f);

    // A synth drum (the clap) doesn't go through the mixer: every channel muted, it still plays.
    for (int c = 0; c < mixer::numChannels; ++c)
        p.set (ParamIDs::mix (c, "Mute"), 1.0f);
    CHECK (p.level (38, 300.0) < 1.0e-4);
    auto clapPeak = 0.0f;
    const auto clap = p.play (39);
    for (const auto x : clap[0])
        clapPeak = std::max (clapPeak, std::abs (x));
    CHECK (clapPeak > 0.05f);
}

TEST_CASE ("MIXER: VOICES counts hits, not mics; presets keep the channels and the bleed; the page", "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);

    // VOICES 2 on the snare: five quick hits of three mics each, at most two hits (six voices) sounding.
    p.set (ParamIDs::drum (2, "Voices"), 2.0f);
    juce::MidiBuffer midi;
    for (int i = 0; i < 5; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), i * 30);
    juce::AudioBuffer<float> buffer (2, blockSize);
    p.processor->processBlock (buffer, midi);
    juce::MidiBuffer none;
    for (int b = 0; b < 3; ++b)
        p.processor->processBlock (buffer, none);
    CHECK (p.processor->getDrumKit().getNumActiveVoices() <= 6);
    CHECK (p.processor->getDrumKit().getNumActiveVoices() >= 4);

    // The kit's 48-hit limit counts hits too: 20 snare hits of three mics each are 60 voices.
    p.set (ParamIDs::drum (2, "Voices"), 48.0f);
    for (int b = 0; b < (int) (1.5 * sampleRate / blockSize); ++b)   // the earlier hits rung out (1 s files)
        p.processor->processBlock (buffer, none);
    REQUIRE (p.processor->getDrumKit().getNumActiveVoices() == 0);
    midi.clear();
    for (int i = 0; i < 20; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 100), i * 10);
    p.processor->processBlock (buffer, midi);
    midi.clear();
    CHECK (p.processor->getDrumKit().getNumActiveVoices() == 60);

    // A preset keeps the channels' settings and the bleed table.
    p.set (ParamIDs::mix (mixer::overheads, "Level"), -3.0f);
    auto m = p.processor->getModMatrix();
    m.bleed (mixer::kickIn, 1) = { -9.0f, true, false };
    p.processor->setModMatrix (m);
    const auto rig = Rig::fromXml (*p.processor->captureRig().toXml());
    REQUIRE (rig.has_value());
    CHECK (rig->parameters.at (ParamIDs::mix (mixer::overheads, "Level")) == Catch::Approx (-3.0f));
    CHECK (rig->modulation.bleed (mixer::kickIn, 1) == mixer::BleedCell { -9.0f, true, false });

    // The page: the channels the kit plays into; a bleed row's change (one undo step).
    MixerPage page (*p.processor);
    page.setSize (1400, 650);
    CHECK (page.getChannelsInUse() == juce::Array<int> { mixer::kickIn, mixer::snareTop, mixer::overheads, mixer::room });
    page.selectChannel (mixer::room);
    page.setBleed (0, -4.0f, false, true);
    CHECK (p.processor->getModMatrix().bleed (mixer::room, 0).gainDb == Catch::Approx (-4.0f));
}

namespace
{
    // A send as the page would set it.
    void setSend (Plugin& p, int strip, int slot, const mixer::Send& send)
    {
        auto m = p.processor->getModMatrix();
        m.send (strip, slot) = send;
        p.processor->setModMatrix (m);
    }
    mixer::Send sendTo (int target, float directDb = 0.0f, mixer::SendMode mode = mixer::SendMode::postFader)
    {
        mixer::Send s;
        s.target = target;
        s.directDb = directDb;
        s.bleedDb = directDb;
        s.mode = mode;
        return s;
    }
    constexpr int busStrip (int bus) { return mixer::numChannels + bus; }
}

TEST_CASE ("MIXER BUSES: a channel routed into a bus, the bus's fader, mute, polarity and output; bus into a later bus only",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    const auto top = p.level (38, 300.0), room = p.level (38, 2000.0);
    REQUIRE (top > 0.05);

    // The snare top into BUS 1, at 0 dB: the same in MAIN.
    p.processor->setMixerRoute (mixer::snareTop, 1);
    CHECK (p.processor->getMixerRoute (mixer::snareTop) == 1);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));
    // The bus 6 dB down: the snare top alone.
    p.set (ParamIDs::mixBus (0, "Level"), -6.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    // Its pan hard left: nothing on the right.
    p.set (ParamIDs::mixBus (0, "Level"), 0.0f);
    p.set (ParamIDs::mixBus (0, "Pan"), -1.0f);
    {
        const auto out = p.play (38);
        CHECK (levelAt (out[1], 300.0, 2400, 9600) < top * 0.001);
        CHECK (levelAt (out[0], 300.0, 2400, 9600) == Catch::Approx (top).epsilon (0.02));
    }
    p.set (ParamIDs::mixBus (0, "Pan"), 0.0f);
    // MUTE the bus: the snare top gone, the room still there.
    p.set (ParamIDs::mixBus (0, "Mute"), 1.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
    p.set (ParamIDs::mixBus (0, "Mute"), 0.0f);
    // Polarity: the bus upside down (the snare top soloed, so only it).
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 1.0f);
    const auto up = p.play (38)[0];
    p.set (ParamIDs::mixBus (0, "Phase"), 1.0f);
    const auto down = p.play (38)[0];
    for (size_t i = 100; i < 4000; i += 97)
        CHECK (down[i] == Catch::Approx (-up[i]).margin (1.0e-5));
    p.set (ParamIDs::mixBus (0, "Phase"), 0.0f);
    p.set (ParamIDs::mix (mixer::snareTop, "Solo"), 0.0f);

    // BUS 1 into BUS 2, and BUS 2 down 6 dB: the snare top down 6 dB.
    p.processor->setMixerRoute (busStrip (0), 2);
    p.set (ParamIDs::mixBus (1, "Level"), -6.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    // BUS 2 can't go back into BUS 1 (it would loop): refused.
    p.processor->setMixerRoute (busStrip (1), 1);
    CHECK (p.processor->getMixerRoute (busStrip (1)) == 0);
    // ... nor if it's set some other way (an old preset): the bus plays to its output instead.
    p.set (ParamIDs::mixBus (1, "Route"), 1.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    p.set (ParamIDs::mixBus (1, "Route"), 0.0f);

    // BUS 2's OUTPUT to OUT 2: the snare top there, not in MAIN.
    p.processor->setMixerOutput (busStrip (1), 1);
    const auto out = p.play (38);
    const auto out2 = p.processor->getChannelIndexInProcessBlockBuffer (false, 1, 0);
    REQUIRE (out2 >= 0);
    CHECK (levelAt (out[(size_t) out2], 300.0, 2400, 9600) == Catch::Approx (top * 0.501).epsilon (0.02));
    CHECK (levelAt (out[0], 300.0, 2400, 9600) < top * 0.001);
    CHECK (levelAt (out[0], 2000.0, 2400, 9600) == Catch::Approx (room).epsilon (0.02));
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::mixBus (1, "Output")));
    CHECK (! ParamIDs::notInRigs.contains (ParamIDs::mixBus (1, "Route")));
    CHECK (! ParamIDs::notInRigs.contains (ParamIDs::mix (0, "Route")));
    // Choosing an output takes the channel off its bus.
    p.processor->setMixerOutput (mixer::snareTop, 0);
    CHECK (p.processor->getMixerRoute (mixer::snareTop) == 0);

    // SOLO among the buses: BUS 3 soloed silences BUS 1's snare top; the channels straight to MAIN still play.
    p.processor->setMixerRoute (mixer::snareTop, 1);
    p.processor->setMixerRoute (busStrip (0), 0);
    p.set (ParamIDs::mixBus (2, "Solo"), 1.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    CHECK (p.level (38, 2000.0) == Catch::Approx (room).epsilon (0.02));
}

TEST_CASE ("MIXER SENDS: pre / post fader, direct and bleed apart with their own pans, MUTE MODE, into DELAY, REVERB and an FX bus",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    p.processor->importSfzKit (writeKit (temp.folder), drums::notemap::Layout::generalMidi);
    REQUIRE (p.processor->waitForInstruments());
    p.set (ParamIDs::drumsMap, (float) drums::notemap::Layout::generalMidi);
    p.set (ParamIDs::drum (2, "VelToVol"), 0.0f);
    const auto top = p.level (38, 300.0), bleed = p.level (38, 500.0);
    REQUIRE (top > 0.05);

    // The snare top sent at -6 dB into BUS 1 (to MAIN): the sine 1.5 times as loud (in phase).
    setSend (p, mixer::snareTop, 0, sendTo (1, -6.0f));
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 1.501).epsilon (0.02));
    // POST FADER: the channel's fader down to -inf takes the send with it; PRE FADER: the send alone.
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), -60.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    setSend (p, mixer::snareTop, 0, sendTo (1, -6.0f, mixer::SendMode::preFader));
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::snareTop, "Level"), 0.0f);

    // MUTE MODE. PRE SEND (as before): a muted channel's sends are silent too.
    p.set (ParamIDs::mix (mixer::snareTop, "Mute"), 1.0f);
    CHECK (p.level (38, 300.0) < top * 0.001);
    // POST SEND: its pre-fader send goes on (the snare's "reverb alone"); a post-fader one doesn't.
    p.set (ParamIDs::mixMuteMode, 1.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top * 0.501).epsilon (0.02));
    setSend (p, mixer::snareTop, 0, sendTo (1, -6.0f));
    CHECK (p.level (38, 300.0) < top * 0.001);
    p.set (ParamIDs::mix (mixer::snareTop, "Mute"), 0.0f);
    p.set (ParamIDs::mixMuteMode, 0.0f);
    setSend (p, mixer::snareTop, 0, {});

    // DIRECT and BLEED: KICK IN carries the snare as bleed (500 Hz). Muted with POST SEND, a pre-fader send to BUS 1:
    // its bleed level, on and pan apart from its direct sound's.
    p.set (ParamIDs::mix (mixer::kickIn, "Mute"), 1.0f);
    p.set (ParamIDs::mixMuteMode, 1.0f);
    auto send = sendTo (1, 0.0f, mixer::SendMode::preFader);
    send.bleedOn = false;
    setSend (p, mixer::kickIn, 0, send);
    CHECK (p.level (38, 500.0) < bleed * 0.001);
    CHECK (p.level (36, 100.0) > 0.05);   // the kick's own sound in it, still sent
    send.bleedOn = true;
    send.bleedDb = -12.0f;
    setSend (p, mixer::kickIn, 0, send);
    CHECK (p.level (38, 500.0) == Catch::Approx (bleed * 0.251).epsilon (0.02));
    send.bleedDb = 0.0f;
    send.bleedPan = 1.0f;   // the bleed hard right
    setSend (p, mixer::kickIn, 0, send);
    {
        const auto out = p.play (38);
        CHECK (levelAt (out[0], 500.0, 2400, 9600) < bleed * 0.001);
        CHECK (levelAt (out[1], 500.0, 2400, 9600) == Catch::Approx (bleed).epsilon (0.02));
    }
    setSend (p, mixer::kickIn, 0, {});
    p.set (ParamIDs::mix (mixer::kickIn, "Mute"), 0.0f);
    p.set (ParamIDs::mixMuteMode, 0.0f);

    // REVERB: the snare top sent there (the REVERB on): sound after the 1 s sine has stopped; without the send, none.
    p.set (ParamIDs::reverbOn, 1.0f);
    const auto tail = [&]
    {
        const auto out = p.play (38, 1.6);
        return rms (out[0], (size_t) (1.15 * sampleRate), (size_t) (1.5 * sampleRate));
    };
    const auto dry = tail();
    setSend (p, mixer::snareTop, 0, sendTo (mixer::reverbTarget));
    const auto wet = tail();
    INFO ("dry tail " << dry << ", wet tail " << wet);
    CHECK (dry < 1.0e-4);
    CHECK (wet > 1.0e-3);
    // ... and with the REVERB off, the send is silent (nothing to hear it).
    p.set (ParamIDs::reverbOn, 0.0f);
    CHECK (p.level (38, 300.0) == Catch::Approx (top).epsilon (0.02));
    setSend (p, mixer::snareTop, 0, {});

    // An FX BUS: the snare top routed into FX bus A, whose OUTPUT is OUT 2: the snare top there.
    p.set (ParamIDs::ins (0, "Output"), 1.0f);
    p.processor->setMixerRoute (mixer::snareTop, mixer::firstFxRoute);
    const auto out = p.play (38);
    const auto out2 = p.processor->getChannelIndexInProcessBlockBuffer (false, 1, 0);
    REQUIRE (out2 >= 0);
    CHECK (levelAt (out[(size_t) out2], 300.0, 2400, 9600) > top * 0.5);
    CHECK (levelAt (out[0], 300.0, 2400, 9600) < top * 0.001);
    // A send into FX bus A as well as the channel to MAIN: in both.
    p.processor->setMixerRoute (mixer::snareTop, 0);
    setSend (p, mixer::snareTop, 1, sendTo (mixer::firstFxRoute));
    const auto both = p.play (38);
    CHECK (levelAt (both[(size_t) out2], 300.0, 2400, 9600) > top * 0.5);
    CHECK (levelAt (both[0], 300.0, 2400, 9600) == Catch::Approx (top).epsilon (0.02));
}

TEST_CASE ("MIXER MENU: STEREO REVERSE, STEREO and MULTICHANNEL OUTPUTS; presets keep the sends, routes and mute mode, not outputs",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"), true);
    auto& state = p.processor->getParameters();
    const auto value = [&state] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };

    p.set (ParamIDs::mix (mixer::hiHat, "Pan"), -0.4f);
    p.set (ParamIDs::mixBus (3, "Pan"), 0.7f);
    auto send = sendTo (2);
    send.directPan = 0.5f;
    send.bleedPan = -0.25f;
    setSend (p, mixer::room, 1, send);
    p.processor->mixerStereoReverse();
    CHECK (value (ParamIDs::mix (mixer::hiHat, "Pan")) == Catch::Approx (0.4f));
    CHECK (value (ParamIDs::mixBus (3, "Pan")) == Catch::Approx (-0.7f));
    CHECK (value (ParamIDs::mix (mixer::kickIn, "Pan")) == Catch::Approx (0.0f));
    CHECK (p.processor->getModMatrix().send (mixer::room, 1).directPan == Catch::Approx (-0.5f));
    CHECK (p.processor->getModMatrix().send (mixer::room, 1).bleedPan == Catch::Approx (0.25f));

    // MULTICHANNEL: by instrument; STEREO: everything to MAIN, off the buses.
    p.processor->setMixerRoute (mixer::tom2, 3);
    p.processor->applyMixerOutputs (true);
    CHECK (p.processor->getMixerOutput (mixer::kickOut) == 1);
    CHECK (p.processor->getMixerOutput (mixer::snareBottom) == 2);
    CHECK (p.processor->getMixerOutput (mixer::tom2) == 4);
    CHECK (p.processor->getMixerRoute (mixer::tom2) == 0);
    CHECK (p.processor->getMixerOutput (mixer::ambience) == 7);
    CHECK (p.processor->getMixerOutput (busStrip (0)) == 0);
    p.processor->applyMixerOutputs (false);
    for (int strip = 0; strip < mixer::numStrips; ++strip)
        CHECK (p.processor->getMixerOutput (strip) == 0);

    // A preset: routes, mute mode and sends kept; outputs left with the project.
    p.processor->setMixerRoute (mixer::snareTop, 4);
    p.processor->setMixerRoute (busStrip (3), 6);
    p.set (ParamIDs::mixMuteMode, 1.0f);
    p.processor->setMixerOutput (busStrip (5), 3);
    RigLibrary::Entry entry;
    entry.rig = *Rig::fromXml (*p.processor->captureRig().toXml());
    p.processor->setMixerRoute (mixer::snareTop, 0);
    p.processor->setMixerRoute (busStrip (3), 0);
    p.set (ParamIDs::mixMuteMode, 0.0f);
    setSend (p, mixer::room, 1, {});
    p.processor->setMixerOutput (busStrip (5), 5);
    p.processor->loadRig (entry);
    CHECK (p.processor->getMixerRoute (mixer::snareTop) == 4);
    CHECK (p.processor->getMixerRoute (busStrip (3)) == 6);
    CHECK (value (ParamIDs::mixMuteMode) == Catch::Approx (1.0f));
    CHECK (p.processor->getModMatrix().send (mixer::room, 1).target == 2);
    CHECK (p.processor->getMixerOutput (busStrip (5)) == 5);
    // The sends as XML and back.
    const auto xml = p.processor->getModMatrix().toXml();
    CHECK (ModMatrix::fromXml (*xml) == p.processor->getModMatrix());
}

TEST_CASE ("MIXER page 0.21.0: bus strips, MICS / BUSES, the OUTPUT and SEND buttons' words, a send set from its editor, MENU...",
           "[mixer]")
{
    TempFolder temp;
    Plugin p (temp.folder.getChildFile ("Presets"));
    MixerPage page (*p.processor);
    page.setSize (1400, 650);

    // Every channel and bus shown; MICS off leaves the buses (the chosen channel gives way to the first bus).
    for (int strip = 0; strip < mixer::numStrips; ++strip)
        CHECK (page.isStripShown (strip));
    page.selectChannel (mixer::snareTop);
    page.showGroups (false, true);
    CHECK (! page.isStripShown (mixer::snareTop));
    CHECK (page.isStripShown (busStrip (0)));
    CHECK (page.getSelectedStrip() == busStrip (0));
    CHECK (page.getTab() == MixerPage::Tab::sends);   // a bus: its sends
    page.showGroups (true, true);

    // OUTPUT's words: the output, or the bus / FX bus it's routed into.
    CHECK (page.describeOutput (mixer::room) == "Main");
    p.processor->setMixerRoute (mixer::room, 3);
    CHECK (page.describeOutput (mixer::room) == "Bus 3");
    p.processor->setMixerRoute (mixer::room, mixer::firstFxRoute + 1);
    CHECK (page.describeOutput (mixer::room) == "FX Bus B");
    p.processor->setMixerOutput (mixer::room, 4);
    CHECK (page.describeOutput (mixer::room) == "Out 5");

    // A send's target from its slot's menu: one undo step; a bus can't send into an earlier bus; OFF clears it.
    page.setSendTarget (mixer::overheads, 1, mixer::reverbTarget);
    CHECK (page.describeSend (mixer::overheads, 1) == "Reverb");
    CHECK (page.describeSend (mixer::overheads, 0) == "+ SEND");
    page.setSendTarget (busStrip (4), 0, 2);
    CHECK (! p.processor->getModMatrix().send (busStrip (4), 0).isOn());
    page.setSendTarget (busStrip (4), 0, 7);
    CHECK (p.processor->getModMatrix().send (busStrip (4), 0).target == 7);
    page.setSendTarget (busStrip (4), 0, 0);
    CHECK (p.processor->getModMatrix().send (busStrip (4), 0) == mixer::Send {});

    // The editor's send for the chosen channel.
    page.selectChannel (mixer::kickIn);
    page.showTab (MixerPage::Tab::sends);
    auto send = sendTo (mixer::firstFxRoute, -3.0f, mixer::SendMode::preFader);
    send.bleedOn = false;
    page.setSend (1, send);
    CHECK (p.processor->getModMatrix().send (mixer::kickIn, 1) == send);

    // MENU...: POST SEND, STEREO OUTPUTS, MULTICHANNEL OUTPUTS (with a note on the DAW's outputs).
    page.runMenuItem (MixerPage::postSend);
    CHECK (p.processor->getParameters().getRawParameterValue (ParamIDs::mixMuteMode)->load() == Catch::Approx (1.0f));
    page.runMenuItem (MixerPage::multichannelOutputs);
    CHECK (page.describeOutput (mixer::snareBottom) == "Out 3");
    CHECK (page.getNote().contains ("DAW"));
    page.runMenuItem (MixerPage::stereoOutputs);
    CHECK (page.describeOutput (mixer::snareBottom) == "Main");
    page.update();
}
