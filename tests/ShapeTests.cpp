// SHAPE (stage 3b): SKEW, PINCH, FORMANT and CRUSH, measured on the sound.
#include "SynthTestUtils.h"

#include "dsp/synth/WaveBend.h"
#include "dsp/synth/Wavetable.h"

namespace
{
    // One note of oscillator 1 (a sine unless said), shaped by `setup`.
    template <typename Setup>
    std::vector<float> play (int note, double seconds, Setup setup, synth::OscWave wave = synth::OscWave::sine)
    {
        Player p;
        p.osc().wave = wave;
        setup (p.osc());
        p.on (note);
        return p.render (seconds)[0];
    }

    // The same shape done the plain way: read from the richest table level
    // with no smoothing of edges (what the anti-aliasing is measured against).
    std::vector<float> naive (const synth::OscSettings& s, double hz, size_t length)
    {
        synth::WarpTable warp;
        warp.update (s.skew, s.pinch);
        const auto bend = synth::WaveBend::make (s, warp);
        const auto* table = synth::tables::get (s.wave == synth::OscWave::sine ? synth::tables::Wave::sine : synth::tables::Wave::saw, 0);
        const auto read = [table] (float x) { return synth::tables::read (table, x); };
        std::vector<float> out (length);
        double phase = 0.0;
        for (auto& x : out)
        {
            auto v = bend.stepped ((float) phase, read);
            if (bend.levels > 0)
                v = (float) ((std::floor (v * bend.levels) + 0.5) / bend.levels) * bend.bitGain;
            x = v;
            phase += hz / sampleRate;
            phase -= std::floor (phase);
        }
        return out;
    }

    // Where in each cycle (0 to 1) the wave peaks, on average, measured from its rising zero crossings.
    double peakPosition (const std::vector<float>& x, double hz, size_t from, size_t to)
    {
        const auto period = sampleRate / hz;
        double total = 0.0;
        int cycles = 0;
        for (auto i = from + 1; i + (size_t) period < to; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                auto best = i;
                for (auto j = i; j < i + (size_t) period; ++j)
                    if (x[j] > x[best])
                        best = j;
                total += (double) (best - i) / period;
                ++cycles;
                i += (size_t) (period * 0.9);
            }
        return cycles > 0 ? total / cycles : -1.0;
    }

    // The steepest fall from one sample to the next (a wave's slope where it's steepest going down).
    double steepestFall (const std::vector<float>& x, size_t from, size_t to)
    {
        double steepest = 0.0;
        for (auto i = from + 1; i < to; ++i)
            steepest = std::max (steepest, (double) (x[i - 1] - x[i]));
        return steepest;
    }
}

TEST_CASE ("SHAPE at its neutral settings is off: nothing is bent", "[shape]")
{
    synth::OscSettings s;
    CHECK_FALSE (s.hasBend());
    synth::WarpTable warp;
    warp.update (0.0f, 0.0f);
    CHECK_FALSE (synth::WaveBend::make (s, warp).active);
    CHECK (warp.identity);
    for (float u = 0.0f; u <= 1.0f; u += 0.125f)
        CHECK (juce::exactlyEqual (warp (u), u));
    s.skew = 0.1f;
    CHECK (s.hasBend());
    warp.update (0.1f, 0.0f);
    CHECK_FALSE (warp.identity);
    warp.update (0.0f, 0.0f);   // and back
    CHECK (warp.identity);
}

TEST_CASE ("SKEW squashes one side of the wave: the sine's peak comes earlier (right) or later (left)", "[shape]")
{
    const auto at = [] (float skew)
    {
        return peakPosition (play (45, 1.0, [skew] (synth::OscSettings& o) { o.skew = skew; }), 110.0, 12000, 48000);
    };
    const auto plain = at (0.0f);
    CHECK_THAT (plain, WithinAbs (0.25, 0.01));        // a sine peaks a quarter of the way through
    CHECK (at (0.6f) < 0.17);                          // squashed to the left: sooner
    CHECK (at (-0.6f) > 0.33);                         // the other way: later
    CHECK (at (0.3f) < plain);                         // and by less for less
    CHECK (at (0.3f) > at (0.6f));

    // The curves keep their ends, never run backwards, and are smooth (no
    // corner where the cycle joins round), at full SKEW and PINCH either way.
    for (const auto [skew, pinch] : { std::pair { 1.0f, 0.0f }, std::pair { -1.0f, 0.0f }, std::pair { 0.0f, 1.0f }, std::pair { 0.0f, -1.0f },
                                      std::pair { 0.7f, -0.7f } })
    {
        INFO (skew << " " << pinch);
        synth::WarpTable warp;
        warp.update (skew, pinch);
        CHECK_THAT (warp (0.0f), WithinAbs (0.0, 1.0e-6));
        CHECK_THAT (warp (1.0f), WithinAbs (1.0, 1.0e-6));
        for (float u = 0.0f; u < 0.999f; u += 0.001f)
            CHECK (warp (u + 0.001f) >= warp (u));
        const auto slopeAtStart = (warp (0.002f) - warp (0.0f)) / 0.002f;
        const auto slopeAtEnd = (warp (1.0f) - warp (0.998f)) / 0.002f;
        CHECK_THAT (slopeAtStart, WithinAbs (slopeAtEnd, 0.05 * slopeAtStart + 0.01));
    }
}

TEST_CASE ("PINCH zooms the middle of the wave in (gentler there) or out (steeper)", "[shape]")
{
    // A sine falls steepest in its middle (half way through the cycle).
    const auto fall = [] (float pinch)
    {
        return steepestFall (play (45, 1.0, [pinch] (synth::OscSettings& o) { o.pinch = pinch; }), 12000, 48000);
    };
    const auto plain = fall (0.0f);
    CHECK_THAT (plain, WithinAbs (juce::MathConstants<double>::twoPi * 110.0 / sampleRate, 0.0005));
    // Zoomed out, the middle is squeezed: steeper.
    CHECK (fall (-1.0f) > 1.8 * plain);
    // Zoomed in, the middle is stretched: the wave's middle is now gentle, so it's
    // steepest nearer its edges, but the middle itself falls far more slowly.
    const auto zoomed = play (45, 1.0, [] (synth::OscSettings& o) { o.pinch = 1.0f; });
    double middleFall = 0.0;
    for (size_t i = 12001; i < 48000; ++i)
        if (zoomed[i - 1] > 0.0f && zoomed[i] <= 0.0f)   // the falling zero crossing: the middle of the cycle
            middleFall = std::max (middleFall, (double) (zoomed[i - 1] - zoomed[i]));
    CHECK (middleFall < 0.2 * plain);
}

TEST_CASE ("FORMANT: faster with a gap after it (left), or only the start of the wave, slower (right)", "[shape]")
{
    // Left, CUT: the sine twice as fast for half the cycle, then silence.
    const auto fast = play (33, 1.0, [] (synth::OscSettings& o) { o.formant = -1.0f / 3.0f; o.formantMode = synth::FormantMode::cut; });
    const auto period = sampleRate / 55.0;
    double firstHalf = 0.0, secondHalf = 0.0;
    for (int cycle = 10; cycle < 40; ++cycle)
    {
        // From each rising zero crossing of the cycle's start.
        const auto start = (size_t) (cycle * period);
        firstHalf += rms (fast, start + (size_t) (0.05 * period), start + (size_t) (0.45 * period));
        secondHalf += rms (fast, start + (size_t) (0.55 * period), start + (size_t) (0.95 * period));
    }
    CHECK (secondHalf < 0.01 * firstHalf);
    CHECK (levelAt (fast, 110.0, 12000, 24000) > levelAt (fast, 55.0, 12000, 24000));   // the sine's energy moves to twice the note

    // Right: only the first half of the sine fits the cycle, stretched over it: always above zero.
    const auto slow = play (33, 1.0, [] (synth::OscSettings& o) { o.formant = 0.5f; o.formantMode = synth::FormantMode::cut; });
    CHECK (*std::min_element (slow.begin() + 12000, slow.end()) > -0.05f);
    CHECK (*std::max_element (slow.begin() + 12000, slow.end()) > 0.9f);
}

TEST_CASE ("FORMANT's MODE: CUT leaves a hard edge, FADE, X-FADE and X-FADE 2 don't", "[shape]")
{
    // Stretched (right), only the first 71% of a sine fits the cycle: it's
    // cut off mid-swing (near its lowest), and the next cycle starts at 0.
    const auto biggestJump = [] (synth::FormantMode mode)
    {
        const auto out = play (33, 1.0, [mode] (synth::OscSettings& o) { o.formant = 0.25f; o.formantMode = mode; });
        double biggest = 0.0;
        for (size_t i = 12001; i < 48000; ++i)
            biggest = std::max (biggest, (double) std::abs (out[i] - out[i - 1]));
        return biggest;
    };
    const auto cut = biggestJump (synth::FormantMode::cut);
    CHECK (cut > 0.3);   // the edge (smoothed over two samples, but still a jump)
    for (const auto mode : { synth::FormantMode::fade, synth::FormantMode::crossfade, synth::FormantMode::crossfade2 })
    {
        INFO (synth::formantModeNames[(int) mode]);
        CHECK (biggestJump (mode) < 0.05);   // a 55 Hz sine moves about 0.007 a sample
    }
}

TEST_CASE ("CRUSH RATE holds the wave in steps: 3 a cycle at most", "[shape][crush]")
{
    const auto out = play (21, 1.5, [] (synth::OscSettings& o) { o.crush = 1.0f; o.crushMode = synth::CrushMode::rate; });   // 27.5 Hz
    // Count the jumps (bigger than a sine moves in a sample at this pitch) in 20 cycles.
    const auto period = sampleRate / 27.5;
    int jumps = 0;
    for (auto i = (size_t) (10 * period); i < (size_t) (30 * period); ++i)
        if (std::abs (out[i] - out[i - 1]) > 0.1f && std::abs (out[i - 1] - out[i - 2]) <= 0.1f)
            ++jumps;
    CHECK (jumps == 60);
    // Between the jumps it holds still.
    int still = 0;
    for (auto i = (size_t) (10 * period); i < (size_t) (30 * period); ++i)
        still += std::abs (out[i] - out[i - 1]) < 1.0e-6f ? 1 : 0;
    CHECK (still > (int) (20 * period * 0.95));
}

TEST_CASE ("CRUSH BIT rounds the wave to fewer levels: at full, a sine becomes a square", "[shape][crush]")
{
    const auto out = play (33, 1.0, [] (synth::OscSettings& o) { o.crush = 1.0f; o.crushMode = synth::CrushMode::bit; });
    int atFull = 0;
    for (size_t i = 12000; i < 48000; ++i)
        atFull += std::abs (std::abs (out[i]) - 1.0f) < 0.05f ? 1 : 0;
    CHECK (atFull > (int) (0.97 * 36000));
    // A square's odd harmonics: the 3rd at a third of the fundamental, no 2nd
    // (measured over 30 whole cycles of the note, so the loud fundamental
    // doesn't leak into the others).
    constexpr size_t cycles30 = 26182;
    const auto fundamental = levelAt (out, 55.0, 12000, cycles30);
    CHECK_THAT (levelAt (out, 165.0, 12000, cycles30) / fundamental, WithinAbs (1.0 / 3.0, 0.02));
    CHECK (levelAt (out, 110.0, 12000, cycles30) < 0.01 * fundamental);

    // Halfway: 11 levels each side, so the steps are small.
    synth::OscSettings s;
    s.crush = 0.5f;
    s.crushMode = synth::CrushMode::bit;
    const synth::WarpTable none;
    CHECK (synth::WaveBend::make (s, none).levels == 11);
    s.crushMode = synth::CrushMode::both;
    CHECK (synth::WaveBend::make (s, none).levels == 11);
    CHECK (synth::WaveBend::make (s, none).steps == 28);
}

TEST_CASE ("SHAPE stays clean: hard edges smoothed, far less folds back than done the plain way", "[shape][aliasing]")
{
    // What folds back lands between the harmonics. At A5 (880 Hz, a lead's
    // range) and E7 (2637 Hz, the top of most parts).
    const auto check = [] (const char* what, auto setup, synth::OscWave wave, double betterAtA5, double worstAtA5)
    {
        for (const auto note : { 81, 100 })
        {
            Player p;
            p.osc().wave = wave;
            setup (p.osc());
            p.on (note);
            const auto ours = p.render (1.6)[0];
            const auto plain = naive (p.osc(), noteHz (note), ours.size());
            const auto oursWorst = Spectrum (ours, 4096).loudestBetweenHarmonics (noteHz (note), 20000.0);
            const auto plainWorst = Spectrum (plain, 4096).loudestBetweenHarmonics (noteHz (note), 20000.0);
            WARN (what << " at " << juce::roundToInt (noteHz (note)) << " Hz: loudest between harmonics " << oursWorst << " dB (the plain way: " << plainWorst << " dB)");
            if (note == 81)   // the limits: a little short of what was measured here at A5 (E7 is reported)
            {
                CHECK (oursWorst < plainWorst - betterAtA5);
                CHECK (oursWorst < worstAtA5);
            }
        }
    };
    check ("SKEW on a saw", [] (synth::OscSettings& o) { o.skew = 0.7f; }, synth::OscWave::saw, 40.0, -90.0);                       // -117 (plain -32)
    check ("PINCH on a saw", [] (synth::OscSettings& o) { o.pinch = 0.7f; }, synth::OscWave::saw, 40.0, -90.0);                     // -116 (plain -22)
    check ("FORMANT faster, CUT, on a saw", [] (synth::OscSettings& o) { o.formant = -0.45f; o.formantMode = synth::FormantMode::cut; },
           synth::OscWave::saw, 15.0, -40.0);                                                                                       // -43 (plain -25)
    // Fading both ends, the plain way happens to come out cleaner on a saw
    // (-52 dB); ours reads fewer harmonics, as the other modes need: -44.
    check ("FORMANT faster, X-FADE, on a saw", [] (synth::OscSettings& o) { o.formant = -0.45f; o.formantMode = synth::FormantMode::crossfade; },
           synth::OscWave::saw, -10.0, -40.0);
    check ("FORMANT slower, CUT (a hard edge), on a sine", [] (synth::OscSettings& o) { o.formant = 0.25f; o.formantMode = synth::FormantMode::cut; },
           synth::OscWave::sine, 8.0, -45.0);                                                                                       // -49 (plain -38)
    check ("FORMANT faster, FADE, on a sine", [] (synth::OscSettings& o) { o.formant = -0.45f; o.formantMode = synth::FormantMode::fade; },
           synth::OscWave::sine, 5.0, -55.0);                                                                                       // -61 (plain -52)
    check ("CRUSH RATE on a sine", [] (synth::OscSettings& o) { o.crush = 0.8f; o.crushMode = synth::CrushMode::rate; },
           synth::OscWave::sine, 10.0, -40.0);                                                                                      // -43 (plain -30)
    check ("CRUSH BIT on a sine", [] (synth::OscSettings& o) { o.crush = 0.7f; o.crushMode = synth::CrushMode::bit; },
           synth::OscWave::sine, 8.0, -42.0);                                                                                       // -46 (plain -34)
}

TEST_CASE ("SHAPE bends wavetables, unison copies and chord notes too", "[shape]")
{
    const auto table = synth::factoryTables::get (synth::factoryTables::names.indexOf ("Analog Morph"));
    const auto render = [&] (float skew, bool copies)
    {
        Player p;
        p.osc().wave = synth::OscWave::wavetable;
        p.osc().table = table.get();
        p.osc().skew = skew;
        if (copies)
        {
            p.osc().unisonMix = 1.0f;
            p.osc().unisonVoices = 3;
            p.osc().chord[0].on = true;
        }
        p.on (45);
        return p.render (0.5)[0];
    };
    CHECK_THAT (peakPosition (render (0.0f, false), 110.0, 6000, 24000), WithinAbs (0.25, 0.01));   // frame 1: a sine
    CHECK (peakPosition (render (0.6f, false), 110.0, 6000, 24000) < 0.17);

    // With copies, the bent and plain differ in every part (not just the key's own copy).
    const auto plain = render (0.0f, true), bent = render (0.6f, true);
    CHECK (rms (plain, 6000, 24000) > 0.1);
    std::vector<float> difference (plain.size());
    for (size_t i = 0; i < plain.size(); ++i)
        difference[i] = plain[i] - bent[i];
    CHECK (rms (difference, 6000, 24000) > 0.3 * rms (plain, 6000, 24000));
}

TEST_CASE ("CPU: a bent supersaw chord", "[shape][performance]")
{
    const auto percent = [] (const char* what, auto setup)
    {
        Player p;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
            o.unisonDetune = 0.4f;
            setup (o);
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (5.0);
        const auto cost = (juce::Time::getMillisecondCounterHiRes() - started) / 5000.0 * 100.0;
        WARN (what << ": 2 saws x 7 copies x 8 keys, " << cost << "% of one core");
        return cost;
    };
    const auto plain = percent ("plain", [] (synth::OscSettings&) {});
    percent ("SKEW", [] (synth::OscSettings& o) { o.skew = 0.4f; });
    percent ("FORMANT CUT", [] (synth::OscSettings& o) { o.formant = -0.3f; o.formantMode = synth::FormantMode::cut; });
    percent ("FORMANT X-FADE", [] (synth::OscSettings& o) { o.formant = -0.3f; o.formantMode = synth::FormantMode::crossfade; });
    percent ("CRUSH RATE", [] (synth::OscSettings& o) { o.crush = 0.3f; o.crushMode = synth::CrushMode::rate; });
    percent ("CRUSH BIT", [] (synth::OscSettings& o) { o.crush = 0.3f; o.crushMode = synth::CrushMode::bit; });
    const auto all = percent ("all of them (SKEW, FORMANT CUT, CRUSH BOTH)", [] (synth::OscSettings& o)
    {
        o.skew = 0.4f;
        o.formant = -0.3f;
        o.formantMode = synth::FormantMode::cut;
        o.crush = 0.3f;
        o.crushMode = synth::CrushMode::both;
    });
    CHECK (all < 60.0);
    CHECK (plain < all);
}

