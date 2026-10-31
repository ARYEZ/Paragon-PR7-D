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
//   PARAGON_SNAPSHOT_PARAMS=id=value,...   parameters set (real units)
//   PARAGON_SNAPSHOT_PAGE=drums      THE DRUMS, the page it opens on (also with no PAGE) (PARAGON_SNAPSHOT_DRUM=n: slot n chosen;
//                                    PARAGON_SNAPSHOT_DRUMS=1: a beat just played, its rows lit; =2 a file of nine kicks, the 4th chosen;
//                                    PARAGON_SNAPSHOT_DRUMSEQ=1: the SEQUENCER view, a beat playing with the song;
//                                    PARAGON_SNAPSHOT_DRUMEDIT=1: the clap with an envelope, an anchor, GAIN and LP, =2 with DRUMS=2 a fade;
//                                    PARAGON_SNAPSHOT_DRUMCHOSEN=1: the hats and cymbal chosen together, the tom's playhead;
//                                    any of these shows the DRUM view; with none, the KIT VIEW (0.14.0), the page's first:
//                                    PARAGON_SNAPSHOT_KIT=1: the kick, snare, crash and china just hit, glowing, the ride's bell chosen)
//   PARAGON_SNAPSHOT_PAGE=fx         THE RACK (PARAGON_SNAPSHOT_RACK=1: at work on a beat, the sends and bus A too; PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi: that view, the inserts' on bus PARAGON_SNAPSHOT_BUS=1-4)
//   PARAGON_SNAPSHOT_NOTES=1         the kick, snare and closed hat's keys held on the keyboard (they light)
//   PARAGON_SNAPSHOT_MIDI=1          demo MIDI links and a knob learning (with PAGE=settings: the links list)
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
        processor.loadRig (*processor.getRigLibrary().findFactory ("Room Beat"));
        processor.saveCurrentRigAs ("My Room Beat");
        processor.saveCurrentRigAs ("Live Funk Kit");
    }

    // THE GROOVES page: a demo pack of our own (made here, as a pack's files are written), linked and read.
    const auto groovePack = rigFolder.getChildFile ("My Grooves");
    if (page == "grooves")
    {
        const auto write = [&] (const juce::String& path, double bpm, int hat, int bars, int busy, int softness, bool swing = false)
        {
            juce::MidiMessageSequence track;
            track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)));
            track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
            const auto add = [&] (double beat, int note, int velocity)
            {
                track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, velocity - softness)).withTimeStamp (beat * 480));
                track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (beat * 480 + 60));
            };
            for (int bar = 0; bar < bars; ++bar)
            {
                const auto b = bar * 4.0;
                add (b, 36, 120);
                add (b + 2.5, 36, 100);
                add (b + 1.0, 38, 115);
                add (b + 3.0, 38, 118);
                for (int e = 0; e < 8 * busy; ++e)
                    add (b + (swing ? e / 2 + (e % 2 == 1 ? 2.0 / 3.0 : 0.0) : e * 0.5 / busy), hat, e % 2 == 0 ? 100 : 70);
            }
            juce::MidiFile file;
            file.setTicksPerQuarterNote (480);
            track.updateMatchedPairs();
            file.addTrack (track);
            const auto f = groovePack.getChildFile (path);
            f.getParentDirectory().createDirectory();
            juce::FileOutputStream out (f);
            file.writeTo (out);
        };
        write ("Rock 96 BPM/Verse 01.mid", 96.0, 42, 4, 1, 0);
        write ("Rock 96 BPM/Verse 02.mid", 96.0, 42, 4, 1, 20);
        write ("Rock 96 BPM/Chorus 01.mid", 96.0, 46, 4, 1, 0);
        write ("Rock 96 BPM/Bridge 01.mid", 96.0, 51, 8, 1, 10);
        write ("Rock 96 BPM/Fill 01.mid", 96.0, 49, 1, 2, 0);
        write ("Funk 104 BPM/Verse 01.mid", 104.0, 42, 2, 2, 10);
        write ("Funk 104 BPM/Chorus 01.mid", 104.0, 22, 2, 2, 0);
        write ("Ballads/Slow 68 BPM/Verse 01.mid", 68.0, 51, 4, 1, 40);
        write ("Shuffle 124 BPM/Verse 01.mid", 124.0, 42, 2, 1, 0, true);
        write ("Shuffle 124 BPM/Chorus 01.mid", 124.0, 51, 2, 1, 0, true);
        // PARAGON_SNAPSHOT_GROOVEDB=1 (0.15.0): a groove database in the pack too (tests/data's, its grooves invented).
        if (env ("PARAGON_SNAPSHOT_GROOVEDB") == "1")
        {
            groovePack.getChildFile ("Demo Database").createDirectory();
            juce::File (PARAGON_TEST_DATA).getChildFile ("midiDB").copyFileTo (groovePack.getChildFile ("Demo Database/midiDB"));
        }
        REQUIRE (processor.getGrooveLibrary().addFolder (groovePack));
        processor.getGrooveLibrary().scanNow();
        processor.getGrooveLibrary().setFavourite (groovePack.getChildFile ("Rock 96 BPM/Chorus 01.mid"), true);
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

    // Keys held, played for a moment so the hits are sounding.
    if (env ("PARAGON_SNAPSHOT_NOTES") == "1")
    {
        for (const auto note : { 36, 38, 42 })
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
        // THE RACK. PARAGON_SNAPSHOT_RACK=1: all four units at work on "Room Beat" playing with the song (ECHELON's low
        // cut, a dip, a dynamic presence band and an air shelf; DOMINION; CONSTRICTOR; the LIMITER on MODERN with TRUE
        // PEAK, 6 dB in), the editor's timers run so the ladders and screens read it.
        // PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter opens that unit's page.
        view->showPage (MainView::Page::fx);
        if (env ("PARAGON_SNAPSHOT_RACK") == "1")
        {
            processor.loadRig (*processor.getRigLibrary().findFactory ("Room Beat"));
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
            // The SENDS: DELAY in ping-pong eighths dotted, REVERB a hall; the snare and clap send to the delay.
            set (ParamIDs::delayOn, 1.0f);
            set (ParamIDs::delayPingPong, 1.0f);
            set (ParamIDs::delaySync, 7.0f);
            set (ParamIDs::delayFeedback, 0.45f);
            set (ParamIDs::delayToReverb, 0.3f);
            set (ParamIDs::reverbOn, 1.0f);
            set (ParamIDs::reverbType, (float) (int) effects::Reverb::Type::hall);
            set (ParamIDs::reverbSize, 0.65f);
            for (int k = 0; k < 12; ++k)
                set (ParamIDs::drum (k, "SendDelay"), k == 2 || k == 3 ? 0.4f : 0.0f);
            // MOD / LO-FI: the Vortex in STEREO, AEON after it, a gently worn cassette.
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
            // THE INSERTS: the kick and snare through bus A: SKYLINE driven, a SEQUENCE glitch.
            set (ParamIDs::drum (0, "Insert"), 1.0f);
            set (ParamIDs::drum (2, "Insert"), 1.0f);
            set (ParamIDs::ins (0, "DriveOn"), 1.0f);
            set (ParamIDs::ins (0, "DriveModel"), (float) (int) effects::Drive::Model::skyline);
            set (ParamIDs::ins (0, "DriveGain"), 7.0f);
            set (ParamIDs::ins (0, "GlitchOn"), 1.0f);
            set (ParamIDs::ins (0, "GlitchMix"), 0.6f);
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
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            for (int block = 0; block < 700; ++block)
            {
                processor.processBlock (buffer, midi);
                song.ppq += 256.0 / 48000.0 * 2.0;
                if (block > 600 && block % 10 == 0)   // the timers fire only when they're due
                {
                    juce::Thread::sleep (35);
                    juce::Timer::callPendingTimersSynchronously();
                }
            }
            processor.setPlayHead (nullptr);
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
    if (env ("PARAGON_SNAPSHOT_MIDI") == "1")
    {
        // A few MIDI links on the kick's knobs and the kit's, and the snare's DECAY learning.
        MidiLinks links;
        links.link (74, ParamIDs::drum (0, "Tone"));
        links.link (71, ParamIDs::drum (0, "Decay"));
        links.link (1, ParamIDs::drumsLevel);
        links.link (7, ParamIDs::reverbMix);
        processor.setMidiLinks (links);
        processor.startMidiLearn (ParamIDs::drum (0, "Snap"));
        processor.controllerForTest (74, 0.6f);
        if (env ("PARAGON_SNAPSHOT_PAGE") == "settings")
        {
            view->showPage (MainView::Page::drums);
            view->showPage (MainView::Page::settings);
        }
    }
    if (page == "drums" || page.isEmpty())
    {
        // THE DRUMS: slot PARAGON_SNAPSHOT_DRUM (1-12) chosen; PARAGON_SNAPSHOT_DRUMS=1 plays a beat into it first (kick,
        // snare and closed hat just hit: their rows lit).
        view->showPage (MainView::Page::drums);
        auto& drums = view->getDrumPage();
        for (const auto* what : { "PARAGON_SNAPSHOT_DRUM", "PARAGON_SNAPSHOT_DRUMS", "PARAGON_SNAPSHOT_DRUMEDIT", "PARAGON_SNAPSHOT_DRUMCHOSEN" })
            if (env (what).isNotEmpty())
                drums.showView (DrumPage::View::drum);
        if (env ("PARAGON_SNAPSHOT_KIT") == "1")
        {
            drums.showView (DrumPage::View::kit);
            drums.select (13);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (const auto note : { 36, 38, 47, 51 })   // BD, SD, CY, CHN (ROOT 36)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int b = 0; b < 8; ++b)
                processor.processBlock (buffer, midi);
            drums.updateLights();
        }
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
        if (env ("PARAGON_SNAPSHOT_DRUMS") != "2" && env ("PARAGON_SNAPSHOT_KIT").isEmpty())
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
    if (page == "grooves")
    {
        // Every groove listed; TYPE: verses and choruses; TEMPO: the slowest excluded; the second playing a moment in.
        view->showPage (MainView::Page::grooves);
        auto& grooves = view->getGroovesPage();
        grooves.refresh();
        if (env ("PARAGON_SNAPSHOT_GROOVEDB") == "1")
        {
            // The database's rock song chosen in the tree, its second groove playing.
            grooves.chooseFolder (groovePack.getChildFile ("Demo Database/midiDB/Demo Grooves/Straight 4-4/Rock Song 100 BPM"));
            grooves.playRow (1, true);
            grooves.getTable().selectRow (1);
        }
        else
        {
            grooves.toggleFilter (grooves::Column::section, "Verse", false);
            grooves.toggleFilter (grooves::Column::section, "Chorus", false);
            grooves.toggleFilter (grooves::Column::tempo, "Below 80", true);
            grooves.playRow (1, true);
            grooves.getTable().selectRow (1);
        }
        // TAP TO FIND: =1 the panel open, a push beat tapped (kick, snare, eighth hats) and the loop running; =2 found.
        if (const auto tap = env ("PARAGON_SNAPSHOT_TAP"); tap.isNotEmpty())
        {
            grooves.clearFilters();
            processor.stopGroove();
            grooves.openTapPanel();
            auto& panel = grooves.getTapPanel();
            for (const auto b : { 0.0, 1.5, 2.5, 4.0, 5.5, 6.5 }) panel.addTap ((int) grooves::Lane::kick, b + (b > 3 ? 0.04 : -0.03));
            for (const auto b : { 1.0, 3.0, 5.0, 7.0 }) panel.addTap ((int) grooves::Lane::snare, b + 0.05);
            for (int e = 0; e < 16; ++e) panel.addTap ((int) grooves::Lane::hats, e * 0.5 + (e % 3 == 0 ? 0.03 : -0.02));
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            for (int b = 0; b < 230; ++b)
                processor.processBlock (buffer, midi);
            if (tap == "2")
                panel.getFindButton().onClick();
        }
        // GROOVE PARTS: the playing groove's HI-HAT ticked.
        if (env ("PARAGON_SNAPSHOT_PARTS") == "1")
            grooves.toggleDrumPart ((int) grooves::DrumPart::hats);
        if (env ("PARAGON_SNAPSHOT_GROOVESEQ").isNotEmpty())
            grooves.toPattern (1);   // TO SEQUENCER B: its note in the status line
        if (env ("PARAGON_SNAPSHOT_GROOVESEQ") == "2")
        {
            // ...and the pattern it made, in the SEQUENCER.
            view->showPage (MainView::Page::drums);
            view->getDrumPage().showView (DrumPage::View::sequencer);
            view->getDrumPage().getSequencer().selectPattern (1);   // B already: its button lit now, not at the next tick
        }
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < 300; ++b)
            processor.processBlock (buffer, midi);
        grooves.refresh();
        // THE SONG TRACK: a song of the demo pack's Rock grooves (the first verse two times round, a fill, the chorus,
        // the second verse muted, the bridge), shown in the keyboard's place, playing with the DAW in the chorus.
        if (env ("PARAGON_SNAPSHOT_SONG").isNotEmpty())
        {
            processor.stopGroove();
            for (const auto* name : { "Verse 01", "Fill 01", "Chorus 01", "Verse 02", "Fill 01", "Bridge 01" })
                if (const auto g = grooves::read (groovePack.getChildFile ("Rock 96 BPM").getChildFile (juce::String (name) + ".mid"), processor.grooveLayout (0)))
                    processor.addToSong (*g, 0);
            auto edited = processor.getSong();
            edited.resize (0, 32.0);
            edited.blocks[3].muted = true;
            processor.setSong (edited);
            view->showSong (true);
            view->getSongTrack().choose (2);
            struct Host final : juce::AudioPlayHead
            {
                double ppq = 42.5;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setBpm (96.0);
                    info.setPpqPosition (ppq);
                    info.setIsPlaying (true);
                    return info;
                }
            };
            static Host host;
            processor.setPlayHead (&host);
            for (int b = 0; b < 20; ++b)
            {
                processor.processBlock (buffer, midi);
                host.ppq += 256.0 / 48000.0 * 96.0 / 60.0;
            }
            processor.setPlayHead (nullptr);
            // =3: THE SONG CREATOR on Rock's first verse, a song made with the bridge structure, its chorus heard.
            if (env ("PARAGON_SNAPSHOT_SONG") == "3")
            {
                view->openSongCreator (groovePack.getChildFile ("Rock 96 BPM/Verse 01.mid"));
                auto& creator = view->getSongCreator();
                creator.chooseStructure (1);
                creator.makeSong (false);
                creator.audition ((int) grooves::Part::chorus, 0);
                view->getSongTrack().choose (-1);
                processor.songBroadcaster.dispatchPendingMessages();   // the strip's line as the plugin shows it
            }
            // =4 (0.11.0): tempo changes (96, gliding from bar 9 to 140 at bar 13) and 3/4 from bar 19, the TEMPO view.
            if (env ("PARAGON_SNAPSHOT_SONG") == "4")
            {
                auto timed = processor.getSong();
                timed.addTempo (0.0, 96.0);
                timed.tempos[(size_t) timed.addTempo (32.0, 96.0)].ramp = true;
                timed.addTempo (48.0, 140.0);
                timed.setMeter (72.0, 3, 4);
                processor.setSong (timed);
                view->getSongTrack().setView (SongTrack::View::tempo);
                processor.songBroadcaster.dispatchPendingMessages();
            }
            // =5 (0.12.0): REC armed, a take's "Recorded" block after the song.
            if (env ("PARAGON_SNAPSHOT_SONG") == "5")
            {
                auto recorded = processor.getSong();
                song::Block take;
                take.name = "Recorded";
                take.length = take.loopLength = 8.0;
                for (int e = 0; e < 16; ++e)
                    take.hits.push_back ({ e * 0.5, e % 4 == 0 ? 0 : e % 4 == 2 ? 2 : 6, e % 2 == 0 ? 0.9f : 0.5f });
                recorded.place (take, recorded.endBeat());
                processor.setSong (recorded);
                processor.setSongRecording (true);
                view->getSongTrack().getRecordButton().setToggleState (true, juce::dontSendNotification);
                processor.songBroadcaster.dispatchPendingMessages();
            }

            // =2: EDIT BLOCK on the chorus, its snare chosen.
            if (env ("PARAGON_SNAPSHOT_SONG") == "2")
            {
                view->getBlockEditor().open (2);
                view->getBlockEditor().chooseDrum (2, false);
            }
        }
    }
    // A GROOVE HELD OVER THE SEQUENCER's GRID (0.13.0): the DRUMS page's SEQUENCER, the frame and what dropping would do.
    if (page == "grooves" && env ("PARAGON_SNAPSHOT_SEQDROP") == "1")
    {
        view->getGroovesPage().toggleDrumPart (-1);
        view->showPage (MainView::Page::drums);
        view->getDrumPage().showView (DrumPage::View::sequencer);
        view->getDrumPage().getSequencer().getGrid().itemDragEnter ({ "groove:1", nullptr, { 40, 40 } });
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
// snapshot can't show one): the drum models, SNARE ticked, one highlighted.
TEST_CASE ("Render a drop-down menu to a PNG", "[.][snapshot-menu]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "menu.png");
    ParagonLookAndFeel look;
    const auto& items = synth::drums::modelNames;
    constexpr int scale = 2, width = 150, itemHeight = 26;
    juce::Image image (juce::Image::ARGB, width * scale, (itemHeight * items.size() + 8) * scale, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale ((float) scale));
    look.drawPopupMenuBackground (g, width, itemHeight * items.size() + 8);
    for (int i = 0; i < items.size(); ++i)
        look.drawPopupMenuItem (g, { 0, 4 + i * itemHeight, width, itemHeight }, false, true, i == 5, items[i] == "Snare", false,
                                items[i], {}, nullptr, nullptr);

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
