#include "SynthDisplays.h"

#include "ui/ParagonLookAndFeel.h"

namespace DisplayStyle
{
    void drawScreen (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (juce::Colour (0xff060407));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);
    }

    void drawCurve (juce::Graphics& g, const juce::Path& curve, juce::Rectangle<float> area, float baseline, bool lit)
    {
        const auto gradient = ParagonLookAndFeel::menuHighlight (area);

        // A faint fill between the curve and the baseline.
        juce::Path fill (curve);
        fill.lineTo (curve.getCurrentPosition().x, baseline);
        fill.lineTo (area.getX(), baseline);
        fill.closeSubPath();
        auto fillGradient = gradient;
        fillGradient.multiplyOpacity (lit ? 0.18f : 0.06f);
        g.setGradientFill (fillGradient);
        g.fillPath (fill);

        auto glow = gradient;
        glow.multiplyOpacity (lit ? 0.25f : 0.08f);
        g.setGradientFill (glow);
        g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto line = gradient;
        line.multiplyOpacity (lit ? 1.0f : 0.35f);
        g.setGradientFill (line);
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
