// THE STEP FX LANE (0.73.0): the effects themselves, on known signals (the sequencer's clock given by hand), and the
// lane's text. In the plugin: DrumSeqPluginTests.cpp's 0.73.0 cases.
#include "TestUtils.h"

#include "dsp/synth/StepFx.h"

namespace
{
    using synth::drumseq::FxStep;
    using synth::drumseq::FxType;
    using synth::drumseq::Pattern;

    // 120 BPM, 1/16 steps at 48 kHz: 6000 samples a step; a 16-step pattern.
    constexpr double samplesPerStep = 6000.0, beatsPerStep = 0.25, length = 16.0;

    // A signal no two stretches of which are alike: a sine whose pitch rises slowly (so a copy is recognisably a copy).
    std::vector<float> chirp (int n, double from = 300.0, double to = 900.0)
    {
        std::vector<float> x ((size_t) n);
        auto phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            phase += (from + (to - from) * i / n) / sampleRate;
            x[(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * phase);
        }
        return x;
    }
    std::vector<float> tone (int n, double hz, float level = 0.5f)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);
        return x;
    }

    Pattern withFx (FxType type, double start, double steps, int rate = 3, float amount = 1.0f)
    {
        Pattern p;
        FxStep f;
        f.type = type;
        f.start = start;
        f.length = steps;
        f.rate = rate;
        f.amount = amount;
        p.fx.push_back (f);
        return p;
    }

    // Runs the signal through in 512-sample blocks from step `fromStep` (mono: left only, or both sides alike).
    std::vector<float> run (const std::vector<float>& in, const Pattern* pattern, double fromStep = 0.0, bool stereo = true)
    {
        synth::stepfx::Processor fx;
        fx.prepare (sampleRate);
        auto left = in, right = in;
        for (size_t i = 0; i < in.size(); i += 512)
        {
            const auto n = (int) std::min<size_t> (512, in.size() - i);
            fx.process (left.data() + i, stereo ? right.data() + i : nullptr, n, fromStep + (double) i / samplesPerStep,
                        1.0 / samplesPerStep, beatsPerStep, pattern, length);
        }
        if (stereo)
            for (size_t i = 0; i < in.size(); ++i)
                REQUIRE (left[i] == right[i]);
        return left;
    }

    float biggestJump (const std::vector<float>& x, size_t from, size_t to)
    {
        auto most = 0.0f;
        for (auto i = from + 1; i < to; ++i)
            most = std::max (most, std::abs (x[i] - x[i - 1]));
        return most;
    }
}

TEST_CASE ("STEP FX 0.73.0: with no effect (or the sequencer stopped) the sound passes untouched", "[stepfx]")
{
    const auto in = chirp (48000);
    Pattern empty;
    CHECK (run (in, &empty) == in);
    CHECK (run (in, nullptr) == in);
    const auto repeat = withFx (FxType::repeat, 0.0, 16.0);
    // Stopped (step < 0): untouched, whatever the lane holds.
    synth::stepfx::Processor fx;
    fx.prepare (sampleRate);
    auto x = in;
    fx.process (x.data(), nullptr, 512, -1.0, 1.0 / samplesPerStep, beatsPerStep, &repeat, length);
    CHECK (std::equal (x.begin(), x.begin() + 512, in.begin()));
}

TEST_CASE ("STEP FX 0.73.0: REPEAT plays its first slice as it came, then loops it; AMOUNT fades the repeats", "[stepfx]")
{
    const auto in = chirp (16 * 6000);
    const auto pattern = withFx (FxType::repeat, 4.0, 4.0);   // steps 4-8, slices of a 1/16 (one step)
    const auto out = run (in, &pattern);
    // Before it and during its first slice: the sound itself (but for the smoothing's guess at the joins: tiny).
    for (size_t i = 0; i < 5 * 6000; ++i)
        REQUIRE_THAT (out[i], WithinAbs (in[i], 2.0e-3));
    // Slices 2-4 (steps 5, 6, 7): copies of step 4, once the 3 ms smoothing at each restart has gone.
    for (int k = 1; k < 4; ++k)
        for (size_t u = 2000; u < 6000; u += 7)
            REQUIRE_THAT (out[(size_t) (4 + k) * 6000 + u], WithinAbs (in[4 * 6000 + u], 1.0e-4));
    // After it: the sound again.
    for (size_t i = 8 * 6000 + 2000; i < 16 * 6000; ++i)
        REQUIRE_THAT (out[i], WithinAbs (in[i], 1.0e-4));

    // AMOUNT 50%: each repeat half the one before.
    const auto fading = withFx (FxType::repeat, 4.0, 4.0, 3, 0.5f);
    const auto faded = run (in, &fading);
    for (int k = 1; k < 4; ++k)
        CHECK_THAT (faded[(size_t) (4 + k) * 6000 + 3000], WithinAbs (std::pow (0.5f, (float) k) * in[4 * 6000 + 3000], 1.0e-4));

    // RATE 1/8: slices of two steps.
    const auto eighths = withFx (FxType::repeat, 4.0, 4.0, 2);
    const auto longer = run (in, &eighths);
    for (size_t u = 2000; u < 12000; u += 11)
        REQUIRE_THAT (longer[6 * 6000 + u], WithinAbs (in[4 * 6000 + u], 1.0e-4));
}

TEST_CASE ("STEP FX 0.73.0: REVERSE plays each slice's predecessor backwards", "[stepfx]")
{
    const auto in = chirp (16 * 6000);
    const auto pattern = withFx (FxType::reverse, 4.0, 4.0);
    const auto out = run (in, &pattern);
    for (int k = 0; k < 4; ++k)
        for (int u = 2000; u < 6000; u += 13)
        {
            const auto at = (size_t) ((4 + k) * 6000 + u);
            REQUIRE_THAT (out[at], WithinAbs (in[(size_t) ((4 + k) * 6000 - 1 - u)], 1.0e-4));
        }
}

TEST_CASE ("STEP FX 0.73.0: CHOP gates each RATE, AMOUNT deep", "[stepfx]")
{
    const auto in = tone (16 * 6000, 440.0);
    const auto pattern = withFx (FxType::chop, 0.0, 8.0, 3, 1.0f);   // 1/16 gate: open 3000 samples, shut 3000
    const auto out = run (in, &pattern);
    for (int k = 0; k < 8; ++k)
    {
        CHECK (rms (out, (size_t) k * 6000 + 500, (size_t) k * 6000 + 2900) > 0.3);
        CHECK (rms (out, (size_t) k * 6000 + 3500, (size_t) k * 6000 + 5900) < 1.0e-4);
    }
    // AMOUNT 50%: it closes to half.
    const auto half = withFx (FxType::chop, 0.0, 8.0, 3, 0.5f);
    const auto halved = run (in, &half);
    CHECK_THAT (rms (halved, 3500, 5900) / rms (halved, 500, 2900), WithinAbs (0.5, 0.01));
}

TEST_CASE ("STEP FX 0.73.0: TAPE STOP slows the sound down to a stop; half-way it's an octave down", "[stepfx]")
{
    const auto in = tone (16 * 6000, 1000.0);
    const auto pattern = withFx (FxType::tapeStop, 0.0, 8.0);   // 48,000 samples to a stop
    const auto out = run (in, &pattern);
    // Speed 1 - x: at the start about 1000 Hz, half-way about 500 Hz, a quarter of the way about 750 Hz.
    CHECK_THAT (frequencyOf (out, 200, 2600), WithinAbs (975.0, 20.0));
    CHECK_THAT (frequencyOf (out, 23000, 25000), WithinAbs (500.0, 15.0));
    CHECK_THAT (frequencyOf (out, 11000, 13000), WithinAbs (750.0, 15.0));
    // At the end, silent; after it, the tone again.
    CHECK (rms (out, 47000, 48000) < 0.01);
    CHECK (rms (out, 49000, 60000) > 0.3);
    // AMOUNT 50%: to half speed by the end (an octave down), still sounding.
    const auto half = withFx (FxType::tapeStop, 0.0, 8.0, 3, 0.5f);
    const auto slowed = run (in, &half);
    CHECK_THAT (frequencyOf (slowed, 45500, 47900), WithinAbs (505.0, 15.0));
    CHECK (rms (slowed, 45500, 47900) > 0.3);
}

TEST_CASE ("STEP FX 0.73.0: FILTER sweeps a low-pass down; BITCRUSH steps the levels and holds samples", "[stepfx]")
{
    const auto high = tone (16 * 6000, 5000.0);
    const auto filter = withFx (FxType::filter, 0.0, 8.0);
    const auto swept = run (high, &filter);
    CHECK (rms (swept, 1000, 3000) > 0.3);                        // open at first
    CHECK (rms (swept, 45000, 47900) < 0.01 * rms (high, 45000, 47900));   // near 150 Hz at the end: 5 kHz all but gone
    CHECK (rms (swept, 52000, 60000) > 0.3);                      // and back after it

    const auto in = tone (16 * 6000, 220.0);
    const auto crush = withFx (FxType::bitcrush, 0.0, 8.0);
    const auto crushed = run (in, &crush);
    // 4 bits: every value a multiple of 1/16; each held 16 samples.
    for (size_t i = 1000; i < 40000; ++i)
        REQUIRE_THAT (crushed[i] * 16.0f, WithinAbs (std::round (crushed[i] * 16.0f), 1.0e-3));
    auto changes = 0;
    for (size_t i = 1001; i < 1001 + 1600; ++i)
        changes += crushed[i] != crushed[i - 1] ? 1 : 0;
    CHECK (changes <= 100);   // at most one a 16 samples
    CHECK (changes >= 50);
}

TEST_CASE ("STEP FX 0.73.0: no clicks where an effect starts, ends or loops", "[stepfx]")
{
    const auto in = tone (16 * 6000, 110.0, 0.8f);
    const auto steady = biggestJump (in, 1, in.size());   // a 110 Hz sine's biggest step
    for (const auto type : { FxType::repeat, FxType::reverse, FxType::tapeStop, FxType::chop, FxType::filter })
    {
        const auto pattern = withFx (type, 2.5, 5.0);   // starting and ending part-way through a step
        const auto out = run (in, &pattern);
        INFO (synth::drumseq::fxNames[(int) type]);
        CHECK (biggestJump (out, 1, out.size()) < 6.0f * steady + 0.002f);
    }
}

TEST_CASE ("STEP FX 0.73.0: the lane saves and loads as text; a pattern with notes alone is as before", "[stepfx]")
{
    Pattern p;
    p.fx.push_back ({ 8.0, 2.0, FxType::tapeStop, 3, 0.75f });
    p.fx.push_back ({ 0.0, 4.0, FxType::repeat, 1, 1.0f });
    p.sortFx();
    CHECK (p.fx[0].type == FxType::repeat);
    Pattern back;
    back.decodeFx (p.encodeFx());
    CHECK (back.fx == p.fx);
    CHECK (back.fxAt (1.0) == 0);
    CHECK (back.fxAt (4.0) == -1);
    CHECK (back.fxAt (9.5) == 1);
    // Junk and unknown types are left out; empty text: none.
    back.decodeFx ("x 99:1:1:1:1 0:1:1:1:1");
    CHECK (back.fx.size() == 1);
    CHECK (back.fx[0].type == FxType::bitcrush);   // (99 limited to the last type)
    back.decodeFx ({});
    CHECK (back.fx.empty());
    CHECK (Pattern().isEmpty());
    CHECK (! p.isEmpty());
}
