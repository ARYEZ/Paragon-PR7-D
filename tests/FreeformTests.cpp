// FREEFORM (stage 3d, part 2): the drawing, its smoothing and edges, playing it,
// and the resampler's ways of cutting a long file into frames, measured.
#include "SynthTestUtils.h"

#include "PluginProcessor.h"
#include "dsp/synth/Freeform.h"

using Catch::Matchers::WithinRel;

namespace
{
    double harmonicOf (const std::vector<float>& cycle, int h)
    {
        double re = 0.0, im = 0.0;
        const auto size = (int) cycle.size();
        for (int i = 0; i < size; ++i)
        {
            const auto t = juce::MathConstants<double>::twoPi * h * i / size;
            re += cycle[(size_t) i] * std::cos (t);
            im += cycle[(size_t) i] * std::sin (t);
        }
        return 2.0 * std::sqrt (re * re + im * im) / size;
    }

    double peakBetween (const std::vector<float>& x, int from, int to)
    {
        auto peak = 0.0;
        for (int i = from; i < to; ++i)
            peak = std::max (peak, (double) std::abs (x[(size_t) i]));
        return peak;
    }

    void setWave (ParagonX1AudioProcessor& processor, int osc, synth::OscWave wave)
    {
        auto* parameter = processor.getParameters().getParameter (ParamIDs::osc (osc, "Wave"));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) wave));
    }

    std::vector<float> playNote (ParagonX1AudioProcessor& processor, int note, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        std::vector<float> out;
        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            midi.clear();
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }
        midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        buffer.clear();
        processor.processBlock (buffer, midi);
        return out;
    }
}

TEST_CASE ("FREEFORM drawings: flat is silent, a drawn sine is one harmonic, a drawn saw a saw's", "[freeform]")
{
    using synth::FreeformDrawing;
    const auto flat = FreeformDrawing().render();
    CHECK (peakBetween (flat, 0, (int) flat.size()) == 0.0);
    CHECK (FreeformDrawing().isFlat());

    const auto sine = FreeformDrawing::startingShape (1).render();
    REQUIRE ((int) sine.size() == FreeformDrawing::cycle);
    CHECK_THAT (harmonicOf (sine, 1), WithinAbs (1.0, 0.001));
    for (int h = 2; h <= 20; ++h)
        CHECK (harmonicOf (sine, h) < 1.0e-3);

    // 512 straight-line points: a saw's harmonics, to within 2% for the first ten.
    const auto saw = FreeformDrawing::startingShape (2).render();
    for (int h = 1; h <= 10; ++h)
    {
        INFO ("harmonic " << h);
        CHECK_THAT (harmonicOf (saw, h), WithinRel (2.0 / (juce::MathConstants<double>::pi * h), 0.02));
    }
    CHECK (FreeformDrawing::startingShapes.size() == 5);
}

TEST_CASE ("FREEFORM SMOOTH: rounds the corners, taking away high harmonics; SPLIT gives the parts their own", "[freeform]")
{
    auto square = synth::FreeformDrawing::startingShape (3);
    const auto sharp = square.render();
    square.smoothA = 0.5f;
    const auto smooth = square.render();
    CHECK (harmonicOf (smooth, 1) > 0.8 * harmonicOf (sharp, 1));      // the note stays
    CHECK (harmonicOf (smooth, 15) < 0.1 * harmonicOf (sharp, 15));    // the buzz goes
    CHECK (harmonicOf (smooth, 31) < 0.05 * harmonicOf (sharp, 31));

    // A zigzag (every point the other way): SMOOTH B after SPLIT irons out
    // its half and leaves A's alone.
    synth::FreeformDrawing zigzag;
    for (int i = 0; i < synth::FreeformDrawing::points; ++i)
        zigzag.values[(size_t) i] = i % 2 == 0 ? 0.5f : -0.5f;
    zigzag.smoothB = 1.0f;
    zigzag.split = 0.5f;
    const auto split = zigzag.render();
    const auto cycle = synth::FreeformDrawing::cycle;
    CHECK (peakBetween (split, 64, cycle / 2 - 64) > 0.45);             // A: as drawn
    CHECK (peakBetween (split, cycle / 2 + 64, cycle - 64) < 0.02);     // B: smoothed flat
}

TEST_CASE ("FREEFORM EDGE: ADAPT and ZERO join the cycle's end to its start; SYMMETRY mirrors it", "[freeform]")
{
    using synth::FreeformEdge;
    auto saw = synth::FreeformDrawing::startingShape (2);
    const auto jump = [] (const std::vector<float>& w) { return std::abs (w.back() - w.front()); };
    CHECK (jump (saw.render()) > 1.9);   // a saw jumps at the end: a hard edge

    saw.edge = 1.0f;
    saw.edgeMode = FreeformEdge::adapt;
    const auto adapted = saw.render();
    CHECK (jump (adapted) < 0.01);
    // Only the ends move: the middle is as drawn.
    const auto plain = synth::FreeformDrawing::startingShape (2).render();
    CHECK_THAT (adapted[1024], WithinAbs (plain[1024], 1.0e-6));

    saw.edgeMode = FreeformEdge::zero;
    const auto zeroed = saw.render();
    CHECK (std::abs (zeroed.front()) < 1.0e-6);
    CHECK (std::abs (zeroed.back()) < 0.01);

    saw.edgeMode = FreeformEdge::symmetry;
    const auto mirrored = saw.render();
    for (int i = 0; i < synth::FreeformDrawing::cycle; i += 97)
        CHECK_THAT (mirrored[(size_t) i], WithinAbs (mirrored[(size_t) (synth::FreeformDrawing::cycle - 1 - i)], 1.0e-6));
}

TEST_CASE ("FREEFORM drawings are saved as text and come back the same", "[freeform]")
{
    auto drawing = synth::FreeformDrawing::startingShape (4);
    drawing.smoothA = 0.25f;
    drawing.smoothB = 0.75f;
    drawing.split = 0.4f;
    drawing.normalise = false;
    drawing.edge = 0.5f;
    drawing.edgeMode = synth::FreeformEdge::zero;
    const auto back = synth::FreeformDrawing::decode (drawing.encode());
    CHECK (back.smoothA == 0.25f);
    CHECK (back.smoothB == 0.75f);
    CHECK (back.split == 0.4f);
    CHECK_FALSE (back.normalise);
    CHECK (back.edge == 0.5f);
    CHECK (back.edgeMode == synth::FreeformEdge::zero);
    for (int i = 0; i < synth::FreeformDrawing::points; ++i)
        CHECK_THAT (back.values[(size_t) i], WithinAbs (drawing.values[(size_t) i], 0.0006));
    // Damaged text: a flat drawing, not a crash.
    CHECK (synth::FreeformDrawing::decode ("1 2 3").isFlat());
}

TEST_CASE ("FREEFORM plays the drawing: a drawn sine is a pure note, band-limited high up", "[freeform]")
{
    const auto table = synth::Wavetable::build (synth::FreeformDrawing::startingShape (1).source());
    Player p;
    p.osc().wave = synth::OscWave::freeform;
    p.osc().table = table.get();
    p.on (45);
    const auto out = p.render (0.5)[0];
    CHECK (levelAt (out, 110.0, 12000, 4800) > 0.5);
    CHECK (levelAt (out, 220.0, 12000, 4800) < 0.001 * levelAt (out, 110.0, 12000, 4800));

    // A drawn square at C7: no folded-back harmonics (the table's levels, as the saw's).
    const auto squareTable = synth::Wavetable::build (synth::FreeformDrawing::startingShape (3).source());
    Player high;
    high.osc().wave = synth::OscWave::freeform;
    high.osc().table = squareTable.get();
    high.on (96);
    const auto bright = high.render (2.0)[0];
    const Spectrum spectrum (bright, 12000);
    CHECK (spectrum.loudestBetweenHarmonics (noteHz (96), 20000.0, 30.0) < -80.0);
}

TEST_CASE ("FREEFORM in the plugin: drawn, played, kept with presets, projects and undo, and FFT-edited", "[freeform][processor]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    CHECK (processor.getDrawing (0).isFlat());

    processor.setDrawing (0, synth::FreeformDrawing::startingShape (1));
    processor.drawingEditFinished();
    setWave (processor, 0, synth::OscWave::freeform);
    processor.finishTableBuilds();
    REQUIRE (processor.getSynthSettings().oscillators[0].table != nullptr);
    CHECK (processor.isRigModified());
    {
        const auto out = playNote (processor, 45, 120);
        CHECK (levelAt (out, 110.0, 12000, 4800) > 0.05);
        CHECK (levelAt (out, 220.0, 12000, 4800) < 0.002 * levelAt (out, 110.0, 12000, 4800));
    }

    // Presets and projects keep the drawing.
    const auto rig = processor.captureRig();
    REQUIRE (rig.drawings.count (0) == 1);
    const auto reloaded = Rig::fromXml (*rig.toXml());
    REQUIRE (reloaded.has_value());
    REQUIRE (reloaded->drawings.count (0) == 1);
    CHECK_THAT (reloaded->drawings.at (0).values[128], WithinAbs (1.0, 0.001));
    CHECK (rig.drawings.count (1) == 0);   // flat ones aren't saved
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        reopened.finishTableBuilds();
        CHECK_THAT (reopened.getDrawing (0).values[128], WithinAbs (1.0, 0.001));
        CHECK (reopened.getSynthSettings().oscillators[0].table != nullptr);
        CHECK (reopened.getDrawing (1).isFlat());
    }

    // Undo takes a stroke back.
    processor.flushUndoStep();
    processor.setDrawing (0, synth::FreeformDrawing::startingShape (2));
    processor.drawingEditFinished();
    processor.flushUndoStep();
    CHECK (processor.getDrawing (0) == synth::FreeformDrawing::startingShape (2));
    processor.undo();
    CHECK (processor.getDrawing (0) == synth::FreeformDrawing::startingShape (1));

    // The FFT editor works on the drawing too: a drawn saw with its 2nd harmonic cut.
    processor.setDrawing (0, synth::FreeformDrawing::startingShape (2));
    synth::SpectrumEdit edit;
    edit.gainsDb[1] = synth::SpectrumEdit::minDb;
    processor.setSpectrum (0, edit);
    processor.finishTableBuilds();
    REQUIRE (processor.getSynthSettings().oscillators[0].spectral != nullptr);
    CHECK (processor.getEditedTable (0)->getName() == "Freeform");
    const auto out = playNote (processor, 45, 120);
    CHECK (levelAt (out, 220.0, 12000, 4800) < 0.002 * levelAt (out, 110.0, 12000, 4800));
    CHECK (levelAt (out, 330.0, 12000, 4800) > 0.2 * levelAt (out, 110.0, 12000, 4800));
}

TEST_CASE ("Resampler: a long file cut by root note, every nth frame, spread, or a part of it", "[freeform][files]")
{
    namespace files = synth::wavetableFiles;
    // A second of a sample's position (0 to 1), so a frame's first sample says where it was cut.
    std::vector<float> audio (48000);
    for (size_t i = 0; i < audio.size(); ++i)
        audio[i] = (float) i / (float) audio.size();

    CHECK (files::cycleLength (57, 48000.0) == 218);   // A3, 220 Hz
    CHECK (files::cycleLength (69, 44100.0) == 100);   // A4 at 44.1 kHz

    // By default: 2048s, one after another (23 fit).
    const auto plain = files::cut ("Plain", audio, 48000.0, 0, {});
    CHECK (plain.frameSize == 2048);
    CHECK (plain.numFrames() == 23);

    // The root note A3: frames one cycle of 220 Hz long (218 samples); 220 fit.
    files::CutOptions root;
    root.rootNote = 57;
    const auto byNote = files::cut ("Note", audio, 48000.0, 0, root);
    CHECK (byNote.frameSize == 218);
    CHECK (byNote.numFrames() == 220);
    CHECK_THAT ((*byNote.samples)[218], WithinAbs (218.0 / 48000.0, 1.0e-6));   // frame 2 starts where frame 1 ended

    // Every 4th frame of 1024: 47 frames in all, so 12 kept, 4096 samples apart.
    files::CutOptions everyFourth;
    everyFourth.frameSize = 1024;
    everyFourth.step = 4;
    const auto fourth = files::cut ("Fourth", audio, 48000.0, 0, everyFourth);
    CHECK (fourth.numFrames() == 12);
    CHECK_THAT ((*fourth.samples)[1024], WithinAbs (4096.0 / 48000.0, 1.0e-6));

    // 16 spread through it: the first at the start, the last ending at the end.
    files::CutOptions spread;
    spread.frameSize = 2048;
    spread.spread = 16;
    const auto spreadOut = files::cut ("Spread", audio, 48000.0, 0, spread);
    REQUIRE (spreadOut.numFrames() == 16);
    CHECK ((*spreadOut.samples)[0] == 0.0f);
    CHECK_THAT (spreadOut.samples->back(), WithinAbs (47999.0 / 48000.0, 1.0e-6));

    // The second half only.
    files::CutOptions half;
    half.start = 0.5;
    const auto secondHalf = files::cut ("Half", audio, 48000.0, 0, half);
    CHECK (secondHalf.numFrames() == 11);
    CHECK_THAT ((*secondHalf.samples)[0], WithinAbs (0.5, 1.0e-6));

    // A chosen frame length beats the file's own; the file's beats 2048.
    CHECK (files::cut ("Marked", audio, 48000.0, 256, {}).frameSize == 256);
    files::CutOptions sized;
    sized.frameSize = 512;
    CHECK (files::cut ("Sized", audio, 48000.0, 256, sized).frameSize == 512);
    // Too short a part for one frame: nothing.
    files::CutOptions tiny;
    tiny.frameSize = 2048;
    tiny.start = 0.99;
    CHECK_FALSE (files::cut ("Tiny", audio, 48000.0, 0, tiny).isValid());
}
