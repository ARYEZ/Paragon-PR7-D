#pragma once

#include "dsp/eq/EqDesign.h"
#include "dsp/effects/EffectUtils.h"

#include <array>
#include <atomic>
#include <vector>

// DOMINION (stomp 5, PR7-E 0.10.0): a dynamic resonance suppressor,
// inspired by oeksound soothe 3 as its manual describes it (it finds the
// resonant peaks in the sound as it plays and turns them down with filters
// that follow them). soothe's algorithm isn't published: this is our own
// design, and its numbers are our choices.
//
//   - A bank of filters 1/12 octave apart, 20 Hz to 20 kHz, each listening
//     to its part of the spectrum (a 1/6-octave band pass) and measuring its
//     level.
//   - SOFT: a part is a resonance when it sticks out above the parts around
//     it (their average over up to an octave either side, less for higher
//     DETAIL), whatever the overall level. HARD: when it's over a fixed level
//     (lower for more DEPTH), so it depends on how loud the track is.
//   - DEPTH scales how much of that it takes away, the depth curve (bands
//     you draw, like an EQ) scales it across the spectrum, MAX CUT limits it.
//   - The cuts: a bell for each filter, as narrow as DETAIL says (half an
//     octave to a sixteenth), gains solved so the bells together make the
//     cut wanted (they overlap), moving with ATTACK and RELEASE (faster for
//     higher frequencies, and shaped by the tilts).
//   - Zero latency: no look-ahead (like soothe's low-latency mode).
//   - Stereo: left/right or mid/side, LINK (listen to both together or
//     each alone) and FOCUS (more on one, less on the other).
//   - Then WET TRIM, MIX, DELTA (only what it takes away) and OUT.
namespace dominion
{
    // Saved by index: only ever add to the end.
    enum class Mode { soft, hard };
    inline const juce::StringArray modeNames { "Soft", "Hard" };
    enum class StereoMode { leftRight, midSide };
    inline const juce::StringArray stereoNames { "L/R", "M/S" };
    enum class Shape { lowCut, lowShelf, highShelf, highCut, bell, bandPass, bandReject, tilt };
    inline const juce::StringArray shapeNames { "Low Cut", "Low Shelf", "High Shelf", "High Cut", "Bell",
                                                "Band Pass", "Band Reject", "Tilt" };

    inline constexpr int maxBands = 8;
    inline constexpr double maxDepthDb = 18.0;   // a depth-curve band's range
    inline constexpr double maxCutLimitDb = 40.0;   // MAX CUT at the top: no limit

    // A band of the depth curve: boosting makes it work harder there,
    // cutting leaves that part alone. Band passes add up in parallel
    // (only their parts are worked on); the rest multiply.
    struct Band
    {
        bool used = false, on = true;
        Shape shape = Shape::bell;
        double frequency = 1000.0, depthDb = 0.0, q = 1.0;
        int slope = eq::defaultSlope;
        double focus = 0.0;   // -100 (left / mid only) to 100 (right / side only)
        bool isActive() const noexcept { return used && on; }
    };

    struct Settings
    {
        bool on = false;
        Mode mode = Mode::soft;
        double depth = 5.0;        // 0 to 20
        double detail = 50.0;      // 0 to 100: broad build-ups to narrow resonances
        double attack = 50.0, release = 50.0;   // 0 to 100: fast to slow
        StereoMode stereo = StereoMode::midSide;
        double link = 100.0;       // %
        double focus = 0.0;        // -100 to 100
        double detailTiltLow = 0.0, detailTiltHigh = 0.0, attackTiltLow = 0.0, attackTiltHigh = 0.0,
               releaseTiltLow = 0.0, releaseTiltHigh = 0.0;   // -100 to 100
        double maxCutDb = maxCutLimitDb;
        double wetTrimDb = 0.0, mix = 100.0, outDb = 0.0;
        bool delta = false;
        std::array<Band, maxBands> bands {};
        int listenBand = -1;       // a band being listened to (its part of the delta), or -1
    };

    // The depth curve, in dB, at `hz` for channel 0 (left or mid) or 1 (right or side).
    double depthCurveDb (const Settings&, double hz, int channel, double sampleRate);

    // How the tilts weigh a frequency: 1 in the lows (below about 400 Hz)
    // fading to 0 by 1.2 kHz; 1 in the highs (above about 3 kHz) fading to 0 by 1 kHz.
    double lowWeight (double hz) noexcept;
    double highWeight (double hz) noexcept;

    class Processor
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;
        void process (juce::dsp::AudioBlock<float>, const Settings&) noexcept;

        // For the display (any thread): the filters' frequencies and how far
        // each is cutting now, per channel (left/mid, right/side), in dB (0 or more).
        int getNumFilters() const noexcept { return numFilters; }
        double getFrequency (int filter) const noexcept { return frequencies[(size_t) filter]; }
        float getReductionDb (int channel, int filter) const noexcept { return shown[(size_t) juce::jlimit (0, 1, channel)][(size_t) filter].load(); }

        static constexpr int maxFilters = 128;

    private:
        struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        struct State { double z1 = 0, z2 = 0; };
        static double tick (const Biquad& f, State& s, double x) noexcept
        {
            const auto y = f.b0 * x + s.z1;
            s.z1 = f.b1 * x - f.a1 * y + s.z2;
            s.z2 = f.b2 * x - f.a2 * y;
            return y;
        }

        void updateShape (const Settings&);      // detail and tilts: cut widths, overlaps, timing
        void updateCurve (const Settings&);      // the depth curve at each filter
        void computeTargets (const Settings&, int numChannels, double seconds) noexcept;

        static constexpr int subBlock = 32;
        double sampleRate = 48000.0;
        int numFilters = 0;
        std::array<double, maxFilters> frequencies {};

        // Listening
        std::array<Biquad, maxFilters> detectors {};
        std::array<std::array<State, maxFilters>, 2> detectorStates {};
        std::array<std::array<double, maxFilters>, 2> power {};
        std::array<double, maxFilters> measure {};   // power smoothing per sample

        // Cutting
        std::array<double, maxFilters> cosW {}, alpha {}, attackSeconds {}, releaseSeconds {};
        std::array<int, maxFilters> windowHalf {};   // SOFT's neighbourhood, in filters either side
        // The bells' overlap: how much of bell j's cut lands on filter k (k within reach of j).
        std::array<std::array<float, 2 * 48 + 1>, maxFilters> overlap {};
        int reach = 0;
        std::array<std::array<double, maxFilters>, 2> reduction {};   // the cut wanted, dB, smoothed
        std::array<std::array<double, maxFilters>, 2> gains {};       // each bell's gain, dB
        std::array<std::array<Biquad, maxFilters>, 2> cuts {};
        std::array<std::array<State, maxFilters>, 2> cutStates {};
        std::array<std::array<double, maxFilters>, 2> curve {};       // the depth curve, as a factor

        Settings shapedFor, curvedFor;
        bool shaped = false, curved = false;

        juce::AudioBuffer<float> dry, work;
        effects::BypassFade bypass;
        juce::SmoothedValue<float> mix, wetTrim, out;
        eq::Design listenDesign;
        std::array<std::array<std::array<double, 2>, eq::maxSections>, 2> listenStates {};
        int listening = -1;

        std::array<std::array<std::atomic<float>, maxFilters>, 2> shown {};
    };
}
