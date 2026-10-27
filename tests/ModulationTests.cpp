// SYNC, FM and AM (stage 3c): measured on the sound, against what each should make.
#include "SynthTestUtils.h"

#include "dsp/synth/OscModulation.h"

namespace
{
    constexpr auto twoPi = juce::MathConstants<double>::twoPi;

    // Oscillator 1 (a sine unless said, starting at phase 0), set up by `setup`, playing `note`.
    template <typename Setup>
    std::vector<float> play (int note, double seconds, Setup setup, synth::OscWave wave = synth::OscWave::sine)
    {
        Player p;
        p.osc().wave = wave;
        setup (p);
        p.on (note);
        return p.render (seconds)[0];
    }

    // RATE's knob position for the `index`th ratio of a grid.
    float rateFor (synth::ModGrid grid, int index)
    {
        const auto& ratios = synth::modulation::gridRatios (grid);
        return 0.5f + 0.5f * (float) index / (float) (ratios.size() - 1);
    }

    // SYNC's amount for a ratio.
    float syncFor (double ratio) { return (float) (std::log2 (ratio) / 5.0); }

    double biggestStep (const std::vector<float>& x, size_t from, size_t to)
    {
        double biggest = 0.0;
        for (auto i = from + 1; i < to; ++i)
            biggest = std::max (biggest, (double) std::abs (x[i] - x[i - 1]));
        return biggest;
    }

    int crossings (const std::vector<float>& x, size_t from, size_t to)
    {
        int count = 0;
        for (auto i = from + 1; i < to; ++i)
            count += x[i - 1] < 0.0f && x[i] >= 0.0f ? 1 : 0;
        return count;
    }
}

TEST_CASE ("The modulator's RATE lands on its GRID's ratios", "[modulation]")
{
    using namespace synth;
    using modulation::ratio;
    CHECK_THAT (ratio (0.5f, ModGrid::free), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (ratio (0.0f, ModGrid::free), WithinAbs (1.0 / 16.0, 1.0e-6));
    CHECK_THAT (ratio (1.0f, ModGrid::free), WithinAbs (16.0, 1.0e-4));
    CHECK_THAT (ratio (0.2f, ModGrid::harmonic), WithinAbs (1.0, 1.0e-6));   // left of centre: 1, as Avenger's grids
    CHECK_THAT (ratio (rateFor (ModGrid::harmonic, 1), ModGrid::harmonic), WithinAbs (1.5, 1.0e-6));
    CHECK_THAT (ratio (rateFor (ModGrid::harmonic, 4), ModGrid::harmonic), WithinAbs (4.0, 1.0e-6));
    CHECK_THAT (ratio (1.0f, ModGrid::harmonic), WithinAbs (16.0, 1.0e-6));
    CHECK_THAT (ratio (rateFor (ModGrid::semitone, 7), ModGrid::semitone), WithinAbs (std::exp2 (7.0 / 12.0), 1.0e-5));
    CHECK (modulation::gridRatios (ModGrid::octave) == std::vector<float> { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f });
    // Octaves and a major third: 1, 1.26, 2, 2.52, 4...
    CHECK_THAT (ratio (rateFor (ModGrid::octaveMajor, 1), ModGrid::octaveMajor), WithinAbs (std::exp2 (4.0 / 12.0), 1.0e-5));
    CHECK_THAT (ratio (rateFor (ModGrid::octaveMajor, 3), ModGrid::octaveMajor), WithinAbs (2.0 * std::exp2 (4.0 / 12.0), 1.0e-5));
    CHECK_THAT (ratio (rateFor (ModGrid::octaveFifth, 1), ModGrid::octaveFifth), WithinAbs (std::exp2 (7.0 / 12.0), 1.0e-5));
}

TEST_CASE ("SYNC: a copy restarted every cycle, running faster, at the note's pitch", "[modulation][sync]")
{
    // Twice as fast: a sine restarted every cycle is just its octave.
    const auto octave = play (45, 1.0, [] (Player& p) { p.osc().syncAmount = syncFor (2.0); });
    CHECK_THAT (levelAt (octave, 220.0, 12000, 24000), WithinAbs (1.0, 0.01));
    CHECK (levelAt (octave, 110.0, 12000, 24000) < 0.01);

    // 2.5 times: not a harmonic, but restarted every cycle the sound repeats at
    // the note: only the note's harmonics, loudest round 2.5 times it.
    const auto synced = play (45, 1.6, [] (Player& p) { p.osc().syncAmount = syncFor (2.5); });
    const Spectrum spectrum (synced, 4096);
    CHECK (spectrum.loudestBetweenHarmonics (110.0, 5000.0) < -70.0);
    CHECK (levelAt (synced, 330.0, 12000, 24000) > levelAt (synced, 110.0, 12000, 24000));
    CHECK (levelAt (synced, 220.0, 12000, 24000) > levelAt (synced, 110.0, 12000, 24000));

    // ADD: the wave and the copy, half each.
    const auto added = play (45, 1.0, [] (Player& p) { p.osc().syncAmount = syncFor (2.0); p.osc().syncMode = synth::SyncMode::add; });
    CHECK_THAT (levelAt (added, 110.0, 12000, 24000), WithinAbs (0.5, 0.01));
    CHECK_THAT (levelAt (added, 220.0, 12000, 24000), WithinAbs (0.5, 0.01));
}

TEST_CASE ("SYNC's WIN modes fade the copy in and out each cycle, so it never jumps", "[modulation][sync]")
{
    // 2.25 times: the copy is a quarter into a cycle (at its top) when it restarts at 0.
    const auto jump = [] (synth::SyncMode mode)
    {
        return biggestStep (play (33, 1.0, [mode] (Player& p) { p.osc().syncAmount = syncFor (2.25); p.osc().syncMode = mode; }), 12000, 48000);
    };
    CHECK (jump (synth::SyncMode::pure) > 0.3);       // the restart (smoothed over two samples, but a jump)
    CHECK (jump (synth::SyncMode::pureWindowed) < 0.05);
    CHECK (jump (synth::SyncMode::add) > 0.15);
    CHECK (jump (synth::SyncMode::addWindowed) < 0.05);
}

TEST_CASE ("SYNC's envelope sweeps it from each note's start and falls back over LENGTH", "[modulation][sync]")
{
    const auto swept = play (45, 1.0, [] (Player& p)
    {
        p.osc().syncEnvAmount = 1.0f;     // starts 5 octaves up...
        p.osc().syncEnvMs = 300.0f;       // ...back to the note by 300 ms
    });
    const auto early = crossings (swept, 0, 960);          // the first 20 ms
    const auto late = crossings (swept, 24000, 24960);     // 20 ms half a second in
    CHECK (early > 8 * late);
    CHECK_THAT (frequencyOf (swept, 24000, 48000), WithinAbs (110.0, 0.1));   // plain again once it's fallen
    // The curve shapes the fall: halfway through LENGTH, a slow-start curve is still higher.
    CHECK (synth::modulation::syncEnvelope (150.0f, 300.0f, 0.8f) > synth::modulation::syncEnvelope (150.0f, 300.0f, 0.0f));
    CHECK_THAT (synth::modulation::syncEnvelope (150.0f, 300.0f, 0.0f), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (synth::modulation::syncEnvelope (400.0f, 300.0f, 0.0f), WithinAbs (0.0, 1.0e-6));
}

TEST_CASE ("FM reads the wave ahead and behind as the modulator swings: exactly phase modulation", "[modulation][fm]")
{
    // A 55 Hz sine, modulated by a sine at the same rate, depth 0.5 cycles:
    // sin (2 pi (t + 0.5 sin (2 pi t))) at every sample.
    const auto out = play (33, 0.5, [] (Player& p)
    {
        p.osc().fmAmount = 0.5f;   // 2 x 0.5^2 = 0.5 cycles
        p.osc().modGrid = synth::ModGrid::harmonic;
        p.osc().modRate = 0.5f;    // x1
    });
    double worst = 0.0;
    for (size_t i = 2000; i < 20000; ++i)
    {
        const auto t = (double) i * 55.0 / sampleRate;
        const auto expected = std::sin (twoPi * (t + 0.5 * std::sin (twoPi * t)));
        worst = std::max (worst, std::abs (expected - (double) out[i]));
    }
    CHECK (worst < 0.01);
}

TEST_CASE ("FM from another oscillator: it modulates, and isn't heard itself", "[modulation][fm]")
{
    // Oscillator 2, an octave up, modulates oscillator 1.
    const auto setup = [] (Player& p)
    {
        p.osc().fmAmount = 0.5f;
        p.osc().fmSource = synth::fmFirstOscillator + 1;
        auto& modulator = p.osc (1);
        modulator.on = true;
        modulator.wave = synth::OscWave::sine;
        modulator.transpose = 12.0f;
        modulator.randomPhase = false;
    };
    const auto out = play (33, 0.5, setup);
    double worst = 0.0;
    for (size_t i = 2000; i < 20000; ++i)
    {
        const auto t = (double) i * 55.0 / sampleRate;
        const auto expected = std::sin (twoPi * (t + 0.5 * std::sin (twoPi * 2.0 * t)));
        worst = std::max (worst, std::abs (expected - (double) out[i]));
    }
    CHECK (worst < 0.01);   // nothing of oscillator 2 added: it only modulates

    // With FM at 0, oscillator 2 is an ordinary oscillator again, heard.
    const auto plain = play (33, 0.5, [&] (Player& p) { setup (p); p.osc().fmAmount = 0.0f; });
    CHECK_THAT (levelAt (plain, 110.0, 6000, 8730), WithinAbs (1.0, 0.01));   // 20 cycles of it, 10 of oscillator 1: neither leaks into the other
}

TEST_CASE ("AM at full is ring modulation: the sum and difference, not the note", "[modulation][am]")
{
    // A 220 Hz sine times a sine at twice that: 220 and 660 Hz, half each.
    const auto out = play (57, 1.0, [] (Player& p)
    {
        p.osc().amAmount = 1.0f;
        p.osc().modGrid = synth::ModGrid::harmonic;
        p.osc().modRate = rateFor (synth::ModGrid::harmonic, 2);   // x2
    });
    CHECK_THAT (levelAt (out, 220.0, 12000, 24000), WithinAbs (0.5, 0.01));
    CHECK_THAT (levelAt (out, 660.0, 12000, 24000), WithinAbs (0.5, 0.01));
    CHECK (levelAt (out, 440.0, 12000, 24000) < 0.01);

    // Half: half the plain sine, plus half the ring modulation: at 220 Hz those
    // two are a quarter cycle apart (sine and cosine), so they add up to
    // sqrt (0.5^2 + 0.25^2); 660 Hz at a quarter.
    const auto half = play (57, 1.0, [] (Player& p)
    {
        p.osc().amAmount = 0.5f;
        p.osc().modGrid = synth::ModGrid::harmonic;
        p.osc().modRate = rateFor (synth::ModGrid::harmonic, 2);
    });
    CHECK_THAT (levelAt (half, 220.0, 12000, 24000), WithinAbs (std::sqrt (0.5 * 0.5 + 0.25 * 0.25), 0.01));
    CHECK_THAT (levelAt (half, 660.0, 12000, 24000), WithinAbs (0.25, 0.01));
}

TEST_CASE ("PHASE LOCK: every note the same; unlocked, each note's modulator starts anywhere", "[modulation]")
{
    // Two notes from engines making different random choices (seeds 1 and 2).
    const auto note = [] (bool locked, int seed)
    {
        return play (45, 0.2, [locked, seed] (Player& p)
        {
            p.engine.seedRandom (seed);
            p.osc().fmAmount = 0.4f;
            p.osc().modPhaseLock = locked;
            p.osc().modRate = rateFor (synth::ModGrid::harmonic, 2);
        });
    };
    CHECK (note (true, 1) == note (true, 2));
    CHECK (note (false, 1) != note (false, 2));
}

TEST_CASE ("SYNC, FM and AM stay clean: far less folds back than done the plain way", "[modulation][aliasing]")
{
    // The plain way: the richest table, no smoothing of SYNC's restart.
    const auto naive = [] (const synth::OscSettings& s, double hz, size_t length)
    {
        namespace mod = synth::modulation;
        const auto* saw = synth::tables::get (synth::tables::Wave::saw, 0);
        const auto ratio = mod::ratio (s.modRate, s.modGrid);
        const auto syncRatio = mod::syncRatio (s.syncAmount);
        std::vector<float> out (length);
        double phase = 0.0;
        for (auto& v : out)
        {
            auto x = phase;
            if (s.hasFm())
                x += mod::fmDepth (s.fmAmount) * std::sin (twoPi * phase * ratio);
            x -= std::floor (x);
            auto y = s.hasSync() ? x * syncRatio : x;
            y -= std::floor (y);
            v = synth::tables::read (saw, (float) y);
            if (s.hasAm())
                v *= (float) (1.0 - s.amAmount + s.amAmount * std::sin (twoPi * phase * ratio));
            phase += hz / sampleRate;
            phase -= std::floor (phase);
        }
        return out;
    };
    const auto check = [&] (const char* what, auto setup, double better, double worst)
    {
        for (const auto note : { 81, 100 })
        {
            Player p;
            p.osc().wave = synth::OscWave::saw;
            setup (p.osc());
            p.on (note);
            const auto ours = p.render (1.6)[0];
            const auto plain = naive (p.osc(), noteHz (note), ours.size());
            const auto oursWorst = Spectrum (ours, 4096).loudestBetweenHarmonics (noteHz (note), 20000.0);
            const auto plainWorst = Spectrum (plain, 4096).loudestBetweenHarmonics (noteHz (note), 20000.0);
            WARN (what << " at " << juce::roundToInt (noteHz (note)) << " Hz: loudest between harmonics " << oursWorst << " dB (the plain way: " << plainWorst << " dB)");
            if (note == 81)
            {
                CHECK (oursWorst < plainWorst - better);
                CHECK (oursWorst < worst);
            }
        }
    };
    // Limits a little short of what was measured at A5 (E7 is reported).
    check ("SYNC (x5.7) on a saw", [] (synth::OscSettings& o) { o.syncAmount = 0.5f; }, 30.0, -45.0);                                       // -51 (plain -13)
    check ("SYNC (x5.7) WIN on a saw", [] (synth::OscSettings& o) { o.syncAmount = 0.5f; o.syncMode = synth::SyncMode::pureWindowed; },
           60.0, -85.0);                                                                                                                      // -93
    check ("FM (sine x1, 0.32 cycles) on a saw", [] (synth::OscSettings& o) { o.fmAmount = 0.4f; o.modRate = 0.5f; }, 70.0, -100.0);        // -119 (plain -24)
    check ("AM (sine x3) on a saw", [] (synth::OscSettings& o) { o.amAmount = 0.7f; o.modRate = rateFor (synth::ModGrid::harmonic, 3); },
           70.0, -100.0);                                                                                                                     // -116 (plain -33)
}

TEST_CASE ("CPU: SYNC, FM and AM on a supersaw chord", "[modulation][performance]")
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
    percent ("SYNC with its envelope", [] (synth::OscSettings& o) { o.syncAmount = 0.3f; o.syncEnvAmount = 0.4f; });
    percent ("FM", [] (synth::OscSettings& o) { o.fmAmount = 0.3f; });
    percent ("AM", [] (synth::OscSettings& o) { o.amAmount = 0.5f; });
    const auto all = percent ("SYNC, FM and AM", [] (synth::OscSettings& o) { o.syncAmount = 0.3f; o.fmAmount = 0.3f; o.amAmount = 0.5f; });
    CHECK (all < 60.0);
    CHECK (plain < all);
}
