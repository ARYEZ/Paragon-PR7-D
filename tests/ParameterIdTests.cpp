// Every parameter ID is the synth's or the rack's alone. A release build doesn't check this (JUCE's check is a debug
// assertion), and two parameters sharing an ID would save, load and automate one as the other.
#include "PluginProcessor.h"
#include <catch2/catch_test_macros.hpp>
#include <set>

TEST_CASE ("Parameter IDs: none shared between the synth and the rack", "[processor]")
{
    ParagonX1AudioProcessor processor;
    std::set<juce::String> ids;
    for (auto* parameter : static_cast<juce::AudioProcessor&> (processor).getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
        {
            INFO (withId->paramID);
            CHECK (ids.insert (withId->paramID).second);
        }
    CHECK (ids.size() > 1700);
}
