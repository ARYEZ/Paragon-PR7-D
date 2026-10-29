#pragma once

#include "PluginProcessor.h"

// The multipoint editor (stage 4b): a modulation envelope's or a drawn LFO
// shape's points, edited as Avenger's are.
//   - drag a point to move it (between its neighbours; the first stays at
//     the start, and an LFO shape's last at the end of the cycle)
//   - drag a line up or down to curve it
//   - double-click empty space to add a point; double-click a point to delete it
//   - right-click a point: delete it, or put the SUSTAIN or LOOP points there
// An envelope's time runs left to right in seconds (or beats with SYNC on);
// SUSTAIN is marked S and the loop shaded between L1 and L2.
class MultiPointEditor final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::Timer
{
public:
    explicit MultiPointEditor (ParagonX1AudioProcessor&);

    // travel: an oscillator's TRAVEL envelope (stage 4e); drumEnvelope: a drum slot's, across its hit (6c)
    enum class Kind { envelope, lfoShape, pitchEnvelope, travel, drumEnvelope };
    void setTarget (Kind, int index);
    Kind getKind() const noexcept { return kind; }
    // Across one cycle (x 0 to 1): a drawn LFO shape or a TRAVEL envelope.
    bool isCycle() const noexcept { return kind == Kind::lfoShape || kind == Kind::travel || kind == Kind::drumEnvelope; }
    // The drum envelope's shapes (its right-click menu, for the tests too): 1 flat, 2 back to front, 3 upside down,
    // 4 fade in, 5 fade out, 6 gate (half, then nothing), 7 swell.
    void applyDrumShape (int choice);
    juce::String caption;   // written small in the top corner (the drum envelope's name)
    int getIndex() const noexcept { return index; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Where things are drawn, and back (for the tests).
    juce::Rectangle<float> getPlotArea() const;
    juce::Point<float> pointPosition (int point) const;
    float xAt (float pixelX) const;
    float yAt (float pixelY) const;
    int pointNear (juce::Point<float>) const;   // -1: none
    float viewLength() const;                   // the x shown across the plot

    // The shape now, and a change to it (an undo step when `finished`).
    ModShapeData getShape() const;
    void setShape (const ModShapeData&, bool finished);

private:
    void timerCallback() override;
    float pixelX (float x) const;
    float pixelY (float y) const;

    ParagonX1AudioProcessor& processor;
    Kind kind = Kind::envelope;
    int index = 0;
    ModShapeData shown;
    int dragging = -1, curving = -1;
    float dragStartCurve = 0.0f, dragStartY = 0.0f;
    float lockedLength = 0.0f;   // the view's length while dragging (so it doesn't move under the mouse)
};
