#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/eq/EqDesign.h"
#include "ui/EqDisplay.h"
#include "ui/EqPage.h"
#include "ui/Footswitch.h"

#include <complex>

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
    // The plugin, with stereo in and out, playing signals through the EQ.
    struct EqRig
    {
        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 256;

        explicit EqRig (int inputs = 2)
        {
            processor.setPlayConfigDetails (inputs, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            set (ParamIDs::eqOn, 1.0f);
        }

        void set (const juce::String& id, float plain)
        {
            auto* p = processor.getParameters().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }

        void setBand (int index, const eq::Band& b)
        {
            set (ParamIDs::eqBand (index, "Shape"), (float) b.shape);
            set (ParamIDs::eqBand (index, "Freq"), (float) b.frequency);
            set (ParamIDs::eqBand (index, "Gain"), (float) b.gainDb);
            set (ParamIDs::eqBand (index, "Q"), (float) b.q);
            set (ParamIDs::eqBand (index, "Slope"), (float) b.slope);
            set (ParamIDs::eqBand (index, "Place"), (float) b.placement);
            set (ParamIDs::eqBand (index, "On"), b.on ? 1.0f : 0.0f);
            set (ParamIDs::eqBand (index, "Used"), b.used ? 1.0f : 0.0f);
        }

        // Plays left and right (functions of the sample index) for `seconds`; returns both outputs.
        std::array<std::vector<float>, 2> play (double seconds, const std::function<float (int)>& left,
                                                const std::function<float (int)>& right)
        {
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            for (int start = 0; start < (int) (seconds * sampleRate); start += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    buffer.setSample (0, i, left (played + i));
                    buffer.setSample (1, i, right (played + i));
                }
                played += blockSize;
                processor.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + blockSize);
            }
            return out;
        }

        ParagonX1AudioProcessor processor;
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
        rig.set (ParamIDs::eqOn, on ? 1.0f : 0.0f);
        if (! on)
            rig.setBand (0, band (eq::Shape::bell, 1000.0, 12.0));   // off: even a band does nothing
        const auto out = rig.play (0.2, tone (440.0), tone (660.0, 0.2f));
        for (int i = 0; i < 2000; ++i)
        {
            REQUIRE (out[0][(size_t) i] == tone (440.0) (i));
            REQUIRE (out[1][(size_t) i] == tone (660.0, 0.2f) (i));
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
        rig.set (ParamIDs::eqBand (0, "Slope"), (float) slopeIndex (48));
        rig.set (ParamIDs::eqBand (0, "Shape"), (float) eq::Shape::lowCut);
        const auto changing = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (changing[0]) < normal * 1.2f);
    }

    SECTION ("Switching the band off and on")
    {
        rig.set (ParamIDs::eqBand (0, "On"), 0.0f);
        const auto off = rig.play (0.1, tone (200.0), tone (200.0));
        rig.set (ParamIDs::eqBand (0, "On"), 1.0f);
        const auto on = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (off[0]) < normal * 1.2f);
        CHECK (largestStep (on[0]) < normal * 1.2f);
    }

    SECTION ("Dragging the gain from +12 to -12 dB in one go glides")
    {
        rig.set (ParamIDs::eqBand (0, "Gain"), -12.0f);
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
            rig.set (ParamIDs::eqGainScale, scale);
            CHECK_THAT (levelDb (rig.play (0.5, tone (1000.0), tone (1000.0))[0], 1000.0), WithinAbs (base + expected, 0.1));
        }
    }

    SECTION ("OUTPUT")
    {
        EqRig rig;
        rig.set (ParamIDs::eqOutput, -9.0f);
        CHECK_THAT (levelDb (rig.play (0.3, tone (500.0), tone (500.0))[1], 500.0), WithinAbs (base - 9.0, 0.05));
    }

    SECTION ("AUTO GAIN takes off the curve's average lift, an octave at a time")
    {
        // +6 dB above 1 kHz is about +6 over the top five octaves of ten, plus the shelf's slope.
        EqRig rig;
        rig.setBand (0, band (eq::Shape::highShelf, 1000.0, 6.0));
        rig.set (ParamIDs::eqAutoGain, 1.0f);
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
    for (int i = 0; i < ParamIDs::eqNumBands; ++i)
    {
        auto b = band (eq::Shape::bell, 300.0 + 100.0 * i, 12.0);
        b.used = false;
        rig.setBand (i, b);
    }
    const auto untouched = rig.play (0.2, tone (700.0), tone (700.0));
    CHECK_THAT (levelDb (untouched[0], 700.0), WithinAbs (20.0 * std::log10 (0.25), 0.01));

    // All 24, the costliest way: 96 dB/oct cuts and shelves, on mid and side.
    for (int i = 0; i < ParamIDs::eqNumBands; ++i)
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

TEST_CASE ("EQ: projects from before 0.4.0, with the graphic EQ on and boosted, come back flat", "[eq][state]")
{
    // 0.3.0's eqOn switched the graphic EQ (whose bands are gone); it's the
    // new EQ's switch now. On, with no bands, it's flat.
    juce::MemoryBlock saved;
    {
        EqRig old;
        old.processor.getStateInformation (saved);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    for (const auto& [id, value] : { std::pair { "eq80", 12.0 }, std::pair { "eqLevel", 6.0 }, std::pair { "eqUltraLo", 1.0 } })
    {
        auto* param = xml->createNewChildElement ("PARAM");
        param->setAttribute ("id", id);
        param->setAttribute ("value", value);
    }
    juce::MemoryBlock oldProject;
    juce::AudioProcessor::copyXmlToBinary (*xml, oldProject);

    EqRig rig;
    rig.processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
    const auto out = rig.play (0.3, tone (80.0), tone (80.0));
    CHECK_THAT (levelDb (out[0], 80.0), WithinAbs (20.0 * std::log10 (0.25), 0.01));
}

//==============================================================================
TEST_CASE ("EQ display: double-click adds a band there, drag moves it, the wheel sets Q, Delete removes it", "[eq][ui]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    EqDisplay display (processor);
    display.setBounds (0, 0, 1000, 400);
    EqBands bands (processor);

    const auto event = [&] (juce::Point<float> at, juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &display, &display, now, at, now, clicks, false);
    };

    // A bell at 1 kHz, +6 dB.
    const juce::Point<float> where { display.xForFrequency (1000.0), display.yForDb (6.0) };
    display.mouseDoubleClick (event (where, {}, 2));
    REQUIRE (bands.get (0, "Used") > 0.5f);
    CHECK ((eq::Shape) juce::roundToInt (bands.get (0, "Shape")) == eq::Shape::bell);
    CHECK_THAT (bands.get (0, "Freq"), WithinAbs (1000.0, 15.0));
    CHECK_THAT (bands.get (0, "Gain"), WithinAbs (6.0, 0.1));
    CHECK (display.getSelected() == 0);

    // At the far left: a low cut.
    display.mouseDoubleClick (event ({ display.xForFrequency (15.0), display.yForDb (0.0) }, {}, 2));
    CHECK ((eq::Shape) juce::roundToInt (bands.get (1, "Shape")) == eq::Shape::lowCut);

    // Drag band 1 to 2 kHz, -3 dB.
    display.refresh();
    display.mouseDown (event (where));
    const juce::Point<float> to { display.xForFrequency (2000.0), display.yForDb (-3.0) };
    const auto now = juce::Time::getCurrentTime();
    display.mouseDrag (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), to, juce::ModifierKeys::leftButtonModifier,
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &display, &display, now, where, now, 1, true));
    display.mouseUp (event (to));
    CHECK_THAT (bands.get (0, "Freq"), WithinAbs (2000.0, 30.0));
    CHECK_THAT (bands.get (0, "Gain"), WithinAbs (-3.0, 0.1));

    // The wheel narrows it.
    display.refresh();
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = 0.5f;
    display.mouseMove (event (to));
    display.mouseWheelMove (event (to), wheel);
    CHECK (bands.get (0, "Q") > 1.5f);

    // Delete.
    CHECK (display.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (bands.get (0, "Used") < 0.5f);
    CHECK (display.getSelected() == -1);
}

TEST_CASE ("EQ page: stomp 4's name opens it; BACK returns to the pedal", "[eq][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    std::vector<Footswitch*> stomps;
    std::vector<EqPage*> pages;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
    {
        if (auto* stomp = dynamic_cast<Footswitch*> (&c))
            stomps.push_back (stomp);
        if (auto* page = dynamic_cast<EqPage*> (&c))
            pages.push_back (page);
        for (auto* child : c.getChildren())
            collect (*child);
    };
    collect (*editor);
    REQUIRE (stomps.size() == 7);
    REQUIRE (pages.size() == 1);
    CHECK_FALSE (pages[0]->isVisible());

    stomps[3]->onSelect();
    CHECK (pages[0]->isVisible());
    CHECK_FALSE (stomps[3]->isVisible());   // the pedal is behind it

    pages[0]->onClose();
    CHECK_FALSE (pages[0]->isVisible());
    CHECK (stomps[3]->isVisible());
}

