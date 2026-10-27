#include "DockPanels.h"

#include "Parameters.h"
#include "ui/ParagonLookAndFeel.h"

namespace DockStyle
{
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, area.getY(),
                                                 juce::Colour (0xff09070a), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (ParagonColours::text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (title, area, juce::Justification::centredLeft, false);
    }
}

namespace
{
    void styleButton (juce::TextButton& button, bool primary)
    {
        button.setColour (juce::TextButton::buttonColourId, primary ? ParagonColours::accentDim.withAlpha (0.6f) : juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, primary ? ParagonColours::accent : ParagonColours::text);
    }
}

//==============================================================================
SettingsPage::SettingsPage (ParagonX1AudioProcessor& p)
    : processor (p),
      tuneAttachment (p.getParameters(), ParamIDs::tune, tune)
{
    tune.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    tune.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    tune.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    tune.setColour (juce::Slider::thumbColourId, ParagonColours::accent);
    tune.setDoubleClickReturnValue (true, 0.0);
    tune.setTooltip ("Tunes the whole synth up or down, in cents (hundredths of a semitone). "
                     "Not saved with presets: it suits your band, not the sound. Double-click for 0.");

    styleButton (openFolder, false);
    openFolder.onClick = [this]
    {
        processor.getRigLibrary().getFolder().createDirectory();
        processor.getRigLibrary().getFolder().startAsProcess();
    };

    for (auto* component : std::initializer_list<juce::Component*> { &tune, &openFolder })
        addAndMakeVisible (component);
    processor.rigBroadcaster.addChangeListener (this);
}

SettingsPage::~SettingsPage()
{
    processor.rigBroadcaster.removeChangeListener (this);
}

void SettingsPage::paint (juce::Graphics& g)
{
    const auto section = [&g] (juce::Rectangle<int> area, const juce::String& title, const juce::String& body)
    {
        DockStyle::drawPanel (g, area.toFloat());
        auto inner = area.reduced (24, 18);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title, inner.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (body, inner.removeFromTop (60), juce::Justification::topLeft, 4, 1.0f);
    };

    section (tuneArea, "TUNE", "Tunes the whole synth, in cents: 0 is concert pitch (A = 440 Hz). "
                               "It stays as it is when you change presets.");
    section (rigsArea, "PRESETS", "Your presets are files in this folder, so you can back them up or share them:\n"
                                  + processor.getRigLibrary().getFolder().getFullPathName());
    section (aboutArea, "ABOUT",
             juce::String ("Paragon PR7-K v") + JucePlugin_VersionString
                 + ". The PR7 family's synthesizer, inspired by VPS Avenger (our own design and code). "
                   "Free and open source under the GNU AGPLv3.\n"
                   "Built with JUCE (AGPLv3) and the Inter typeface (SIL OFL).");
}

void SettingsPage::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (area.getHeight() / 2 - 6);
    area.removeFromTop (12);
    tuneArea = top.removeFromLeft (top.getWidth() / 2 - 6);
    rigsArea = top.withTrimmedLeft (12);
    aboutArea = area;

    auto tuneControls = tuneArea.reduced (24, 18).withTrimmedTop (90);
    tune.setBounds (tuneControls.removeFromTop (32).withWidth (juce::jmin (420, tuneControls.getWidth())));
    openFolder.setBounds (rigsArea.reduced (24, 18).withTrimmedTop (100).removeFromTop (32).withWidth (220));
}
