#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

#include <set>

// ORACLE's notes, as a piano roll: what was heard (its pitch, a thin line)
// under the notes found in it, with the song's bars along the top.
//
//   - Click a note to select it (Shift or Ctrl: add to the selection), or
//     drag across empty space to select several.
//   - Drag a note up or down: its pitch. Drag its left or right end: where
//     it starts or ends. Alt-drag: move it in time.
//   - Double-click a note: split it there. Double-click empty space: add a
//     note. J: join. Delete: remove. Up and down: a semitone (Shift: an octave).
//   - Ctrl+Z, Ctrl+Shift+Z: undo, redo (ORACLE's own). Ctrl+A: select all. F: fit.
//   - Mouse wheel: along the song. Shift: up and down. Ctrl: zoom in time.
//     Alt: zoom in pitch.
class OracleRoll final : public juce::Component,
                         private juce::Timer
{
public:
    explicit OracleRoll (ParagonX1AudioProcessor&);

    const std::set<juce::uint32>& getSelection() const noexcept { return selection; }
    void setSelection (std::set<juce::uint32>);
    void selectAll();
    void fit();   // all the notes in view
    std::function<void()> onSelectionChanged;

    float xForSeconds (double seconds) const noexcept;
    double secondsForX (float x) const noexcept;
    float yForPitch (double pitch) const noexcept;   // the middle of its row
    double pitchForY (float y) const noexcept;
    juce::Rectangle<float> rectFor (const oracle::Note&) const noexcept;

    // Where the song is shown: from `start`, `seconds` long.
    void setView (double start, double seconds);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { none, pitch, move, start, end, select };

    void timerCallback() override;
    oracle::Transcriber& transcriber() { return processor.getOracle(); }
    const oracle::Note* noteAt (juce::Point<float>) const;
    Drag dragFor (const oracle::Note&, juce::Point<float>) const;
    std::vector<juce::uint32> selected() const { return { selection.begin(), selection.end() }; }
    void showMenu (const oracle::Note&, juce::Point<float>);
    void fitPitch();

    ParagonX1AudioProcessor& processor;
    juce::Rectangle<float> plot, keys, ruler;
    double viewStart = 0.0, viewSeconds = 12.0, lowPitch = 52.0, highPitch = 76.0;
    bool following = true, pitchFitted = false, wasHearing = false;
    juce::uint32 shownVersion = 0;
    std::set<juce::uint32> selection;

    Drag drag = Drag::none;
    bool edited = false;
    juce::uint32 grabbed = 0;
    juce::Point<float> dragFrom, dragTo;
    std::vector<oracle::Note> beforeDrag;
};

// The tile to drag the MIDI file from, onto a track in the DAW. Clicking it
// instead shows the file in its folder (for a DAW that won't take a drag).
class MidiDragTile final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit MidiDragTile (ParagonX1AudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::File write() const;
    ParagonX1AudioProcessor& processor;
    bool dragging = false;
};

// ORACLE's own page (stomp 1), over the pedal and dock, as the EQ's and
// DOMINION's are: ON and what it's hearing, SENSITIVITY, MIN NOTE and
// QUANTIZE, undo, the MIDI to drag; the piano roll; the selected note.
class OraclePage final : public juce::Component,
                         private juce::Timer
{
public:
    explicit OraclePage (ParagonX1AudioProcessor&);

    std::function<void()> onClose;
    OracleRoll& getRoll() noexcept { return roll; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applySettings();

    ParagonX1AudioProcessor& processor;
    OracleRoll roll;
    MidiDragTile midi;

    juce::ToggleButton onButton { "ON" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment;
    juce::Slider sensitivity { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow },
                 minNote { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::ComboBox quantize;
    juce::Label sensitivityLabel { {}, "SENSITIVITY" }, minNoteLabel { {}, "MIN NOTE" }, quantizeLabel { {}, "QUANTIZE" };
    juce::TextButton undo { "UNDO" }, redo { "REDO" }, clear { "CLEAR" }, fit { "FIT" }, back { "BACK TO PEDAL" };

    juce::Rectangle<int> topBar, bottomBar, statusArea, nowArea;
    juce::uint32 shownVersion = 0;
};
