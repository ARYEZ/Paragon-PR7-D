#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

// MOD, MOD 2 and LO-FI's pages (stage 7c): each unit's EDIT on the FX page's MOD / LO-FI view.

// What MOD's (or MOD 2's) LFO does from now over the next two cycles, scrolling as it plays: the Vortex's clock,
// AEON's delay, Hadron's stages, Razor's volume (WAVE's shape), the pan... (Modulation::sweepShape, the shapes the
// effect plays), at DEPTH where the type has one. Two lines where two things move in turn (the Dimension's two
// voices, the harmonic tremolo's lows and highs, auto-pan's left and right).
class SweepDisplay final : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::Timer
{
public:
    SweepDisplay (ParagonX1AudioProcessor&, int which);
    void paint (juce::Graphics&) override;

    // What's drawn moving, for the type ("DELAY", "VOLUME"...), and how far the LFO swings it (0 to 1). For tests too.
    static juce::String movingName (effects::Modulation::Type);
    static float swing (const effects::Modulation::Settings&) noexcept;

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    int which;
};

// LO-FI's band: what gets through, 20 Hz to 20 kHz (a speaker's resonant bump marked), or for the bitcrusher a sine
// as it comes out (the levels its bits leave, each held for its samples).
class LoFiDisplay final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    explicit LoFiDisplay (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    ParagonX1AudioProcessor& processor;
    int shownType = -1;
    float shownAmount = -1.0f;
};

// MOD's, MOD 2's or LO-FI's page: ON, TYPE and BACK TO RACK along the top, its display, and its controls (for MOD,
// only those the type uses, as on PR7-G: RATE or SYNC's note, AEON's MANUAL and RES, Hadron's CIRCUIT...).
class ModFxPage final : public juce::Component,
                        private juce::Timer
{
public:
    enum class Kind { mod, mod2, loFi };
    ModFxPage (ParagonX1AudioProcessor&, Kind);
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    Kind getKind() const noexcept { return kind; }
    juce::Component& getDisplay() noexcept { return *display; }
    std::vector<ParamControl*> getControls();   // all of them, shown or not
    std::vector<ParamControl*> getShownControls();

private:
    void timerCallback() override;
    bool shows (const ParamControl&) const;

    ParagonX1AudioProcessor& processor;
    Kind kind;
    std::unique_ptr<juce::Component> display;
    std::vector<std::unique_ptr<ParamControl>> top, controls;
    juce::TextButton back { "BACK TO RACK" };
    juce::String layoutKey;   // what the controls were laid out for (the type, SYNC)
};
