#include "Song.h"

namespace song
{
    double Song::snap (double beat) const noexcept
    {
        const auto bar = beatsPerBar();
        return juce::jmax (0.0, std::round (beat / bar) * bar);
    }

    int Song::blockAt (double beat) const noexcept
    {
        for (int i = 0; i < (int) blocks.size(); ++i)
            if (beat >= blocks[(size_t) i].start && beat < blocks[(size_t) i].end())
                return i;
        return -1;
    }

    void Song::settle (int keepFirst)
    {
        // In time order (the one just placed first among equals), then each block starting no earlier than the one
        // before it ends.
        std::vector<std::pair<Block, bool>> ordered;
        for (int i = 0; i < (int) blocks.size(); ++i)
            ordered.emplace_back (std::move (blocks[(size_t) i]), i == keepFirst);
        std::stable_sort (ordered.begin(), ordered.end(), [] (const auto& a, const auto& b)
        {
            return a.first.start != b.first.start ? a.first.start < b.first.start : (a.second && ! b.second);
        });
        blocks.clear();
        for (auto& [b, placed] : ordered)
        {
            if (! blocks.empty() && b.start < blocks.back().end() - 1.0e-9)
                b.start = blocks.back().end();
            blocks.push_back (std::move (b));
        }
    }

    int Song::place (Block b, double atBeat)
    {
        auto at = snap (atBeat);
        // Inside a block: before or after it, whichever edge is nearer the drop.
        if (const auto inside = blockAt (atBeat); inside >= 0)
        {
            const auto& there = blocks[(size_t) inside];
            at = atBeat - there.start < there.length * 0.5 ? there.start : there.end();
        }
        b.start = at;
        b.length = juce::jmax (beatsPerBar() * 0.25, b.length);
        blocks.push_back (std::move (b));
        // The new one goes in front of any block that starts where it does, and those move along.
        settle ((int) blocks.size() - 1);
        for (int i = 0; i < (int) blocks.size(); ++i)
            if (juce::exactlyEqual (blocks[(size_t) i].start, at))
                return i;
        return (int) blocks.size() - 1;
    }

    int Song::move (int index, double toBeat)
    {
        if (index < 0 || index >= (int) blocks.size())
            return -1;
        auto b = blocks[(size_t) index];
        blocks.erase (blocks.begin() + index);
        return place (std::move (b), toBeat);
    }

    void Song::resize (int index, double newLength)
    {
        if (index < 0 || index >= (int) blocks.size())
            return;
        const auto bar = beatsPerBar();
        blocks[(size_t) index].length = juce::jmax (bar, std::round (newLength / bar) * bar);
        settle (index);
    }

    int Song::duplicate (int index)
    {
        if (index < 0 || index >= (int) blocks.size())
            return -1;
        auto copy = blocks[(size_t) index];
        copy.start = blocks[(size_t) index].end();
        blocks.insert (blocks.begin() + index + 1, copy);
        settle (index + 1);
        return index + 1;
    }

    void Song::remove (int index)
    {
        if (index >= 0 && index < (int) blocks.size())
            blocks.erase (blocks.begin() + index);
    }

    std::vector<Hit> Song::events() const
    {
        std::vector<Hit> out;
        for (const auto& b : blocks)
        {
            if (b.muted || b.loopLength <= 0.0)
                continue;
            for (auto repeat = 0.0; repeat < b.length - 1.0e-9; repeat += b.loopLength)
                for (const auto& h : b.hits)
                    if (repeat + h.beat < b.length - 1.0e-9)
                        out.push_back ({ b.start + repeat + h.beat, h.slot, h.velocity });
        }
        std::stable_sort (out.begin(), out.end(), [] (const Hit& a, const Hit& c) { return a.beat < c.beat; });
        return out;
    }

    juce::MidiFile Song::toMidi (double bpm, const std::function<int (int)>& noteFor) const
    {
        constexpr int ticks = 960;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, "Song"));
        track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / juce::jmax (1.0, bpm))));
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (numerator, denominator));
        for (const auto& h : events())
        {
            const auto note = noteFor (h.slot);
            if (note < 0 || note > 127)
                continue;
            const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (h.velocity * 127.0f));
            track.addEvent (juce::MidiMessage::noteOn (10, note, velocity).withTimeStamp (h.beat * ticks));
            track.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (h.beat * ticks + ticks / 16.0));
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp (juce::jmax (endBeat() * ticks, track.getEndTime()));
        track.addEvent (end);
        track.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ticks);
        file.addTrack (track);
        return file;
    }

    std::unique_ptr<juce::XmlElement> Song::toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("Song");
        xml->setAttribute ("num", numerator);
        xml->setAttribute ("den", denominator);
        for (const auto& b : blocks)
        {
            auto* e = xml->createNewChildElement ("Block");
            e->setAttribute ("name", b.name);
            e->setAttribute ("source", b.source.getFullPathName());
            e->setAttribute ("part", (int) b.part);
            e->setAttribute ("start", b.start);
            e->setAttribute ("length", b.length);
            e->setAttribute ("loop", b.loopLength);
            e->setAttribute ("muted", b.muted);
            juce::StringArray hits;   // "beat:slot:velocity"
            for (const auto& h : b.hits)
                hits.add (juce::String (h.beat, 6) + ":" + juce::String (h.slot) + ":" + juce::String (h.velocity, 4));
            e->setAttribute ("hits", hits.joinIntoString (" "));
        }
        return xml;
    }

    Song Song::fromXml (const juce::XmlElement& xml)
    {
        Song s;
        s.numerator = juce::jlimit (1, 32, xml.getIntAttribute ("num", 4));
        s.denominator = juce::jlimit (1, 32, xml.getIntAttribute ("den", 4));
        for (const auto* e : xml.getChildWithTagNameIterator ("Block"))
        {
            Block b;
            b.name = e->getStringAttribute ("name");
            if (const auto path = e->getStringAttribute ("source"); juce::File::isAbsolutePath (path))
                b.source = juce::File (path);
            b.part = (grooves::Part) juce::jlimit (0, grooves::partNames.size() - 1, e->getIntAttribute ("part"));
            b.start = juce::jmax (0.0, e->getDoubleAttribute ("start"));
            b.length = juce::jmax (0.25, e->getDoubleAttribute ("length", 4.0));
            b.loopLength = juce::jmax (0.25, e->getDoubleAttribute ("loop", 4.0));
            b.muted = e->getBoolAttribute ("muted");
            for (const auto& token : juce::StringArray::fromTokens (e->getStringAttribute ("hits"), " ", {}))
            {
                const auto f = juce::StringArray::fromTokens (token, ":", {});
                if (f.size() == 3)
                    b.hits.push_back ({ juce::jmax (0.0, f[0].getDoubleValue()), juce::jlimit (0, synth::drums::numSlots - 1, f[1].getIntValue()),
                                        juce::jlimit (0.0f, 1.0f, f[2].getFloatValue()) });
            }
            s.blocks.push_back (std::move (b));
        }
        s.settle();
        return s;
    }

    Block blockFrom (const grooves::Groove& g, const synth::drums::notemap::Map& layout, grooves::TempoFactor factor, float velocityScale)
    {
        Block b;
        b.name = g.name;
        b.source = g.file;
        b.part = grooves::partOf (g.file);
        const auto scale = 1.0 / grooves::factorOf (factor);
        b.loopLength = juce::jmax (0.25, g.lengthBeats * scale);
        b.length = b.loopLength;
        for (const auto& h : g.hits)
            if (const auto slot = h.note >= 0 && h.note < 128 ? layout[(size_t) h.note] : -1; slot >= 0 && h.beat * scale < b.loopLength)
                b.hits.push_back ({ h.beat * scale, slot, juce::jlimit (0.0f, 1.0f, h.velocity * velocityScale) });
        return b;
    }

    void changeTempo (Block& b, grooves::TempoFactor factor)
    {
        const auto scale = 1.0 / grooves::factorOf (factor);
        b.loopLength *= scale;
        b.length *= scale;
        for (auto& h : b.hits)
            h.beat *= scale;
    }

    //==============================================================================
    // EDITING INSIDE A BLOCK (0.9.0)
    namespace
    {
        bool has (unsigned slots, int slot) noexcept { return slot >= 0 && slot < 32 && (slots & (1u << slot)) != 0; }

        // In time order; on one drum and beat (within a hair) only the loudest kept.
        void tidy (std::vector<Hit>& hits)
        {
            std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b)
            {
                return a.beat != b.beat ? a.beat < b.beat : a.slot != b.slot ? a.slot < b.slot : a.velocity > b.velocity;
            });
            std::vector<Hit> kept;
            for (const auto& h : hits)
            {
                const auto same = std::find_if (kept.rbegin(), kept.rend(), [&h] (const Hit& k)
                {
                    return k.slot == h.slot && std::abs (k.beat - h.beat) < 1.0e-6;
                });
                if (same == kept.rend())
                    kept.push_back (h);
                else
                    same->velocity = std::max (same->velocity, h.velocity);
            }
            std::stable_sort (kept.begin(), kept.end(), [] (const Hit& a, const Hit& b) { return a.beat < b.beat; });
            hits = std::move (kept);
        }
    }

    unsigned slotsOf (unsigned parts) noexcept
    {
        auto slots = 0u;
        for (int k = 0; k < synth::drums::numSlots; ++k)
            if (grooves::inParts (parts, k))
                slots |= 1u << k;
        return slots;
    }

    unsigned slotsIn (const Block& b) noexcept
    {
        auto slots = 0u;
        for (const auto& h : b.hits)
            if (h.slot >= 0 && h.slot < synth::drums::numSlots)
                slots |= 1u << h.slot;
        return slots;
    }

    std::vector<Hit> unrolled (const Block& b)
    {
        std::vector<Hit> out;
        if (b.loopLength <= 0.0)
            return out;
        for (auto repeat = 0.0; repeat < b.length - 1.0e-9; repeat += b.loopLength)
            for (const auto& h : b.hits)
                if (repeat + h.beat < b.length - 1.0e-9)
                    out.push_back ({ repeat + h.beat, h.slot, h.velocity });
        return out;
    }

    void flatten (Block& b)
    {
        b.hits = unrolled (b);
        b.loopLength = b.length;
    }

    void removeDrums (Block& b, unsigned slots)
    {
        std::erase_if (b.hits, [slots] (const Hit& h) { return has (slots, h.slot); });
    }

    void pasteDrums (Block& b, const Block& clip, unsigned slots)
    {
        removeDrums (b, slots);
        if (clip.loopLength > 0.0)
            for (auto repeat = 0.0; repeat < b.loopLength - 1.0e-9; repeat += clip.loopLength)
                for (const auto& h : clip.hits)
                    if (has (slots, h.slot) && repeat + h.beat < b.loopLength - 1.0e-9)
                        b.hits.push_back ({ repeat + h.beat, h.slot, h.velocity });
        tidy (b.hits);
    }

    void quantize (Block& b, double grid, unsigned slots)
    {
        if (grid <= 0.0)
            return;
        for (auto& h : b.hits)
            if (has (slots, h.slot))
            {
                h.beat = std::round (h.beat / grid) * grid;
                if (h.beat >= b.loopLength - 1.0e-9)
                    h.beat = 0.0;   // pushed to the groove's end: its start, the next time round
            }
        tidy (b.hits);
    }

    void scaleVelocity (Block& b, float factor, unsigned slots)
    {
        for (auto& h : b.hits)
            if (has (slots, h.slot))
                h.velocity = juce::jlimit (1.0f / 127.0f, 1.0f, h.velocity * factor);
    }

    void moveDrum (Block& b, int from, int to)
    {
        if (from == to || to < 0 || to >= synth::drums::numSlots)
            return;
        for (auto& h : b.hits)
            if (h.slot == from)
                h.slot = to;
        tidy (b.hits);
    }

    void addHit (Block& b, Hit hit)
    {
        if (hit.beat < 0.0 || hit.beat >= b.loopLength || hit.slot < 0 || hit.slot >= synth::drums::numSlots)
            return;
        std::erase_if (b.hits, [&hit] (const Hit& h) { return h.slot == hit.slot && std::abs (h.beat - hit.beat) < 1.0e-6; });
        b.hits.push_back (hit);
        tidy (b.hits);
    }

    bool removeHitNear (Block& b, int slot, double beat, double within)
    {
        auto best = b.hits.end();
        auto distance = within;
        for (auto it = b.hits.begin(); it != b.hits.end(); ++it)
            if (it->slot == slot && std::abs (it->beat - beat) <= distance)
            {
                distance = std::abs (it->beat - beat);
                best = it;
            }
        if (best == b.hits.end())
            return false;
        b.hits.erase (best);
        return true;
    }

    int Song::split (int index, double atBeat)
    {
        if (index < 0 || index >= (int) blocks.size())
            return -1;
        auto& first = blocks[(size_t) index];
        const auto bar = beatsPerBar();
        const auto cut = first.start + std::round ((atBeat - first.start) / bar) * bar;
        if (cut <= first.start + 1.0e-9 || cut >= first.end() - 1.0e-9)
            return -1;
        flatten (first);
        auto second = first;
        const auto at = cut - first.start;
        first.length = first.loopLength = at;
        std::erase_if (first.hits, [at] (const Hit& h) { return h.beat >= at - 1.0e-9; });
        second.start = cut;
        second.length = second.loopLength = second.length - at;
        std::erase_if (second.hits, [at] (const Hit& h) { return h.beat < at - 1.0e-9; });
        for (auto& h : second.hits)
            h.beat -= at;
        blocks.insert (blocks.begin() + index + 1, std::move (second));
        return index + 1;
    }

    int Song::mergeWithNext (int index)
    {
        if (index < 0 || index + 1 >= (int) blocks.size())
            return -1;
        auto& first = blocks[(size_t) index];
        const auto& next = blocks[(size_t) index + 1];
        auto hits = unrolled (first);
        for (auto h : unrolled (next))
        {
            h.beat += next.start - first.start;
            hits.push_back (h);
        }
        first.length = first.loopLength = next.end() - first.start;
        first.hits = std::move (hits);
        tidy (first.hits);
        blocks.erase (blocks.begin() + index + 1);
        return index;
    }

    juce::Colour colourOf (grooves::Part part) noexcept
    {
        switch (part)
        {
            case grooves::Part::intro: return juce::Colour (0xff8a8fa3);
            case grooves::Part::verse: return juce::Colour (0xff3fb5c4);
            case grooves::Part::preChorus: return juce::Colour (0xffd8c64a);
            case grooves::Part::chorus: return juce::Colour (0xffb26ad0);
            case grooves::Part::bridge: return juce::Colour (0xff7bc25a);
            case grooves::Part::fill: return juce::Colour (0xffff7a1a);
            case grooves::Part::ending: return juce::Colour (0xffe0483e);
            case grooves::Part::countIn: return juce::Colour (0xff6b6569);
            case grooves::Part::beat: break;
        }
        return juce::Colour (0xff4a7fd0);
    }
}
