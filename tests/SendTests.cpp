// THE SENDS (PR7-K's stage 7b): each drum's DELAY and REVERB sends, their returns into the mix, DELAY > REVERB,
// measured from what the plugin plays (steady sines as drum samples: PluginTestUtils.h). (The effects themselves: DelayReverbTests.cpp, PR7-G's own tests.)
#include "TestUtils.h"
#include "PluginTestUtils.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/FxPage.h"
#include "ui/MainView.h"

namespace
{
    struct Plugin
    {
        ParagonX1AudioProcessor processor;
        Plugin()
        {
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            processor.seedRandomForTest (1);
            // The delay: plain DIGITAL repeats every 200 ms, the echoes as loud as the playing (MIX 100%).
            set (ParamIDs::delaySync, 0.0f);
            set (ParamIDs::delayTime, 200.0f);
            set (ParamIDs::delayFeedback, 0.4f);
            set (ParamIDs::delayMix, 1.0f);
        }
        void set (const juce::String& id, float value)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }
        // Another drum playing with slot 0, `semitones` above it.
        void addDrum (int slot, float semitones) { others.push_back ({ slot, semitones }); }
        // A sine at `note`'s pitch, `held` seconds long, from slot 0 (and the drums added, each at its own pitch), hit
        // at the start; `seconds` in all. Both channels.
        std::array<std::vector<float>, 2> play (int note, double held, double seconds, const std::function<void (int block)>& during = {})
        {
            juce::MidiBuffer starts;   // as MIDI from the DAW (the on-screen keyboard's presses land by the clock: not repeatable)
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            testkit::useSample (processor, 0, testkit::sine (noteHz (note), held));
            starts.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor (0), 0.8f), 0);
            for (const auto& [slot, semitones] : others)
            {
                testkit::useSample (processor, slot, testkit::sine (noteHz (note + semitones), held));
                starts.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor (slot), 0.8f), 0);
            }
            const auto blocks = (int) (seconds * sampleRate) / blockSize;
            for (int b = 0; b < blocks; ++b)
            {
                if (during)
                    during (b);
                auto midi = std::exchange (starts, {});   // the hits in the first block, at its first sample
                buffer.clear();
                processor.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + blockSize);
            }
            return out;
        }
        std::vector<std::pair<int, float>> others;
    };

    // A note's level over [from, from + length) seconds.
    double tone (const std::vector<float>& x, double hz, double from, double length = 0.08)
    {
        return levelAt (x, hz, (size_t) (from * sampleRate), (size_t) (length * sampleRate));
    }
    double peakOf (const std::vector<float>& x, double from = 0.0)
    {
        auto peak = 0.0f;
        for (auto i = (size_t) (from * sampleRate); i < x.size(); ++i)
            peak = std::max (peak, std::abs (x[i]));
        return peak;
    }
}

TEST_CASE ("SENDS: each drum's DELAY send: 100% echoes, 0% none, the dry sound as before", "[sends]")
{
    // A4 on drum 1 (sending 100%), E5 on drum 2 (sending nothing), each 100 ms long.
    Plugin p;
    p.addDrum (1, 7.0f);
    p.set (ParamIDs::drum (1, "SendDelay"), 0.0f);
    p.set (ParamIDs::delayOn, 1.0f);
    const auto out = p.play (69, 0.1, 0.8);
    // While they play: both.
    CHECK (tone (out[0], 440.0, 0.02) > 0.1);
    CHECK (tone (out[0], noteHz (76), 0.02) > 0.1);
    // The first echo (200-300 ms): A4 only, at the playing's level (MIX 100%); the second (400-500 ms) at FEEDBACK's 40%.
    const auto echo = tone (out[0], 440.0, 0.21), second = tone (out[0], 440.0, 0.41);
    CHECK_THAT (echo / tone (out[0], 440.0, 0.02), Catch::Matchers::WithinRel (1.0, 0.1));
    CHECK_THAT (second / echo, Catch::Matchers::WithinRel (0.4, 0.15));
    // Drum 2 adds nothing to the echoes: after both end, the same as without it, sample for sample.
    Plugin alone;
    alone.set (ParamIDs::delayOn, 1.0f);
    const auto withoutIt = alone.play (69, 0.1, 0.8);
    const auto after = (size_t) (0.13 * sampleRate);
    CHECK (std::vector<float> (out[0].begin() + (long) after, out[0].end()) == std::vector<float> (withoutIt[0].begin() + (long) after, withoutIt[0].end()));
    // In the gaps (between the hit's end and its echo): silence.
    CHECK (peakOf (std::vector<float> (out[0].begin() + (long) (0.13 * sampleRate), out[0].begin() + (long) (0.19 * sampleRate))) < 1.0e-3);
}

TEST_CASE ("SENDS: a send at 50% gives echoes 6 dB down", "[sends]")
{
    const auto echoAt = [] (float send)
    {
        Plugin p;
        p.set (ParamIDs::drum (0, "SendDelay"), send);
        p.set (ParamIDs::delayOn, 1.0f);
        const auto out = p.play (69, 0.1, 0.4);
        return tone (out[0], 440.0, 0.21);
    };
    CHECK_THAT (20.0 * std::log10 (echoAt (0.5f) / echoAt (1.0f)), WithinAbs (-6.02, 0.2));
}

TEST_CASE ("SENDS: REVERB's tail comes from what's sent to it; DELAY > REVERB sends the echoes on into it", "[sends]")
{
    const auto tailAfter = [] (float send, float delayToReverb, bool delayOn)
    {
        Plugin p;
        p.set (ParamIDs::reverbOn, 1.0f);
        p.set (ParamIDs::reverbType, (float) (int) effects::Reverb::Type::hall);
        p.set (ParamIDs::reverbMix, 1.0f);
        p.set (ParamIDs::drum (0, "SendReverb"), send);
        p.set (ParamIDs::delayOn, delayOn ? 1.0f : 0.0f);
        p.set (ParamIDs::delayFeedback, 0.0f);   // one echo (at 0.2 s), so anything long after is the reverb
        p.set (ParamIDs::delayToReverb, delayToReverb);
        const auto out = p.play (69, 0.2, 1.6);
        return rms (out[0], (size_t) (1.2 * sampleRate), out[0].size());   // long after the hit and its echoes
    };
    const auto full = tailAfter (1.0f, 0.0f, false);
    CHECK (full > 1.0e-3);                                 // a tail
    CHECK (tailAfter (0.0f, 0.0f, false) < 1.0e-7);        // nothing sent: no tail
    // Sending nothing to the REVERB, but the DELAY's echoes on into it: a tail (from the echoes), only with DELAY > REVERB.
    CHECK (tailAfter (0.0f, 1.0f, true) > 1.0e-3);
    CHECK (tailAfter (0.0f, 0.0f, true) < 1.0e-7);
}

TEST_CASE ("SENDS: stereo is kept: a sound on the left echoes on the left; PING-PONG bounces a centred one", "[sends]")
{
    for (const auto type : { effects::Delay::Type::digital, effects::Delay::Type::analog, effects::Delay::Type::tape, effects::Delay::Type::mod })
    {
        INFO ((int) type);
        Plugin p;
        p.set (ParamIDs::drum (0, "Pan"), -1.0f);
        p.set (ParamIDs::delayOn, 1.0f);
        p.set (ParamIDs::delayType, (float) (int) type);
        const auto out = p.play (69, 0.1, 0.6);
        CHECK (tone (out[0], 440.0, 0.21) > 0.05);
        if (type == effects::Delay::Type::tape)
            CHECK (tone (out[1], 440.0, 0.21) < 1.0e-4 * tone (out[0], 440.0, 0.21));   // the right: only TAPE's hiss, no echo
        else
            CHECK (peakOf (out[1]) == 0.0);   // the right: nothing at all
    }

    // PING-PONG (as on PR7-G: what's sent goes in as one, so even a centred sound bounces): left, then right.
    Plugin pp;
    pp.set (ParamIDs::delayOn, 1.0f);
    pp.set (ParamIDs::delayPingPong, 1.0f);
    pp.set (ParamIDs::delayFeedback, 0.6f);
    const auto out = pp.play (69, 0.1, 0.7);
    const auto l1 = tone (out[0], 440.0, 0.21), r1 = tone (out[1], 440.0, 0.21);
    const auto l2 = tone (out[0], 440.0, 0.41), r2 = tone (out[1], 440.0, 0.41);
    CHECK (l1 > 10.0 * r1);
    CHECK (r2 > 10.0 * l2);
}

TEST_CASE ("SENDS: switching DELAY on mid-hit changes nothing; send amounts don't matter while it's off", "[sends]")
{
    // The same hits three ways: no delay; the delay switched on half way (at MIX 0, so it adds nothing you'd hear);
    // and different send amounts on two drums with the delay off. The sound must be the same, sample for sample.
    const auto run = [] (int how)
    {
        Plugin p;
        p.addDrum (1, 12.0f);
        p.set (ParamIDs::drum (1, "LowPass"), 2000.0f);   // its own filter on the way, as before the sends
        p.set (ParamIDs::delayMix, 0.0f);
        if (how == 2)
            p.set (ParamIDs::drum (1, "SendDelay"), 0.3f);
        return p.play (57, 0.5, 0.5, [&p, how] (int block)
        {
            if (how == 1 && block == 40)
                p.set (ParamIDs::delayOn, 1.0f);
        })[0];
    };
    const auto plain = run (0);
    CHECK (run (1) == plain);
    CHECK (run (2) == plain);
}

TEST_CASE ("SENDS: the returns go through the rack (the LIMITER holds the echoes too)", "[sends][rack]")
{
    Plugin p;
    p.set (ParamIDs::delayOn, 1.0f);
    p.set (ParamIDs::delayMix, 1.0f);
    p.set (ParamIDs::delayFeedback, 0.9f);
    p.set (ParamIDs::reverbOn, 1.0f);
    p.set (ParamIDs::reverbMix, 1.0f);
    p.set (ParamIDs::outputGain, 12.0f);
    p.set (ParamIDs::limitOn, 1.0f);
    p.set (ParamIDs::limitGain, 6.0f);
    p.set (ParamIDs::limitCeiling, -1.0f);
    const auto out = p.play (57, 0.3, 1.5);
    for (int c = 0; c < 2; ++c)
        CHECK (20.0 * std::log10 (peakOf (out[(size_t) c], 0.05)) <= -1.0 + 0.01);
    CHECK (rms (out[0], (size_t) (1.0 * sampleRate), out[0].size()) > 1.0e-3);   // and the echoes and tail are there
}

TEST_CASE ("SENDS: CPU, the kit through DELAY and REVERB", "[sends][performance]")
{
    const auto cpu = [] (bool on)
    {
        auto best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            Plugin p;
            if (on)
            {
                p.set (ParamIDs::delayOn, 1.0f);
                p.set (ParamIDs::reverbOn, 1.0f);
            }
            juce::AudioBuffer<float> buffer (2, blockSize);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < (int) (4.0 * sampleRate) / blockSize; ++b)
            {
                juce::MidiBuffer midi;   // a drum every eighth of a second, round the kit
                if (b % 23 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor ((b / 23) % 12), (juce::uint8) 100), 0);
                p.processor.processBlock (buffer, midi);
            }
            best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - started) / 4000.0 * 100.0);
        }
        return best;
    };
    const auto off = cpu (false), on = cpu (true);
    WARN ("the kit's drum synths, a hit every eighth of a second: the sends off " << off << "% of one core; DELAY and REVERB on " << on << "%");
    CHECK (on < off + 15.0);   // generous for CI machines
}

TEST_CASE ("SENDS: each drum's sends reach the kit", "[sends]")
{
    Plugin p;
    p.set (ParamIDs::drum (3, "SendDelay"), 0.25f);
    p.set (ParamIDs::drum (3, "SendReverb"), 0.75f);
    p.set (ParamIDs::reverbOn, 1.0f);
    const auto s = p.processor.getKitSettings();
    CHECK_THAT (s.slots[3].send[0], WithinAbs (0.25, 1.0e-4));
    CHECK_THAT (s.slots[3].send[1], WithinAbs (0.75, 1.0e-4));
    CHECK (s.sendOn == std::array<bool, synth::numSends> { false, true });
    CHECK (p.processor.getDelaySettings().returnOnly);
    CHECK (p.processor.getReverbSettings().returnOnly);
}

TEST_CASE ("The FX page's SENDS view: DELAY and REVERB units, the matrix, their pages; laid out cleanly", "[sends][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    view->showPage (MainView::Page::fx);
    auto& page = view->getFxPage();
    CHECK (page.getView() == FxPage::View::rack);   // MASTER first, as in 0.17.0
    page.getSendsTab().onClick();
    CHECK (page.getView() == FxPage::View::sends);
    CHECK (page.getSendUnit (0).getSpec().name == "DELAY");
    CHECK (page.getSendUnit (1).getSpec().name == "REVERB");
    std::vector<juce::Component*> shown { &page.getSendUnit (0), &page.getSendUnit (1), &page.getSendMatrix(), &page.getSendsTab(), &page.getMasterTab() };
    for (size_t i = 0; i < shown.size(); ++i)
    {
        INFO (shown[i]->getBounds().toString());
        CHECK (shown[i]->isVisible());
        CHECK (page.getLocalBounds().contains (shown[i]->getBounds()));
        for (size_t j = i + 1; j < shown.size(); ++j)
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
    }
    CHECK_FALSE (page.getUnit (0).isVisible());

    // The matrix: a knob for each drum's REVERB send (the snare's, here); DELAY's power switch is delayOn.
    CHECK (page.getSendMatrix().send (2, 1).getParameterId() == ParamIDs::drum (2, "SendReverb"));
    for (int k = 0; k < 12; ++k)
        for (int which = 0; which < 2; ++which)
        {
            INFO (k << " " << which << " " << page.getSendMatrix().send (k, which).getBounds().toString());
            CHECK (page.getSendMatrix().send (k, which).getBounds().getWidth() > 20);
            CHECK (page.getSendMatrix().getLocalBounds().contains (page.getSendMatrix().send (k, which).getBounds()));
            if (k > 0)
                CHECK_FALSE (page.getSendMatrix().send (k, which).getBounds().intersects (page.getSendMatrix().send (k - 1, which).getBounds()));
        }
    auto& delayOn = *processor.getParameters().getParameter (ParamIDs::delayOn);
    page.getSendUnit (0).getPower().toggle();
    CHECK (delayOn.getValue() > 0.5f);

    // EDIT opens each page; every control inside it; BACK TO RACK returns to SENDS.
    for (int i = 0; i < 2; ++i)
    {
        page.getSendUnit (i).getEdit().onClick();
        auto& sendPage = i == 0 ? page.getDelayPage() : page.getReverbPage();
        CHECK (page.getView() == (i == 0 ? FxPage::View::delay : FxPage::View::reverb));
        CHECK (sendPage.isVisible());
        CHECK (sendPage.getSends().size() == 12);
        for (auto* c : sendPage.getControls())
        {
            INFO (c->getBounds().toString());
            CHECK (sendPage.getLocalBounds().contains (c->getBounds()));
        }
        for (auto* c : sendPage.getSends())
            CHECK (sendPage.getLocalBounds().contains (c->getBounds()));
        CHECK (sendPage.getLocalBounds().contains (sendPage.getDisplay().getBounds()));
        sendPage.onClose();
        CHECK (page.getView() == FxPage::View::sends);
    }

    // The echoes drawn: at the delay time, the first at MIX, each next FEEDBACK down; ping-pong alternating sides.
    auto set = [&] (const char* id, float v) { auto* q = processor.getParameters().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    set (ParamIDs::delaySync, 0.0f);
    set (ParamIDs::delayTime, 500.0f);
    set (ParamIDs::delayMix, 0.5f);
    set (ParamIDs::delayFeedback, 0.5f);
    set (ParamIDs::delayPingPong, 1.0f);
    const auto echoes = dynamic_cast<EchoDisplay&> (page.getDelayPage().getDisplay()).echoes();
    REQUIRE (echoes.size() >= 4);
    CHECK_THAT (echoes[0].seconds, WithinAbs (0.5, 1.0e-3));
    CHECK_THAT (echoes[1].seconds, WithinAbs (1.0, 1.0e-3));
    CHECK_THAT (echoes[0].level, WithinAbs (0.5, 1.0e-3));
    CHECK_THAT (echoes[1].level, WithinAbs (0.25, 1.0e-3));
    CHECK (echoes[0].side == -1);
    CHECK (echoes[1].side == 1);
}

TEST_CASE ("SENDS: the factory preset Room Kit: the snare rings on in the room, the kick stays dry", "[sends][rigs]")
{
    // A hit with the preset's REVERB on, and again with it off: the kick (sending nothing) the same sample for sample;
    // the snare (sending 60%) with a tail after it that's only there with the room.
    const auto hit = [] (int slot, bool room)
    {
        Plugin p;
        const auto* entry = p.processor.getRigLibrary().findFactory ("Room Kit");
        REQUIRE (entry != nullptr);
        p.processor.loadRig (*entry);
        CHECK (p.processor.getKitSettings().sendOn == std::array<bool, synth::numSends> { false, true });
        if (! room)
            p.set (ParamIDs::reverbOn, 0.0f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, testkit::keyFor (slot), (juce::uint8) 127), 0);
        juce::AudioBuffer<float> buffer (2, blockSize);
        std::vector<float> out;
        for (int b = 0; b < (int) (1.0 * sampleRate) / blockSize; ++b)
        {
            buffer.clear();
            p.processor.processBlock (buffer, midi);
            midi.clear();
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }
        return out;
    };
    CHECK (hit (0, true) == hit (0, false));
    const auto wet = hit (2, true), dry = hit (2, false);
    std::vector<float> room (wet.size());
    for (size_t i = 0; i < room.size(); ++i)
        room[i] = wet[i] - dry[i];
    const auto early = rms (dry, 0, (size_t) (0.1 * sampleRate));
    const auto tail = rms (room, (size_t) (0.25 * sampleRate), (size_t) (0.45 * sampleRate));
    WARN ("the snare's room, 0.25 to 0.45 s on, against its hit: " << db (tail / early) << " dB");
    CHECK (tail > 0.01 * early);   // within 40 dB of the hit: a room you'd hear
}
