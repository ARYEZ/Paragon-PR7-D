#pragma once

#include "PluginProcessor.h"
#include "ui/SynthControls.h"

// The ARP page (stage 5a, 5b): the 8 arpeggiators, a tab each.
//
//   ARP 1-8 | PATTERN A B C D, CHAIN | -1 +1 | START FROM | the oscillators it plays
//   the pattern: blocks on a grid of steps (across) and transposition (up and down),
//   with each block's VELOCITY, SLIDE and RATCHET in lanes under it (CHORDS mode: its CHORD, TONES and SUB too);
//   beside it the NOTES column: NOTE SOURCE, the RANDOMIZER, EXPORT MIDI, ARPS TOGETHER
//   its controls: ON, MODE, SPEED, TIMING, OCTAVES, GATE, LENGTH, VELOCITY,
//                 START / END NOTE, HUMANIZE, STRUM, TRIGGER, SHUFFLE

// The pattern editor, as Avenger's:
//   - click an empty place to put a block there (at that transposition);
//     drag it up or down to transpose it, its right edge to lengthen it
//   - right-click a block to delete it
//   - VELOCITY lane: drag a block's bar; SLIDE lane: click to glide into the
//     next block; RATCHET lane: click for more repeats, right-click for fewer
//   - CHORDS mode: CHORD lane click for its type; TONES click one on or off;
//     SUB click for a note under the chord, right-click for how far under
// Blocks on an octave are orange, the rest amber (Avenger's green and yellow).
class ArpPatternEditor final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::Timer
{
public:
    explicit ArpPatternEditor (ParagonX1AudioProcessor&);
    void setTarget (int arp, int pattern);
    int getArp() const noexcept { return arp; }
    int getPattern() const noexcept { return pattern; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int lowestTranspose = -24, highestTranspose = 24;

    // Where things are (for the tests), and the pattern now / a change to it.
    int getLength() const;
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> cell (int step, int transpose) const;
    juce::Rectangle<float> velocityArea (int step) const;
    juce::Rectangle<float> slideArea (int step) const;
    juce::Rectangle<float> ratchetArea (int step) const;
    juce::Rectangle<float> chordArea (int step) const;   // CHORDS mode's lanes
    juce::Rectangle<float> tonesArea (int step) const;
    juce::Rectangle<float> toneArea (int step, int tone) const;
    juce::Rectangle<float> subArea (int step) const;
    bool showsChords() const;
    synth::arp::Pattern getShown() const;
    void setShown (const synth::arp::Pattern&, bool finished);

private:
    void timerCallback() override;
    int stepAt (float x) const;
    int transposeAt (float y) const;

    ParagonX1AudioProcessor& processor;
    int arp = 0, pattern = 0;
    synth::arp::Pattern shown;
    std::vector<float> lastSettings;
    // A drag: which block, and what it moves (0 transposition, 1 length, 2 velocity).
    int dragStep = -1, dragWhat = 0;
};

// The randomizer's chances: a bar each (0 to 100%), dragged.
class ChanceBars final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    ChanceBars (const juce::String& title, const juce::StringArray& names);
    std::vector<float> values;
    std::vector<bool> dimmed;   // out of the scale: no chance whatever the bar says
    std::function<void (int index, float value, bool finished)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Rectangle<float> barArea (int index) const;

private:
    void setFrom (juce::Point<float>, bool finished);
    juce::String title;
    juce::StringArray names;
    int dragging = -1;
};

// EXPORT MIDI: click to save the arp's notes as a MIDI file, or drag it onto a track.
class MidiExportButton final : public juce::TextButton
{
public:
    explicit MidiExportButton (ParagonX1AudioProcessor&);
    int arp = 0;
    // Another source (THE DRUMS' sequencer): the file name offered, and what makes the file to drag or save.
    juce::String fileName;
    std::function<juce::File()> makeDragFile;
    std::function<bool (const juce::File&)> exportTo;
    void mouseDrag (const juce::MouseEvent&) override;
    void clicked() override;

private:
    ParagonX1AudioProcessor& processor;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragged = false;
};

// The NOTES column beside the pattern (stage 5b): NOTE SOURCE, the note RANDOMIZER, EXPORT MIDI, ARPS TOGETHER.
class ArpNotesPanel final : public juce::Component,
                            private juce::Timer
{
public:
    explicit ArpNotesPanel (ParagonX1AudioProcessor&);
    void setArp (int arp);
    std::function<void()> onRoll;   // ROLL: the page rolls a new pattern

    void paint (juce::Graphics&) override;
    void resized() override;

    synth::arp::Randomizer get() const;
    void set (const synth::arp::Randomizer&, bool finished);
    ChanceBars& getNotes() noexcept { return notes; }
    juce::TextButton& getRollButton() noexcept { return roll; }

private:
    void timerCallback() override;
    void refresh();

    ParagonX1AudioProcessor& processor;
    int arp = 0;
    synth::arp::Randomizer shown;
    juce::Label sourceLabel, randomLabel, scaleLabel, seedLabel, rollLabel;
    ParamControl noteSource, together;
    juce::TextButton roll { "ROLL" }, seedDown { "-" }, seedUp { "+" }, autoDice { "AUTODICE" }, symmetry { "SYMMETRY" };
    std::array<juce::TextButton, 5> rolls;   // what ROLL changes: NOTES OCT LEN VEL REST
    juce::ComboBox scale;
    ChanceBars notes, octaves, lengths, more;
    MidiExportButton exportMidi;
};

class ArpPage final : public juce::Component,
                      private juce::Timer
{
public:
    explicit ArpPage (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    void selectArp (int index);
    void selectPattern (int index);   // also the arp's PATTERN (the one it plays)
    int getSelectedArp() const noexcept { return selected; }
    ArpPatternEditor& getEditor() noexcept { return editor; }
    void shift (int semitones);       // every block of the pattern shown up or down
    void startFrom (int choice);
    void route (int oscillator, bool toThisArp);   // the oscillator's notes from this arp, or straight from the keys
    bool isRouted (int oscillator) const;
    void rollPattern();                            // the RANDOMIZER's ROLL: a new seed, a new pattern
    ArpNotesPanel& getNotesPanel() noexcept { return notesPanel; }

private:
    void timerCallback() override;
    std::vector<ParamControl*> controls();

    ParagonX1AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    struct ArpTab final : public juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool lit = false;
    };
    std::vector<std::unique_ptr<ArpTab>> arpTabs;
    std::vector<std::unique_ptr<juce::TextButton>> patternButtons, oscillatorButtons;
    juce::TextButton down { "-1" }, up { "+1" };
    juce::ComboBox startFromBox;
    juce::Label patternLabel, startLabel, oscillatorLabel, hint;
    ArpPatternEditor editor;
    ArpNotesPanel notesPanel;
    ParamControl on, mode, speed, timing, octaves, gate, length, velocity, lowKey, highKey, humanize, strum, trigger, shuffle, chain;
    int selected = 0;
};
