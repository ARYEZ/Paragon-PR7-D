#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

// DOMINION's own presets (0.10.3), apart from PR7-E's rigs: a preset holds
// only DOMINION's settings (MODE, DEPTH, DETAIL, the timing, tilts, stereo,
// scale, output and the depth curve), never its on switch or anything else
// in the rig. The factory ones are built in, filed by category; yours are
// small files in a "Dominion Presets" folder beside the rigs.
struct DominionPreset
{
    juce::String name, category;
    // DOMINION's parameters, in their real units, by ID. Applying a preset
    // sets every one of DOMINION's settings: those it doesn't mention go to
    // their defaults (so a preset sounds the same whatever was there before).
    std::map<juce::String, float> parameters;
    bool isFactory = false;
    juce::File file;   // for your own
};

class DominionPresets
{
public:
    explicit DominionPresets (juce::File folder);

    static constexpr const char* fileExtension = ".pr7dom";

    // Every one of DOMINION's settings a preset holds: its parameters, but its on switch.
    static bool holds (const juce::String& parameterId);

    static const std::vector<DominionPreset>& factory();
    std::vector<DominionPreset> user() const;   // read from the folder, by name
    const DominionPreset* findFactory (const juce::String& name) const;

    // DOMINION's settings as they are now.
    static DominionPreset capture (juce::AudioProcessorValueTreeState&, const juce::String& name);
    // Sets DOMINION's settings to the preset's (each change a gesture, so it's one undo step).
    static void apply (const DominionPreset&, juce::AudioProcessorValueTreeState&);
    // Whether DOMINION's settings are still the preset's.
    static bool matches (const DominionPreset&, juce::AudioProcessorValueTreeState&);

    bool save (const DominionPreset&) const;   // as yours, by its name (replacing one of the same name)
    bool remove (const DominionPreset&) const;

    const juce::File& getFolder() const noexcept { return folder; }
    void setFolder (juce::File newFolder) { folder = std::move (newFolder); }

private:
    juce::File folder;
};
