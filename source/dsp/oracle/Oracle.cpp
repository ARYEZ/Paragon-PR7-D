#include "Oracle.h"

#include <algorithm>
#include <cmath>

namespace oracle
{
namespace
{
    // Below this a moment isn't kept at all (silence, a tail dying away).
    constexpr float quietestDb = -60.0f;
    // A phrase: what's heard with no silence longer than this inside it.
    // Hearing more of one finds all of its notes again.
    constexpr double phraseGap = 0.15;
    // The pitch detector judges mostly the older part of its window (YIN
    // compares each sample with the one a period later): a reading belongs
    // to this far into the window. Measured in the tests (onsets within ~10 ms).
    constexpr double readingPoint = 0.36;

    double median (std::vector<double> values)
    {
        if (values.empty())
            return 0.0;
        const auto middle = values.begin() + (std::ptrdiff_t) (values.size() / 2);
        std::nth_element (values.begin(), middle, values.end());
        return *middle;
    }

    bool sameTempo (const TempoPoint& a, const TempoPoint& b)
    {
        return std::abs (a.bpm - b.bpm) < 1.0e-3 && a.numerator == b.numerator && a.denominator == b.denominator;
    }
}

//==============================================================================
std::vector<Note> findNotes (const std::vector<Frame>& frames, const Settings& settings, double hop)
{
    const auto sensitivity = juce::jlimit (0.0f, 100.0f, settings.sensitivity) / 100.0;
    // A move to another note: the smoothed pitch this far (in semitones) from
    // where the note was, for 50 ms. Lower with more SENSITIVITY.
    const auto moveSemitones = juce::jmap (sensitivity, 1.0, 0.4);
    // A fresh attack on the same note: the level jumping this much (dB).
    const auto attackDb = juce::jmap (sensitivity, 14.0, 5.0);
    const auto minFrames = juce::jmax (2, juce::roundToInt (settings.minNoteMs / 1000.0 / hop));
    // Gaps up to this long (a consonant, a breath between syllables) don't end a note.
    const auto bridge = 0.035 + hop;

    std::vector<Note> found;

    // The voiced moments, in groups with no gap longer than `bridge`.
    std::vector<size_t> voiced;
    for (size_t i = 0; i < frames.size(); ++i)
        if (frames[i].voiced())
            voiced.push_back (i);

    size_t groupStart = 0;
    while (groupStart < voiced.size())
    {
        auto groupEnd = groupStart + 1;
        while (groupEnd < voiced.size() && frames[voiced[groupEnd]].seconds - frames[voiced[groupEnd - 1]].seconds <= bridge)
            ++groupEnd;

        const auto n = (int) (groupEnd - groupStart);
        std::vector<double> pitch ((size_t) n), level ((size_t) n), time ((size_t) n), smooth ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto& f = frames[voiced[groupStart + (size_t) i]];
            pitch[(size_t) i] = f.pitch;
            level[(size_t) i] = f.levelDb;
            time[(size_t) i] = f.seconds;
        }

        // A lone reading an octave (or more) away from its neighbours is
        // the detector slipping, not the singer: take the neighbours' pitch.
        {
            auto cleaned = pitch;
            for (int i = 0; i < n; ++i)
            {
                std::vector<double> around;
                for (int j = juce::jmax (0, i - 2); j <= juce::jmin (n - 1, i + 2); ++j)
                    around.push_back (pitch[(size_t) j]);
                const auto middle = median (around);
                if (std::abs (pitch[(size_t) i] - middle) > 6.0)
                    cleaned[(size_t) i] = middle;
            }
            pitch = cleaned;
        }

        // Smoothed over ~150 ms, which averages out vibrato (5 to 7 swings a second).
        const auto reach = juce::jmax (1, juce::roundToInt (0.075 / hop));
        for (int i = 0; i < n; ++i)
        {
            double sum = 0.0;
            int count = 0;
            for (int j = juce::jmax (0, i - reach); j <= juce::jmin (n - 1, i + reach); ++j, ++count)
                sum += pitch[(size_t) j];
            smooth[(size_t) i] = sum / count;
        }

        // Where the notes change.
        std::vector<std::pair<int, int>> segments;
        int start = 0, away = 0;
        const auto sustain = juce::jmax (2, juce::roundToInt (0.05 / hop));
        for (int i = 1; i < n; ++i)
        {
            // Where this note is: the middle of its last 300 ms.
            std::vector<double> recent;
            for (int j = juce::jmax (start, i - juce::roundToInt (0.3 / hop)); j < i; ++j)
                recent.push_back (pitch[(size_t) j]);
            const auto reference = median (recent);
            const auto longEnough = i - start >= minFrames;

            away = longEnough && std::abs (smooth[(size_t) i] - reference) > moveSemitones ? away + 1 : 0;
            int split = -1;
            if (away >= sustain)
            {
                // The move itself: the first reading past half way to the
                // threshold that stays there. The smoothed pitch sees a move
                // up to `reach` readings early, so look that far ahead too.
                split = i;
                for (int k = juce::jmax (start + 1, i - away - reach); k < juce::jmin (n - 1, i + reach); ++k)
                    if (std::abs (pitch[(size_t) k] - reference) > moveSemitones * 0.5
                        && std::abs (pitch[(size_t) k + 1] - reference) > moveSemitones * 0.5)
                    {
                        split = k;
                        break;
                    }
            }
            else if (longEnough)
            {
                // A fresh attack: the level jumping up from a dip in the last 60 ms.
                int quietest = i - 1;
                for (int j = juce::jmax (start, i - 6); j < i; ++j)
                    if (level[(size_t) j] < level[(size_t) quietest])
                        quietest = j;
                if (level[(size_t) i] - level[(size_t) quietest] >= attackDb && quietest + 1 - start >= minFrames)
                    split = quietest + 1;
            }

            if (split > start)
            {
                segments.emplace_back (start, split - 1);
                start = split;
                away = 0;
            }
        }
        segments.emplace_back (start, n - 1);

        for (auto [a, b] : segments)
        {
            double peak = -200.0;
            for (int i = a; i <= b; ++i)
                peak = std::max (peak, level[(size_t) i]);
            // The ends that are much quieter than the note: its tail, or the
            // next note's breath, not the note.
            while (b > a && level[(size_t) b] < peak - 24.0)
                --b;
            while (a < b && level[(size_t) a] < peak - 24.0)
                ++a;

            // Its pitch: the middle of what was sung, without the scoop in and the fall off the end.
            const auto length = b - a + 1;
            const auto skip = length >= 5 ? length / 5 : 0;
            std::vector<double> middle;
            for (int i = a + skip; i <= b - skip; ++i)
                middle.push_back (pitch[(size_t) i]);
            const auto sung = median (middle);

            Note note;
            note.start = time[(size_t) a] - hop * 0.5;
            note.end = time[(size_t) b] + hop * 0.5;
            note.pitch = juce::jlimit (0, 127, juce::roundToInt (sung));
            note.cents = (float) ((sung - note.pitch) * 100.0);
            // Velocity from its loudest 10 ms: -42 dBFS (very quiet) is 24, -6 dBFS is 120.
            note.velocity = juce::roundToInt (juce::jmap (juce::jlimit (-42.0, -6.0, peak), -42.0, -6.0, 24.0, 120.0));
            found.push_back (note);
        }

        groupStart = groupEnd;
    }

    // Notes too short to be notes: part of a neighbour (a quick wobble at
    // the join), or dropped (a click, a stray syllable).
    const auto minLength = settings.minNoteMs / 1000.0;
    for (size_t i = 0; i < found.size();)
    {
        if (found[i].length() >= minLength - hop * 0.25)
        {
            ++i;
            continue;
        }
        const auto touches = [&] (const Note& a, const Note& b) { return b.start - a.end <= bridge && std::abs (a.pitch - b.pitch) <= 2; };
        if (i > 0 && touches (found[i - 1], found[i]))
            found[i - 1].end = std::max (found[i - 1].end, found[i].end);
        else if (i + 1 < found.size() && touches (found[i], found[i + 1]))
            found[i + 1].start = found[i].start;
        found.erase (found.begin() + (std::ptrdiff_t) i);
    }
    return found;
}

//==============================================================================
const TempoPoint& TempoMap::pointAt (double seconds) const noexcept
{
    auto at = std::upper_bound (points.begin(), points.end(), seconds, [] (double s, const TempoPoint& p) { return s < p.seconds; });
    return at == points.begin() ? points.front() : *(at - 1);
}

double TempoMap::ppqAt (double seconds) const noexcept
{
    const auto& p = pointAt (seconds);
    return p.ppq + (seconds - p.seconds) * p.bpm / 60.0;
}

double TempoMap::secondsAt (double ppq) const noexcept
{
    const TempoPoint* best = &points.front();
    for (const auto& p : points)
        if (p.ppq <= ppq)
            best = &p;
    return best->seconds + (ppq - best->ppq) * 60.0 / best->bpm;
}

bool TempoMap::note (const TempoPoint& point)
{
    if (! known)
    {
        points.assign (1, point);
        known = true;
        return true;
    }
    if (std::abs (ppqAt (point.seconds) - point.ppq) < 0.01 && sameTempo (pointAt (point.seconds), point))
        return false;
    points.erase (std::remove_if (points.begin(), points.end(), [&] (const TempoPoint& p) { return std::abs (p.seconds - point.seconds) < 1.0e-6; }),
                  points.end());
    points.insert (std::upper_bound (points.begin(), points.end(), point.seconds, [] (double s, const TempoPoint& p) { return s < p.seconds; }),
                   point);
    return true;
}

double quantizeQuarters (int index) noexcept
{
    switch (index)
    {
        case 1: return 0.125;
        case 2: return 0.25;
        case 3: return 1.0 / 6.0;
        case 4: return 0.5;
        case 5: return 1.0 / 3.0;
        case 6: return 1.0;
        default: return 0.0;
    }
}

juce::MidiFile toMidi (const std::vector<Note>& notes, const TempoMap& tempo, int quantize)
{
    constexpr int ticksPerQuarter = 960;
    const auto grid = quantizeQuarters (quantize);
    const auto snap = [grid] (double quarters) { return grid > 0.0 ? std::round (quarters / grid) * grid : quarters; };

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "ORACLE"), 0.0);

    // The tempo and time signature: from the start, then wherever they change.
    const TempoPoint* previous = nullptr;
    for (const auto& p : tempo.getPoints())
    {
        const auto tick = previous == nullptr ? 0.0 : std::round (juce::jmax (0.0, p.ppq) * ticksPerQuarter);
        if (previous == nullptr || std::abs (previous->bpm - p.bpm) > 1.0e-3)
            track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / p.bpm)), tick);
        if (previous == nullptr || previous->numerator != p.numerator || previous->denominator != p.denominator)
            track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (p.numerator, p.denominator), tick);
        previous = &p;
    }

    // The notes in quarter notes from bar 1, quantized, one at a time.
    std::vector<std::tuple<double, double, const Note*>> placed;
    for (const auto& note : notes)
    {
        auto start = snap (tempo.ppqAt (note.start));
        auto end = snap (tempo.ppqAt (note.end));
        if (grid > 0.0 && end <= start)
            end = start + grid;
        if (end <= 0.0 || end <= start)
            continue;
        placed.emplace_back (juce::jmax (0.0, start), end, &note);
    }
    std::sort (placed.begin(), placed.end(), [] (const auto& a, const auto& b) { return std::get<0> (a) < std::get<0> (b); });
    for (size_t i = 0; i < placed.size(); ++i)
    {
        auto [start, end, note] = placed[i];
        if (i + 1 < placed.size())
            end = std::min (end, std::get<0> (placed[i + 1]));
        const auto on = std::round (start * ticksPerQuarter), off = std::round (end * ticksPerQuarter);
        if (off <= on)
            continue;
        track.addEvent (juce::MidiMessage::noteOn (1, note->pitch, (juce::uint8) juce::jlimit (1, 127, note->velocity)), on);
        track.addEvent (juce::MidiMessage::noteOff (1, note->pitch), off);
    }
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarter);
    file.addTrack (track);
    return file;
}

//==============================================================================
bool Timeline::differs (const TimelineMark& mark) const noexcept
{
    if (! any || mark.known != last.known || mark.playing != last.playing)
        return true;
    if (! mark.known || ! mark.playing)
        return false;
    if (std::abs (mark.bpm - last.bpm) > 1.0e-3 || mark.numerator != last.numerator || mark.denominator != last.denominator)
        return true;
    // Still running on from the last mark, or has the song position jumped (a loop, a click on the timeline)?
    const auto expected = last.hostSample + (mark.tapIndex - last.tapIndex);
    return std::abs (mark.hostSample - expected) > 2;
}

void Timeline::write (const TimelineMark& mark) noexcept
{
    if (! differs (mark))
        return;
    const auto scope = fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return;   // full: try again with the next block
    marks[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = mark;
    last = mark;
    any = true;
}

int Timeline::read (TimelineMark* destination, int maximum) noexcept
{
    const auto scope = fifo.read (juce::jmin (maximum, fifo.getNumReady()));
    for (int i = 0; i < scope.blockSize1; ++i)
        destination[i] = marks[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        destination[scope.blockSize1 + i] = marks[(size_t) (scope.startIndex2 + i)];
    return scope.blockSize1 + scope.blockSize2;
}

//==============================================================================
void Transcriber::prepare (double sampleRate)
{
    detector.prepare (sampleRate);
    window.assign ((size_t) detector.windowSizeAtHostRate(), 0.0f);
    hop = juce::jmax ((std::int64_t) 1, (std::int64_t) std::llround (sampleRate * hopSeconds));
    preparedRate = sampleRate;
    nextCentre = -1;
    endRun();
}

void Transcriber::update (const InputTap& tap, Timeline& timeline, bool listening)
{
    const juce::ScopedLock scope (lock);
    const auto sampleRate = tap.getSampleRate();
    if (sampleRate <= 0.0)
        return;
    if (! juce::exactlyEqual (sampleRate, preparedRate))
        prepare (sampleRate);

    // The total first: every mark for the samples it covers is in by then.
    const auto total = tap.getTotalWritten();
    TimelineMark incoming[64];
    for (int count; (count = timeline.read (incoming, 64)) > 0;)
        pending.insert (pending.end(), incoming, incoming + count);

    const auto size = (std::int64_t) window.size();
    const auto before = (std::int64_t) std::llround ((double) size * readingPoint);
    const auto applyMarks = [&] (std::int64_t upTo)
    {
        bool applied = false;
        while (! pending.empty() && pending.front().tapIndex <= upTo)
        {
            current = pending.front();
            pending.pop_front();
            applied = true;
        }
        return applied;
    };

    if (! listening)
    {
        applyMarks (total);
        nextCentre = -1;
        endRun();
        hearing = false;
        return;
    }

    // Starting, or fallen too far behind (the computer was busy): from now.
    if (nextCentre < 0 || nextCentre - before < total - tap.capacity() + 8192)
    {
        nextCentre = total - (size - before);
        endRun();
    }

    double changedFrom = 1.0e12, changedTo = -1.0e12;
    while (nextCentre + (size - before) <= total)
    {
        if (applyMarks (nextCentre) && current.known && current.playing)
            tempo.note ({ (double) current.hostSample / sampleRate, current.ppq, current.bpm, current.numerator, current.denominator });

        const auto playing = ! current.known || current.playing;
        if (! playing)
        {
            endRun();
            nextCentre += hop;
            continue;
        }

        if (! tap.readAt (nextCentre - before, window.data(), (int) size))
        {
            nextCentre = total - (size - before);
            endRun();
            break;
        }

        const auto seconds = current.known ? (double) (current.hostSample + (nextCentre - current.tapIndex)) / sampleRate
                                           : (double) nextCentre / sampleRate;
        Frame frame;
        frame.seconds = seconds;
        // The level over 10 ms, or a whole cycle of a low note (a bass's
        // 41 Hz takes 24 ms): shorter, and it would rise and fall within
        // each cycle, which would look like fresh attacks.
        const auto levelOver = [&] (double halfSeconds)
        {
            const auto reach = juce::jlimit ((std::int64_t) 1, before, (std::int64_t) std::llround (sampleRate * halfSeconds));
            double sum = 0.0;
            for (auto i = before - reach; i < before + reach; ++i)
                sum += (double) window[(size_t) i] * window[(size_t) i];
            return (float) juce::Decibels::gainToDecibels (std::sqrt (sum / (double) (2 * reach)), -100.0);
        };
        frame.levelDb = levelOver (0.005);
        if (frame.levelDb > quietestDb)
            if (const auto result = detector.detect (window.data(), (int) size))
            {
                frame.pitch = (float) (69.0 + 12.0 * std::log2 (result->frequency / 440.0));
                if (result->frequency < 100.0)
                    frame.levelDb = levelOver (0.5 / result->frequency);
            }

        // Running on from the last moment, or a new pass (play pressed, a loop, a jump)?
        const auto continuing = runActive && std::abs (seconds - (runLast + hopSeconds)) < hopSeconds * 0.5;
        if (! continuing)
            pushUndo();   // a new take: one undo takes its notes back
        const auto from = continuing ? runLast + 1.0e-9 : seconds - hopSeconds * 0.5;
        const auto to = seconds + hopSeconds * 0.5;

        // What this replaces: anything heard there before.
        const auto first = std::lower_bound (frames.begin(), frames.end(), from, [] (const Frame& f, double s) { return f.seconds < s; });
        const auto last = std::lower_bound (first, frames.end(), to, [] (const Frame& f, double s) { return f.seconds < s; });
        const auto at = frames.erase (first, last);
        if (frame.levelDb > quietestDb)
            frames.insert (at, frame);

        runActive = true;
        runLast = nowSeconds = seconds;
        changedFrom = std::min (changedFrom, from);
        changedTo = std::max (changedTo, to);
        nextCentre += hop;
    }

    hearing = ! current.known || current.playing;
    if (changedTo >= changedFrom)
        findNotesAround (changedFrom, changedTo);
}

void Transcriber::findNotesAround (double from, double to)
{
    // Out to the ends of the phrases it touches, so their notes are found whole.
    auto first = std::lower_bound (frames.begin(), frames.end(), from, [] (const Frame& f, double s) { return f.seconds < s; });
    while (first != frames.begin() && (first == frames.end() ? from : first->seconds) - (first - 1)->seconds < phraseGap)
        --first;
    auto last = std::lower_bound (first, frames.end(), to, [] (const Frame& f, double s) { return f.seconds < s; });
    while (last != frames.end() && last->seconds - (last == frames.begin() ? to : std::max (to, (last - 1)->seconds)) < phraseGap)
        ++last;

    // A long phrase (a line sung without a breath) needn't be read again
    // from its start each time: what decides a note looks at most 300 ms
    // around it, so notes that ended over a second before what's new are
    // settled. Start (and stop) at the edge of one of them instead.
    constexpr double settled = 1.0;
    const auto byTime = [] (const Frame& f, double s) { return f.seconds < s; };
    if (first != frames.end() && from - first->seconds > settled * 2.0)
        for (auto n = notes.rbegin(); n != notes.rend(); ++n)
            if (n->end < from - settled && n->start > first->seconds)
            {
                first = std::lower_bound (first, frames.end(), n->start + hopSeconds * 0.25, byTime);
                break;
            }
    if (last != frames.begin() && (last - 1)->seconds - to > settled * 2.0)
        for (const auto& n : notes)
            if (n.start > to + settled && n.start < (last - 1)->seconds)
            {
                last = std::lower_bound (first, last, n.start - hopSeconds * 0.25, byTime);
                break;
            }
    // The notes to find again: those in these phrases, and those in what
    // was just heard again (replaced, perhaps by silence). A note that only
    // touches its edge (its last reading just before it) stays.
    // (Their edges are half a reading out from the first and last readings;
    // a neighbouring note that ends or starts right there, legato, stays.)
    const auto phraseFrom = first != last ? first->seconds - hopSeconds * 0.4 : 1.0e12;
    const auto phraseTo = first != last ? (last - 1)->seconds + hopSeconds * 0.4 : -1.0e12;
    const auto margin = hopSeconds * 0.6;

    const std::vector<Frame> phrase (first, last);
    auto found = findNotes (phrase, settings);
    notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n)
                 {
                     return (n.end > phraseFrom && n.start < phraseTo) || (n.end > from + margin && n.start < to - margin);
                 }),
                 notes.end());
    for (auto& note : found)
        note.id = nextId();
    notes.insert (notes.end(), found.begin(), found.end());
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    changed();
}

void Transcriber::setSettings (const Settings& newSettings)
{
    const juce::ScopedLock scope (lock);
    pushUndo();
    settings = newSettings;
    notes = findNotes (frames, settings);
    for (auto& note : notes)
        note.id = nextId();
    changed();
}

//==============================================================================
void Transcriber::pushUndo()
{
    undoStack.push_back (notes);
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void Transcriber::beginEdit()
{
    const juce::ScopedLock scope (lock);
    pushUndo();
}

void Transcriber::setNotes (std::vector<Note> newNotes, bool undoable)
{
    const juce::ScopedLock scope (lock);
    if (undoable)
        pushUndo();
    for (auto& note : newNotes)
    {
        if (note.id == 0)
            note.id = nextId();
        note.pitch = juce::jlimit (0, 127, note.pitch);
    }
    std::sort (newNotes.begin(), newNotes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    notes = std::move (newNotes);
    changed();
}

const Note* Transcriber::find (juce::uint32 id) const noexcept
{
    for (const auto& note : notes)
        if (note.id == id)
            return &note;
    return nullptr;
}

void Transcriber::transpose (const std::vector<juce::uint32>& ids, int semitones)
{
    auto edited = notes;
    for (auto& note : edited)
        if (std::find (ids.begin(), ids.end(), note.id) != ids.end())
            note.pitch += semitones;
    setNotes (std::move (edited));
}

void Transcriber::remove (const std::vector<juce::uint32>& ids)
{
    auto edited = notes;
    edited.erase (std::remove_if (edited.begin(), edited.end(), [&] (const Note& n) { return std::find (ids.begin(), ids.end(), n.id) != ids.end(); }),
                  edited.end());
    setNotes (std::move (edited));
}

juce::uint32 Transcriber::split (juce::uint32 id, double seconds)
{
    const auto* note = find (id);
    if (note == nullptr || seconds < note->start + 0.02 || seconds > note->end - 0.02)
        return 0;
    auto edited = notes;
    auto right = *note;
    right.start = seconds;
    right.id = nextId();
    for (auto& n : edited)
        if (n.id == id)
            n.end = seconds;
    edited.push_back (right);
    setNotes (std::move (edited));
    return right.id;
}

void Transcriber::join (std::vector<juce::uint32> ids)
{
    std::vector<const Note*> chosen;
    for (const auto& note : notes)
        if (std::find (ids.begin(), ids.end(), note.id) != ids.end())
            chosen.push_back (&note);
    if (chosen.size() == 1)
    {
        const auto at = std::find_if (notes.begin(), notes.end(), [&] (const Note& n) { return &n == chosen.front(); });
        if (at + 1 == notes.end())
            return;
        chosen.push_back (&*(at + 1));
    }
    if (chosen.size() < 2)
        return;

    auto joined = *chosen.front();
    std::vector<juce::uint32> gone;
    for (const auto* note : chosen)
    {
        joined.end = std::max (joined.end, note->end);
        joined.velocity = std::max (joined.velocity, note->velocity);
        if (note != chosen.front())
            gone.push_back (note->id);
    }
    auto edited = notes;
    edited.erase (std::remove_if (edited.begin(), edited.end(), [&] (const Note& n) { return std::find (gone.begin(), gone.end(), n.id) != gone.end(); }),
                  edited.end());
    for (auto& n : edited)
        if (n.id == joined.id)
            n = joined;
    setNotes (std::move (edited));
}

juce::uint32 Transcriber::add (double start, double end, int pitch)
{
    if (end <= start)
        return 0;
    auto edited = notes;
    Note note;
    note.start = start;
    note.end = end;
    note.pitch = pitch;
    note.id = nextId();
    edited.push_back (note);
    setNotes (std::move (edited));
    return note.id;
}

void Transcriber::undo()
{
    const juce::ScopedLock scope (lock);
    if (undoStack.empty())
        return;
    redoStack.push_back (notes);
    notes = undoStack.back();
    undoStack.pop_back();
    changed();
}

void Transcriber::redo()
{
    const juce::ScopedLock scope (lock);
    if (redoStack.empty())
        return;
    undoStack.push_back (notes);
    notes = redoStack.back();
    redoStack.pop_back();
    changed();
}

void Transcriber::clear()
{
    const juce::ScopedLock scope (lock);
    pushUndo();
    notes.clear();
    frames.clear();
    endRun();
    changed();
}

juce::File Transcriber::writeMidiFile (const juce::File& folder) const
{
    if (! folder.createDirectory())
        return {};
    const auto file = folder.getChildFile ("ORACLE " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".mid")
                          .getNonexistentSibling (false);
    juce::FileOutputStream stream (file);
    if (! stream.openedOk() || ! toMidi().writeTo (stream))
        return {};
    return file;
}

//==============================================================================
juce::ValueTree Transcriber::toValueTree() const
{
    const juce::ScopedLock scope (lock);
    juce::ValueTree tree ("Oracle");
    tree.setProperty ("version", 1, nullptr);
    tree.setProperty ("sensitivity", settings.sensitivity, nullptr);
    tree.setProperty ("minNoteMs", settings.minNoteMs, nullptr);
    tree.setProperty ("quantize", quantize, nullptr);

    juce::MemoryOutputStream f, n, t;
    for (const auto& frame : frames)
    {
        f.writeDouble (frame.seconds);
        f.writeFloat (frame.pitch);
        f.writeFloat (frame.levelDb);
    }
    for (const auto& note : notes)
    {
        n.writeDouble (note.start);
        n.writeDouble (note.end);
        n.writeInt (note.pitch);
        n.writeInt (note.velocity);
        n.writeFloat (note.cents);
    }
    for (const auto& p : tempo.getPoints())
    {
        t.writeDouble (p.seconds);
        t.writeDouble (p.ppq);
        t.writeDouble (p.bpm);
        t.writeInt (p.numerator);
        t.writeInt (p.denominator);
    }
    tree.setProperty ("frames", f.getMemoryBlock().toBase64Encoding(), nullptr);
    tree.setProperty ("notes", n.getMemoryBlock().toBase64Encoding(), nullptr);
    tree.setProperty ("tempo", t.getMemoryBlock().toBase64Encoding(), nullptr);
    return tree;
}

void Transcriber::fromValueTree (const juce::ValueTree& tree)
{
    const juce::ScopedLock scope (lock);
    frames.clear();
    notes.clear();
    tempo.reset();
    undoStack.clear();
    redoStack.clear();
    endRun();
    if (tree.isValid() && tree.hasType ("Oracle"))
    {
        settings.sensitivity = juce::jlimit (0.0f, 100.0f, (float) tree.getProperty ("sensitivity", 50.0f));
        settings.minNoteMs = juce::jlimit (20.0f, 500.0f, (float) tree.getProperty ("minNoteMs", 80.0f));
        setQuantize ((int) tree.getProperty ("quantize", 0));

        const auto block = [&] (const char* name)
        {
            juce::MemoryBlock data;
            data.fromBase64Encoding (tree[name].toString());
            return data;
        };
        {
            juce::MemoryInputStream in (block ("frames"), true);
            while (in.getNumBytesRemaining() >= 16)
            {
                Frame frame;
                frame.seconds = in.readDouble();
                frame.pitch = in.readFloat();
                frame.levelDb = in.readFloat();
                frames.push_back (frame);
            }
        }
        {
            juce::MemoryInputStream in (block ("notes"), true);
            while (in.getNumBytesRemaining() >= 28)
            {
                Note note;
                note.start = in.readDouble();
                note.end = in.readDouble();
                note.pitch = juce::jlimit (0, 127, in.readInt());
                note.velocity = juce::jlimit (1, 127, in.readInt());
                note.cents = in.readFloat();
                note.id = nextId();
                notes.push_back (note);
            }
        }
        {
            juce::MemoryInputStream in (block ("tempo"), true);
            while (in.getNumBytesRemaining() >= 32)
            {
                TempoPoint p;
                p.seconds = in.readDouble();
                p.ppq = in.readDouble();
                p.bpm = juce::jlimit (10.0, 999.0, in.readDouble());
                p.numerator = juce::jlimit (1, 32, in.readInt());
                p.denominator = juce::jlimit (1, 32, in.readInt());
                tempo.note (p);
            }
        }
    }
    changed();
}
}
