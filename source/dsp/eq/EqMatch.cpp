#include "EqMatch.h"

namespace eq::match
{
    namespace
    {
        constexpr double silenceDb = -70.0;     // frames quieter than this don't count
        constexpr double ignoreBelowDb = 60.0;  // a spectrum's points this far under its loudest are "nothing there"

        size_t nearest (double hz)
        {
            const auto& points = grid();
            const auto index = std::round (12.0 * std::log2 (std::max (hz, 1.0) / points.front()));
            return (size_t) juce::jlimit (0.0, (double) points.size() - 1.0, index);
        }

        double interpolate (const std::vector<double>& frequencies, const std::vector<double>& values, double hz)
        {
            if (hz <= frequencies.front())
                return values.front();
            if (hz >= frequencies.back())
                return values.back();
            const auto above = (size_t) std::distance (frequencies.begin(), std::upper_bound (frequencies.begin(), frequencies.end(), hz));
            const auto below = above - 1;
            const auto t = std::log (hz / frequencies[below]) / std::log (frequencies[above] / frequencies[below]);
            return values[below] + (values[above] - values[below]) * t;
        }
    }

    const std::vector<double>& grid()
    {
        static const auto points = []
        {
            std::vector<double> result;
            for (int k = 0; 20.0 * std::pow (2.0, k / 12.0) <= 20000.0 * 1.0001; ++k)
                result.push_back (20.0 * std::pow (2.0, k / 12.0));
            return result;
        }();
        return points;
    }

    //==========================================================================
    std::unique_ptr<juce::XmlElement> Spectrum::toXml (const juce::String& name) const
    {
        auto xml = std::make_unique<juce::XmlElement> ("MatchReference");
        xml->setAttribute ("version", 1);
        xml->setAttribute ("name", name);
        xml->setAttribute ("seconds", seconds);
        for (size_t i = 0; i < db.size() && i < grid().size(); ++i)
        {
            auto* point = xml->createNewChildElement ("P");
            point->setAttribute ("hz", grid()[i]);
            point->setAttribute ("db", db[i]);
        }
        return xml;
    }

    std::optional<Spectrum> Spectrum::fromXml (const juce::XmlElement& xml)
    {
        if (! xml.hasTagName ("MatchReference"))
            return std::nullopt;
        std::vector<double> frequencies, values;
        for (const auto* point : xml.getChildWithTagNameIterator ("P"))
        {
            const auto hz = point->getDoubleAttribute ("hz");
            if (hz > 0.0 && (frequencies.empty() || hz > frequencies.back()))
            {
                frequencies.push_back (hz);
                values.push_back (point->getDoubleAttribute ("db"));
            }
        }
        if (frequencies.size() < 2)
            return std::nullopt;
        // Kept as frequency and dB pairs, so a reference still loads if the grid changes.
        Spectrum spectrum;
        spectrum.seconds = xml.getDoubleAttribute ("seconds");
        for (const auto hz : grid())
            spectrum.db.push_back (interpolate (frequencies, values, hz));
        return spectrum;
    }

    //==========================================================================
    void Analyser::reset()
    {
        pending.clear();
        std::fill (power.begin(), power.end(), 0.0);
        frames = 0.0;
    }

    void Analyser::add (const float* samples, int numSamples, double sampleRate)
    {
        if (sampleRate <= 0.0 || numSamples <= 0)
            return;
        if (! juce::approximatelyEqual (sampleRate, rate))
        {
            reset();   // a spectrum is only comparable at one sample rate
            rate = sampleRate;
        }
        pending.insert (pending.end(), samples, samples + numSamples);

        size_t start = 0;
        while (pending.size() - start >= (size_t) size)
        {
            const auto* frame = pending.data() + start;
            double sum = 0.0;
            for (int i = 0; i < size; ++i)
                sum += (double) frame[i] * frame[i];
            const auto rmsDb = 10.0 * std::log10 (sum / size + 1.0e-30);
            if (rmsDb > silenceDb)
            {
                std::copy (frame, frame + size, work.begin());
                std::fill (work.begin() + size, work.end(), 0.0f);
                window.multiplyWithWindowingTable (work.data(), (size_t) size);
                fft.performFrequencyOnlyForwardTransform (work.data());
                for (size_t bin = 0; bin < power.size(); ++bin)
                    power[bin] += (double) work[bin] * work[bin];
                frames += 1.0;
            }
            start += (size_t) hop;
        }
        pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) start);
    }

    Spectrum Analyser::spectrum() const
    {
        Spectrum result;
        result.seconds = seconds();
        if (frames <= 0.0)
            return result;
        // Each point: the average power of the bins within a 24th of an octave of it.
        const auto binOf = [this] (double hz) { return hz * size / rate; };
        const auto last = (double) power.size() - 1.0;
        for (const auto hz : grid())
        {
            const auto from = binOf (hz * std::pow (2.0, -1.0 / 24.0)), to = binOf (hz * std::pow (2.0, 1.0 / 24.0));
            double sum = 0.0;
            int count = 0;
            for (auto bin = (int) std::ceil (from); bin <= (int) std::floor (to) && bin <= (int) last; ++bin, ++count)
                sum += power[(size_t) bin];
            if (count == 0)   // narrower than a bin (the lows): between the two nearest
            {
                const auto at = juce::jlimit (1.0, last, binOf (hz));
                const auto below = (size_t) std::floor (at), above = std::min (below + 1, (size_t) last);
                sum = power[below] + (power[above] - power[below]) * (at - (double) below);
                count = 1;
            }
            result.db.push_back (10.0 * std::log10 (sum / count / frames + 1.0e-30));
        }
        return result;
    }

    std::optional<Spectrum> analyseFile (const juce::File& file, const std::function<bool()>& shouldStop)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return std::nullopt;

        Analyser analyser;
        constexpr int chunk = 1 << 16;
        juce::AudioBuffer<float> buffer ((int) juce::jmax (1u, reader->numChannels), chunk);
        std::vector<float> mono ((size_t) chunk);
        for (juce::int64 position = 0; position < reader->lengthInSamples; position += chunk)
        {
            if (shouldStop && shouldStop())
                return std::nullopt;
            const auto count = (int) std::min<juce::int64> (chunk, reader->lengthInSamples - position);
            reader->read (&buffer, 0, count, position, true, true);
            std::fill (mono.begin(), mono.end(), 0.0f);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::addWithMultiply (mono.data(), buffer.getReadPointer (ch), 1.0f / (float) buffer.getNumChannels(), count);
            analyser.add (mono.data(), count, reader->sampleRate);
        }
        return analyser.spectrum();
    }

    //==========================================================================
    std::vector<double> target (const Spectrum& track, const Spectrum& reference, const std::vector<double>& existingDb, double detail)
    {
        const auto& points = grid();
        std::vector<double> result (points.size(), 0.0);
        if (! track.isValid() || ! reference.isValid())
            return result;

        // Only where both have something to compare.
        const auto loudestTrack = *std::max_element (track.db.begin(), track.db.end());
        const auto loudestReference = *std::max_element (reference.db.begin(), reference.db.end());
        std::vector<bool> counts (points.size());
        for (size_t i = 0; i < points.size(); ++i)
            counts[i] = track.db[i] > loudestTrack - ignoreBelowDb && reference.db[i] > loudestReference - ignoreBelowDb;

        std::vector<double> raw (points.size(), 0.0);
        for (size_t i = 0; i < points.size(); ++i)
            raw[i] = reference.db[i] - track.db[i] - (existingDb.size() == points.size() ? existingDb[i] : 0.0);

        // Tone, not loudness: take out the average difference over 100 Hz to 10 kHz.
        double sum = 0.0;
        int count = 0;
        for (size_t i = 0; i < points.size(); ++i)
            if (counts[i] && points[i] >= 100.0 && points[i] <= 10000.0)
            {
                sum += raw[i];
                ++count;
            }
        const auto level = count > 0 ? sum / count : 0.0;
        for (size_t i = 0; i < points.size(); ++i)
            raw[i] = counts[i] ? raw[i] - level : 0.0;

        // Smoothed over an octave (DETAIL 0) to a sixth of an octave (1), then limited.
        const auto width = juce::jmap (juce::jlimit (0.0, 1.0, detail), 1.0, 1.0 / 6.0);
        const auto half = std::max (0, (int) std::round (width * 12.0 / 2.0));
        for (int i = 0; i < (int) points.size(); ++i)
        {
            double total = 0.0;
            int n = 0;
            for (int j = std::max (0, i - half); j <= std::min ((int) points.size() - 1, i + half); ++j, ++n)
                total += raw[(size_t) j];
            result[(size_t) i] = juce::jlimit (-maxTargetDb, maxTargetDb, total / std::max (n, 1));
        }
        return result;
    }

    std::vector<double> curveOnGrid (const std::vector<Band>& bands, double sampleRate)
    {
        std::vector<double> curve (grid().size(), 0.0);
        for (const auto& band : bands)
        {
            const auto designed = design (band, sampleRate);
            for (size_t i = 0; i < curve.size(); ++i)
                curve[i] += responseDb (designed, grid()[i], sampleRate);
        }
        return curve;
    }

    std::vector<Band> fit (const std::vector<double>& targetDb, const Options& options)
    {
        const auto& points = grid();
        std::vector<Band> bands;
        if (targetDb.size() != points.size() || options.maxBands <= 0)
            return bands;

        const auto detail = juce::jlimit (0.0, 1.0, options.detail);
        const auto amount = juce::jlimit (0.0, 1.0, options.amount);
        const auto tolerance = juce::jmap (detail, 1.0, 0.25);   // dB left over that's fine
        const auto maxBands = std::min (options.maxBands, (int) std::round (juce::jmap (detail, 4.0, 16.0)));
        std::vector<double> wanted (points.size());
        for (size_t i = 0; i < points.size(); ++i)
            wanted[i] = targetDb[i] * amount;

        const auto residual = [&]
        {
            const auto curve = curveOnGrid (bands, options.sampleRate);
            std::vector<double> r (points.size());
            for (size_t i = 0; i < points.size(); ++i)
                r[i] = wanted[i] - curve[i];
            return r;
        };
        const auto error = [] (const std::vector<double>& r)
        {
            double sum = 0.0;
            for (const auto value : r)
                sum += value * value;
            return std::sqrt (sum / (double) r.size());
        };
        // Where to read the leftover difference to re-balance a band: its
        // frequency, or for a shelf, well into the part it lifts or cuts.
        const auto probe = [] (const Band& band)
        {
            if (band.shape == Shape::lowShelf)
                return nearest (std::max (20.0, band.frequency / 3.0));
            if (band.shape == Shape::highShelf)
                return nearest (std::min (20000.0, band.frequency * 3.0));
            return nearest (band.frequency);
        };

        auto r = residual();
        auto before = error (r);
        while ((int) bands.size() < maxBands)
        {
            // The biggest difference left, and how wide it is (to half its height).
            size_t peak = 0;
            for (size_t i = 1; i < r.size(); ++i)
                if (std::abs (r[i]) > std::abs (r[peak]))
                    peak = i;
            const auto gain = r[peak];
            if (std::abs (gain) < tolerance)
                break;
            const auto sameSide = [&] (size_t i) { return r[i] * gain > 0.0 && std::abs (r[i]) > 0.5 * std::abs (gain); };
            auto low = peak, high = peak;
            while (low > 0 && sameSide (low - 1))
                --low;
            while (high + 1 < r.size() && sameSide (high + 1))
                ++high;

            Band band;
            band.used = true;
            band.gainDb = juce::jlimit (-maxGainDb, maxGainDb, gain);
            band.slope = defaultSlope;
            if (low == 0 && points[peak] < 200.0)
            {
                band.shape = Shape::lowShelf;   // runs off the bottom: a shelf, its corner where it's fallen by half
                band.frequency = std::sqrt (points[high] * points[std::min (high + 1, points.size() - 1)]);
                band.q = 0.7;
            }
            else if (high == points.size() - 1 && points[peak] > 4000.0)
            {
                band.shape = Shape::highShelf;
                band.frequency = std::sqrt (points[low] * points[low == 0 ? 0 : low - 1]);
                band.q = 0.7;
            }
            else
            {
                // A bell whose half-height width is the bump's: Q from the bandwidth in octaves.
                const auto octaves = std::max (1.0 / 6.0, std::log2 (points[high] / points[low]) + 1.0 / 12.0);
                const auto ratio = std::pow (2.0, octaves);
                band.shape = Shape::bell;
                band.frequency = points[peak];
                band.q = juce::jlimit (0.3, 6.0, std::sqrt (ratio) / (ratio - 1.0));
            }
            bands.push_back (band);

            // Re-balance every band's gain against what's left, a few times over.
            for (int pass = 0; pass < 6; ++pass)
            {
                r = residual();
                for (auto& b : bands)
                    b.gainDb = juce::jlimit (-maxGainDb, maxGainDb, b.gainDb + 0.7 * r[probe (b)]);
            }
            r = residual();
            const auto after = error (r);
            if (after > before * 0.98)   // it didn't help: stop here
            {
                bands.pop_back();
                r = residual();
                break;
            }
            before = after;
        }

        // Bands that ended up doing next to nothing aren't worth a slot.
        bands.erase (std::remove_if (bands.begin(), bands.end(), [] (const Band& b) { return std::abs (b.gainDb) < 0.2; }), bands.end());
        return bands;
    }
}
