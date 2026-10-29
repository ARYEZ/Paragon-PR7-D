#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The pages' pictures' shared look (PR7-K's synth displays').

// A dark inset screen with a faint grid, shared by the displays.
namespace DisplayStyle
{
    void drawScreen (juce::Graphics&, juce::Rectangle<float> area);
    // A curve in the plugin's red-pink-orange, with a soft glow and a faint fill down to `baseline`.
    void drawCurve (juce::Graphics&, const juce::Path& curve, juce::Rectangle<float> area, float baseline, bool lit = true);
}
