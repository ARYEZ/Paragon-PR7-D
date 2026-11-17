// The window: its layout, the pages and their tabs, and the on-screen keyboard.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PluginTestUtils.h"
#include "ui/Finish.h"
#include "ui/PedalBody.h"

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
            // A list's rows are placed by JUCE beyond its visible part, for scrolling; so is a viewport's content.
            if (dynamic_cast<juce::ListBox*> (&parent) != nullptr
                || (parent.getParentComponent() != nullptr && dynamic_cast<juce::Viewport*> (parent.getParentComponent()) != nullptr))
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
            testkit::useRootKeys (processor);   // (0.59.0: ROOT's keys, as these tests play them)
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

TEST_CASE ("The window opens on the DRUMS page; GROOVES, FX, PRESETS and SETTINGS cover it; the song strip and OUTPUT stay", "[ui]")
{
    Window w;
    // 0.41.0: no on-screen keyboard anywhere; the SONG track has its strip.
    CHECK (inside<juce::MidiKeyboardComponent> (*w.view) == nullptr);
    auto* keyboard = &w.view->getSongTrack();
    CHECK_FALSE (keyboard->isVisible());   // 0.61.0: hidden when the window opens
    w.view->setTrackShown (true);
    CHECK (keyboard->isVisible());
    CHECK (w.view->getPage() == MainView::Page::drums);
    CHECK (w.view->getDrumPage().isVisible());
    checkLayout (*w.view, "drums");

    // The six tabs (0.20.0: MIXER), side by side in the header, the shown page's lit.
    std::vector<juce::Button*> tabs;
    for (const auto* id : { "drumsTab", "groovesTab", "mixerTab", "fxTab", "presetsTab", "settingsTab" })
    {
        auto* tab = dynamic_cast<juce::Button*> (w.view->findChildWithID (id));
        INFO (id);
        REQUIRE (tab != nullptr);
        CHECK (tab->getBottom() < 64);
        CHECK (tab->getWidth() > 110);   // six share the group (each its icon and name)
        if (! tabs.empty())
            CHECK (tab->getX() >= tabs.back()->getRight());
        tabs.push_back (tab);
    }
    CHECK (tabs[0]->getToggleState());

    const std::pair<MainView::Page, int> pages[] = { { MainView::Page::grooves, 1 }, { MainView::Page::mixer, 2 }, { MainView::Page::fx, 3 },
                                                     { MainView::Page::presets, 4 }, { MainView::Page::settings, 5 } };
    for (const auto& [page, tab] : pages)
    {
        INFO (tab);
        w.view->showPage (page);
        CHECK_FALSE (w.view->getDrumPage().isVisible());
        CHECK (keyboard->isVisible());
        CHECK (tabs[(size_t) tab]->getToggleState());
        checkLayout (*w.view, "page");
    }
    CHECK (w.view->getFxPage().isVisible() == false);   // SETTINGS shown last
    CHECK (w.view->getGroovesPage().isVisible() == false);
    tabs[0]->onClick();   // as clicking DRUMS does
    CHECK (w.view->getDrumPage().isVisible());
}

TEST_CASE ("SHOW / HIDE MIDI TRACK (0.61.0): the window opens without the track, shorter; the button and the song's tools bring it back", "[ui][track]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);

    // Opens hidden: the view 752 tall, the window that shape (wider than 16:9).
    CHECK_FALSE (view->isTrackShown());
    CHECK_FALSE (view->getSongTrack().isVisible());
    CHECK (view->getHeight() == MainView::heightFor (false));
    CHECK (MainView::heightFor (false) == 752);
    const auto width = editor->getWidth();
    CHECK (editor->getHeight() == width * 752 / MainView::width);
    checkLayout (*view, "hidden");

    // The button at the OUTPUT strip's foot says what it will do.
    juce::TextButton* button = nullptr;
    std::vector<juce::TextButton*> buttons;
    findAll (*view, buttons);
    for (auto* b : buttons)
        if (b->getButtonText() == "SHOW\nMIDI TRACK")
            button = b;
    REQUIRE (button != nullptr);
    CHECK (button->isVisible());
    CHECK (button->getBottom() <= view->getHeight());
    CHECK (button->getX() > 1480);

    // Shown: the track, the full 900, the window taller at the same width.
    button->onClick();
    CHECK (view->isTrackShown());
    CHECK (view->getSongTrack().isVisible());
    CHECK (button->getButtonText() == "HIDE\nMIDI TRACK");
    CHECK (view->getHeight() == MainView::height);
    CHECK (editor->getWidth() == width);
    CHECK (std::abs (editor->getHeight() - width * 900 / MainView::width) <= 1);
    CHECK (view->getSongTrack().getBottom() <= view->getHeight());
    checkLayout (*view, "shown");

    // Hidden again: back to the shorter window, same width.
    button->onClick();
    CHECK_FALSE (view->getSongTrack().isVisible());
    CHECK (std::abs (editor->getHeight() - width * 752 / MainView::width) <= 1);

    // A width the person dragged to is kept through it.
    editor->setSize (1280, 1280 * 752 / 1600);
    view->setTrackShown (true);
    CHECK (editor->getWidth() == 1280);
    CHECK (editor->getHeight() == 720);
    view->setTrackShown (false);

    // The song's tools bring it back (ADD TO SONG, EDIT PLAY STYLE, the GRID EDITOR, the SONG CREATOR call showSong).
    view->showSong (true);
    CHECK (view->isTrackShown());
    view->setTrackShown (false);

    // THE DRUM MACHINE keeps the full height (it covers everything under the header) and hides the button.
    view->getDrumPage().showView (DrumPage::View::machine);
    CHECK (view->getHeight() == MainView::height);
    CHECK_FALSE (button->isVisible());
    CHECK_FALSE (view->getSongTrack().isVisible());
    view->getDrumPage().showView (DrumPage::View::kit);
    CHECK (view->getHeight() == MainView::heightFor (false));
    CHECK (button->isVisible());

    // Each window opens with it hidden again (not saved).
    view->setTrackShown (true);
    editor.reset();
    editor.reset (processor.createEditor());
    view = dynamic_cast<MainView*> (editor->getChildComponent (0));
    CHECK_FALSE (view->isTrackShown());
}

TEST_CASE ("Notes play the kit (the processor's keyboard state, as the DAW's notes); others don't", "[ui]")
{
    Window w;
    w.processor.setPlayConfigDetails (0, 2, 48000.0, 256);
    w.processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);   // silence until a key goes down

    w.processor.getKeyboardState().noteOn (1, 38, 0.9f);   // the snare, as clicking its key does
    for (int block = 0; block < 4; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) > 0.05f);
    CHECK (w.processor.getActiveVoices() >= 1);
    w.processor.getKeyboardState().noteOff (1, 38, 0.0f);

    w.processor.getKeyboardState().noteOn (1, 72, 0.9f);   // outside the kit: nothing new
    for (int block = 0; block < 200; ++block)
        w.processor.processBlock (buffer, midi);
    CHECK (buffer.getMagnitude (0, 256) == 0.0f);
    CHECK (w.processor.getActiveVoices() == 0);
    w.processor.getKeyboardState().noteOff (1, 72, 0.0f);

    // (0.41.0: the on-screen keyboard and its tinted keys are gone; the kit still plays from notes, as above.)
}

TEST_CASE ("OBSIDIAN and AURORA (0.87.0): SETTINGS offers the two, an old Pearl White opens as Obsidian, every window follows", "[ui]")
{
    // Saved numbers keep their meaning: the retired Pearl White (0) and anything unknown open as Obsidian, the old
    // Pearl Black (1) is Obsidian, the old Pearl White + Neon (2) is Aurora.
    CHECK (Finish::fromSaved (0) == Finish::Type::obsidian);
    CHECK (Finish::fromSaved (1) == Finish::Type::obsidian);
    CHECK (Finish::fromSaved (2) == Finish::Type::aurora);
    CHECK (Finish::fromSaved (7) == Finish::Type::obsidian);
    Finish::set (Finish::Type::retiredPearlWhite, false);
    CHECK (Finish::get() == Finish::Type::obsidian);

    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> first (processor.createEditor()), second (processor.createEditor());
    std::vector<juce::TextButton*> buttons;
    findAll (*first, buttons);
    juce::StringArray finishes;
    juce::TextButton* aurora = nullptr;
    juce::TextButton* obsidian = nullptr;
    for (auto* button : buttons)
    {
        const auto text = button->getButtonText();
        if (text.containsIgnoreCase ("PEARL") || text.containsIgnoreCase ("NEON") || text == "OBSIDIAN" || text == "AURORA")
            finishes.add (text);
        if (text == "AURORA")
            aurora = button;
        if (text == "OBSIDIAN")
            obsidian = button;
    }
    CHECK (finishes.joinIntoString (",") == "OBSIDIAN,AURORA");
    REQUIRE (aurora != nullptr);
    REQUIRE (obsidian != nullptr);
    const auto lit = [] (juce::TextButton* b) { return b->getToggleState(); };
    CHECK (lit (obsidian));
    CHECK_FALSE (lit (aurora));

    // Chosen (here without saving the preference), it shows in both windows: the button lit, the printing's ink.
    Finish::set (Finish::Type::aurora, false);
    CHECK_FALSE (Finish::isBlack());
    CHECK (lit (aurora));
    CHECK_FALSE (lit (obsidian));
    std::vector<juce::TextButton*> secondButtons;
    findAll (*second, secondButtons);
    for (auto* button : secondButtons)
        if (button->getButtonText() == "AURORA")
            CHECK (lit (button));
    CHECK (Finish::ink() == ParagonColours::ink);
    for (const auto type : { Finish::Type::aurora, Finish::Type::obsidian })
    {
        Finish::set (type, false);
        first->setSize (1600, 752);
        CHECK (first->createComponentSnapshot (first->getLocalBounds()).isValid());
    }
    CHECK (Finish::isBlack());   // left on Obsidian, the default
    CHECK (Finish::ink().getPerceivedBrightness() > 0.8f);
}

TEST_CASE ("AURORA's pearl is 10% less bright; OBSIDIAN is unchanged", "[ui]")
{
    // The unit's chassis and face (THE DRUM MACHINE's) drawn with and without the dimming: on Aurora every pixel of
    // the pearl comes out at 90% (to within rounding), so pure white is about 230; on Obsidian the two are identical.
    const auto draw = [] (bool dim)
    {
        juce::Image image (juce::Image::ARGB, 400, 300, true, juce::SoftwareImageType());
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        const auto grain = Finish::makeSatinGrain (400, 300, 99);
        PedalBody::paintUnit (g, { 40.0f, 40.0f, 320.0f, 220.0f }, { 70.0f, 70.0f, 260.0f, 150.0f },
                              [&grain] (juce::Graphics& gg) { gg.drawImageAt (grain, 0, 0); }, {}, dim);
        return image;
    };
    const auto brightness = [] (juce::Colour c) { return juce::jmax (c.getRed(), c.getGreen(), c.getBlue()); };

    Finish::set (Finish::Type::aurora, false);
    const auto bright = draw (false), dimmed = draw (true);
    int worst = 0, brightest = 0;
    juce::Path inside;   // the chassis, 2 px in from its edge (outside it is the glow, which isn't pearl)
    inside.addRoundedRectangle (juce::Rectangle<float> (40.0f, 40.0f, 320.0f, 220.0f).reduced (2.0f), PedalBody::Shape{}.chassisRadius - 2.0f);
    for (int y = 40; y < 260; ++y)
        for (int x = 40; x < 360; ++x)
        {
            if (! inside.contains ((float) x + 0.5f, (float) y + 0.5f))
                continue;
            const auto before = bright.getPixelAt (x, y), after = dimmed.getPixelAt (x, y);
            for (const auto [b, a] : { std::pair { before.getRed(), after.getRed() }, { before.getGreen(), after.getGreen() }, { before.getBlue(), after.getBlue() } })
                worst = juce::jmax (worst, std::abs ((int) a - juce::roundToInt ((1.0f - Finish::auroraDimming) * (float) b)));
            brightest = juce::jmax (brightest, (int) brightness (after));
        }
    CHECK (worst <= 1);
    CHECK (Finish::auroraDimming == 0.10f);
    CHECK (brightest <= 231);
    CHECK (brightest >= 215);   // still a white unit, not grey: the brightest pearl near 230
    CHECK (Finish::pearl (juce::Colours::white) == juce::Colour (230, 230, 230));

    Finish::set (Finish::Type::obsidian, false);
    const auto blackPlain = draw (false), blackDimmed = draw (true);
    int blackDifference = 0;
    for (int y = 0; y < 300; ++y)
        for (int x = 0; x < 400; ++x)
            if (blackPlain.getPixelAt (x, y) != blackDimmed.getPixelAt (x, y))
                ++blackDifference;
    CHECK (blackDifference == 0);
    CHECK (Finish::pearl (juce::Colours::white) == juce::Colours::white);
}
