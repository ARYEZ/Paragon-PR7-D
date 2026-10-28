#pragma once

#include "PluginProcessor.h"
#include "ui/CompPage.h"
#include "ui/DominionPage.h"
#include "ui/EqPage.h"
#include "ui/LimiterPage.h"
#include "ui/RackUnit.h"
#include "ui/SendPages.h"

// The FX page: two views, SENDS and MASTER (tabs at the top).
//   SENDS (stage 7b): PR7-G's DELAY and REVERB as two rack units, and the send matrix (each oscillator's sends,
//   DELAY > REVERB). Their returns join the synth before the MASTER rack.
//   MASTER (stage 7a): THE RACK, PR7-E 0.18.1's four rack units in the signal chain's order (ECHELON, DOMINION,
//   CONSTRICTOR, the LIMITER), each with its POWER switch, meter, screen, keys and knobs.
// A unit's EDIT (or its name) opens that effect's big page here; the page's BACK TO RACK returns to its view.
class FxPage final : public juce::Component
{
public:
    explicit FxPage (ParagonX1AudioProcessor&);

    enum class View { rack, eq, dominion, comp, limiter,   // rack: the MASTER view
                      sends, delay, reverb };              // stage 7b
    void show (View);
    View getView() const noexcept { return view; }

    RackUnit& getUnit (int index) noexcept { return *units[(size_t) index]; }
    RackUnit& getSendUnit (int index) noexcept { return *sendUnits[(size_t) index]; }
    SendPage& getDelayPage() noexcept { return delayPage; }
    SendPage& getReverbPage() noexcept { return reverbPage; }
    SendMatrix& getSendMatrix() noexcept { return matrix; }
    juce::TextButton& getSendsTab() noexcept { return sendsTab; }
    juce::TextButton& getMasterTab() noexcept { return masterTab; }
    EqPage& getEqPage() noexcept { return eqPage; }
    DominionPage& getDominionPage() noexcept { return dominionPage; }
    CompPage& getCompPage() noexcept { return compPage; }
    LimiterPage& getLimiterPage() noexcept { return limiterPage; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    View view = View::rack;
    std::array<std::unique_ptr<RackUnit>, 4> units;
    std::array<std::unique_ptr<RackUnit>, 2> sendUnits;
    juce::TextButton sendsTab { "SENDS" }, masterTab { "MASTER" };
    SendMatrix matrix;
    SendPage delayPage, reverbPage;
    EqPage eqPage;
    DominionPage dominionPage;
    CompPage compPage;
    LimiterPage limiterPage;
};
