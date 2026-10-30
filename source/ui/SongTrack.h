// THE SONG TRACK's strip (PR7-D 0.8.0): the song's blocks in a row, bar by bar, where the on-screen keyboard is (KEYS |
// SONG beside it switches). Our own design, inspired by the song track of commercial drum software.
//
// Drag a groove from the GROOVES list (or a MIDI file) onto it: a block. Drag a block to move it; drag its right edge to
// make it longer (the groove repeats) or shorter. Click a block to choose it (DELETE removes it); right-click for
// DUPLICATE, REMOVE, MUTE, SONG PART and CHANGE TEMPO. Click the bar numbers to put PLAY's start there. While the DAW
// plays, the song plays with it (bar 1 is the DAW's bar 1); PLAY plays it with the DAW stopped. EXPORT MIDI: the song
// as one MIDI file (click to save, drag to a DAW track).
#pragma once

#include "PluginProcessor.h"
#include "ui/DrumSeqPage.h"

class SongTrack final : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::FileDragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit SongTrack (ParagonX1AudioProcessor&);
    ~SongTrack() override;

    // A groove dragged from the GROOVES list ("groove:<row>") dropped at a beat: MainView asks the page for it.
    std::function<void (int row, double beat)> onGrooveDropped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // For tests and snapshots.
    int getChosen() const noexcept { return chosen; }
    void choose (int block) { chosen = block; repaint(); }
    double beatAt (float x) const noexcept;      // on the lane, not snapped
    float xOf (double beat) const noexcept;
    juce::Rectangle<int> getLane() const noexcept { return lane; }
    void showMenuFor (int block, double atBeat = -1.0);   // as right-clicking it (at that beat: SPLIT HERE's place)
    juce::PopupMenu menuFor (int block, double atBeat = -1.0);
    // EDIT BLOCK... (0.9.0), or a double-click on a block: MainView opens the block editor on it.
    std::function<void (int block)> onEditBlock;
    double getStartBeat() const noexcept { return startBeat; }
    juce::TextButton& getPlayButton() noexcept { return play; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void edit (const std::function<void (song::Song&)>&);   // the song changed by `f`, as an undo step
    bool nearRightEdge (int block, juce::Point<float>) const;
    double beatsShown() const noexcept;
    void updateStatus();

    ParagonX1AudioProcessor& processor;
    juce::TextButton play { "PLAY" };
    MidiExportButton exportButton;
    juce::Label status;
    juce::Rectangle<int> lane, ruler;
    int chosen = -1;
    double startBeat = 0.0;   // where PLAY starts (the bar numbers clicked)
    // A block being moved or made longer, and where it would go.
    enum class Drag { none, move, resize } drag = Drag::none;
    double grabOffset = 0.0, dragBeat = 0.0;
    double dropBeat = -1.0;   // a groove or file over the lane: where it would go (-1: none)
    double shownPosition = -2.0;
};
