#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Reverb.h"
#include "ui/ParagonLookAndFeel.h"

// Hidden test (the "[.]" tag): renders the editor to a PNG for reviewing the
// UI without a DAW. Run with:
//   PARAGON_SNAPSHOT=editor.png ./ParagonX1Tests "[snapshot]"
// Options (environment variables):
//   PARAGON_SNAPSHOT_WIDTH=n         the window's width (default 1600; 2560 for a sharp 2x look)
//   PARAGON_SNAPSHOT_PAGE=presets    the preset browser (with two example user presets), or =settings
//   PARAGON_SNAPSHOT_PRESET=name     that factory preset loaded
//   PARAGON_SNAPSHOT_OSC=n           oscillator tab n (1 to 8) selected
//   PARAGON_SNAPSHOT_OSCPAGE=voices  the oscillator's VOICES page (unison, vibrato, chorder); =shape its SHAPE page; =fm its FM page; =fft its FFT page; =draw its DRAW page; =travel its TRAVEL page (PARAGON_SNAPSHOT_TRAVEL=1: oscillator 1 with a demo envelope and pattern)
//   PARAGON_SNAPSHOT_FILTERPAGE=n     the FILTER panel's SHP n tab (shaper n's page)
//   PARAGON_SNAPSHOT_FFT=band:dB,...  a curve drawn for oscillator 1; PARAGON_SNAPSHOT_FFT_VIEW=harmonic|bins
//   PARAGON_SNAPSHOT_PARAMS=id=value,...   parameters set (real units)
//   PARAGON_SNAPSHOT_PAGE=drums      THE DRUMS (PARAGON_SNAPSHOT_DRUM=n: slot n chosen; PARAGON_SNAPSHOT_DRUMS=1: a beat just played, its rows lit;
//                                    PARAGON_SNAPSHOT_DRUMSEQ=1: the SEQUENCER view, a beat playing with the song;
//                                    PARAGON_SNAPSHOT_DRUMEDIT=1: the clap with an envelope, an anchor, GAIN and LP)
//   PARAGON_SNAPSHOT_PAGE=fx         THE RACK (PARAGON_SNAPSHOT_RACK=1: at work on a chord, the sends too; PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi: that view, the inserts' on bus PARAGON_SNAPSHOT_BUS=1-4)
//   PARAGON_SNAPSHOT_NOTES=1         a chord held on the keyboard (so the PLAY panel counts notes, and keys light)
TEST_CASE ("Render the editor to a PNG", "[.][snapshot]")
{
    const auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "editor.png");

    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);

    const auto rigFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotRigs");
    rigFolder.deleteRecursively();
    processor.useRigFolder (rigFolder);

    const auto page = env ("PARAGON_SNAPSHOT_PAGE");
    if (page == "presets")
    {
        processor.loadRig (*processor.getRigLibrary().findFactory ("Warm Pad"));
        processor.saveCurrentRigAs ("My Pad");
        processor.saveCurrentRigAs ("Night Drive Lead");
    }

    if (const auto name = env ("PARAGON_SNAPSHOT_PRESET"); name.isNotEmpty())
    {
        const auto* entry = processor.getRigLibrary().findFactory (name);
        REQUIRE (entry != nullptr);
        processor.loadRig (*entry);
    }

    for (const auto& pair : juce::StringArray::fromTokens (env ("PARAGON_SNAPSHOT_PARAMS"), ",", {}))
    {
        const auto id = pair.upToFirstOccurrenceOf ("=", false, false).trim();
        auto* parameter = processor.getParameters().getParameter (id);
        INFO (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }

    // A chord held, played for a moment so the notes are sounding.
    if (env ("PARAGON_SNAPSHOT_NOTES") == "1")
    {
        for (const auto note : { 48, 55, 60, 64 })
            processor.getKeyboardState().noteOn (1, note, 0.8f);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < 40; ++block)
            processor.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    if (page == "fx")
    {
        // THE RACK (stage 7a). PARAGON_SNAPSHOT_RACK=1: all four units at work on "Unison Strings" holding a chord
        // (ECHELON's low cut, a dip, a dynamic presence band and an air shelf; DOMINION; CONSTRICTOR; the LIMITER
        // on MODERN with TRUE PEAK, 6 dB in), the editor's timers run so the ladders and screens read it.
        // PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter opens that unit's page.
        view->showPage (MainView::Page::fx);
        if (env ("PARAGON_SNAPSHOT_RACK") == "1")
        {
            processor.loadRig (*processor.getRigLibrary().findFactory ("Unison Strings"));
            const auto set = [&] (const juce::String& id, float value)
            {
                auto* parameter = processor.getParameters().getParameter (id);
                REQUIRE (parameter != nullptr);
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            };
            struct Band { eq::Shape shape; float hz, db, q, dynamic; };
            const Band bands[] = { { eq::Shape::lowCut, 40.0f, 0.0f, 0.7f, 0.0f }, { eq::Shape::bell, 280.0f, -3.0f, 1.2f, 0.0f },
                                   { eq::Shape::bell, 3200.0f, 1.5f, 1.5f, -4.0f }, { eq::Shape::highShelf, 11000.0f, 2.0f, 0.7f, 0.0f } };
            for (int i = 0; i < 4; ++i)
            {
                set (ParamIDs::eqBand (i, "Used"), 1.0f);
                set (ParamIDs::eqBand (i, "Shape"), (float) (int) bands[i].shape);
                set (ParamIDs::eqBand (i, "Freq"), bands[i].hz);
                set (ParamIDs::eqBand (i, "Gain"), bands[i].db);
                set (ParamIDs::eqBand (i, "Q"), bands[i].q);
                set (ParamIDs::eqBand (i, "Dyn"), bands[i].dynamic);
            }
            set (ParamIDs::eqOn, 1.0f);
            set (ParamIDs::domOn, 1.0f);
            set (ParamIDs::domDepth, 7.0f);
            set (ParamIDs::compOn, 1.0f);
            set (ParamIDs::compThreshold, -26.0f);
            set (ParamIDs::compRatio, 3.0f);
            set (ParamIDs::limitOn, 1.0f);
            set (ParamIDs::limitTruePeak, 1.0f);
            set (ParamIDs::limitGain, 6.0f);
            // The SENDS (7b): DELAY in ping-pong eighths dotted, REVERB a hall; oscillator 2 (an octave up) sends less.
            set (ParamIDs::delayOn, 1.0f);
            set (ParamIDs::delayPingPong, 1.0f);
            set (ParamIDs::delaySync, 7.0f);
            set (ParamIDs::delayFeedback, 0.45f);
            set (ParamIDs::delayToReverb, 0.3f);
            set (ParamIDs::reverbOn, 1.0f);
            set (ParamIDs::reverbType, (float) (int) effects::Reverb::Type::hall);
            set (ParamIDs::reverbSize, 0.65f);
            set (ParamIDs::osc (1, "On"), 1.0f);
            set (ParamIDs::osc (1, "Transpose"), 12.0f);
            set (ParamIDs::osc (1, "SendDelay"), 0.4f);
            set (ParamIDs::osc (1, "SendReverb"), 0.6f);
            // MOD / LO-FI (7c): the Vortex in STEREO, AEON after it, a gently worn cassette.
            set (ParamIDs::modOn, 1.0f);
            set (ParamIDs::modStereo, 1.0f);
            set (ParamIDs::modMix, 0.6f);
            set (ParamIDs::mod2On, 1.0f);
            set (ParamIDs::mod2Depth, 0.6f);
            set (ParamIDs::mod2Res, 6.5f);
            set (ParamIDs::lofiOn, 1.0f);
            set (ParamIDs::lofiType, (float) (int) effects::LoFi::Type::cassette);
            set (ParamIDs::lofiAmount, 3.0f);
            set (ParamIDs::lofiNoise, 2.0f);
            // THE INSERTS (7d): oscillator 2 through bus A: SKYLINE driven, a SEQUENCE glitch.
            set (ParamIDs::osc (1, "Insert"), 1.0f);
            set (ParamIDs::ins (0, "DriveOn"), 1.0f);
            set (ParamIDs::ins (0, "DriveModel"), (float) (int) effects::Drive::Model::skyline);
            set (ParamIDs::ins (0, "DriveGain"), 7.0f);
            set (ParamIDs::ins (0, "GlitchOn"), 1.0f);
            set (ParamIDs::ins (0, "GlitchMix"), 0.6f);
            for (const auto note : { 48, 55, 60, 64, 67 })
                processor.getKeyboardState().noteOn (1, note, 0.85f);
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            for (int block = 0; block < 700; ++block)
            {
                processor.processBlock (buffer, midi);
                if (block > 600 && block % 10 == 0)   // the timers fire only when they're due
                {
                    juce::Thread::sleep (35);
                    juce::Timer::callPendingTimersSynchronously();
                }
            }
        }
        const auto fx = env ("PARAGON_SNAPSHOT_FX");
        auto& fxPage = view->getFxPage();
        if (const auto bus = env ("PARAGON_SNAPSHOT_BUS").getIntValue(); bus >= 1 && bus <= 4)
            fxPage.getInsertStrip().setBus (bus - 1);
        fxPage.show (fx == "eq" ? FxPage::View::eq : fx == "dominion" ? FxPage::View::dominion : fx == "comp" ? FxPage::View::comp
                     : fx == "limiter" ? FxPage::View::limiter : fx == "sends" ? FxPage::View::sends : fx == "delay" ? FxPage::View::delay
                     : fx == "reverb" ? FxPage::View::reverb : fx == "modfx" ? FxPage::View::modFx : fx == "mod" ? FxPage::View::mod
                     : fx == "mod2" ? FxPage::View::mod2 : fx == "lofi" ? FxPage::View::loFi : fx == "inserts" ? FxPage::View::inserts
                     : fx == "insdrive" ? FxPage::View::insertDrive : fx == "insglitch" ? FxPage::View::insertGlitch
                     : fx == "insmod" ? FxPage::View::insertMod : fx == "inslofi" ? FxPage::View::insertLoFi : FxPage::View::rack);
        // A display reads its bands and analyser on its own timer, but only while it's on screen, which a snapshot
        // never is (as PR7-E's snapshots, refresh them by hand). ECHELON with its dynamic band 3 selected.
        if (fx == "eq")
        {
            fxPage.getEqPage().getDisplay().refresh();
            fxPage.getEqPage().getDisplay().select (2);
            fxPage.getEqPage().refreshControls();
            if (env ("PARAGON_SNAPSHOT_MATCH") == "1")
            {
                fxPage.getEqPage().showMatch (true);
                fxPage.getEqPage().getMatchPanel().refresh();
            }
        }
        if (fx == "dominion")
            fxPage.getDominionPage().getDisplay().refresh();
        if (fx == "comp")
            fxPage.getCompPage().getDisplay().refresh();
        if (fx == "limiter")
            fxPage.getLimiterPage().refresh();
    }
    if (const auto osc = env ("PARAGON_SNAPSHOT_OSC").getIntValue(); osc > 0)
        view->getOscillatorPanel().selectOscillator (osc - 1);
    if (env ("PARAGON_SNAPSHOT_TRAVEL") == "1")
    {
        // Oscillator 1's TRAVEL with a drawn envelope, a pattern and strength curves.
        auto matrix = processor.getModMatrix();
        auto& t = matrix.travel[0];
        t.envelope = { { { 0.0f, 0.0f, 0.0f }, { 0.3f, 1.0f, -0.4f }, { 0.7f, 0.35f, 0.3f }, { 1.0f, 0.8f, 0.0f } }, -1, -1, -1 };
        using M = synth::travel::StepMode;
        const synth::travel::Step steps[] = { { M::normal, 1.0f, 1 }, { M::normal, 2.0f, 1 }, { M::trigger, 4.0f, 1 }, { M::reverse, 1.0f, 1 },
                                              { M::pingPongHalf, 2.0f, 2 }, { M::high, 1.0f, 1 }, { M::silence, 1.0f, 1 }, { M::low, 1.0f, 1 } };
        for (int k = 0; k < 8; ++k)
        {
            t.pattern.steps[(size_t) k] = steps[k];
            t.pattern.strength[(size_t) k] = { k % 2 == 0 ? 1.0f : 0.2f, 0.6f, k % 2 == 0 ? 0.3f : 1.0f, k == 4, k == 2 ? -0.6f : 0.0f };
        }
        processor.setModMatrix (matrix);
        view->getOscillatorPanel().selectOscillator (0);
    }
    if (env ("PARAGON_SNAPSHOT_OSCPAGE") == "travel")
        view->getOscillatorPanel().showPage (OscillatorPanel::Page::travel);
    if (env ("PARAGON_SNAPSHOT_OSCPAGE") == "voices")
        view->getOscillatorPanel().showPage (OscillatorPanel::Page::voices);
    if (env ("PARAGON_SNAPSHOT_OSCPAGE") == "shape")
        view->getOscillatorPanel().showPage (OscillatorPanel::Page::shape);
    if (env ("PARAGON_SNAPSHOT_OSCPAGE") == "fm")
        view->getOscillatorPanel().showPage (OscillatorPanel::Page::fm);
    if (env ("PARAGON_SNAPSHOT_OSCPAGE") == "fft")
        view->getOscillatorPanel().showPage (OscillatorPanel::Page::fft);
    if (env ("PARAGON_SNAPSHOT_PAGE") == "mod")
        view->showPage (MainView::Page::modulation);
    if (env ("PARAGON_SNAPSHOT_PAGE") == "arp")
    {
        // ARP 1 on, oscillators 1 and 2 routed to it, with a pattern that shows every kind of block.
        view->showPage (MainView::Page::arp);
        auto& page = view->getArpPage();
        if (env ("PARAGON_SNAPSHOT_ARP") == "1")
        {
            page.route (0, true);
            page.route (1, true);
            if (auto* on = processor.getParameters().getParameter (ParamIDs::arp (0, "On")))
                on->setValueNotifyingHost (1.0f);
            synth::arp::Pattern p;
            const int transposes[] = { 0, 12, 7, 0, 3, 12, 19, 0, -5, 0, 7, 12, 0, 5, 7, 24 };
            for (int k = 0; k < 16; ++k)
            {
                auto& b = p.blocks[(size_t) k];
                b.on = k != 6 && k != 13;
                b.transpose = transposes[k];
                b.velocity = k % 4 == 0 ? 1.0f : 0.6f + 0.05f * (float) (k % 3);
                b.length = k == 8 ? 2 : 1;
                b.slide = k == 3 || k == 11;
                b.ratchet = k == 15 ? 3 : (k == 5 ? 2 : 1);
            }
            page.getEditor().setShown (p, true);
        }
        if (env ("PARAGON_SNAPSHOT_ARP") == "2")
        {
            // CHORDS mode (5b): a chord progression with tones switched off and sub notes, and a randomizer set up.
            page.route (0, true);
            auto& parameters = processor.getParameters();
            parameters.getParameter (ParamIDs::arp (0, "On"))->setValueNotifyingHost (1.0f);
            auto* mode = parameters.getParameter (ParamIDs::arp (0, "Mode"));
            mode->setValueNotifyingHost (mode->convertTo0to1 ((float) (int) synth::arp::Mode::chords));
            auto* source = parameters.getParameter (ParamIDs::arp (0, "NoteSource"));
            source->setValueNotifyingHost (source->convertTo0to1 (2.0f));
            synth::arp::Pattern p;
            const char* chords[] = { "Min7", "Min7", "Maj7", "Maj7", "Sus4", "7", "Min9", "Power" };
            const int transposes[] = { 0, 0, 8, 8, 5, 7, 3, 0 };
            for (int k = 0; k < 8; ++k)
            {
                auto& b = p.blocks[(size_t) (k * 2)];
                b.on = true;
                b.length = k % 2 == 0 ? 2 : 1;
                b.transpose = transposes[k];
                b.chord = synth::arp::chordNames.indexOf (chords[k]);
                b.velocity = k % 2 == 0 ? 1.0f : 0.7f;
                b.sub = k % 4 == 0;
                b.subTranspose = k == 4 ? -24 : -12;
                b.chordMask = k == 3 ? 0x1f & ~0x4 : 0x1f;
            }
            page.getEditor().setShown (p, true);
            auto r = synth::arp::Randomizer {};
            r.scale = synth::arp::scaleNames.indexOf ("Dorian");
            r.autoDice = true;
            r.seed = 42;
            r.rollLengths = false;
            auto matrix = processor.getModMatrix();
            matrix.arpRandomizers[0] = r;
            processor.setModMatrix (matrix);
        }
        page.selectArp (0);
    }
    if (const auto lfo = env ("PARAGON_SNAPSHOT_LFO").getIntValue(); lfo > 0)
        view->getModPage().getModulatorPanel().selectLfo (lfo - 1);
    if (const auto envelope = env ("PARAGON_SNAPSHOT_ENV").getIntValue(); envelope > 0)
        view->getModPage().getModulatorPanel().selectEnvelope (envelope - 1);
    if (env ("PARAGON_SNAPSHOT_PITCH") == "1")
        view->getModPage().getModulatorPanel().selectPitch();
    if (env ("PARAGON_SNAPSHOT_MIDI") == "1")
    {
        // A few MIDI links, a knob learning, and a route made by dropping LFO 1 on the cutoff.
        MidiLinks links;
        links.link (74, ParamIDs::fltCutoff);
        links.link (71, ParamIDs::fltRes);
        links.link (1, ParamIDs::osc (0, "WtIndex"));
        links.link (7, ParamIDs::ampVolume);
        processor.setMidiLinks (links);
        processor.startMidiLearn (ParamIDs::fltDrive);
        processor.controllerForTest (74, 0.6f);
        view->addRoute ("lfo1", ParamIDs::fltCutoff);
        view->getSourceStrip().refresh();
        if (env ("PARAGON_SNAPSHOT_PAGE") == "settings")
        {
            view->showPage (MainView::Page::synth);
            view->showPage (MainView::Page::settings);
        }
    }
    if (env ("PARAGON_SNAPSHOT_STEP") == "2")   // the STEP tab as the sound has it
        view->getModPage().getModulatorPanel().selectStepSeq();
    if (env ("PARAGON_SNAPSHOT_STEP") == "1")
    {
        // A pattern with heights, a rest and linked steps (on both rows, for STEREO).
        auto matrix = processor.getModMatrix();
        for (auto& row : matrix.stepPattern.rows)
            for (int k = 0; k < synth::seq::maxSteps; ++k)
            {
                auto& step = row[(size_t) k];
                step.level = k % 4 == 0 ? 1.0f : k % 4 == 2 ? 0.7f : 0.45f;
                step.on = k % 8 != 7;
                step.tied = k % 8 == 4;
            }
        matrix.stepPattern.rows[1][0].on = false;
        processor.setModMatrix (matrix);
        view->getModPage().getModulatorPanel().selectStepSeq();
    }
    if (const auto shape = env ("PARAGON_SNAPSHOT_SHAPE").getIntValue(); shape > 0)
        view->getModPage().getModulatorPanel().selectShape (shape - 1);
    if (const auto page = env ("PARAGON_SNAPSHOT_FILTERPAGE").getIntValue(); page > 0)
        view->getFilterPanel().showPage (page);
    if (env ("PARAGON_SNAPSHOT_OSCPAGE") == "draw")
        view->getOscillatorPanel().showPage (OscillatorPanel::Page::draw);
    if (const auto curve = env ("PARAGON_SNAPSHOT_FFT"); curve.isNotEmpty())
    {
        // A curve for oscillator 1 ("band:dB band:dB ...", as presets keep it), and its view (FFT_VIEW=harmonic|bins).
        processor.setSpectrum (0, synth::SpectrumEdit::decode (curve.replaceCharacter (',', ' ')));
        processor.finishTableBuilds();
    }
    if (env ("PARAGON_SNAPSHOT_FFT_VIEW") == "harmonic")
        view->getOscillatorPanel().getSpectrumEditor().setView (SpectrumEditor::View::harmonic);
    if (env ("PARAGON_SNAPSHOT_FFT_VIEW") == "bins")
        view->getOscillatorPanel().getSpectrumEditor().setView (SpectrumEditor::View::bins);
    if (page == "drums")
    {
        // THE DRUMS (stage 6a): the kit on, slot PARAGON_SNAPSHOT_DRUM (1-12) chosen; PARAGON_SNAPSHOT_DRUMS=1 plays a
        // beat into it first (kick, snare and closed hat just hit: their rows lit).
        if (auto* on = processor.getParameters().getParameter (ParamIDs::drumsOn))
            on->setValueNotifyingHost (1.0f);
        view->showPage (MainView::Page::drums);
        auto& drums = view->getDrumPage();
        if (env ("PARAGON_SNAPSHOT_DRUMS") == "2")
        {
            // A file of nine kicks (each a semitone higher) in the kick's slot, its 4th kick chosen.
            auto file = std::make_shared<synth::drums::Sample>();
            file->name = "Kit_1_Kick";
            file->sampleRate = 44100.0;
            file->channels.assign (1, std::vector<float> ((size_t) (4.0 * 44100.0), 0.0f));
            for (int k = 0; k < 9; ++k)
            {
                synth::drums::SlotSettings kick;
                kick.pitch = (float) k;
                kick.decay = 0.3f;
                const auto hit = synth::drums::Kit::renderHit (kick, 1.0f, 44100.0, 0.4);
                const auto at = (int) (0.05 * 44100.0) + (int) (k * 0.45 * 44100.0);
                for (int i = 0; i < hit.getNumSamples() && at + i < file->length(); ++i)
                    file->channels[0][(size_t) (at + i)] = hit.getSample (0, i);
            }
            processor.setDrumSample (0, file);
            if (auto* m = processor.getParameters().getParameter (ParamIDs::drum (0, "Model")))
                m->setValueNotifyingHost (m->convertTo0to1 ((float) (int) synth::drums::Model::sample));
            drums.select (0);
            drums.setSize (1476, 672);
            drums.getDisplay().pickHit (3);
            if (env ("PARAGON_SNAPSHOT_DRUMEDIT") == "2")   // and a fade out drawn over it
            {
                auto matrix = processor.getModMatrix();
                matrix.drumEnvelopes[0] = { { { 0.0f, 1.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
                processor.setModMatrix (matrix);
                drums.getEnvelopeEditor().setTarget (MultiPointEditor::Kind::drumEnvelope, 0);
            }
            drums.refreshPictures();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMS") != "2")
            drums.select (juce::jmax (1, env ("PARAGON_SNAPSHOT_DRUM").getIntValue()) - 1);
        if (env ("PARAGON_SNAPSHOT_DRUMS") == "1")
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (const auto note : { 36, 38, 42 })
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int b = 0; b < 6; ++b)
                processor.processBlock (buffer, midi);
            drums.updateLights();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMEDIT") == "1")
        {
            // THE SAMPLE EDITOR (6c): the clap with a drawn envelope (its flams held, its tail cut), an anchor on its
            // main slap, GAIN and LP.
            const auto set = [&processor] (const juce::String& id, float value)
            {
                if (auto* p = processor.getParameters().getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
            };
            set (ParamIDs::drum (3, "Delay"), -22.0f);
            set (ParamIDs::drum (3, "Gain"), 3.0f);
            set (ParamIDs::drum (3, "LowPass"), 7000.0f);
            set (ParamIDs::drum (3, "Decay"), 0.7f);
            auto matrix = processor.getModMatrix();
            matrix.drumEnvelopes[3] = { { { 0.0f, 1.0f, 0.0f }, { 0.35f, 0.9f, -0.4f }, { 0.6f, 0.25f, 0.0f }, { 0.75f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
            processor.setModMatrix (matrix);
            processor.updateDrumReverses();   // the clap's hit length, which the envelope spans
            drums.select (3);
            drums.refreshPictures();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMCHOSEN") == "1")
        {
            // 6c part 2: the hats and cymbal chosen together, a kit and sequence "loaded", the tom's hit playing.
            drums.clickRow (6, {});
            drums.clickRow (8, juce::ModifierKeys::ctrlModifier);
            drums.clickRow (10, juce::ModifierKeys::ctrlModifier);
            drums.clickRow (11, juce::ModifierKeys::ctrlModifier);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int b = 0; b < 12; ++b)
                processor.processBlock (buffer, midi);
            drums.updateLights();
        }
        if (env ("PARAGON_SNAPSHOT_DRUMSEQ") == "1")
        {
            // THE DRUM SEQUENCER (stage 6b): a beat in pattern A (accented hats with a roll, a pitched tom fill, a
            // reversed cymbal swelling into the bar, a short open hat), played with the song to just past beat 2, the
            // snares chosen.
            synth::drumseq::Pattern beat;
            const auto note = [&beat] (int row, double start, float velocity = 1.0f) -> synth::drumseq::Note&
            {
                synth::drumseq::Note n;
                n.row = row;
                n.start = start;
                n.velocity = velocity;
                beat.notes.push_back (n);
                return beat.notes.back();
            };
            for (const auto k : { 0.0, 3.0, 8.0, 10.0 })
                note (0, k);
            note (2, 4.0);
            note (2, 12.0);
            for (int k = 0; k < 14; k += 2)
                note (6, k, k % 4 == 0 ? 1.0f : 0.55f);
            note (6, 14.0, 0.8f).roll = 2;
            note (10, 7.0, 0.7f).gate = 0.25f;
            note (9, 13.0).pitch = 2.0f;
            note (7, 14.0);
            note (5, 15.0).pitch = -3.0f;
            note (11, 15.0).reverse = synth::drumseq::Reverse::endSync;
            note (1, 6.0, 0.6f).pan = -0.5f;
            auto matrix = processor.getModMatrix();
            matrix.drumPatterns[0] = beat;
            processor.setModMatrix (matrix);
            if (auto* on = processor.getParameters().getParameter (ParamIDs::dseqOn))
                on->setValueNotifyingHost (1.0f);
            if (auto* sh = processor.getParameters().getParameter (ParamIDs::shuffle))
                sh->setValueNotifyingHost (0.2f);
            processor.updateDrumReverses();

            struct Song final : juce::AudioPlayHead
            {
                double ppq = 0.0;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setBpm (120.0);
                    info.setPpqPosition (ppq);
                    info.setIsPlaying (true);
                    return info;
                }
            };
            static Song song;
            processor.setPlayHead (&song);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 58; ++b)   // 1.14 beats: the playhead on step 5
            {
                processor.processBlock (buffer, midi);
                song.ppq += 512.0 / 48000.0 * 2.0;
            }
            processor.setPlayHead (nullptr);
            drums.showView (DrumPage::View::sequencer);
            drums.select (2);
            drums.getSequencer().getGrid().setSelection ({ 4, 5 });
            drums.updateLights();
        }
    }
    if (page == "presets")
        view->showPage (MainView::Page::presets);
    else if (page == "settings")
        view->showPage (MainView::Page::settings);

    const auto width = juce::jmax (960, juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_WIDTH", "1600").getIntValue());
    editor->setSize (width, width * 9 / 16);
    editor->resized();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}

// PARAGON_SNAPSHOT=menu.png ./ParagonX1Tests "[snapshot-menu]": a drop-down
// menu as the plugin draws it (menus open in their own window, so the editor
// snapshot can't show one): the FILTER's types, LP 24 ticked, one highlighted.
TEST_CASE ("Render a drop-down menu to a PNG", "[.][snapshot-menu]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "menu.png");
    ParagonLookAndFeel look;
    const auto& items = synth::filterTypeNames;
    constexpr int scale = 2, width = 150, itemHeight = 26;
    juce::Image image (juce::Image::ARGB, width * scale, (itemHeight * items.size() + 8) * scale, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale ((float) scale));
    look.drawPopupMenuBackground (g, width, itemHeight * items.size() + 8);
    for (int i = 0; i < items.size(); ++i)
        look.drawPopupMenuItem (g, { 0, 4 + i * itemHeight, width, itemHeight }, false, true, i == 5, items[i] == "LP 24", false,
                                items[i], {}, nullptr, nullptr);

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
