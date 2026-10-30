#include "Rhythm.h"

#include <set>

namespace grooves
{
    using namespace synth::drums::notemap;

    int laneOf (int slot) noexcept
    {
        switch (slot)
        {
            case BD: return (int) Lane::kick;
            case SD: case SD2: case RIM: return (int) Lane::snare;
            case CH: case OH: case HH: return (int) Lane::hats;
            case RD: case RB: return (int) Lane::ride;
            case LT: case MT: case HT: return (int) Lane::toms;
            case CY: case CR2: case CHN: case SPL: return (int) Lane::crash;
            default: return -1;   // the pedal hat (a foot), the clap, percussion
        }
    }

    int slotOfLane (int lane) noexcept
    {
        constexpr int slots[] = { BD, SD, CH, RD, MT, CY };
        return lane >= 0 && lane < numLanes ? slots[lane] : -1;
    }

    juce::String Rhythm::encode() const
    {
        juce::StringArray lanes;
        for (int l = 0; l < numLanes; ++l)
        {
            juce::StringArray at;
            for (const auto b : beats[(size_t) l])
                if (b < maxBeats)
                    at.add (juce::String (juce::roundToInt (b * 12.0)));
            if (! at.isEmpty())
                lanes.add (juce::String (l) + ":" + at.joinIntoString (","));
        }
        return juce::String (lengthBeats, 4) + ";" + juce::String (beatsPerBar, 4) + ";" + lanes.joinIntoString ("|");
    }

    Rhythm Rhythm::decode (const juce::String& text)
    {
        Rhythm r;
        const auto parts = juce::StringArray::fromTokens (text, ";", {});
        if (parts.size() < 3)
            return r;
        r.lengthBeats = juce::jmax (0.25, parts[0].getDoubleValue());
        r.beatsPerBar = juce::jmax (0.25, parts[1].getDoubleValue());
        for (const auto& lane : juce::StringArray::fromTokens (parts[2], "|", {}))
        {
            const auto l = lane.upToFirstOccurrenceOf (":", false, false).getIntValue();
            if (l < 0 || l >= numLanes)
                continue;
            for (const auto& t : juce::StringArray::fromTokens (lane.fromFirstOccurrenceOf (":", false, false), ",", {}))
                r.beats[(size_t) l].push_back (t.getIntValue() / 12.0);
            std::sort (r.beats[(size_t) l].begin(), r.beats[(size_t) l].end());
        }
        return r;
    }

    Rhythm rhythmOf (const Groove& g, const Map& layout)
    {
        Rhythm r;
        r.lengthBeats = g.lengthBeats;
        r.beatsPerBar = g.beatsPerBar();
        for (const auto& h : g.hits)
        {
            if (h.beat >= Rhythm::maxBeats || h.note < 0 || h.note > 127)
                continue;
            if (const auto lane = laneOf (layout[(size_t) h.note]); lane >= 0)
                r.beats[(size_t) lane].push_back (h.beat);
        }
        return r;
    }

    Rhythm firstBars (const Rhythm& r, double lengthBeats)
    {
        Rhythm out;
        out.lengthBeats = lengthBeats;
        out.beatsPerBar = r.beatsPerBar;
        for (int l = 0; l < numLanes; ++l)
            for (const auto b : r.beats[(size_t) l])
                if (b < lengthBeats)
                    out.beats[(size_t) l].push_back (b);
        return out;
    }

    namespace
    {
        // A group's hits in [start, start + length) of a looping rhythm, as grid positions from `start`.
        std::set<int> onGrid (const std::vector<double>& beats, double loopLength, double start, double length, double grid)
        {
            std::set<int> cells;
            const auto numCells = juce::jmax (1, juce::roundToInt (length / grid));
            if (beats.empty() || loopLength <= 0.0)
                return cells;
            // Every time round the loop that reaches into the window.
            for (auto cycle = std::floor (start / loopLength) * loopLength; cycle < start + length; cycle += loopLength)
                for (const auto b : beats)
                {
                    if (b >= loopLength)
                        continue;
                    const auto t = cycle + b - start;
                    // Just under the window's end rounds round to its start (a tap a hair early for beat 1).
                    if (t < -0.5 * grid || t >= length)
                        continue;
                    cells.insert (((juce::roundToInt (t / grid) % numCells) + numCells) % numCells);
                }
            return cells;
        }

        double overlap (const std::set<int>& a, const std::set<int>& b)
        {
            if (a.empty() || b.empty())
                return 0.0;
            auto both = 0;
            for (const auto c : a)
                both += (int) b.count (c);
            return 2.0 * both / (double) (a.size() + b.size());
        }
    }

    double match (const Rhythm& query, const Rhythm& groove, double grid)
    {
        if (query.isEmpty() || groove.isEmpty() || grid <= 0.0)
            return 0.0;
        const auto length = query.lengthBeats;
        std::array<std::set<int>, numLanes> tapped;
        auto totalWeight = 0.0;
        for (int l = 0; l < numLanes; ++l)
        {
            tapped[(size_t) l] = onGrid (query.beats[(size_t) l], query.lengthBeats, 0.0, length, grid);
            totalWeight += (double) tapped[(size_t) l].size();
        }
        if (totalWeight <= 0.0)
            return 0.0;

        auto best = 0.0;
        const auto step = juce::jmax (0.25, groove.beatsPerBar);
        const auto hats = (size_t) Lane::hats, ride = (size_t) Lane::ride;
        for (auto start = 0.0; start < groove.lengthBeats - 1.0e-9; start += step)
        {
            std::array<std::set<int>, numLanes> has;
            for (int l = 0; l < numLanes; ++l)
                if (! tapped[(size_t) l].empty() || l == (int) hats || l == (int) ride)
                    has[(size_t) l] = onGrid (groove.beats[(size_t) l], groove.lengthBeats, start, length, grid);
            // The riding hand: a tapped hi-hat is matched against the ride if the groove has no hats there, and back.
            if (has[hats].empty())
                has[hats] = has[ride];
            else if (has[ride].empty())
                has[ride] = has[hats];
            auto score = 0.0;
            for (int l = 0; l < numLanes; ++l)
                if (! tapped[(size_t) l].empty())
                    score += (double) tapped[(size_t) l].size() * overlap (tapped[(size_t) l], has[(size_t) l]);
            best = juce::jmax (best, score / totalWeight);
        }
        return best;
    }
}
