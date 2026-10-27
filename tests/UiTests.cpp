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
    // own test is below. The SHAPER switches are on the shapers' pages, one per oscillator.)
    for (const auto* what : ParamIDs::oscSettings)
        if (juce::String (what) != "On" && juce::String (what) != "Table" && ! juce::String (what).startsWith ("Shp"))
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

TEST_CASE ("The oscillator's FM page: SYNC, FM and AM beside the wave, laid out cleanly", "[ui][modulation]")
{
    Window w;
    auto& oscillators = w.view->getOscillatorPanel();
    oscillators.selectOscillator (4);
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (4, "FmAmount"))->isVisible());

    oscillators.showPage (OscillatorPanel::Page::fm);
    for (const auto* what : { "SyncAmount", "SyncMode", "SyncEnv", "SyncEnvTime", "SyncEnvCurve", "FmAmount", "FmSource", "AmAmount", "AmShape",
                              "ModRate", "ModGrid", "ModLock", "Wave", "Gain" })
    {
        INFO (what);
        CHECK (controlFor (*w.view, ParamIDs::osc (4, what))->isVisible());
    }
    for (const auto* what : { "Skew", "Transpose", "UniMix" })
    {
        INFO (what);
        CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (4, what))->isVisible());
    }
    checkLayout (*w.view, "fm");

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

    auto* fm = inside<juce::Slider> (*controlFor (*w.view, ParamIDs::osc (4, "FmAmount")));
    REQUIRE (fm != nullptr);
    fm->setValue (0.3, juce::sendNotificationSync);
    CHECK (w.processor.getSynthSettings().oscillators[4].hasFm());
}

TEST_CASE ("The oscillator's FFT page: the editor, its views and knobs, laid out cleanly", "[ui][fft]")
{
    Window w;
    auto& oscillators = w.view->getOscillatorPanel();
    oscillators.selectOscillator (2);
    CHECK_FALSE (oscillators.getSpectrumEditor().isVisible());
    oscillators.showPage (OscillatorPanel::Page::fft);
    REQUIRE (oscillators.getSpectrumEditor().isVisible());
    for (const auto* what : { "FftSweep", "FftFlip", "FftLp", "FftHp", "FftSlope", "FftVSpeed", "FftVStrength", "Wave", "Gain" })
    {
        INFO (what);
        CHECK (controlFor (*w.view, ParamIDs::osc (2, what))->isVisible());
    }
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (2, "Transpose"))->isVisible());
    checkLayout (*w.view, "fft");

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

    // Each view's bands sit left to right and are found again where they're drawn.
    auto& editor = oscillators.getSpectrumEditor();
    for (const auto view : { SpectrumEditor::View::free, SpectrumEditor::View::harmonic, SpectrumEditor::View::bins })
    {
        editor.setView (view);
        const auto last = view == SpectrumEditor::View::harmonic ? SpectrumEditor::harmonicColumns : synth::SpectrumEdit::bands - 1;
        for (const auto band : { 0, 1, 7, 23, last })
        {
            INFO ((int) view << " band " << band);
            CHECK (editor.bandAt (editor.xForBand (band)) == band);
        }
        CHECK (editor.xForBand (0) < editor.xForBand (last));
    }
    CHECK_THAT (editor.dbAt (editor.yForDb (-12.0f)), WithinAbs (-12.0, 0.01));
    CHECK (SpectrumEditor::intervalName (3) == "5th");
    CHECK (SpectrumEditor::intervalName (7) == "b7");
}

TEST_CASE ("The oscillator's DRAW page: drawing switches the oscillator to FREEFORM and changes what plays", "[ui][freeform]")
{
    Window w;
    auto& oscillators = w.view->getOscillatorPanel();
    oscillators.selectOscillator (1);
    CHECK_FALSE (oscillators.getFreeformEditor().isVisible());
    oscillators.showPage (OscillatorPanel::Page::draw);
    auto& editor = oscillators.getFreeformEditor();
    REQUIRE (editor.isVisible());
    CHECK (controlFor (*w.view, ParamIDs::osc (1, "Wave"))->isVisible());
    CHECK_FALSE (controlFor (*w.view, ParamIDs::osc (1, "Transpose"))->isVisible());
    checkLayout (*w.view, "draw");

    // A stroke across the top-left quarter: those points go up, the rest stay flat.
    const auto area = editor.getPlotArea();
    const auto at = [&] (float x, float y) { return juce::Point<float> (area.getX() + area.getWidth() * x, area.getY() + area.getHeight() * y); };
    const auto event = [&] (juce::Point<float> p, int clicks = 1)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &editor, &editor, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), clicks, false);
    };
    editor.mouseDown (event (at (0.02f, 0.25f)));
    editor.mouseDrag (event (at (0.23f, 0.25f)));
    editor.mouseUp (event (at (0.23f, 0.25f)));
    CHECK (juce::roundToInt (value (w.processor, ParamIDs::osc (1, "Wave"))) == (int) synth::OscWave::freeform);
    const auto& drawing = w.processor.getDrawing (1);
    CHECK_THAT (drawing.values[64], WithinAbs (0.5, 0.02));
    CHECK_THAT (drawing.values[300], WithinAbs (0.0, 1.0e-6));
    CHECK (editor.pointAt (editor.getPlotArea().getRight() + 50.0f) == synth::FreeformDrawing::points - 1);
    CHECK_THAT (editor.valueAt (area.getCentreY()), WithinAbs (0.0, 0.01));
}

TEST_CASE ("The FILTER panel's SHP tabs: each shaper's page, its controls pointed at it, laid out cleanly", "[ui][shaper]")
{
    Window w;
    auto& panel = w.view->getFilterPanel();
    CHECK (controlFor (*w.view, ParamIDs::fltCutoff)->isVisible());
    for (int k = 0; k < ParamIDs::numShapers; ++k)
    {
        INFO ("shaper " << k + 1);
        panel.showPage (k + 1);
        CHECK (panel.getPage() == k + 1);
        CHECK_FALSE (controlFor (*w.view, ParamIDs::fltCutoff)->isVisible());
        for (const auto* what : { "On", "Model", "Place", "Drive", "DriveVel", "Mix", "Output", "Eq1Gain", "Eq1Freq", "Eq1Q",
                                  "Eq2Gain", "Eq2Freq", "Eq2Q", "EqSplit" })
        {
            INFO (what);
            auto* control = controlFor (*w.view, ParamIDs::shp (k, what));
            REQUIRE (control != nullptr);
            CHECK (control->isVisible());
        }
        for (int i = 0; i < ParamIDs::numOscillators; ++i)
            CHECK (controlFor (*w.view, ParamIDs::osc (i, ParamIDs::oscSettings[ParamIDs::oscSetting ("Shp1") + k])) != nullptr);
        checkLayout (*w.view, "shaper");

        std::vector<juce::Component*> shown;
        for (auto* child : panel.getChildren())
            if (child->isVisible())
                shown.push_back (child);
        for (size_t i = 0; i < shown.size(); ++i)
            for (size_t j = i + 1; j < shown.size(); ++j)
            {
                INFO (shown[i]->getBounds().toString() << " and " << shown[j]->getBounds().toString());
                CHECK_FALSE (shown[i]->getBounds().intersects (shown[j]->getBounds()));
            }
    }
    panel.showPage (0);
    CHECK (controlFor (*w.view, ParamIDs::fltCutoff)->isVisible());
}

TEST_CASE ("The MOD page: LFOs, macros and the matrix; adding and editing a route reaches the synth", "[ui][mod]")
{
    Window w;
    w.view->showPage (MainView::Page::modulation);
    auto& page = w.view->getModPage();
    REQUIRE (page.isVisible());
    CHECK_FALSE (w.view->getOscillatorPanel().isVisible());
    checkLayout (*w.view, "mod");

    // Each LFO tab points the controls at its LFO; SYNC swaps RATE for LENGTH.
    page.getModulatorPanel().selectLfo (2);
    CHECK (controlFor (*w.view, ParamIDs::lfo (2, "Rate"))->isVisible());
    auto* sync = w.processor.getParameters().getParameter (ParamIDs::lfo (2, "Sync"));
    sync->setValueNotifyingHost (sync->convertTo0to1 ((float) synth::mod::LfoSync::straight));
    page.getModulatorPanel().selectLfo (2);
    CHECK (controlFor (*w.view, ParamIDs::lfo (2, "Division"))->isVisible());
    CHECK_FALSE (controlFor (*w.view, ParamIDs::lfo (2, "Rate"))->isVisible());
    for (const auto* id : ParamIDs::macros)
        CHECK (controlFor (*w.view, id) != nullptr);

    // + ADD ROUTE adds a row (LFO 1, no destination yet: not playing).
    auto& matrix = page.getMatrixPanel();
    CHECK (matrix.getNumRows() == 0);
    matrix.addRoute();
    REQUIRE (matrix.getNumRows() == 1);
    CHECK (w.processor.getModMatrix().routes.size() == 1);
    CHECK (w.processor.getNumPlayingRoutes() == 0);

    // A destination (as its menu would set it), a source and an amount, from the row's own controls.
    auto edited = w.processor.getModMatrix();
    edited.routes[0].destination = ParamIDs::fltCutoff;
    w.processor.setModMatrix (edited);
    auto* row = matrix.getRow (0);
    REQUIRE (row != nullptr);
    row->source.setSelectedId ((int) synth::mod::Source::modWheel + 1, juce::sendNotificationSync);
    row->amount.setValue (-0.25, juce::sendNotificationSync);
    row->mode.onClick();   // what a click runs
    const auto& route = w.processor.getModMatrix().routes[0];
    CHECK (route.source == "modWheel");
    CHECK_THAT (route.amount, WithinAbs (-0.25, 1.0e-6));
    CHECK (route.percent);
    CHECK (w.processor.getNumPlayingRoutes() == 1);
    checkLayout (*w.view, "mod with a route");

    // X deletes it.
    row->remove.onClick();
    CHECK (w.processor.getModMatrix().routes.empty());
}
