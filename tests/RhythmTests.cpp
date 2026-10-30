// TAP TO FIND (PR7-D 0.6.0): a groove's rhythm by drum group, and how close a tapped rhythm is to a groove's.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "grooves/Rhythm.h"

using Catch::Matchers::WithinAbs;

namespace
{
    using grooves::Lane;
    using grooves::Rhythm;

    Rhythm rhythm (std::initializer_list<std::pair<Lane, std::vector<double>>> lanes, double length = 8.0, double perBar = 4.0)
    {
        Rhythm r;
        r.lengthBeats = length;
        r.beatsPerBar = perBar;
        for (const auto& [lane, beats] : lanes)
            r.beats[(size_t) lane] = beats;
        return r;
    }

    std::vector<double> every (double step, double length, double offset = 0.0)
    {
        std::vector<double> b;
        for (auto t = offset; t < length - 1e-9; t += step)
            b.push_back (t);
        return b;
    }

    // A 2-bar rock beat: kick on 1 and 3 (and the "and" of 3 in bar 2), snare on 2 and 4, eighth hats.
    Rhythm rock (Lane hand = Lane::hats)
    {
        return rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } }, { hand, every (0.5, 8.0) } });
    }
    constexpr double eighth = 0.5;
}

TEST_CASE ("TAP: the same rhythm matches fully; only the groups tapped count", "[tap]")
{
    CHECK_THAT (grooves::match (rock(), rock(), eighth), WithinAbs (1.0, 1e-9));
    // Kick and snare tapped: the groove's hats don't matter.
    const auto kickSnare = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (kickSnare, rock(), eighth), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("TAP: closer rhythms score higher", "[tap]")
{
    const auto tapped = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    // One kick different (no 6.5): close. Kicks on every beat: further. Snare on 1 and 3 instead: far.
    const auto oneOff = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    const auto fourFloor = rhythm ({ { Lane::kick, every (1.0, 8.0) }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    const auto swapped = rhythm ({ { Lane::kick, { 1.0, 3.0, 5.0, 7.0 } }, { Lane::snare, { 0.0, 2.0, 4.0, 6.0 } } });
    const auto a = grooves::match (tapped, oneOff, eighth), b = grooves::match (tapped, fourFloor, eighth), c = grooves::match (tapped, swapped, eighth);
    INFO ("one off " << a << ", four on the floor " << b << ", swapped " << c);
    CHECK (a > b);
    CHECK (b > c);
    CHECK (a > 0.9);
    CHECK (c < 0.2);
}

TEST_CASE ("TAP: QUANTIZE: taps a little off land on the grid", "[tap]")
{
    // Tapped late and early by up to a sixteenth's half: on the 1/8 grid, the same.
    const auto loose = rhythm ({ { Lane::kick, { 0.06, 1.97, 4.1, 5.92, 6.55 } }, { Lane::snare, { 1.08, 2.93, 5.0, 7.1 } } });
    CHECK_THAT (grooves::match (loose, rock(), eighth), WithinAbs (1.0, 1e-9));
    // A tap a hair early for beat 1 (at the loop's end) counts on beat 1.
    const auto early = rhythm ({ { Lane::kick, { 7.95, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (early, rock(), eighth), WithinAbs (1.0, 1e-9));
    // On the 1/16 grid an off-by-a-sixteenth tap is a miss.
    const auto offSixteenth = rhythm ({ { Lane::kick, { 0.25, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK (grooves::match (offSixteenth, rock(), 0.25) < 1.0);
}

TEST_CASE ("TAP: HI-HAT and RIDE stand in for each other (the riding hand)", "[tap]")
{
    const auto hats = rhythm ({ { Lane::hats, every (0.5, 8.0) } });
    CHECK_THAT (grooves::match (hats, rock (Lane::ride), eighth), WithinAbs (1.0, 1e-9));
    const auto ride = rhythm ({ { Lane::ride, every (0.5, 8.0) } });
    CHECK_THAT (grooves::match (ride, rock (Lane::hats), eighth), WithinAbs (1.0, 1e-9));
    // A groove with both: each is itself.
    auto both = rock (Lane::hats);
    both.beats[(size_t) Lane::ride] = every (1.0, 8.0);
    CHECK (grooves::match (ride, both, eighth) < 0.8);
}

TEST_CASE ("TAP: a groove is tried from each of its bars, and loops when shorter", "[tap]")
{
    // A 4-bar groove: bars 1-2 just quarter kicks; bars 3-4 the rock beat. The tap finds bars 3-4.
    auto four = rhythm ({ { Lane::kick, every (1.0, 8.0) } }, 16.0);
    const auto beat = rock();   // held: a loop over part of a temporary would read it after it's gone
    for (const auto b : beat.beats[(size_t) Lane::kick]) four.beats[(size_t) Lane::kick].push_back (8.0 + b);
    for (const auto b : beat.beats[(size_t) Lane::snare]) four.beats[(size_t) Lane::snare].push_back (8.0 + b);
    std::sort (four.beats[(size_t) Lane::kick].begin(), four.beats[(size_t) Lane::kick].end());
    const auto kickSnare = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0, 6.5 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (kickSnare, four, eighth), WithinAbs (1.0, 1e-9));
    // A one-bar groove, looped, against a two-bar tap of the same bar twice.
    const auto oneBar = rhythm ({ { Lane::kick, { 0.0, 2.0 } }, { Lane::snare, { 1.0, 3.0 } } }, 4.0);
    const auto twice = rhythm ({ { Lane::kick, { 0.0, 2.0, 4.0, 6.0 } }, { Lane::snare, { 1.0, 3.0, 5.0, 7.0 } } });
    CHECK_THAT (grooves::match (twice, oneBar, eighth), WithinAbs (1.0, 1e-9));
}

TEST_CASE ("TAP: nothing tapped, or nothing in the groove: 0", "[tap]")
{
    CHECK (grooves::match (Rhythm(), rock(), eighth) == 0.0);
    CHECK (grooves::match (rock(), Rhythm(), eighth) == 0.0);
    const auto toms = rhythm ({ { Lane::toms, { 0.0, 1.0 } } });
    CHECK (grooves::match (toms, rock(), eighth) == 0.0);   // the groove has no toms
}

TEST_CASE ("TAP: a groove's rhythm through its map, kept as text to the twelfth of a beat", "[tap]")
{
    grooves::Groove g;
    g.lengthBeats = 8.0;
    g.hits = { { 0.0, 36, 1.0f }, { 1.0, 38, 1.0f }, { 1.0 / 3.0, 22, 1.0f }, { 2.0, 51, 1.0f }, { 3.0, 49, 1.0f },
               { 3.5, 44, 1.0f }, { 40.0, 36, 1.0f } };
    const auto r = grooves::rhythmOf (g, synth::drums::notemap::grooveLibrary());
    CHECK (r.beats[(size_t) Lane::kick] == std::vector<double> { 0.0 });   // the one past 32 beats left out
    CHECK (r.beats[(size_t) Lane::snare] == std::vector<double> { 1.0 });
    CHECK (r.beats[(size_t) Lane::hats].size() == 1);                        // 22: the closed hat; 44 (pedal) not a lane
    CHECK (r.beats[(size_t) Lane::ride] == std::vector<double> { 2.0 });
    CHECK (r.beats[(size_t) Lane::crash] == std::vector<double> { 3.0 });
    const auto back = Rhythm::decode (r.encode());
    CHECK_THAT (back.beats[(size_t) Lane::hats][0], WithinAbs (1.0 / 3.0, 1e-9));
    CHECK_THAT (back.lengthBeats, WithinAbs (8.0, 1e-9));
    CHECK (back.numHits() == r.numHits());
    CHECK (grooves::firstBars (r, 2.0).numHits() == 3);   // kick, triplet hat, snare
}
