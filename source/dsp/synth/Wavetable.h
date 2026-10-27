#pragma once

#include <juce_core/juce_core.h>

#include "WaveTables.h"

#include <functional>
#include <memory>
#include <vector>

// WAVETABLES (stage 3): an oscillator that plays a row of single-cycle
// waves ("frames") and moves through them with its INDEX, as Avenger's
// wavetable oscillator does. Our own design and code.
//
// Each frame is stored at the same 28 levels as the basic waves
// (WaveTables.h): level 0 with up to 512 harmonics, each level after it a
// third of an octave fewer. A note reads the level whose harmonics all fit
// under the limit, so a wavetable is as free of aliasing as the saw, at any
// pitch, with no oversampling. Moving the INDEX blends two neighbouring
// frames: two table reads a sample, the same as a PULSE.
//
// Building a table (an FFT of each frame, then one inverse FFT per level)
// takes a few milliseconds a frame, so it's done on the message thread when
// a table is chosen or loaded, never on the audio thread.
namespace synth
{
    // What a table is made from: its frames, one after another, as loaded
    // (from the factory list, or a WAV file). This is what presets and
    // projects save; the playable levels are rebuilt from it.
    struct WavetableSource
    {
        juce::String name;
        int frameSize = 2048;                               // samples per frame (any length from 32 to 4096)
        std::shared_ptr<const std::vector<float>> samples;  // numFrames() x frameSize

        int numFrames() const noexcept { return samples == nullptr || frameSize <= 0 ? 0 : (int) samples->size() / frameSize; }
        bool isValid() const noexcept { return numFrames() >= 1; }
        // The same table: same name, size and the very same samples (cheap, for undo).
        bool operator== (const WavetableSource& other) const noexcept
        {
            return name == other.name && frameSize == other.frameSize && samples == other.samples;
        }
    };

    class Wavetable
    {
    public:
        static constexpr int maxFrames = 256;               // as Avenger's
        static constexpr int maxFrameSize = 4096;
        static constexpr int minFrameSize = 32;

        // Builds the playable levels. Frames beyond maxFrames are left out
        // (evenly, keeping the first and last); each frame's DC is removed,
        // and the whole table scaled so its loudest frame peaks at 1 (its
        // frames keep their levels against each other).
        static std::shared_ptr<const Wavetable> build (const WavetableSource&);

        // The same, each harmonic h (from 1) scaled by `gain (h)`: the FFT
        // editor's edits. The scale stays the unedited source's, so boosting
        // harmonics makes it louder and cutting them quieter, as an EQ would.
        // `normalise` false keeps the source's own level (the basic waves,
        // whose unedited tables aren't scaled either).
        static std::shared_ptr<const Wavetable> build (const WavetableSource&, const std::function<float (int harmonic)>& gain,
                                                       bool normalise = true);

        // The amplitudes of harmonics 1 to `count` of one cycle (element 0 unused).
        static std::vector<float> harmonicLevels (const float* cycle, int size, int count);

        int getNumFrames() const noexcept { return numFrames; }
        const WavetableSource& getSource() const noexcept { return source; }
        const juce::String& getName() const noexcept { return source.name; }

        // One level of one frame: `size` samples plus one (the first again),
        // size a power of two.
        struct Level
        {
            const float* data;
            int size;
        };
        Level get (int frame, int level) const noexcept
        {
            frame = juce::jlimit (0, numFrames - 1, frame);
            level = juce::jlimit (0, tables::numLevels - 1, level);
            return { data.data() + (size_t) frame * frameStride + levelOffsets[(size_t) level], levelSizes[(size_t) level] };
        }

        // Reads a level at `phase` (0 to 1), interpolating between samples.
        static float read (Level level, float phase) noexcept
        {
            const auto position = phase * (float) level.size;
            const auto index = (int) position;
            const auto fraction = position - (float) index;
            const auto* at = level.data + (index & (level.size - 1));
            return at[0] + (at[1] - at[0]) * fraction;
        }

        // How many samples each level holds: 8 per harmonic (as the basic
        // waves' 4096 for 512), a power of two, and never fewer than 2048.
        // Reading between samples blurs a little and leaves faint copies of
        // the harmonics far above hearing, which fold back down as tones
        // between them; the more samples per cycle, the fainter they are. A
        // level of 10 harmonics in 128 samples left them at -47 dB on a bright
        // frame; in 2048, they're below -90 dB, as the saw's are.
        static int levelSize (int level) noexcept;

        // Where INDEX (0 to 1) lands with only `framesUsed` of the frames in
        // play (FRAMES: 2 to 256, spread evenly from the first to the last):
        // the frame below, the frame above and how far between them (0 to 1).
        struct Position
        {
            int below = 0, above = 0;
            float between = 0.0f;
        };
        Position locate (float index, int framesUsed, bool steppy) const noexcept;

        // Memory the levels take, in bytes (about 250 KB a frame: 16 MB for a
        // 64-frame table, 64 MB for a full 256), for the tests' budget.
        size_t getMemoryBytes() const noexcept { return data.size() * sizeof (float); }

    private:
        Wavetable() = default;

        WavetableSource source;
        int numFrames = 0;
        size_t frameStride = 0;
        std::array<size_t, tables::numLevels> levelOffsets {};
        std::array<int, tables::numLevels> levelSizes {};
        std::vector<float> data;
    };

    // The factory's tables: our own, generated, built the first time they're
    // wanted and kept from then on (shared by every instance of the plugin).
    namespace factoryTables
    {
        // Saved by index (osc*Table): only ever append.
        extern const juce::StringArray names;

        // The table, built if it hasn't been yet (message thread, or tests).
        std::shared_ptr<const Wavetable> get (int index);

        // Its frames, as generated (no building).
        WavetableSource source (int index);
    }

    // Reading WAV (or AIFF, FLAC...) files into tables.
    namespace wavetableFiles
    {
        // How to cut a file into frames (the resampler's options, as
        // Avenger's): the frame length, which frames, and the stretch of the
        // file to take them from. The defaults load as before: the length the
        // file says (its "clm " chunk), else one cycle for a short file, else
        // 2048s, one after another.
        struct CutOptions
        {
            int frameSize = 0;         // samples (0: as above)
            int rootNote = -1;         // or a MIDI note the file is in (a frame is one cycle of it): 24 = C1
            int step = 1;              // one after another (1), or every 2nd, 3rd, 4th...
            int spread = 0;            // or this many frames spread evenly through it, overlapping if need be (0: off)
            double start = 0.0, end = 1.0;   // the part of the file to use, 0 to 1
        };

        // How many samples a cycle of `note` is at `sampleRate` (a root note's frame length).
        inline int cycleLength (int note, double sampleRate)
        {
            return juce::roundToInt (sampleRate / (440.0 * std::exp2 ((note - 69) / 12.0)));
        }

        // The frames for `options` from mono audio at `sampleRate`.
        WavetableSource cut (const juce::String& name, const std::vector<float>& audio, double sampleRate, int markedFrameSize, const CutOptions& options);

        // The frames in a file, mixed to mono. How it's cut into frames:
        //   - a frame size written in the file (the "clm " chunk other
        //     wavetable synths write), or
        //   - a whole file of 4096 samples or fewer: one frame (a single cycle), or
        //   - otherwise frames of 2048 samples, the size most wavetable packs use.
        // Over 256 frames: 256 spread evenly through it. Empty if the file
        // can't be read.
        WavetableSource load (const juce::File&, const CutOptions& options = {});

        // A file's length and sample rate (to offer the options), or 0 samples if it can't be read.
        struct FileInfo { juce::int64 samples = 0; double sampleRate = 0.0; int markedFrameSize = 0; };
        FileInfo inspect (const juce::File&);

        // The frame size a "clm " chunk gives, or 0 (the bytes of a WAV file).
        int frameSizeFromChunk (const juce::MemoryBlock& wavFile);

        // Saving a table's frames in a preset or project, and back: 16-bit,
        // compressed, as text (a 64-frame table is about 200 KB).
        juce::String encode (const WavetableSource&);
        WavetableSource decode (const juce::String& name, int frameSize, const juce::String& text);
    }
}
