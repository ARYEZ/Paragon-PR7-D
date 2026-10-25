#include "Parameters.h"

#include "dsp/eq/Dynamics.h"
#include "dsp/eq/EqDesign.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asFrequency = [] (float hz, int)
    {
        return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    };
    const ToText asQ = [] (float q, int) { return juce::String (q, q < 10.0f ? 2 : 1); };

    // A range whose knob travel is logarithmic (frequency, Q): each octave
    // takes the same distance.
    Range logRange (double min, double max)
    {
        return { (float) min, (float) max,
                 [] (float start, float end, float normalised) { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
    }

    // From PR7-C (Parameters.cpp), for COMP's RATIO: a knob whose printed
    // marks sit evenly round it: between two marks the travel is even in log
    // (or straight, from a mark of 0). `snap` tidies a value (the SOFT stretch).
    Range markedRange (std::vector<float> marks, std::function<float (float)> snap = {})
    {
        const auto steps = (float) (marks.size() - 1);
        auto between = [] (float low, float high, float t) { return low > 0.0f ? low * std::pow (high / low, t) : low + (high - low) * t; };
        auto within = [] (float low, float high, float value) { return low > 0.0f ? std::log (value / low) / std::log (high / low) : (value - low) / (high - low); };
        return { marks.front(), marks.back(),
                 [marks, steps, between] (float, float, float proportion)
                 {
                     const auto position = juce::jlimit (0.0f, 1.0f, proportion) * steps;
                     const auto below = juce::jmin ((int) position, (int) steps - 1);
                     return between (marks[(size_t) below], marks[(size_t) below + 1], position - (float) below);
                 },
                 [marks, steps, within] (float, float, float value)
                 {
                     for (size_t i = 1; i < marks.size(); ++i)
                         if (value <= marks[i])
                             return ((float) (i - 1) + within (marks[i - 1], marks[i], juce::jmax (value, marks[i - 1]))) / steps;
                     return 1.0f;
                 },
                 [marks, snap] (float, float, float value)
                 {
                     value = juce::jlimit (marks.front(), marks.back(), value);
                     return snap ? snap (value) : value;
                 } };
    }

    // RATIO: SOFT, then 1.5:1 to infinite. Below 1.25 is the SOFT position.
    Range ratioRange()
    {
        using ParamRanges::compRatioMarks;
        return markedRange ({ std::begin (compRatioMarks), std::end (compRatioMarks) },
                            [] (float ratio) { return ratio < 1.25f ? 1.0f : ratio; });
    }

    const ToText asRatio = [] (float ratio, int)
    {
        if (ratio < 1.25f)
            return juce::String ("Soft");
        if (ratio >= 29.5f)
            return juce::String ("Inf:1");
        return juce::String (ratio, ratio < 10.0f ? 1 : 0) + ":1";
    };

    const ToText asMs = [] (float ms, int)
    {
        return ms < 1000.0f ? juce::String (ms, ms < 10.0f ? 1 : 0) + " ms" : juce::String (ms / 1000.0f, 2) + " s";
    };

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

    // Stomp 4, the EQ (PR7-E 0.4.0): 24 band slots, each unused until a
    // band is created in it, then OUTPUT, GAIN SCALE and AUTO GAIN. eqOn
    // (version 8) was the graphic EQ's switch; it's the stomp's now.
    constexpr int v8 = 8, v30 = 30;
    layout.add (makeSwitch (ParamIDs::eqOn, "EQ On", v8, false));
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
    {
        const auto id = [band] (const char* what) { return ParamIDs::eqBand (band, what); };
        const auto name = [band] (const juce::String& what) { return "EQ Band " + juce::String (band + 1) + " " + what; };
        const eq::Band defaults;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Used"), v30 }, name ("Used"), false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("On"), v30 }, name ("On"), true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Shape"), v30 }, name ("Shape"), eq::shapeNames, 0));
        layout.add (makeFloat (id ("Freq").toRawUTF8(), name ("Frequency"), logRange (eq::minFrequency, eq::maxFrequency),
                               (float) defaults.frequency, v30, asFrequency));
        layout.add (makeFloat (id ("Gain").toRawUTF8(), name ("Gain"), { (float) -eq::maxGainDb, (float) eq::maxGainDb, 0.01f },
                               0.0f, v30, asDb));
        layout.add (makeFloat (id ("Q").toRawUTF8(), name ("Q"), logRange (eq::minQ, eq::maxQ), (float) defaults.q, v30, asQ));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Slope"), v30 }, name ("Slope"), eq::slopeNames, eq::defaultSlope));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Place"), v30 }, name ("Placement"), eq::placementNames, 0));
    }
    // Dynamic EQ (0.5.0), each band's, after all the bands' settings above
    // so those keep their places in the host's list.
    constexpr int v31 = 31;
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
    {
        const auto id = [band] (const char* what) { return ParamIDs::eqBand (band, what); };
        const auto name = [band] (const juce::String& what) { return "EQ Band " + juce::String (band + 1) + " " + what; };
        const eq::Band defaults;
        layout.add (makeFloat (id ("Dyn").toRawUTF8(), name ("Dynamic Range"),
                               { (float) -eq::dynamics::maxRangeDb, (float) eq::dynamics::maxRangeDb, 0.01f }, 0.0f, v31, asDb));
        layout.add (makeSwitch (id ("DynAuto").toRawUTF8(), name ("Auto Threshold"), v31, defaults.autoThreshold));
        layout.add (makeFloat (id ("Thresh").toRawUTF8(), name ("Threshold"),
                               { (float) eq::dynamics::minThresholdDb, (float) eq::dynamics::maxThresholdDb, 0.1f },
                               (float) defaults.thresholdDb, v31, asDb));
        layout.add (makeFloat (id ("Attack").toRawUTF8(), name ("Attack"), { 0.0f, 1.0f, 0.01f }, (float) defaults.attack, v31, asPercent));
        layout.add (makeFloat (id ("Release").toRawUTF8(), name ("Release"), { 0.0f, 1.0f, 0.01f }, (float) defaults.release, v31, asPercent));
    }
    // Stomp 2, COMP (0.9.0): ranges and defaults as on PR7-C.
    {
        using namespace ParamRanges;
        constexpr int v33 = 33;
        layout.add (makeSwitch (ParamIDs::compOn, "Compressor", v33, false));
        layout.add (makeFloat (ParamIDs::compThreshold, "Comp Threshold", { compThresholdMin, compThresholdMax, 0.1f }, -18.0f, v33, asDb));
        layout.add (makeFloat (ParamIDs::compRatio, "Comp Ratio", ratioRange(), 4.0f, v33, asRatio));
        layout.add (makeFloat (ParamIDs::compAttack, "Comp Attack", logRange (compAttackMin, compAttackMax), 10.0f, v33, asMs));
        layout.add (makeFloat (ParamIDs::compRelease, "Comp Release", logRange (compReleaseMin, compReleaseMax), 300.0f, v33, asMs));
        layout.add (makeSwitch (ParamIDs::compAuto, "Comp Auto Release", v33, false));
        layout.add (makeFloat (ParamIDs::compMakeUp, "Comp Make Up", { 0.0f, compMakeUpMax, 0.1f }, 0.0f, v33, asDb));
        layout.add (makeChoice (ParamIDs::compCharacter, "Comp Character", v33, compCharacters, 0));
        layout.add (makeSwitch (ParamIDs::compLink, "Comp Link", v33, true));
    }

    // Stomp 7, the LIMITER (0.8.0): ranges and defaults as on PR7-C.
    constexpr int v32 = 32;
    layout.add (makeSwitch (ParamIDs::limitOn, "Limiter", v32, false));
    layout.add (makeFloat (ParamIDs::limitThreshold, "Limit Threshold", { ParamRanges::limitThresholdMin, 0.0f, 0.1f }, -1.0f, v32, asDb));
    layout.add (makeFloat (ParamIDs::limitMakeUp, "Limit Make Up", { 0.0f, ParamRanges::limitMakeUpMax, 0.1f }, 0.0f, v32, asDb));
    layout.add (makeChoice (ParamIDs::limitMode, "Limit Mode", v32, ParamRanges::limitModes, 0));
    layout.add (makeFloat (ParamIDs::limitMix, "Limit Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, v32,
                           [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }));
    layout.add (makeSwitch (ParamIDs::limitLink, "Limit Link", v32, true));
    layout.add (makeFloat (ParamIDs::limitTrim, "Output Trim", { ParamRanges::limitTrimMin, ParamRanges::limitTrimMax, 0.1f }, 0.0f, v32, asDb));

    layout.add (makeFloat (ParamIDs::eqOutput, "EQ Output", { -36.0f, 36.0f, 0.01f }, 0.0f, v30, asDb));
    layout.add (makeFloat (ParamIDs::eqGainScale, "EQ Gain Scale", { 0.0f, 2.0f, 0.01f }, 1.0f, v30, asPercent));
    layout.add (makeSwitch (ParamIDs::eqAutoGain, "EQ Auto Gain", v30, false));
    return layout;
}
