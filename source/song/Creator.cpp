#include "Creator.h"

#include <set>

namespace song
{
    using grooves::Part;

    int Structure::totalBars() const noexcept
    {
        auto n = 0;
        for (const auto& s : sections)
            n += s.bars;
        return n;
    }

    juce::String encode (const std::vector<Section>& sections)
    {
        juce::StringArray out;
        for (const auto& s : sections)
            out.add (grooves::partNames[(int) s.part] + " " + juce::String (s.bars));
        return out.joinIntoString (", ");
    }

    std::vector<Section> decode (const juce::String& text)
    {
        std::vector<Section> out;
        for (const auto& item : juce::StringArray::fromTokens (text, ",", {}))
        {
            const auto t = item.trim();
            const auto name = t.upToLastOccurrenceOf (" ", false, false).trim();
            const auto bars = t.fromLastOccurrenceOf (" ", false, false).getIntValue();
            for (int k = 0; k < numSongParts; ++k)
                if (grooves::partNames[k].equalsIgnoreCase (name) && bars > 0)
                    out.push_back ({ (Part) k, juce::jmin (bars, 256) });
        }
        return out;
    }

    const std::vector<Structure>& factoryStructures()
    {
        // Common song forms in our own words (A the verse, B the chorus, C the bridge), each with an intro, a fill's bar
        // before each chorus (the section before it a bar shorter, so the bars stay round) and an ending.
        static const std::vector<Structure> structures {
            { "Verse / Chorus (ABAB)", decode ("Intro 4, Verse 7, Fill 1, Chorus 8, Verse 7, Fill 1, Chorus 8, Ending 1") },
            { "With Bridge (ABABCB)", decode ("Intro 4, Verse 7, Fill 1, Chorus 8, Verse 7, Fill 1, Chorus 8, Bridge 7, Fill 1, Chorus 8, Ending 1") },
            { "With Pre-Chorus", decode ("Intro 4, Verse 8, Pre-Chorus 3, Fill 1, Chorus 8, Verse 8, Pre-Chorus 3, Fill 1, Chorus 8, Ending 1") },
            { "AABA", decode ("Intro 4, Verse 8, Verse 7, Fill 1, Chorus 8, Verse 8, Ending 1") },
            { "Chorus First (BABAB)", decode ("Chorus 8, Verse 7, Fill 1, Chorus 8, Verse 7, Fill 1, Chorus 8, Chorus 8, Ending 1") },
            { "12-Bar Three Times", decode ("Verse 12, Verse 11, Fill 1, Verse 12, Ending 1") },
        };
        return structures;
    }

    std::vector<Section> structureOf (const Song& s)
    {
        std::vector<Section> out;
        for (const auto& b : s.blocks)
            out.push_back ({ b.part, juce::jmax (1, juce::roundToInt (b.length / s.beatsPerBarAt (b.start))) });
        return out;
    }

    Columns variationsOf (const juce::File& source, const std::vector<grooves::Entry>& all, const juce::File& root)
    {
        // The song folder: the source's, up a folder at a time while it has fewer than two kinds of song part (a pack
        // keeping its fills in "Fills" next to "Grooves", say), never past the linked folder.
        const auto inside = [&all] (const juce::File& folder)
        {
            std::vector<const grooves::Entry*> found;
            for (const auto& e : all)
                if (e.file.isAChildOf (folder))
                    found.push_back (&e);
            return found;
        };
        const auto kinds = [] (const std::vector<const grooves::Entry*>& entries)
        {
            std::set<Part> parts;
            for (const auto* e : entries)
                parts.insert (e->part);
            return parts.size();
        };
        auto folder = source.getParentDirectory();
        auto found = inside (folder);
        while (kinds (found) < 2 && folder != root && folder.isAChildOf (root))
        {
            folder = folder.getParentDirectory();
            found = inside (folder);
        }
        Columns columns;
        for (const auto* e : found)
            columns[(size_t) e->part].push_back (*e);
        for (auto& c : columns)
            std::stable_sort (c.begin(), c.end(), [&source] (const grooves::Entry& a, const grooves::Entry& b)
            {
                if ((a.file == source) != (b.file == source))
                    return a.file == source;
                return a.name.compareNatural (b.name) < 0;
            });
        return columns;
    }

    int standInFor (Part part, const std::array<int, numSongParts>& counts) noexcept
    {
        if (counts[(size_t) part] > 0)
            return (int) part;
        std::vector<Part> order;   // (a vector: a braced list assigned to an initializer_list wouldn't outlive the switch)
        switch (part)
        {
            case Part::intro:     order = { Part::verse, Part::beat, Part::chorus }; break;
            case Part::verse:     order = { Part::beat, Part::chorus, Part::bridge }; break;
            case Part::preChorus: order = { Part::verse, Part::beat, Part::chorus }; break;
            case Part::chorus:    order = { Part::verse, Part::beat, Part::bridge }; break;
            case Part::bridge:    order = { Part::chorus, Part::verse, Part::beat }; break;
            case Part::ending:    order = { Part::fill, Part::chorus, Part::verse, Part::beat }; break;
            case Part::beat:      order = { Part::verse, Part::chorus, Part::bridge }; break;
            case Part::fill: case Part::countIn: return -1;
        }
        for (const auto p : order)
            if (counts[(size_t) p] > 0)
                return (int) p;
        // Anything with a groove, the fills and count-ins last.
        for (const auto p : { Part::verse, Part::chorus, Part::beat, Part::bridge, Part::preChorus, Part::intro, Part::ending, Part::fill })
            if (counts[(size_t) p] > 0)
                return (int) p;
        return -1;
    }

    std::vector<Pick> pick (const std::vector<Section>& sections, const std::array<int, numSongParts>& counts)
    {
        std::array<int, numSongParts> next {};
        std::vector<Pick> out;
        for (const auto& s : sections)
        {
            const auto column = standInFor (s.part, counts);
            if (column < 0)
            {
                out.push_back ({});
                continue;
            }
            // Each time a column is used, its next groove (a verse standing in for the intro, then the verse: two
            // different verses).
            auto& n = next[(size_t) column];
            out.push_back ({ column, n % counts[(size_t) column] });
            ++n;
        }
        return out;
    }

    void replaceGroove (Block& target, const Block& with)
    {
        target.name = with.name;
        target.source = with.source;
        target.hits = with.hits;
        target.loopLength = with.loopLength;
    }

    std::vector<Structure> loadStructures (const juce::File& file)
    {
        std::vector<Structure> out;
        if (const auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("SongStructures"))
            for (const auto* e : xml->getChildWithTagNameIterator ("Structure"))
                if (auto sections = decode (e->getStringAttribute ("sections")); ! sections.empty())
                    out.push_back ({ e->getStringAttribute ("name", "Mine"), std::move (sections), true });
        return out;
    }

    bool saveStructures (const juce::File& file, const std::vector<Structure>& structures)
    {
        juce::XmlElement xml ("SongStructures");
        for (const auto& s : structures)
        {
            auto* e = xml.createNewChildElement ("Structure");
            e->setAttribute ("name", s.name);
            e->setAttribute ("sections", encode (s.sections));
        }
        file.getParentDirectory().createDirectory();
        return xml.writeTo (file);
    }
}
