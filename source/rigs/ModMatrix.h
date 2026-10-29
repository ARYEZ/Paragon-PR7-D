#pragma once

#include <juce_core/juce_core.h>

#include "dsp/synth/SynthSettings.h"
#include "dsp/synth/DrumSequencer.h"
#include "dsp/synth/MultiPoint.h"

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
    // The ARPEGGIATORS' patterns (stage 5a): A to D for each of the 8.
    // On the heap (40 KB): a ModMatrix is copied for undo, presets and comparisons, often on the stack, and
    // Windows gives a thread 1 MB (with it inline, 0.15.0's build crashed there).
    std::vector<synth::arp::Patterns> arps = filledArps();
    // Each arp's note RANDOMIZER (stage 5b).
    std::array<synth::arp::Randomizer, synth::arp::numArps> arpRandomizers {};
    // THE DRUM SEQUENCER's patterns A to D (stage 6b); each pattern's notes on the heap.
    std::vector<synth::drumseq::Pattern> drumPatterns = std::vector<synth::drumseq::Pattern> ((size_t) synth::drumseq::numPatterns);
    // THE SAMPLE EDITOR's envelopes, one per drum slot (6c).
    static constexpr int numDrumSlots = 12;
    std::vector<ModShapeData> drumEnvelopes = std::vector<ModShapeData> ((size_t) numDrumSlots, ModShapeData::flatDrumEnvelope());
    static std::vector<synth::arp::Patterns> filledArps()
    {
        return std::vector<synth::arp::Patterns> ((size_t) synth::arp::numArps, synth::arp::Patterns::makeDefault());
    }

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
            && pitchEnvelope == o.pitchEnvelope && stepPattern == o.stepPattern && travel == o.travel && arps == o.arps
            && arpRandomizers == o.arpRandomizers && drumPatterns == o.drumPatterns && drumEnvelopes == o.drumEnvelopes;
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
        for (int i = 0; i < (int) drumEnvelopes.size(); ++i)
            if (! (drumEnvelopes[(size_t) i] == ModShapeData::flatDrumEnvelope()))
                addShape ("DrumEnvelope", i, drumEnvelopes[(size_t) i]);
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
        // The arps' patterns, those changed from new: each pattern's blocks as
        // "step:length:transpose:velocity:slide:ratchet:chord:tones:sub:subTranspose" (0.15.0 wrote
        // the first six), only the blocks that are on.
        const auto fresh = synth::arp::Patterns::makeDefault();
        for (int a = 0; a < synth::arp::numArps; ++a)
            for (int p = 0; p < synth::arp::numPatterns; ++p)
            {
                const auto& pattern = arps[(size_t) a].patterns[(size_t) p];
                if (pattern == fresh.patterns[(size_t) p])
                    continue;
                juce::StringArray blocks;
                for (int k = 0; k < synth::arp::maxSteps; ++k)
                {
                    const auto& b = pattern.blocks[(size_t) k];
                    if (b.on)
                        blocks.add (juce::String (k) + ":" + juce::String (b.length) + ":" + juce::String (b.transpose) + ":"
                                    + juce::String (b.velocity, 3) + ":" + (b.slide ? "1" : "0") + ":" + juce::String (b.ratchet) + ":" + juce::String (b.chord) + ":"
                                    + juce::String (b.chordMask) + ":" + (b.sub ? "1" : "0") + ":" + juce::String (b.subTranspose));
                }
                auto* e = xml->createNewChildElement ("ArpPattern");
                e->setAttribute ("arp", a + 1);
                e->setAttribute ("pattern", synth::arp::patternNames[p]);
                e->setAttribute ("blocks", blocks.joinIntoString (" "));
            }
        // The randomizers changed from new.
        for (int a = 0; a < synth::arp::numArps; ++a)
        {
            const auto& r = arpRandomizers[(size_t) a];
            if (r == synth::arp::Randomizer {})
                continue;
            auto* e = xml->createNewChildElement ("ArpRandom");
            e->setAttribute ("arp", a + 1);
            e->setAttribute ("scale", synth::arp::scaleNames[r.scale]);
            e->setAttribute ("notes", joined (r.noteChances));
            e->setAttribute ("octaves", joined (r.octaveChances));
            e->setAttribute ("lengths", joined (r.lengthChances));
            e->setAttribute ("rest", r.restChance);
            e->setAttribute ("slide", r.slideChance);
            e->setAttribute ("ratchet", r.ratchetChance);
            e->setAttribute ("velocityLow", r.velocityLow);
            e->setAttribute ("velocityHigh", r.velocityHigh);
            e->setAttribute ("roll", juce::String (r.rollNotes ? "n" : "") + (r.rollOctaves ? "o" : "") + (r.rollLengths ? "l" : "")
                                         + (r.rollVelocity ? "v" : "") + (r.rollRests ? "r" : ""));
            e->setAttribute ("symmetry", r.symmetry);
            e->setAttribute ("autoDice", r.autoDice);
            e->setAttribute ("seed", r.seed);
        }
        // The drum patterns with notes.
        for (int p = 0; p < (int) drumPatterns.size(); ++p)
            if (! drumPatterns[(size_t) p].isEmpty())
            {
                auto* e = xml->createNewChildElement ("DrumPattern");
                e->setAttribute ("pattern", synth::drumseq::patternNames[p]);
                e->setAttribute ("notes", drumPatterns[(size_t) p].encode());
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
        for (const auto* e : xml.getChildWithTagNameIterator ("DrumEnvelope"))
            if (const auto index = e->getIntAttribute ("index") - 1; index >= 0 && index < numDrumSlots)
            {
                auto d = ModShapeData::decode (e->getStringAttribute ("points"), -1, -1, -1, true);   // x within 0 to 1
                if (d.points.size() >= 2)
                    m.drumEnvelopes[(size_t) index] = d;
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
        for (const auto* e : xml.getChildWithTagNameIterator ("ArpPattern"))
        {
            const auto a = e->getIntAttribute ("arp") - 1;
            const auto p = synth::arp::patternNames.indexOf (e->getStringAttribute ("pattern"));
            if (a < 0 || a >= synth::arp::numArps || p < 0)
                continue;
            auto& pattern = m.arps[(size_t) a].patterns[(size_t) p];
            pattern = {};
            for (const auto& item : juce::StringArray::fromTokens (e->getStringAttribute ("blocks"), " ", {}))
            {
                const auto parts = juce::StringArray::fromTokens (item, ":", {});
                const auto k = parts[0].getIntValue();
                if ((parts.size() != 6 && parts.size() != 10) || k < 0 || k >= synth::arp::maxSteps)
                    continue;
                auto& b = pattern.blocks[(size_t) k];
                b.on = true;
                b.length = juce::jlimit (1, synth::arp::maxSteps, parts[1].getIntValue());
                b.transpose = juce::jlimit (-36, 36, parts[2].getIntValue());
                b.velocity = juce::jlimit (0.0f, 1.0f, parts[3].getFloatValue());
                b.slide = parts[4].getIntValue() != 0;
                b.ratchet = juce::jlimit (1, synth::arp::maxRatchet, parts[5].getIntValue());
                if (parts.size() == 10)
                {
                    b.chord = juce::jlimit (0, synth::arp::chordNames.size() - 1, parts[6].getIntValue());
                    b.chordMask = parts[7].getIntValue() & 0x1f;
                    b.sub = parts[8].getIntValue() != 0;
                    b.subTranspose = juce::jlimit (-24, 0, parts[9].getIntValue());
                }
            }
        }
        for (const auto* e : xml.getChildWithTagNameIterator ("DrumPattern"))
            if (const auto p = synth::drumseq::patternNames.indexOf (e->getStringAttribute ("pattern")); p >= 0)
                m.drumPatterns[(size_t) p] = synth::drumseq::Pattern::decode (e->getStringAttribute ("notes"));
        for (const auto* e : xml.getChildWithTagNameIterator ("ArpRandom"))
        {
            const auto a = e->getIntAttribute ("arp") - 1;
            if (a < 0 || a >= synth::arp::numArps)
                continue;
            auto& r = m.arpRandomizers[(size_t) a];
            r.scale = juce::jmax (0, synth::arp::scaleNames.indexOf (e->getStringAttribute ("scale")));
            split (e->getStringAttribute ("notes"), r.noteChances);
            split (e->getStringAttribute ("octaves"), r.octaveChances);
            split (e->getStringAttribute ("lengths"), r.lengthChances);
            r.restChance = (float) e->getDoubleAttribute ("rest", r.restChance);
            r.slideChance = (float) e->getDoubleAttribute ("slide", r.slideChance);
            r.ratchetChance = (float) e->getDoubleAttribute ("ratchet", r.ratchetChance);
            r.velocityLow = (float) e->getDoubleAttribute ("velocityLow", r.velocityLow);
            r.velocityHigh = (float) e->getDoubleAttribute ("velocityHigh", r.velocityHigh);
            const auto roll = e->getStringAttribute ("roll");
            r.rollNotes = roll.containsChar ('n');
            r.rollOctaves = roll.containsChar ('o');
            r.rollLengths = roll.containsChar ('l');
            r.rollVelocity = roll.containsChar ('v');
            r.rollRests = roll.containsChar ('r');
            r.symmetry = e->getBoolAttribute ("symmetry");
            r.autoDice = e->getBoolAttribute ("autoDice");
            r.seed = e->getIntAttribute ("seed", 1);
        }
        return m;
    }

    // Chances as text, "0.3 0.6 ...", and back (missing ones left as they were).
    template <size_t N>
    static juce::String joined (const std::array<float, N>& values)
    {
        juce::StringArray items;
        for (const auto v : values)
            items.add (juce::String (v, 3));
        return items.joinIntoString (" ");
    }
    template <size_t N>
    static void split (const juce::String& text, std::array<float, N>& values)
    {
        const auto items = juce::StringArray::fromTokens (text, " ", {});
        for (size_t i = 0; i < N && (int) i < items.size(); ++i)
            values[i] = juce::jlimit (0.0f, 1.0f, items[(int) i].getFloatValue());
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
