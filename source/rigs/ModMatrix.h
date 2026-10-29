#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/DrumSequencer.h"
#include "dsp/synth/MultiPoint.h"

#include <vector>

// A multipoint shape as presets keep it (PR7-K's stage 4b): here, a drum's envelope (THE SAMPLE EDITOR), x 0 to 1
// across the hit.
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

    // A new drum envelope (6c): flat at the top, changing nothing; x 0 to 1 across the hit.
    static ModShapeData flatDrumEnvelope()
    {
        return { { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 };
    }
    // The drum envelope's menu (6c): back to front (each line's curve turned round with it), and upside down.
    ModShapeData mirrored() const
    {
        ModShapeData d { {}, -1, -1, -1 };
        for (size_t i = points.size(); i-- > 0;)
        {
            const auto curve = i > 0 ? -points[i - 1].curve : 0.0f;   // the line that led to it now leads away
            d.points.push_back ({ 1.0f - points[i].x, points[i].y, curve });
        }
        return d;
    }
    ModShapeData flipped() const
    {
        auto d = *this;
        for (auto& p : d.points)
            p.y = 1.0f - p.y;
        return d;
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
    // `lfo`: x kept within 0 to 1 (always, for a drum's envelope; the flag is PR7-K's, kept so the calls read the same).
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
            return flatDrumEnvelope();
        d.sustain = sustain;
        d.loopStart = loopStart;
        d.loopEnd = loopEnd;
        return d;
    }
};

// The kit's preset data that isn't parameters: THE DRUM SEQUENCER's patterns (PR7-K's 6b) and THE SAMPLE EDITOR's
// envelopes (6c). The name and the XML ("Modulation", "DrumPattern", "DrumEnvelope") are PR7-K's, where these lived in
// its mod matrix: kept so PR7-K presets' and projects' drum data reads the same here.
struct ModMatrix
{
    // THE DRUM SEQUENCER's patterns A to D; each pattern's notes on the heap.
    std::vector<synth::drumseq::Pattern> drumPatterns = std::vector<synth::drumseq::Pattern> ((size_t) synth::drumseq::numPatterns);
    // THE SAMPLE EDITOR's envelopes, one per drum slot.
    static constexpr int numDrumSlots = 12;
    std::vector<ModShapeData> drumEnvelopes = std::vector<ModShapeData> ((size_t) numDrumSlots, ModShapeData::flatDrumEnvelope());

    bool operator== (const ModMatrix& o) const noexcept { return drumPatterns == o.drumPatterns && drumEnvelopes == o.drumEnvelopes; }
    bool isEmpty() const noexcept { return *this == ModMatrix {}; }

    // As XML (in presets and projects) and back: only what's changed from new.
    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("Modulation");
        for (int i = 0; i < (int) drumEnvelopes.size(); ++i)
            if (! (drumEnvelopes[(size_t) i] == ModShapeData::flatDrumEnvelope()))
            {
                auto* e = xml->createNewChildElement ("DrumEnvelope");
                e->setAttribute ("index", i + 1);
                e->setAttribute ("points", drumEnvelopes[(size_t) i].encode());
                e->setAttribute ("sustain", -1);
                e->setAttribute ("loopStart", -1);
                e->setAttribute ("loopEnd", -1);
            }
        for (int p = 0; p < (int) drumPatterns.size(); ++p)
            if (! drumPatterns[(size_t) p].isEmpty())
            {
                auto* e = xml->createNewChildElement ("DrumPattern");
                e->setAttribute ("pattern", synth::drumseq::patternNames[p]);
                e->setAttribute ("notes", drumPatterns[(size_t) p].encode());
            }
        return xml;
    }

    // Anything else in it (a PR7-K preset's routes, LFO shapes, arp patterns...) is left out.
    static ModMatrix fromXml (const juce::XmlElement& xml)
    {
        ModMatrix m;
        for (const auto* e : xml.getChildWithTagNameIterator ("DrumEnvelope"))
            if (const auto index = e->getIntAttribute ("index") - 1; index >= 0 && index < numDrumSlots)
            {
                auto d = ModShapeData::decode (e->getStringAttribute ("points"), -1, -1, -1, true);   // x within 0 to 1
                if (d.points.size() >= 2)
                    m.drumEnvelopes[(size_t) index] = d;
            }
        for (const auto* e : xml.getChildWithTagNameIterator ("DrumPattern"))
            if (const auto p = synth::drumseq::patternNames.indexOf (e->getStringAttribute ("pattern")); p >= 0)
                m.drumPatterns[(size_t) p] = synth::drumseq::Pattern::decode (e->getStringAttribute ("notes"));
        return m;
    }
};
