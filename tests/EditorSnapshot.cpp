#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Finish.h"

namespace
{
    juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        if (parent.getComponentID() == id)
            return &parent;
        for (auto* child : parent.getChildren())
            if (auto* found = findById (*child, id))
                return found;
        return nullptr;
    }
}

// Hidden test (the "[.]" tag): renders the editor to a PNG for reviewing the
// UI without a DAW. Run with:
//   PARAGON_SNAPSHOT=editor.png ./ParagonX1Tests "[snapshot]"
// Optionally set PARAGON_SNAPSHOT_EFFECTS=1 to switch on the tuner and the
// EQ (the stomps that work so far), and the delay and reverb. PARAGON_SNAPSHOT_PAGE=presets shows
// the rig browser, with a couple of example user rigs, and =settings the SETTINGS page. PARAGON_SNAPSHOT_WIDTH
// sets the window width (default 1280).
template <typename Type>
Type* findType (juce::Component& parent)
{
    if (auto* match = dynamic_cast<Type*> (&parent))
        return match;
    for (auto* child : parent.getChildren())
        if (auto* found = findType<Type> (*child))
            return found;
    return nullptr;
}

TEST_CASE ("Render the editor to a PNG", "[.][snapshot]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "editor.png");

    ParagonX1AudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    const auto showPresets = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "presets";

    // PARAGON_SNAPSHOT_FINISH=black or =neon: the pearl black unit, or pearl
    // white with neon keys (not saved as the preference).
    const auto finish = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_FINISH", {});
    Finish::set (finish == "black" ? Finish::Type::pearlBlack : finish == "neon" ? Finish::Type::pearlWhiteNeon : Finish::Type::pearlWhite, false);
    const auto rigFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotRigs");
    rigFolder.deleteRecursively();
    processor.useRigFolder (rigFolder);

    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EFFECTS", {}) == "1")
        for (const auto* id : { "tunerOn", "eqOn", "delayOn", "reverbOn" })
            processor.getParameters().getParameter (id)->setValueNotifyingHost (1.0f);

    // PARAGON_SNAPSHOT_RIG=1: a saved rig loaded on the main page, as in the mockup.
    if (showPresets || juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_RIG", {}) == "1")
    {
        processor.saveCurrentRigAs ("Mix Bus");
        processor.saveCurrentRigAs ("Vocal Check");
        processor.loadRig (*processor.getRigLibrary().find (rigFolder.getChildFile (juce::String ("Mix Bus") + Rig::fileExtension)));
        processor.getParameters().getParameter ("eqLevel")->setValueNotifyingHost (0.6f);   // shows the * marker
    }

    // PARAGON_SNAPSHOT_FACTORY=name: that factory rig loaded (and selected on the PRESETS page).
    if (const auto factory = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_FACTORY", {}); factory.isNotEmpty())
        if (const auto* entry = processor.getRigLibrary().findFactory (factory))
            processor.loadRig (*entry);

    // PARAGON_SNAPSHOT_TUNER=1: the tuner on, after hearing a short sung
    // phrase (some notes sharp, some flat, some with vibrato) that ends on a
    // held A3 4 cents sharp, and the expression pedal assigned to the wah.
    // With PARAGON_SNAPSHOT_BLOCK=9 the TUNER panel shows its trace.
    const auto showTuner = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_TUNER", {}) == "1";
    if (showTuner)
    {
        auto set = [&] (const char* id, float value)
        {
            auto* p = processor.getParameters().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        set ("tunerOn", 1.0f);
        set ("expressionTarget", 2.0f);
        set ("expression", 0.6f);
        set ("expressionMin", 0.2f);

        struct SungNote { double hz, cents, seconds, vibratoCents; };   // hz 0: a breath
        const SungNote phrase[] = { { 220.0, 6, 1.2, 0 }, { 0, 0, 0.25, 0 }, { 261.63, 14, 1.5, 20 }, { 0, 0, 0.2, 0 },
                                    { 293.66, -4, 1.0, 0 }, { 329.63, 22, 0.8, 0 }, { 293.66, -8, 1.6, 25 }, { 0, 0, 0.4, 0 },
                                    { 261.63, 10, 1.0, 0 }, { 220.0, 3, 2.0, 15 }, { 0, 0, 0.3, 0 }, { 196.0, -12, 1.2, 0 },
                                    { 220.0, 4, 2.5, 10 } };
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        double phase = 0.0;
        int block = 0;
        for (const auto& sung : phrase)
        {
            for (int b = 0; b < (int) (sung.seconds * 48000.0 / 256.0); ++b, ++block)
            {
                buffer.clear();
                for (int i = 0; i < 256; ++i)
                {
                    const auto t = (double) (block * 256 + i) / 48000.0;
                    const auto cents = sung.cents + sung.vibratoCents * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t);
                    phase += juce::MathConstants<double>::twoPi * sung.hz * std::pow (2.0, cents / 1200.0) / 48000.0;
                    if (sung.hz > 0.0)
                        buffer.setSample (0, i, (float) (0.25 * std::sin (phase) + 0.1 * std::sin (2.0 * phase)));
                }
                processor.processBlock (buffer, midi);
                if (block % 6 == 5)
                    processor.updateTuner();   // about 30 times a second, as its timer does
            }
        }
    }

    // PARAGON_SNAPSHOT_EQ=g1,g2,...,g9 switches the graphic EQ on with these band gains (dB).
    if (const auto eq = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EQ", {}); eq.isNotEmpty())
    {
        const auto gains = juce::StringArray::fromTokens (eq, ",", {});
        processor.getParameters().getParameter ("eqOn")->setValueNotifyingHost (1.0f);
        for (int band = 0; band < juce::jmin (gains.size(), ParamIDs::eqBands.size()); ++band)
        {
            auto* p = processor.getParameters().getParameter (ParamIDs::eqBands[band]);
            p->setValueNotifyingHost (p->convertTo0to1 (gains[band].getFloatValue()));
        }
    }

    // PARAGON_SNAPSHOT_EXPRESSION=0..1 sets the expression pedal's position.
    if (const auto pedal = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EXPRESSION", {}); pedal.isNotEmpty())
        processor.getParameters().getParameter ("expression")->setValueNotifyingHost (pedal.getFloatValue());

    // PARAGON_SNAPSHOT_PARAMS=id=value,id=value sets any parameters (plain values, e.g. modType=8).
    for (const auto& setting : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PARAMS", {}), ",", {}))
        if (auto* p = processor.getParameters().getParameter (setting.upToFirstOccurrenceOf ("=", false, false)))
            p->setValueNotifyingHost (p->convertTo0to1 (setting.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (showTuner)
        for (int i = 0; i < 12; ++i)   // let the editor's timers run: the tuner reads a few times
        {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep (40);
        }
    if (showPresets)
        if (auto* tab = dynamic_cast<juce::Button*> (findById (*editor, "presetsTab")))
            tab->onClick();
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "settings")
        if (auto* tab = dynamic_cast<juce::Button*> (findById (*editor, "settingsTab")))
            tab->onClick();
    // PARAGON_SNAPSHOT_BLOCK=n opens block n's knobs (0 = pitch, 1 = comp ... 8 = lo-fi, 9 = the TUNER panel) in place of the graphic EQ.
    if (const auto block = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_BLOCK", {}); block.isNotEmpty())
        if (auto* chain = findType<SignalChain> (*editor))
            chain->onSelect (block.getIntValue());
    if (const auto width = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_WIDTH", {}).getIntValue(); width > 0)
        editor->setSize (width, width * 9 / 16);
    editor->resized();
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
