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
    DominionPreset make (const juce::String& category, const juce::String& name,
                         std::initializer_list<std::pair<const char*, float>> settings, std::initializer_list<BandSpec> bands)
    {
        DominionPreset preset;
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

DominionPresets::DominionPresets (juce::File f) : folder (std::move (f)) {}

bool DominionPresets::holds (const juce::String& id)
{
    return id.startsWith ("dom") && id != ParamIDs::domOn;
}

const std::vector<DominionPreset>& DominionPresets::factory()
{
    // Our own starting points, not copies of anyone's presets. The depth
    // curve does most of the steering: where to work, and where to leave alone.
    using namespace ParamIDs;
    static const std::vector<DominionPreset> presets {
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
    return presets;
}

const DominionPreset* DominionPresets::findFactory (const juce::String& name) const
{
    for (const auto& preset : factory())
        if (preset.name == name)
            return &preset;
    return nullptr;
}

std::vector<DominionPreset> DominionPresets::user() const
{
    std::vector<DominionPreset> presets;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("DominionPreset"))
            continue;
        DominionPreset preset;
        preset.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        preset.file = file;
        for (const auto* p : xml->getChildWithTagNameIterator ("Param"))
            if (const auto id = p->getStringAttribute ("id"); holds (id))
                preset.parameters[id] = (float) p->getDoubleAttribute ("value");
        presets.push_back (std::move (preset));
    }
    std::sort (presets.begin(), presets.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });
    return presets;
}

DominionPreset DominionPresets::capture (juce::AudioProcessorValueTreeState& state, const juce::String& name)
{
    DominionPreset preset;
    preset.name = name;
    for (auto* parameter : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && holds (ranged->paramID))
            preset.parameters[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());
    return preset;
}

void DominionPresets::apply (const DominionPreset& preset, juce::AudioProcessorValueTreeState& state)
{
    for (auto* parameter : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr || ! holds (ranged->paramID))
            continue;
        const auto found = preset.parameters.find (ranged->paramID);
        const auto normalised = found != preset.parameters.end() ? ranged->convertTo0to1 (found->second) : ranged->getDefaultValue();
        if (juce::approximatelyEqual (ranged->getValue(), normalised))
            continue;
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (normalised);
        ranged->endChangeGesture();
    }
}

bool DominionPresets::matches (const DominionPreset& preset, juce::AudioProcessorValueTreeState& state)
{
    for (auto* parameter : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr || ! holds (ranged->paramID))
            continue;
        const auto found = preset.parameters.find (ranged->paramID);
        const auto wanted = found != preset.parameters.end() ? ranged->convertTo0to1 (found->second) : ranged->getDefaultValue();
        if (std::abs (ranged->getValue() - wanted) > 1.0e-4f)
            return false;
    }
    return true;
}

bool DominionPresets::save (const DominionPreset& preset) const
{
    if (preset.name.trim().isEmpty() || ! folder.createDirectory())
        return false;
    juce::XmlElement xml ("DominionPreset");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("name", preset.name.trim());
    for (const auto& [id, value] : preset.parameters)
    {
        auto* p = xml.createNewChildElement ("Param");
        p->setAttribute ("id", id);
        p->setAttribute ("value", value);
    }
    return xml.writeTo (folder.getChildFile (juce::File::createLegalFileName (preset.name.trim()) + fileExtension));
}

bool DominionPresets::remove (const DominionPreset& preset) const
{
    return ! preset.isFactory && preset.file.existsAsFile() && preset.file.deleteFile();
}
