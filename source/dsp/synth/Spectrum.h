#pragma once

#include "SynthSettings.h"
#include "Wavetable.h"

#include <array>

// The FFT editor (stage 3d), modelled on Avenger's (our own design and code):
// a gain drawn for each of a wave's first 256 harmonics ("bands"), as Avenger's
// 256-band FFT. Harmonics above 256 (up to the 512 a table holds) follow the
// last band.
//
// The drawn curve, SWEEP (shifting it up or down the harmonics) and FLIP
// (valleys become mountains) are baked into a rebuilt copy of the
// oscillator's table, in the background, so playing an edited wave costs
// what playing a wavetable does. VSPEED / VSTRENGTH (the bands pulsing)
// build a second copy with the bands pushed the other way, and the
// oscillator swings between the two. LP, HP and SLOPE are live instead
// (blending the table's own brightness levels: see the engine), so they can
// move as fast as anything else.
namespace synth
{
    struct SpectrumEdit
    {
        static constexpr int bands = 256;
        static constexpr float minDb = -48.0f;   // at the bottom: that harmonic is gone
        static constexpr float maxDb = 24.0f;

        std::array<float, bands> gainsDb {};

        bool isFlat() const noexcept
        {
            return std::all_of (gainsDb.begin(), gainsDb.end(), [] (float g) { return std::abs (g) < 0.01f; });
        }
        bool operator== (const SpectrumEdit& other) const noexcept { return gainsDb == other.gainsDb; }

        void reset() noexcept { gainsDb.fill (0.0f); }

        // RANDOM: every band a random gain, as Avenger's. Ours leans to cuts
        // (most bands somewhere between -24 dB and +6 dB) so it stays usable.
        void randomise (juce::Random& random) noexcept
        {
            for (auto& g : gainsDb)
                g = juce::jlimit (minDb, maxDb, -24.0f + 30.0f * random.nextFloat());
        }

        // As text for presets and projects: one decimal a band, flat ones left out ("band:dB").
        juce::String encode() const
        {
            juce::StringArray items;
            for (int b = 0; b < bands; ++b)
                if (std::abs (gainsDb[(size_t) b]) >= 0.01f)
                    items.add (juce::String (b) + ":" + juce::String (gainsDb[(size_t) b], 1));
            return items.joinIntoString (" ");
        }
        static SpectrumEdit decode (const juce::String& text)
        {
            SpectrumEdit edit;
            for (const auto& item : juce::StringArray::fromTokens (text, " ", {}))
            {
                const auto band = item.upToFirstOccurrenceOf (":", false, false).getIntValue();
                if (band >= 0 && band < bands && item.contains (":"))
                    edit.gainsDb[(size_t) band] = juce::jlimit (minDb, maxDb, item.fromFirstOccurrenceOf (":", false, false).getFloatValue());
            }
            return edit;
        }
    };

    namespace spectrum
    {
        inline constexpr float sweepOctaves = 4.0f;   // SWEEP at either end: the curve moved 4 octaves

        // The dB the curve gives harmonic `h` (from 1), SWEEP (-1 to 1) and FLIP applied.
        inline float gainDbFor (const SpectrumEdit& edit, int h, float sweep, bool flip) noexcept
        {
            // SWEEP moves the drawing up (right) or down (left): harmonic h
            // gets what was drawn at h / 2^octaves (between bands, in between).
            const auto at = (float) h * std::exp2 (-sweepOctaves * juce::jlimit (-1.0f, 1.0f, sweep));
            float db = 0.0f;
            if (at >= 1.0f)
            {
                const auto position = juce::jmin (at - 1.0f, (float) (SpectrumEdit::bands - 1));
                const auto below = juce::jmin ((int) position, SpectrumEdit::bands - 2);
                const auto t = juce::jlimit (0.0f, 1.0f, position - (float) below);
                const auto a = edit.gainsDb[(size_t) below], b = edit.gainsDb[(size_t) below + 1];
                // A silenced band stays silent right up to it (no half-silence smeared across).
                db = a <= SpectrumEdit::minDb + 0.01f && t < 0.5f ? a : b <= SpectrumEdit::minDb + 0.01f && t >= 0.5f ? b : a + (b - a) * t;
            }
            if (flip)
                db = -db;
            return juce::jlimit (SpectrumEdit::minDb, SpectrumEdit::maxDb, db);
        }

        inline float gainFor (const SpectrumEdit& edit, int h, float sweep, bool flip) noexcept
        {
            const auto db = gainDbFor (edit, h, sweep, flip);
            return db <= SpectrumEdit::minDb + 0.01f ? 0.0f : std::pow (10.0f, db / 20.0f);
        }

        // VSTRENGTH's pattern: how far, and which way, each harmonic pulses
        // (-1 to 1, spread unevenly so neighbours move differently: our choice).
        inline float pulsePattern (int h) noexcept { return std::sin ((float) h * 2.39996323f); }

        // One cycle of a basic wave (band-limited, 512 harmonics) to edit, as a one-frame source.
        inline WavetableSource basicWave (OscWave wave, float pulseWidth)
        {
            constexpr int size = tables::size;
            auto samples = std::make_shared<std::vector<float>> ((size_t) size);
            const auto table = [] (OscWave w)
            {
                return tables::get (w == OscWave::triangle ? tables::Wave::triangle : w == OscWave::sine ? tables::Wave::sine : tables::Wave::saw, 0);
            };
            const auto* t = table (wave);
            const auto width = juce::jlimit (0.01f, 0.99f, pulseWidth);
            for (int i = 0; i < size; ++i)
            {
                const auto phase = (float) i / (float) size;
                auto value = tables::read (t, phase);
                if (wave == OscWave::pulse)
                {
                    auto other = phase + width;
                    other -= (float) (other >= 1.0f);
                    value -= tables::read (t, other);
                }
                (*samples)[(size_t) i] = value;
            }
            return { oscWaveNames[(int) wave], size, samples };
        }
    }
}
