#pragma once

#include "PluginProcessor.h"
#include "dsp/synth/Drums.h"
#include "ui/SynthControls.h"

// THE DRUMS page (stage 6a): the kit's 12 slots down the left (a row each: its key, its drum, a picture of its hit,
// MUTE and SOLO; it lights when it plays; click it to choose it and hear it), and the chosen drum's settings on the
// right, with a large picture of its hit. Its pictures come from rendering a copy of the hit (as DRIVE's page does),
// redrawn when a setting changes. Drop an audio file on a row or on the picture to load it into that slot.

// A drum's hit as a picture: the peak level per column, and how long it lasts (to -60 dB, at most 4 s).
struct HitPicture
{
    std::vector<float> peaks;
    double seconds = 0.0;

    static HitPicture make (const synth::drums::SlotSettings&, int columns);
};

class DrumRow final : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::SettableTooltipClient
{
public:
    DrumRow (ParagonX1AudioProcessor&, int slot);
    std::function<void (int slot)> onSelect;

    void setSelected (bool);
    void setPicture (HitPicture);
    void setLit (float amount);        // 0 to 1: the hit's flash
    int getSlot() const noexcept { return slot; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    ParagonX1AudioProcessor& processor;
    int slot;
    bool selected = false;
    float lit = 0.0f;
    HitPicture picture;
    ParamControl mute, solo;
};

class DrumHitDisplay final : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public juce::SettableTooltipClient
{
public:
    explicit DrumHitDisplay (ParagonX1AudioProcessor&);
    void setSlot (int);
    void setPicture (HitPicture);
    const HitPicture& getPicture() const noexcept { return picture; }

    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    ParagonX1AudioProcessor& processor;
    int slot = 0;
    HitPicture picture;
};

class DrumPage final : public juce::Component,
                       private juce::Timer
{
public:
    explicit DrumPage (ParagonX1AudioProcessor&);
    ~DrumPage() override;

    void select (int slot);
    int getSelected() const noexcept { return selected; }
    DrumRow& getRow (int slot) noexcept { return *rows[(size_t) slot]; }
    DrumHitDisplay& getDisplay() noexcept { return display; }
    // The chosen drum's controls (for tests): whatever parameter each is pointed at now.
    std::vector<ParamControl*> getSlotControls();
    void refreshPictures();   // now, not on the next tick (tests and snapshots)
    void updateLights();      // the rows' hit lights, from the kit (the timer; tests and snapshots)

    void paint (juce::Graphics&) override;
    void resized() override;

    // Whether a file looks like audio the kit can load.
    static bool isAudioFile (const juce::String& path);

private:
    void timerCallback() override;
    void chooseSample();
    juce::String keyFor (int slot) const;   // what a slot's picture was made from

    ParagonX1AudioProcessor& processor;
    int selected = 0;
    std::vector<std::unique_ptr<DrumRow>> rows;
    std::array<juce::String, synth::drums::numSlots> pictureKeys;
    std::vector<std::unique_ptr<ParamControl>> kitControls, slotControls, routingControls;
    DrumHitDisplay display;
    juce::TextButton play { "PLAY" }, load { "LOAD SAMPLE..." }, clear { "CLEAR" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> kitArea, drumArea;
    int tick = 0;
};
