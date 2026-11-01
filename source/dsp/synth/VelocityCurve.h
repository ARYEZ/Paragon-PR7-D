#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <array>

// A VELOCITY CURVE (PR7-D 0.18.0): how hard a hit comes in (0 to 1, a note's velocity / 127) turned into how hard it
// plays. Inspired by drum software's velocity curve box; our own design. A line through up to 8 points, left to right:
//   - the first and last points are the LIMITS: below the first's input every hit plays at its output (or, with GATE,
//     doesn't play at all); above the last's, at its output. Dragging them in narrows what comes out (e.g. the left
//     one up: soft hits come out harder) or what goes in;
//   - the points between bend it (e.g. 91 in comes out 114);
//   - SOFT rounds each stretch between points into an S, so the response has no corners (our reading of the
//     reference's "soft curve": its manual says only that it curves the response).
// New: a straight line from (0, 0) to (1, 1), which changes nothing.
namespace synth::drums
{
    struct VelocityCurve
    {
        static constexpr int maxPoints = 8;
        std::array<juce::Point<float>, maxPoints> points { { { 0.0f, 0.0f }, { 1.0f, 1.0f } } };
        int count = 2;
        bool soft = false, gate = false;

        bool isStraight() const noexcept
        {
            return count == 2 && ! soft && ! gate && points[0] == juce::Point<float> (0.0f, 0.0f) && points[1] == juce::Point<float> (1.0f, 1.0f);
        }

        // The velocity a hit plays at, 0 to 1; -1: gated (it doesn't play).
        float apply (float in) const noexcept
        {
            in = juce::jlimit (0.0f, 1.0f, in);
            if (count < 2)
                return in;
            const auto& first = points[0];
            const auto& last = points[(size_t) count - 1];
            if (in < first.x)
                return gate ? -1.0f : first.y;
            if (in >= last.x)
                return last.y;
            for (int i = 1; i < count; ++i)
            {
                const auto& a = points[(size_t) i - 1];
                const auto& b = points[(size_t) i];
                if (in <= b.x)
                {
                    auto t = b.x > a.x ? (in - a.x) / (b.x - a.x) : 1.0f;
                    if (soft)
                        t = t * t * (3.0f - 2.0f * t);
                    return juce::jlimit (0.0f, 1.0f, a.y + (b.y - a.y) * t);
                }
            }
            return last.y;
        }

        // A point added between its neighbours (as a double-click on the line); its index, or -1 if full.
        int add (juce::Point<float> p)
        {
            if (count >= maxPoints)
                return -1;
            p.x = juce::jlimit (points[0].x, points[(size_t) count - 1].x, p.x);
            int at = 1;
            while (at < count - 1 && points[(size_t) at].x < p.x)
                ++at;
            for (int i = count; i > at; --i)
                points[(size_t) i] = points[(size_t) i - 1];
            points[(size_t) at] = { p.x, juce::jlimit (0.0f, 1.0f, p.y) };
            ++count;
            return at;
        }
        void remove (int index)
        {
            if (index <= 0 || index >= count - 1)
                return;   // the limits stay
            for (int i = index; i < count - 1; ++i)
                points[(size_t) i] = points[(size_t) i + 1];
            --count;
        }
        // A point moved, kept between its neighbours (and within 0 to 1).
        void move (int index, juce::Point<float> p)
        {
            if (index < 0 || index >= count)
                return;
            const auto lo = index > 0 ? points[(size_t) index - 1].x : 0.0f;
            const auto hi = index < count - 1 ? points[(size_t) index + 1].x : 1.0f;
            points[(size_t) index] = { juce::jlimit (lo, hi, p.x), juce::jlimit (0.0f, 1.0f, p.y) };
        }

        bool operator== (const VelocityCurve& o) const noexcept
        {
            if (count != o.count || soft != o.soft || gate != o.gate)
                return false;
            for (int i = 0; i < count; ++i)
                if (points[(size_t) i] != o.points[(size_t) i])
                    return false;
            return true;
        }

        // As text: "x,y x,y ..." then " soft" / " gate" (velocities as 0 to 127, to 1 / 1000).
        juce::String encode() const
        {
            juce::StringArray items;
            for (int i = 0; i < count; ++i)
                items.add (juce::String (points[(size_t) i].x * 127.0f, 3) + "," + juce::String (points[(size_t) i].y * 127.0f, 3));
            if (soft)
                items.add ("soft");
            if (gate)
                items.add ("gate");
            return items.joinIntoString (" ");
        }
        static VelocityCurve decode (const juce::String& text)
        {
            VelocityCurve c;
            int n = 0;
            for (const auto& item : juce::StringArray::fromTokens (text, " ", {}))
            {
                if (item == "soft")
                    c.soft = true;
                else if (item == "gate")
                    c.gate = true;
                else if (item.containsChar (',') && n < maxPoints)
                {
                    const auto x = juce::jlimit (0.0f, 1.0f, item.upToFirstOccurrenceOf (",", false, false).getFloatValue() / 127.0f);
                    const auto y = juce::jlimit (0.0f, 1.0f, item.fromFirstOccurrenceOf (",", false, false).getFloatValue() / 127.0f);
                    if (n > 0 && x < c.points[(size_t) n - 1].x)
                        continue;   // out of order: skipped
                    c.points[(size_t) n++] = { x, y };
                }
            }
            c.count = n >= 2 ? n : 2;
            if (n < 2)
                c.points = { { { 0.0f, 0.0f }, { 1.0f, 1.0f } } };
            return c;
        }
    };
}
