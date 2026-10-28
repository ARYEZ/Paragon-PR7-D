#include "FxPage.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/RackSpecs.h"

FxPage::FxPage (ParagonX1AudioProcessor& processor)
    : matrix (processor), delayPage (processor, SendPage::Kind::delay), reverbPage (processor, SendPage::Kind::reverb),
      eqPage (processor), dominionPage (processor), compPage (processor), limiterPage (processor)
{
    RackActions actions;
    actions.openPage = [this] (int unit)
    {
        constexpr View views[] = { View::eq, View::dominion, View::comp, View::limiter };
        show (views[juce::jlimit (0, 3, unit)]);
    };
    actions.openMatch = [this]
    {
        show (View::eq);
        eqPage.showMatch (true);
    };
    actions.analyserOn = [this] { return eqPage.isAnalyserOn(); };
    actions.setAnalyser = [this] (bool on) { eqPage.setAnalyser (on); };
    auto specs = makeRackSpecs (processor, std::move (actions));
    for (size_t i = 0; i < units.size(); ++i)
    {
        units[i] = std::make_unique<RackUnit> (processor.getParameters(), std::move (specs[i]));
        addAndMakeVisible (*units[i]);
    }
    for (auto* page : std::initializer_list<juce::Component*> { &eqPage, &dominionPage, &compPage, &limiterPage })
        addChildComponent (page);

    // THE SENDS (stage 7b).
    auto sendSpecs = makeSendSpecs (processor, [this] (int send) { show (send == 0 ? View::delay : View::reverb); });
    for (size_t i = 0; i < sendUnits.size(); ++i)
    {
        sendUnits[i] = std::make_unique<RackUnit> (processor.getParameters(), std::move (sendSpecs[i]));
        addChildComponent (*sendUnits[i]);
    }
    addChildComponent (matrix);
    addChildComponent (delayPage);
    addChildComponent (reverbPage);
    delayPage.onClose = [this] { show (View::sends); };
    reverbPage.onClose = [this] { show (View::sends); };
    for (auto* tab : { &sendsTab, &masterTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (971);
        tab->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        tab->setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
        tab->setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        tab->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        addAndMakeVisible (*tab);
    }
    sendsTab.setTooltip ("The SENDS: DELAY and REVERB, fed by each oscillator's send; they come back in before the MASTER rack");
    masterTab.setTooltip ("The MASTER rack: ECHELON, DOMINION, CONSTRICTOR and the LIMITER, on everything");
    sendsTab.onClick = [this] { show (View::sends); };
    masterTab.onClick = [this] { show (View::rack); };
    show (View::rack);
    eqPage.onClose = [this] { show (View::rack); };
    dominionPage.onClose = [this] { show (View::rack); };
    compPage.onClose = [this] { show (View::rack); };
    limiterPage.onClose = [this] { show (View::rack); };
}

void FxPage::show (View newView)
{
    view = newView;
    for (auto& unit : units)
        unit->setVisible (view == View::rack);
    for (auto& unit : sendUnits)
        unit->setVisible (view == View::sends);
    matrix.setVisible (view == View::sends);
    delayPage.setVisible (view == View::delay);
    reverbPage.setVisible (view == View::reverb);
    const auto onRackView = view == View::rack || view == View::sends;
    sendsTab.setVisible (onRackView);
    masterTab.setVisible (onRackView);
    (view == View::sends ? sendsTab : masterTab).setToggleState (true, juce::dontSendNotification);
    eqPage.setVisible (view == View::eq);
    dominionPage.setVisible (view == View::dominion);
    compPage.setVisible (view == View::comp);
    limiterPage.setVisible (view == View::limiter);
    if (view == View::eq)
        eqPage.getDisplay().grabKeyboardFocus();
    else if (view == View::dominion)
        dominionPage.getDisplay().grabKeyboardFocus();
    else if (view == View::comp)
        compPage.getDisplay().grabKeyboardFocus();
}

void FxPage::paint (juce::Graphics& g)
{
    if (view == View::rack || view == View::sends)
        DockStyle::drawPanel (g, getLocalBounds().toFloat());
}

void FxPage::resized()
{
    // The tabs, then the four units, one to a row as in a rack, each as wide as the page allows at its drawn proportions.
    auto area = getLocalBounds().reduced (12, 10);
    auto tabs = area.removeFromTop (26);
    sendsTab.setBounds (tabs.removeFromLeft (110));
    tabs.removeFromLeft (4);
    masterTab.setBounds (tabs.removeFromLeft (110));
    area.removeFromTop (8);
    const auto rackArea = area;
    constexpr int gap = 8;
    const auto byHeight = (float) (area.getHeight() - 3 * gap) / 4.0f;
    const auto width = juce::jmin ((float) area.getWidth(), byHeight * RackUnit::referenceWidth / RackUnit::referenceHeight);
    const auto height = width * RackUnit::referenceHeight / RackUnit::referenceWidth;
    const auto top = (float) area.getY() + ((float) area.getHeight() - 4.0f * height - 3.0f * gap) * 0.5f;
    for (size_t i = 0; i < units.size(); ++i)
        units[i]->setBounds (juce::Rectangle<float> ((float) area.getCentreX() - width * 0.5f, top + (float) i * (height + gap), width, height).toNearestInt());
    for (auto* page : std::initializer_list<juce::Component*> { &eqPage, &dominionPage, &compPage, &limiterPage, &delayPage, &reverbPage })
        page->setBounds (getLocalBounds());

    // SENDS: the two units at the master units' size, the matrix under them.
    auto sendsArea = rackArea;
    for (size_t i = 0; i < sendUnits.size(); ++i)
    {
        sendUnits[i]->setBounds (juce::Rectangle<float> ((float) sendsArea.getCentreX() - width * 0.5f, (float) sendsArea.getY(), width, height).toNearestInt());
        sendsArea.removeFromTop (juce::roundToInt (height) + gap);
    }
    matrix.setBounds (sendsArea.withSizeKeepingCentre (juce::roundToInt (width), sendsArea.getHeight()));
}
