#pragma once

#include "PluginProcessor.h"
#include "dsp/synth/Drums.h"

// THE ARTICULATIONS (PR7-D 0.17.0), over the KIT VIEW's top left: the chosen drum's piece and its articulations, as
// in drum software where each drum is one instrument played several ways (inspired by its articulation list; our own
// look). From the top:
//   - the piece's name;
//   - ALL: every articulation chosen together (the DRUM page's settings then go to all of them);
//   - a row per articulation: its ON (off: its notes play its stand-in, named beside it), its name (click: choose it
//     and hear it), the note it's on in the map, and its LEVEL (drag; double-click: 0 dB). A row lights as it plays,
//     so a stand-in playing for one that's off shows where the sound came from;
//   - the hi-hat: THE PEDAL (CC 4): the openness the pedal is at, live, and where each step begins (drag the marks);
//   - the cymbals: MUTE TAIL, how they're grabbed to stop them ringing (OFF, NOTE OFF, AFTERTOUCH), for all of it.
class ArticulationPanel final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    explicit ArticulationPanel (ParagonX1AudioProcessor&);

    // The piece shown: the one `slot` belongs to (DrumPage's chosen drum), and which of its slots are chosen.
    void showSlot (int slot, const std::vector<int>& chosen);
    int getPiece() const noexcept { return piece; }
    // Choosing: one articulation (and hearing it), or ALL. DrumPage does the choosing.
    std::function<void (int slot)> onChoose;
    std::function<void (const std::vector<int>& slots)> onChooseAll;

    // Each row's light and the pedal's meter, from the kit (DrumPage's timer).
    void updateLights();
    // The height it needs for its piece.
    int wantedHeight() const;

    // For tests: as a click on a row's ON, on its name, on ALL; a MUTE TAIL choice; a pedal step set.
    void toggleOn (int slot);
    void clickName (int slot);
    void clickAll();
    void setMuteTail (synth::drums::MuteTail);
    void setPedalStep (int step, int value);
    juce::StringArray describeRows() const;   // "Closed D2 ON", "Open 1 off -> Open 2", ...

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct Row { int slot = -1; juce::Rectangle<int> area, power, name, level; };
    std::vector<Row> layoutRows() const;
    juce::Rectangle<int> allArea() const;
    juce::Rectangle<int> extraArea() const;          // the pedal or MUTE TAIL, under the rows
    juce::Rectangle<int> pedalBar() const;
    std::array<juce::Rectangle<int>, 3> tailChoices() const;
    bool isHat() const noexcept;
    bool isCymbal() const noexcept;
    float paramValue (const juce::String& id) const;
    void setParam (const juce::String& id, float value);   // one undo step (a gesture round it)
    int pedalStep (int i) const;

    ParagonX1AudioProcessor& processor;
    int piece = 0, slotShown = 0;
    std::vector<int> chosen;
    std::array<float, 6> lights {};
    int pedal = 127;
    // Dragging: a row's LEVEL, or a pedal step's mark.
    int dragLevelSlot = -1, dragStep = -1;
    juce::RangedAudioParameter* dragging = nullptr;
};
