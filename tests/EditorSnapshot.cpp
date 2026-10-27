#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
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
