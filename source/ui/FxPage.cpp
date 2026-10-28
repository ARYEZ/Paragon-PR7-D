#include "FxPage.h"

#include "ui/DockPanels.h"
#include "ui/RackSpecs.h"

FxPage::FxPage (ParagonX1AudioProcessor& processor)
    : eqPage (processor), dominionPage (processor), compPage (processor), limiterPage (processor)
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
    if (view == View::rack)
        DockStyle::drawPanel (g, getLocalBounds().toFloat());
}

void FxPage::resized()
{
    // The four units, one to a row as in a rack, each as wide as the page allows at its drawn proportions.
    auto area = getLocalBounds().reduced (12, 10);
    constexpr int gap = 8;
    const auto byHeight = (float) (area.getHeight() - 3 * gap) / 4.0f;
    const auto width = juce::jmin ((float) area.getWidth(), byHeight * RackUnit::referenceWidth / RackUnit::referenceHeight);
    const auto height = width * RackUnit::referenceHeight / RackUnit::referenceWidth;
    const auto top = (float) area.getY() + ((float) area.getHeight() - 4.0f * height - 3.0f * gap) * 0.5f;
    for (size_t i = 0; i < units.size(); ++i)
        units[i]->setBounds (juce::Rectangle<float> ((float) area.getCentreX() - width * 0.5f, top + (float) i * (height + gap), width, height).toNearestInt());
    for (auto* page : std::initializer_list<juce::Component*> { &eqPage, &dominionPage, &compPage, &limiterPage })
        page->setBounds (getLocalBounds());
}
