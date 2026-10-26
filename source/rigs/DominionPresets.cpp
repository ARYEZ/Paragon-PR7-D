#include "DominionPresets.h"

#include "Parameters.h"
#include "dsp/dominion/Dominion.h"
#include "dsp/eq/EqDesign.h"

namespace
{
    using dominion::Shape;

    struct BandSpec
    {
        Shape shape;
        float frequency, depthDb = 0.0f, q = 1.0f;
        int slopeDb = 24;
    };

    int slopeIndex (int dbPerOctave)
    {
        for (int i = 0; i < eq::numSlopes; ++i)
            if (eq::slopes[i] == dbPerOctave)
                return i;
        return eq::defaultSlope;
    }

    // A factory preset: the settings it changes from the defaults, and its depth-curve bands.
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
            const auto set = [&] (const char* what, float value) { preset.parameters[ParamIDs::domBand (index, what)] = value; };
            set ("Used", 1.0f);
            set ("Shape", (float) b.shape);
            set ("Freq", b.frequency);
            set ("Depth", b.depthDb);
            set ("Q", b.q);
            set ("Slope", (float) slopeIndex (b.slopeDb));
            ++index;
        }
        return preset;
    }
}

bool DominionPresetTraits::holds (const juce::String& id)
{
    return id.startsWith ("dom") && id != ParamIDs::domOn;
}

std::vector<StompPreset> DominionPresetTraits::makeFactory()
{
    // Our own starting points, not copies of anyone's presets. The depth
    // curve does most of the steering: where to work, and where to leave alone.
    using namespace ParamIDs;
    return {
        make ("", "Default", {}, {}),

        make ("Vocals", "Vocal De-Harsh", { { domDepth, 5.0f }, { domDetail, 60.0f }, { domAttack, 40.0f } },
              { { Shape::lowCut, 200.0f }, { Shape::bell, 3500.0f, 4.0f, 0.8f } }),
        make ("Vocals", "Vocal Sibilance", { { domDepth, 7.0f }, { domDetail, 70.0f }, { domAttack, 20.0f }, { domRelease, 30.0f } },
              { { Shape::lowCut, 4000.0f }, { Shape::bell, 7000.0f, 4.0f, 1.0f } }),
        make ("Vocals", "Vocal Mud & Boom", { { domDepth, 4.0f }, { domDetail, 30.0f }, { domRelease, 60.0f } },
              { { Shape::lowCut, 80.0f }, { Shape::highCut, 900.0f } }),

        make ("Drums", "Overheads Ring", { { domDepth, 6.0f }, { domDetail, 75.0f }, { domAttack, 30.0f }, { domRelease, 40.0f } },
              { { Shape::lowCut, 600.0f } }),
        make ("Drums", "Snare Ring", { { domDepth, 6.0f }, { domDetail, 80.0f }, { domAttack, 60.0f } },
              { { Shape::bandPass, 450.0f, 0.0f, 0.7f, 12 } }),
        make ("Drums", "Kick Boom", { { domDepth, 4.0f }, { domDetail, 35.0f }, { domAttack, 70.0f }, { domRelease, 60.0f } },
              { { Shape::lowCut, 30.0f }, { Shape::highCut, 250.0f } }),

        make ("Instruments", "Acoustic Guitar", { { domDepth, 4.0f }, { domDetail, 50.0f } }, { { Shape::lowCut, 120.0f } }),
        make ("Instruments", "Electric Guitar Fizz", { { domDepth, 5.0f }, { domDetail, 60.0f } }, { { Shape::lowCut, 2500.0f } }),
        make ("Instruments", "Bass Smoother", { { domDepth, 4.0f }, { domDetail, 40.0f }, { domRelease, 70.0f }, { domAttackLow, 40.0f } },
              { { Shape::highCut, 1200.0f } }),
        make ("Instruments", "Piano & Keys", { { domDepth, 3.5f }, { domDetail, 55.0f } }, { { Shape::lowCut, 150.0f } }),

        make ("Mix & Master", "Mix Bus Gentle", { { domDepth, 2.5f }, { domDetail, 40.0f }, { domRelease, 60.0f } },
              { { Shape::lowCut, 100.0f } }),
        make ("Mix & Master", "Master Polish", { { domDepth, 1.5f }, { domDetail, 50.0f }, { domMaxCut, 3.0f } },
              { { Shape::lowCut, 60.0f } }),

        make ("Creative", "Hard Carve", { { domMode, 1.0f }, { domDepth, 12.0f }, { domDetail, 85.0f }, { domAttack, 10.0f }, { domRelease, 20.0f } }, {}),
    };
}
