#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/SynthSettings.h"

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

// A multipoint shape as presets keep it (stage 4b): a modulation envelope
// (x in seconds or beats) or a drawn LFO shape (x 0 to 1 across a cycle).
struct ModShapeData
{
    std::vector<synth::mod::ShapePoint> points;
    int sustain = -1, loopStart = -1, loopEnd = -1;

    bool operator== (const ModShapeData& o) const noexcept
    {
        if (points.size() != o.points.size() || sustain != o.sustain || loopStart != o.loopStart || loopEnd != o.loopEnd)
            return false;
        for (size_t i = 0; i < points.size(); ++i)
            if (! juce::exactlyEqual (points[i].x, o.points[i].x) || ! juce::exactlyEqual (points[i].y, o.points[i].y)
                || ! juce::exactlyEqual (points[i].curve, o.points[i].curve))
                return false;
        return true;
    }

    // A new envelope: up in 50 ms, down to 60% (SUSTAIN), out by 1.2 s; LOOP round the top and the sustain.
    static ModShapeData defaultEnvelope()
    {
        return { { { 0.0f, 0.0f, 0.0f }, { 0.05f, 1.0f, -0.4f }, { 0.4f, 0.6f, 0.0f }, { 1.2f, 0.0f, -0.4f } }, 2, 1, 2 };
    }
    // A new PITCH envelope: from +half the RANGE down to no change (the middle) in 150 ms.
    static ModShapeData defaultPitchEnvelope()
    {
        return { { { 0.0f, 0.75f, -0.5f }, { 0.15f, 0.5f, 0.0f } }, -1, -1, -1 };
    }
    // A new drawn LFO shape: a triangle.
    static ModShapeData defaultLfoShape()
    {
        return { { { 0.0f, 0.5f, 0.0f }, { 0.25f, 1.0f, 0.0f }, { 0.75f, 0.0f, 0.0f }, { 1.0f, 0.5f, 0.0f } }, -1, -1, -1 };
    }

    // For the audio thread: plain data, the points sorted, at most 64.
    synth::mod::Shape toShape() const
    {
        synth::mod::Shape shape;
        shape.count = juce::jmin ((int) points.size(), synth::mod::maxShapePoints);
        for (int i = 0; i < shape.count; ++i)
            shape.points[(size_t) i] = points[(size_t) i];
        const auto valid = [&shape] (int index) { return index >= 0 && index < shape.count ? index : -1; };
        shape.sustain = valid (sustain);
        shape.loopStart = valid (loopStart);
        shape.loopEnd = valid (loopEnd);
        return shape;
    }

    // As text: "x:y:curve ..." then the flags.
    juce::String encode() const
    {
        juce::StringArray items;
        for (const auto& p : points)
            items.add (juce::String (p.x, 4) + ":" + juce::String (p.y, 4) + ":" + juce::String (p.curve, 3));
        return items.joinIntoString (" ");
    }
    static ModShapeData decode (const juce::String& text, int sustain, int loopStart, int loopEnd, bool lfo)
    {
        ModShapeData d;
        float lastX = 0.0f;
        for (const auto& item : juce::StringArray::fromTokens (text, " ", {}))
        {
            const auto parts = juce::StringArray::fromTokens (item, ":", {});
            if (parts.size() != 3 || (int) d.points.size() >= synth::mod::maxShapePoints)
                continue;
            auto x = juce::jmax (lastX, parts[0].getFloatValue());   // never backwards
            if (lfo)
                x = juce::jlimit (0.0f, 1.0f, x);
            d.points.push_back ({ x, juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue()), juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) });
            lastX = x;
        }
        if (d.points.size() < 2)
            return lfo ? defaultLfoShape() : defaultEnvelope();
        d.sustain = sustain;
        d.loopStart = loopStart;
        d.loopEnd = loopEnd;
        return d;
    }
};

// An oscillator's TRAVEL (stage 4e) as presets keep it: its envelope (x 0 to 1
// across a cycle, y the place in the wavetable) and its pattern's steps and strengths.
struct TravelData
{
    ModShapeData envelope = defaultEnvelope();
    synth::travel::Pattern pattern {};

    // A new one: a straight ramp from the first frame to the last.
    static ModShapeData defaultEnvelope() { return { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 }; }
    bool operator== (const TravelData& o) const noexcept { return envelope == o.envelope && pattern == o.pattern; }
    bool isDefault() const noexcept { return *this == TravelData {}; }

    // The steps as text: "mode:iterations:length" each; the strengths "start:middle:end:hasMiddle:curve" each.
    juce::String encodeSteps() const
    {
        juce::StringArray items;
        for (const auto& s : pattern.steps)
            items.add (juce::String ((int) s.mode) + ":" + juce::String (s.iterations, 1) + ":" + juce::String (s.length));
        return items.joinIntoString (" ");
    }
    juce::String encodeStrength() const
    {
        juce::StringArray items;
        for (const auto& s : pattern.strength)
            items.add (juce::String (s.start, 3) + ":" + juce::String (s.middle, 3) + ":" + juce::String (s.end, 3) + ":"
                       + (s.hasMiddle ? "1" : "0") + ":" + juce::String (s.curve, 3));
        return items.joinIntoString (" ");
    }
    void decodeSteps (const juce::String& text)
    {
        const auto items = juce::StringArray::fromTokens (text, " ", {});
        for (int i = 0; i < juce::jmin (items.size(), synth::travel::maxSteps); ++i)
        {
            const auto parts = juce::StringArray::fromTokens (items[i], ":", {});
            if (parts.size() != 3)
                continue;
            auto& s = pattern.steps[(size_t) i];
            s.mode = (synth::travel::StepMode) juce::jlimit (0, synth::travel::stepModeNames.size() - 1, parts[0].getIntValue());
            s.iterations = synth::travel::clampIterations (parts[1].getFloatValue());
            s.length = juce::jlimit (1, 2, parts[2].getIntValue());
        }
    }
    void decodeStrength (const juce::String& text)
    {
        const auto items = juce::StringArray::fromTokens (text, " ", {});
        for (int i = 0; i < juce::jmin (items.size(), synth::travel::maxSteps); ++i)
        {
            const auto parts = juce::StringArray::fromTokens (items[i], ":", {});
            if (parts.size() != 5)
                continue;
            auto& s = pattern.strength[(size_t) i];
            s.start = juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue());
            s.middle = juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue());
            s.end = juce::jlimit (0.0f, 1.0f, parts[2].getFloatValue());
            s.hasMiddle = parts[3].getIntValue() != 0;
            s.curve = juce::jlimit (-1.0f, 1.0f, parts[4].getFloatValue());
        }
    }
};

struct ModMatrix
{
    std::vector<ModRouteData> routes;
    std::array<juce::String, 3> macroNames;   // empty: "MACRO 1"... (the owner can rename them)
    // The modulation envelopes' and drawn LFO shapes' points (stage 4b).
    std::array<ModShapeData, synth::mod::numEnvelopes> envelopes = filled<synth::mod::numEnvelopes> (ModShapeData::defaultEnvelope());
    std::array<ModShapeData, synth::mod::numLfoShapes> lfoShapes = filled<synth::mod::numLfoShapes> (ModShapeData::defaultLfoShape());
    // The PITCH module's envelope (stage 4c).
    ModShapeData pitchEnvelope = ModShapeData::defaultPitchEnvelope();
    // The STEP SEQUENCER's steps (stage 4d): new, every step on at full height, none linked.
    synth::seq::Pattern stepPattern {};
    // Each oscillator's TRAVEL (stage 4e).
    std::array<TravelData, synth::numOscillators> travel {};

    template <int N>
    static std::array<ModShapeData, (size_t) N> filled (const ModShapeData& d)
    {
        std::array<ModShapeData, (size_t) N> a;
        a.fill (d);
        return a;
    }

    bool operator== (const ModMatrix& o) const noexcept
    {
        return routes == o.routes && macroNames == o.macroNames && envelopes == o.envelopes && lfoShapes == o.lfoShapes
            && pitchEnvelope == o.pitchEnvelope && stepPattern == o.stepPattern && travel == o.travel;
    }
    bool isEmpty() const noexcept { return *this == ModMatrix {}; }

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
        // Shapes, only those changed from new.
        const auto addShape = [&xml] (const char* tag, int index, const ModShapeData& d)
        {
            auto* e = xml->createNewChildElement (tag);
            e->setAttribute ("index", index + 1);
            e->setAttribute ("points", d.encode());
            e->setAttribute ("sustain", d.sustain);
            e->setAttribute ("loopStart", d.loopStart);
            e->setAttribute ("loopEnd", d.loopEnd);
        };
        for (int i = 0; i < synth::mod::numEnvelopes; ++i)
            if (! (envelopes[(size_t) i] == ModShapeData::defaultEnvelope()))
                addShape ("Envelope", i, envelopes[(size_t) i]);
        for (int i = 0; i < synth::mod::numLfoShapes; ++i)
            if (! (lfoShapes[(size_t) i] == ModShapeData::defaultLfoShape()))
                addShape ("LfoShape", i, lfoShapes[(size_t) i]);
        if (! (pitchEnvelope == ModShapeData::defaultPitchEnvelope()))
            addShape ("PitchEnvelope", 0, pitchEnvelope);
        if (! (stepPattern == synth::seq::Pattern {}))
        {
            auto* e = xml->createNewChildElement ("StepSeq");
            e->setAttribute ("left", encodeRow (stepPattern.rows[0]));
            e->setAttribute ("right", encodeRow (stepPattern.rows[1]));
        }
        for (int i = 0; i < synth::numOscillators; ++i)
        {
            const auto& t = travel[(size_t) i];
            if (t.isDefault())
                continue;
            auto* e = xml->createNewChildElement ("Travel");
            e->setAttribute ("index", i + 1);
            e->setAttribute ("points", t.envelope.encode());
            e->setAttribute ("steps", t.encodeSteps());
            e->setAttribute ("strength", t.encodeStrength());
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
        for (const auto lfo : { false, true })
            for (const auto* e : xml.getChildWithTagNameIterator (lfo ? "LfoShape" : "Envelope"))
            {
                const auto index = e->getIntAttribute ("index") - 1;
                const auto count = lfo ? synth::mod::numLfoShapes : synth::mod::numEnvelopes;
                if (index < 0 || index >= count)
                    continue;
                const auto d = ModShapeData::decode (e->getStringAttribute ("points"), e->getIntAttribute ("sustain", -1),
                                                     e->getIntAttribute ("loopStart", -1), e->getIntAttribute ("loopEnd", -1), lfo);
                (lfo ? m.lfoShapes[(size_t) index] : m.envelopes[(size_t) index]) = d;
            }
        if (const auto* e = xml.getChildByName ("PitchEnvelope"))
            m.pitchEnvelope = ModShapeData::decode (e->getStringAttribute ("points"), e->getIntAttribute ("sustain", -1),
                                                    e->getIntAttribute ("loopStart", -1), e->getIntAttribute ("loopEnd", -1), false);
        if (const auto* e = xml.getChildByName ("StepSeq"))
        {
            m.stepPattern.rows[0] = decodeRow (e->getStringAttribute ("left"));
            m.stepPattern.rows[1] = decodeRow (e->getStringAttribute ("right"));
        }
        for (const auto* e : xml.getChildWithTagNameIterator ("Travel"))
        {
            const auto index = e->getIntAttribute ("index") - 1;
            if (index < 0 || index >= synth::numOscillators)
                continue;
            auto& t = m.travel[(size_t) index];
            t.envelope = ModShapeData::decode (e->getStringAttribute ("points"), -1, -1, -1, true);
            t.decodeSteps (e->getStringAttribute ("steps"));
            t.decodeStrength (e->getStringAttribute ("strength"));
        }
        return m;
    }

    // A row of steps as text: "height[flags] ...", flags "-" deleted, ">" linked to the next.
    static juce::String encodeRow (const synth::seq::Row& row)
    {
        juce::StringArray items;
        for (const auto& step : row)
            items.add (juce::String (step.level, 3) + (step.on ? "" : "-") + (step.tied ? ">" : ""));
        return items.joinIntoString (" ");
    }
    static synth::seq::Row decodeRow (const juce::String& text)
    {
        synth::seq::Row row {};
        const auto items = juce::StringArray::fromTokens (text, " ", {});
        for (int i = 0; i < juce::jmin (items.size(), synth::seq::maxSteps); ++i)
        {
            const auto& item = items[i];
            auto& step = row[(size_t) i];
            step.level = juce::jlimit (0.0f, 1.0f, item.getFloatValue());
            step.on = ! item.containsChar ('-');
            step.tied = item.containsChar ('>');
        }
        return row;
    }
};
