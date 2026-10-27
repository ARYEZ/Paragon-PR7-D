#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// MIDI LEARN (stage 4f): which of your MIDI controller's knobs and faders
// (by CC number) turns which of the synth's knobs (by parameter ID). Kept
// with the project, and as a default (a file in the presets folder) that a
// new instance starts with. One knob per controller and one controller per
// knob: linking either again replaces the old link.
struct MidiLink
{
    int controller = 0;          // CC 0 to 127
    juce::String parameterId;
    bool operator== (const MidiLink& o) const noexcept { return controller == o.controller && parameterId == o.parameterId; }
};

struct MidiLinks
{
    std::vector<MidiLink> links;

    bool operator== (const MidiLinks& o) const noexcept { return links == o.links; }
    bool isEmpty() const noexcept { return links.empty(); }

    // The controller linked to a knob (-1: none).
    int controllerFor (const juce::String& parameterId) const
    {
        for (const auto& l : links)
            if (l.parameterId == parameterId)
                return l.controller;
        return -1;
    }

    void link (int controller, const juce::String& parameterId)
    {
        std::erase_if (links, [&] (const MidiLink& l) { return l.controller == controller || l.parameterId == parameterId; });
        links.push_back ({ juce::jlimit (0, 127, controller), parameterId });
        std::sort (links.begin(), links.end(), [] (const MidiLink& a, const MidiLink& b) { return a.controller < b.controller; });
    }

    void forget (const juce::String& parameterId)
    {
        std::erase_if (links, [&] (const MidiLink& l) { return l.parameterId == parameterId; });
    }

    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("MidiLinks");
        for (const auto& l : links)
        {
            auto* e = xml->createNewChildElement ("Link");
            e->setAttribute ("cc", l.controller);
            e->setAttribute ("parameter", l.parameterId);
        }
        return xml;
    }

    static MidiLinks fromXml (const juce::XmlElement& xml)
    {
        MidiLinks m;
        for (const auto* e : xml.getChildWithTagNameIterator ("Link"))
        {
            const auto cc = e->getIntAttribute ("cc", -1);
            const auto id = e->getStringAttribute ("parameter");
            if (cc >= 0 && cc <= 127 && id.isNotEmpty())
                m.link (cc, id);
        }
        return m;
    }
};
