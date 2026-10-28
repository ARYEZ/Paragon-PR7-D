#include "PresetButton.h"

#include "ParagonLookAndFeel.h"

PresetButton::PresetButton (Hooks h) : hooks (std::move (h))
{
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    setTooltip (hooks.stompName + "'s own presets (apart from the synth's): they change only " + hooks.stompName
                + "'s settings, not whether it's on. Save your own here too");
    onClick = [this] { showMenu(); };
    refresh();
    startTimerHz (5);
}

void PresetButton::refresh()
{
    const auto name = hooks.currentName() + (hooks.modified() ? " *" : "");
    if (getButtonText() != name)
        setButtonText (name);
}

void PresetButton::showMenu()
{
    // The factory presets by category, then yours, then saving and deleting.
    // A category "Group/Name" (the genres) is a submenu "Name" under a GROUP
    // header, so a long list of them doesn't make the menu taller than the screen.
    const auto& factory = hooks.factory();
    const auto yours = hooks.user();
    const auto current = hooks.currentName();
    juce::PopupMenu menu;
    juce::String category = "-", group;
    juce::PopupMenu submenu;
    bool submenuTicked = false;
    const auto endSubmenu = [&]
    {
        if (submenu.getNumItems() > 0)
            menu.addSubMenu (category.fromFirstOccurrenceOf ("/", false, false), submenu, true, juce::Image(), submenuTicked);
        submenu = {};
        submenuTicked = false;
    };
    for (int i = 0; i < (int) factory.size(); ++i)
    {
        const auto& preset = factory[(size_t) i];
        if (preset.category != category)
        {
            endSubmenu();
            category = preset.category;
            const auto newGroup = category.upToFirstOccurrenceOf ("/", false, false);
            if (category.contains ("/") ? newGroup != group : category.isNotEmpty())
                menu.addSectionHeader (newGroup.toUpperCase());
            group = category.contains ("/") ? newGroup : juce::String();
        }
        if (category.contains ("/"))
        {
            submenu.addItem (100 + i, preset.name, true, preset.name == current);
            submenuTicked = submenuTicked || preset.name == current;
        }
        else
            menu.addItem (100 + i, preset.name, true, preset.name == current);
    }
    endSubmenu();
    menu.addSectionHeader ("YOUR PRESETS");
    if (yours.empty())
        menu.addItem (99, "(none yet: SAVE AS... adds one)", false);
    for (int i = 0; i < (int) yours.size(); ++i)
        menu.addItem (1000 + i, yours[(size_t) i].name, true, yours[(size_t) i].name == current);
    menu.addSeparator();
    menu.addItem (1, "Save as...");
    const auto mine = std::find_if (yours.begin(), yours.end(), [&] (const StompPreset& p) { return p.name == current; });
    menu.addItem (2, "Delete \"" + current + "\"", mine != yours.end());
    menu.addItem (3, "Show the presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, factory, yours] (int chosen)
    {
        if (chosen >= 1000 && chosen - 1000 < (int) yours.size())
            hooks.load (yours[(size_t) (chosen - 1000)]);
        else if (chosen >= 100 && chosen - 100 < (int) factory.size())
            hooks.load (factory[(size_t) (chosen - 100)]);
        else if (chosen == 1)
            askForName();
        else if (chosen == 2)
        {
            const auto name = hooks.currentName();
            for (const auto& p : yours)
                if (p.name == name)
                    hooks.remove (p);
        }
        else if (chosen == 3)
        {
            hooks.folder().createDirectory();
            hooks.folder().revealToUser();
        }
        refresh();
    });
}

void PresetButton::askForName()
{
    auto* window = new juce::AlertWindow ("Save a " + hooks.stompName + " preset", "A name for it (kept with your " + hooks.stompName + " presets):",
                                          juce::MessageBoxIconType::NoIcon, this);
    const auto current = hooks.currentName();
    window->addTextEditor ("name", current == "Default" || current == "Untitled" ? juce::String ("My Preset") : current);
    window->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result != 1)
            return;
        if (const auto name = window->getTextEditorContents ("name").trim(); name.isNotEmpty())
            hooks.saveAs (name);
        refresh();
    }), true);
}
