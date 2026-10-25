#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "EqDesign.h"

#include <optional>
#include <vector>

// EQ Match (PR7-E 0.7.0): the maths. Inspired by Pro-Q 4's EQ Match, as its
// manual describes it (listen to your track and a reference, then add bands
// that make the one sound like the other); how ours analyses and fits is our
// own design, and its numbers are our choices.
//
//   - Analyser: the long-term average spectrum of some audio (a few seconds
//     to a whole song), from 8192-point FFTs half overlapping. Near-silent
//     moments (below -70 dB) don't count, so pauses don't skew it.
//   - target(): what the EQ should add: reference minus track minus what the
//     EQ's bands already do, smoothed (DETAIL: an octave wide to a sixth of
//     an octave), with the overall level difference taken out (it matches
//     tone, not loudness), ignoring where either has next to nothing (60 dB
//     under its loudest), and limited to +-12 dB.
//   - fit(): bands for that target, one at a time where the difference is
//     biggest (a bell as wide as the bump; a shelf when it runs off either
//     end), each time re-balancing the gains of all of them, until what's
//     left is small or the bands run out. AMOUNT scales the target first.
namespace eq::match
{
    // The points the spectra and targets are kept at: 20 Hz to 20 kHz, twelve an octave.
    const std::vector<double>& grid();

    inline constexpr double minSeconds = 3.0;   // of sound, before a spectrum counts
    inline constexpr double maxTargetDb = 12.0;

    // A long-term average spectrum on grid(), in dB (relative: only differences matter).
    struct Spectrum
    {
        std::vector<double> db;
        double seconds = 0.0;   // of sound it was taken from
        bool isValid() const noexcept { return db.size() == grid().size() && seconds >= minSeconds; }

        // As text (frequency and dB pairs), for a saved reference.
        std::unique_ptr<juce::XmlElement> toXml (const juce::String& name) const;
        static std::optional<Spectrum> fromXml (const juce::XmlElement&);
    };

    class Analyser
    {
    public:
        void reset();
        void add (const float* samples, int numSamples, double sampleRate);   // mono
        Spectrum spectrum() const;
        double seconds() const noexcept { return frames * hop / std::max (1.0, rate); }

    private:
        static constexpr int order = 13, size = 1 << order, hop = size / 2;
        juce::dsp::FFT fft { order };
        juce::dsp::WindowingFunction<float> window { (size_t) size, juce::dsp::WindowingFunction<float>::hann, false };
        std::vector<float> pending, work = std::vector<float> ((size_t) size * 2);
        std::vector<double> power = std::vector<double> ((size_t) size / 2 + 1, 0.0);
        double frames = 0.0, rate = 0.0;
    };

    // Reads an audio file and analyses it all (mixing its channels).
    // `shouldStop` is checked as it goes. Message or background thread.
    std::optional<Spectrum> analyseFile (const juce::File&, const std::function<bool()>& shouldStop = {});

    // `existingDb`: the EQ's current curve on grid() (empty: flat). DETAIL 0 to 1.
    std::vector<double> target (const Spectrum& track, const Spectrum& reference, const std::vector<double>& existingDb, double detail);

    struct Options
    {
        double detail = 0.5;       // 0: a few broad bands; 1: many, matching finer differences
        double amount = 1.0;       // 0 to 1: how much of the difference to make up
        int maxBands = 24;         // no more than the free slots
        double sampleRate = 48000.0;
    };
    std::vector<Band> fit (const std::vector<double>& targetDb, const Options&);

    // The curve of `bands` on grid(), in dB.
    std::vector<double> curveOnGrid (const std::vector<Band>&, double sampleRate);
}
