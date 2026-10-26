#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "dsp/InputTap.h"
#include "dsp/PitchDetector.h"

#include <array>
#include <atomic>
#include <deque>
#include <vector>

// ORACLE (stomp 1, the owner's name), melody to MIDI, inspired by what
// Celemony Melodyne and Steinberg VariAudio do with a single line: it listens
// to a part as the DAW plays it, finds its notes, lets you fix them, and
// hands them over as a MIDI file to drag onto a track. Our own design
// throughout (theirs isn't published); one note at a time, like the tuner.
//
//   - Listening: every 10 ms, the pitch (the tuner's YIN detector) and the
//     level, placed where they are in the song from the DAW's position.
//     Only while the DAW plays; playing a passage again replaces what was
//     heard there before, as a new take would.
//   - Notes (findNotes): a note ends at a silence, at a move to another
//     note that stays (judged on the pitch smoothed over ~150 ms, so
//     vibrato doesn't split a held note), or at a fresh attack (a jump in
//     level) on the same note. Its pitch is the middle of what was sung,
//     leaving out the scoop in and the fall off the end.
//   - The tempo map: the DAW's tempo and time signature, so the MIDI file
//     lines up with the song from bar 1.
namespace oracle
{
    // Every 10 ms of the part: the pitch as a MIDI note number (69 = A4 at
    // 440 Hz, fractional), or below 0 for no clear note, and the level (dBFS).
    struct Frame
    {
        double seconds = 0.0;   // in the song
        float pitch = -1.0f;
        float levelDb = -100.0f;
        bool voiced() const noexcept { return pitch >= 0.0f; }
    };

    struct Note
    {
        double start = 0.0, end = 0.0;   // seconds in the song
        int pitch = 60;                  // MIDI note number
        int velocity = 90;
        float cents = 0.0f;              // where it was sung, from the note: +12 is 12 cents sharp
        juce::uint32 id = 0;             // for the editor's selection
        double length() const noexcept { return end - start; }
    };

    struct Settings
    {
        float sensitivity = 50.0f;   // 0 to 100: higher splits into notes more readily
        float minNoteMs = 80.0f;     // shorter notes are joined to a neighbour or dropped
    };

    constexpr double hopSeconds = 0.01;

    // The notes in a stretch of frames (sorted by time, `hop` apart).
    std::vector<Note> findNotes (const std::vector<Frame>& frames, const Settings&, double hop = hopSeconds);

    // The DAW's tempo and time signature where the part was heard: a point
    // wherever they changed or the song position jumped, the tempo steady
    // between them.
    struct TempoPoint
    {
        double seconds = 0.0, ppq = 0.0, bpm = 120.0;   // ppq: quarter notes from the song's start
        int numerator = 4, denominator = 4;
    };

    class TempoMap
    {
    public:
        TempoMap() { points.push_back ({}); }

        double ppqAt (double seconds) const noexcept;
        double secondsAt (double ppq) const noexcept;
        const TempoPoint& pointAt (double seconds) const noexcept;
        // Records where the DAW says it is; false if that's what the map already says.
        bool note (const TempoPoint&);
        void reset() { points.assign (1, {}); known = false; }
        const std::vector<TempoPoint>& getPoints() const noexcept { return points; }

    private:
        std::vector<TempoPoint> points;   // sorted by seconds
        bool known = false;               // the first real point replaces the 120 BPM default
    };

    // QUANTIZE for the MIDI file: never reorder, only append (saved by index).
    const juce::StringArray quantizeNames { "Off", "1/32", "1/16", "1/16 T", "1/8", "1/8 T", "1/4" };
    double quantizeQuarters (int index) noexcept;   // 0 for Off

    // A MIDI file of `notes` from the song's start (bar 1): the tempo map's
    // tempos and time signature, 960 ticks to the quarter note, the notes
    // one at a time (a note ends where the next one starts).
    juce::MidiFile toMidi (const std::vector<Note>& notes, const TempoMap&, int quantize);

    //==========================================================================
    // Where the DAW is, from the audio thread to ORACLE: a mark wherever the
    // song position stops running on as expected (play, stop, a jump, a loop,
    // a tempo change), tied to the input tap's sample count.
    struct TimelineMark
    {
        std::int64_t tapIndex = 0;     // the input tap sample it applies from
        std::int64_t hostSample = 0;   // the song position there, in samples
        double ppq = 0.0, bpm = 120.0;
        int numerator = 4, denominator = 4;
        bool playing = false;
        bool known = false;            // false: the host gives no position (ORACLE runs on its own clock)
    };

    class Timeline
    {
    public:
        // Audio thread, before the chunk's samples go into the tap.
        void write (const TimelineMark& mark) noexcept;
        // Message thread.
        int read (TimelineMark* destination, int maximum) noexcept;

    private:
        bool differs (const TimelineMark&) const noexcept;
        static constexpr int size = 1024;
        juce::AbstractFifo fifo { size };
        std::array<TimelineMark, size> marks;
        TimelineMark last;
        bool any = false;
    };

    //==========================================================================
    // The transcription, on the message thread (the processor's timer).
    class Transcriber
    {
    public:
        // Reads what the input tap has gained since last time, while `listening`
        // (ORACLE on). Finds the notes again where anything was heard.
        void update (const InputTap&, Timeline&, bool listening);

        // Whether it heard the DAW playing at the last update.
        bool isHearing() const noexcept { return hearing; }
        double getNowSeconds() const noexcept { return nowSeconds; }

        const std::vector<Frame>& getFrames() const noexcept { return frames; }
        const std::vector<Note>& getNotes() const noexcept { return notes; }
        const TempoMap& getTempo() const noexcept { return tempo; }
        const Settings& getSettings() const noexcept { return settings; }
        // Changing these finds every note again (one undo step).
        void setSettings (const Settings&);
        int getQuantize() const noexcept { return quantize; }
        void setQuantize (int index) noexcept { quantize = juce::jlimit (0, quantizeNames.size() - 1, index); }
        // Grows with every change, for the page to repaint.
        juce::uint32 getVersion() const noexcept { return version; }

        // Editing. Each is one undo step; a drag calls beginEdit() once, then
        // setNotes (..., false) as it goes.
        void beginEdit();
        void setNotes (std::vector<Note>, bool undoable = true);
        void transpose (const std::vector<juce::uint32>& ids, int semitones);
        void remove (const std::vector<juce::uint32>& ids);
        juce::uint32 split (juce::uint32 id, double seconds);   // the new right half's id, or 0
        // Joins the notes into one (the first's pitch); a single note joins the next one.
        void join (std::vector<juce::uint32> ids);
        juce::uint32 add (double start, double end, int pitch);
        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }
        void undo();
        void redo();
        void clear();   // everything heard, the notes and the tempo map (undo brings the notes back)

        const Note* find (juce::uint32 id) const noexcept;

        juce::MidiFile toMidi() const { return oracle::toMidi (notes, tempo, quantize); }
        // Writes the MIDI file into `folder`, named for now; the file, or none on failure.
        juce::File writeMidiFile (const juce::File& folder) const;

        // Saved with the DAW project.
        juce::ValueTree toValueTree() const;
        void fromValueTree (const juce::ValueTree&);

        // For the plugin's state, which a host may ask for on another thread.
        juce::CriticalSection& getLock() noexcept { return lock; }

    private:
        void prepare (double sampleRate);
        void endRun() noexcept { runActive = false; }
        void addFrame (const Frame&);
        void findNotesAround (double from, double to);
        void pushUndo();
        void changed() noexcept { ++version; }
        juce::uint32 nextId() noexcept { return ++lastId; }

        juce::CriticalSection lock;
        PitchDetector detector;
        double preparedRate = 0.0;
        std::int64_t hop = 480, nextCentre = -1;
        std::vector<float> window;
        std::deque<TimelineMark> pending;
        TimelineMark current;
        bool runActive = false, hearing = false;
        double runLast = 0.0, nowSeconds = 0.0;

        std::vector<Frame> frames;   // sorted by time
        std::vector<Note> notes;     // sorted by start
        TempoMap tempo;
        Settings settings;
        int quantize = 0;
        juce::uint32 version = 0, lastId = 0;
        std::vector<std::vector<Note>> undoStack, redoStack;
    };
}
