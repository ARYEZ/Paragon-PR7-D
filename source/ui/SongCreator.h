// THE SONG CREATOR (PR7-D 0.10.0, the song track part 3): over the page, the song track below it. Our own design, inspired
// by the song creator in chapter 7 of the manual the owner sent.
//
// The SOURCE: a groove dropped here (from the GROOVES list, or a MIDI file in a linked folder), or right-click a groove
// (or a song block): USE WITH SONG CREATOR. Its song folder's grooves in columns by song part: click one to hear it,
// double-click (or drag it onto the song track) to add it to the song; right-click for COPY, REPLACE ALL "..." ON TRACK
// and REPLACE THE CHOSEN BLOCK. On the left, SONG STRUCTURES (ours and yours): MAKE SONG (the song made from the columns,
// in place of the one there), ADD TO END, and SAVE SONG'S STRUCTURE... (the song track's parts and lengths, no notes).
#pragma once

#include "PluginProcessor.h"

class SongCreator final : public juce::Component,
                          public juce::DragAndDropTarget,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener
{
public:
    explicit SongCreator (ParagonX1AudioProcessor&);
    ~SongCreator() override;

    void open();
    std::function<void()> onClose;
    std::function<int()> chosenBlock;                       // the song track's chosen block (-1: none)
    std::function<void (int row)> onGrooveRowDropped;       // a groove dragged from the GROOVES list: MainView finds its file

    void setSource (const juce::File&);
    juce::File getSource() const { return source; }
    const song::Columns& getColumns() const noexcept { return columns; }
    static const std::array<grooves::Part, 8>& shownParts();   // the columns, left to right

    // STRUCTURES.
    const std::vector<song::Structure>& getStructures() const noexcept { return structures; }
    int getChosenStructure() const noexcept { return chosenStructure; }
    void chooseStructure (int);
    void makeSong (bool addToEnd);                          // MAKE SONG / ADD TO END with the chosen structure
    bool saveStructure (const juce::String& name);          // the song track's
    juce::PopupMenu structureMenu (int);

    // THE COLUMNS' GROOVES (part: grooves::Part; index: in its column).
    void audition (int part, int index);
    int addToSong (int part, int index, double beat = -1.0);
    int replaceAll (int part, int index);                   // how many blocks it replaced
    bool replaceChosenBlock (int part, int index);
    juce::PopupMenu grooveMenu (int part, int index);
    juce::Rectangle<int> cellOf (int part, int index) const;
    // (0.13.0) A groove of its columns dragged onto the sequencer's grid: in place of pattern `pattern`.
    bool toPattern (int part, int index, int pattern);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshStructures();
    std::pair<int, int> grooveAt (juce::Point<int>) const;   // (part, index), part -1: none
    int structureAt (juce::Point<int>) const;
    const grooves::Entry* entry (int part, int index) const;
    void askName (const juce::String& title, const juce::String& start, std::function<void (juce::String)> then);

    ParagonX1AudioProcessor& processor;
    juce::File source;
    song::Columns columns;
    std::vector<song::Structure> structures;
    int chosenStructure = 0;
    std::pair<int, int> chosenGroove { -1, 0 };
    juce::Rectangle<int> structureArea, columnArea, sourceArea;
    bool dragging = false;

    juce::TextButton close { "CLOSE" }, make { "MAKE SONG" }, addEnd { "ADD TO END" }, save { "SAVE SONG'S STRUCTURE..." };
    std::unique_ptr<juce::AlertWindow> nameWindow;
};
