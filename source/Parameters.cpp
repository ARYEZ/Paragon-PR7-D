#include "Parameters.h"

#include "dsp/effects/GraphicEq.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const juce::String& name, Range range,
                                                          float defaultValue, int versionHint, const ToText& toText)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, versionHint }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText));
    }

    // Version hint 1: tells hosts which plugin version introduced the parameter.
    std::unique_ptr<juce::AudioParameterFloat> makeGainParameter (const char* id, const juce::String& name)
    {
        return makeFloat (id, name, { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, 0.0f, 1, asDb);
    }

    std::unique_ptr<juce::AudioParameterBool> makeSwitch (const char* id, const juce::String& name, int versionHint,
                                                          bool defaultValue = true)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, versionHint }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const char* id, const juce::String& name, int versionHint,
                                                            const juce::StringArray& choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, versionHint }, name, choices, defaultIndex);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    // Version hints are the plugin version (in PR7-G's numbering, which
    // PR7-B and PR7-E carried on) that introduced each parameter.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeGainParameter (ParamIDs::inputGain, "Input"));
    layout.add (makeGainParameter (ParamIDs::outputGain, "Output"));

    layout.add (makeFloat (ParamIDs::presence, "Presence", { -10.0f, 10.0f, 0.1f }, 0.0f, 3, asDb));

    // The RIG LEVEL knob on the pedal face, saved with each rig to balance rigs' volumes.
    layout.add (makeFloat (ParamIDs::rigLevel, "Rig Level", { -24.0f, 12.0f, 0.1f }, 0.0f, 4, asDb));

    // The pedal's BYPASS button. It's also the plugin's official bypass, so
    // the DAW's bypass uses it too.
    layout.add (makeSwitch (ParamIDs::bypass, "Bypass", 5, false));

    // The tuner and the expression pedal.
    constexpr int v6 = 6;
    layout.add (makeSwitch (ParamIDs::tunerOn, "Tuner", v6, false));
    // Heel down (0%) by default, where a real pedal rests: assigned to
    // VOLUME it's silent until rocked forward, as a real volume pedal is.
    layout.add (makeFloat (ParamIDs::expression, "Expression", { 0.0f, 1.0f, 0.001f }, 0.0f, v6, asPercent));
    layout.add (makeChoice (ParamIDs::expressionTarget, "Expression Target", v6, ExpressionTargets::names, ExpressionTargets::off));
    layout.add (makeFloat (ParamIDs::expressionMin, "Expression Min", { 0.0f, 1.0f, 0.01f }, 0.0f, v6, asPercent));
    layout.add (makeFloat (ParamIDs::expressionMax, "Expression Max", { 0.0f, 1.0f, 0.01f }, 1.0f, v6, asPercent));

    // The graphic EQ (stomp 4 for now). Off by default. Its bands are the
    // bass EQ's from PR7-B 0.2.0 (version 25).
    constexpr int v8 = 8, v25 = 25;
    layout.add (makeSwitch (ParamIDs::eqOn, "EQ On", v8, false));
    for (int band = 0; band < ParamIDs::eqBands.size(); ++band)
        layout.add (makeFloat (ParamIDs::eqBands[band].toRawUTF8(), "EQ " + ParamIDs::eqBandNames[band] + " Hz",
                               { -effects::GraphicEq::maxGainDb, effects::GraphicEq::maxGainDb, 0.1f }, 0.0f, v25, asDb));
    layout.add (makeFloat (ParamIDs::eqLevel, "EQ Level", { -12.0f, 12.0f, 0.1f }, 0.0f, v8, asDb));
    layout.add (makeSwitch (ParamIDs::eqUltraLo, "EQ Ultra Lo", v25, false));
    layout.add (makeSwitch (ParamIDs::eqUltraHi, "EQ Ultra Hi", v25, false));
    return layout;
}
