// WAVETABLES (stage 3): the tables' contents, and what the oscillator does with them, measured.
#include "SynthTestUtils.h"

#include "dsp/synth/Wavetable.h"

namespace
{
    // The amplitude of harmonic `h` in one cycle of `size` samples.
    double harmonicOf (const float* cycle, int size, int h)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < size; ++i)
        {
            const auto t = juce::MathConstants<double>::twoPi * h * i / size;
            re += cycle[i] * std::cos (t);
            im += cycle[i] * std::sin (t);
        }
        return 2.0 * std::sqrt (re * re + im * im) / size;
    }

    double harmonicOf (synth::Wavetable::Level level, int h) { return harmonicOf (level.data, level.size, h); }

    // A table whose frame k is a sine at harmonic harmonics[k].
    synth::WavetableSource sines (std::initializer_list<int> harmonics, int frameSize = 2048)
    {
        auto samples = std::make_shared<std::vector<float>>();
        for (const auto h : harmonics)
            for (int i = 0; i < frameSize; ++i)
                samples->push_back ((float) std::sin (juce::MathConstants<double>::twoPi * h * i / frameSize));
        return { "Sines", frameSize, samples };
    }

    // A plain ramp, drawn sample by sample: harmonics all the way up (as a WAV of a raw saw would be).
    synth::WavetableSource ramp (int frameSize)
    {
        auto samples = std::make_shared<std::vector<float>> ((size_t) frameSize);
        for (int i = 0; i < frameSize; ++i)
            (*samples)[(size_t) i] = 2.0f * (float) i / (float) frameSize - 1.0f;
        return { "Ramp", frameSize, samples };
    }

    // A WAV file's bytes: 16-bit mono, with a "clm " chunk if `clm` isn't empty.
    juce::MemoryBlock wavBytes (const std::vector<float>& samples, const juce::String& clm = {})
    {
        juce::MemoryOutputStream out;
        const auto dataBytes = (int) samples.size() * 2;
        juce::MemoryOutputStream clmChunk;
        if (clm.isNotEmpty())
        {
            clmChunk.write ("clm ", 4);
            const auto text = clm.toStdString();
            clmChunk.writeInt ((int) text.size());
            clmChunk.write (text.data(), text.size());
            if (text.size() % 2 == 1)
                clmChunk.writeByte (0);
        }
        out.write ("RIFF", 4);
        out.writeInt (4 + 24 + (int) clmChunk.getDataSize() + 8 + dataBytes);
        out.write ("WAVE", 4);
        out.write ("fmt ", 4);
        out.writeInt (16);
        out.writeShort (1);         // PCM
        out.writeShort (1);         // mono
        out.writeInt (48000);
        out.writeInt (48000 * 2);
        out.writeShort (2);
        out.writeShort (16);
        out.write (clmChunk.getData(), clmChunk.getDataSize());
        out.write ("data", 4);
        out.writeInt (dataBytes);
        for (const auto x : samples)
            out.writeShort ((short) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, x) * 32767.0f));
        return out.getMemoryBlock();
    }
}

TEST_CASE ("Wavetables: each frame keeps its own harmonics, scaled so the loudest frame peaks at 1", "[wavetable]")
{
    const auto table = synth::Wavetable::build (sines ({ 1, 3, 7 }));
    REQUIRE (table != nullptr);
    CHECK (table->getNumFrames() == 3);
    for (int frame = 0; frame < 3; ++frame)
    {
        const auto level = table->get (frame, 0);
        const auto wanted = frame == 0 ? 1 : frame == 1 ? 3 : 7;
        INFO ("frame " << frame);
        CHECK_THAT (harmonicOf (level, wanted), WithinAbs (1.0, 1.0e-3));
        for (int h = 1; h <= 10; ++h)
            if (h != wanted)
                CHECK (harmonicOf (level, h) < 1.0e-4);
    }

    // Half as loud a file plays just as loud (scaled to peak at 1), and DC is removed.
    auto quiet = sines ({ 2 });
    auto samples = std::make_shared<std::vector<float>> (*quiet.samples);
    for (auto& x : *samples)
        x = 0.5f * x + 0.25f;
    quiet.samples = samples;
    const auto scaled = synth::Wavetable::build (quiet);
    const auto level = scaled->get (0, 0);
    CHECK_THAT (harmonicOf (level, 2), WithinAbs (1.0, 1.0e-3));
    double mean = 0.0;
    for (int i = 0; i < level.size; ++i)
        mean += level.data[i];
    CHECK (std::abs (mean / level.size) < 1.0e-5);
}

TEST_CASE ("Wavetables: every level holds only the harmonics it's allowed, so nothing can alias", "[wavetable][aliasing]")
{
    // A raw ramp has harmonics all the way up; each level keeps them only up to its count.
    const auto table = synth::Wavetable::build (ramp (2048));
    for (const auto levelIndex : { 0, 3, 9, 15, 21, 27 })
    {
        const auto level = table->get (0, levelIndex);
        const auto allowed = synth::tables::harmonicsAt (levelIndex);
        INFO ("level " << levelIndex << ", " << allowed << " harmonics");
        CHECK (level.size >= 8 * allowed);
        CHECK (harmonicOf (level, allowed) > 0.5 / allowed);          // its top harmonic is there...
        if (allowed + 1 < level.size / 2)
            CHECK (harmonicOf (level, allowed + 1) < 1.0e-5);          // ...and the next isn't
        CHECK (level.data[level.size] == level.data[0]);               // the guard sample repeats the first
    }
}

TEST_CASE ("WAVETABLE plays the frame INDEX points at, blends SMOOTH between frames, and jumps STEPPY", "[wavetable]")
{
    const auto table = synth::Wavetable::build (sines ({ 1, 3 }));
    const auto play = [&] (float index, bool steppy)
    {
        Player p;
        p.osc().wave = synth::OscWave::wavetable;
        p.osc().table = table.get();
        p.osc().tableIndex = index;
        p.osc().tableSteppy = steppy;
        p.on (57);   // 220 Hz
        return p.render (0.5)[0];
    };
    const auto first = play (0.0f, false);
    CHECK_THAT (levelAt (first, 220.0, 12000, 12000), WithinAbs (1.0, 0.01));
    CHECK (levelAt (first, 660.0, 12000, 12000) < 0.001);

    const auto second = play (1.0f, false);
    CHECK (levelAt (second, 220.0, 12000, 12000) < 0.001);
    CHECK_THAT (levelAt (second, 660.0, 12000, 12000), WithinAbs (1.0, 0.01));

    const auto between = play (0.25f, false);   // a quarter of the way: 3/4 of the first, 1/4 of the second
    CHECK_THAT (levelAt (between, 220.0, 12000, 12000), WithinAbs (0.75, 0.01));
    CHECK_THAT (levelAt (between, 660.0, 12000, 12000), WithinAbs (0.25, 0.01));

    const auto stepped = play (0.4f, true);     // STEPPY: the nearest frame only
    CHECK_THAT (levelAt (stepped, 220.0, 12000, 12000), WithinAbs (1.0, 0.01));
    CHECK (levelAt (stepped, 660.0, 12000, 12000) < 0.001);
}

TEST_CASE ("WAVETABLE: INDEX glides, with no clicks, when it moves while a note plays", "[wavetable]")
{
    const auto table = synth::Wavetable::build (sines ({ 1, 2 }));
    Player p;
    p.osc().wave = synth::OscWave::wavetable;
    p.osc().table = table.get();
    p.on (57);
    std::vector<float> out;
    for (int block = 0; block < 100; ++block)
    {
        p.osc().tableIndex = (float) block / 99.0f;   // a sweep, a step each block
        const auto part = p.render (256.0 / sampleRate)[0];
        out.insert (out.end(), part.begin(), part.end());
    }
    // A sine at 220 Hz and its octave never move more than this from one sample to the next;
    // a jump between frames would.
    double biggest = 0.0;
    for (size_t i = 4800; i < out.size(); ++i)
        biggest = std::max (biggest, (double) std::abs (out[i] - out[i - 1]));
    CHECK (biggest < 2.0 * juce::MathConstants<double>::twoPi * 440.0 / sampleRate);
}

TEST_CASE ("WAVETABLE FRAMES: INDEX steps through that many frames, spread from first to last", "[wavetable]")
{
    const auto table = synth::Wavetable::build (sines ({ 1, 2, 3, 4, 5 }));
    // All five: index 0.5 is the middle frame (harmonic 3).
    auto position = table->locate (0.5f, 256, false);
    CHECK (position.below == 2);
    CHECK (position.between == 0.0f);
    // Two: the first and the last, index 0.5 halfway between them.
    position = table->locate (0.5f, 2, false);
    CHECK (position.below == 0);
    CHECK (position.above == 4);
    CHECK_THAT (position.between, WithinAbs (0.5, 1.0e-6));
    // Three: first, middle and last.
    position = table->locate (0.75f, 3, false);
    CHECK (position.below == 2);
    CHECK (position.above == 4);
    CHECK_THAT (position.between, WithinAbs (0.5, 1.0e-6));
    // STEPPY with three: 0.3 is nearest the middle frame.
    position = table->locate (0.3f, 3, true);
    CHECK (position.below == 2);
    CHECK (position.above == 2);
}

TEST_CASE ("WAVETABLE is as free of aliasing as the saw: a bright frame high up the keyboard", "[wavetable][aliasing]")
{
    // The Hard Sync table's last frame (a saw restarting 8 times a cycle: very bright) at E7.
    const auto table = synth::factoryTables::get (synth::factoryTables::names.indexOf ("Hard Sync"));
    Player p;
    p.osc().wave = synth::OscWave::wavetable;
    p.osc().table = table.get();
    p.osc().tableIndex = 1.0f;
    p.on (100);
    const auto out = p.render (1.6);
    const Spectrum spectrum (out[0], 4096);
    const auto worst = spectrum.loudestBetweenHarmonics (noteHz (100), 20000.0);
    WARN ("Hard Sync's brightest frame at 2.6 kHz, loudest between harmonics: " << worst << " dB");
    CHECK (worst < -90.0);
}

TEST_CASE ("Factory wavetables: ten of our own, each changing as INDEX moves", "[wavetable][factory]")
{
    using namespace synth;
    REQUIRE (factoryTables::names.size() == 10);

    // Building one (done on the message thread when a table is first chosen): how long it takes.
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto timed = Wavetable::build (factoryTables::source (3));
    const auto ms = juce::Time::getMillisecondCounterHiRes() - started;
    WARN ("generating and building a 64-frame factory table: " << ms << " ms");
    CHECK (timed != nullptr);
    CHECK (ms < 2000.0);
    for (int index = 0; index < factoryTables::names.size(); ++index)
    {
        const auto table = factoryTables::get (index);
        INFO (factoryTables::names[index]);
        REQUIRE (table != nullptr);
        CHECK (table->getNumFrames() == 64);
        CHECK (table->getName() == factoryTables::names[index]);
        CHECK (table->getMemoryBytes() < 20 * 1024 * 1024);   // about 16 MB, built only when chosen

        // Peaks about 1 (a band-limited wave overshoots a little, as the saw does).
        float peak = 0.0f;
        for (int frame = 0; frame < table->getNumFrames(); ++frame)
        {
            const auto level = table->get (frame, 0);
            for (int i = 0; i < level.size; ++i)
                peak = std::max (peak, std::abs (level.data[i]));
        }
        CHECK (peak > 0.9f);
        CHECK (peak < 1.25f);

        // The first and last frames sound different.
        const auto first = table->get (0, 6), last = table->get (63, 6);
        double difference = 0.0;
        for (int i = 0; i < first.size; ++i)
            difference = std::max (difference, (double) std::abs (first.data[i] - last.data[i]));
        CHECK (difference > 0.2);
    }

    // A few of them, checked for what they say they are.
    const auto table = [] (const char* name) { return factoryTables::get (factoryTables::names.indexOf (name)); };
    const auto pulse = table ("Pulse Width");
    CHECK (harmonicOf (pulse->get (0, 0), 2) < 1.0e-3);                     // 50%: a square, no even harmonics
    CHECK (harmonicOf (pulse->get (63, 0), 2) > 0.05);                      // a thin pulse has them
    const auto rise = table ("Harmonic Rise");
    CHECK (harmonicOf (rise->get (0, 0), 2) < 1.0e-3);                      // the first frame: just the sine
    CHECK (harmonicOf (rise->get (63, 0), 64) > 0.5 * harmonicOf (rise->get (63, 0), 1) / 64.0);   // the last: 64 harmonics...
    CHECK (harmonicOf (rise->get (63, 0), 65) < 1.0e-4);                    // ...and no more
    const auto analog = table ("Analog Morph");
    CHECK (harmonicOf (analog->get (0, 0), 2) < 1.0e-3);                    // starts as a sine
    CHECK (harmonicOf (analog->get (0, 0), 3) < 1.0e-3);
    const auto organ = table ("Organ Stack");
    CHECK (harmonicOf (organ->get (63, 0), 5) < 1.0e-3);                    // only octaves and fifths: no 5th harmonic
    CHECK (harmonicOf (organ->get (63, 0), 16) > 0.05);
}

TEST_CASE ("WAV files: cut into frames by their clm chunk, as one cycle, or in 2048s", "[wavetable][files]")
{
    const auto folder = juce::File::createTempFile ("pr7k-wavetables");
    folder.createDirectory();
    const auto write = [&] (const juce::String& name, const juce::MemoryBlock& bytes)
    {
        const auto file = folder.getChildFile (name);
        file.replaceWithData (bytes.getData(), bytes.getSize());
        return file;
    };
    const auto cycles = [] (int frameSize, int frames)
    {
        std::vector<float> samples;
        for (int k = 0; k < frames; ++k)
            for (int i = 0; i < frameSize; ++i)
                samples.push_back (0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * (k + 1) * i / frameSize));
        return samples;
    };

    // Written by another wavetable synth: 256-sample frames, as its chunk says.
    const auto tagged = synth::wavetableFiles::load (write ("Tagged.wav", wavBytes (cycles (256, 8), "<!>256 10000000 wavetable")));
    CHECK (tagged.frameSize == 256);
    CHECK (tagged.numFrames() == 8);
    CHECK (tagged.name == "Tagged");
    CHECK (synth::wavetableFiles::frameSizeFromChunk (wavBytes (cycles (256, 1), "<!>256")) == 256);

    // One cycle of an odd length (single-cycle packs often use 600): one frame of it.
    const auto single = synth::wavetableFiles::load (write ("Single.wav", wavBytes (cycles (600, 1))));
    CHECK (single.frameSize == 600);
    CHECK (single.numFrames() == 1);
    const auto built = synth::Wavetable::build (single);
    CHECK_THAT (harmonicOf (built->get (0, 0), 1), WithinAbs (1.0, 0.01));
    CHECK (harmonicOf (built->get (0, 0), 2) < 1.0e-3);

    // A longer file with no chunk: frames of 2048.
    const auto plain = synth::wavetableFiles::load (write ("Plain.wav", wavBytes (cycles (2048, 5))));
    CHECK (plain.frameSize == 2048);
    CHECK (plain.numFrames() == 5);

    // Over 256 frames: 256, spread through it.
    const auto many = synth::wavetableFiles::load (write ("Many.wav", wavBytes (cycles (64, 300), "<!>64")));
    CHECK (many.numFrames() == 256);

    // Not audio: nothing.
    CHECK (! synth::wavetableFiles::load (write ("Not.wav", juce::MemoryBlock ("hello", 5))).isValid());
    folder.deleteRecursively();
}

TEST_CASE ("Wavetables survive saving: 16-bit, compressed, within a 32767th", "[wavetable][files]")
{
    const auto table = synth::factoryTables::source (synth::factoryTables::names.indexOf ("Vowels"));
    const auto text = synth::wavetableFiles::encode (table);
    WARN ("a 64-frame table saved: " << text.length() / 1024 << " KB of text");
    CHECK (text.length() < 400 * 1024);
    const auto back = synth::wavetableFiles::decode ("Vowels", 2048, text);
    REQUIRE (back.numFrames() == table.numFrames());
    float peak = 0.0f, error = 0.0f;
    for (size_t i = 0; i < table.samples->size(); ++i)
    {
        peak = std::max (peak, std::abs ((*table.samples)[i]));
        error = std::max (error, std::abs ((*table.samples)[i] - (*back.samples)[i]));
    }
    CHECK (error <= peak / 32767.0f * 0.51f + 1.0e-7f);
    CHECK (! synth::wavetableFiles::decode ("Bad", 2048, "not base64 at all!").isValid());
}

TEST_CASE ("CPU: wavetable supersaw chords cost about what saw ones do", "[wavetable][performance]")
{
    const auto table = synth::factoryTables::get (0);
    const auto percent = [&] (bool wavetable)
    {
        Player p;
        for (int i = 0; i < 2; ++i)
        {
            auto& o = p.osc (i);
            o.on = true;
            o.wave = wavetable ? synth::OscWave::wavetable : synth::OscWave::saw;
            o.table = table.get();
            o.tableIndex = 0.37f;   // between two frames: two reads a sample
            o.unisonVoices = 7;
            o.unisonMix = 0.8f;
            o.unisonDetune = 0.4f;
        }
        for (const auto note : { 48, 52, 55, 59, 62, 64, 67, 71 })
            p.on (note);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        for (int block = 0; block < 1000; ++block)
        {
            p.osc (0).tableIndex = 0.37f + 0.3f * (float) std::sin (block * 0.05);   // moving, as under modulation
            p.render (256.0 / sampleRate);
        }
        const auto cost = (juce::Time::getMillisecondCounterHiRes() - started) / (256000.0 / sampleRate * 1000.0) * 100.0;
        WARN ((wavetable ? "wavetable" : "saw") << ": 2 oscillators x 7 copies x 8 keys, " << cost << "% of one core");
        return cost;
    };
    const auto saw = percent (false);
    const auto wavetable = percent (true);
    CHECK (wavetable < 40.0);
    CHECK (wavetable < saw * 3.0);
}

//==============================================================================
// The plugin: loading a file, and keeping it with the project, the preset and undo.
#include "PluginProcessor.h"

namespace
{
    // A one-frame WAV file: a sine at harmonic `h` of a 2048-sample cycle.
    juce::File writeCycle (const juce::File& folder, const juce::String& name, int h)
    {
        std::vector<float> samples (2048);
        for (int i = 0; i < 2048; ++i)
            samples[(size_t) i] = 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * h * i / 2048);
        const auto file = folder.getChildFile (name + ".wav");
        const auto bytes = wavBytes (samples);
        file.replaceWithData (bytes.getData(), bytes.getSize());
        return file;
    }

    // The strongest harmonic (of the first 16) in a table's first frame.
    int strongestHarmonic (const synth::Wavetable& table)
    {
        int best = 1;
        for (int h = 2; h <= 16; ++h)
            if (harmonicOf (table.get (0, 0), h) > harmonicOf (table.get (0, 0), best))
                best = h;
        return best;
    }

    float plain (ParagonX1AudioProcessor& processor, const char* id)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        return parameter->convertFrom0to1 (parameter->getValue());
    }
}

TEST_CASE ("A loaded wavetable plays, and is kept with the project, the preset and undo", "[wavetable][processor]")
{
    const auto folder = juce::File::createTempFile ("pr7k-tables");
    folder.createDirectory();
    const auto file = writeCycle (folder, "Third", 3);

    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    CHECK (processor.getSynthSettings().oscillators[0].table == nullptr);   // a saw: no table built for nothing

    // Loading: oscillator 1 becomes a WAVETABLE playing it.
    REQUIRE (processor.loadWavetableFile (0, file));
    CHECK (processor.hasLoadedWavetable (0));
    CHECK (juce::roundToInt (plain (processor, "osc1Wave")) == (int) synth::OscWave::wavetable);
    const auto* playing = processor.getSynthSettings().oscillators[0].table;
    REQUIRE (playing != nullptr);
    CHECK (playing->getName() == "Third");
    CHECK (strongestHarmonic (*playing) == 3);
    CHECK_FALSE (processor.loadWavetableFile (0, folder.getChildFile ("missing.wav")));

    // It sounds: A3 (220 Hz) comes out at its 3rd harmonic, 660 Hz.
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
        std::vector<float> out;
        for (int block = 0; block < 100; ++block)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
            midi.clear();
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }
        CHECK (levelAt (out, 660.0, 9600, 12000) > 10.0 * levelAt (out, 220.0, 9600, 12000));
    }

    // The project: a new instance gets it back from the saved state.
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    {
        ParagonX1AudioProcessor reopened;
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        REQUIRE (reopened.hasLoadedWavetable (0));
        CHECK (reopened.getWavetable (0)->getName() == "Third");
        CHECK (strongestHarmonic (*reopened.getWavetable (0)) == 3);
        CHECK (reopened.getSynthSettings().oscillators[0].table != nullptr);
        CHECK_FALSE (reopened.hasLoadedWavetable (1));
    }

    // The preset: saved inside it, and back.
    const auto rig = processor.captureRig();
    REQUIRE (rig.wavetables.count (0) == 1);
    const auto reloaded = Rig::fromXml (*rig.toXml());
    REQUIRE (reloaded.has_value());
    REQUIRE (reloaded->wavetables.count (0) == 1);
    CHECK (reloaded->wavetables.at (0).name == "Third");
    CHECK (reloaded->wavetables.at (0).numFrames() == 1);

    // Choosing a factory table forgets the file; undo brings it back.
    processor.flushUndoStep();
    processor.chooseFactoryWavetable (0, synth::factoryTables::names.indexOf ("Organ Stack"));
    CHECK_FALSE (processor.hasLoadedWavetable (0));
    CHECK (processor.getSynthSettings().oscillators[0].table->getName() == "Organ Stack");
    processor.flushUndoStep();
    processor.undo();
    REQUIRE (processor.hasLoadedWavetable (0));
    CHECK (processor.getSynthSettings().oscillators[0].table->getName() == "Third");
    processor.redo();
    CHECK (processor.getSynthSettings().oscillators[0].table->getName() == "Organ Stack");

    // A preset without one (Init) lets it go.
    processor.loadWavetableFile (0, file);
    const auto* init = processor.getRigLibrary().findFactory ("Init");
    REQUIRE (init != nullptr);
    processor.loadRig (*init);
    CHECK_FALSE (processor.hasLoadedWavetable (0));
    CHECK (processor.getSynthSettings().oscillators[0].table == nullptr);

    folder.deleteRecursively();
}

TEST_CASE ("Wavetable presets: the factory's play their tables", "[wavetable][processor]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);
    for (const auto* name : { "Vowel Pad", "Sync Lead", "Bit Bass" })
    {
        const auto* entry = processor.getRigLibrary().findFactory (name);
        REQUIRE (entry != nullptr);
        processor.loadRig (*entry);
        const auto* table = processor.getSynthSettings().oscillators[0].table;
        INFO (name);
        REQUIRE (table != nullptr);
        CHECK (synth::factoryTables::names.contains (table->getName()));
    }
}
