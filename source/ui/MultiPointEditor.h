#pragma once

#include "PluginProcessor.h"

// The multipoint editor (PR7-K's, from its stage 4b): a drum's envelope across its hit (THE SAMPLE EDITOR), x 0 to 1.
//   - drag a point to move it (between its neighbours; the first stays at the start, the last at the end)
//   - drag a line up or down to curve it
//   - double-click empty space to add a point; double-click a point to delete it
//   - right-click a point to delete it; right-click empty space for ready-made shapes
class MultiPointEditor final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::Timer
{
public:
    explicit MultiPointEditor (ParagonX1AudioProcessor&);

    // drumEnvelope: a drum slot's, across its hit (the only kind left of PR7-K's five).
    enum class Kind { drumEnvelope };
    void setTarget (Kind, int index);
    Kind getKind() const noexcept { return kind; }
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

    // The shape now, and a change to it (an undo step when `finished`).
    ModShapeData getShape() const;
    void setShape (const ModShapeData&, bool finished);

private:
    void timerCallback() override;
    float pixelX (float x) const;
    float pixelY (float y) const;

    ParagonX1AudioProcessor& processor;
    Kind kind = Kind::drumEnvelope;
    int index = 0;
    ModShapeData shown;
    int dragging = -1, curving = -1;
    float dragStartCurve = 0.0f, dragStartY = 0.0f;
};
