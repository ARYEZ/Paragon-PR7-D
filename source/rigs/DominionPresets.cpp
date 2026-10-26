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

    // Genre presets (0.15.0), filed as "Genres/<genre>": what the genre rigs
    // use on their vocal, guitars and mix. Our own reading of where each
    // style tends to ring or turn harsh, not anyone's settings.
    std::vector<StompPreset> makeGenres()
    {
        using namespace ParamIDs;
        // A vocal: the harsh middle steered by a bell, the low end left alone.
        const auto vocal = [] (const juce::String& genre, float depth, float lowCutHz, float bellHz, float bellDb)
        {
            return make ("Genres/" + genre, genre + " Vocal", { { domDepth, depth }, { domDetail, 60.0f }, { domAttack, 35.0f } },
                         { { Shape::lowCut, lowCutHz }, { Shape::bell, bellHz, bellDb, 0.8f } });
        };
        // Distorted guitars: the fizz and the upper-mid whistle, nothing under the low cut.
        const auto guitars = [] (const juce::String& genre, float depth, float lowCutHz, float bellHz, float bellDb)
        {
            return make ("Genres/" + genre, genre + " Guitars", { { domDepth, depth }, { domDetail, 65.0f }, { domAttack, 30.0f } },
                         { { Shape::lowCut, lowCutHz }, { Shape::bell, bellHz, bellDb, 0.9f } });
        };
        // A mix: gentle, and gentler still on the low end.
        const auto mix = [] (const juce::String& genre, float depth, float bellHz, float bellDb)
        {
            return make ("Genres/" + genre, genre + " Mix", { { domDepth, depth }, { domDetail, 45.0f }, { domRelease, 60.0f } },
                         { { Shape::lowCut, 150.0f }, { Shape::bell, bellHz, bellDb, 0.8f } });
        };
        return {
            vocal ("Modern Metal", 5.0f, 250.0f, 3000.0f, 3.0f),
            guitars ("Modern Metal", 5.0f, 1500.0f, 4500.0f, 3.0f),
            mix ("Modern Metal", 3.0f, 3500.0f, 4.0f),

            vocal ("Metalcore", 5.5f, 250.0f, 3200.0f, 4.0f),
            guitars ("Metalcore", 5.0f, 1500.0f, 4000.0f, 3.0f),
            mix ("Metalcore", 3.0f, 3000.0f, 4.0f),

            vocal ("Djent", 4.5f, 300.0f, 3500.0f, 3.0f),
            guitars ("Djent", 6.0f, 1200.0f, 3500.0f, 4.0f),   // tight, glassy and clinical: more of it
            mix ("Djent", 3.0f, 4000.0f, 4.0f),

            vocal ("Nu Metal", 5.0f, 250.0f, 2500.0f, 3.0f),
            guitars ("Nu Metal", 4.5f, 2000.0f, 5000.0f, 3.0f),
            mix ("Nu Metal", 2.5f, 3000.0f, 3.0f),

            vocal ("Argent Metal", 4.0f, 200.0f, 2500.0f, 3.0f),
            guitars ("Argent Metal", 6.5f, 1000.0f, 3000.0f, 5.0f),   // saturated, dense guitars: their whistles worked hardest
            mix ("Argent Metal", 3.5f, 3500.0f, 5.0f),

            vocal ("Hard Rock", 4.0f, 200.0f, 3000.0f, 3.0f),
            guitars ("Hard Rock", 4.0f, 2000.0f, 4500.0f, 2.0f),
            mix ("Hard Rock", 2.0f, 3000.0f, 3.0f),

            vocal ("Grunge", 3.5f, 200.0f, 2500.0f, 2.0f),
            guitars ("Grunge", 3.5f, 1500.0f, 3500.0f, 2.0f),   // fuzz is meant to be rough: a light touch
            mix ("Grunge", 2.0f, 3000.0f, 2.0f),

            vocal ("Symphonic Metal", 6.0f, 300.0f, 3000.0f, 5.0f),   // a soprano's piercing high notes
            guitars ("Symphonic Metal", 5.0f, 1500.0f, 4000.0f, 3.0f),
            mix ("Symphonic Metal", 3.0f, 3000.0f, 4.0f),

            vocal ("Industrial", 4.5f, 300.0f, 2000.0f, 3.0f),
            guitars ("Industrial", 5.0f, 1500.0f, 3500.0f, 3.0f),
            mix ("Industrial", 3.0f, 4000.0f, 4.0f),
        };
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
    std::vector<StompPreset> presets {
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

    // Genre presets (0.15.0) after these, each genre in its own submenu.
    auto genres = makeGenres();
    presets.insert (presets.end(), genres.begin(), genres.end());
    return presets;
}
