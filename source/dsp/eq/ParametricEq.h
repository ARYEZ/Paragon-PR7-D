#pragma once

#include "Dynamics.h"
#include "EqDesign.h"
#include "dsp/effects/EffectUtils.h"

// The EQ stomp's sound: up to 24 bands, each on the stereo signal or only
// its left, right, mid or side, then OUTPUT. Zero latency. See EqDesign.h
// for the filters.
//
//   - Turning a band's frequency, gain or Q glides over 40 ms, redesigning
//     its filters every 32 samples, so dragging never zips.
//   - Changing what a band is (switching it on or off, its shape, slope or
//     placement) crossfades from the old band to the new over 30 ms, so
//     it never clicks.
//   - SOLO plays only the part of the spectrum one band works on (see
//     eq::soloDesign), crossfading in and out.
//   - Dynamic bands (see Dynamics.h) listen to the EQ's input and move
//     their gain every 32 samples, without the glide (their attack and
//     release are the smoothing). RANGE and THRESHOLD glide as gain does.
//   - AUTO GAIN is an estimate from the curve, as in Pro-Q: the curve's
//     average change in level over the audible range, taken off. Not a
//     measurement of the music.
namespace eq
{
    class ParametricEq
    {
    public:
        static constexpr int numBands = 24;

        struct Settings
        {
            bool on = false;
            std::array<Band, numBands> bands {};
            double outputDb = 0.0;
            double gainScale = 1.0;   // 0 to 2: how much of each band's gain applies
            bool autoGain = false;
            int soloBand = -1;        // a band being soloed, or -1
        };

        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        // One channel (mono) or two (left, right). On mono, the left, right
        // and side bands do nothing and mid bands work on the whole signal.
        void process (juce::dsp::AudioBlock<float>, const Settings&) noexcept;

        // AUTO GAIN's current correction, in dB (0 when it's off).
        double getAutoGainDb() const noexcept { return autoGainDb; }

        // A band's dynamics now, for the display (any thread): how far its
        // gain has moved, the level it hears and the threshold it uses, in
        // dB. `active` is false for a static band.
        struct DynamicReading
        {
            bool active = false;
            float changeDb = 0.0f, levelDb = (float) dynamics::floorDb, thresholdDb = 0.0f;
        };
        DynamicReading getDynamics (int band) const noexcept;

        // The EQ's curve, in dB at `hz`, as heard on `which`: the stereo
        // bands, plus that placement's own (all bands except left, right,
        // mid and side ones for Placement::stereo). For the display.
        static double curveDb (const Settings&, double hz, double sampleRate, Placement which) noexcept;

        // The estimate AUTO GAIN takes off: the stereo curve's average dB
        // from 20 Hz to 20 kHz, each octave counted equally (bands on one
        // side count half). Dynamic bands count at their set gain.
        static double averageChangeDb (const Settings&, double sampleRate) noexcept;

    private:
        static constexpr int subBlock = 32;

        struct State
        {
            std::array<std::array<double, 2>, maxSections> z {};   // per section: the filter's two memories
        };

        struct Running
        {
            Design design;
            Placement placement = Placement::stereo;
            std::array<State, effects::maxChannels> states {};
        };

        // What a dynamic band hears (see Dynamics.h).
        struct Detector
        {
            bool running = false;
            Design filter;   // SOLO's filter for the band
            std::array<State, 2> states {};
            double power = 0.0, levelDb = dynamics::floorDb;
            double typicalDb = 0.0, heardSeconds = 0.0;   // for AUTO
            bool heard = false;       // typicalDb has been set
        };

        struct BandSlot
        {
            Band applied;   // the band as currently designed (smoothed values in frequency, gain, q)
            juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> frequency, q;
            juce::SmoothedValue<double> gain;
            Running current, previous;
            double fade = 1.0;   // 1: only `current`; below 1: still crossfading from `previous`

            Detector detector;
            juce::SmoothedValue<double> range, threshold;
            double changeDb = 0.0;   // the dynamic gain now
        };

        static bool sameStructure (const Band& a, const Band& b) noexcept;
        void updateBand (int index, const Band& target, double gainScale, bool gainScaleMoved,
                         const float* dryLeft, const float* dryRight, int numSamples, int numChannels) noexcept;
        void detect (BandSlot&, const Band& target, const float* left, const float* right, int numSamples, int numChannels) noexcept;
        static void run (Running&, float* left, float* right, int numSamples, int numChannels) noexcept;
        static double tick (const Design&, State&, double x) noexcept;

        double sampleRate = 48000.0;
        std::array<BandSlot, numBands> slots;
        struct Reading { std::atomic<bool> active { false }; std::atomic<float> changeDb { 0.0f }, levelDb { 0.0f }, thresholdDb { 0.0f }; };
        std::array<Reading, numBands> readings;
        juce::SmoothedValue<double> gainScale;
        juce::AudioBuffer<float> scratch, dry;

        // SOLO
        Running solo;
        int soloBand = -1;
        juce::SmoothedValue<float> soloAmount;

        juce::SmoothedValue<float> output;
        double autoGainDb = 0.0;
        int samplesSinceAutoGain = 0;
        bool autoGainStale = true;
        effects::BypassFade bypass;
    };
}
