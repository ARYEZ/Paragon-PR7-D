#pragma once

#include "PluginProcessor.h"
#include "dsp/synth/Mixer.h"

// THE MIXER page (PR7-D 0.20.0): a multi-mic kit's mic channels, as a sampled drum kit's mixer has them (inspired by
// the reference's mixer chapter; our own look and words). A strip per channel, left to right:
//   - BLEED: on, and its level (every other drum heard through this mic, together);
//   - PAN, the FADER (with its meter), M (mute), S (solo), the polarity (flip);
//   - OUTPUT (MAIN or OUT 2 to 16), and its name (click it to choose the channel).
// Beside them, the chosen channel's PROPERTIES: BLEED FROM INSTRUMENTS (each drum that bleeds into it: on, level,
// polarity) and CLOSE MIC AUDIO (the drums it's meant for), TIME OFFSET (the channel later, up to 10 ms, to line
// overheads up with close mics), RELEASE (each hit faded after this long: shorter room tails, a gated sound).
// Channels no loaded kit plays into are dimmed. Only multi-mic drums use the mixer; the rest go their own way.
class MixerPage final : public juce::Component,
                        private juce::Timer
{
public:
    explicit MixerPage (ParagonX1AudioProcessor&);
    ~MixerPage() override;

    void selectChannel (int channel);
    int getSelectedChannel() const noexcept { return selected; }
    juce::Array<int> getChannelsInUse() const;      // the channels a loaded kit plays into
    // For tests: a bleed cell set as its row's controls would (one undo step).
    void setBleed (int piece, float gainDb, bool invert, bool on);
    void update();   // the meters, and which channels are in use (the timer; snapshots)

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshProperties();

    class Strip;
    class PieceRow;
    ParagonX1AudioProcessor& processor;
    std::vector<std::unique_ptr<Strip>> strips;
    std::vector<std::unique_ptr<PieceRow>> rows;
    juce::Slider offset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 release { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> offsetAttachment, releaseAttachment;
    juce::Rectangle<int> stripsArea, propertiesArea;
    int selected = synth::mixer::snareTop;
    juce::Array<int> inUse;
};
