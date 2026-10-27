#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

// The ARP page (stage 5a): the 8 arpeggiators, a tab each.
//
//   ARP 1-8 | PATTERN A B C D, CHAIN | -1 +1 | START FROM | the oscillators it plays
//   the pattern: blocks on a grid of steps (across) and transposition (up and down),
//   with each block's VELOCITY, SLIDE and RATCHET in lanes under it
//   its controls: ON, MODE, SPEED, TIMING, OCTAVES, GATE, LENGTH, VELOCITY,
//                 START / END NOTE, HUMANIZE, STRUM, TRIGGER, SHUFFLE

// The pattern editor, as Avenger's:
//   - click an empty place to put a block there (at that transposition);
//     drag it up or down to transpose it, its right edge to lengthen it
//   - right-click a block to delete it
//   - VELOCITY lane: drag a block's bar; SLIDE lane: click to glide into the
//     next block; RATCHET lane: click for more repeats, right-click for fewer
// Blocks on an octave are orange, the rest amber (Avenger's green and yellow).
class ArpPatternEditor final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::Timer
{
public:
    explicit ArpPatternEditor (ParagonX1AudioProcessor&);
    void setTarget (int arp, int pattern);
    int getArp() const noexcept { return arp; }
    int getPattern() const noexcept { return pattern; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int lowestTranspose = -24, highestTranspose = 24;

    // Where things are (for the tests), and the pattern now / a change to it.
    int getLength() const;
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> cell (int step, int transpose) const;
    juce::Rectangle<float> velocityArea (int step) const;
    juce::Rectangle<float> slideArea (int step) const;
    juce::Rectangle<float> ratchetArea (int step) const;
    synth::arp::Pattern getShown() const;
    void setShown (const synth::arp::Pattern&, bool finished);

private:
    void timerCallback() override;
    int stepAt (float x) const;
    int transposeAt (float y) const;

    ParagonX1AudioProcessor& processor;
    int arp = 0, pattern = 0;
    synth::arp::Pattern shown;
    std::vector<float> lastSettings;
    // A drag: which block, and what it moves (0 transposition, 1 length, 2 velocity).
    int dragStep = -1, dragWhat = 0;
};

class ArpPage final : public juce::Component,
                      private juce::Timer
{
public:
    explicit ArpPage (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    void selectArp (int index);
    void selectPattern (int index);   // also the arp's PATTERN (the one it plays)
    int getSelectedArp() const noexcept { return selected; }
    ArpPatternEditor& getEditor() noexcept { return editor; }
    void shift (int semitones);       // every block of the pattern shown up or down
    void startFrom (int choice);
    void route (int oscillator, bool toThisArp);   // the oscillator's notes from this arp, or straight from the keys
    bool isRouted (int oscillator) const;

private:
    void timerCallback() override;
    std::vector<ParamControl*> controls();

    ParagonX1AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    struct ArpTab final : public juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool lit = false;
    };
    std::vector<std::unique_ptr<ArpTab>> arpTabs;
    std::vector<std::unique_ptr<juce::TextButton>> patternButtons, oscillatorButtons;
    juce::TextButton down { "-1" }, up { "+1" };
    juce::ComboBox startFromBox;
    juce::Label patternLabel, startLabel, oscillatorLabel, hint;
    ArpPatternEditor editor;
    ParamControl on, mode, speed, timing, octaves, gate, length, velocity, lowKey, highKey, humanize, strum, trigger, shuffle, chain;
    int selected = 0;
};
