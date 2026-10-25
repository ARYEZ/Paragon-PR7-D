#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"

// The panels along the bottom of the PROCESSOR page, as in the mockup:
//
//   RIG BROWSER | SELECTED RIG | (the selected block's knobs) | EXPRESSION ASSIGN
//
// plus the SETTINGS page.

// Dark rounded panel with a small grey title, shared by the dock panels.
namespace DockStyle
{
    void drawPanel (juce::Graphics&, juce::Rectangle<float> area);
    void drawTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
}

//==============================================================================
// RIG BROWSER: shortcuts into the PRESETS page. Clicking a filter or a bank
// opens the full browser showing just those rigs.
class RigNavigator final : public juce::Component
{
public:
    RigNavigator();

    // `filter`: 0 all, 1 favourites, 2 mine, 3 factory. `bank`: -1 all, 0-3 = A-D.
    std::function<void (int filter, int bank)> onShow;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Row
    {
        juce::String text, letter;
        Icons::Type icon;
        int filter, bank;
        juce::Rectangle<int> area;
    };
    std::vector<Row> rows;
    void layoutRows();
    int rowAt (juce::Point<int>) const;

    int hovered = -1, selected = 0;
    juce::Rectangle<int> banksTitle;
};

//==============================================================================
// SELECTED RIG: the loaded rig's name and favourite star, where it's filed
// (bank and number, editable for your own rigs), and SAVE / SAVE AS.
class SelectedRigPanel final : public juce::Component
{
public:
    explicit SelectedRigPanel (ParagonX1AudioProcessor&);

    std::function<void()> onSave, onSaveAs;

    // Re-reads the current rig from the processor.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void refile();

    ParagonX1AudioProcessor& processor;
    juce::ComboBox bankBox, numberBox;
    juce::TextButton save { "SAVE" }, saveAs { "SAVE AS..." };
    juce::Rectangle<int> nameBox, starArea, bankLabel, numberLabel;
    juce::String name;
    bool hasEntry = false, favourite = false, canRefile = false, refreshing = false;
};

//==============================================================================
// SETTINGS: the tuner's reference pitch, the expression pedal's MIDI
// controller, the unit's finish, where rigs are kept, and what the plugin
// is built from.
class SettingsPage final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit SettingsPage (ParagonX1AudioProcessor&);
    ~SettingsPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    ParagonX1AudioProcessor& processor;
    juce::Slider reference { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox ccBox;
    juce::TextButton openFolder { "OPEN RIGS FOLDER" };
    juce::TextButton pearlWhite { "PEARL WHITE" }, pearlWhiteNeon { "WHITE + NEON" }, pearlBlack { "PEARL BLACK" };
    juce::Rectangle<int> tunerArea, expressionArea, finishArea, rigsArea, aboutArea;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { showFinish(); }
    void showFinish();
};
