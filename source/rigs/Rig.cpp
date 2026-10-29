#include "Rig.h"

namespace
{
    // Bump when the file layout changes in a way older code can't read.
    constexpr int fileVersion = 1;

    // Which plugin version's parameters the preset was saved with, for the
    // occasional parameter whose default changes (older plugins ignore it).
    // PR7-K counts afresh from its siblings' numbering:
    // 1: PR7-K 0.1.0, the first synth parameters.
    constexpr int parameterVersion = 1;

    const juce::Identifier rigTag { "ParagonRig" };
    const juce::Identifier parameterTag { "Param" };
    const juce::Identifier wavetableTag { "Wavetable" };
    const juce::Identifier spectrumTag { "Spectrum" };
    const juce::Identifier freeformTag { "Freeform" };
    const juce::Identifier drumSampleTag { "DrumSample" };
}

juce::String Rig::bankName (int bankIndex)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, numBanks - 1, bankIndex)));
}

juce::String Rig::getSlotName() const
{
    return bankName (bank) + juce::String (number).paddedLeft ('0', 2);
}

std::unique_ptr<juce::XmlElement> Rig::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (rigTag);
    xml->setAttribute ("version", fileVersion);
    xml->setAttribute ("parameterVersion", parameterVersion);
    xml->setAttribute ("name", name);
    xml->setAttribute ("bank", bankName (bank));
    xml->setAttribute ("number", number);
    xml->setAttribute ("favourite", favourite);
    xml->setAttribute ("tags", tags.joinIntoString (","));
    if (notes.isNotEmpty())
        xml->setAttribute ("notes", notes);

    for (const auto& [id, value] : parameters)
    {
        auto* parameter = xml->createNewChildElement (parameterTag);
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }
    for (const auto& [oscillator, table] : wavetables)
    {
        if (! table.isValid())
            continue;
        auto* element = xml->createNewChildElement (wavetableTag);
        element->setAttribute ("osc", oscillator + 1);
        element->setAttribute ("name", table.name);
        element->setAttribute ("frameSize", table.frameSize);
        element->addTextElement (synth::wavetableFiles::encode (table));
    }
    for (const auto& [slot, sample] : drumSamples)
    {
        if (sample == nullptr || sample->length() < 2)
            continue;
        auto* element = xml->createNewChildElement (drumSampleTag);
        element->setAttribute ("slot", slot + 1);
        element->setAttribute ("name", sample->name);
        if (sample->path.isNotEmpty())
            element->setAttribute ("path", sample->path);
        element->addTextElement (synth::drums::sampleFiles::encode (*sample));
    }
    for (const auto& [oscillator, edit] : spectra)
    {
        if (edit.isFlat())
            continue;
        auto* element = xml->createNewChildElement (spectrumTag);
        element->setAttribute ("osc", oscillator + 1);
        element->setAttribute ("gains", edit.encode());
    }
    for (const auto& [oscillator, drawing] : drawings)
    {
        if (drawing == synth::FreeformDrawing {})
            continue;
        auto* element = xml->createNewChildElement (freeformTag);
        element->setAttribute ("osc", oscillator + 1);
        element->setAttribute ("drawing", drawing.encode());
    }
    if (! modulation.isEmpty())
        xml->addChildElement (modulation.toXml().release());
    return xml;
}

std::optional<Rig> Rig::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (rigTag) || xml.getIntAttribute ("version", 0) > fileVersion)
        return std::nullopt;

    Rig rig;
    rig.name = xml.getStringAttribute ("name", "Untitled").trim();
    if (rig.name.isEmpty())
        rig.name = "Untitled";

    const auto bankLetter = xml.getStringAttribute ("bank", "A").toUpperCase();
    rig.bank = bankLetter.isEmpty() ? 0 : juce::jlimit (0, numBanks - 1, (int) (bankLetter[0] - 'A'));
    rig.number = juce::jlimit (1, maxNumber, xml.getIntAttribute ("number", 1));
    rig.favourite = xml.getBoolAttribute ("favourite");
    rig.tags = juce::StringArray::fromTokens (xml.getStringAttribute ("tags"), ",", {});
    rig.tags.trim();
    rig.tags.removeEmptyStrings();
    rig.notes = xml.getStringAttribute ("notes");

    for (const auto* parameter : xml.getChildWithTagNameIterator (parameterTag))
        if (const auto id = parameter->getStringAttribute ("id"); id.isNotEmpty())
            rig.parameters[id] = (float) parameter->getDoubleAttribute ("value");

    for (const auto* element : xml.getChildWithTagNameIterator (wavetableTag))
    {
        const auto oscillator = element->getIntAttribute ("osc") - 1;
        auto table = synth::wavetableFiles::decode (element->getStringAttribute ("name"), element->getIntAttribute ("frameSize"),
                                                    element->getAllSubText());
        if (oscillator >= 0 && oscillator < 8 && table.isValid())
            rig.wavetables[oscillator] = std::move (table);
    }
    for (const auto* element : xml.getChildWithTagNameIterator (drumSampleTag))
    {
        const auto slot = element->getIntAttribute ("slot") - 1;
        if (slot >= 0 && slot < synth::drums::numSlots)
            if (auto sample = synth::drums::sampleFiles::decode (element->getStringAttribute ("name"), element->getAllSubText()))
            {
                std::const_pointer_cast<synth::drums::Sample> (sample)->path = element->getStringAttribute ("path");
                rig.drumSamples[slot] = std::move (sample);
            }
    }
    for (const auto* element : xml.getChildWithTagNameIterator (spectrumTag))
    {
        const auto oscillator = element->getIntAttribute ("osc") - 1;
        const auto edit = synth::SpectrumEdit::decode (element->getStringAttribute ("gains"));
        if (oscillator >= 0 && oscillator < 8 && ! edit.isFlat())
            rig.spectra[oscillator] = edit;
    }
    for (const auto* element : xml.getChildWithTagNameIterator (freeformTag))
    {
        const auto oscillator = element->getIntAttribute ("osc") - 1;
        if (oscillator >= 0 && oscillator < 8)
            rig.drawings[oscillator] = synth::FreeformDrawing::decode (element->getStringAttribute ("drawing"));
    }
    if (const auto* modulation = xml.getChildByName ("Modulation"))
        rig.modulation = ModMatrix::fromXml (*modulation);

    return rig;
}
