#pragma once

#include "dsp/synth/VelocityCurve.h"

#include <juce_gui_basics/juce_gui_basics.h>

// A VELOCITY CURVE drawn to edit (PR7-D 0.18.0): velocity in along the bottom, out up the side, both 0 to 127.
//   - drag a point; the first and last are the limits (in and out: drag them anywhere between their neighbours);
//   - double-click the line to add a point, right-click a point between to remove it;
//   - while dragging, "in -> out" beside the point.
// The curve comes from `getCurve` when painted; each change goes to `onChange` (while dragging: `final` false; on
// release, or an add / remove: true, one undo step).
class VelocityCurveEditor final : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    VelocityCurveEditor();

    std::function<synth::drums::VelocityCurve()> getCurve;
    std::function<void (const synth::drums::VelocityCurve&, bool final)> onChange;
    void setLabel (const juce::String& text) { label = text; repaint(); }
    // The last velocity that came through (a dot on the line; -1: none), for the incoming curve.
    void showVelocity (int velocityIn);

    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (juce::Point<float>) const;
    juce::Point<float> fromScreen (juce::Point<float>) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    int pointAt (juce::Point<float> at) const;
    synth::drums::VelocityCurve curve() const { return getCurve ? getCurve() : synth::drums::VelocityCurve {}; }

    juce::String label;
    int dragging = -1;
    synth::drums::VelocityCurve dragged;
    int lastIn = -1;
};
