#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/effects/MultibandComp.h"

using Catch::Matchers::WithinAbs;
namespace mb = effects::multiband;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // A few steady sine tones (Hz, dBFS peak), on the left; `side` tones go
    // left and inverted right (sides only), the rest both (middle).
    struct Tone
    {
        double hz, levelDb;
        bool side = false;
    };

    // Plays `seconds` of the tones through `processor`; returns what came out (both channels).
    juce::AudioBuffer<float> play (mb::Processor& processor, const mb::Settings& settings, const std::vector<Tone>& tones, double seconds,
                                   bool on = true)
    {
        const auto total = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> block (2, blockSize);
        for (int start = 0; start < total; start += blockSize)
        {
            const auto n = juce::jmin (blockSize, total - start);
            for (int i = 0; i < n; ++i)
            {
                double mid = 0.0, side = 0.0;
                for (const auto& t : tones)
                {
                    const auto x = juce::Decibels::decibelsToGain (t.levelDb) * std::sin (juce::MathConstants<double>::twoPi * t.hz * (start + i) / sampleRate);
                    (t.side ? side : mid) += x;
                }
                block.setSample (0, i, (float) (mid + side));
                block.setSample (1, i, (float) (mid - side));
            }
            auto audio = juce::dsp::AudioBlock<float> (block).getSubBlock (0, (size_t) n);
            processor.process (audio, settings, on);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, start, block, ch, 0, n);
        }
        return out;
    }

    // A tone's peak level (dBFS) in the last `seconds` of a channel, by correlation.
    double levelDb (const juce::AudioBuffer<float>& audio, int channel, double hz, double seconds = 0.5)
    {
        const auto n = (int) (seconds * sampleRate), from = audio.getNumSamples() - n;
        double re = 0.0, im = 0.0;
        for (int i = from; i < audio.getNumSamples(); ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * hz * i / sampleRate;
            re += audio.getSample (channel, i) * std::cos (phase);
            im += audio.getSample (channel, i) * std::sin (phase);
        }
        return juce::Decibels::gainToDecibels (2.0 * std::sqrt (re * re + im * im) / n, -200.0);
    }

    mb::Settings oneBand (float lowHz, float highHz)
    {
        mb::Settings settings;
        auto& band = settings.bands[0];
        band.used = true;
        band.lowHz = lowHz;
        band.highHz = highHz;
        band.slope = 3;   // 24 dB/oct
        band.attackMs = 1.0f;
        band.releaseMs = 50.0f;
        band.kneeDb = 0.0f;
        return settings;
    }
}

TEST_CASE ("COMP's bands: the static curve for the four kinds of dynamics", "[multiband]")
{
    mb::Band band;
    band.thresholdDb = -20.0f;
    band.ratio = 4.0f;
    band.kneeDb = 0.0f;

    band.mode = mb::Mode::compress;
    band.rangeDb = -30.0f;   // downward compression: 3 of every 4 dB over come off
    CHECK_THAT (mb::gainChangeDb (-10.0f, band), WithinAbs (-7.5, 1.0e-4));
    CHECK_THAT (mb::gainChangeDb (-30.0f, band), WithinAbs (0.0, 1.0e-6));
    band.rangeDb = -5.0f;    // RANGE is the most it moves
    CHECK_THAT (mb::gainChangeDb (-10.0f, band), WithinAbs (-5.0, 1.0e-4));

    band.rangeDb = 12.0f;    // upward compression: quiet parts come up
    CHECK_THAT (mb::gainChangeDb (-28.0f, band), WithinAbs (6.0, 1.0e-4));
    CHECK_THAT (mb::gainChangeDb (-60.0f, band), WithinAbs (12.0, 1.0e-4));
    CHECK_THAT (mb::gainChangeDb (-10.0f, band), WithinAbs (0.0, 1.0e-6));

    band.mode = mb::Mode::expand;
    band.rangeDb = -30.0f;   // downward expansion: each dB under goes 3 dB further down
    CHECK_THAT (mb::gainChangeDb (-25.0f, band), WithinAbs (-15.0, 1.0e-4));
    CHECK_THAT (mb::gainChangeDb (-10.0f, band), WithinAbs (0.0, 1.0e-6));
    band.rangeDb = 6.0f;     // upward expansion: loud parts come up
    CHECK_THAT (mb::gainChangeDb (-18.0f, band), WithinAbs (6.0, 1.0e-4));
    CHECK_THAT (mb::gainChangeDb (-30.0f, band), WithinAbs (0.0, 1.0e-6));

    // A soft knee starts easing in below the threshold and is half way (in slope) at it.
    band.mode = mb::Mode::compress;
    band.rangeDb = -30.0f;
    band.kneeDb = 10.0f;
    CHECK (mb::gainChangeDb (-22.0f, band) < 0.0f);
    CHECK_THAT (mb::gainChangeDb (-26.0f, band), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (mb::gainChangeDb (0.0f, band), WithinAbs (-15.0, 1.0e-4));
}

TEST_CASE ("COMP's bands: a band that isn't working leaves the sound exactly as it was", "[multiband]")
{
    mb::Processor processor;
    processor.prepare (sampleRate, blockSize);
    auto settings = oneBand (200.0f, 2000.0f);
    settings.bands[0].thresholdDb = 0.0f;   // never reached
    settings.bands[0].rangeDb = -30.0f;

    mb::Processor none;
    none.prepare (sampleRate, blockSize);
    const std::vector<Tone> tones { { 100.0, -12.0 }, { 700.0, -12.0 }, { 5000.0, -18.0, true } };
    const auto withBand = play (processor, settings, tones, 0.5);
    const auto without = play (none, mb::Settings {}, tones, 0.5);
    float worst = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < withBand.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (withBand.getSample (ch, i) - without.getSample (ch, i)));
    CHECK (worst < 1.0e-6f);   // no filtering, no phase change

    // And with no bands at all, COMP's bands pass the sound bit for bit.
    juce::AudioBuffer<float> block (2, blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
        block.setSample (0, i, std::sin ((float) i * 0.1f));
        block.setSample (1, i, std::cos ((float) i * 0.07f));
    }
    juce::AudioBuffer<float> copy (block);
    auto audio = juce::dsp::AudioBlock<float> (block);
    none.process (audio, mb::Settings {}, true);
    for (int i = 0; i < blockSize; ++i)
        CHECK (juce::exactlyEqual (block.getSample (0, i), copy.getSample (0, i)));
}

TEST_CASE ("COMP's bands: a band is flat inside, half way at its edges, untouched outside", "[multiband]")
{
    mb::Processor processor;
    processor.prepare (sampleRate, blockSize);
    auto settings = oneBand (300.0f, 3000.0f);
    settings.bands[0].rangeDb = 0.0f;      // no dynamics: just its OUTPUT
    settings.bands[0].outputDb = -12.0f;

    for (const auto& [hz, expected, within] : std::initializer_list<std::tuple<double, double, double>> {
             { 950.0, -12.0, 0.3 }, { 300.0, -6.0, 0.6 }, { 3000.0, -6.0, 0.6 }, { 60.0, 0.0, 0.2 }, { 15000.0, 0.0, 0.2 } })
    {
        INFO (hz << " Hz");
        const auto out = play (processor, settings, { { hz, -20.0 } }, 0.6);
        CHECK_THAT (levelDb (out, 0, hz) + 20.0, WithinAbs (expected, within));
    }
}

TEST_CASE ("COMP's bands: compression and expansion, down and up, only in the band", "[multiband]")
{
    // A tone in the band (800 Hz), and one below and one above it that the band must leave alone.
    const auto run = [] (mb::Mode mode, float rangeDb, float thresholdDb, float ratio, double inBandDb)
    {
        mb::Processor processor;
        processor.prepare (sampleRate, blockSize);
        auto settings = oneBand (250.0f, 2500.0f);
        auto& band = settings.bands[0];
        band.mode = mode;
        band.rangeDb = rangeDb;
        band.thresholdDb = thresholdDb;
        band.ratio = ratio;
        const auto out = play (processor, settings, { { 800.0, inBandDb }, { 50.0, -12.0 }, { 12000.0, -12.0 } }, 1.5);
        CHECK_THAT (levelDb (out, 0, 50.0), WithinAbs (-12.0, 0.3));
        CHECK_THAT (levelDb (out, 0, 12000.0), WithinAbs (-12.0, 0.3));
        return levelDb (out, 0, 800.0);
    };

    SECTION ("Downward compression: 20 dB over at 4:1 comes down 15 dB")
    {
        CHECK_THAT (run (mb::Mode::compress, -30.0f, -30.0f, 4.0f, -10.0), WithinAbs (-25.0, 1.5));
    }
    SECTION ("Upward compression: a quiet part comes up, as far as RANGE")
    {
        CHECK_THAT (run (mb::Mode::compress, 12.0f, -20.0f, 4.0f, -40.0), WithinAbs (-28.0, 1.5));
    }
    SECTION ("Downward expansion (a gate): a quiet part goes down, a loud one doesn't")
    {
        CHECK (run (mb::Mode::expand, -30.0f, -30.0f, 4.0f, -45.0) < -70.0);
        CHECK_THAT (run (mb::Mode::expand, -30.0f, -30.0f, 4.0f, -10.0), WithinAbs (-10.0, 0.3));
    }
    SECTION ("Upward expansion: a loud part comes up")
    {
        CHECK_THAT (run (mb::Mode::expand, 6.0f, -20.0f, 2.0f, -10.0), WithinAbs (-4.0, 0.8));
    }
}

TEST_CASE ("COMP's bands: on the sides only, the middle is left alone", "[multiband]")
{
    mb::Processor processor;
    processor.prepare (sampleRate, blockSize);
    auto settings = oneBand (250.0f, 2500.0f);
    settings.bands[0].stereo = mb::Stereo::side;
    settings.bands[0].thresholdDb = -40.0f;
    settings.bands[0].rangeDb = -20.0f;
    settings.bands[0].ratio = 30.0f;
    const auto out = play (processor, settings, { { 700.0, -10.0 }, { 900.0, -10.0, true } }, 1.0);

    // Middle and sides back from left and right.
    juce::AudioBuffer<float> mid (1, out.getNumSamples()), side (1, out.getNumSamples());
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        mid.setSample (0, i, 0.5f * (out.getSample (0, i) + out.getSample (1, i)));
        side.setSample (0, i, 0.5f * (out.getSample (0, i) - out.getSample (1, i)));
    }
    CHECK_THAT (levelDb (mid, 0, 700.0), WithinAbs (-10.0, 0.2));
    CHECK (levelDb (side, 0, 900.0) < -25.0);
}

TEST_CASE ("COMP's bands: LOOK-AHEAD makes COMP 20 ms late and has the band ready before a peak", "[multiband]")
{
    // Look-ahead on, no bands: exactly 20 ms late.
    {
        mb::Processor processor;
        processor.prepare (sampleRate, blockSize);
        mb::Settings settings;
        settings.lookahead = true;
        juce::AudioBuffer<float> block (2, blockSize);
        block.clear();
        block.setSample (0, 10, 1.0f);
        block.setSample (1, 10, 1.0f);
        std::vector<float> heard;
        for (int b = 0; b < 8; ++b)
        {
            auto audio = juce::dsp::AudioBlock<float> (block);
            processor.process (audio, settings, true);
            for (int i = 0; i < blockSize; ++i)
                heard.push_back (block.getSample (0, i));
            block.clear();
        }
        CHECK (processor.lookaheadSamples() == 960);
        CHECK_THAT (heard[10 + 960], WithinAbs (1.0, 1.0e-6));
        CHECK_THAT (heard[10], WithinAbs (0.0, 1.0e-6));
    }

    // A loud burst in the band after silence: with 10 ms of look-ahead and a
    // slower attack, the band is already turning it down when it starts.
    const auto firstMs = [] (bool lookahead)
    {
        mb::Processor processor;
        processor.prepare (sampleRate, blockSize);
        auto settings = oneBand (250.0f, 2500.0f);
        settings.lookahead = lookahead;
        auto& band = settings.bands[0];
        band.thresholdDb = -40.0f;
        band.rangeDb = -20.0f;
        band.ratio = 30.0f;
        band.attackMs = 3.0f;
        band.lookaheadMs = 10.0f;
        band.character = effects::Compressor::Character::fet;
        const auto start = 24000;
        juce::AudioBuffer<float> out (2, 48000);
        juce::AudioBuffer<float> block (2, blockSize);
        for (int from = 0; from < out.getNumSamples(); from += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = from + i;
                const auto x = n >= start ? 0.5f * std::sin (juce::MathConstants<float>::twoPi * 800.0f * (float) n / 48000.0f) : 0.0f;
                block.setSample (0, i, x);
                block.setSample (1, i, x);
            }
            auto audio = juce::dsp::AudioBlock<float> (block);
            processor.process (audio, settings, true);
            out.copyFrom (0, from, block, 0, 0, blockSize);
        }
        // The loudest of the burst's first 2 ms, as it comes out (later with look-ahead).
        const auto outStart = start + processor.lookaheadSamples();
        float loudest = 0.0f;
        for (int i = outStart; i < outStart + 96; ++i)
            loudest = juce::jmax (loudest, std::abs (out.getSample (0, i)));
        return juce::Decibels::gainToDecibels (loudest / 0.5f);
    };
    const auto without = firstMs (false), with = firstMs (true);
    INFO ("the burst's first 2 ms: " << without << " dB without look-ahead, " << with << " dB with");
    CHECK (without > -3.0);    // gets through before the band reacts
    CHECK (with < -10.0);      // already turned down
}

TEST_CASE ("COMP's bands: MIX from none to twice, and SOLO", "[multiband]")
{
    const auto inBand = [] (float mix, int solo)
    {
        mb::Processor processor;
        processor.prepare (sampleRate, blockSize);
        auto settings = oneBand (250.0f, 2500.0f);
        settings.bands[0].rangeDb = 0.0f;
        settings.bands[0].outputDb = -3.0f;
        settings.mix = mix;
        settings.soloBand = solo;
        const auto out = play (processor, settings, { { 800.0, -20.0 }, { 60.0, -20.0 } }, 0.6);
        return std::pair { levelDb (out, 0, 800.0), levelDb (out, 0, 60.0) };
    };
    CHECK_THAT (inBand (0.0f, -1).first, WithinAbs (-20.0, 0.1));                  // none of it
    CHECK_THAT (inBand (1.0f, -1).first, WithinAbs (-23.0, 0.3));                  // as set
    const auto twice = juce::Decibels::gainToDecibels (1.0 + 2.0 * (juce::Decibels::decibelsToGain (-3.0) - 1.0)) - 20.0;
    CHECK_THAT (inBand (2.0f, -1).first, WithinAbs (twice, 0.4));                  // twice the change (in level)
    const auto [band, below] = inBand (1.0f, 0);
    CHECK_THAT (band, WithinAbs (-23.0, 0.5));                                     // SOLO: the band, as processed...
    CHECK (below < -55.0);                                                         // ...and not what's outside it
}

TEST_CASE ("COMP's bands: six bands at 48 dB/oct, stereo, run in real time comfortably", "[multiband][performance]")
{
    mb::Processor processor;
    processor.prepare (sampleRate, blockSize);
    mb::Settings settings;
    settings.lookahead = true;
    const float edges[] = { 20.0f, 120.0f, 400.0f, 1200.0f, 3500.0f, 9000.0f, 20000.0f };
    for (int b = 0; b < mb::maxBands; ++b)
    {
        auto& band = settings.bands[(size_t) b];
        band.used = true;
        band.lowHz = edges[b];
        band.highHz = edges[b + 1];
        band.slope = 6;
        band.thresholdDb = -40.0f;
        band.lookaheadMs = 5.0f;
        band.stereo = (mb::Stereo) (b % 3);
    }
    const auto started = juce::Time::getMillisecondCounterHiRes();
    play (processor, settings, { { 80.0, -10.0 }, { 700.0, -10.0 }, { 5000.0, -10.0, true } }, 10.0);
    const auto percent = (juce::Time::getMillisecondCounterHiRes() - started) / 10000.0 * 100.0;
    WARN ("Six bands use " << percent << "% of one core in real time");
    CHECK (percent < 25.0);
}
