#include "Wavetable.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <complex>
#include <cstring>
#include <mutex>

namespace synth
{
    namespace
    {
        using Complex = std::complex<float>;
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        int log2Of (int powerOfTwo) noexcept
        {
            int order = 0;
            while ((1 << order) < powerOfTwo)
                ++order;
            return order;
        }

        // One FFT of each size, made once (building tables only, never the audio thread).
        const juce::dsp::FFT& fftOfSize (int size)
        {
            static std::mutex lock;
            static std::array<std::unique_ptr<juce::dsp::FFT>, 16> ffts;
            const std::scoped_lock scoped (lock);
            auto& fft = ffts[(size_t) log2Of (size)];
            if (fft == nullptr)
                fft = std::make_unique<juce::dsp::FFT> (log2Of (size));
            return *fft;
        }

        // A frame's harmonics 1 to `count`, each as its amplitude and phase (a
        // complex number: x(t) = sum of |c| cos (h t + arg c)).
        std::vector<Complex> harmonicsOf (const float* frame, int size, int count)
        {
            std::vector<Complex> result ((size_t) count + 1);
            if (juce::isPowerOfTwo (size))
            {
                std::vector<Complex> in ((size_t) size), out ((size_t) size);
                for (int i = 0; i < size; ++i)
                    in[(size_t) i] = frame[i];
                fftOfSize (size).perform (in.data(), out.data(), false);
                for (int h = 1; h <= count; ++h)
                    result[(size_t) h] = out[(size_t) h] * (2.0f / (float) size);
                return result;
            }
            // Any other length (a single cycle from a file): worked out directly.
            std::vector<double> cosines ((size_t) size), sines ((size_t) size);
            for (int i = 0; i < size; ++i)
            {
                cosines[(size_t) i] = std::cos (twoPi * i / size);
                sines[(size_t) i] = std::sin (twoPi * i / size);
            }
            for (int h = 1; h <= count; ++h)
            {
                double re = 0.0, im = 0.0;
                for (int i = 0; i < size; ++i)
                {
                    const auto at = (size_t) (((std::int64_t) h * i) % size);
                    re += frame[i] * cosines[at];
                    im -= frame[i] * sines[at];
                }
                result[(size_t) h] = { (float) (re * 2.0 / size), (float) (im * 2.0 / size) };
            }
            return result;
        }

        // One cycle of `size` samples from harmonics 1 to `count` (the rest left out).
        void synthesise (const std::vector<Complex>& harmonics, int count, float* out, int size)
        {
            std::vector<Complex> spectrum ((size_t) size), result ((size_t) size);
            count = juce::jmin (count, (int) harmonics.size() - 1, size / 2 - 1);
            for (int h = 1; h <= count; ++h)
            {
                const auto c = harmonics[(size_t) h] * (0.5f * (float) size);
                spectrum[(size_t) h] = c;
                spectrum[(size_t) (size - h)] = std::conj (c);
            }
            fftOfSize (size).perform (spectrum.data(), result.data(), true);   // scaled by 1 / size
            for (int i = 0; i < size; ++i)
                out[i] = result[(size_t) i].real();
        }
    }

    int Wavetable::levelSize (int level) noexcept
    {
        return juce::jmax (2048, (int) juce::nextPowerOfTwo (8 * tables::harmonicsAt (level)));
    }

    std::shared_ptr<const Wavetable> Wavetable::build (const WavetableSource& from)
    {
        return build (from, nullptr);
    }

    std::vector<float> Wavetable::harmonicLevels (const float* cycle, int size, int count)
    {
        count = juce::jmin (count, size / 2 - 1);
        const auto harmonics = harmonicsOf (cycle, size, count);
        std::vector<float> levels ((size_t) count + 1);
        for (int h = 1; h <= count; ++h)
            levels[(size_t) h] = std::abs (harmonics[(size_t) h]);
        return levels;
    }

    std::shared_ptr<const Wavetable> Wavetable::build (const WavetableSource& from, const std::function<float (int harmonic)>& gain, bool normalise)
    {
        if (! from.isValid() || from.frameSize < minFrameSize || from.frameSize > maxFrameSize)
            return nullptr;

        std::shared_ptr<Wavetable> table (new Wavetable());
        table->source = from;
        const auto available = from.numFrames();
        table->numFrames = juce::jmin (available, maxFrames);

        size_t offset = 0;
        for (int level = 0; level < tables::numLevels; ++level)
        {
            table->levelOffsets[(size_t) level] = offset;
            table->levelSizes[(size_t) level] = levelSize (level);
            offset += (size_t) levelSize (level) + 1;
        }
        table->frameStride = offset;
        table->data.resize (table->frameStride * (size_t) table->numFrames);

        // The scale: the loudest source frame (less its DC) peaks at 1.
        const auto* samples = from.samples->data();
        const auto frameStart = [&] (int frame)
        {
            // More frames than allowed: spread evenly, keeping the first and the last.
            const auto sourceFrame = available <= maxFrames ? frame
                                   : juce::roundToInt ((double) frame * (available - 1) / (maxFrames - 1));
            return samples + (size_t) sourceFrame * (size_t) from.frameSize;
        };
        float peak = 0.0f;
        for (int frame = 0; frame < table->numFrames; ++frame)
        {
            const auto* x = frameStart (frame);
            double mean = 0.0;
            for (int i = 0; i < from.frameSize; ++i)
                mean += x[i];
            mean /= from.frameSize;
            for (int i = 0; i < from.frameSize; ++i)
                peak = juce::jmax (peak, std::abs (x[i] - (float) mean));
        }
        const auto scale = normalise && peak > 1.0e-6f ? 1.0f / peak : 1.0f;

        // Each frame's harmonics (the DC, harmonic 0, is left out), then each
        // level from as many of them as it holds.
        const auto count = juce::jmin (tables::maxHarmonics, from.frameSize / 2 - 1);
        for (int frame = 0; frame < table->numFrames; ++frame)
        {
            auto harmonics = harmonicsOf (frameStart (frame), from.frameSize, count);
            for (size_t h = 1; h < harmonics.size(); ++h)
                harmonics[h] *= scale * (gain != nullptr ? gain ((int) h) : 1.0f);
            for (int level = 0; level < tables::numLevels; ++level)
            {
                const auto size = table->levelSizes[(size_t) level];
                auto* out = table->data.data() + (size_t) frame * table->frameStride + table->levelOffsets[(size_t) level];
                synthesise (harmonics, juce::jmin (count, tables::harmonicsAt (level)), out, size);
                out[size] = out[0];
            }
        }
        return table;
    }

    Wavetable::Position Wavetable::locate (float index, int framesUsed, bool steppy) const noexcept
    {
        Position position;
        if (numFrames <= 1)
            return position;
        const auto used = juce::jlimit (2, numFrames, framesUsed);
        const auto at = juce::jlimit (0.0f, 1.0f, index) * (float) (used - 1);
        // FRAMES picks `used` frames spread evenly from the first to the last.
        const auto frameOf = [&] (int k) { return juce::roundToInt ((float) k * (float) (numFrames - 1) / (float) (used - 1)); };
        if (steppy)
        {
            position.below = position.above = frameOf (juce::roundToInt (at));
            return position;
        }
        const auto k = juce::jmin ((int) at, used - 2);
        position.below = frameOf (k);
        position.above = frameOf (k + 1);
        position.between = at - (float) k;
        return position;
    }

    //==========================================================================
    namespace factoryTables
    {
        const juce::StringArray names { "Analog Morph", "Pulse Width", "Harmonic Rise", "Vowels", "Hard Sync",
                                        "Wavefold", "FM Sweep", "Bit Steps", "Comb Sweep", "Organ Stack" };

        namespace
        {
            constexpr int frames = 64;
            constexpr int frameSize = 2048;
            constexpr int fine = 16384;   // time-domain shapes are drawn this finely, then their top harmonics left out

            // A frame from its harmonics (amplitudes of sines, 1-based).
            void fromSines (const std::vector<float>& sines, float* out)
            {
                std::vector<Complex> harmonics (sines.size());
                for (size_t h = 1; h < sines.size(); ++h)
                    harmonics[h] = { 0.0f, -sines[h] };   // sin = cos shifted back a quarter cycle
                synthesise (harmonics, (int) sines.size() - 1, out, frameSize);
            }

            // A frame from a shape drawn finely in time (0 <= t < 1): its first
            // 512 harmonics, so nothing it has above them can alias.
            template <typename Shape>
            void fromShape (Shape shape, float* out)
            {
                std::vector<float> drawn ((size_t) fine);
                for (int i = 0; i < fine; ++i)
                    drawn[(size_t) i] = (float) shape ((double) i / fine);
                const auto harmonics = harmonicsOf (drawn.data(), fine, tables::maxHarmonics);
                synthesise (harmonics, tables::maxHarmonics, out, frameSize);
            }

            double frac (double x) { return x - std::floor (x); }

            // A formant's gain at `hz`: a resonance at `centre`, `width` Hz wide.
            double formant (double hz, double centre, double width)
            {
                const auto d = (hz - centre) / (0.5 * width);
                return 1.0 / (1.0 + d * d);
            }

            WavetableSource generate (int index)
            {
                auto samples = std::make_shared<std::vector<float>> ((size_t) (frames * frameSize));
                for (int k = 0; k < frames; ++k)
                {
                    const auto t = (double) k / (frames - 1);   // 0 to 1 through the table
                    auto* out = samples->data() + (size_t) k * frameSize;
                    std::vector<float> sines ((size_t) tables::maxHarmonics + 1);
                    switch (index)
                    {
                        case 0:
                        {
                            // Sine, triangle, saw, square, blended in turn (all with their
                            // first harmonic in the same phase, so nothing cancels on the way).
                            const auto shape = [] (int which, int h) -> double
                            {
                                constexpr auto pi = juce::MathConstants<double>::pi;
                                switch (which)
                                {
                                    case 0:  return h == 1 ? 1.0 : 0.0;
                                    case 1:  return h % 2 == 0 ? 0.0 : 8.0 / (pi * pi * h * h) * (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0);
                                    case 2:  return 2.0 / (pi * h) * (h % 2 == 1 ? 1.0 : -1.0);
                                    default: return h % 2 == 0 ? 0.0 : 4.0 / (pi * h);
                                }
                            };
                            const auto at = t * 3.0;
                            const auto from = juce::jmin (2, (int) at);
                            const auto mix = at - from;
                            for (int h = 1; h <= tables::maxHarmonics; ++h)
                                sines[(size_t) h] = (float) ((1.0 - mix) * shape (from, h) + mix * shape (from + 1, h));
                            fromSines (sines, out);
                            break;
                        }
                        case 1:
                        {
                            // A pulse from square (50%) to a thin 4%.
                            const auto width = 0.5 - t * 0.46;
                            fromShape ([width] (double p) { return p < width ? 1.0 : -1.0; }, out);
                            break;
                        }
                        case 2:
                        {
                            // Harmonics added one by one (to 64), each fading in, 1/h as a saw's.
                            const auto count = 1.0 + t * 63.0;
                            for (int h = 1; h <= 64; ++h)
                                sines[(size_t) h] = (float) (juce::jlimit (0.0, 1.0, count - h + 1.0) / h);
                            fromSines (sines, out);
                            break;
                        }
                        case 3:
                        {
                            // A sung "ah", "eh", "ee", "oh", "oo" on A2 (110 Hz): a saw's
                            // harmonics through three formants that glide between the
                            // vowels. Formants from published averages for adult male
                            // voices; the widths and the blend are our own.
                            static constexpr double vowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                                                     { 570, 840, 2410 }, { 300, 870, 2240 } };
                            const auto at = t * 4.0;
                            const auto from = juce::jmin (3, (int) at);
                            const auto mix = at - from;
                            for (int h = 1; h <= tables::maxHarmonics; ++h)
                            {
                                const auto hz = 110.0 * h;
                                double gain = 0.0;
                                for (int f = 0; f < 3; ++f)
                                {
                                    const auto centre = std::exp ((1.0 - mix) * std::log (vowels[from][f]) + mix * std::log (vowels[from + 1][f]));
                                    gain += formant (hz, centre, 60.0 + 0.06 * centre) * (f == 0 ? 1.0 : f == 1 ? 0.7 : 0.4);
                                }
                                sines[(size_t) h] = (float) (gain / h);
                            }
                            fromSines (sines, out);
                            break;
                        }
                        case 4:
                        {
                            // A saw restarted every cycle of the note, running 1 to 8 times faster.
                            const auto ratio = 1.0 + t * 7.0;
                            fromShape ([ratio] (double p) { return 2.0 * frac (p * ratio) - 1.0; }, out);
                            break;
                        }
                        case 5:
                        {
                            // A sine pushed into a folder, harder and harder.
                            const auto gain = 1.0 + t * 5.0;
                            fromShape ([gain] (double p) { return std::sin (gain * juce::MathConstants<double>::halfPi * std::sin (twoPi * p)); }, out);
                            break;
                        }
                        case 6:
                        {
                            // Frequency (phase) modulation by a sine an octave up, deeper and deeper.
                            const auto depth = t * 6.0;
                            fromShape ([depth] (double p) { return std::sin (twoPi * p + depth * std::sin (twoPi * 2.0 * p)); }, out);
                            break;
                        }
                        case 7:
                        {
                            // A sine in fewer and fewer steps (64 down to 2).
                            const auto steps = std::round (std::exp2 (6.0 - t * 5.0));
                            fromShape ([steps] (double p) { return std::round (std::sin (twoPi * p) * steps) / steps; }, out);
                            break;
                        }
                        case 8:
                        {
                            // A saw with a comb of notches moving down through its harmonics.
                            const auto spacing = 0.02 + t * 0.23;
                            for (int h = 1; h <= tables::maxHarmonics; ++h)
                                sines[(size_t) h] = (float) ((0.5 + 0.5 * std::cos (twoPi * h * spacing)) / h);
                            fromSines (sines, out);
                            break;
                        }
                        default:
                        {
                            // An organ's stops drawn out in turn: octaves, then fifths.
                            static constexpr int stops[] = { 1, 2, 4, 3, 8, 6, 16, 12 };
                            const auto drawn = t * 8.0;
                            for (int s = 0; s < 8; ++s)
                                sines[(size_t) stops[s]] = (float) (juce::jlimit (0.0, 1.0, drawn - s + 1.0) * (s == 0 ? 1.0 : 0.6));
                            fromSines (sines, out);
                            break;
                        }
                    }
                }
                return { names[index], frameSize, samples };
            }
        }

        WavetableSource source (int index)
        {
            return generate (juce::jlimit (0, names.size() - 1, index));
        }

        std::shared_ptr<const Wavetable> get (int index)
        {
            static std::mutex lock;
            static std::vector<std::shared_ptr<const Wavetable>> built ((size_t) names.size());
            index = juce::jlimit (0, names.size() - 1, index);
            const std::scoped_lock scoped (lock);
            auto& table = built[(size_t) index];
            if (table == nullptr)
                table = Wavetable::build (generate (index));
            return table;
        }
    }

    //==========================================================================
    namespace wavetableFiles
    {
        int frameSizeFromChunk (const juce::MemoryBlock& file)
        {
            const auto* bytes = static_cast<const char*> (file.getData());
            const auto size = file.getSize();
            if (size < 12 || std::memcmp (bytes, "RIFF", 4) != 0 || std::memcmp (bytes + 8, "WAVE", 4) != 0)
                return 0;
            for (size_t at = 12; at + 8 <= size;)
            {
                const auto length = (size_t) juce::ByteOrder::littleEndianInt (bytes + at + 4);
                if (std::memcmp (bytes + at, "clm ", 4) == 0)
                {
                    // e.g. "<!>2048 10000000 wavetable (...)"
                    const juce::String text (bytes + at + 8, juce::jmin (length, size - at - 8));
                    if (text.startsWith ("<!>"))
                    {
                        const auto frameSize = text.substring (3).getIntValue();
                        return frameSize >= Wavetable::minFrameSize && frameSize <= Wavetable::maxFrameSize ? frameSize : 0;
                    }
                    return 0;
                }
                at += 8 + length + (length & 1);   // chunks are padded to an even length
            }
            return 0;
        }

        WavetableSource cut (const juce::String& name, const std::vector<float>& audio, double sampleRate, int markedFrameSize, const CutOptions& options)
        {
            const auto length = (int) audio.size();
            if (length < Wavetable::minFrameSize)
                return {};
            const auto from = juce::jlimit (0, length - 1, (int) (options.start * length));
            const auto to = juce::jlimit (from + 1, length, (int) std::ceil (options.end * length));
            const auto span = to - from;

            // The frame length: chosen, from a root note, as the file says, one cycle, or 2048.
            auto frameSize = options.frameSize;
            if (frameSize == 0 && options.rootNote >= 0)
                frameSize = cycleLength (options.rootNote, sampleRate);
            if (frameSize == 0)
                frameSize = markedFrameSize;
            if (frameSize == 0)
                frameSize = span <= Wavetable::maxFrameSize ? span : 2048;
            frameSize = juce::jlimit (Wavetable::minFrameSize, Wavetable::maxFrameSize, frameSize);
            if (span < frameSize)
                return {};

            // Where each frame starts.
            std::vector<int> starts;
            if (options.spread > 0)
            {
                // Spread evenly, first at the start, last ending at the end (Avenger's "sub divide").
                const auto count = juce::jlimit (1, Wavetable::maxFrames, options.spread);
                for (int k = 0; k < count; ++k)
                    starts.push_back (from + (count == 1 ? 0 : juce::roundToInt ((double) k * (span - frameSize) / (count - 1))));
            }
            else
            {
                const auto step = juce::jmax (1, options.step);
                for (auto at = from; at + frameSize <= to; at += frameSize * step)
                    starts.push_back (at);
                if ((int) starts.size() > Wavetable::maxFrames)
                {
                    // Too many: 256 spread evenly through them.
                    std::vector<int> kept;
                    for (int k = 0; k < Wavetable::maxFrames; ++k)
                        kept.push_back (starts[(size_t) juce::roundToInt ((double) k * ((int) starts.size() - 1) / (Wavetable::maxFrames - 1))]);
                    starts = std::move (kept);
                }
            }
            auto samples = std::make_shared<std::vector<float>> (starts.size() * (size_t) frameSize);
            for (size_t k = 0; k < starts.size(); ++k)
                std::copy_n (audio.data() + starts[k], frameSize, samples->data() + k * (size_t) frameSize);
            return { name, frameSize, samples };
        }

        namespace
        {
            // A file mixed to mono (up to 2^23 samples, about three minutes at 48 kHz).
            bool readMono (const juce::File& file, std::vector<float>& mono, double& sampleRate)
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
                if (reader == nullptr || reader->lengthInSamples < Wavetable::minFrameSize)
                    return false;
                const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, juce::int64 { 1 } << 23);
                const auto channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
                juce::AudioBuffer<float> audio (channels, length);
                if (! reader->read (&audio, 0, length, 0, true, channels > 1))
                    return false;
                mono.assign ((size_t) length, 0.0f);
                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < length; ++i)
                        mono[(size_t) i] += audio.getSample (c, i) / (float) channels;
                sampleRate = reader->sampleRate;
                return true;
            }

            int markedFrameSize (const juce::File& file)
            {
                juce::MemoryBlock bytes;
                return file.hasFileExtension ("wav") && file.loadFileAsData (bytes) ? frameSizeFromChunk (bytes) : 0;
            }
        }

        FileInfo inspect (const juce::File& file)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr)
                return {};
            return { reader->lengthInSamples, reader->sampleRate, markedFrameSize (file) };
        }

        WavetableSource load (const juce::File& file, const CutOptions& options)
        {
            std::vector<float> mono;
            double sampleRate = 48000.0;
            if (! readMono (file, mono, sampleRate))
                return {};
            return cut (file.getFileNameWithoutExtension(), mono, sampleRate, markedFrameSize (file), options);
        }

        juce::String encode (const WavetableSource& table)
        {
            if (! table.isValid())
                return {};
            const auto& samples = *table.samples;
            float peak = 0.0f;
            for (const auto x : samples)
                peak = juce::jmax (peak, std::abs (x));
            const auto scale = peak > 0.0f ? peak : 1.0f;

            juce::MemoryOutputStream compressed;
            {
                juce::GZIPCompressorOutputStream zip (compressed, 9);
                zip.writeFloat (scale);
                for (const auto x : samples)
                    zip.writeShort ((short) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, x / scale) * 32767.0f));
            }
            return juce::Base64::toBase64 (compressed.getData(), compressed.getDataSize());
        }

        WavetableSource decode (const juce::String& name, int frameSize, const juce::String& text)
        {
            if (frameSize < Wavetable::minFrameSize || frameSize > Wavetable::maxFrameSize)
                return {};
            juce::MemoryOutputStream compressed;
            if (! juce::Base64::convertFromBase64 (compressed, text))
                return {};
            juce::MemoryInputStream input (compressed.getData(), compressed.getDataSize(), false);
            juce::GZIPDecompressorInputStream zip (input);
            juce::MemoryBlock raw;
            zip.readIntoMemoryBlock (raw);
            if (raw.getSize() < 4 + 2 * (size_t) frameSize)
                return {};
            juce::MemoryInputStream data (raw, false);
            const auto scale = data.readFloat();
            const auto count = (raw.getSize() - 4) / 2;
            const auto numFrames = juce::jmin ((int) (count / (size_t) frameSize), Wavetable::maxFrames);
            auto samples = std::make_shared<std::vector<float>> ((size_t) numFrames * (size_t) frameSize);
            for (auto& x : *samples)
                x = (float) data.readShort() / 32767.0f * scale;
            return { name, frameSize, samples };
        }
    }
}
