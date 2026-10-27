#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/Modulation.h"

#include <vector>

// The MOD MATRIX as a preset keeps it (stage 4a): each route by its
// source's and destination's names (the destination is the parameter ID of
// the knob it moves), so later versions can add sources and destinations
// without disturbing saved sounds. A route to a destination this version
// doesn't know is kept, but does nothing.
struct ModRouteData
{
    bool on = true;
    juce::String source { "lfo1" };
    juce::String destination;
    float amount = 0.0f;       // -1 to 1
    bool percent = false;      // % (a share of the travel left) rather than + (added)
    float lagMs = 0.0f;        // 0 to 2000
    float curve = 0.0f;        // -1 to 1

    bool operator== (const ModRouteData& o) const noexcept
    {
        return on == o.on && source == o.source && destination == o.destination && juce::exactlyEqual (amount, o.amount)
            && percent == o.percent && juce::exactlyEqual (lagMs, o.lagMs) && juce::exactlyEqual (curve, o.curve);
    }
};

struct ModMatrix
{
    std::vector<ModRouteData> routes;
    std::array<juce::String, 3> macroNames;   // empty: "MACRO 1"... (the owner can rename them)

    bool operator== (const ModMatrix& o) const noexcept { return routes == o.routes && macroNames == o.macroNames; }
    bool isEmpty() const noexcept
    {
        return routes.empty() && std::all_of (macroNames.begin(), macroNames.end(), [] (const juce::String& n) { return n.isEmpty(); });
    }

    juce::String macroName (int index) const
    {
        const auto& name = macroNames[(size_t) juce::jlimit (0, 2, index)];
        return name.isNotEmpty() ? name : "MACRO " + juce::String (index + 1);
    }

    // As XML (in presets) and back.
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("Modulation");
        for (int i = 0; i < 3; ++i)
            if (macroNames[(size_t) i].isNotEmpty())
                xml->setAttribute ("macro" + juce::String (i + 1), macroNames[(size_t) i]);
        for (const auto& r : routes)
        {
            auto* e = xml->createNewChildElement ("Route");
            e->setAttribute ("on", r.on);
            e->setAttribute ("source", r.source);
            e->setAttribute ("destination", r.destination);
            e->setAttribute ("amount", r.amount);
            e->setAttribute ("percent", r.percent);
            e->setAttribute ("lag", r.lagMs);
            e->setAttribute ("curve", r.curve);
        }
        return xml;
    }

    static ModMatrix fromXml (const juce::XmlElement& xml)
    {
        ModMatrix m;
        for (int i = 0; i < 3; ++i)
            m.macroNames[(size_t) i] = xml.getStringAttribute ("macro" + juce::String (i + 1)).substring (0, 24);
        for (const auto* e : xml.getChildWithTagNameIterator ("Route"))
        {
            if ((int) m.routes.size() >= synth::mod::maxRoutes)
                break;
            ModRouteData r;
            r.on = e->getBoolAttribute ("on", true);
            r.source = e->getStringAttribute ("source");
            r.destination = e->getStringAttribute ("destination");
            r.amount = juce::jlimit (-1.0f, 1.0f, (float) e->getDoubleAttribute ("amount"));
            r.percent = e->getBoolAttribute ("percent");
            r.lagMs = juce::jlimit (0.0f, 2000.0f, (float) e->getDoubleAttribute ("lag"));
            r.curve = juce::jlimit (-1.0f, 1.0f, (float) e->getDoubleAttribute ("curve"));
            m.routes.push_back (r);
        }
        return m;
    }
};
