// PR7-G 0.52.0's DRIVE and GLITCH tests (stage 7d), unchanged but for:
//  - the rig: PR7-K has no audio input, so `EffectRig` stands in for PR7-G's plugin. It keeps the settings in a
//    PR7-K processor's parameters, insert bus A's ("ins1Drive..." for PR7-G's "drive...": the same ranges and
//    defaults, read by the same getInsertDriveSettings / getInsertGlitchSettings the plugin uses), and runs the input
//    through the DRIVE then the GLITCH, one of each a side as PR7-K's buses do (PR7-G's are mono);
//  - left out: the expression pedal on GLITCH, GLITCH's PRE / POST, and the noise gate (not in PR7-K).
// The buses in the synth are tested in InsertTests.cpp.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/Glitch.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    namespace GIds
    {
        const juce::String driveAttack = ParamIDs::ins (0, "DriveAttack");
        const juce::String driveGain = ParamIDs::ins (0, "DriveGain");
        const juce::String driveGate = ParamIDs::ins (0, "DriveGate");
        const juce::String driveHigh = ParamIDs::ins (0, "DriveHigh");
        const juce::String driveLow = ParamIDs::ins (0, "DriveLow");
        const juce::String driveMidFreq = ParamIDs::ins (0, "DriveMidFreq");
        const juce::String driveMiddle = ParamIDs::ins (0, "DriveMiddle");
        const juce::String driveModel = ParamIDs::ins (0, "DriveModel");
        const juce::String driveOn = ParamIDs::ins (0, "DriveOn");
        const juce::String driveTone = ParamIDs::ins (0, "DriveTone");
        const juce::String glitchChaos = ParamIDs::ins (0, "GlitchChaos");
        const juce::String glitchDensity = ParamIDs::ins (0, "GlitchDensity");
        const juce::String glitchDivision = ParamIDs::ins (0, "GlitchDivision");
        const juce::String glitchMix = ParamIDs::ins (0, "GlitchMix");
        const juce::String glitchMode = ParamIDs::ins (0, "GlitchMode");
        const juce::String glitchOn = ParamIDs::ins (0, "GlitchOn");
        const juce::String glitchPitch = ParamIDs::ins (0, "GlitchPitch");
        const juce::String glitchSync = ParamIDs::ins (0, "GlitchSync");
        const juce::String glitchTime = ParamIDs::ins (0, "GlitchTime");
    }

    // The song's position, as PR7-G's tests' playhead gives it.
    struct PlayHead
    {
        double bpm = 120.0;
        bool playing = false;
        double startBeats = 0.0;
    };

    struct EffectRig
    {
        EffectRig()
        {
            for (int c = 0; c < 2; ++c)
            {
                drive[(size_t) c].prepare (sampleRate, blockSize);
                glitch[(size_t) c].prepare (sampleRate, blockSize);
            }
        }

        void set (const juce::String& id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        std::vector<std::vector<float>> run (const std::vector<float>& input)
        {
            std::vector<std::vector<float>> output (2);
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (size_t start = 0; start < input.size(); start += blockSize)
            {
                const auto count = (int) std::min<size_t> (blockSize, input.size() - start);
                buffer.setSize (2, count, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    std::copy (input.begin() + (long) start, input.begin() + (long) start + count, buffer.getWritePointer (ch));
                juce::dsp::AudioBlock<float> block (buffer);
                const auto songBeats = playHead.playing ? playHead.startBeats + (double) start / sampleRate * playHead.bpm / 60.0 : -1.0;
                for (size_t c = 0; c < 2; ++c)
                {
                    drive[c].process (block.getSingleChannelBlock (c), processor.getInsertDriveSettings (0));
                    glitch[c].process (block.getSingleChannelBlock (c), processor.getInsertGlitchSettings (0), playHead.bpm, songBeats);
                }
                for (int ch = 0; ch < 2; ++ch)
                    output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + count);
            }
            return output;
        }

        std::vector<float> runMono (const std::vector<float>& input) { return run (input)[0]; }

        PlayHead playHead;
        ParagonX1AudioProcessor processor;
        std::array<effects::Drive, 2> drive;
        std::array<effects::Glitch, 2> glitch;
    };

    std::vector<float> sine (double hz, float amplitude, double seconds)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        return samples;
    }

    std::vector<float> impulse (double seconds, float amplitude = 1.0f)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate), 0.0f);
        samples[0] = amplitude;
        return samples;
    }

    std::vector<float> silence (double seconds) { return std::vector<float> ((size_t) (seconds * sampleRate), 0.0f); }

    float rms (const std::vector<float>& samples, double fromSeconds = 0.0, double toSeconds = 1.0e9)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto to = std::min (samples.size(), (size_t) (toSeconds * sampleRate));
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += samples[i] * samples[i];
        return (float) std::sqrt (sum / (double) std::max<size_t> (1, to - from));
    }

    float dB (float ratio) { return 20.0f * std::log10 (ratio); }

    // Level of one frequency in a signal (Goertzel algorithm), skipping the
    // first `fromSeconds` so filters and fades have settled.
    float levelAt (const std::vector<float>& samples, double hz, double fromSeconds = 0.1)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto n = samples.size() - from;
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (float) (2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) n);
    }

    bool allFinite (const std::vector<float>& samples)
    {
        return std::all_of (samples.begin(), samples.end(), [] (float s) { return std::isfinite (s); });
    }

    float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
    {
        float result = 0.0f;
        for (auto i = from; i < std::min (a.size(), b.size()); ++i)
            result = std::max (result, std::abs (a[i] - b[i]));
        return result;
    }
}

TEST_CASE ("GLITCH: each mode chops, repeats or smears the guitar as it says", "[effects][glitch]")
{
    const auto glitchRig = [] (EffectRig& rig, effects::Glitch::Mode mode, float density, float chaos, float pitch = 0.0f, float mixAmount = 1.0f)
    {
        rig.set (GIds::glitchOn, 1.0f);
        rig.set (GIds::glitchMode, (float) mode);
        rig.set (GIds::glitchSync, 0.0f);
        rig.set (GIds::glitchTime, 100.0f);   // 4800 samples at 48 kHz
        rig.set (GIds::glitchDensity, density);
        rig.set (GIds::glitchChaos, chaos);
        rig.set (GIds::glitchPitch, pitch);
        rig.set (GIds::glitchMix, mixAmount);
    };
    // Noise: every stretch of it different, so a repeat is easy to recognise.
    std::vector<float> noise ((size_t) sampleRate);
    juce::Random random (42);
    for (auto& sample : noise)
        sample = (random.nextFloat() * 2.0f - 1.0f) * 0.3f;

    SECTION ("MIX at 0 leaves the guitar as it was, in every mode")
    {
        for (const auto mode : { effects::Glitch::Mode::pitchDelay, effects::Glitch::Mode::sequence,
                                 effects::Glitch::Mode::interfere, effects::Glitch::Mode::texturizer })
        {
            EffectRig rig;
            glitchRig (rig, mode, 10.0f, 10.0f, 7.0f, 0.0f);
            const auto output = rig.runMono (noise);
            REQUIRE (allFinite (output));
            CHECK (maxAbsDifference (output, noise, (size_t) (0.05 * sampleRate)) < 1.0e-3f);
        }
    }

    SECTION ("Sequence, every step and no chaos: each step repeats the one before it")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::sequence, 10.0f, 0.0f);
        const auto output = rig.runMono (noise);
        constexpr size_t step = 4800;
        // The middle of the fourth step is the third step's audio (clear of the short fades at the edges).
        std::vector<float> got (output.begin() + 3 * step + 200, output.begin() + 4 * step - 200);
        std::vector<float> expected (noise.begin() + 2 * step + 200, noise.begin() + 3 * step - 200);
        CHECK (maxAbsDifference (got, expected) < 0.01f);
    }

    SECTION ("Interfere: the signal drops out in places")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::interfere, 10.0f, 0.0f);
        const auto output = rig.runMono (noise);
        REQUIRE (allFinite (output));
        auto silentStretches = 0;
        for (double start = 0.1; start < 0.95; start += 0.005)
            if (rms (output, start, start + 0.005) < 0.01f * rms (noise))
                ++silentStretches;
        CHECK (silentStretches >= 3);
    }

    SECTION ("Pitch Delay: each echo an octave above the last")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::pitchDelay, 4.0f, 0.0f, 12.0f);
        auto input = sine (330.0, 0.3f, 0.3);
        const auto tail = silence (0.7);
        input.insert (input.end(), tail.begin(), tail.end());
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        // After the note stops, the echoes are up an octave (and more), not at the note.
        std::vector<float> echoes (output.begin() + (size_t) (0.35 * sampleRate), output.begin() + (size_t) (0.5 * sampleRate));
        CHECK (dB (levelAt (echoes, 660.0, 0.0) / (levelAt (echoes, 330.0, 0.0) + 1.0e-9f)) > 10.0f);
    }

    SECTION ("Texturizer: a cloud of grains that carries on after the note")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::texturizer, 6.0f, 2.0f);
        auto input = sine (440.0, 0.3f, 0.6);
        const auto tail = silence (0.6);
        input.insert (input.end(), tail.begin(), tail.end());
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        CHECK (rms (output, 0.65, 0.75) > 0.02f);                     // still sounding after the note
        CHECK (dB (rms (output, 0.2, 0.55) / 0.212f) > -12.0f);       // about as loud as the guitar while playing
    }

    SECTION ("Synced, with the song playing: steps start on the song's grid lines")
    {
        // 1/16 at 120 BPM: steps of a quarter of a beat (6000 samples). The
        // song starts 0.1 beats in, so the first grid line comes 3600 samples
        // in, then every 6000. Every step repeats the one before it, so the
        // step from 9600 is a repeat of the audio from 3600 to 9600.
        const auto run = [&] (bool playing)
        {
            EffectRig rig;
            glitchRig (rig, effects::Glitch::Mode::sequence, 10.0f, 0.0f);
            rig.set (GIds::glitchSync, 1.0f);
            rig.set (GIds::glitchDivision, 3.0f);   // 1/16
            rig.playHead.playing = playing;
            rig.playHead.startBeats = 0.1;
            return rig.runMono (noise);
        };
        const auto matchesGrid = [&] (const std::vector<float>& output)
        {
            std::vector<float> got (output.begin() + 9600 + 200, output.begin() + 15600 - 200);
            std::vector<float> expected (noise.begin() + 3600 + 200, noise.begin() + 9600 - 200);
            return maxAbsDifference (got, expected) < 0.01f;
        };
        CHECK (matchesGrid (run (true)));
        CHECK_FALSE (matchesGrid (run (false)));   // stopped: steps count from switch-on (0, 6000, 12000...)
    }

    SECTION ("Killswitch: each step plays, then cuts to silence for its last part")
    {
        // DENSITY 5: the last half of each 100 ms step (4800 samples) is cut.
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::killswitch, 5.0f, 0.0f);
        const auto output = rig.runMono (noise);
        for (int step = 2; step < 8; ++step)
        {
            const auto at = [&] (int sample) { return (double) (step * 4800 + sample) / sampleRate; };
            CHECK (rms (output, at (2600), at (4700)) < 1.0e-3f);                            // cut
            CHECK (rms (output, at (300), at (2200)) > 0.8f * rms (noise, at (300), at (2200)));   // playing
        }
    }
}

TEST_CASE ("Fuzzes: Cosmonaut sustains with odd harmonics and scoops the mids; Halo clips unevenly and cleans up", "[effects][drive]")
{
    const auto play = [] (effects::Drive::Model model, float amplitude, float drive = 5.0f, float tone = 5.0f, double hz = 220.0)
    {
        EffectRig rig;
        rig.set (GIds::driveOn, 1.0f);
        rig.set (GIds::driveModel, (float) model);
        rig.set (GIds::driveGain, drive);
        rig.set (GIds::driveTone, tone);
        const auto output = rig.runMono (sine (hz, amplitude, 1.0));
        REQUIRE (allFinite (output));
        return output;
    };

    SECTION ("At LEVEL 0 dB both sit near the guitar's own level; Cosmonaut gets louder with SUSTAIN, as the pedal does")
    {
        for (const auto drive : { 2.0f, 5.0f, 9.0f })
        {
            INFO ("Halo, FUZZ " << drive);
            CHECK (std::abs (dB (rms (play (effects::Drive::Model::halo, 0.3f, drive), 0.3) / (0.3f / std::sqrt (2.0f)))) < 3.0f);
        }
        const auto cosmonautDb = [&] (float drive) { return dB (rms (play (effects::Drive::Model::muff, 0.3f, drive), 0.3) / (0.3f / std::sqrt (2.0f))); };
        CHECK_THAT (cosmonautDb (5.0f), WithinAbs (0.0, 1.5));
        CHECK (cosmonautDb (9.0f) > cosmonautDb (2.0f));
        CHECK (cosmonautDb (9.0f) - cosmonautDb (2.0f) < 8.0f);
    }

    SECTION ("Cosmonaut: the bass is clipped far less than the mids (the 0.047 uF in series with its diodes)")
    {
        // The share of the output that is harmonics: a low E against a note two octaves up.
        const auto distortion = [&] (double hz)
        {
            const auto output = play (effects::Drive::Model::muff, 0.2f, 7.0f, 5.0f, hz);
            const auto fundamental = levelAt (output, hz, 0.3);
            return dB ((levelAt (output, 3.0 * hz, 0.3) + levelAt (output, 5.0 * hz, 0.3)) / fundamental);
        };
        CHECK (distortion (82.4) < distortion (330.0) - 3.0f);
    }

    SECTION ("Cosmonaut compresses: ten times the picking strength is far less than ten times louder")
    {
        const auto level = [&] (float amplitude) { return rms (play (effects::Drive::Model::muff, amplitude, 7.0f, 5.0f, 1000.0), 0.3); };
        CHECK (dB (level (0.3f) / level (0.03f)) < 8.0f);
    }

    SECTION ("Cosmonaut clips both halves alike (odd harmonics); Halo clips one harder (a strong 2nd)")
    {
        const auto secondToThird = [&] (effects::Drive::Model model)
        {
            const auto output = play (model, 0.3f, 7.0f, 7.0f);
            return dB (levelAt (output, 440.0, 0.3) / levelAt (output, 660.0, 0.3));
        };
        CHECK (secondToThird (effects::Drive::Model::halo) > secondToThird (effects::Drive::Model::muff) + 10.0f);
    }

    SECTION ("Halo cleans up as the guitar's volume comes down")
    {
        const auto harmonics = [&] (float amplitude)
        {
            const auto output = play (effects::Drive::Model::halo, amplitude, 5.0f);
            return dB ((levelAt (output, 440.0, 0.3) + levelAt (output, 660.0, 0.3)) / levelAt (output, 220.0, 0.3));
        };
        CHECK (harmonics (0.3f) > harmonics (0.01f) + 10.0f);
    }

    SECTION ("Cosmonaut's TONE scoops the mids at noon")
    {
        // A quiet 1 kHz tone (still fuzzed), against 250 Hz, through the tone stack.
        const auto midsAgainstLows = [&] (float tone)
        {
            const auto mids = play (effects::Drive::Model::muff, 0.3f, 5.0f, tone, 1000.0);
            const auto lows = play (effects::Drive::Model::muff, 0.3f, 5.0f, tone, 250.0);
            return dB (rms (mids, 0.3) / rms (lows, 0.3));
        };
        CHECK (midsAgainstLows (5.0f) < midsAgainstLows (10.0f) - 3.0f);
    }
}

TEST_CASE ("Halo: the germanium fuzz circuit, its working point, FUZZ and clean-up", "[effects][drive]")
{
    SECTION ("At rest it sits where the circuit's analysis says")
    {
        // Q1's collector is one germanium junction (about 0.2 V) above Q2's
        // emitter, and Q2's collector sits mid-supply with room to swing.
        effects::Halo halo;
        halo.prepare (384000.0);
        CHECK_THAT (halo.q1CollectorVolts() - halo.q2EmitterVolts(), WithinAbs (0.2, 0.05));
        CHECK_THAT (halo.q2EmitterVolts(), WithinAbs (0.41, 0.05));
        CHECK (halo.q2CollectorVolts() > 4.0);
        CHECK (halo.q2CollectorVolts() < 6.5);
    }

    const auto play = [] (float amplitude, float fuzz, float tone = 5.0f, double hz = 220.0)
    {
        EffectRig rig;
        rig.set (GIds::driveOn, 1.0f);
        rig.set (GIds::driveModel, (float) effects::Drive::Model::halo);
        rig.set (GIds::driveGain, fuzz);
        rig.set (GIds::driveTone, tone);
        const auto output = rig.runMono (sine (hz, amplitude, 1.0));
        REQUIRE (allFinite (output));
        return output;
    };
    const auto gainDb = [&] (float amplitude, float fuzz) { return dB (rms (play (amplitude, fuzz), 0.3) / (amplitude / std::sqrt (2.0f))); };

    SECTION ("FUZZ takes the emitter resistance out of Q2: over 20 dB more gain for a quiet note")
    {
        CHECK (gainDb (0.002f, 10.0f) - gainDb (0.002f, 0.0f) > 20.0f);
        CHECK (gainDb (0.002f, 5.0f) > gainDb (0.002f, 0.0f) + 3.0f);
    }

    SECTION ("Played hard it's saturated: FUZZ hardly changes the level, and harder playing barely gets louder")
    {
        CHECK (std::abs (gainDb (0.3f, 10.0f) - gainDb (0.3f, 0.0f)) < 2.0f);
        const auto level = [&] (float amplitude) { return dB (rms (play (amplitude, 5.0f), 0.3)); };
        CHECK (level (1.0f) - level (0.1f) < 6.0f);
    }

    SECTION ("It cleans up: a light touch is almost clean, a hard one is fuzz")
    {
        const auto harmonics = [&] (float amplitude)
        {
            const auto output = play (amplitude, 5.0f);
            return dB ((levelAt (output, 440.0, 0.3) + levelAt (output, 660.0, 0.3)) / levelAt (output, 220.0, 0.3));
        };
        CHECK (harmonics (0.01f) < -40.0f);
        CHECK (harmonics (0.3f) > -10.0f);
    }

    SECTION ("The original has no tone control: TONE changes nothing")
    {
        const auto dark = play (0.3f, 5.0f, 0.0f), bright = play (0.3f, 5.0f, 10.0f);
        CHECK (maxAbsDifference (dark, bright, 0) < 1.0e-6f);
    }

    SECTION ("Its 2.2 uF input into the low input impedance thins the lowest bass")
    {
        CHECK (gainDb (0.002f, 5.0f) - dB (rms (play (0.002f, 5.0f, 5.0f, 50.0), 0.3) / (0.002f / std::sqrt (2.0f))) > 2.0f);
    }
}

TEST_CASE ("Rabid: the RAT 2's op-amp gain, symmetric diodes and FILTER", "[effects][drive]")
{
    EffectRig rig;
    rig.set (GIds::driveOn, 1.0f);
    rig.set (GIds::driveModel, (float) effects::Drive::Model::rabid);

    SECTION ("Two diodes clip both halves alike: odd harmonics, little even")
    {
        rig.set (GIds::driveGain, 7.0f);
        rig.set (GIds::driveTone, 10.0f);
        const auto output = rig.runMono (sine (220.0, 0.2f, 0.5));
        REQUIRE (allFinite (output));
        const auto fundamental = levelAt (output, 220.0);
        CHECK (dB (levelAt (output, 660.0) / fundamental) > -20.0f);
        CHECK (dB (levelAt (output, 440.0) / fundamental) < -35.0f);
    }

    SECTION ("Aliasing stays down at full DISTORTION")
    {
        rig.set (GIds::driveGain, 10.0f);
        rig.set (GIds::driveTone, 10.0f);
        const auto output = rig.runMono (sine (5000.0, 0.5f, 0.5));
        CHECK (dB (levelAt (output, 13000.0) / levelAt (output, 5000.0)) < -45.0f);
    }

    SECTION ("TONE down (the FILTER turned up) darkens it")
    {
        const auto trebleShare = [&] (float tone)
        {
            rig.set (GIds::driveGain, 6.0f);
            rig.set (GIds::driveTone, tone);
            const auto output = rig.runMono (sine (220.0, 0.2f, 0.5));
            // The 13th and 15th harmonics: odd, as symmetric clipping makes (the even ones are all but absent).
            return dB ((levelAt (output, 2860.0) + levelAt (output, 3300.0)) / levelAt (output, 220.0));
        };
        CHECK (trebleShare (0.0f) < trebleShare (10.0f) - 10.0f);   // about 15 dB measured
    }

    SECTION ("At noon, near the guitar's own level")
    {
        rig.set (GIds::driveGain, 5.0f);
        rig.set (GIds::driveTone, 5.0f);
        const auto input = sine (440.0, 0.3f, 0.5);
        const auto level = dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2));
        CHECK_THAT (level, WithinAbs (0.0, 2.0));
    }

    SECTION ("At full DISTORTION the op-amp runs out of bandwidth: a quiet 4 kHz gets far less gain than 500 Hz")
    {
        rig.set (GIds::driveGain, 10.0f);
        rig.set (GIds::driveTone, 10.0f);
        const auto gainAt = [&] (double hz)
        {
            const auto input = sine (hz, 0.0003f, 0.5);   // quiet enough not to clip
            return rms (rig.runMono (input), 0.2) / rms (input, 0.2);
        };
        CHECK (dB (gainAt (4000.0)) < dB (gainAt (500.0)) - 6.0f);
    }
}

TEST_CASE ("Abyss: the MT-2's gain hump, diodes and EQ", "[effects][drive]")
{
    struct Knobs { float dist = 5.0f, high = 5.0f, low = 5.0f, middle = 5.0f, midFreq = 5.0f; };
    const auto play = [] (const std::vector<float>& input, Knobs k)
    {
        EffectRig rig;
        rig.set (GIds::driveOn, 1.0f);
        rig.set (GIds::driveModel, (float) effects::Drive::Model::abyss);
        rig.set (GIds::driveGain, k.dist);
        rig.set (GIds::driveHigh, k.high);
        rig.set (GIds::driveLow, k.low);
        rig.set (GIds::driveMiddle, k.middle);
        rig.set (GIds::driveMidFreq, k.midFreq);
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        return output;
    };
    const auto smallGain = [&] (double hz, Knobs k)
    {
        k.dist = 0.0f;
        const auto input = sine (hz, 0.0002f, 0.6);
        return dB (rms (play (input, k), 0.3) / rms (input, 0.3));
    };
    const auto note = sine (220.0, 0.2f, 0.6);
    const auto levelDb = [&] (float dist) { Knobs k; k.dist = dist; return dB (rms (play (note, k), 0.3) / rms (note, 0.3)); };
    Knobs hi1, hi0, lo1, lo0, mid1, mid0;
    hi1.high = 10.0f; hi0.high = 0.0f; lo1.low = 10.0f; lo0.low = 0.0f; mid1.middle = 10.0f; mid0.middle = 0.0f;
    Knobs m1lo = mid1, m1hi = mid1;
    m1lo.midFreq = 0.0f;
    m1hi.midFreq = 10.0f;

    SECTION ("Heavily saturated even at DIST 0, as the pedal is; LEVEL 0 dB matches the guitar at noon")
    {
        CHECK_THAT (levelDb (5.0f), WithinAbs (0.0, 1.0));
        CHECK (std::abs (levelDb (10.0f) - levelDb (0.0f)) < 4.0f);
        const auto output = play (note, {});
        CHECK (dB (levelAt (output, 660.0, 0.3) / levelAt (output, 220.0, 0.3)) > -20.0f);   // strong odd harmonics
        CHECK (dB (levelAt (output, 440.0, 0.3) / levelAt (output, 220.0, 0.3)) < -40.0f);   // and little even: symmetric diodes and rails
    }

    SECTION ("The gain hump: with a flat EQ, a quiet 800 Hz gets far more gain than 50 Hz or 12 kHz")
    {
        const auto mids = smallGain (800.0, {});
        CHECK (mids > smallGain (50.0, {}) + 25.0f);
        CHECK (mids > smallGain (12000.0, {}) + 40.0f);
    }

    SECTION ("The EQ's ranges from its parts: LOW about 38 dB at 100 Hz and MIDDLE about 23 dB at 400 Hz, end to end")
    {
        CHECK_THAT (smallGain (100.0, lo1) - smallGain (100.0, lo0), WithinAbs (38.0, 2.0));
        CHECK_THAT (smallGain (400.0, mid1) - smallGain (400.0, mid0), WithinAbs (23.0, 2.0));
        CHECK (smallGain (6400.0, hi1) - smallGain (6400.0, hi0) > 20.0f);   // about 34 dB here
    }

    SECTION ("MID FREQ moves the MIDDLE boost up from the low mids")
    {
        const auto lowMidsOverUpper = [&] (Knobs k) { return smallGain (300.0, k) - smallGain (2000.0, k); };
        CHECK (lowMidsOverUpper (m1lo) > lowMidsOverUpper (m1hi) + 10.0f);
    }
}

TEST_CASE ("Chimera: the Klon's clean blend, germanium clipping, mid hump and TREBLE", "[effects][drive]")
{
    const auto play = [] (const std::vector<float>& input, float gain, float treble = 5.0f)
    {
        EffectRig rig;
        rig.set (GIds::driveOn, 1.0f);
        rig.set (GIds::driveModel, (float) effects::Drive::Model::chimera);
        rig.set (GIds::driveGain, gain);
        rig.set (GIds::driveTone, treble);
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        return output;
    };
    const auto note = sine (220.0, 0.2f, 0.6);
    const auto levelDb = [&] (float gain) { return dB (rms (play (note, gain), 0.3) / rms (note, 0.3)); };
    const auto thirdDb = [&] (float gain)
    {
        const auto output = play (note, gain);
        return dB (levelAt (output, 660.0, 0.3) / levelAt (output, 220.0, 0.3));
    };
    const auto secondDb = [&] (float gain)
    {
        const auto output = play (note, gain);
        return dB (levelAt (output, 440.0, 0.3) / levelAt (output, 220.0, 0.3));
    };
    const auto smallGain = [&] (double hz, float gain, float treble)
    {
        const auto input = sine (hz, 0.001f, 0.6);
        return dB (rms (play (input, gain, treble), 0.3) / rms (input, 0.3));
    };

    SECTION ("At GAIN noon, near the guitar's level; louder at both ends, as GAIN's second gang trades clean level for drive")
    {
        CHECK_THAT (levelDb (5.0f), WithinAbs (0.0, 1.0));
        CHECK (levelDb (0.0f) > levelDb (5.0f) + 4.0f);   // the Klon's famous clean boost at low GAIN
        CHECK (levelDb (10.0f) > levelDb (5.0f));
    }

    SECTION ("Germanium diodes clip both halves alike: odd harmonics that grow with GAIN, no even ones")
    {
        CHECK (thirdDb (0.0f) < -80.0f);
        CHECK (thirdDb (5.0f) > thirdDb (0.0f) + 40.0f);
        CHECK (thirdDb (10.0f) > thirdDb (5.0f) + 15.0f);
        CHECK (secondDb (10.0f) < -60.0f);
    }

    SECTION ("The mid hump: at full GAIN a quiet 1 kHz gets far more gain than 100 Hz or 10 kHz")
    {
        const auto mids = smallGain (1000.0, 10.0f, 5.0f);
        CHECK (mids > smallGain (100.0, 10.0f, 5.0f) + 12.0f);
        CHECK (mids > smallGain (10000.0, 10.0f, 5.0f) + 20.0f);
    }

    SECTION ("TREBLE spans about 24 dB at 8 kHz, as its parts give ((10k + 4.7k) / 1.8k against 4.7k / 11.8k)")
    {
        CHECK_THAT (smallGain (8000.0, 2.0f, 10.0f) - smallGain (8000.0, 2.0f, 0.0f), WithinAbs (24.0, 2.0));
    }
}

TEST_CASE ("Skyline: a Tube Screamer whose ATTACK sets the bass into the clipping, with a gate", "[effects][drive]")
{
    const auto play = [] (float model, const std::vector<float>& input, float attack, float gate = 0.0f, float drive = 6.0f)
    {
        EffectRig rig;
        rig.set (GIds::driveOn, 1.0f);
        rig.set (GIds::driveModel, model);
        rig.set (GIds::driveGain, drive);
        rig.set (GIds::driveAttack, attack);
        rig.set (GIds::driveGate, gate);
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        return output;
    };
    const auto skyline = (float) effects::Drive::Model::skyline, banshee = (float) effects::Drive::Model::screamer;

    SECTION ("ATTACK's eight capacitors against the 4.7k cut the bass from about 70 Hz to about 1 kHz")
    {
        CHECK_THAT (effects::Drive::skylineAttackHz (0), WithinAbs (72.0, 1.0));     // 470 nF
        CHECK_THAT (effects::Drive::skylineAttackHz (6), WithinAbs (720.0, 5.0));    // 47 nF, the TS808
        CHECK_THAT (effects::Drive::skylineAttackHz (7), WithinAbs (1026.0, 5.0));   // 33 nF
    }

    SECTION ("On ATTACK 7 with the gate off it is exactly the Tube Screamer (Banshee)")
    {
        const auto input = sine (196.0, 0.3f, 0.5);
        CHECK (maxAbsDifference (play (skyline, input, (float) effects::Drive::skylineTubeScreamerAttack),
                                 play (banshee, input, 0.0f)) < 1.0e-6f);
    }

    SECTION ("ATTACK 1 lets far more of a low E into the clipping than ATTACK 8")
    {
        const auto lowE = sine (82.4, 0.3f, 0.6);
        const auto harmonics = [&] (float attack)
        {
            const auto output = play (skyline, lowE, attack);
            return dB ((levelAt (output, 3.0 * 82.4, 0.3) + levelAt (output, 5.0 * 82.4, 0.3)) / levelAt (output, 82.4, 0.3));
        };
        CHECK (harmonics (0.0f) > harmonics (7.0f) + 6.0f);
    }

    SECTION ("GATE silences quiet hiss but lets playing through")
    {
        juce::Random random (3);
        std::vector<float> hiss ((size_t) (0.6 * sampleRate));
        for (auto& sample : hiss)
            sample = 0.0003f * (random.nextFloat() * 2.0f - 1.0f);   // about -75 dB
        const auto open = play (skyline, hiss, 6.0f, 0.0f), gated = play (skyline, hiss, 6.0f, 8.0f);
        CHECK (dB (rms (gated, 0.3) / rms (open, 0.3)) < -20.0f);

        const auto note = sine (196.0, 0.2f, 0.6);
        CHECK_THAT (dB (rms (play (skyline, note, 6.0f, 8.0f), 0.3) / rms (play (skyline, note, 6.0f, 0.0f), 0.3)), WithinAbs (0.0, 0.5));
    }
}

TEST_CASE ("The Screamer drive behaves like a Tube Screamer", "[effects][drive]")
{
    EffectRig rig;
    rig.set (GIds::driveOn, 1.0f);
    rig.set (GIds::driveModel, (float) effects::Drive::Model::screamer);

    SECTION ("It's the default for new rigs")
    {
        EffectRig fresh;
        CHECK (juce::roundToInt (fresh.processor.getParameters().getRawParameterValue (GIds::driveModel)->load())
               == (int) effects::Drive::Model::screamer);
    }

    SECTION ("Mid hump: the bass passes clean while the mids are boosted")
    {
        // A quiet signal, so nothing clips and this is just the frequency response.
        rig.set (GIds::driveGain, 0.0f);
        rig.set (GIds::driveTone, 10.0f);
        const auto gainAt = [&] (double hz)
        {
            const auto input = sine (hz, 0.003f, 0.5);
            return dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2));
        };
        CHECK (gainAt (1000.0) - gainAt (100.0) > 9.0f);

        // TONE: dark at 0 (the top well below the mids), bright at 10.
        const auto brightTreble = gainAt (6000.0) - gainAt (1000.0);
        rig.set (GIds::driveTone, 0.0f);
        const auto darkTreble = gainAt (6000.0) - gainAt (1000.0);
        CHECK (darkTreble < -10.0f);
        CHECK (brightTreble - darkTreble > 12.0f);
    }

    SECTION ("Its diodes clip both halves alike: odd harmonics, hardly any even")
    {
        rig.set (GIds::driveGain, 7.0f);
        rig.set (GIds::driveTone, 10.0f);
        const auto output = rig.runMono (sine (500.0, 0.3f, 0.5));
        REQUIRE (allFinite (output));
        const auto fundamental = levelAt (output, 500.0);
        CHECK (dB (levelAt (output, 1500.0) / fundamental) > -25.0f);
        CHECK (dB (levelAt (output, 1000.0) / fundamental) < -50.0f);
    }

    SECTION ("Its level at noon is close to the guitar's own")
    {
        rig.set (GIds::driveGain, 5.0f);
        rig.set (GIds::driveTone, 5.0f);
        const auto input = sine (440.0, 0.3f, 0.5);
        CHECK_THAT (dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2)), WithinAbs (0.0, 3.0));
    }

    SECTION ("Aliasing stays down at full drive")
    {
        rig.set (GIds::driveGain, 10.0f);
        rig.set (GIds::driveTone, 10.0f);
        const auto output = rig.runMono (sine (5000.0, 0.5f, 0.5));
        CHECK (dB (levelAt (output, 13000.0) / levelAt (output, 5000.0)) < -45.0f);
    }
}
