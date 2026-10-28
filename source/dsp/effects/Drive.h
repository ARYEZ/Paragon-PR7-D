#pragma once

#include "Abyss.h"
#include "Chimera.h"
#include "Cosmonaut.h"
#include "Halo.h"
#include "Rabid.h"
#include "EffectUtils.h"

namespace effects
{
    // Footswitch 2: an overdrive, distortion or fuzz pedal, before the amp, in seven models.
    //
    // SCREAMER is modelled on the Ibanez TS808 Tube Screamer's circuit:
    //
    //   x --+--------------------------------------------(+)--> tone --> level
    //       |                                              ^
    //       +--> 720 Hz low cut -> gain -> diode clipping -+
    //
    // Its op-amp only amplifies above about 720 Hz, and its diodes clip just
    // that amplified part, which is added back on top of the clean guitar.
    // So the bass stays clean and tight and the mids get pushed: the famous
    // mid "hump" that makes it the classic boost in front of a driven amp.
    // The diodes clip both halves of the wave alike (odd harmonics), gently,
    // as diodes in an op-amp's feedback loop do. Its tone control works
    // after a fixed treble cut at 723 Hz, from dark to slightly bright.
    //
    // RABID is modelled on the ProCo RAT 2, op-amp and all (see Rabid.h;
    // PR7-G 0.43.0, replacing the original "Classic" overdrive). DRIVE is its
    // DISTORTION; TONE is its FILTER turned around, so up is brighter, as on
    // the other models.
    //
    // Clipping creates new high harmonics. Without oversampling, the ones
    // above half the sample rate fold back down as harsh, unmusical tones
    // (aliasing). The clipping runs at 8x the rate to keep them out.
    //
    // COSMONAUT is modelled on the green Russian Big Muff, stage by stage
    // from its circuit (see Cosmonaut.h; PR7-G 0.41.0, replacing the
    // original "Muff Fuzz"). DRIVE is its SUSTAIN: two clipping stages give
    // the long sustain, a small capacitor lets the bass through less
    // clipped, and the passive tone stack always scoops the mids.
    //
    // SKYLINE is inspired by the Horizon Devices Precision Drive, a modern
    // metal take on the TS808 above (PR7-G 0.42.0). The same circuit, with:
    //
    //   - ATTACK: an 8-way switch choosing the capacitor in the clipping
    //     stage's 4.7k leg (470 nF down to 33 nF, from builders' traces of
    //     the pedal), so the bass reaching the diodes is cut from about 70 Hz
    //     (full, low-mid punch) to about 1 kHz (tight, pick attack). The
    //     TS808's 47 nF is position 7. TONE is its BRIGHT knob.
    //   - GATE: a gate on the pedal's output, listening to the guitar.
    //
    // CHIMERA is modelled on the Klon Centaur, the "transparent" overdrive
    // (see Chimera.h; PR7-G 0.44.0). DRIVE is its GAIN, TONE its TREBLE.
    //
    // ABYSS is modelled on the Boss MT-2 Metal Zone (see Abyss.h; PR7-G
    // 0.45.0). DRIVE is its DIST; its own HIGH, LOW, MIDDLE and MID FREQ
    // replace TONE.
    //
    // HALO is modelled on the germanium two-transistor fuzz of 1966-68, the
    // whole circuit solved as it is (see Halo.h; PR7-G 0.50.0, replacing
    // PR7-G's earlier, generic FACE FUZZ in the same place in the list).
    // DRIVE is its FUZZ; it has no tone control, so TONE isn't used.
    class Drive
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        enum class Model { rabid, screamer, muff, halo, skyline, chimera, abyss };
        static inline const juce::StringArray modelNames { "Rabid", "Banshee", "Cosmonaut", "Halo", "Skyline", "Chimera", "Abyss" };

        // Skyline's ATTACK positions: the capacitor in nF, position 1 to 8.
        // Saved by number: only append.
        static constexpr float skylineAttackNanofarads[] = { 470.0f, 330.0f, 220.0f, 100.0f, 82.0f, 68.0f, 47.0f, 33.0f };
        static inline const juce::StringArray skylineAttackNames { "1", "2", "3", "4", "5", "6", "7", "8" };
        static constexpr int skylineTubeScreamerAttack = 6;   // 47 nF, the TS808's value

        // Where Skyline's ATTACK cuts the bass into the diodes, in Hz (for tests).
        static float skylineAttackHz (int attack) noexcept;

        struct Settings
        {
            bool on = false;
            Model model = Model::screamer;
            float drive = 5.0f;      // 0 to 10
            float tone = 5.0f;       // 0 to 10
            float levelDb = 0.0f;
            int attack = skylineTubeScreamerAttack;   // Skyline's ATTACK, 0 to 7
            float gate = 0.0f;                        // Skyline's GATE, 0 (off) to 10 (fast and hard)
            float high = 5.0f, low = 5.0f, middle = 5.0f, midFreq = 5.0f;   // Abyss's EQ, 0 to 10
        };

        Drive();
        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings) noexcept;

        static float diodeClip (float x) noexcept;

    private:
        void processAbyss (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processChimera (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processRabid (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processScreamer (juce::dsp::AudioBlock<float> block, const Settings& settings, float lowCutHz, float gate) noexcept;
        void processCosmonaut (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processHalo (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

        double sampleRate = 48000.0;
        juce::dsp::Oversampling<float> oversampling;
        Rabid rabid;
        Chimera chimera;
        Abyss abyss;

        // Screamer: the clean guitar and the clipping path, oversampled together
        // so they stay exactly in step when they're added back together.
        juce::dsp::Oversampling<float> screamerOversampling;
        juce::AudioBuffer<float> screamerPaths;
        juce::dsp::IIR::Filter<float> screamerLowCut;
        float screamerLowCutNow = 0.0f;
        float gateEnvelope = 0.0f, gateGain = 1.0f;   // Skyline's gate
        float clipLowPassState = 0.0f, toneState = 0.0f, outputCouplingState = 0.0f;
        juce::SmoothedValue<float> screamerGain, screamerTone, screamerLevel;
        // The fuzzes' circuits.
        Cosmonaut cosmonaut;
        Halo halo;
        juce::SmoothedValue<float> fuzzGain, fuzzTone, fuzzLevel;
        Model lastModel = Model::screamer;
        BypassFade bypass;
    };
}
