// Shared by the synth's tests: an engine to play, and ways to measure what comes out.
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_dsp/juce_dsp.h>

#include "dsp/synth/Engine.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // The engine with its settings, played a block at a time.
    struct Player
    {
        Player()
        {
            engine.prepare (sampleRate);
            engine.seedRandom (1);   // the same random choices every run, on every computer
            settings.oscillators[0].randomPhase = false;   // the same start every time, for repeatable tests
        }

        synth::OscSettings& osc (int index = 0) { return settings.oscillators[(size_t) index]; }

        void on (int note, float velocity = 1.0f) { engine.noteOn (note, velocity, settings); }
        void off (int note) { engine.noteOff (note, settings); }
        // A MIDI message, played at the start of the next render.
        void midi (const juce::MidiMessage& message) { pending.addEvent (message, 0); }

        // Renders `seconds`; returns left and right.
        std::array<std::vector<float>, 2> render (double seconds)
        {
            std::array<std::vector<float>, 2> out;
            const auto total = (int) (seconds * sampleRate);
            for (auto& channel : out)
                channel.resize ((size_t) total);
            for (int start = 0; start < total; start += blockSize)
            {
                const auto n = juce::jmin (blockSize, total - start);
                engine.process (out[0].data() + start, out[1].data() + start, n, pending, 0, settings);
                pending.clear();
            }
            return out;
        }

        synth::Engine engine;
        synth::Settings settings;
        juce::MidiBuffer pending;
    };

    inline double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += (double) x[i] * x[i];
        return std::sqrt (sum / (double) std::max<size_t> (1, to - from));
    }

    inline double db (double gain) { return 20.0 * std::log10 (juce::jmax (1.0e-12, gain)); }

    // The level of `hz` in samples [from, from + length), as a fraction of full scale (Goertzel, whole cycles).
    inline double levelAt (const std::vector<float>& x, double hz, size_t from, size_t length)
    {
        const auto cycles = std::floor ((double) length * hz / sampleRate);
        length = std::max<size_t> (1, (size_t) std::round (cycles * sampleRate / hz));
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < from + length; ++i)
        {
            const auto s0 = x[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) length;
    }

    // Frequency of a steady tone from its rising zero crossings, in [from, to).
    inline double frequencyOf (const std::vector<float>& x, size_t from, size_t to)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (auto i = from + 1; i < to; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const auto exact = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0.0) first = exact; else ++crossings;
                last = exact;
            }
        return crossings > 0 ? sampleRate * crossings / (last - first) : 0.0;
    }

    // A spectrum (dB per bin, 0 dB = the loudest bin) of 65536 samples from `from`, Blackman-Harris windowed.
    struct Spectrum
    {
        static constexpr int order = 16, size = 1 << order;
        static constexpr double binHz = sampleRate / size;
        std::vector<double> db;

        Spectrum (const std::vector<float>& x, size_t from)
        {
            REQUIRE (x.size() >= from + (size_t) size);
            juce::dsp::FFT fft (order);
            std::vector<float> data ((size_t) size * 2, 0.0f);
            for (int i = 0; i < size; ++i)
            {
                const auto t = juce::MathConstants<double>::twoPi * i / (size - 1);
                const auto window = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
                data[(size_t) i] = (float) (x[from + (size_t) i] * window);
            }
            fft.performFrequencyOnlyForwardTransform (data.data());
            const auto peak = *std::max_element (data.begin(), data.begin() + size / 2);
            for (int i = 0; i < size / 2; ++i)
                db.push_back (20.0 * std::log10 (juce::jmax (1.0e-12, (double) data[(size_t) i] / peak)));
        }

        // The loudest bin below `maxHz` that isn't within `guardHz` of a harmonic of `f0`.
        double loudestBetweenHarmonics (double f0, double maxHz, double guardHz = 12.0) const
        {
            auto loudest = -400.0;
            for (int bin = (int) (30.0 / binHz); bin < (int) (maxHz / binHz); ++bin)
            {
                const auto hz = bin * binHz;
                const auto nearest = std::round (hz / f0) * f0;
                if (std::abs (hz - nearest) > guardHz)
                    loudest = std::max (loudest, db[(size_t) bin]);
            }
            return loudest;
        }

        double at (double hz) const
        {
            const auto bin = (int) std::round (hz / binHz);
            auto loudest = -400.0;
            for (int b = bin - 3; b <= bin + 3; ++b)
                loudest = std::max (loudest, db[(size_t) b]);
            return loudest;
        }
    };

    inline double noteHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
}

