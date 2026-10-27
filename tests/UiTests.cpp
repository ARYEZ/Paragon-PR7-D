// The window: its layout, the oscillator tabs, the pages and the on-screen keyboard.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

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
            // A list's rows are placed by JUCE beyond its visible part, for scrolling.
            if (dynamic_cast<juce::ListBox*> (&parent) != nullptr)
                continue;

            const auto name = path + "/" + (child->getName().isNotEmpty() ? child->getName()
                                                                          : juce::String (typeid (*child).name()));
            INFO (name << " at " << child->getBounds().toString() << " in " << parent.getLocalBounds().toString());
            CHECK_FALSE (child->getBounds().isEmpty());
            CHECK (parent.getLocalBounds().expanded (1).contains (child->getBounds()));
            checkLayout (*child, name);
        }
    }

    float value (ParagonX1AudioProcessor& processor, const juce::String& id)
    {
        return processor.getParameters().getRawParameterValue (id)->load();
    }

    // The control showing `id` in the window, or nullptr.
    ParamControl* controlFor (juce::Component& root, const juce::String& id)
    {
        std::vector<ParamControl*> controls;
        findAll (root, controls);
        for (auto* control : controls)
            if (control->getParameterId() == id)
                return control;
        return nullptr;
    }

    template <typename Type>
    Type* inside (juce::Component& parent)
    {
        std::vector<Type*> found;
        findAll (parent, found);
        return found.empty() ? nullptr : found.front();
    }

    struct Window
    {
        Window()
        {
            editor.reset (processor.createEditor());
            editor->setSize (1600, 900);
            view = dynamic_cast<MainView*> (editor->getChildComponent (0));
            REQUIRE (view != nullptr);
        }
        ParagonX1AudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        MainView* view = nullptr;
    };
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

TEST_CASE ("The oscillator tabs point one set of controls at the chosen oscillator", "[ui]")
{
    Window w;
    auto& oscillators = w.view->getOscillatorPanel();
    CHECK (oscillators.getSelected() == 0);
    REQUIRE (controlFor (*w.view, ParamIDs::osc (0, "Volume")) != nullptr);

    oscillators.selectOscillator (2);
    CHECK (controlFor (*w.view, ParamIDs::osc (0, "Volume")) == nullptr);
    auto* volume = controlFor (*w.view, ParamIDs::osc (2, "Volume"));
    REQUIRE (volume != nullptr);

    // Turning OSC 3's VOLUME knob turns OSC 3's volume, and leaves OSC 1's alone.
    auto* knob = inside<juce::Slider> (*volume);
    REQUIRE (knob != nullptr);
    knob->setValue (0.5, juce::sendNotificationSync);
    CHECK_THAT (value (w.processor, ParamIDs::osc (2, "Volume")), WithinAbs (0.5, 1.0e-3));
    CHECK_THAT (value (w.processor, ParamIDs::osc (0, "Volume")), WithinAbs (1.0, 1.0e-6));

    // Every setting of the tab is re-pointed, not just some. (ON is the tab's
    // own LED; TABLE is the wavetable chooser, which follows the tab too: its
    // own test is below.)
    for (const auto* what : ParamIDs::oscSettings)
        if (juce::String (what) != "On" && juce::String (what) != "Table")
            CHECK (controlFor (*w.view, ParamIDs::osc (2, what)) != nullptr);
}

TEST_CASE ("Each oscillator tab's LED switches that oscillator on and off", "[ui]")
{
    Window w;
    std::vector<juce::ToggleButton*> leds;
    findAll (w.view->getOscillatorPanel(), leds);
    // The first eight toggles in the panel are the tabs' LEDs, in order.
    REQUIRE (leds.size() >= (size_t) ParamIDs::numOscillators);
    CHECK (leds[0]->getToggleState());   // OSC 1 is on in a new sound
    CHECK_FALSE (leds[4]->getToggleState());
    leds[4]->setToggleState (true, juce::sendNotificationSync);
    CHECK (value (w.processor, ParamIDs::osc (4, "On")) > 0.5f);
}

TEST_CASE ("PRESETS and SETTINGS cover the synth panels; the keyboard stays", "[ui]")
{
    Window w;
    auto* keyboard = inside<juce::MidiKeyboardComponent> (*w.view);
    REQUIRE (keyboard != nullptr);
    CHECK (w.view->getOscillatorPanel().isVisible());

    w.view->showPage (MainView::Page::presets);
    CHECK_FALSE (w.view->getOscillatorPanel().isVisible());
    CHECK (keyboard->isVisible());
    checkLayout (*w.view, "presets");

    w.view->showPage (MainView::Page::settings);
    CHECK_FALSE (w.view->getOscillatorPanel().isVisible());
    checkLayout (*w.view, "settings");

    w.view->showPage (MainView::Page::synth);
    CHECK (w.view->getOscillatorPanel().isVisible());
}

TEST_CASE ("The on-screen keyboard plays the synth", "[ui]")
{
    Window w;
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);   // silence until a key goes down

    w.processor.getKeyboardState().noteOn (1, 60, 0.9f);   // as clicking a key does
    for (int block = 0; block < 4; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) > 0.05f);
    CHECK (w.processor.getActiveVoices() == 1);

    w.processor.getKeyboardState().noteOff (1, 60, 0.0f);
    for (int block = 0; block < 100; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);
    CHECK (w.processor.getActiveVoices() == 0);
}

TEST_CASE ("The oscillator's VOICES page: unison, vibrato and the chorder, laid out cleanly", "[ui]")
{
    Window w;
    auto& oscillators = w.view->getOscillatorPanel();
    oscillators.selectOscillator (1);
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (1, "UniMix"))->isVisible());   // on the WAVE page, hidden

    oscillators.showPage (OscillatorPanel::Page::voices);
    for (const auto* what : { "UniVoices", "UniMix", "UniDetune", "UniSpread", "UniOctaves", "VibMode", "VibRate", "VibAmount", "Ch1On", "Ch4Vol" })
    {
        INFO (what);
        auto* control = controlFor (*w.view, ParamIDs::osc (1, what));
        REQUIRE (control != nullptr);
        CHECK (control->isVisible());
    }
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (1, "Transpose"))->isVisible());
    checkLayout (*w.view, "voices");

    // Turning MIX on this page turns oscillator 2's unison up.
    auto* mix = inside<juce::Slider> (*controlFor (*w.view, ParamIDs::osc (1, "UniMix")));
    REQUIRE (mix != nullptr);
    mix->setValue (0.7, juce::sendNotificationSync);
    CHECK_THAT (value (w.processor, ParamIDs::osc (1, "UniMix")), WithinAbs (0.7, 1.0e-3));

    oscillators.showPage (OscillatorPanel::Page::wave);
    CHECK (controlFor (*w.view, ParamIDs::osc (1, "Transpose"))->isVisible());
}

TEST_CASE ("WAVETABLE: the oscillator shows TABLE, INDEX, MODE and FRAMES in place of WIDTH, without overlaps", "[ui][wavetable]")
{
    Window w;
    auto& oscillators = w.view->getOscillatorPanel();
    oscillators.selectOscillator (1);
    CHECK (controlFor (*w.view, ParamIDs::osc (1, "Width"))->isVisible());
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (1, "WtIndex"))->isVisible());
    CHECK_FALSE (oscillators.getWavetableChooser().isVisible());

    auto* wave = w.processor.getParameters().getParameter (ParamIDs::osc (1, "Wave"));
    wave->setValueNotifyingHost (wave->convertTo0to1 ((float) synth::OscWave::wavetable));
    oscillators.selectOscillator (1);   // as the panel's timer would see it
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (1, "Width"))->isVisible());
    for (const auto* what : { "WtIndex", "WtMode", "WtFrames", "Transpose", "Volume", "Voices" })
    {
        INFO (what);
        CHECK (controlFor (*w.view, ParamIDs::osc (1, what))->isVisible());
    }
    REQUIRE (oscillators.getWavetableChooser().isVisible());
    checkLayout (*w.view, "wavetable");

    // Nothing on the panel overlaps anything else.
    std::vector<juce::Component*> shown;
    for (auto* child : oscillators.getChildren())
        if (child->isVisible() && (dynamic_cast<ParamControl*> (child) != nullptr || child == &oscillators.getWavetableChooser()))
            shown.push_back (child);
    for (size_t i = 0; i < shown.size(); ++i)
        for (size_t j = i + 1; j < shown.size(); ++j)
        {
            INFO (shown[i]->getBounds().toString() << " and " << shown[j]->getBounds().toString());
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
        }

    // Choosing a factory table from the TABLE menu sets oscillator 2's.
    auto* box = inside<juce::ComboBox> (oscillators.getWavetableChooser());
    REQUIRE (box != nullptr);
    box->setSelectedId (synth::factoryTables::names.indexOf ("Vowels") + 1, juce::sendNotificationSync);
    CHECK (juce::roundToInt (value (w.processor, ParamIDs::osc (1, "Table"))) == synth::factoryTables::names.indexOf ("Vowels"));
    CHECK (w.processor.getWavetable (1)->getName() == "Vowels");
    CHECK (box->getText() == "Vowels");
}

TEST_CASE ("The oscillator's SHAPE page: SKEW, PINCH, FORMANT and CRUSH beside the wave, laid out cleanly", "[ui][shape]")
{
    Window w;
    auto& oscillators = w.view->getOscillatorPanel();
    oscillators.selectOscillator (2);
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (2, "Skew"))->isVisible());   // on the WAVE page, hidden

    oscillators.showPage (OscillatorPanel::Page::shape);
    for (const auto* what : { "Skew", "Pinch", "Formant", "FormantMode", "Crush", "CrushMode", "Wave", "Width", "Gain" })
    {
        INFO (what);
        CHECK (controlFor (*w.view, ParamIDs::osc (2, what))->isVisible());
    }
    for (const auto* what : { "Transpose", "Noise", "UniMix" })
    {
        INFO (what);
        CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (2, what))->isVisible());
    }
    checkLayout (*w.view, "shape");

    std::vector<juce::Component*> shown;
    for (auto* child : oscillators.getChildren())
        if (child->isVisible() && dynamic_cast<juce::Button*> (child) == nullptr)
            shown.push_back (child);
    for (size_t i = 0; i < shown.size(); ++i)
        for (size_t j = i + 1; j < shown.size(); ++j)
        {
            INFO (shown[i]->getBounds().toString() << " and " << shown[j]->getBounds().toString());
            CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
        }

    // Its knobs turn oscillator 3's.
    auto* skew = inside<juce::Slider> (*controlFor (*w.view, ParamIDs::osc (2, "Skew")));
    REQUIRE (skew != nullptr);
    skew->setValue (0.4, juce::sendNotificationSync);
    CHECK_THAT (value (w.processor, ParamIDs::osc (2, "Skew")), WithinAbs (0.4, 1.0e-3));
    CHECK (w.processor.getSynthSettings().oscillators[2].hasBend());
}
