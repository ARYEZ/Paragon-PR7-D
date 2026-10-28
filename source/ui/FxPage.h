#pragma once

#include "PluginProcessor.h"
#include "ui/CompPage.h"
#include "ui/DominionPage.h"
#include "ui/EqPage.h"
#include "ui/LimiterPage.h"
#include "ui/RackUnit.h"

// The FX page (stage 7a): THE RACK, PR7-E 0.18.1's four rack units in the signal chain's order (ECHELON, DOMINION,
// CONSTRICTOR, the LIMITER), each with its POWER switch, meter, screen, keys and knobs. Its EDIT (or its name)
// opens that effect's big page here, in the rack's place; the page's close button comes back to the rack.
class FxPage final : public juce::Component
{
public:
    explicit FxPage (ParagonX1AudioProcessor&);

    enum class View { rack, eq, dominion, comp, limiter };
    void show (View);
    View getView() const noexcept { return view; }

    RackUnit& getUnit (int index) noexcept { return *units[(size_t) index]; }
    EqPage& getEqPage() noexcept { return eqPage; }
    DominionPage& getDominionPage() noexcept { return dominionPage; }
    CompPage& getCompPage() noexcept { return compPage; }
    LimiterPage& getLimiterPage() noexcept { return limiterPage; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    View view = View::rack;
    std::array<std::unique_ptr<RackUnit>, 4> units;
    EqPage eqPage;
    DominionPage dominionPage;
    CompPage compPage;
    LimiterPage limiterPage;
};
