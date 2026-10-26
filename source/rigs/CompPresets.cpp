#include "CompPresets.h"

#include "Parameters.h"
#include "dsp/effects/MultibandComp.h"

namespace
{
    namespace mb = effects::multiband;
    using Character = effects::Compressor::Character;

    // A band: where it is and what it does (the rest at their defaults).
    struct BandSpec
    {
        float lowHz, highHz, thresholdDb, rangeDb, ratio = 3.0f;
        mb::Mode mode = mb::Mode::compress;
        float attackMs = 10.0f, releaseMs = 150.0f;
        Character character = Character::vca;
        mb::Stereo stereo = mb::Stereo::stereo;
        float kneeDb = 6.0f;
    };

    // The full band's settings (it's on unless a preset says otherwise), then its bands.
    StompPreset make (const juce::String& category, const juce::String& name,
                      std::initializer_list<std::pair<const char*, float>> settings, std::initializer_list<BandSpec> bands)
    {
        StompPreset preset;
        preset.category = category;
        preset.name = name;
        preset.isFactory = true;
        for (const auto& [id, value] : settings)
            preset.parameters[id] = value;
        int index = 0;
        for (const auto& b : bands)
        {
            const auto set = [&] (const char* what, float value) { preset.parameters[ParamIDs::mbBand (index, what)] = value; };
            set ("Used", 1.0f);
            set ("Low", b.lowHz);
            set ("High", b.highHz);
            set ("Thresh", b.thresholdDb);
            set ("Range", b.rangeDb);
            set ("Ratio", b.ratio);
            set ("Mode", (float) b.mode);
            set ("Attack", b.attackMs);
            set ("Release", b.releaseMs);
            set ("Char", (float) b.character);
            set ("Stereo", (float) b.stereo);
            set ("Knee", b.kneeDb);
            ++index;
        }
        return preset;
    }

    constexpr auto opto = (float) Character::opto;
    constexpr float bottom = mb::minHz, top = mb::maxHz;
}

bool CompPresetTraits::holds (const juce::String& id)
{
    return (id.startsWith ("comp") && id != ParamIDs::compOn) || id.startsWith ("mb");
}

std::vector<StompPreset> CompPresetTraits::makeFactory()
{
    // Our own starting points, from common mixing moves, not copies of
    // anyone's presets. Thresholds depend on how loud the material is: set
    // THRESHOLD by ear (a few dB of gain reduction is usually plenty).
    using namespace ParamIDs;
    const auto expand = mb::Mode::expand;
    return {
        make ("", "Default", {}, {}),

        // Vocals
        make ("Vocals", "Vocal Leveler", { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 3.0f }, { compAttack, 15.0f },
                                           { compRelease, 250.0f }, { compMakeUp, 3.0f } }, {}),
        make ("Vocals", "Vocal Smooth & Bright", { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 2.5f }, { compMakeUp, 2.0f } },
              { { 2500.0f, 7000.0f, -32.0f, -5.0f, 3.0f, mb::Mode::compress, 3.0f, 80.0f },   // the harsh part, tamed when it pokes out
                { 9000.0f, top, -42.0f, 3.0f, 2.0f } }),                                       // the air, brought up when it's quiet
        make ("Vocals", "Vocal Body", { { compWide, 0.0f } },
              { { 150.0f, 500.0f, -36.0f, 4.0f, 2.0f, mb::Mode::compress, 20.0f, 200.0f } }),  // fullness brought up from below

        // Drums
        make ("Drums", "Drum Bus Glue", { { compThreshold, -20.0f }, { compRatio, 2.0f }, { compAttack, 30.0f }, { compRelease, 150.0f },
                                          { compAuto, 1.0f }, { compMakeUp, 2.0f } }, {}),
        make ("Drums", "Kick Tight", { { compWide, 0.0f } },
              { { bottom, 120.0f, -24.0f, -6.0f, 4.0f, mb::Mode::compress, 8.0f, 120.0f, Character::fet } }),
        make ("Drums", "Snare Punch", { { compWide, 0.0f } },
              { { 150.0f, 400.0f, -24.0f, 4.0f, 2.0f, expand, 1.0f, 60.0f, Character::fet, mb::Stereo::stereo, 3.0f } }),
        make ("Drums", "Room Gate", { { compWide, 0.0f } },
              { { bottom, top, -40.0f, -18.0f, 3.0f, expand, 1.0f, 120.0f, Character::fet } }),

        // Bass and instruments
        make ("Instruments", "Bass Even", { { compCharacter, opto }, { compThreshold, -22.0f }, { compRatio, 4.0f }, { compMakeUp, 3.0f } },
              { { bottom, 90.0f, -26.0f, -4.0f, 3.0f, mb::Mode::compress, 20.0f, 200.0f } }),
        make ("Instruments", "Guitar Fizz Control", { { compWide, 0.0f } },
              { { 3000.0f, 9000.0f, -34.0f, -6.0f, 3.0f, mb::Mode::compress, 2.0f, 80.0f } }),
        make ("Instruments", "Piano & Keys Even", { { compCharacter, opto }, { compThreshold, -24.0f }, { compRatio, 2.0f }, { compMakeUp, 1.5f } },
              { { bottom, 200.0f, -28.0f, -3.0f, 2.0f } }),

        // Mix and mastering
        make ("Mix & Master", "Mix Bus Glue", { { compThreshold, -16.0f }, { compRatio, 1.5f }, { compAttack, 30.0f }, { compRelease, 200.0f },
                                                { compAuto, 1.0f }, { compMakeUp, 1.0f } }, {}),
        make ("Mix & Master", "Master Four Bands", { { compWide, 0.0f }, { mbLookahead, 1.0f } },
              { { bottom, 120.0f, -24.0f, -3.0f, 2.0f, mb::Mode::compress, 20.0f, 200.0f },
                { 120.0f, 800.0f, -24.0f, -2.0f, 1.5f, mb::Mode::compress, 15.0f, 150.0f },
                { 800.0f, 5000.0f, -26.0f, -2.0f, 1.5f, mb::Mode::compress, 10.0f, 120.0f },
                { 5000.0f, top, -30.0f, -2.0f, 1.5f, mb::Mode::compress, 5.0f, 100.0f } }),
        make ("Mix & Master", "Low End Control", { { compWide, 0.0f } },
              { { bottom, 150.0f, -24.0f, -4.0f, 3.0f, mb::Mode::compress, 20.0f, 150.0f, Character::vca, mb::Stereo::mid },
                { bottom, 150.0f, -40.0f, -12.0f, 4.0f, mb::Mode::compress, 10.0f, 150.0f, Character::vca, mb::Stereo::side } }),
        make ("Mix & Master", "De-Harsh", { { compWide, 0.0f } },
              { { 2000.0f, 6000.0f, -30.0f, -3.0f, 2.0f, mb::Mode::compress, 3.0f, 80.0f } }),
        make ("Mix & Master", "Air Lift", { { compWide, 0.0f } },
              { { 10000.0f, top, -40.0f, 3.0f, 2.0f, mb::Mode::compress, 20.0f, 200.0f } }),

        // Creative
        make ("Creative", "Upward Squash", { { compWide, 0.0f } },
              { { bottom, 250.0f, -30.0f, 12.0f, 10.0f }, { 250.0f, 3000.0f, -30.0f, 12.0f, 10.0f }, { 3000.0f, top, -30.0f, 12.0f, 10.0f } }),
    };
}
