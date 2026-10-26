#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "dsp/effects/Compressor.h"
#include "dsp/effects/EffectUtils.h"
#include "dsp/eq/EqDesign.h"

// COMP's bands (PR7-E 0.13.0): multiband compression and expansion,
// inspired by FabFilter Pro-MB as its manual describes it. Our own design:
// Pro-MB's "Dynamic Phase" isn't published.
//
//   - Up to six bands, placed anywhere: a band is the part of the spectrum
//     between its LOW and HIGH edges (the lowest reaching down to the
//     bottom, the highest up to the top), with edges as steep as SLOPE.
//   - No crossovers splitting the sound: each band is a pair of shelves on
//     the whole signal (from the EQ's matched designs), one changing the
//     level from its low edge up, one changing it back from its high edge.
//     The band is flat in its middle and half way at each edge, so two
//     bands sharing an edge join as crossover bands do. A band whose gain
//     isn't changing is exactly nothing (a 0 dB shelf is no filter at
//     all): no delay, and the sound's phase only moves while a band works.
//   - Each band hears its part of the input through a band pass (the same
//     edges and slope), with COMP's detectors: VCA and OPTO follow its
//     loudness (RMS; OPTO slower, with a release that stretches the longer
//     it works), FET every peak. (FET's grit is the full-band COMP's only.)
//   - Four kinds of dynamics, from MODE and RANGE's sign (as on Pro-MB):
//     COMPRESS with a negative RANGE turns loud parts down (downward
//     compression); with a positive one, it brings quiet parts up (upward
//     compression). EXPAND with a negative RANGE turns quiet parts down
//     (expansion, gating); with a positive one, loud parts up (upward
//     expansion, more punch). RATIO scales it, KNEE softens it round the
//     threshold, |RANGE| is the most it moves.
//   - STEREO: both channels (LINK sets how much they share one reading), or
//     the middle or the sides only. OUTPUT: the band's own level.
//   - LOOK-AHEAD: switched on, the whole of COMP is 20 ms late (reported to
//     the DAW, which makes up for it on playback), and each band hears up
//     to 20 ms ahead of what it's changing, so it can be ready for a peak.
//     Off: no delay at all.
//   - MIX: from 0 (none) to 200% (twice the effect), as Pro-MB's.
namespace effects::multiband
{
    inline constexpr int maxBands = 6;
    inline constexpr double lookaheadSeconds = 0.02;
    inline constexpr float minHz = 20.0f, maxHz = 20000.0f;   // an edge at either end: none (the band reaches the end)
    inline constexpr float maxRangeDb = 30.0f;

    // Saved by index: only ever add to the end.
    enum class Mode { compress, expand };
    inline const juce::StringArray modeNames { "Compress", "Expand" };
    enum class Stereo { stereo, mid, side };
    inline const juce::StringArray stereoNames { "Stereo", "Mid", "Side" };
    // Edge slopes: the EQ's first seven (6 to 48 dB/oct), by the EQ's index.
    inline constexpr int numSlopes = 7, defaultSlope = 3;   // 24 dB/oct

    struct Band
    {
        bool used = false, on = true;
        float lowHz = minHz, highHz = maxHz;
        int slope = defaultSlope;
        float thresholdDb = -20.0f, rangeDb = -12.0f, ratio = 4.0f, kneeDb = 6.0f;
        float attackMs = 10.0f, releaseMs = 150.0f;
        Mode mode = Mode::compress;
        Compressor::Character character = Compressor::Character::vca;
        Stereo stereo = Stereo::stereo;
        float link = 1.0f;          // 0 to 1
        float outputDb = 0.0f;
        float lookaheadMs = 0.0f;   // 0 to 20, while look-ahead is on

        bool isActive() const noexcept { return used && on; }
        bool hasLowEdge() const noexcept { return lowHz > minHz * 1.001f; }
        bool hasHighEdge() const noexcept { return highHz < maxHz * 0.999f; }
    };

    struct Settings
    {
        std::array<Band, maxBands> bands {};
        bool lookahead = false;
        float mix = 1.0f;   // 0 to 2
        int soloBand = -1;  // a band to hear on its own (its part of the sound, as processed), or -1
    };

    // The static curve: how far a band moves the level (dB: - down, + up)
    // for a steady `levelDb` in its part of the sound.
    float gainChangeDb (float levelDb, const Band&) noexcept;

    // A band's filters for a gain change of `gainDb` (its shelves; nothing for 0 dB).
    eq::Design bandDesign (const Band&, double gainDb, double sampleRate) noexcept;
    // What a band hears: its part of the spectrum.
    eq::Design detectorDesign (const Band&, double sampleRate) noexcept;

    class Processor
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;

        // One channel or two (left, right), in place. `on` is COMP's switch
        // (the bands fade in and out with it). With look-ahead on, the sound
        // comes out lookaheadSamples() late whether COMP is on or not, so the
        // delay the DAW was told about never changes under it.
        void process (juce::dsp::AudioBlock<float>, const Settings&, bool on) noexcept;

        int lookaheadSamples() const noexcept { return lookaheadLength; }

        // A band now, for the page (any thread): how far it's moving the
        // level, and the level it hears (dB).
        struct Reading
        {
            float changeDb = 0.0f, levelDb = -100.0f;
        };
        Reading getReading (int band) const noexcept;

    private:
        static constexpr int subBlock = 32;
        using Memory = std::array<std::array<double, 2>, eq::maxSections>;

        struct Running
        {
            bool active = false;
            Band shape;                                    // the edges and slope the filters were made for
            eq::Design detector;
            std::array<Memory, 2> detectorMemory {}, bandMemory {}, soloMemory {};
            std::array<eq::Design, 2> design {};           // per channel (one for mid or side)
            std::array<double, 2> designedDb {};
            std::array<float, 2> meanSquare {}, peak {}, changeDb {}, stretch {};
            std::atomic<float> shownChangeDb { 0.0f }, shownLevelDb { -100.0f };
        };

        static double run (const eq::Design&, Memory&, double x) noexcept;
        void startBand (int index, const Band&) noexcept;

        double sampleRate = 48000.0;
        int lookaheadLength = 0, historyLength = 0, written = 0;
        bool fresh = true;
        std::array<std::vector<float>, 2> history;   // the input, for look-ahead and the detectors
        std::array<Running, maxBands> bands;
        BypassFade bypass;
        juce::AudioBuffer<float> dry;
        juce::SmoothedValue<float> mix { 1.0f };
    };
}
