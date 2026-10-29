#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// Presets ("rigs" in the code, as across the PR7 family): files, the
// library, loading and saving, and the factory presets.
#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/FactoryRigs.h"

using Catch::Matchers::WithinAbs;

namespace
{
    // A fresh, empty rigs folder for each test.
    struct TempRigFolder
    {
        TempRigFolder() { folder.deleteRecursively(); }
        ~TempRigFolder() { folder.deleteRecursively(); }
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1RigTests")
                                .getChildFile (juce::Uuid().toString());
    };

    // Stand-ins for the tests below: a number in the preset (with a default),
    // another (that can go below zero), an on/off that's off by default.
    const juce::String someTime = ParamIDs::ampAttack;            // ms
    const juce::String someFine = ParamIDs::osc (0, "Fine");      // cents, -100 to 100
    const juce::String someSwitch = ParamIDs::osc (0, "Invert");  // off by default

    struct RigRig   // a processor with its presets in a temporary folder
    {
        RigRig()
        {
            processor.setPlayConfigDetails (1, 2, 48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            processor.useRigFolder (temp.folder);
        }

        void set (const juce::String& id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        float get (const juce::String& id) { return processor.getParameters().getRawParameterValue (id)->load(); }

        const RigLibrary::Entry& entryNamed (const juce::String& name)
        {
            for (const auto& entry : processor.getRigLibrary().getEntries())
                if (entry.rig.name == name)
                    return entry;
            FAIL ("No preset called " << name);
            return processor.getRigLibrary().getEntries().front();
        }

        TempRigFolder temp;
        ParagonX1AudioProcessor processor;
    };
}

TEST_CASE ("A rig survives being written to a file and read back", "[rigs]")
{
    Rig rig;
    rig.name = "AC/DC: \"Back in Black\" & <more>";
    rig.bank = 2;
    rig.number = 17;
    rig.favourite = true;
    rig.tags = { "Rock", "High Gain" };
    rig.parameters = { { "flt1Cutoff", 650.0f }, { "osc1Fine", -4.0f } };

    const auto restored = Rig::fromXml (*juce::XmlDocument::parse (rig.toXml()->toString()));
    REQUIRE (restored.has_value());
    CHECK (restored->name == rig.name);
    CHECK (restored->getSlotName() == "C17");
    CHECK (restored->favourite);
    CHECK (restored->tags == rig.tags);
    CHECK (restored->parameters == rig.parameters);
}

TEST_CASE ("Files that aren't rigs, or are from a newer version, are refused", "[rigs]")
{
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<SomethingElse/>")).has_value());
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<ParagonRig version=\"99\" name=\"x\"/>")).has_value());
}

TEST_CASE ("Every factory rig is valid", "[rigs]")
{
    ParagonX1AudioProcessor processor;
    juce::StringArray names, slots;

    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        names.addIfNotAlreadyThere (rig.name);
        slots.addIfNotAlreadyThere (rig.getSlotName());

        for (const auto& [id, value] : rig.parameters)
        {
            INFO (id);
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
            REQUIRE (parameter != nullptr);                          // no typos in parameter IDs
            CHECK_FALSE (ParamIDs::notInRigs.contains (id));        // rigs don't touch global settings
            const auto range = parameter->getNormalisableRange();
            CHECK (value >= range.start);
            CHECK (value <= range.end);
        }
    }

    CHECK (names.size() == (int) makeFactoryRigs().size());   // unique names
    CHECK (slots.size() == (int) makeFactoryRigs().size());   // no two in the same slot
}

TEST_CASE ("PR7-K's factory presets: Init at A01, then starting points that say what they are; PR7-E's mixing rigs are gone", "[rigs]")
{
    const auto rigs = makeFactoryRigs();
    REQUIRE (rigs.size() >= 8);
    CHECK (rigs.front().name == "Init");
    CHECK (rigs.front().getSlotName() == "A01");
    CHECK (rigs.front().parameters.empty());   // everything at its default: one saw, no filter

    for (const auto& rig : rigs)
    {
        INFO (rig.name);
        CHECK (rig.notes.isNotEmpty());
        CHECK_FALSE (rig.tags.isEmpty());
    }

    RigRig r;
    for (const auto* mixingRig : { "Lead Vocal", "Kick Punch", "Mix Brighter", "Master Gentle Polish", "Modern Metal Vocal" })
        CHECK (r.processor.getRigLibrary().findFactory (mixingRig) == nullptr);
}

TEST_CASE ("Every factory preset plays: a note sounds, stays clean, and fades away after it's let go", "[rigs]")
{
    RigRig r;
    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        r.processor.loadRig (r.entryNamed (rig.name));
        r.processor.prepareToPlay (48000.0, 256);   // fresh

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        float loudest = 0.0f;
        auto finite = true;
        // Held for 5 seconds (long enough for the slowest, the riser), then let go.
        for (int block = 0; block < 48000 * 5 / 256; ++block)
        {
            r.processor.processBlock (buffer, midi);
            midi.clear();
            loudest = juce::jmax (loudest, buffer.getMagnitude (0, 0, 256), buffer.getMagnitude (1, 0, 256));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
        }
        midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
        for (int block = 0; block < 48000 * 4 / 256; ++block)
        {
            r.processor.processBlock (buffer, midi);
            midi.clear();
        }
        CHECK (finite);
        CHECK (juce::Decibels::gainToDecibels (loudest) > -24.0f);   // it sounds
        CHECK (juce::Decibels::gainToDecibels (loudest) < 3.0f);     // and isn't wildly hot (OUTPUT at its -6 dB)
        CHECK (buffer.getMagnitude (0, 0, 256) < 1.0e-4f);           // gone 4 seconds after letting go
        CHECK (r.processor.getActiveVoices() == 0);                  // and so is its note
    }
}

TEST_CASE ("A rig's notes are saved with it and kept by Save As", "[rigs]")
{
    Rig rig;
    rig.name = "Noted";
    rig.notes = "For the mix bus.\nTrim first, then limit.";
    const auto loaded = Rig::fromXml (*rig.toXml());
    REQUIRE (loaded.has_value());
    CHECK (loaded->notes == rig.notes);

    // Saving a copy of a factory rig keeps its notes.
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("My Init"));
    const auto* saved = r.processor.getRigLibrary().find (r.processor.getCurrentRig().file);
    REQUIRE (saved != nullptr);
    CHECK (saved->rig.notes == r.entryNamed ("Init").rig.notes);
}

TEST_CASE ("The rig library saves, finds, renames and deletes rigs", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);
    const auto factoryCount = library.getEntries().size();

    Rig rig;
    rig.name = "Cyber Drive";
    const auto first = library.saveAsNew (rig);
    REQUIRE (first.existsAsFile());
    REQUIRE (library.find (first) != nullptr);

    SECTION ("A taken name gets a number")
    {
        const auto second = library.saveAsNew (rig);
        REQUIRE (library.find (second) != nullptr);
        CHECK (library.find (second)->rig.name == "Cyber Drive 2");
        CHECK (second != first);
    }

    SECTION ("Names that can't be file names still save")
    {
        rig.name = "AC/DC: Live?";
        const auto file = library.saveAsNew (rig);
        REQUIRE (file.existsAsFile());
        CHECK (library.find (file)->rig.name == "AC/DC: Live?");
    }

    SECTION ("Overwrite and delete")
    {
        auto changed = library.find (first)->rig;
        changed.tags = { "Lead" };
        REQUIRE (library.overwrite (first, changed));
        CHECK (library.find (first)->rig.tags == juce::StringArray { "Lead" });

        REQUIRE (library.remove (first));
        CHECK (library.find (first) == nullptr);
        CHECK (library.getEntries().size() == factoryCount);
    }

    SECTION ("A corrupt rig file is skipped, not fatal")
    {
        temp.folder.getChildFile (juce::String ("broken") + Rig::fileExtension).replaceWithText ("<ParagonRig name=");
        library.rescan();
        CHECK (library.getEntries().size() == factoryCount + 1);
    }
}

TEST_CASE ("Favourites stick, including on factory rigs", "[rigs]")
{
    TempRigFolder temp;
    {
        RigLibrary library (temp.folder);
        library.setFavourite (*library.findFactory ("Init"), true);
    }
    {
        RigLibrary reopened (temp.folder);
        CHECK (reopened.findFactory ("Init")->rig.favourite);
        reopened.setFavourite (*reopened.findFactory ("Init"), false);
    }

    RigLibrary reopenedAgain (temp.folder);
    CHECK_FALSE (reopenedAgain.findFactory ("Init")->rig.favourite);
}

TEST_CASE ("Rigs are sorted by bank and number, and < > wrap round", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);

    Rig late;
    late.name = "Late";
    late.bank = Rig::numBanks - 1;   // the last bank, after every factory rig
    late.number = 50;
    library.saveAsNew (late);

    const auto& entries = library.getEntries();
    for (size_t i = 1; i < entries.size(); ++i)
        CHECK (entries[i - 1].rig.bank * 100 + entries[i - 1].rig.number <= entries[i].rig.bank * 100 + entries[i].rig.number);

    CHECK (entries.back().rig.name == "Late");
    CHECK (library.step (&entries.back(), 1) == &entries.front());
    CHECK (library.step (&entries.front(), -1) == &entries.back());
    CHECK (library.step (nullptr, 1) == &entries.front());

    CHECK (library.nextFreeNumber (0) == 43);   // the factory presets fill A01 to A42
}

TEST_CASE ("Saving and loading a preset restores the sound but not the global settings", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::fltOn, 1.0f);
    r.set (someTime, 7.5f);
    r.set (someFine, 6.0f);
    r.set (ParamIDs::patchLevel, -3.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("My Sound"));

    // Change everything, including the global OUTPUT and TUNE.
    r.set (ParamIDs::fltOn, 0.0f);
    r.set (someTime, 1.0f);
    r.set (someFine, -2.0f);
    r.set (ParamIDs::patchLevel, 0.0f);
    r.set (ParamIDs::tune, 5.0f);
    r.set (ParamIDs::outputGain, -4.0f);

    r.processor.loadRig (r.entryNamed ("My Sound"));

    CHECK (r.get (ParamIDs::fltOn) > 0.5f);
    CHECK_THAT (r.get (someTime), WithinAbs (7.5, 0.01));
    CHECK_THAT (r.get (someFine), WithinAbs (6.0, 0.01));
    CHECK_THAT (r.get (ParamIDs::patchLevel), WithinAbs (-3.0, 0.01));
    // Global settings are left alone.
    CHECK_THAT (r.get (ParamIDs::tune), WithinAbs (5.0, 0.01));
    CHECK_THAT (r.get (ParamIDs::outputGain), WithinAbs (-4.0, 0.01));
}

TEST_CASE ("A preset that doesn't mention a parameter sets it to its default", "[rigs]")
{
    // E.g. a preset saved by an older version, before that parameter existed.
    RigRig r;
    r.set (someSwitch, 1.0f);
    r.set (someFine, 6.0f);

    Rig old;
    old.name = "Old Preset";
    old.parameters = { { ParamIDs::fltOn, 1.0f } };
    r.processor.getRigLibrary().saveAsNew (old);
    r.processor.loadRig (r.entryNamed ("Old Preset"));

    CHECK (r.get (ParamIDs::fltOn) > 0.5f);
    CHECK (r.get (someSwitch) < 0.5f);
    CHECK_THAT (r.get (someFine), WithinAbs (0.0, 0.01));
}

TEST_CASE ("The modified marker tracks changes since loading or saving", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    CHECK_FALSE (r.processor.isRigModified());

    SECTION ("Turning a knob marks it modified")
    {
        r.set (someTime, 9.0f);
        CHECK (r.processor.isRigModified());
    }

    SECTION ("The global settings don't count")
    {
        r.set (ParamIDs::tune, 3.0f);
        r.set (ParamIDs::outputGain, -3.0f);
        CHECK_FALSE (r.processor.isRigModified());
    }

    SECTION ("Saving clears it")
    {
        r.set (someTime, 9.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Saved"));
        CHECK_FALSE (r.processor.isRigModified());
        r.set (someTime, 2.0f);
        CHECK (r.processor.isRigModified());
        REQUIRE (r.processor.saveCurrentRig());   // a user preset now, so plain save works
        CHECK_FALSE (r.processor.isRigModified());
    }
}

TEST_CASE ("Factory presets can't be saved over", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    r.set (someFine, 9.0f);
    CHECK_FALSE (r.processor.saveCurrentRig());   // the UI then asks for a new name
    CHECK (r.entryNamed ("Init").rig.parameters.count (someFine) == 0);
}

TEST_CASE ("< and > step through the presets in order", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("Second"));   // filed after bank A's factory presets, at A43
    REQUIRE (r.processor.saveCurrentRigAs ("Third"));    // A44
    CHECK (r.entryNamed ("Second").rig.getSlotName() == "A43");
    r.processor.loadRig (r.entryNamed ("Tight Kit"));   // A42
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Second");
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Third");
    r.processor.stepRig (1);   // wraps round to the first
    CHECK (r.processor.getCurrentRig().name == "Init");
    r.processor.stepRig (-1);   // and back to the last
    CHECK (r.processor.getCurrentRig().name == "Third");
}

TEST_CASE ("Deleting the loaded preset keeps the sound, now unsaved", "[rigs]")
{
    RigRig r;
    r.set (someTime, 8.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("Temporary"));
    REQUIRE (r.processor.deleteRig (r.entryNamed ("Temporary")));

    CHECK_FALSE (r.processor.getCurrentRig().isUserRig());
    CHECK (r.processor.isRigModified());
    CHECK_THAT (r.get (someTime), WithinAbs (8.0, 0.01));
}

TEST_CASE ("The DAW project remembers which preset was loaded", "[rigs][state]")
{
    juce::MemoryBlock saved;
    juce::File rigFile;
    {
        RigRig r;
        r.set (someTime, 8.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Project Preset"));
        r.set (someTime, 3.0f);   // tweaked after saving
        rigFile = r.processor.getCurrentRig().file;
        r.processor.getStateInformation (saved);
    }

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getCurrentRig().name == "Project Preset");
    CHECK (restored.getCurrentRig().file == rigFile);
    CHECK (restored.isRigModified());   // the tweak is remembered as unsaved
    CHECK_THAT (restored.getParameters().getRawParameterValue (someTime)->load(), WithinAbs (3.0, 0.01));
}

TEST_CASE ("Each preset's LEVEL adds to OUTPUT", "[rigs]")
{
    const auto play = [] (float levelDb)
    {
        RigRig r;
        r.set (ParamIDs::patchLevel, levelDb);
        r.set (ParamIDs::osc (0, "PhaseRand"), 0.0f);   // the same note both times
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 127), 0);
        double sum = 0.0;
        for (int block = 0; block < 40; ++block)
        {
            r.processor.processBlock (buffer, midi);
            midi.clear();
            if (block >= 20)
                sum += buffer.getRMSLevel (0, 0, 256);
        }
        return sum;
    };
    CHECK_THAT (play (6.0f) / play (0.0f), WithinAbs (1.9953, 0.01));
}

TEST_CASE ("Presets live in their own Paragon PR7-K folder, apart from its siblings' rigs", "[rigs]")
{
    const auto folder = RigLibrary::defaultFolder();
    CHECK (folder.getFileName() == "Rigs");
    CHECK (folder.getParentDirectory().getFileName() == "Paragon PR7-K");
    CHECK (folder.getParentDirectory().getParentDirectory().getFileName() == "Paragon");

    // PR7-E's (mixing), PR7-B's (bass) and PR7-G's (guitar) rigs folders sit next to it, and are different folders.
    for (const auto* sibling : { "Paragon PR7-E", "Paragon PR7-B", "Paragon PR7-G" })
    {
        const auto siblingFolder = folder.getParentDirectory().getSiblingFile (sibling).getChildFile ("Rigs");
        CHECK (folder != siblingFolder);
        CHECK_FALSE (folder.isAChildOf (siblingFolder.getParentDirectory()));
    }
}

TEST_CASE ("Only .pr7rig files are rigs: the old Paragon X1 .x1rig files aren't read", "[rigs]")
{
    TempRigFolder temp;
    REQUIRE (temp.folder.createDirectory());
    Rig old;
    old.name = "From X1";
    REQUIRE (old.toXml()->writeTo (temp.folder.getChildFile ("From X1.x1rig")));

    RigLibrary library (temp.folder);
    CHECK (std::none_of (library.getEntries().begin(), library.getEntries().end(),
                         [] (const auto& entry) { return entry.rig.name == "From X1"; }));
}
