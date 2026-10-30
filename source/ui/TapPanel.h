// TAP TO FIND (PR7-D 0.6.0): the panel the rhythm is tapped in. Our own design, inspired by the tap-a-rhythm search of
// commercial drum software: six pads (KICK, SNARE, HI-HAT, RIDE, TOMS, CRASH: a click plays the drum and taps it; notes
// from the DAW or a controller tap too), a two-bar loop with a metronome while the panel is open (one drum a pass:
// everything tapped stays), TEMPO and QUANTIZE, CLEAR (all, or a row's x), and FIND.
#pragma once

#include "PluginProcessor.h"

class TapPanel final : public juce::Component, private juce::Timer
{
public:
    explicit TapPanel (ParagonX1AudioProcessor&);
    ~TapPanel() override;

    // Opens it: the loop starts from its first beat at `bpm`, two bars of `beatsPerBar` (the taps from before kept).
    void open (double bpm, double beatsPerBar);
    void close();

    std::function<void (const grooves::Rhythm&, double grid)> onFind;   // FIND: the taps, on QUANTIZE's grid
    std::function<void()> onClose;

    // The taps so far (beats in the loop), for tests and FIND.
    grooves::Rhythm getRhythm() const;
    double getGrid() const;
    void addTap (int lane, double beat);   // as the processor records one
    void clearLane (int lane);              // -1: all
    juce::TextButton& getPad (int lane) noexcept { return pads[(size_t) lane]; }
    juce::TextButton& getFindButton() noexcept { return find; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Rectangle<int> laneArea (int lane) const;   // a row of the loop's picture

    ParagonX1AudioProcessor& processor;
    std::array<juce::TextButton, grooves::numLanes> pads, clears;
    juce::Slider tempo { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox quantize;
    juce::Label tempoLabel { {}, "TEMPO" }, quantizeLabel { {}, "QUANTIZE" }, hint;
    juce::TextButton clearAll { "CLEAR" }, cancel { "CLOSE" }, find { "FIND" };
    std::vector<ParagonX1AudioProcessor::Tap> taps;
    double loopBeats = 8.0, beatsPerBar = 4.0;
    juce::Rectangle<int> loopArea;
};
