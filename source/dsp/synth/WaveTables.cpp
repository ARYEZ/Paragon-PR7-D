#include "WaveTables.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace synth::tables
{
    namespace
    {
        // A wave's harmonic h (from 1): its sine amplitude.
        double amplitude (Wave wave, int h)
        {
            constexpr auto pi = juce::MathConstants<double>::pi;
            switch (wave)
            {
                case Wave::saw:
                    // A rising ramp, -1 to +1: -(2 / pi) * the sum of sin (h x) / h.
                    return -2.0 / (pi * h);
                case Wave::triangle:
                    // Odd harmonics only, falling as 1 / h^2, alternating in sign.
                    if (h % 2 == 0)
                        return 0.0;
                    return 8.0 / (pi * pi * h * h) * (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0);
                case Wave::sine:
                    return h == 1 ? 1.0 : 0.0;
            }
            return 0.0;
        }

        struct Store
        {
            Store()
            {
                // sin (2 pi h i / size) is sine[(h * i) mod size]: exact, and fast to sum.
                std::vector<double> sine ((size_t) size);
                for (int i = 0; i < size; ++i)
                    sine[(size_t) i] = std::sin (juce::MathConstants<double>::twoPi * i / size);

                std::vector<double> sum ((size_t) size);
                for (int w = 0; w < numWaves; ++w)
                {
                    // Each level holds every harmonic of the level after it plus a
                    // few more, so build from the sine up, adding harmonics.
                    std::fill (sum.begin(), sum.end(), 0.0);
                    int built = 0;
                    for (int level = numLevels - 1; level >= 0; --level)
                    {
                        for (int h = built + 1; h <= harmonicsAt (level); ++h)
                            if (const auto a = amplitude ((Wave) w, h); ! juce::exactlyEqual (a, 0.0))
                                for (int i = 0; i < size; ++i)
                                    sum[(size_t) i] += a * sine[(size_t) (((std::int64_t) h * i) & (size - 1))];
                        built = harmonicsAt (level);

                        auto& table = waves[(size_t) w][(size_t) level];
                        table.resize ((size_t) size + 1);
                        for (int i = 0; i < size; ++i)
                            table[(size_t) i] = (float) sum[(size_t) i];
                        table[(size_t) size] = table[0];
                    }
                }
            }

            static constexpr int numWaves = 3;
            std::array<std::array<std::vector<float>, numLevels>, numWaves> waves;
        };

        const Store& store()
        {
            static const Store instance;   // built once, thread-safely, the first time it's needed (prepareToPlay asks)
            return instance;
        }
    }

    namespace
    {
        // Each level's harmonic count, worked out once.
        struct Counts
        {
            Counts()
            {
                for (int level = 0; level < numLevels; ++level)
                    harmonics[(size_t) level] = level == numLevels - 1
                        ? 1 : juce::jmax (1, (int) std::floor ((double) maxHarmonics * std::exp2 (-(double) level / 3.0) + 1.0e-9));
            }
            std::array<int, numLevels> harmonics {};
        };

        const Counts& counts()
        {
            static const Counts instance;
            return instance;
        }
    }

    int harmonicsAt (int level) noexcept
    {
        return counts().harmonics[(size_t) juce::jlimit (0, numLevels - 1, level)];
    }

    double harmonicLimitHz (double sampleRate) noexcept
    {
        return juce::jmax (sampleRate - 20000.0, 0.45 * sampleRate);
    }

    int levelFor (double frequencyHz, double sampleRate) noexcept
    {
        if (frequencyHz <= 0.0)
            return 0;
        const auto allowed = harmonicLimitHz (sampleRate) / frequencyHz;   // how many harmonics fit
        // The first level (most harmonics) within the limit: a short search
        // through the counts (they fall as the level rises), run for every
        // oscillator copy every 16 samples, so kept cheap.
        const auto& harmonics = counts().harmonics;
        int low = 0, high = numLevels - 1;
        while (low < high)
        {
            const auto middle = (low + high) / 2;
            if ((double) harmonics[(size_t) middle] > allowed)
                low = middle + 1;
            else
                high = middle;
        }
        return low;
    }

    const float* get (Wave wave, int level) noexcept
    {
        return store().waves[(size_t) wave][(size_t) juce::jlimit (0, numLevels - 1, level)].data();
    }
}
