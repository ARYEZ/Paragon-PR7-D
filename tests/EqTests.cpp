#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/eq/EqDesign.h"
#include "dsp/eq/EqMatch.h"
#include "dsp/eq/ParametricEq.h"

#include <complex>
#include <optional>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;

    eq::Band band (eq::Shape shape, double hz, double gainDb = 0.0, double q = 1.0, int slope = eq::defaultSlope)
    {
        eq::Band b;
        b.used = true;
        b.shape = shape;
        b.frequency = hz;
        b.gainDb = gainDb;
        b.q = q;
        b.slope = slope;
        return b;
    }

    int slopeIndex (int dbPerOctave)
    {
        for (int i = 0; i < eq::numSlopes; ++i)
            if (eq::slopes[i] == dbPerOctave)
                return i;
        FAIL ("No slope " << dbPerOctave);
        return 0;
    }

    double at (const eq::Band& b, double hz, double sampleRate = 48000.0)
    {
        return eq::responseDb (eq::design (b, sampleRate), hz, sampleRate);
    }

    // The analog bell the EQ aims for, written out here independently:
    // (s^2 + s A w/Q + w^2) / (s^2 + s w/(A Q) + w^2), A = 10^(gain/40).
    double analogBellDb (double hz, double f0, double gainDb, double q)
    {
        const auto A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * pi * f0;
        const std::complex<double> s (0.0, 2.0 * pi * hz);
        return 20.0 * std::log10 (std::abs ((s * s + s * A * w0 / q + w0 * w0) / (s * s + s * w0 / (A * q) + w0 * w0)));
    }

    // The worst difference from the analog bell over 20 Hz to 20 kHz.
    double worstBellError (const eq::Design& design, double f0, double gainDb, double q, double sampleRate)
    {
        double worst = 0.0;
        for (auto hz = 20.0; hz <= juce::jmin (20000.0, 0.499 * sampleRate); hz *= 1.01)
            worst = std::max (worst, std::abs (eq::responseDb (design, hz, sampleRate) - analogBellDb (hz, f0, gainDb, q)));
        return worst;
    }

    // The textbook digital bell (the bilinear transform, as most DAW EQs),
    // for comparison: it squashes curves near the top of the range.
    eq::Design bilinearBell (double f0, double gainDb, double q, double sampleRate)
    {
        const auto A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f0 / sampleRate, alpha = std::sin (w) / (2.0 * q);
        const auto a0 = 1.0 + alpha / A;
        eq::Design d;
        d.numSections = 1;
        d.sections[0] = { (1.0 + alpha * A) / a0, -2.0 * std::cos (w) / a0, (1.0 - alpha * A) / a0, -2.0 * std::cos (w) / a0, (1.0 - alpha / A) / a0 };
        return d;
    }

    //==========================================================================
    // PR7-E's parameter IDs for the EQ, as the tests below set them.
    namespace eqIds
    {
        constexpr auto eqOn = "eqOn", eqOutput = "eqOutput", eqGainScale = "eqGainScale", eqAutoGain = "eqAutoGain";
        constexpr int eqNumBands = eq::ParametricEq::numBands;
        inline juce::String eqBand (int band, const char* what) { return "eqB" + juce::String (band + 1) + what; }
    }

    // The EQ as PR7-E ran it, standing in for that plugin's processor: the
    // settings set from PR7-E's parameter IDs, a mono track copied to both
    // sides first, and the EQ's readings (SOLO, AUTO GAIN, dynamics). PR7-K
    // keeps the EQ for its effects rack (a later stage) but has no EQ parameters yet.
    struct EqProcessor
    {
        void setEqSolo (int band) { settings.soloBand = band; }
        eq::ParametricEq::Settings getEqSettings() const { return settings; }
        double getEqAutoGainDb() const { return eqProcessor.getAutoGainDb(); }
        eq::ParametricEq::DynamicReading getEqDynamics (int band) const { return eqProcessor.getDynamics (band); }
        int getLatencySamples() const { return 0; }   // the EQ adds none

        eq::ParametricEq eqProcessor;
        eq::ParametricEq::Settings settings;
        int inputs = 2;
    };

    // The EQ, with stereo (or mono) in and stereo out, playing signals through it.
    struct EqRig
    {
        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 256;

        explicit EqRig (int inputs = 2)
        {
            processor.inputs = inputs;
            processor.eqProcessor.prepare (sampleRate, blockSize);
            set (eqIds::eqOn, 1.0f);
        }

        void set (const juce::String& id, float plain)
        {
            auto& s = processor.settings;
            if (id == eqIds::eqOn) { s.on = plain > 0.5f; return; }
            if (id == eqIds::eqOutput) { s.outputDb = plain; return; }
            if (id == eqIds::eqGainScale) { s.gainScale = plain; return; }
            if (id == eqIds::eqAutoGain) { s.autoGain = plain > 0.5f; return; }
            for (int i = 0; i < eqIds::eqNumBands; ++i)
            {
                const auto prefix = "eqB" + juce::String (i + 1);
                if (! id.startsWith (prefix) || ! juce::CharacterFunctions::isUpperCase (id[prefix.length()]))
                    continue;
                const auto what = id.substring (prefix.length());
                auto& b = s.bands[(size_t) i];
                if (what == "Used") b.used = plain > 0.5f;
                else if (what == "On") b.on = plain > 0.5f;
                else if (what == "Shape") b.shape = (eq::Shape) juce::roundToInt (plain);
                else if (what == "Freq") b.frequency = plain;
                else if (what == "Gain") b.gainDb = plain;
                else if (what == "Q") b.q = plain;
                else if (what == "Slope") b.slope = juce::roundToInt (plain);
                else if (what == "Place") b.placement = (eq::Placement) juce::roundToInt (plain);
                else if (what == "Dyn") b.dynamicRangeDb = plain;
                else if (what == "DynAuto") b.autoThreshold = plain > 0.5f;
                else if (what == "Thresh") b.thresholdDb = plain;
                else if (what == "Attack") b.attack = plain;
                else if (what == "Release") b.release = plain;
                else FAIL ("not an EQ band setting: " << id);
                return;
            }
            FAIL ("not an EQ setting: " << id);
        }

        void setBand (int index, const eq::Band& b)
        {
            set (eqIds::eqBand (index, "Shape"), (float) b.shape);
            set (eqIds::eqBand (index, "Freq"), (float) b.frequency);
            set (eqIds::eqBand (index, "Gain"), (float) b.gainDb);
            set (eqIds::eqBand (index, "Q"), (float) b.q);
            set (eqIds::eqBand (index, "Slope"), (float) b.slope);
            set (eqIds::eqBand (index, "Place"), (float) b.placement);
            set (eqIds::eqBand (index, "On"), b.on ? 1.0f : 0.0f);
            set (eqIds::eqBand (index, "Used"), b.used ? 1.0f : 0.0f);
        }

        // Plays left and right (functions of the sample index) for `seconds`; returns both outputs.
        std::array<std::vector<float>, 2> play (double seconds, const std::function<float (int)>& left,
                                                const std::function<float (int)>& right)
        {
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int start = 0; start < (int) (seconds * sampleRate); start += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    buffer.setSample (0, i, left (played + i));
                    // A mono track is heard on both sides, as PR7-E played it.
                    buffer.setSample (1, i, processor.inputs == 1 ? left (played + i) : right (played + i));
                }
                played += blockSize;
                processor.eqProcessor.process (juce::dsp::AudioBlock<float> (buffer), processor.settings);
                for (int ch = 0; ch < 2; ++ch)
                    out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + blockSize);
            }
            return out;
        }

        EqProcessor processor;
        int played = 0;
    };

    std::function<float (int)> tone (double hz, float level = 0.25f)
    {
        return [hz, level] (int n) { return level * (float) std::sin (2.0 * pi * hz * n / EqRig::sampleRate); };
    }

    std::function<float (int)> silence() { return [] (int) { return 0.0f; }; }

    // The level of `hz` in the last half of `samples`, in dB (full scale
    // sine = 0 dB), over a whole number of its cycles so nothing leaks.
    double levelDb (const std::vector<float>& samples, double hz)
    {
        const auto cycles = std::floor ((double) (samples.size() / 2) * hz / EqRig::sampleRate);
        const auto from = samples.size() - (size_t) std::round (cycles * EqRig::sampleRate / hz);
        const auto coefficient = 2.0 * std::cos (2.0 * pi * hz / EqRig::sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const auto amplitude = 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) (samples.size() - from);
        return 20.0 * std::log10 (std::max (amplitude, 1.0e-12));
    }

    float largestStep (const std::vector<float>& samples)
    {
        float step = 0.0f;
        for (size_t i = 1; i < samples.size(); ++i)
            step = std::max (step, std::abs (samples[i] - samples[i - 1]));
        return step;
    }
}

//==============================================================================
TEST_CASE ("EQ curves: a bell matches the analog curve, far closer than an ordinary digital EQ near the top", "[eq]")
{
    SECTION ("Exactly its gain at its own frequency, and flat far away")
    {
        for (const auto hz : { 50.0, 1000.0, 10000.0, 16000.0 })
        {
            INFO (hz << " Hz");
            CHECK_THAT (at (band (eq::Shape::bell, hz, 12.0), hz), WithinAbs (12.0, 0.01));
            CHECK_THAT (at (band (eq::Shape::bell, hz, -9.0, 4.0), hz), WithinAbs (-9.0, 0.01));
        }
        CHECK_THAT (at (band (eq::Shape::bell, 1000.0, 12.0, 2.0), 50.0), WithinAbs (0.0, 0.05));
    }

    SECTION ("Within a tenth of a dB of analog in the lows and mids")
    {
        for (const auto hz : { 100.0, 1000.0, 3000.0 })
            CHECK (worstBellError (eq::design (band (eq::Shape::bell, hz, 12.0), 48000.0), hz, 12.0, 1.0, 48000.0) < 0.12);
    }

    SECTION ("At 15 kHz, 44.1 kHz: within about a dB, where the bilinear transform is 7 dB off")
    {
        const auto matched = worstBellError (eq::design (band (eq::Shape::bell, 15000.0, 12.0), 44100.0), 15000.0, 12.0, 1.0, 44100.0);
        const auto ordinary = worstBellError (bilinearBell (15000.0, 12.0, 1.0, 44100.0), 15000.0, 12.0, 1.0, 44100.0);
        WARN ("15 kHz bell at 44.1 kHz: matched " << matched << " dB, bilinear " << ordinary << " dB from analog");
        CHECK (matched < 1.2);
        CHECK (ordinary > 5.0);
    }

    SECTION ("At 96 kHz: within 0.3 dB for bands up to 20 kHz, half a dB for one at 25 kHz")
    {
        for (const auto hz : { 5000.0, 15000.0, 20000.0 })
            CHECK (worstBellError (eq::design (band (eq::Shape::bell, hz, 12.0), 96000.0), hz, 12.0, 1.0, 96000.0) < 0.3);
        CHECK (worstBellError (eq::design (band (eq::Shape::bell, 25000.0, 12.0), 96000.0), 25000.0, 12.0, 1.0, 96000.0) < 0.55);
    }
}

TEST_CASE ("EQ curves: shelves, cuts and the other shapes", "[eq]")
{
    SECTION ("Shelves: the full gain one side, half way at the frequency, steeper with the slope")
    {
        const auto shelf12 = band (eq::Shape::lowShelf, 200.0, 12.0);
        CHECK_THAT (at (shelf12, 15.0), WithinAbs (12.0, 0.15));
        CHECK_THAT (at (shelf12, 200.0), WithinAbs (6.0, 0.05));
        CHECK (std::abs (at (shelf12, 4000.0)) < 0.1);

        const auto high = band (eq::Shape::highShelf, 4000.0, -8.0);
        CHECK_THAT (at (high, 18000.0), WithinAbs (-8.0, 0.4));
        CHECK_THAT (at (high, 4000.0), WithinAbs (-4.0, 0.1));
        CHECK (std::abs (at (high, 100.0)) < 0.05);

        // An octave above a 24 dB/oct low shelf, nearly nothing is left of it; a 6 dB/oct one is still lifting.
        CHECK (at (band (eq::Shape::lowShelf, 200.0, 12.0, 1.0, slopeIndex (24)), 400.0) < 0.3);
        CHECK (at (band (eq::Shape::lowShelf, 200.0, 12.0, 1.0, slopeIndex (6)), 400.0) > 2.0);
    }

    SECTION ("Cuts: -3 dB at the frequency, then exactly their slope")
    {
        for (const auto slope : { 6, 12, 24, 48, 96 })
        {
            INFO (slope << " dB/oct");
            const auto low = band (eq::Shape::lowCut, 400.0, 0.0, 1.0, slopeIndex (slope));
            CHECK_THAT (at (low, 400.0), WithinAbs (-3.01, 0.05));
            CHECK_THAT (at (low, 50.0) - at (low, 100.0), WithinAbs (-(double) slope, 0.5));   // an octave, well below
            const auto high = band (eq::Shape::highCut, 1000.0, 0.0, 1.0, slopeIndex (slope));
            CHECK_THAT (at (high, 1000.0), WithinAbs (-3.01, 0.1));
            CHECK (std::abs (at (high, 40.0)) < 0.1);
        }
        // Q above 1 makes a resonant peak at the corner (about 9 dB at Q 4, 12 dB/oct).
        const auto resonant = band (eq::Shape::lowCut, 400.0, 0.0, 4.0, slopeIndex (12));
        double peak = -100.0;
        for (auto hz = 300.0; hz < 800.0; hz += 2.0)
            peak = std::max (peak, at (resonant, hz));
        CHECK_THAT (peak, WithinAbs (9.2, 0.3));
    }

    SECTION ("Notch, band pass, all pass, tilts")
    {
        CHECK (at (band (eq::Shape::notch, 1000.0, 0.0, 4.0), 1000.0) < -40.0);
        CHECK (std::abs (at (band (eq::Shape::notch, 1000.0, 0.0, 4.0), 4000.0)) < 0.2);

        CHECK_THAT (at (band (eq::Shape::bandPass, 1000.0), 1000.0), WithinAbs (0.0, 0.05));
        CHECK (at (band (eq::Shape::bandPass, 1000.0), 100.0) < -18.0);

        for (const auto hz : { 30.0, 300.0, 3000.0, 15000.0 })
            CHECK (std::abs (at (band (eq::Shape::allPass, 1000.0, 0.0, 2.0), hz)) < 0.01);   // only the phase turns

        const auto tilt = band (eq::Shape::tiltShelf, 1000.0, 12.0);
        CHECK_THAT (at (tilt, 20.0), WithinAbs (-6.0, 0.2));
        CHECK_THAT (at (tilt, 1000.0), WithinAbs (0.0, 0.05));
        CHECK_THAT (at (tilt, 18000.0), WithinAbs (6.0, 0.4));

        // FLAT TILT +10: 1 dB per octave through its frequency.
        const auto flat = band (eq::Shape::flatTilt, 1000.0, 10.0);
        CHECK_THAT (at (flat, 1000.0), WithinAbs (0.0, 0.01));
        CHECK_THAT (at (flat, 125.0), WithinAbs (-3.0, 0.4));
        CHECK_THAT (at (flat, 8000.0), WithinAbs (3.0, 0.4));
    }

    SECTION ("Every shape at every slope, and bands above the top of the range, are stable")
    {
        for (int shape = 0; shape < eq::shapeNames.size(); ++shape)
            for (int slope = 0; slope < eq::numSlopes; ++slope)
                for (const auto hz : { 10.0, 1000.0, 21000.0, 30000.0 })
                    for (const auto sampleRate : { 44100.0, 96000.0 })
                    {
                        const auto d = eq::design (band ((eq::Shape) shape, hz, 12.0, 8.0, slope), sampleRate);
                        for (int i = 0; i < d.numSections; ++i)
                        {
                            const auto& s = d.sections[(size_t) i];
                            INFO (eq::shapeNames[shape] << " " << eq::slopeNames[slope] << " " << hz << " Hz at " << sampleRate);
                            // Poles inside the unit circle: |a2| < 1 and |a1| < 1 + a2.
                            REQUIRE (std::abs (s.a2) < 1.0);
                            REQUIRE (std::abs (s.a1) < 1.0 + s.a2);
                            REQUIRE (std::isfinite (s.b0 + s.b1 + s.b2));
                        }
                    }
    }
}

//==============================================================================
TEST_CASE ("EQ sound: bands change the level of what they're set to, and nothing else", "[eq]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 6.0));
    const auto out = rig.play (0.6, [] (int n) { return tone (1000.0) (n) + tone (100.0) (n); },
                                    [] (int n) { return tone (1000.0) (n) + tone (100.0) (n); });
    for (const auto& channel : out)
    {
        CHECK_THAT (levelDb (channel, 1000.0), WithinAbs (20.0 * std::log10 (0.25) + 6.0, 0.1));
        CHECK_THAT (levelDb (channel, 100.0), WithinAbs (20.0 * std::log10 (0.25) + at (band (eq::Shape::bell, 1000.0, 6.0), 100.0), 0.1));
    }
}

TEST_CASE ("EQ sound: switched off, or with no bands, it leaves the sound exactly as it was", "[eq]")
{
    for (const auto on : { false, true })
    {
        EqRig rig;
        rig.set (eqIds::eqOn, on ? 1.0f : 0.0f);
        if (! on)
            rig.setBand (0, band (eq::Shape::bell, 1000.0, 12.0));   // off: even a band does nothing
        const auto out = rig.play (0.2, tone (440.0), tone (660.0, 0.2f));
        // Within 1e-6 (-120 dB), not bit for bit: on Apple's arm64 compiler the
        // test's own tone() can come out one float step (1 ulp) apart from the
        // samples the rig fed in, as it fuses multiply-adds differently.
        for (int i = 0; i < 2000; ++i)
        {
            REQUIRE_THAT (out[0][(size_t) i], WithinAbs (tone (440.0) (i), 1.0e-6));
            REQUIRE_THAT (out[1][(size_t) i], WithinAbs (tone (660.0, 0.2f) (i), 1.0e-6));
        }
    }
}

TEST_CASE ("EQ sound: stereo, left, right, mid and side bands work on just that", "[eq][stereo]")
{
    const auto boost = [] (eq::Placement placement)
    {
        auto b = band (eq::Shape::bell, 1000.0, 12.0);
        b.placement = placement;
        return b;
    };
    const auto base = 20.0 * std::log10 (0.25);

    SECTION ("Left only: the right channel is untouched")
    {
        EqRig rig;
        rig.setBand (0, boost (eq::Placement::left));
        const auto out = rig.play (0.5, tone (1000.0), tone (1000.0));
        CHECK_THAT (levelDb (out[0], 1000.0), WithinAbs (base + 12.0, 0.1));
        CHECK_THAT (levelDb (out[1], 1000.0), WithinAbs (base, 0.05));
    }

    SECTION ("Mid: a sound in the middle (the same both sides) is boosted on both")
    {
        EqRig rig;
        rig.setBand (0, boost (eq::Placement::mid));
        const auto out = rig.play (0.5, tone (1000.0), tone (1000.0));
        CHECK_THAT (levelDb (out[0], 1000.0), WithinAbs (base + 12.0, 0.1));
        CHECK_THAT (levelDb (out[1], 1000.0), WithinAbs (base + 12.0, 0.1));
    }

    SECTION ("Side: a sound in the middle is untouched; what differs between the sides is boosted")
    {
        EqRig rig;
        rig.setBand (0, boost (eq::Placement::side));
        const auto middle = rig.play (0.5, tone (1000.0), tone (1000.0));
        CHECK_THAT (levelDb (middle[0], 1000.0), WithinAbs (base, 0.05));
        CHECK_THAT (levelDb (middle[1], 1000.0), WithinAbs (base, 0.05));

        // Opposite polarity on each side: all side, no mid.
        const auto wide = rig.play (0.5, tone (1000.0), [] (int n) { return -tone (1000.0) (n); });
        CHECK_THAT (levelDb (wide[0], 1000.0), WithinAbs (base + 12.0, 0.1));
        CHECK_THAT (levelDb (wide[1], 1000.0), WithinAbs (base + 12.0, 0.1));
    }

    SECTION ("A mono track: stereo and mid bands work, left, right and side ones do nothing")
    {
        EqRig rig (1);
        rig.setBand (0, boost (eq::Placement::mid));
        rig.setBand (1, band (eq::Shape::bell, 300.0, 6.0));
        auto sideBand = boost (eq::Placement::side);
        sideBand.frequency = 3000.0;
        rig.setBand (2, sideBand);
        const auto out = rig.play (0.5, [] (int n) { return tone (1000.0) (n) + tone (300.0) (n) + tone (3000.0) (n); }, silence());
        for (const auto& channel : out)   // the mono input, played on both sides
        {
            CHECK_THAT (levelDb (channel, 1000.0), WithinAbs (base + 12.0 + at (band (eq::Shape::bell, 300.0, 6.0), 1000.0), 0.15));
            CHECK_THAT (levelDb (channel, 300.0), WithinAbs (base + 6.0 + at (boost (eq::Placement::stereo), 300.0), 0.15));
            CHECK_THAT (levelDb (channel, 3000.0),
                        WithinAbs (base + at (boost (eq::Placement::stereo), 3000.0) + at (band (eq::Shape::bell, 300.0, 6.0), 3000.0), 0.15));
        }
    }
}

TEST_CASE ("EQ sound: no clicks when a band changes shape or switches on, no zipper when dragging", "[eq]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 200.0, 12.0, 2.0));
    rig.play (0.3, tone (200.0), tone (200.0));
    const auto steady = rig.play (0.2, tone (200.0), tone (200.0));
    const auto normal = largestStep (steady[0]);

    SECTION ("A bell becomes a 48 dB/oct low cut, mid-note: it crossfades")
    {
        rig.set (eqIds::eqBand (0, "Slope"), (float) slopeIndex (48));
        rig.set (eqIds::eqBand (0, "Shape"), (float) eq::Shape::lowCut);
        const auto changing = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (changing[0]) < normal * 1.2f);
    }

    SECTION ("Switching the band off and on")
    {
        rig.set (eqIds::eqBand (0, "On"), 0.0f);
        const auto off = rig.play (0.1, tone (200.0), tone (200.0));
        rig.set (eqIds::eqBand (0, "On"), 1.0f);
        const auto on = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (off[0]) < normal * 1.2f);
        CHECK (largestStep (on[0]) < normal * 1.2f);
    }

    SECTION ("Dragging the gain from +12 to -12 dB in one go glides")
    {
        rig.set (eqIds::eqBand (0, "Gain"), -12.0f);
        const auto moving = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (moving[0]) < normal * 1.2f);
        CHECK_THAT (levelDb (rig.play (0.3, tone (200.0), tone (200.0))[0], 200.0), WithinAbs (20.0 * std::log10 (0.25) - 12.0, 0.1));
    }
}

TEST_CASE ("EQ SOLO plays only the band's part of the spectrum", "[eq]")
{
    EqRig rig;
    rig.setBand (3, band (eq::Shape::bell, 2000.0, -6.0, 4.0));
    rig.processor.setEqSolo (3);
    const auto out = rig.play (0.5, [] (int n) { return tone (2000.0) (n) + tone (200.0) (n); },
                                    [] (int n) { return tone (2000.0) (n) + tone (200.0) (n); });
    // The 2 kHz the band works on, as it comes in (not cut: solo lets you hear it); 200 Hz far quieter.
    CHECK_THAT (levelDb (out[0], 2000.0), WithinAbs (20.0 * std::log10 (0.25), 0.2));
    CHECK (levelDb (out[0], 200.0) < 20.0 * std::log10 (0.25) - 30.0);

    rig.processor.setEqSolo (-1);
    const auto back = rig.play (0.5, tone (200.0), tone (200.0));
    CHECK_THAT (levelDb (back[0], 200.0), WithinAbs (20.0 * std::log10 (0.25) + at (band (eq::Shape::bell, 2000.0, -6.0, 4.0), 200.0), 0.1));
}

TEST_CASE ("EQ OUTPUT, GAIN SCALE and AUTO GAIN", "[eq]")
{
    const auto base = 20.0 * std::log10 (0.25);

    SECTION ("GAIN SCALE: 0% flat, 50% half, 200% double the bands' gain")
    {
        for (const auto [scale, expected] : { std::pair { 0.0f, 0.0 }, std::pair { 0.5f, 3.0 }, std::pair { 2.0f, 12.0 } })
        {
            EqRig rig;
            rig.setBand (0, band (eq::Shape::bell, 1000.0, 6.0));
            rig.set (eqIds::eqGainScale, scale);
            CHECK_THAT (levelDb (rig.play (0.5, tone (1000.0), tone (1000.0))[0], 1000.0), WithinAbs (base + expected, 0.1));
        }
    }

    SECTION ("OUTPUT")
    {
        EqRig rig;
        rig.set (eqIds::eqOutput, -9.0f);
        CHECK_THAT (levelDb (rig.play (0.3, tone (500.0), tone (500.0))[1], 500.0), WithinAbs (base - 9.0, 0.05));
    }

    SECTION ("AUTO GAIN takes off the curve's average lift, an octave at a time")
    {
        // +6 dB above 1 kHz is about +6 over the top five octaves of ten, plus the shelf's slope.
        EqRig rig;
        rig.setBand (0, band (eq::Shape::highShelf, 1000.0, 6.0));
        rig.set (eqIds::eqAutoGain, 1.0f);
        const auto out = rig.play (0.5, tone (100.0), tone (100.0));
        auto settings = rig.processor.getEqSettings();
        const auto expected = -eq::ParametricEq::averageChangeDb (settings, EqRig::sampleRate);
        CHECK (expected < -2.0);
        CHECK (expected > -4.0);
        CHECK_THAT (rig.processor.getEqAutoGainDb(), WithinAbs (expected, 0.01));
        CHECK_THAT (levelDb (out[0], 100.0), WithinAbs (base + expected + at (band (eq::Shape::highShelf, 1000.0, 6.0), 100.0), 0.1));
    }
}

TEST_CASE ("EQ: zero latency, 24 bands run in real time comfortably, and unused slots do nothing", "[eq][performance]")
{
    EqRig rig;
    CHECK (rig.processor.getLatencySamples() == 0);

    // Unused slots, however they're set, do nothing.
    for (int i = 0; i < eqIds::eqNumBands; ++i)
    {
        auto b = band (eq::Shape::bell, 300.0 + 100.0 * i, 12.0);
        b.used = false;
        rig.setBand (i, b);
    }
    const auto untouched = rig.play (0.2, tone (700.0), tone (700.0));
    CHECK_THAT (levelDb (untouched[0], 700.0), WithinAbs (20.0 * std::log10 (0.25), 0.01));

    // All 24, the costliest way: 96 dB/oct cuts and shelves, on mid and side.
    for (int i = 0; i < eqIds::eqNumBands; ++i)
    {
        auto b = band (i % 2 == 0 ? eq::Shape::lowShelf : eq::Shape::highCut, 100.0 + 400.0 * i, 3.0, 1.0, slopeIndex (96));
        b.placement = (eq::Placement) (i % 5);
        rig.setBand (i, b);
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto out = rig.play (3.0, tone (440.0), tone (550.0));
    const auto fraction = (juce::Time::getMillisecondCounterHiRes() - start) / 3000.0;
    WARN ("24 bands at 96 dB/oct use " << juce::String (fraction * 100.0, 1) << "% of one core in real time");
    CHECK (fraction < 0.5);
    for (const auto& channel : out)
        for (const auto sample : channel)
            REQUIRE (std::isfinite (sample));
}

//==============================================================================
//==============================================================================
// Dynamic EQ (0.5.0)
namespace
{
    // Makes band `index` dynamic: its range, and a manual threshold (or AUTO).
    void setDynamic (EqRig& rig, int index, double rangeDb, std::optional<double> thresholdDb, double attack = 0.5, double release = 0.5)
    {
        rig.set (eqIds::eqBand (index, "Dyn"), (float) rangeDb);
        rig.set (eqIds::eqBand (index, "DynAuto"), thresholdDb.has_value() ? 0.0f : 1.0f);
        if (thresholdDb.has_value())
            rig.set (eqIds::eqBand (index, "Thresh"), (float) *thresholdDb);
        rig.set (eqIds::eqBand (index, "Attack"), (float) attack);
        rig.set (eqIds::eqBand (index, "Release"), (float) release);
    }

    // A tone at a level in dB (a full-scale sine is 0 dB, as on the analyser).
    std::function<float (int)> toneDb (double hz, double db) { return tone (hz, (float) std::pow (10.0, db / 20.0)); }

    double outDb (EqRig& rig, double seconds, double hz, double db)
    {
        return levelDb (rig.play (seconds, toneDb (hz, db), toneDb (hz, db))[0], hz);
    }
}

TEST_CASE ("Dynamic EQ: the gain curve, with its soft knee and range", "[eq][dynamic]")
{
    using eq::dynamics::gainChangeDb;
    CHECK (gainChangeDb (-40.0, -30.0, -12.0) == 0.0);                    // well under: nothing
    CHECK_THAT (gainChangeDb (-33.0, -30.0, -12.0), WithinAbs (0.0, 1.0e-9));   // the knee starts 3 dB under
    CHECK_THAT (gainChangeDb (-30.0, -30.0, -12.0), WithinAbs (-0.75, 1.0e-9));
    CHECK_THAT (gainChangeDb (-27.0, -30.0, -12.0), WithinAbs (-3.0, 1.0e-9)); // then a dB for each dB over...
    CHECK_THAT (gainChangeDb (-22.0, -30.0, -12.0), WithinAbs (-8.0, 1.0e-9));
    CHECK_THAT (gainChangeDb (0.0, -30.0, -12.0), WithinAbs (-12.0, 1.0e-9));  // ...up to the range
    CHECK_THAT (gainChangeDb (0.0, -30.0, 6.0), WithinAbs (6.0, 1.0e-9));      // a positive range boosts

    // Automatic timing follows the band's frequency; the knobs scale it 1/4 to 4 times.
    CHECK_THAT (eq::dynamics::attackSeconds (1000.0, 0.5), WithinAbs (0.004, 1.0e-9));
    CHECK_THAT (eq::dynamics::attackSeconds (50.0, 0.5), WithinAbs (0.05, 1.0e-9));
    CHECK_THAT (eq::dynamics::attackSeconds (1000.0, 0.0), WithinAbs (0.001, 1.0e-9));
    CHECK_THAT (eq::dynamics::releaseSeconds (1000.0, 1.0), WithinAbs (0.32, 1.0e-9));
}

TEST_CASE ("Dynamic EQ: a cut works only when the band's part is loud, by as much as it's over, up to its range", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    setDynamic (rig, 0, -12.0, -30.0);

    // 10 dB under the threshold: untouched.
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -40.0), WithinAbs (-40.0, 0.05));
    // 6 dB over: 6 dB down.
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -24.0), WithinAbs (-30.0, 0.3));
    // 24 dB over: the whole range, 12 dB.
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -6.0), WithinAbs (-18.0, 0.3));
    // And back when it's quiet again.
    CHECK_THAT (outDb (rig, 1.0, 1000.0, -40.0), WithinAbs (-40.0, 0.05));

    // With a gain of its own: +6 dB when quiet, back to level (+6 - 6) when loud.
    rig.set (eqIds::eqBand (0, "Gain"), 6.0f);
    rig.set (eqIds::eqBand (0, "Dyn"), -6.0f);
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -40.0), WithinAbs (-34.0, 0.1));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -6.0), WithinAbs (-6.0, 0.3));
}

TEST_CASE ("Dynamic EQ: a positive range boosts when loud", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    setDynamic (rig, 0, 6.0, -30.0);
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -40.0), WithinAbs (-40.0, 0.05));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -10.0), WithinAbs (-4.0, 0.3));
}

TEST_CASE ("Dynamic EQ: a band listens only to its own part of the spectrum", "[eq][dynamic]")
{
    // A 5 kHz de-esser-like band; a loud bass note doesn't set it off.
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 5000.0, 0.0, 2.0));
    setDynamic (rig, 0, -12.0, -30.0);
    const auto quietHighs = [] (int n) { return toneDb (100.0, -6.0) (n) + toneDb (5000.0, -40.0) (n); };
    auto out = rig.play (0.5, quietHighs, quietHighs);
    CHECK_THAT (levelDb (out[0], 5000.0), WithinAbs (-40.0, 0.1));
    CHECK_THAT (levelDb (out[0], 100.0), WithinAbs (-6.0, 0.05));
    CHECK (rig.processor.getEqDynamics (0).changeDb > -0.1f);

    // Loud highs do.
    const auto loudHighs = [] (int n) { return toneDb (100.0, -6.0) (n) + toneDb (5000.0, -6.0) (n); };
    out = rig.play (0.5, loudHighs, loudHighs);
    CHECK_THAT (levelDb (out[0], 5000.0), WithinAbs (-18.0, 0.5));
    CHECK_THAT (levelDb (out[0], 100.0), WithinAbs (-6.0, 0.05));
}

TEST_CASE ("Dynamic EQ: AUTO's threshold settles above the band's usual level, and acts on what stands out", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    setDynamic (rig, 0, -12.0, std::nullopt);

    // A steady part: left alone.
    rig.play (4.0, toneDb (1000.0, -30.0), toneDb (1000.0, -30.0));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -30.0), WithinAbs (-30.0, 0.2));
    CHECK_THAT (rig.processor.getEqDynamics (0).thresholdDb, WithinAbs (-24.0, 0.5));

    // A passage 18 dB louder is pulled down at once...
    rig.play (0.15, toneDb (1000.0, -12.0), toneDb (1000.0, -12.0));
    CHECK (rig.processor.getEqDynamics (0).changeDb < -8.0f);

    // ...and as it carries on, the threshold follows it up and lets go.
    rig.play (15.0, toneDb (1000.0, -12.0), toneDb (1000.0, -12.0));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -12.0), WithinAbs (-12.0, 0.5));
}

TEST_CASE ("Dynamic EQ: ATTACK and RELEASE set how quickly it acts and lets go", "[eq][dynamic]")
{
    const auto block = EqRig::blockSize / EqRig::sampleRate;   // 5.3 ms

    // The first 5 ms of a loud note.
    const auto afterOnset = [&] (double attack)
    {
        EqRig rig;
        rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
        setDynamic (rig, 0, -12.0, -30.0, attack);
        rig.play (0.2, toneDb (1000.0, -60.0), toneDb (1000.0, -60.0));
        rig.play (block, toneDb (1000.0, -6.0), toneDb (1000.0, -6.0));
        return rig.processor.getEqDynamics (0).changeDb;
    };
    CHECK (afterOnset (0.0) < -8.0f);   // fast: most of the way already
    CHECK (afterOnset (1.0) > -5.0f);   // slow: still moving

    // 64 ms after a loud note stops.
    const auto afterStop = [&] (double release)
    {
        EqRig rig;
        rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
        setDynamic (rig, 0, -12.0, -30.0, 0.5, release);
        rig.play (0.5, toneDb (1000.0, -6.0), toneDb (1000.0, -6.0));
        rig.play (12 * block, toneDb (1000.0, -60.0), toneDb (1000.0, -60.0));
        return rig.processor.getEqDynamics (0).changeDb;
    };
    CHECK (afterStop (0.0) > -1.5f);    // fast: let go
    CHECK (afterStop (1.0) < -7.0f);    // slow: still holding most of it
}

TEST_CASE ("Dynamic EQ: a band on one channel listens to and works on just that", "[eq][dynamic][stereo]")
{
    EqRig rig;
    auto b = band (eq::Shape::bell, 1000.0, 0.0, 2.0);
    b.placement = eq::Placement::left;
    rig.setBand (0, b);
    setDynamic (rig, 0, -12.0, -30.0);
    const auto out = rig.play (0.5, toneDb (1000.0, -6.0), toneDb (1000.0, -6.0));
    CHECK_THAT (levelDb (out[0], 1000.0), WithinAbs (-18.0, 0.3));
    CHECK_THAT (levelDb (out[1], 1000.0), WithinAbs (-6.0, 0.01));

    // Loud on the right only: the left band stays put.
    const auto quietLeft = rig.play (1.0, toneDb (1000.0, -40.0), toneDb (1000.0, -6.0));
    CHECK_THAT (levelDb (quietLeft[0], 1000.0), WithinAbs (-40.0, 0.05));
}

TEST_CASE ("Dynamic EQ: becoming dynamic, and static again, glides; cuts and other shapes can't be dynamic", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    const auto loud = toneDb (1000.0, -6.0);
    rig.play (0.3, loud, loud);
    const auto normal = largestStep (rig.play (0.1, loud, loud)[0]);

    setDynamic (rig, 0, -12.0, -30.0);
    CHECK (largestStep (rig.play (0.2, loud, loud)[0]) <= normal * 1.01f);   // it only turns down, smoothly
    CHECK (rig.processor.getEqDynamics (0).active);

    rig.set (eqIds::eqBand (0, "Dyn"), 0.0f);
    CHECK (largestStep (rig.play (0.5, loud, loud)[0]) < normal * 1.05f);
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -6.0), WithinAbs (-6.0, 0.01));
    CHECK_FALSE (rig.processor.getEqDynamics (0).active);

    // A low cut with a range set is just a low cut.
    EqRig cut;
    cut.setBand (0, band (eq::Shape::lowCut, 100.0));
    setDynamic (cut, 0, -12.0, -60.0);
    CHECK_THAT (outDb (cut, 0.5, 1000.0, -6.0), WithinAbs (-6.0 + at (band (eq::Shape::lowCut, 100.0), 1000.0), 0.01));
    CHECK_FALSE (cut.processor.getEqDynamics (0).active);
}

TEST_CASE ("Dynamic EQ: 24 dynamic bands still run in real time comfortably", "[eq][dynamic][performance]")
{
    EqRig rig;
    for (int i = 0; i < eqIds::eqNumBands; ++i)
    {
        auto b = band (i % 2 == 0 ? eq::Shape::bell : eq::Shape::highShelf, 100.0 + 400.0 * i, 3.0, 1.0, slopeIndex (96));
        b.placement = (eq::Placement) (i % 5);
        rig.setBand (i, b);
        setDynamic (rig, i, i % 2 == 0 ? -12.0 : 6.0, std::nullopt);
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto out = rig.play (3.0, tone (440.0), tone (550.0));
    const auto fraction = (juce::Time::getMillisecondCounterHiRes() - start) / 3000.0;
    WARN ("24 dynamic bands use " << juce::String (fraction * 100.0, 1) << "% of one core in real time");
    CHECK (fraction < 0.5);
    for (const auto& channel : out)
        for (const auto sample : channel)
            REQUIRE (std::isfinite (sample));
}

TEST_CASE ("Dynamic EQ: cutting a held low note doesn't distort it", "[eq][dynamic]")
{
    // A gain that moved within each cycle would add harmonics; measuring
    // over a couple of cycles keeps it steady. 6 dB over the threshold, so
    // the gain follows the level (at full range it couldn't move at all).
    for (const auto attack : { 0.0, 0.5 })
    {
        EqRig rig;
        rig.setBand (0, band (eq::Shape::bell, 60.0, 0.0, 1.0));
        setDynamic (rig, 0, -12.0, -30.0, attack);
        const auto out = rig.play (1.5, toneDb (60.0, -24.0), toneDb (60.0, -24.0));
        const auto fundamental = levelDb (out[0], 60.0);
        CHECK_THAT (fundamental, WithinAbs (-30.0, 0.5));
        for (const auto harmonic : { 120.0, 180.0, 240.0 })
        {
            INFO ("attack " << attack << ", " << harmonic << " Hz: " << levelDb (out[0], harmonic) - fundamental << " dB");
            CHECK (levelDb (out[0], harmonic) - fundamental < -60.0);
        }
    }
}

//==============================================================================
// EQ Match (0.7.0)
namespace
{
    // Paul Kellet's economy pink filter: falls 3 dB an octave.
    struct Pink
    {
        juce::Random random { 11 };
        double b0 = 0.0, b1 = 0.0, b2 = 0.0;
        float operator()()
        {
            const auto white = random.nextFloat() * 2.0 - 1.0;
            b0 = 0.99765 * b0 + white * 0.0990460;
            b1 = 0.96300 * b1 + white * 0.2965164;
            b2 = 0.57000 * b2 + white * 1.0526913;
            return (float) (0.05 * (b0 + b1 + b2 + white * 0.1848));
        }
    };

    eq::match::Spectrum analyse (const std::function<float()>& source, double seconds, double sampleRate = 48000.0)
    {
        eq::match::Analyser analyser;
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (auto& s : samples)
            s = source();
        analyser.add (samples.data(), (int) samples.size(), sampleRate);
        return analyser.spectrum();
    }

    double atHz (const std::vector<double>& onGrid, double hz)
    {
        const auto& g = eq::match::grid();
        const auto i = (size_t) std::distance (g.begin(), std::min_element (g.begin(), g.end(), [hz] (double a, double b) { return std::abs (std::log (a / hz)) < std::abs (std::log (b / hz)); }));
        return onGrid[i];
    }

    const std::vector<eq::Band> knownCurve { band (eq::Shape::lowShelf, 120.0, -3.0, 0.7), band (eq::Shape::bell, 1000.0, 4.0, 1.0),
                                             band (eq::Shape::bell, 3000.0, -2.5, 2.0), band (eq::Shape::highShelf, 8000.0, 3.0, 0.7) };
}

TEST_CASE ("EQ Match: the analyser hears a spectrum's shape and ignores silence", "[eq][match]")
{
    juce::Random random (5);
    const auto white = analyse ([&random] { return random.nextFloat() * 0.5f - 0.25f; }, 8.0);
    Pink pink;
    const auto pinkSpectrum = analyse ([&pink] { return pink(); }, 8.0);
    REQUIRE (white.isValid());
    REQUIRE (pinkSpectrum.isValid());

    // Pink falls 3 dB an octave against white: 6 octaves, 100 Hz to 6.4 kHz, 18 dB.
    const auto tilt = (atHz (pinkSpectrum.db, 6400.0) - atHz (white.db, 6400.0)) - (atHz (pinkSpectrum.db, 100.0) - atHz (white.db, 100.0));
    CHECK_THAT (tilt, WithinAbs (-18.0, 1.5));

    // 2 seconds of silence, then 4 of noise: 4 seconds heard.
    int n = 0;
    const auto withPause = analyse ([&] { return n++ < 96000 ? 0.0f : random.nextFloat() * 0.5f - 0.25f; }, 6.0);
    CHECK_THAT (withPause.seconds, WithinAbs (4.0, 0.3));

    // Saved and loaded again, it's the same.
    const auto loaded = eq::match::Spectrum::fromXml (*pinkSpectrum.toXml ("Pink"));
    REQUIRE (loaded.has_value());
    for (size_t i = 0; i < loaded->db.size(); ++i)
        REQUIRE_THAT (loaded->db[i], WithinAbs (pinkSpectrum.db[i], 1.0e-6));
}

TEST_CASE ("EQ Match: the target is the tone difference, not the level, minus what the EQ already does", "[eq][match]")
{
    juce::Random random (6);
    auto track = analyse ([&random] { return random.nextFloat() * 0.5f - 0.25f; }, 5.0);
    const auto bell = eq::match::curveOnGrid ({ band (eq::Shape::bell, 1000.0, 4.0, 1.0) }, 48000.0);

    // The same sound, 6 dB louder: nothing to match.
    auto louder = track;
    for (auto& db : louder.db)
        db += 6.0;
    for (const auto value : eq::match::target (track, louder, {}, 0.5))
        REQUIRE (std::abs (value) < 0.01);
    CHECK (eq::match::fit (eq::match::target (track, louder, {}, 0.5), {}).empty());

    // A reference with a 1 kHz lift, when the EQ already has that lift: nothing left to do.
    auto lifted = track;
    for (size_t i = 0; i < lifted.db.size(); ++i)
        lifted.db[i] += bell[i];
    CHECK (atHz (eq::match::target (track, lifted, {}, 0.5), 1000.0) > 2.0);
    for (const auto value : eq::match::target (track, lifted, bell, 0.5))
        REQUIRE (std::abs (value) < 0.3);

    // Where the reference has nothing (below 60 Hz, 80 dB down), it isn't followed.
    auto noLows = track;
    for (size_t i = 0; i < noLows.db.size(); ++i)
        if (eq::match::grid()[i] < 60.0)
            noLows.db[i] -= 80.0;
    CHECK (std::abs (atHz (eq::match::target (track, noLows, {}, 0.5), 25.0)) < 1.0);
}

TEST_CASE ("EQ Match: the bands it adds follow the difference; DETAIL and AMOUNT", "[eq][match]")
{
    const auto target = eq::match::curveOnGrid (knownCurve, 48000.0);
    const auto worstMiss = [&] (const std::vector<eq::Band>& bands, double scale)
    {
        const auto curve = eq::match::curveOnGrid (bands, 48000.0);
        double worst = 0.0;
        for (size_t i = 0; i < curve.size(); ++i)
            if (eq::match::grid()[i] <= 16000.0)
                worst = std::max (worst, std::abs (curve[i] - target[i] * scale));
        return worst;
    };

    eq::match::Options options;
    const auto bands = eq::match::fit (target, options);
    WARN ("Matched with " << bands.size() << " bands, within " << worstMiss (bands, 1.0) << " dB");
    CHECK (bands.size() <= 8);
    CHECK (worstMiss (bands, 1.0) < 1.0);

    // Less detail: fewer, broader bands, still the overall shape.
    options.detail = 0.0;
    const auto broad = eq::match::fit (target, options);
    CHECK (broad.size() <= 4);
    CHECK (worstMiss (broad, 1.0) < 2.5);

    // Half the amount: half the curve.
    options.detail = 0.5;
    options.amount = 0.5;
    CHECK (worstMiss (eq::match::fit (target, options), 0.5) < 0.75);

    // No more bands than there are free slots.
    options.amount = 1.0;
    options.maxBands = 2;
    CHECK (eq::match::fit (target, options).size() <= 2);
}

TEST_CASE ("EQ Match: fitting is quick enough to follow DETAIL and AMOUNT as they turn", "[eq][match][performance]")
{
    Pink pink;
    juce::Random random (12);
    const auto track = analyse ([&pink] { return pink(); }, 5.0);
    const auto reference = analyse ([&random] { return random.nextFloat() * 0.2f - 0.1f; }, 5.0);
    // The worst case: a jagged difference (a random walk, +-8 dB) that needs many bands.
    auto jagged = eq::match::target (track, reference, {}, 1.0);
    double walk = 0.0;
    for (auto& value : jagged)
        value = (walk = juce::jlimit (-8.0, 8.0, walk + (random.nextFloat() - 0.5f) * 3.0));
    eq::match::Options options;
    options.detail = 1.0;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto bands = eq::match::fit (jagged, options);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
    WARN ("At full detail: " << bands.size() << " bands in " << juce::String (ms, 1) << " ms");
    CHECK (ms < 150.0);
}
