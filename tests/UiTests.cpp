#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/GraphicEqPanel.h"
#include "ui/EqPresets.h"
#include "ui/Finish.h"
#include "ui/PerformanceControls.h"
#include "ui/TunerPanel.h"

using Catch::Matchers::WithinAbs;

namespace
{
    template <typename Type>
    void findAll (juce::Component& parent, std::vector<Type*>& found)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                found.push_back (match);
            findAll (*child, found);
        }
    }

    // Every visible component sits inside its parent and has some size.
    void checkLayout (juce::Component& parent, const juce::String& path)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible() || dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                continue;

            const auto name = path + "/" + (child->getName().isNotEmpty() ? child->getName()
                                                                          : juce::String (typeid (*child).name()));
            INFO (name << " at " << child->getBounds().toString() << " in " << parent.getLocalBounds().toString());
            CHECK_FALSE (child->getBounds().isEmpty());
            CHECK (parent.getLocalBounds().expanded (1).contains (child->getBounds()));
            checkLayout (*child, name);
        }
    }

    std::vector<float> playThrough (ParagonX1AudioProcessor& processor, const std::vector<float>& input)
    {
        juce::AudioBuffer<float> buffer (2, (int) input.size());
        buffer.clear();
        std::copy (input.begin(), input.end(), buffer.getWritePointer (0));
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        return { buffer.getReadPointer (1), buffer.getReadPointer (1) + buffer.getNumSamples() };
    }

    void set (ParagonX1AudioProcessor& processor, const char* id, float plainValue)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }
}

TEST_CASE ("The window lays out cleanly at every size", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    for (const auto width : { 960, 1280, 1920 })
    {
        DYNAMIC_SECTION (width << " wide")
        {
            editor->setSize (width, width * 9 / 16);
            REQUIRE (editor->getWidth() == width);

            // The view is scaled, not re-laid-out: its transform matches the window.
            auto* view = editor->getChildComponent (0);
            REQUIRE (view != nullptr);
            CHECK_THAT (view->getTransform().mat00, WithinAbs ((float) width / (float) MainView::width, 0.001));

            checkLayout (*view, "view");
        }
    }
}

TEST_CASE ("The window opens as big as fits above the taskbar, up to 1824 x 1026", "[ui]")
{
    using Editor = ParagonX1AudioProcessorEditor;
    // 1080p with a 40-pixel taskbar: room left for the DAW's title bar and toolbar.
    const auto fullHd = Editor::openingSize ({ 1920, 1040 });
    CHECK (fullHd == juce::Point<int> (1706, 959));
    CHECK (fullHd.y + 80 <= 1040);
    CHECK (Editor::openingSize ({ 2560, 1400 }) == juce::Point<int> (1824, 1026));
    const auto laptop = Editor::openingSize ({ 1366, 728 });
    CHECK (laptop.x <= 1366 - 40);
    CHECK (laptop.y + 80 <= 728);
    CHECK (Editor::openingSize ({ 800, 600 }).x == 960);   // never below the smallest size
}

TEST_CASE ("PEARL BLACK recolours the unit's printing, and every open window follows", "[ui]")
{
    Finish::set (Finish::Type::pearlWhite, false);
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> first (processor.createEditor()), second (processor.createEditor());

    // RIG LEVEL is printed on the unit: ink on white, light on black.
    const auto rigLevelColour = [] (juce::Component& editor)
    {
        std::vector<juce::Label*> labels;
        findAll (editor, labels);
        for (auto* label : labels)
            if (label->getText() == "RIG LEVEL")
                return label->findColour (juce::Label::textColourId);
        return juce::Colour();
    };
    CHECK (rigLevelColour (*first) == ParagonColours::ink);

    // Chosen in one window (here, without saving the preference), it shows in both.
    Finish::set (Finish::Type::pearlBlack, false);
    CHECK (Finish::isBlack());
    CHECK (rigLevelColour (*first) == Finish::ink());
    CHECK (rigLevelColour (*second) == Finish::ink());
    CHECK (Finish::ink().getPerceivedBrightness() > 0.8f);

    // Pearl white with neon keys: the white unit's ink, the black unit's keys.
    Finish::set (Finish::Type::pearlWhiteNeon, false);
    CHECK (! Finish::isBlack());
    CHECK (Finish::neonKeys());
    CHECK (rigLevelColour (*first) == ParagonColours::ink);

    // The key and knob drawing still works in every finish.
    for (const auto type : { Finish::Type::pearlBlack, Finish::Type::pearlWhiteNeon, Finish::Type::pearlWhite })
    {
        Finish::set (type, false);
        first->setSize (1280, 720);
        const auto image = first->createComponentSnapshot (first->getLocalBounds());
        CHECK (image.isValid());
    }
    CHECK (rigLevelColour (*first) == ParagonColours::ink);
}

TEST_CASE ("The chain and the stomps pick what the dock shows; the graphic EQ is the default", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    std::vector<GraphicEqPanel*> eqPanels;
    findAll (*editor, eqPanels);
    REQUIRE (eqPanels.size() == 1);
    std::vector<TunerPanel*> tunerPanels;
    findAll (*editor, tunerPanels);
    REQUIRE (tunerPanels.size() == 1);
    CHECK (eqPanels[0]->isVisible());
    CHECK_FALSE (tunerPanels[0]->isVisible());

    // The chain has just the EQ (PR7-B's effects went in 0.3.0).
    std::vector<SignalChain*> chains;
    findAll (*editor, chains);
    REQUIRE (chains.size() == 1);
    chains[0]->onSelect (0);
    CHECK (eqPanels[0]->isVisible());

    // TUNER's name shows its panel; the GRAPHIC EQ button goes back.
    std::vector<Footswitch*> footswitches;
    findAll (*editor, footswitches);
    REQUIRE (footswitches.size() == 7);
    footswitches[0]->onSelect();
    CHECK (tunerPanels[0]->isVisible());
    CHECK_FALSE (eqPanels[0]->isVisible());

    std::vector<juce::TextButton*> buttons;
    findAll (*editor, buttons);
    const auto back = std::find_if (buttons.begin(), buttons.end(), [] (auto* b) { return b->getButtonText() == "GRAPHIC EQ"; });
    REQUIRE (back != buttons.end());
    CHECK ((*back)->isVisible());
    (*back)->onClick();
    CHECK (eqPanels[0]->isVisible());
    CHECK_FALSE (tunerPanels[0]->isVisible());
    CHECK_FALSE ((*back)->isVisible());
}

TEST_CASE ("Seven stomps, three over four, labelled from the plan", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);

    std::vector<Footswitch*> stomps;
    findAll (*editor, stomps);
    REQUIRE (stomps.size() == 7);

    const juce::StringArray names { "TUNER", "OPEN", "INSERTS", "EQ", "RESONANCE", "AUTO EQ", "LIMITER" };
    for (size_t i = 0; i < stomps.size(); ++i)
        CHECK (stomps[i]->getTitle() == names[(int) i] + " footswitch");

    // 1-3 on a row above 4-7, each top stomp in a gap between two below it,
    // so a foot on one can't catch another.
    const auto bounds = [&] (size_t i) { return stomps[i]->getBoundsInParent(); };
    for (size_t top = 0; top < 3; ++top)
    {
        INFO ("Stomp " << top + 1);
        CHECK (bounds (top).getY() < bounds (3).getY());
        CHECK (bounds (top).getCentreX() > bounds (top + 3).getCentreX());
        CHECK (bounds (top).getCentreX() < bounds (top + 4).getCentreX());
    }
    for (size_t bottom = 4; bottom < 7; ++bottom)
        CHECK (bounds (bottom).getY() == bounds (3).getY());

    // TUNER's name opens its panel (the trace) in place of the graphic EQ.
    std::vector<TunerPanel*> tunerPanels;
    findAll (*editor, tunerPanels);
    REQUIRE (tunerPanels.size() == 1);
    CHECK_FALSE (tunerPanels[0]->isVisible());
    stomps[0]->onSelect();
    CHECK (tunerPanels[0]->isVisible());
    stomps[3]->onSelect();
    CHECK_FALSE (tunerPanels[0]->isVisible());

    // TUNER and EQ switch what PR7-E already has.
    const auto isOn = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load() > 0.5f; };
    REQUIRE_FALSE (isOn (ParamIDs::tunerOn));
    stomps[0]->toggle();
    CHECK (isOn (ParamIDs::tunerOn));
    stomps[3]->toggle();
    CHECK (isOn (ParamIDs::eqOn));

    // The rest aren't built yet: their pads change nothing and their names open nothing.
    for (const auto i : { 1, 2, 4, 5, 6 })
    {
        INFO (names[i]);
        auto& stomp = *stomps[(size_t) i];
        CHECK (stomp.isPlaceholder());
        CHECK_FALSE (stomp.onSelect);
        const auto before = processor.captureRig().parameters;
        stomp.toggle();
        CHECK_FALSE (stomp.isAnyOn());
        CHECK (processor.captureRig().parameters == before);
    }
}

TEST_CASE ("BYPASS passes the clean guitar, on both channels", "[ui][bypass]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (1, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // Make the processed sound very different from the input...
    set (processor, ParamIDs::eqOn, 1.0f);
    set (processor, ParamIDs::eqLevel, 12.0f);
    set (processor, ParamIDs::inputGain, 12.0f);
    set (processor, ParamIDs::bypass, 1.0f);

    std::vector<float> input (512);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.3f * std::sin (0.05f * (float) i);

    for (int block = 0; block < 4; ++block)
        playThrough (processor, input);   // let the 20 ms bypass fade finish (one block is ~11 ms)
    const auto output = playThrough (processor, input);
    for (size_t i = 0; i < input.size(); ++i)
        REQUIRE_THAT (output[i], WithinAbs (input[i], 1.0e-6));

    CHECK (processor.getBypassParameter() == processor.getParameters().getParameter (ParamIDs::bypass));
}

TEST_CASE ("No latency: BYPASS and the processed sound line up with the input", "[ui][bypass]")
{
    // Nothing in PR7-E's chain looks ahead (the NAM amp, which did, went with
    // PR7-B's bass blocks), so the DAW needn't compensate and bypassing
    // can't shift the sound in time.
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (1, 2, 44100.0, 512);
    processor.prepareToPlay (44100.0, 512);
    CHECK (processor.getLatencySamples() == 0);

    std::vector<float> click (512, 0.0f);
    click[10] = 0.5f;
    for (const auto bypassed : { false, true })
    {
        INFO ("Bypassed: " << (bypassed ? "yes" : "no"));
        set (processor, ParamIDs::bypass, bypassed ? 1.0f : 0.0f);
        for (int block = 0; block < 4; ++block)
            playThrough (processor, std::vector<float> (512, 0.0f));   // let the bypass fade finish
        const auto output = playThrough (processor, click);
        CHECK_THAT (output[10], WithinAbs (0.5, 1.0e-6));
        CHECK_THAT (output[11], WithinAbs (0.0, 1.0e-6));
    }
}

TEST_CASE ("BYPASS is global: rigs don't change it and it doesn't mark them modified", "[ui][bypass]")
{
    ParagonX1AudioProcessor processor;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1UiTests").getChildFile (juce::Uuid().toString());
    processor.useRigFolder (folder);

    processor.loadRig (*processor.getRigLibrary().findFactory ("Init"));
    set (processor, ParamIDs::eqUltraHi, 1.0f);
    REQUIRE (processor.saveCurrentRigAs ("Reverb Rig"));
    const auto reverbRig = processor.getCurrentRig().file;
    processor.loadRig (*processor.getRigLibrary().findFactory ("Init"));
    set (processor, ParamIDs::bypass, 1.0f);
    CHECK_FALSE (processor.isRigModified());

    processor.loadRig (*processor.getRigLibrary().find (reverbRig));
    CHECK (processor.getParameters().getRawParameterValue (ParamIDs::eqUltraHi)->load() > 0.5f);   // the rig did load
    CHECK (processor.getParameters().getRawParameterValue (ParamIDs::bypass)->load() > 0.5f);
    folder.deleteRecursively();
}

TEST_CASE ("The expression screen: click a line to assign the pedal, again to unassign; scroll to step", "[ui]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    ExpressionAssign screen (state, ExpressionAssign::Style::pedal);
    screen.setBounds (0, 0, 88, 101);   // about its size on the unit

    const auto target = [&] { return juce::roundToInt (state.getRawParameterValue (ParamIDs::expressionTarget)->load()); };
    const auto rowCentre = [&] (int line)
    {
        // The glass's content: the heading at the top, the position bar at
        // the bottom, the lines showing evenly between (as the screen lays them out).
        const auto glass = screen.getLocalBounds().toFloat().reduced (7.0f).reduced (5.0f, 4.0f);
        const auto top = glass.getY() + 15.0f, height = (glass.getHeight() - 25.0f) / (float) ExpressionAssign::visibleRows;
        return juce::Point<float> (glass.getCentreX(), top + height * ((float) line + 0.5f));
    };
    const auto click = [&] (int line)
    {
        const auto position = rowCentre (line);
        const auto now = juce::Time::getCurrentTime();
        screen.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &screen, &screen, now, position, now, 1, false));
    };
    const auto scroll = [&] (float deltaY)
    {
        juce::MouseWheelDetails wheel {};
        wheel.deltaY = deltaY;
        const auto now = juce::Time::getCurrentTime();
        screen.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                 &screen, &screen, now, {}, now, 0, false), wheel);
    };

    REQUIRE (target() == ExpressionTargets::off);

    // VOLUME is the only target now (PR7-B's effects went in 0.3.0): the
    // other lines are empty, and clicking them does nothing.
    click (0);
    CHECK (target() == ExpressionTargets::volume);
    click (0);
    CHECK (target() == ExpressionTargets::off);
    for (int line = 1; line < ExpressionAssign::visibleRows; ++line)
        click (line);
    CHECK (target() == ExpressionTargets::off);

    // Scrolling down from unassigned starts at the top, and stops at the end.
    scroll (-1.0f);
    CHECK (target() == ExpressionTargets::volume);
    scroll (-1.0f);
    CHECK (target() == ExpressionTargets::volume);

    // The screen draws with its short list, at both styles' sizes.
    juce::Image image (juce::Image::ARGB, 88, 101, true);
    juce::Graphics g (image);
    screen.paintEntireComponent (g, false);
}

TEST_CASE ("The dock's expression screen scrolls with the buttons beside it", "[ui]")
{
    ParagonX1AudioProcessor processor;
    ExpressionAssign screen (processor.getParameters(), ExpressionAssign::Style::list);
    screen.setBounds (0, 0, 272, 160);   // about its size in the EXPRESSION ASSIGN panel

    std::vector<juce::Button*> buttons;
    findAll (screen, buttons);
    REQUIRE (buttons.size() == 2);
    auto* up = buttons[0];
    auto* down = buttons[1];
    // Both inside the component, to the right of the screen.
    for (auto* button : buttons)
    {
        CHECK (screen.getLocalBounds().contains (button->getBounds()));
        CHECK (button->getX() > screen.getWidth() / 2);
    }

    // The list (just VOLUME) fits on the screen: neither scrolls.
    CHECK (! up->isEnabled());
    CHECK (! down->isEnabled());
    down->onClick();
    juce::Image image (juce::Image::ARGB, 272, 160, true);
    juce::Graphics g (image);
    screen.paintEntireComponent (g, false);   // and still draws

    // Scrolling never assigns the pedal.
    CHECK (juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::expressionTarget)->load()) == ExpressionTargets::off);
}

TEST_CASE ("EQ PRESETS: choosing one sets the faders and switches the EQ on; moving a fader lets go of the name", "[ui]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    GraphicEqPanel panel (state);
    panel.setBounds (0, 0, 520, 220);

    juce::ComboBox* menu = nullptr;
    for (auto* child : panel.getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*> (child))
            menu = box;
    REQUIRE (menu != nullptr);
    CHECK (menu->getSelectedItemIndex() == -1);   // a flat EQ isn't one of the presets

    const auto value = [&] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };
    const auto& scoop = EqPresets::all[0];
    menu->setSelectedItemIndex (0, juce::sendNotificationSync);

    for (int band = 0; band < effects::GraphicEq::numBands; ++band)
        CHECK_THAT (value (ParamIDs::eqBands[band]), WithinAbs (scoop.gainsDb[(size_t) band], 0.05));
    CHECK_THAT (value (ParamIDs::eqLevel), WithinAbs (scoop.levelDb, 0.05));
    CHECK (value (ParamIDs::eqOn) > 0.5f);
    CHECK (menu->getText() == juce::String (scoop.name));

    // Nudge 500 Hz: it's no longer the preset, so the menu says PRESETS again.
    auto* band = state.getParameter (ParamIDs::eqBands[4]);
    band->setValueNotifyingHost (band->convertTo0to1 (0.0f));
    CHECK (menu->getSelectedItemIndex() == -1);

    // A preset that uses the amp's ULTRA LO / ULTRA HI switches them on; one
    // that doesn't switches them off again, so each preset sounds the same
    // whatever was set before.
    const auto withUltra = (int) std::distance (EqPresets::all.begin(),
                                                std::find_if (EqPresets::all.begin(), EqPresets::all.end(),
                                                              [] (const auto& preset) { return preset.ultraLo && preset.ultraHi; }));
    REQUIRE (withUltra < (int) EqPresets::all.size());
    menu->setSelectedItemIndex (withUltra, juce::sendNotificationSync);
    CHECK (value (ParamIDs::eqUltraLo) > 0.5f);
    CHECK (value (ParamIDs::eqUltraHi) > 0.5f);
    CHECK (menu->getSelectedItemIndex() == withUltra);

    // Switching ULTRA HI off by hand makes it your own curve.
    auto* ultraHi = state.getParameter (ParamIDs::eqUltraHi);
    ultraHi->setValueNotifyingHost (0.0f);
    CHECK (menu->getSelectedItemIndex() == -1);

    menu->setSelectedItemIndex (0, juce::sendNotificationSync);
    CHECK (value (ParamIDs::eqUltraLo) < 0.5f);
    CHECK (value (ParamIDs::eqUltraHi) < 0.5f);
}
