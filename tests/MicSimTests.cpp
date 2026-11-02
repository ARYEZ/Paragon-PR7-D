// SIMULATED MICS AND ROOM (PR7-D 0.26.0): synth drums heard through simulated close mics, overheads, room and
// ambience in THE MIXER's channels. Measured: off by default (nothing changes), each mic's arrival time to the sample
// from its distance, the overheads' left / right from where the drum stands, bleed into another drum's close mic
// (quieter, BLEED and BLEED FROM INSTRUMENTS working on it), the room's tail (longer for a bigger room and with
// DECAY), the level kept, a drum left out (its MICS off), routing a mic (to none, to a USER channel and its meter),
// presets and kits keeping the settings, the CPU.
#include "TestUtils.h"
#include "PluginTestUtils.h"
#include <catch2/catch_approx.hpp>

#include "dsp/synth/MicSim.h"
#include "dsp/synth/Mixer.h"
#include "rigs/FactoryRigs.h"
#include "rigs/Rig.h"
#include "ui/MixerPage.h"

namespace
{
    using namespace synth;
    constexpr int snare = 2, hat = 6, kick = 0;

    struct TempFolder
    {
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("pr7d-micsim-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempFolder() { folder.createDirectory(); }
        ~TempFolder() { folder.deleteRecursively(); }
    };

    // A click: one sample at `level`, then silence.
    std::shared_ptr<const drums::Sample> click (float level = 0.5f)
    {
        auto s = std::make_shared<drums::Sample>();
        s->name = "click";
        s->channels.assign (1, std::vector<float> (4800, 0.0f));
        s->channels[0][0] = level;
        return s;
    }

    struct Plugin
    {
        std::unique_ptr<ParagonX1AudioProcessor> processor = std::make_unique<ParagonX1AudioProcessor>();
        TempFolder temp;
        Plugin()
        {
            processor->prepareToPlay (sampleRate, blockSize);
            processor->useRigFolder (temp.folder.getChildFile ("Presets"));
        }
        void set (const juce::String& id, float value) { testkit::set (*processor, id, value); }
        // MAIN (left, right), `seconds` of it, `slot` hit at velocity 127 at the start.
        std::array<std::vector<float>, 2> play (int slot, double seconds = 0.3)
        {
            processor->prepareToPlay (sampleRate, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (slot), (juce::uint8) 127), 0);
            const auto channels = processor->getTotalNumOutputChannels();
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (channels, blockSize);
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                processor->processBlock (buffer, midi);
                midi.clear();
                for (int c = 0; c < 2; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
            }
            return out;
        }
        void solo (int channel, bool on = true) { set (ParamIDs::mix (channel, "Solo"), on ? 1.0f : 0.0f); }
    };

    Rig djentKit()
    {
        for (const auto& rig : makeFactoryRigs())
            if (rig.name == "Djent Kit")
                return rig;
        FAIL ("no Djent Kit");
        return {};
    }

    int firstSound (const std::vector<float>& x, float threshold = 1.0e-6f)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > threshold)
                return (int) i;
        return -1;
    }
    double energy (const std::vector<float>& x, double from = 0.0, double to = 1.0e9)
    {
        double sum = 0.0;
        for (auto i = (size_t) (from * sampleRate); i < x.size() && (double) i < to * sampleRate; ++i)
            sum += (double) x[i] * x[i];
        return sum;
    }
    double peak (const std::array<std::vector<float>, 2>& x)
    {
        double p = 0.0;
        for (const auto& side : x)
            for (const auto v : side)
                p = std::max (p, (double) std::abs (v));
        return p;
    }
}

TEST_CASE ("SIMULATED MICS: off by default (a drum plays as before); on, it's heard through the mixer's channels", "[micsim]")
{
    Plugin p;
    testkit::useSample (*p.processor, snare, click());
    const auto dry = p.play (snare);
    REQUIRE (dry[0][0] > 0.1f);   // the click at the kit's level
    CHECK (firstSound (dry[0]) == 0);
    p.set (ParamIDs::drumsMicSim, 1.0f);
    const auto miked = p.play (snare);
    CHECK (energy (miked[0]) > 0.0);
    // Every channel muted: nothing (it went through them).
    for (int c = 0; c < mixer::numChannels; ++c)
        p.set (ParamIDs::mix (c, "Mute"), 1.0f);
    CHECK (peak (p.play (snare)) < 1.0e-6);
    // That drum's MICS off: it plays its own way again, the channels muted or not.
    p.set (ParamIDs::drum (snare, "Mics"), 0.0f);
    const auto own = p.play (snare);
    CHECK (own[0][0] == Catch::Approx (dry[0][0]).margin (1.0e-6));
}

TEST_CASE ("SIMULATED MICS: each mic hears the drum when its distance says (343 m/s), the overheads left first for the snare",
           "[micsim]")
{
    Plugin p;
    testkit::useSample (*p.processor, snare, click());
    p.set (ParamIDs::drumsMicSim, 1.0f);
    const auto piece = drums::pieceOfSlot (snare);
    const auto expected = [&] (int channel, bool right) { return micsim::tapFor (piece, channel, right, false, sampleRate).delay; };

    p.solo (mixer::snareTop);
    const auto top = p.play (snare);
    CHECK (firstSound (top[0]) == micsim::tapFor (piece, mixer::snareTop, false, true, sampleRate).delay);
    p.solo (mixer::snareTop, false);

    p.solo (mixer::overheads);
    const auto oh = p.play (snare);
    const auto left = firstSound (oh[0]), right = firstSound (oh[1]);
    INFO ("overheads: left " << left << ", right " << right);
    // The overheads' own pan: their channel's PAN in the middle sends each side to both; measure the pair apart by
    // panning the channel hard each way.
    p.set (ParamIDs::mix (mixer::overheads, "Pan"), -1.0f);
    const auto leftOnly = firstSound (p.play (snare)[0]);
    CHECK (leftOnly == expected (mixer::overheads, false));
    p.set (ParamIDs::mix (mixer::overheads, "Pan"), 1.0f);
    const auto rightOnly = firstSound (p.play (snare)[1]);
    CHECK (rightOnly == expected (mixer::overheads, true));
    CHECK (rightOnly > leftOnly);   // the snare is left of the kit's middle
    // About 1.3 m: around 170 samples later than the close mic.
    CHECK (leftOnly > 150);
    CHECK (leftOnly < 200);
    p.set (ParamIDs::mix (mixer::overheads, "Pan"), 0.0f);
    p.solo (mixer::overheads, false);

    // The room pair, about 2.8 m away; the ambience, about 5.5 m.
    p.solo (mixer::room);
    const auto room = firstSound (p.play (snare)[0]);
    CHECK (room == expected (mixer::room, false));
    CHECK (room > 350);
    p.solo (mixer::room, false);
    p.solo (mixer::ambience);
    const auto ambience = firstSound (p.play (snare, 0.4)[0]);
    CHECK (ambience == expected (mixer::ambience, false));
    CHECK (ambience > 700);
    p.solo (mixer::ambience, false);

    // TIME OFFSET moves a channel later, as for a sampled kit.
    p.solo (mixer::snareTop);
    p.set (ParamIDs::mix (mixer::snareTop, "Offset"), 2.0f);
    CHECK (firstSound (p.play (snare)[0]) == micsim::tapFor (piece, mixer::snareTop, false, true, sampleRate).delay + 96);
}

TEST_CASE ("SIMULATED MICS: another drum in a close mic is bleed: quieter and duller, BLEED and BLEED FROM INSTRUMENTS set it",
           "[micsim]")
{
    Plugin p;
    testkit::useSample (*p.processor, snare, testkit::sine (1000.0, 1.0, 0.25f));
    testkit::useSample (*p.processor, hat, testkit::sine (1000.0, 1.0, 0.25f));
    p.set (ParamIDs::drumsMicSim, 1.0f);
    p.solo (mixer::hiHat);
    const auto hatOwn = levelAt (p.play (hat)[0], 1000.0, 2400, 9600);   // 0.05 to 0.25 s
    const auto snareBleed = levelAt (p.play (snare)[0], 1000.0, 2400, 9600);
    INFO ("hat in its mic " << hatOwn << ", the snare in it " << snareBleed);
    REQUIRE (hatOwn > 0.01);
    CHECK (snareBleed < hatOwn * 0.3);   // more than 10 dB down
    CHECK (snareBleed > hatOwn * 0.03);  // but there
    // The channel's BLEED off: the snare gone from it, the hat's own sound kept.
    p.set (ParamIDs::mix (mixer::hiHat, "BleedOn"), 0.0f);
    CHECK (levelAt (p.play (snare)[0], 1000.0, 2400, 9600) < snareBleed * 0.001);
    CHECK (levelAt (p.play (hat)[0], 1000.0, 2400, 9600) == Catch::Approx (hatOwn).epsilon (0.02));
    p.set (ParamIDs::mix (mixer::hiHat, "BleedOn"), 1.0f);
    // BLEED FROM INSTRUMENTS: the snare in the hat mic 6 dB down.
    auto m = p.processor->getModMatrix();
    m.bleed (mixer::hiHat, drums::pieceOfSlot (snare)).gainDb = -6.0f;
    p.processor->setModMatrix (m);
    CHECK (levelAt (p.play (snare)[0], 1000.0, 2400, 9600) == Catch::Approx (snareBleed * 0.501).epsilon (0.03));
    p.solo (mixer::hiHat, false);

    // Duller: the snare's 8 kHz in the hat mic falls further than its 1 kHz does (off the mic's axis).
    testkit::useSample (*p.processor, snare, testkit::sine (8000.0, 1.0, 0.25f));
    p.processor->setModMatrix ({});
    p.solo (mixer::snareTop);
    const auto highOwn = levelAt (p.play (snare)[0], 8000.0, 2400, 9600);
    p.solo (mixer::snareTop, false);
    p.solo (mixer::hiHat);
    const auto highBleed = levelAt (p.play (snare)[0], 8000.0, 2400, 9600);
    testkit::useSample (*p.processor, snare, testkit::sine (1000.0, 1.0, 0.25f));
    const auto lowBleed = levelAt (p.play (snare)[0], 1000.0, 2400, 9600);
    p.solo (mixer::hiHat, false);
    p.solo (mixer::snareTop);
    const auto lowOwn = levelAt (p.play (snare)[0], 1000.0, 2400, 9600);
    INFO ("8 kHz: own " << highOwn << " bleed " << highBleed << "; 1 kHz: own " << lowOwn << " bleed " << lowBleed);
    CHECK (highBleed / highOwn < 0.5 * lowBleed / lowOwn);
}

TEST_CASE ("SIMULATED MICS: the room rings on after the hit; a bigger room and more DECAY longer, TONE brighter", "[micsim]")
{
    Plugin p;
    testkit::useSample (*p.processor, snare, click());
    p.set (ParamIDs::drumsMicSim, 1.0f);
    p.solo (mixer::room);
    const auto tail = [&] (int room, float decay, double from, double to)
    {
        p.set (ParamIDs::drumsRoom, (float) room);
        p.set (ParamIDs::drumsRoomDecay, decay);
        const auto x = p.play (snare, 3.0);
        return energy (x[0], from, to) + energy (x[1], from, to);
    };
    const auto small = tail (0, 1.0f, 0.3, 0.8), studio = tail (1, 1.0f, 0.3, 0.8), hall = tail (3, 1.0f, 0.3, 0.8);
    INFO ("0.3 to 0.8 s: small " << small << ", studio " << studio << ", hall " << hall);
    CHECK (studio > small * 10.0);
    CHECK (hall > studio * 3.0);
    CHECK (tail (1, 2.0f, 0.3, 0.8) > studio * 3.0);
    // The early part: reflections, then the tail: something in the first 50 ms after the direct sound.
    CHECK (tail (1, 1.0f, 0.0, 0.06) > 0.0);
    // A Studio's tail gone well before 3 s (the reverb stops itself).
    CHECK (tail (1, 1.0f, 2.0, 3.0) < 1.0e-12);
    // TONE: brighter tails have more highs.
    p.set (ParamIDs::drumsRoom, 1.0f);
    p.set (ParamIDs::drumsRoomDecay, 1.0f);
    const auto brightness = [&] (float tone)
    {
        p.set (ParamIDs::drumsRoomTone, tone);
        const auto x = p.play (snare, 1.6);
        return Spectrum (std::vector<float> (x[0].begin() + 4800, x[0].end()), 0).at (8000.0)
             - Spectrum (std::vector<float> (x[0].begin() + 4800, x[0].end()), 0).at (500.0);
    };
    CHECK (brightness (1.0f) > brightness (0.0f) + 6.0);
}

TEST_CASE ("SIMULATED MICS: a modelled snare through every mic about as loud as without them; the Djent Kit uses them",
           "[micsim]")
{
    Plugin p;
    p.set (ParamIDs::drum (snare, "Model"), (float) drums::Model::acousticSnare);
    const auto dry = peak (p.play (snare, 0.5));
    p.set (ParamIDs::drumsMicSim, 1.0f);
    const auto miked = peak (p.play (snare, 0.5));
    INFO ("dry " << juce::Decibels::gainToDecibels (dry) << " dB, miked " << juce::Decibels::gainToDecibels (miked) << " dB");
    CHECK (std::abs (juce::Decibels::gainToDecibels (miked / dry)) < 3.0);

    // The factory Djent Kit: on, Live Room.
    const auto rig = djentKit();
    CHECK (rig.parameters.at (ParamIDs::drumsMicSim) > 0.5f);
}

TEST_CASE ("SIMULATED MICS: a mic routed to NONE is silent, to a USER channel plays there (its meter too)", "[micsim]")
{
    Plugin p;
    testkit::useSample (*p.processor, snare, click());
    p.set (ParamIDs::drumsMicSim, 1.0f);
    const auto piece = drums::pieceOfSlot (snare);
    p.processor->setMicRoute (piece, mixer::snareTop, mixer::routeNone);
    p.solo (mixer::snareTop);
    CHECK (peak (p.play (snare)) < 1.0e-7);
    p.solo (mixer::snareTop, false);
    const auto user = p.processor->createUserChannel (piece, mixer::snareTop);
    REQUIRE (user >= mixer::numChannels);
    p.processor->takeMixerPeak (user);
    p.solo (user);
    const auto there = p.play (snare);
    CHECK (firstSound (there[0]) == micsim::tapFor (piece, mixer::snareTop, false, true, sampleRate).delay);
    CHECK (p.processor->takeMixerPeak (user) > 0.05f);
}

TEST_CASE ("SIMULATED MICS: presets and kits keep the switch, the room and each drum's MICS", "[micsim]")
{
    for (const auto* id : { ParamIDs::drumsMicSim, ParamIDs::drumsRoom, ParamIDs::drumsRoomDecay, ParamIDs::drumsRoomTone })
        CHECK (! ParamIDs::notInRigs.contains (id));
    CHECK (! ParamIDs::notInRigs.contains (ParamIDs::drum (snare, "Mics")));
    Plugin p;
    p.set (ParamIDs::drumsMicSim, 1.0f);
    p.set (ParamIDs::drumsRoom, 3.0f);
    p.set (ParamIDs::drum (kick, "Mics"), 0.0f);
    REQUIRE (p.processor->saveDrumKit ("Miked"));
    p.set (ParamIDs::drumsMicSim, 0.0f);
    p.set (ParamIDs::drumsRoom, 0.0f);
    p.set (ParamIDs::drum (kick, "Mics"), 1.0f);
    REQUIRE (p.processor->loadDrumKit (p.processor->drumKitsFolder().getChildFile ("Miked.pr7kit")));
    const auto value = [&] (const juce::String& id)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p.processor->getParameters().getParameter (id));
        return parameter->convertFrom0to1 (parameter->getValue());
    };
    CHECK (value (ParamIDs::drumsMicSim) > 0.5f);
    CHECK (value (ParamIDs::drumsRoom) == Catch::Approx (3.0f));
    CHECK (value (ParamIDs::drum (kick, "Mics")) < 0.5f);
}

TEST_CASE ("SIMULATED MICS: CPU, the Djent Kit's beat through every mic and the room", "[micsim][cpu]")
{
    Plugin p;
    for (const auto& [id, value] : djentKit().parameters)
        if (p.processor->getParameters().getParameter (id) != nullptr)
            p.set (id, value);
    p.set (ParamIDs::drumsMicSim, 1.0f);
    // Kick, snare and hats on sixteenths, toms, crash and china now and then, for 8 s.
    const auto seconds = 8.0;
    juce::AudioBuffer<float> buffer (p.processor->getTotalNumOutputChannels(), blockSize);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto sixteenth = (int) (sampleRate * 60.0 / 130.0 / 4.0);
    int n = 0;
    for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
    {
        juce::MidiBuffer midi;
        for (int i = 0; i < blockSize; ++i)
            if ((b * blockSize + i) % sixteenth == 0)
            {
                const int pattern[] = { kick, hat, snare, hat, kick, hat, snare, 7, kick, hat, snare, 11, kick, 15, snare, 9 };
                midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (pattern[n % 16]), (juce::uint8) 110), i);
                if (pattern[n % 16] != hat)
                    midi.addEvent (juce::MidiMessage::noteOn (10, testkit::keyFor (hat), (juce::uint8) 90), i);
                ++n;
            }
        buffer.clear();
        p.processor->processBlock (buffer, midi);
    }
    const auto share = (juce::Time::getMillisecondCounterHiRes() - start) / (seconds * 1000.0);
    WARN ("simulated mics CPU: " << share * 100.0 << "% of one core");
    CHECK (share < 0.35);
}


TEST_CASE ("SIMULATED MICS page: SIM MICS... opens its panel; a drum's button switches all its articulations; channels in use",
           "[micsim]")
{
    Plugin p;
    MixerPage page (*p.processor);
    page.setSize (1300, 700);
    CHECK (page.getChannelsInUse().isEmpty());
    p.set (ParamIDs::drumsMicSim, 1.0f);
    const auto used = page.getChannelsInUse();
    for (const auto c : { mixer::kickIn, mixer::snareBottom, mixer::cymbals, mixer::overheads, mixer::ambience, mixer::other })
        CHECK (used.contains (c));
    page.openMicSim (true);
    CHECK (page.isMicSimOpen());
    // A drum out: all its articulations (the hi-hat's six).
    const auto hatPiece = drums::pieceOfSlot (hat);
    page.setPieceMiked (hatPiece, false);
    CHECK (! page.isPieceMiked (hatPiece));
    for (int a = 0; a < drums::pieces[(size_t) hatPiece].count; ++a)
        CHECK (p.processor->getParameters().getRawParameterValue (ParamIDs::drum (drums::pieces[(size_t) hatPiece].articulations[(size_t) a].slot, "Mics"))->load() < 0.5f);
    // ROUTE MICS lists the simulated drums' mics (and opening it closes this panel).
    page.openRouter (true);
    CHECK (! page.isMicSimOpen());
    page.chooseRouterPiece (drums::pieceOfSlot (snare));
    CHECK (page.getRouterMics().contains (mixer::overheads));
    CHECK (page.getRouterMics().contains (mixer::snareTop));
}
