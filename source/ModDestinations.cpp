#include "ModDestinations.h"

#include "Parameters.h"

namespace ModDestinations
{
    const std::vector<Destination>& all()
    {
        static const auto list = []
        {
            using synth::mod::Field;
            std::vector<Destination> d;
            const std::pair<const char*, std::pair<const char*, Field>> oscFields[] = {
                { "Volume", { "Volume", Field::oscVolume } }, { "Pan", { "Pan", Field::oscPan } },
                { "Transpose", { "Transpose", Field::oscTranspose } }, { "Fine", { "Fine", Field::oscFine } },
                { "Gain", { "Wave Gain", Field::oscGain } }, { "Width", { "Pulse Width", Field::oscWidth } },
                { "WtIndex", { "Wavetable Index", Field::oscTableIndex } },
                { "Noise", { "Noise", Field::oscNoise } }, { "NoiseColour", { "Noise Colour", Field::oscNoiseColour } },
                { "Sub", { "Sub", Field::oscSub } },
                { "Formant", { "Formant", Field::oscFormant } }, { "Crush", { "Crush", Field::oscCrush } },
                { "SyncAmount", { "Sync", Field::oscSyncAmount } }, { "SyncEnv", { "Sync Envelope", Field::oscSyncEnv } },
                { "FmAmount", { "FM", Field::oscFmAmount } }, { "AmAmount", { "AM", Field::oscAmAmount } },
                { "ModRate", { "Mod Rate", Field::oscModRate } },
                { "UniMix", { "Unison Mix", Field::oscUnisonMix } }, { "UniDetune", { "Unison Detune", Field::oscUnisonDetune } },
                { "UniSpread", { "Unison Spread", Field::oscUnisonSpread } },
                { "VibRate", { "Vibrato Rate", Field::oscVibratoRate } }, { "VibAmount", { "Vibrato Amount", Field::oscVibratoAmount } },
                { "FftLp", { "FFT LP", Field::oscFftLp } }, { "FftHp", { "FFT HP", Field::oscFftHp } },
                { "FftSlope", { "FFT Slope", Field::oscFftSlope } }, { "FftVSpeed", { "FFT VSpeed", Field::oscFftVSpeed } } };
            for (int i = 0; i < ParamIDs::numOscillators; ++i)
                for (const auto& [setting, entry] : oscFields)
                    d.push_back ({ ParamIDs::osc (i, setting), "Osc " + juce::String (i + 1), entry.first, { entry.second, i } });

            d.push_back ({ ParamIDs::fltCutoff, "Filter", "Cutoff", { Field::filterCutoff, 0 } });
            d.push_back ({ ParamIDs::fltRes, "Filter", "Resonance", { Field::filterResonance, 0 } });
            d.push_back ({ ParamIDs::fltDrive, "Filter", "Drive", { Field::filterDrive, 0 } });
            d.push_back ({ ParamIDs::fltKey, "Filter", "Keytrack", { Field::filterKeytrack, 0 } });
            d.push_back ({ ParamIDs::fltEnv, "Filter", "Envelope", { Field::filterEnvelope, 0 } });

            for (int k = 0; k < ParamIDs::numShapers; ++k)
            {
                const auto group = "Shaper " + juce::String (k + 1);
                d.push_back ({ ParamIDs::shp (k, "Drive"), group, "Drive", { Field::shaperDrive, k } });
                d.push_back ({ ParamIDs::shp (k, "Mix"), group, "Mix", { Field::shaperMix, k } });
                d.push_back ({ ParamIDs::shp (k, "Output"), group, "Output", { Field::shaperOutput, k } });
            }

            d.push_back ({ ParamIDs::ampVolume, "Amp", "Volume", { Field::ampVolume, 0 } });
            d.push_back ({ ParamIDs::ampPan, "Amp", "Pan", { Field::ampPan, 0 } });
            return d;
        }();
        return list;
    }

    const Destination* find (const juce::String& parameterId)
    {
        for (const auto& d : all())
            if (d.parameterId == parameterId)
                return &d;
        return nullptr;
    }
}
