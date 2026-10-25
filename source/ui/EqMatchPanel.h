#pragma once

#include "ui/EqDisplay.h"

// EQ Match's panel on the EQ page (MATCH in the top bar), in place of the
// band controls. See dsp/eq/EqMatch.h for how it listens and fits.
//
//   1. YOUR TRACK: RECORD (on when the panel opens), then play the part to
//      match. CLEAR starts again.
//   2. REFERENCE: RECORD INPUT (play another part of the song), LOAD FILE
//      (a song), or SAVED (a reference saved before; and saving either one).
//   3. The display shows the curve that would match (a thick line) and the
//      bands proposed (dashed). DETAIL: a few broad bands, or more, finer
//      ones; AMOUNT: how much of the difference to make up.
//   4. ADD BANDS adds them as ordinary bands (one undo takes them back).
class EqMatchPanel final : public juce::Component,
                           private juce::Timer
{
public:
    EqMatchPanel (ParagonX1AudioProcessor&, EqDisplay&);
    ~EqMatchPanel() override;

    std::function<void()> onClose;   // after ADD BANDS, or CLOSE

    // What it would add now, for tests.
    const std::vector<eq::Band>& getProposal() const noexcept { return proposal; }
    void refresh() { recompute(); updateButtons(); }   // what the timer does
    void addBands();
    juce::Slider& getDetail() noexcept { return detail; }
    juce::Slider& getAmount() noexcept { return amount; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void recompute();
    void updateButtons();
    void showSavedMenu();
    void askForName (const juce::String& title, std::function<void (juce::String)> then);
    int freeSlots() const;

    ParagonX1AudioProcessor& processor;
    EqDisplay& display;
    eq::match::Session& session;

    juce::TextButton trackRecord { "RECORD" }, trackClear { "CLEAR" }, referenceRecord { "RECORD INPUT" },
                     loadFile { "LOAD FILE..." }, saved { "SAVED..." }, add { "ADD BANDS" }, close { "CLOSE" };
    juce::Slider detail { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 amount { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label detailLabel { {}, "DETAIL" }, amountLabel { {}, "AMOUNT" };
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<eq::Band> proposal;
    std::vector<double> existing;   // the EQ's own curve when it was last fitted
    int seenVersion = -1;
    double seenDetail = -1.0, seenAmount = -1.0;
    double lastFitMs = 0.0;
    juce::Rectangle<int> titleArea, trackArea, referenceArea, statusArea;
};
