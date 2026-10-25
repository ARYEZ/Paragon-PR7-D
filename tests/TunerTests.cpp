#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/InputTap.h"
#include "dsp/PitchDetector.h"
#include "dsp/Tuner.h"

using Catch::Matchers::WithinAbs;

namespace
{
    double centsBetween (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

    // A plucked-string-like tone: a fundamental plus decaying overtones,
    // with the 2nd harmonic louder than the fundamental (as a guitar's often
    // is), which is exactly what fools simple tuners into reading an octave up.
    std::vector<float> guitarTone (double frequency, double sampleRate, int numSamples)
    {
        std::vector<float> samples ((size_t) numSamples);
        const double gains[] = { 0.5, 0.8, 0.4, 0.3, 0.2, 0.1 };
        for (int i = 0; i < numSamples; ++i)
        {
            double value = 0.0;
            for (int h = 0; h < 6; ++h)
                value += gains[h] * std::sin (juce::MathConstants<double>::twoPi * frequency * (h + 1) * i / sampleRate + h);
            samples[(size_t) i] = (float) (0.3 * value);
        }
        return samples;
    }
}

TEST_CASE ("Tuner reads guitar notes within a cent at common sample rates", "[tuner]")
{
    const auto sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    // Low B (7-string), the six open strings, and a high fret on the top string.
    const auto frequency = GENERATE (30.87, 61.74, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 987.77);

    PitchDetector detector;
    detector.prepare (sampleRate);
    const auto tone = guitarTone (frequency, sampleRate, detector.windowSizeAtHostRate() + 1000);

    const auto result = detector.detect (tone.data(), (int) tone.size());
    INFO ("sample rate " << sampleRate << ", note " << frequency << " Hz");
    REQUIRE (result.has_value());
    CHECK (std::abs (centsBetween (result->frequency, frequency)) < 1.0);
    CHECK (result->clarity > 0.8);
}

TEST_CASE ("Tuner shows nothing for silence or noise", "[tuner]")
{
    PitchDetector detector;
    detector.prepare (48000.0);
    std::vector<float> samples ((size_t) detector.windowSizeAtHostRate(), 0.0f);
    CHECK_FALSE (detector.detect (samples.data(), (int) samples.size()).has_value());

    juce::Random random (42);
    for (auto& s : samples)
        s = random.nextFloat() * 0.5f - 0.25f;
    CHECK_FALSE (detector.detect (samples.data(), (int) samples.size()).has_value());

    // Not enough audio yet: no reading rather than a wrong one.
    CHECK_FALSE (detector.detect (samples.data(), 100).has_value());
}

TEST_CASE ("Tuner names notes and cents", "[tuner]")
{
    auto note = PitchDetector::toNote (440.0);
    CHECK (note.name == "A");
    CHECK (note.octave == 4);
    CHECK_THAT (note.cents, WithinAbs (0.0, 1.0e-9));

    note = PitchDetector::toNote (82.41);
    CHECK (note.name == "E");
    CHECK (note.octave == 2);
    CHECK (std::abs (note.cents) < 1.0);

    note = PitchDetector::toNote (440.0 * std::pow (2.0, 0.2 / 12.0));   // A4 + 20 cents
    CHECK (note.name == "A");
    CHECK_THAT (note.cents, WithinAbs (20.0, 1.0e-6));

    note = PitchDetector::toNote (466.16 * std::pow (2.0, -0.3 / 12.0)); // A#4 - 30 cents
    CHECK (note.name == "A#");
    CHECK_THAT (note.cents, WithinAbs (-30.0, 0.1));

    note = PitchDetector::toNote (30.87);
    CHECK (note.name == "B");
    CHECK (note.octave == 0);

    // A different reference pitch moves everything with it.
    note = PitchDetector::toNote (432.0, 432.0);
    CHECK (note.name == "A");
    CHECK_THAT (note.cents, WithinAbs (0.0, 1.0e-9));
}

TEST_CASE ("Input tap hands the newest audio to another thread in order", "[tuner]")
{
    InputTap tap (8);
    const float first[] = { 1, 2, 3, 4, 5 };
    const float second[] = { 6, 7, 8, 9, 10, 11 };   // wraps around the ring
    tap.write (first, 5);
    tap.write (second, 6);

    float latest[4] {};
    tap.readLatest (latest, 4);
    CHECK (latest[0] == 8.0f);
    CHECK (latest[3] == 11.0f);
}

namespace
{
    // The plugin with its tuner on, playing a recording through it the way a
    // DAW does (512-sample blocks at 48 kHz), the tuner stepping about 30
    // times a second as its timer would.
    struct TunerRig
    {
        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 512;

        explicit TunerRig (int inputs = 1)
        {
            processor.setPlayConfigDetails (inputs, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            auto* tuner = processor.getParameters().getParameter (ParamIDs::tunerOn);
            tuner->setValueNotifyingHost (1.0f);
        }

        // `left (t)` and `right (t)` give the signal at time t (seconds of the recording).
        void play (double seconds, const std::function<float (double)>& left, const std::function<float (double)>& right = {})
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            for (int block = 0; block < (int) (seconds * sampleRate / blockSize); ++block)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto t = (double) played++ / sampleRate;
                    buffer.setSample (0, i, left (t));
                    buffer.setSample (1, i, right ? right (t) : 0.0f);
                }
                processor.processBlock (buffer, midi);
                if (block % 3 == 2)
                    processor.updateTuner();
            }
        }

        const Tuner& tuner() const { return processor.getTuner(); }

        ParagonX1AudioProcessor processor;
        juce::int64 played = 0;
    };

    // A sung or played note: a fundamental and a few harmonics, at
    // `frequency` (Hz, at time t) and a comfortable level.
    std::function<float (double)> note (std::function<double (double)> frequency)
    {
        return [frequency, phase = std::make_shared<double> (0.0), last = std::make_shared<double> (0.0)] (double t)
        {
            *phase += juce::MathConstants<double>::twoPi * frequency (t) * (t - *last);
            *last = t;
            return (float) (0.25 * std::sin (*phase) + 0.12 * std::sin (2.0 * *phase) + 0.05 * std::sin (3.0 * *phase));
        };
    }

    std::function<double (double)> steady (double hz) { return [hz] (double) { return hz; }; }
    double withCents (double hz, double cents) { return hz * std::pow (2.0, cents / 1200.0); }
}

TEST_CASE ("TUNER: the trace follows a recorded part, and the average says how sharp or flat it sits", "[tuner]")
{
    TunerRig rig;

    SECTION ("A vocal note 12 cents sharp, then one 20 cents flat")
    {
        rig.play (3.0, note (steady (withCents (440.0, 12.0))));   // A4, sharp
        REQUIRE (rig.tuner().getReading().hasNote);
        CHECK (rig.tuner().getReading().note.name == "A");
        CHECK_THAT (rig.tuner().getReading().note.cents, WithinAbs (12.0, 1.0));
        auto average = rig.tuner().getAverage (440.0);
        REQUIRE (average.has_value());
        CHECK_THAT (average->cents, WithinAbs (12.0, 1.0));
        CHECK (average->seconds > 2.5);

        rig.play (3.0, note (steady (withCents (293.66, -20.0))));   // D4, flat
        CHECK (rig.tuner().getReading().note.name == "D");
        average = rig.tuner().getAverage (440.0);
        REQUIRE (average.has_value());
        CHECK_THAT (average->cents, WithinAbs (-4.0, 1.5));   // half the time each: (12 - 20) / 2
    }

    SECTION ("A bass line's low notes")
    {
        rig.play (2.0, note (steady (41.20)));   // E1, a bass's open low E
        REQUIRE (rig.tuner().getReading().hasNote);
        CHECK (rig.tuner().getReading().note.name == "E");
        CHECK (rig.tuner().getReading().note.octave == 1);
        CHECK_THAT (rig.tuner().getAverage (440.0)->cents, WithinAbs (0.0, 2.0));
    }

    SECTION ("The trace shows vibrato as it is; the average finds its centre")
    {
        // A held G4 with a singer's vibrato: +-30 cents, 5.5 times a second.
        rig.play (4.0, note ([] (double t) { return withCents (392.0, 30.0 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t)); }));
        double lowest = 1000.0, highest = 0.0;
        for (const auto& point : rig.tuner().getTrace())
            if (point.hasNote())
            {
                lowest = juce::jmin (lowest, point.note);
                highest = juce::jmax (highest, point.note);
            }
        CHECK (highest - lowest > 0.35);   // the swing shows in the trace (a little less than +-30: each reading spans 170 ms)
        CHECK_THAT (rig.tuner().getAverage (440.0)->cents, WithinAbs (0.0, 3.0));
    }

    SECTION ("A part sitting between two notes reads +-50 cents, not 'in tune'")
    {
        // 48 cents sharp of A4, then 48 cents flat of A#4: nearly the same
        // pitch, a quarter tone out. A plain average of +48 and -48 would say 0.
        rig.play (2.0, note (steady (withCents (440.0, 48.0))));
        rig.play (2.0, note (steady (withCents (466.16, -48.0))));
        CHECK (std::abs (rig.tuner().getAverage (440.0)->cents) > 45.0);
    }

    SECTION ("Measured against the reference: a recording tuned to A = 432 Hz")
    {
        rig.play (2.0, note (steady (432.0)));
        CHECK_THAT (rig.tuner().getAverage (440.0)->cents, WithinAbs (-31.8, 1.0));   // 432 Hz is 31.8 cents below 440
        CHECK_THAT (rig.tuner().getAverage (432.0)->cents, WithinAbs (0.0, 1.0));
    }
}

TEST_CASE ("TUNER hears both channels of a stereo track", "[tuner]")
{
    // A part panned hard right: nothing in the left channel at all.
    TunerRig rig (2);
    rig.play (1.0, [] (double) { return 0.0f; }, note (steady (220.0)));
    REQUIRE (rig.tuner().getReading().hasNote);
    CHECK (rig.tuner().getReading().note.name == "A");
    CHECK (rig.tuner().getReading().note.octave == 3);
}

TEST_CASE ("TUNER's trace keeps 30 seconds, stops with the audio, and RESET empties it", "[tuner]")
{
    TunerRig rig;
    rig.play (35.0, note (steady (110.0)));
    const auto& trace = rig.tuner().getTrace();
    REQUIRE_FALSE (trace.empty());
    CHECK (trace.back().seconds - trace.front().seconds <= Tuner::traceSeconds);
    CHECK (trace.back().seconds - trace.front().seconds > Tuner::traceSeconds - 0.1);
    CHECK_THAT (rig.tuner().getAverage (440.0)->seconds, WithinAbs (30.0, 0.5));

    // The DAW stops sending audio: the trace doesn't grow with the last note.
    const auto size = trace.size();
    for (int i = 0; i < 10; ++i)
        rig.processor.updateTuner();
    CHECK (rig.tuner().getTrace().size() == size);

    // Silence adds gaps, not notes.
    rig.play (1.0, [] (double) { return 0.0f; });
    CHECK_FALSE (rig.tuner().getTrace().back().hasNote());

    rig.processor.resetTuner();
    CHECK (rig.tuner().getTrace().empty());
    CHECK_FALSE (rig.tuner().getAverage (440.0).has_value());
}

TEST_CASE ("TUNER only listens while it's on", "[tuner]")
{
    TunerRig rig;
    rig.processor.getParameters().getParameter (ParamIDs::tunerOn)->setValueNotifyingHost (0.0f);
    rig.play (1.0, note (steady (220.0)));
    CHECK (rig.tuner().getTrace().empty());
    CHECK_FALSE (rig.tuner().getReading().hasNote);
}
