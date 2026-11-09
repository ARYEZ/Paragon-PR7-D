#include "MixPack.h"

#include "NoteMap.h"

namespace synth::drums::mixpack
{
    namespace
    {
        const juce::StringArray mixWords { "CANON", "GUNFIRE", "PUNCHY", "TUNED", "UNTUNED", "VERB" };   // (sorted)

        juce::StringArray tokens (juce::String s)
        {
            s = s.toUpperCase();
            for (const auto c : juce::String ("()-_.,+"))
                s = s.replaceCharacter (c, ' ');
            juce::StringArray t;
            t.addTokens (s, " ", "");
            t.removeEmptyStrings();
            return t;
        }

        bool has (const juce::String& s, const char* word) { return s.contains (word); }

        // The first whole number in `s` after `word` ("TOM 3" -> 3), 0 if none.
        int numberAfter (const juce::String& s, const juce::String& word)
        {
            const auto at = s.indexOf (word);
            if (at < 0)
                return 0;
            auto rest = s.substring (at + word.length()).trimStart();
            return juce::CharacterFunctions::isDigit (rest[0]) ? rest.getIntValue() : 0;
        }

        // The size of a crash (the number before CRASH in a folder's name: "17 CRASH - ORIGINAL"), 0 if none.
        int crashSize (const juce::StringArray& parts)
        {
            for (const auto& p : parts)
                if (p.contains ("CRASH") && juce::CharacterFunctions::isDigit (p.trimStart()[0]))
                    return p.trimStart().getIntValue();
            return 0;
        }
    }

    bool isAudioFile (const juce::File& f)
    {
        return f.existsAsFile() && ! f.getFileName().startsWith ("._")
            && f.hasFileExtension ("wav;aif;aiff;flac;ogg") && f.getSize() > 64;
    }

    bool isParallel (const juce::String& folderName) { return folderName.toUpperCase().contains ("PARALLEL"); }

    MixKey keyOf (const juce::String& folderName)
    {
        auto name = folderName.trim();
        // The order number in front ("04 - ") isn't the mix's.
        if (juce::CharacterFunctions::isDigit (name[0]) && name.indexOf (" - ") > 0 && name.indexOf (" - ") <= 3)
            name = name.fromFirstOccurrenceOf (" - ", false, false);
        MixKey key;
        for (const auto& t : tokens (name))
        {
            if (key.number == 0 && juce::CharacterFunctions::isDigit (t[0]))
                key.number = t.getIntValue();   // ("2B": 2)
            else if (mixWords.contains (t) && ! key.words.contains (t))
                key.words.add (t);
        }
        key.words.sort (false);
        return key;
    }

    MixKey keyOfChoice (int mix)
    {
        static const std::vector<MixKey> keys {
            { 1, {} }, { 1, { "PUNCHY" } }, { 2, {} }, { 2, { "CANON" } }, { 2, { "CANON", "VERB" } },
            { 2, { "GUNFIRE" } }, { 2, { "GUNFIRE", "VERB" } } };
        return keys[(size_t) juce::jlimit (0, (int) keys.size() - 1, mix)];
    }

    int slotFor (const juce::StringArray& parts, const juce::Array<int>& crashSizes, int& kick)
    {
        using namespace notemap;
        const auto all = parts.joinIntoString (" / ");
        kick = 0;
        if (has (all, "KICK") || has (all, "BASS DRUM"))
        {
            // Its number from the folder that has one ("KICK 2"; the top "KICK" folder has none).
            for (const auto& p : parts)
                kick = juce::jmax (kick, numberAfter (p, "KICK"));
            kick = juce::jmax (1, kick);
            return BD;
        }
        if (has (all, "SNARE"))
        {
            if (has (all, "ALT") || has (all, "EXTRA"))
                return -1;
            if (has (all, "SIDESTICK") || has (all, "SIDE STICK") || has (all, "CROSS"))
                return RIM;
            if (has (all, "RIMSHOT") || has (all, "RIM SHOT"))
                return SD2;
            if (has (all, "CENTER") || has (all, "CENTRE"))
                return SD;
            return -1;   // (the edge: no slot of its own)
        }
        // (a folder named TOM / TOMS / TOM 3: "STOMP", a hat's pedal, isn't one)
        if (std::any_of (parts.begin(), parts.end(), [] (const juce::String& p) { return p.startsWith ("TOM"); }))
        {
            for (const auto& p : parts)
                if (p.startsWith ("TOM"))
                    switch (numberAfter (p, "TOM"))
                    {
                        case 1: return HT;
                        case 2: return MT;
                        case 3: return LT;
                        case 4: return T4;
                        case 5: return T5;
                        default: break;
                    }
            return -1;
        }
        if (has (all, "HAT"))
        {
            if (has (all, "TIGHTER"))
                return -1;
            if (has (all, "PEDAL") || has (all, "STOMP") || has (all, "FOOT"))
                return PH;
            if (has (all, "CLOSED"))
                return CH;
            if (has (all, "25%"))
                return HO1;
            if (has (all, "50%"))
                return HH;
            if (has (all, "75%"))
                return HO3;
            if (has (all, "OPEN"))
                return OH;
            return -1;
        }
        if (has (all, "RIDE"))
        {
            if (has (all, "BELL"))
                return RB;
            if (has (all, "EDGE"))
                return RE;
            if (has (all, "CENTER") || has (all, "CENTRE") || has (all, "BOW") || has (all, "TIP"))
                return RD;
            return -1;
        }
        const auto choke = has (all, "CHOKE");
        if (has (all, "CHINA"))
            return choke ? CHC : (has (all, "CENTER") ? -1 : CHN);
        if (has (all, "SPLASH"))
            return choke ? SPC : (has (all, "CENTER") ? -1 : SPL);
        if (has (all, "CRASH"))
        {
            // The crashes by size, smallest first: CRASH, CRASH 2, CRASH 3...; the edge their hit (the centre none:
            // a crash is mostly played on its edge), the chokes the first two's.
            const auto index = juce::jmax (0, crashSizes.indexOf (crashSize (parts)));
            if (choke)
                return index == 0 ? C1C : index == 1 ? C2C : -1;
            if (! has (all, "EDGE"))
                return -1;
            static const int mains[] { CY, CR2, C3, C4, C5, C6 };
            return index < 6 ? mains[index] : -1;
        }
        return -1;
    }

    Plan plan (const juce::File& folder, Version version, int mix)
    {
        Plan result;
        result.name = folder.getFileName();
        // Every folder with audio in it (a "leaf": one mix of one articulation), by its path from the pack's folder.
        std::vector<std::pair<juce::File, juce::StringArray>> leaves;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findDirectories))
        {
            const auto dir = entry.getFile();
            if (dir.getFileName().startsWith ("."))
                continue;
            auto any = false;
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, false))
                if (isAudioFile (f))
                {
                    any = true;
                    break;
                }
            if (! any)
                continue;
            juce::StringArray parts;
            parts.addTokens (dir.getRelativePathFrom (folder).toUpperCase(), juce::File::getSeparatorString(), "");
            parts.removeEmptyStrings();
            // The version asked for (a pack with neither word in a path: every folder).
            const auto all = parts.joinIntoString (" / ");
            const auto boosted = all.contains ("GAIN BOOSTED") || all.contains ("BOOSTED");
            const auto original = all.contains ("ORIGINAL");
            if ((version == Version::original && boosted) || (version == Version::boosted && original && ! boosted))
                continue;
            leaves.emplace_back (dir, parts);
        }
        std::sort (leaves.begin(), leaves.end(), [] (const auto& a, const auto& b) { return a.first.getFullPathName() < b.first.getFullPathName(); });

        juce::Array<int> crashSizes;
        for (const auto& [dir, parts] : leaves)
            if (parts.joinIntoString (" ").contains ("CRASH"))
                crashSizes.addIfNotAlreadyThere (crashSize (parts));
        crashSizes.sort();

        // Grouped by what they play (the kick by its number), each group's mix folders and its PARALLEL one.
        struct Group { std::vector<juce::File> mixes; juce::File parallel; };
        std::map<std::pair<int, int>, Group> groups;
        for (const auto& [dir, parts] : leaves)
        {
            auto piece = parts;
            piece.remove (piece.size() - 1);   // (the mix folder itself)
            int kick = 0;
            const auto slot = slotFor (piece, crashSizes, kick);
            if (slot < 0)
            {
                // Named by its piece (a crash by its size) and the folder above the mix: "SNARE 2 - EDGE",
                // "CYMBALS 17 CRASH CENTER".
                auto label = piece[0];
                for (const auto& p : piece)
                    if (p.contains ("CRASH"))
                        label << " " << p.upToFirstOccurrenceOf (" - ", false, false);
                label << " " << piece[piece.size() - 1];
                result.unused.addIfNotAlreadyThere (label.trim());
                continue;
            }
            auto& g = groups[{ slot, kick }];
            if (isParallel (dir.getFileName()))
            {
                if (g.parallel == juce::File())
                    g.parallel = dir;
            }
            else
                g.mixes.push_back (dir);
        }

        // Each group's mix: the one asked for, else the nearest (the same number with the asked words and the fewest
        // others), else MIX 1, else the first (a drum with one mix).
        const auto want = keyOfChoice (mix);
        const auto choose = [&want] (const std::vector<juce::File>& mixes) -> juce::File
        {
            for (const auto& m : mixes)
                if (keyOf (m.getFileName()) == want)
                    return m;
            juce::File best;
            auto fewest = 1000;
            for (const auto& m : mixes)
            {
                const auto k = keyOf (m.getFileName());
                auto all = k.number == want.number;
                for (const auto& w : want.words)
                    all = all && k.words.contains (w);
                if (all && k.words.size() < fewest)
                {
                    fewest = k.words.size();
                    best = m;
                }
            }
            if (best != juce::File())
                return best;
            for (const auto& m : mixes)
                if (keyOf (m.getFileName()) == keyOfChoice (0))
                    return m;
            return mixes.empty() ? juce::File() : mixes.front();
        };
        for (const auto& [key, g] : groups)
        {
            const auto chosen = choose (g.mixes);
            if (chosen == juce::File())
                continue;
            const auto [slot, kick] = key;
            if (slot == notemap::BD && kick > 1)
            {
                if (result.secondKick == juce::File())
                    result.secondKick = chosen;
                continue;
            }
            if (result.slots.count (slot) > 0)
                continue;
            result.slots[slot] = { chosen, g.parallel, chosen.getFileName() };
        }
        return result;
    }
}
