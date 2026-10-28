#pragma once

#include "EffectUtils.h"

namespace effects
{
    // PR7-G 0.52.0's LO-FI block (PR7-K stage 7c: after MOD 2, before the MASTER rack). On PR7-G the three speakers
    // are mono; PR7-K keeps its stereo (each side through its own speaker). On PR7-G it's last in the chain, after
    // the reverb, so the whole
    // sound (echoes and reverb included) goes "through the phone", the way
    // intros and breakdowns are made in the studio. Five types:
    //
    //   telephone   a phone line: only 300 Hz to 3.4 kHz gets through,
    //               steeply, with a little carbon-mic grit
    //   amRadio     a small radio speaker: wider than a phone, a nasal
    //               midrange bump, gentle saturation
    //   megaphone   a horn speaker: narrow, honky and driven hard
    //   cassette    a worn tape: wobbling pitch (wow and flutter), a dull
    //               top end, tape saturation and hiss
    //   bitcrusher  digital decimation: fewer bits and a lower sample rate
    //
    // AMOUNT is how strong the effect is (narrower, dirtier, wobblier,
    // crunchier); NOISE adds the line hiss, tape hiss or static that sells it.
    class LoFi
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        enum class Type { telephone, amRadio, megaphone, cassette, bitcrusher };
        static inline const juce::StringArray typeNames { "Telephone", "AM Radio", "Megaphone", "Cassette", "Bitcrusher" };

        struct Settings
        {
            bool on = false;
            Type type = Type::telephone;
            float amount = 5.0f;    // 0 to 10
            float noise = 3.0f;     // 0 to 10
            float mix = 1.0f;       // 0 to 1
            float levelDb = 0.0f;
        };

        LoFi();
        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

        // Exposed for tests: the soft clipper the speaker types use.
        static float saturate (float x, float drive) noexcept;

        // PR7-K, for the LO-FI page (and used by the processing itself, so they can't drift apart): the band a
        // speaker or the tape lets through at an AMOUNT, and the bitcrusher's bits and how many samples it holds.
        struct Band { float lowHz, highHz, bumpHz, bumpGain; };   // bumpGain: the speaker's resonance (0: none)
        static Band bandFor (Type, float amount, double sampleRate) noexcept;
        static float crusherBits (float amount) noexcept;
        static int crusherHold (float amount, double sampleRate) noexcept;

        // PR7-K: the hiss's and the tape's wander's random numbers from a set start, so a test hears the same each time.
        void seedRandom (juce::int64 seed) noexcept { random.setSeed (seed); }

    private:
        void setUpFilters (Type type, float amount) noexcept;
        void processSpeaker (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processCassette (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processBitcrusher (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        float nextNoise() noexcept;

        double sampleRate = 48000.0;
        BypassFade bypass;
        juce::SmoothedValue<float> mix, level;
        juce::Random random;

        // Two 12 dB/octave stages each way make the steep band edges a phone
        // line has; the band-pass adds a speaker's resonant bump.
        std::array<juce::dsp::StateVariableTPTFilter<float>, 2> lowCuts, highCuts;
        juce::dsp::StateVariableTPTFilter<float> resonance;
        float resonanceGain = 0.0f, drive = 1.0f;
        Type lastType = Type::telephone;
        float lastAmount = -1.0f;
        float noiseTone[maxChannels] {};

        // Cassette
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> tape;
        float wowPhase = 0.0f, flutterPhase = 0.0f, drift = 0.0f, driftTarget = 0.0f;
        int driftCountdown = 0;

        // Bitcrusher: the held sample and how long until the next one.
        float heldSample[maxChannels] {};
        int holdCountdown = 0;
    };
}
