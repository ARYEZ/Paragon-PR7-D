#include "Tuner.h"

namespace
{
    // A note is held this long after the detector last heard it, so a short
    // gap (a consonant, a pick scrape) doesn't blank the reading.
    constexpr double holdSeconds = 0.6;
    constexpr juce::uint32 holdMs = 600;

    double noteOf (double frequency) { return 69.0 + 12.0 * std::log2 (frequency / 440.0); }
}

void Tuner::update (const InputTap& tap, double referenceHz, juce::uint32 nowMs)
{
    const auto sampleRate = tap.getSampleRate();
    if (sampleRate <= 0.0)
        return;

    if (! juce::exactlyEqual (sampleRate, preparedRate))
    {
        detector.prepare (sampleRate);
        preparedRate = sampleRate;
        window.resize ((size_t) juce::jmin (detector.windowSizeAtHostRate(), tap.capacity()));
        trace.clear();   // times from before are at another rate
    }

    const auto total = tap.getTotalWritten();
    if (total == lastTotal)
    {
        // No new audio: the DAW has stopped sending any. Add nothing to the
        // trace, and let the reading go once the note would have.
        if (nowMs - lastNewAudioMs > holdMs)
            smoothedNote = -1.0;
    }
    else
    {
        lastTotal = total;
        lastNewAudioMs = nowMs;
        const auto seconds = (double) total / sampleRate;

        tap.readLatest (window.data(), (int) window.size());
        Point point { seconds, -1.0 };
        if (const auto result = detector.detect (window.data(), (int) window.size()))
        {
            point.note = noteOf (result->frequency);

            // Steady the reading: ease towards small changes, but jump
            // straight to a new note rather than sliding across to it.
            if (smoothedNote < 0.0 || std::abs (point.note - smoothedNote) > 0.5)
                smoothedNote = point.note;
            else
                smoothedNote += (point.note - smoothedNote) * 0.35;
            lastHeardSeconds = seconds;
        }
        else if (seconds - lastHeardSeconds > holdSeconds)
        {
            smoothedNote = -1.0;   // the note has died away
        }

        trace.push_back (point);
        while (! trace.empty() && trace.front().seconds < seconds - traceSeconds)
            trace.pop_front();
    }

    reading = {};
    if (smoothedNote >= 0.0)
    {
        reading.hasNote = true;
        reading.frequency = 440.0 * std::pow (2.0, (smoothedNote - 69.0) / 12.0);
        reading.note = PitchDetector::toNote (reading.frequency, referenceHz);
    }
}

void Tuner::stop() noexcept
{
    smoothedNote = -1.0;
    reading = {};
}

void Tuner::clear() noexcept
{
    trace.clear();
}

double Tuner::centsFromNearest (double note, double referenceHz) noexcept
{
    const auto semitones = note - 12.0 * std::log2 (referenceHz / 440.0);
    return (semitones - std::round (semitones)) * 100.0;
}

std::optional<Tuner::Average> Tuner::getAverage (double referenceHz) const
{
    // Cents are averaged round a circle, 100 cents to a turn: a part sitting
    // right between two notes reads +-50, where a plain average of +49 and
    // -49 (the same pitch, called sharp of one note and flat of the next)
    // would claim it's in tune. Each point counts for the time it covers.
    double x = 0.0, y = 0.0, seconds = 0.0;
    for (size_t i = 1; i < trace.size(); ++i)
    {
        if (! trace[i].hasNote())
            continue;
        const auto duration = juce::jmin (0.1, trace[i].seconds - trace[i - 1].seconds);
        const auto angle = juce::MathConstants<double>::twoPi * centsFromNearest (trace[i].note, referenceHz) / 100.0;
        x += duration * std::cos (angle);
        y += duration * std::sin (angle);
        seconds += duration;
    }

    if (seconds < 0.25)
        return std::nullopt;
    return Average { std::atan2 (y, x) / juce::MathConstants<double>::twoPi * 100.0, seconds };
}
