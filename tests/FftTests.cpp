// The FFT editor (stage 3d): the curve, SWEEP and FLIP, LP / HP / SLOPE, VSPEED / VSTRENGTH, measured.
#include "SynthTestUtils.h"

#include "PluginProcessor.h"
#include "dsp/synth/Spectrum.h"

namespace
{
    // A saw's harmonic h, as a band-limited saw has it.
    double sawHarmonic (int h) { return 2.0 / (juce::MathConstants<double>::pi * h); }

    // The saw edited by `gain`, as the processor builds it.
    std::shared_ptr<const synth::Wavetable> editedSaw (const std::function<float (int)>& gain)
    {
        return synth::Wavetable::build (synth::spectrum::basicWave (synth::OscWave::saw, 0.5f), gain, false);
    }

    std::vector<float> playSaw (int note, double seconds, const std::function<void (synth::OscSettings&)>& setup)
    {
        Player p;
        p.osc().wave = synth::OscWave::saw;
        setup (p.osc());
        p.on (note);
        return p.render (seconds)[0];
    }
}

TEST_CASE ("FFT curve: each band's gain, SWEEP moving it and FLIP turning it over", "[fft]")
{
    using namespace synth;
    SpectrumEdit edit;
    edit.gainsDb[3] = -12.0f;   // harmonic 4
    edit.gainsDb[4] = SpectrumEdit::minDb;   // harmonic 5: gone
    CHECK_THAT (spectrum::gainDbFor (edit, 4, 0.0f, false), WithinAbs (-12.0, 1.0e-4));
    CHECK_THAT (spectrum::gainFor (edit, 5, 0.0f, false), WithinAbs (0.0, 1.0e-12));
    CHECK_THAT (spectrum::gainDbFor (edit, 1, 0.0f, false), WithinAbs (0.0, 1.0e-4));
    // SWEEP up an octave (a quarter of the way): harmonic 8 gets what was drawn at 4.
    CHECK_THAT (spectrum::gainDbFor (edit, 8, 0.25f, false), WithinAbs (-12.0, 1.0e-4));
    CHECK_THAT (spectrum::gainDbFor (edit, 4, 0.25f, false), WithinAbs (0.0, 1.0e-4));
    // ...and down: harmonic 2 gets what was at 4.
    CHECK_THAT (spectrum::gainDbFor (edit, 2, -0.25f, false), WithinAbs (-12.0, 1.0e-4));
    // FLIP: -12 becomes +12; a silenced band becomes the most boosted.
    CHECK_THAT (spectrum::gainDbFor (edit, 4, 0.0f, true), WithinAbs (12.0, 1.0e-4));
    CHECK_THAT (spectrum::gainDbFor (edit, 5, 0.0f, true), WithinAbs (SpectrumEdit::maxDb, 1.0e-4));
    // Above 256, the last band's gain.
    edit.gainsDb[255] = -6.0f;
    CHECK_THAT (spectrum::gainDbFor (edit, 400, 0.0f, false), WithinAbs (-6.0, 1.0e-4));

    // Saved as text and back.
    const auto back = SpectrumEdit::decode (edit.encode());
    CHECK (back == edit);
    CHECK (SpectrumEdit::decode ("").isFlat());
    CHECK_FALSE (edit.isFlat());
    juce::Random random (1);
    SpectrumEdit randomEdit;
    randomEdit.randomise (random);
    CHECK_FALSE (randomEdit.isFlat());
    for (const auto g : randomEdit.gainsDb)
        CHECK ((g >= SpectrumEdit::minDb && g <= SpectrumEdit::maxDb));
}

TEST_CASE ("FFT: an edited saw plays its curve: cut harmonics gone, the rest as they were", "[fft]")
{
    synth::SpectrumEdit edit;
    edit.gainsDb[1] = synth::SpectrumEdit::minDb;   // the 2nd harmonic: gone
    edit.gainsDb[2] = -12.0f;                       // the 3rd: 12 dB down
    const auto table = editedSaw ([&] (int h) { return synth::spectrum::gainFor (edit, h, 0.0f, false); });
    const auto out = playSaw (45, 1.0, [&] (synth::OscSettings& o) { o.spectral = table.get(); });
    CHECK_THAT (levelAt (out, 110.0, 12000, 24000), WithinAbs (sawHarmonic (1), 0.005));
    CHECK (levelAt (out, 220.0, 12000, 24000) < 0.001);
    CHECK_THAT (levelAt (out, 330.0, 12000, 24000), WithinAbs (sawHarmonic (3) * 0.2512, 0.003));
    CHECK_THAT (levelAt (out, 440.0, 12000, 24000), WithinAbs (sawHarmonic (4), 0.003));

    // A flat curve plays the plain saw: every harmonic the same (to 0.05 dB).
    // (Not sample for sample: built tables keep 2048 samples a level where
    // the saw's own keep 4096, so reading between samples differs a hair.)
    const auto flat = editedSaw ([] (int) { return 1.0f; });
    const auto edited = playSaw (45, 0.5, [&] (synth::OscSettings& o) { o.spectral = flat.get(); });
    const auto plain = playSaw (45, 0.5, [] (synth::OscSettings&) {});
    for (int h = 1; h <= 40; ++h)
    {
        INFO ("harmonic " << h);
        CHECK_THAT (db (levelAt (edited, 110.0 * h, 4800, 9600)), WithinAbs (db (levelAt (plain, 110.0 * h, 4800, 9600)), 0.05));
    }
}

TEST_CASE ("FFT LP and HP: harmonics above (or below) the cutoff taken away, live", "[fft]")
{
    // LP at 8: harmonics 1 to 8 as they were, the 9th and up gone.
    const auto lp = playSaw (45, 1.0, [] (synth::OscSettings& o) { o.fftLp = 8.0f; });
    CHECK_THAT (levelAt (lp, 110.0 * 8, 12000, 24000), WithinAbs (sawHarmonic (8), 0.003));
    CHECK (levelAt (lp, 110.0 * 9, 12000, 24000) < 0.0005);
    CHECK (levelAt (lp, 110.0 * 12, 12000, 24000) < 0.0005);

    // HP at 4: harmonics 1 to 4 gone, the 5th and up as they were.
    const auto hp = playSaw (45, 1.0, [] (synth::OscSettings& o) { o.fftHp = 4.0f; });
    CHECK (levelAt (hp, 110.0, 12000, 24000) < 0.0005);
    CHECK (levelAt (hp, 440.0, 12000, 24000) < 0.0005);
    CHECK_THAT (levelAt (hp, 550.0, 12000, 24000), WithinAbs (sawHarmonic (5), 0.003));

    // Between levels (a third of an octave apart: here 8 and 10 harmonics), the
    // harmonics in between are blended out smoothly: LP at 9 keeps 1 to 8 whole
    // and the 9th and 10th about half.
    const auto between = playSaw (45, 1.0, [] (synth::OscSettings& o) { o.fftLp = 9.0f; });
    const auto ninth = levelAt (between, 990.0, 12000, 24000) / sawHarmonic (9);
    CHECK (ninth > 0.1);
    CHECK (ninth < 0.9);
    CHECK_THAT (levelAt (between, 880.0, 12000, 24000), WithinAbs (sawHarmonic (8), 0.003));

    // SLOPE widens it: at 100%, harmonics well under the cutoff are already partly cut.
    const auto gentle = playSaw (45, 1.0, [] (synth::OscSettings& o) { o.fftLp = 16.0f; o.fftSlope = 1.0f; });
    const auto steep = playSaw (45, 1.0, [] (synth::OscSettings& o) { o.fftLp = 16.0f; });
    CHECK_THAT (levelAt (steep, 110.0 * 10, 12000, 24000), WithinAbs (sawHarmonic (10), 0.003));
    CHECK (levelAt (gentle, 110.0 * 10, 12000, 24000) < 0.8 * sawHarmonic (10));
    CHECK (levelAt (gentle, 110.0 * 24, 12000, 24000) > 0.1 * sawHarmonic (24));   // and some above it stay

    // LP and HP stay as clean as the saw: nothing folds back between harmonics.
    Player p;
    p.osc().fftLp = 200.0f;
    p.osc().fftHp = 3.0f;
    p.on (100);
    const Spectrum spectrum (p.render (1.6)[0], 4096);
    CHECK (spectrum.loudestBetweenHarmonics (noteHz (100), 20000.0) < -90.0);
}

TEST_CASE ("FFT VSPEED and VSTRENGTH: the harmonics swell and fade at VSPEED", "[fft]")
{
    const auto strength = 1.0f;
    const auto gain = [strength] (int h, float sign) { return 1.0f + sign * strength * synth::spectrum::pulsePattern (h); };
    const auto one = editedSaw ([&] (int h) { return gain (h, 1.0f); });
    const auto other = editedSaw ([&] (int h) { return gain (h, -1.0f); });
    const auto out = playSaw (45, 2.0, [&] (synth::OscSettings& o)
    {
        o.spectral = one.get();
        o.spectralPulse = other.get();
        o.fftVSpeedHz = 2.0f;
    });
    // The fundamental over the first second, in 20 ms windows: it swings between
    // 1 - 0.675 and 1 + 0.675 of the saw's (sin 2.4 = 0.675), twice a second.
    double lowest = 1.0e9, highest = 0.0;
    for (size_t at = 2400; at + 960 < 48000; at += 480)
    {
        const auto level = levelAt (out, 110.0, at, 960);
        lowest = std::min (lowest, level);
        highest = std::max (highest, level);
    }
    CHECK (highest / lowest > 3.0);
    CHECK (highest < sawHarmonic (1) * 1.75);
}

TEST_CASE ("FFT in the plugin: drawn, built in the background, kept with presets, projects and undo", "[fft][processor]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    CHECK (processor.getSynthSettings().oscillators[0].spectral == nullptr);   // nothing drawn: nothing built

    synth::SpectrumEdit edit;
    edit.gainsDb[1] = synth::SpectrumEdit::minDb;
    processor.setSpectrum (0, edit);
    processor.spectrumEditFinished();   // as the editor does when a stroke ends: an undo step
    processor.finishTableBuilds();
    REQUIRE (processor.getSynthSettings().oscillators[0].spectral != nullptr);
    CHECK (processor.isRigModified());

    // It sounds: A2's 2nd harmonic is gone.
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);
        std::vector<float> out;
        for (int block = 0; block < 120; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            midi.clear();
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }
        // (4800 samples: whole cycles of every harmonic, so the loud fundamental doesn't leak into the others' readings.)
        CHECK (levelAt (out, 220.0, 12000, 4800) < 0.001 * levelAt (out, 110.0, 12000, 4800));
        CHECK (levelAt (out, 330.0, 12000, 4800) > 0.2 * levelAt (out, 110.0, 12000, 4800));
    }

    // Changing the wave rebuilds it from the new one.
    const auto* sawCopy = processor.getSynthSettings().oscillators[0].spectral;
    auto* wave = processor.getParameters().getParameter (ParamIDs::osc (0, "Wave"));
    wave->setValueNotifyingHost (wave->convertTo0to1 ((float) synth::OscWave::triangle));
    processor.finishTableBuilds();
    REQUIRE (processor.getSynthSettings().oscillators[0].spectral != nullptr);
    CHECK (processor.getSynthSettings().oscillators[0].spectral != sawCopy);
    CHECK (processor.getEditedTable (0)->getName() == "Triangle");

    // Presets and projects keep the curve.
    const auto rig = processor.captureRig();
    REQUIRE (rig.spectra.count (0) == 1);
    const auto reloaded = Rig::fromXml (*rig.toXml());
    REQUIRE (reloaded.has_value());
    CHECK (reloaded->spectra.at (0) == edit);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        reopened.finishTableBuilds();
        CHECK (reopened.getSpectrum (0) == edit);
        CHECK (reopened.getSynthSettings().oscillators[0].spectral != nullptr);
        CHECK (reopened.getSpectrum (1).isFlat());
    }

    // RESET (a flat curve) goes back to the plain wave; undo brings the curve back.
    processor.flushUndoStep();
    processor.setSpectrum (0, {});
    processor.spectrumEditFinished();
    processor.finishTableBuilds();
    CHECK (processor.getSynthSettings().oscillators[0].spectral == nullptr);
    processor.flushUndoStep();
    processor.undo();
    processor.finishTableBuilds();
    CHECK (processor.getSpectrum (0) == edit);
    CHECK (processor.getSynthSettings().oscillators[0].spectral != nullptr);

    // VSTRENGTH builds the second copy; a wavetable's edited copy keeps all its frames.
    auto* strength = processor.getParameters().getParameter (ParamIDs::osc (0, "FftVStrength"));
    strength->setValueNotifyingHost (strength->convertTo0to1 (0.5f));
    processor.chooseFactoryWavetable (0, synth::factoryTables::names.indexOf ("Vowels"));
    processor.finishTableBuilds();
    const auto settings = processor.getSynthSettings().oscillators[0];
    REQUIRE (settings.spectral != nullptr);
    REQUIRE (settings.spectralPulse != nullptr);
    CHECK (settings.spectral->getNumFrames() == 64);
    CHECK (settings.spectralPulse->getNumFrames() == 64);
}

TEST_CASE ("CPU: an FFT-edited supersaw chord, with LP and HP, and pulsing", "[fft][performance]")
{
    synth::SpectrumEdit edit;
    for (int b = 0; b < 32; ++b)
        edit.gainsDb[(size_t) b] = (b % 3 == 0) ? -24.0f : 3.0f;
    const auto table = editedSaw ([&] (int h) { return synth::spectrum::gainFor (edit, h, 0.0f, false); });
    const auto other = editedSaw ([&] (int h) { return synth::spectrum::gainFor (edit, h, 0.0f, false) * 0.5f; });
    const auto percent = [&] (const char* what, const std::function<void (synth::OscSettings&)>& setup)
    {
        Player p;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
            o.unisonDetune = 0.4f;
            setup (o);
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        p.render (5.0);
        const auto cost = (juce::Time::getMillisecondCounterHiRes() - started) / 5000.0 * 100.0;
        WARN (what << ": 2 saws x 7 copies x 8 keys, " << cost << "% of one core");
        return cost;
    };
    const auto plain = percent ("plain", [] (synth::OscSettings&) {});
    const auto edited = percent ("FFT-edited", [&] (synth::OscSettings& o) { o.spectral = table.get(); });
    percent ("with LP", [&] (synth::OscSettings& o) { o.spectral = table.get(); o.fftLp = 30.0f; });
    const auto all = percent ("with LP, HP and pulsing", [&] (synth::OscSettings& o)
    {
        o.spectral = table.get();
        o.spectralPulse = other.get();
        o.fftLp = 30.0f;
        o.fftHp = 2.0f;
    });
    CHECK (edited < 4.0 * plain + 2.0);
    CHECK (all < 60.0);
}

