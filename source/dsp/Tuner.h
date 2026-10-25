#pragma once

#include "InputTap.h"
#include "PitchDetector.h"

#include <deque>
#include <optional>

// The TUNER stomp, for checking the pitch of recorded parts (a vocal, a bass
// line) as they play back, rather than for tuning an instrument. It listens
// to what comes into the plugin, one note at a time, and keeps:
//
//   - the current note, steadied for reading (as PR7-B's tuner did);
//   - a trace of the pitch over the last 30 seconds, unsteadied, so slides,
//     vibrato and drift show as they are;
//   - an average: how far, in cents, the notes in the trace sit from the
//     nearest note, to answer "is this part sharp or flat overall?".
//
// Times are the audio's own: when the DAW stops sending audio the trace
// stops too, instead of filling with the last note heard.
//
// Not the audio thread: the processor updates it from its timer, about 30
// times a second, while the tuner is on.
class Tuner
{
public:
    static constexpr double traceSeconds = 30.0;

    struct Reading
    {
        bool hasNote = false;
        PitchDetector::Note note;
        double frequency = 0.0;
    };

    // One point of the trace: when (seconds of audio since the plugin
    // started) and the pitch as a MIDI note number (69 = A4 at 440 Hz,
    // fractional), or no note (silence, noise, a breath, a chord).
    struct Point
    {
        double seconds = 0.0;
        double note = -1.0;
        bool hasNote() const noexcept { return note >= 0.0; }
    };

    // Looks at the newest audio in `tap` and adds to the trace if any has
    // arrived since the last call. `nowMs` is the wall clock, to notice when
    // the audio stops altogether.
    void update (const InputTap& tap, double referenceHz, juce::uint32 nowMs);

    // The tuner's been switched off: forget the current note (the trace stays).
    void stop() noexcept;

    // RESET: empty the trace, and so the average.
    void clear() noexcept;

    const Reading& getReading() const noexcept { return reading; }
    const std::deque<Point>& getTrace() const noexcept { return trace; }

    struct Average
    {
        double cents = 0.0;     // -50 to +50: sharp is positive
        double seconds = 0.0;   // how much of the trace had a note
    };
    // Nothing until there's at least a quarter of a second of notes.
    std::optional<Average> getAverage (double referenceHz) const;

    // How far `note` (as in Point) is from the nearest note, in cents
    // (-50 to +50), with A4 at `referenceHz`.
    static double centsFromNearest (double note, double referenceHz) noexcept;

private:
    PitchDetector detector;
    double preparedRate = 0.0;
    std::vector<float> window;
    std::int64_t lastTotal = -1;
    juce::uint32 lastNewAudioMs = 0;

    double smoothedNote = -1.0;      // the steadied note, for the reading
    double lastHeardSeconds = -1.0;
    Reading reading;
    std::deque<Point> trace;
};
