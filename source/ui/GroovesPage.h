// THE GROOVES page (PR7-D 0.4.0, plan stage d part 1): the player's own groove folders, browsed and previewed through
// the kit, and dragged out to a DAW track or the desktop. Our own design, inspired by the groove browsers of commercial
// drum software: the folders on the left (each one a filter), the grooves on the right with what their files tell
// (time signature, bars, tempo, the power hand, intensity), the preview's settings above.
#pragma once

#include "PluginProcessor.h"

class GroovesPage final : public juce::Component,
                          public juce::DragAndDropContainer,
                          private juce::TableListBoxModel,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit GroovesPage (ParagonX1AudioProcessor&);
    ~GroovesPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // For tests and snapshots.
    void refresh();                                   // the folders and the list, afresh
    const std::vector<grooves::Entry>& getResults() const noexcept { return results; }
    void chooseFolder (const juce::File&);            // as clicking it in the tree (nothing: all)
    void setSearch (const juce::String&);
    void playRow (int row, bool loop);
    // THE FILTER COLUMNS (0.5.0): a value chosen (`exclude`: excluded) or let go, as clicking (right-clicking) it does.
    void toggleFilter (grooves::Column, const juce::String& value, bool exclude);
    void clearFilters();
    const grooves::Filters& getFilters() const noexcept { return filters; }
    juce::StringArray getColumnValues (grooves::Column) const;   // as listed, "value (count)"
    // INTO THE SEQUENCER (0.5.0): the chosen groove as pattern A-D, as its button does.
    void toPattern (int pattern);
    juce::TableListBox& getTable() noexcept { return table; }
    juce::TreeView& getTree() noexcept { return tree; }

    enum Column { favourite = 1, name, family, signature, bars, tempo, hand, intensity };

private:
    // TableListBoxModel: the grooves.
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void sortOrderChanged (int, bool) override {}
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override;
    juce::String getCellTooltip (int row, int column) override;

    // Dragged out of the window: the groove as a file.
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&, juce::StringArray& files,
                                               bool& canMoveFiles) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildTree();
    void requery();
    void savePreview();
    int folderIndexOf (const juce::File&) const;   // which linked folder holds it (-1: none)
    std::optional<grooves::Groove> readRow (int row) const;

    class FolderItem;
    class FilterColumn;
    std::array<std::unique_ptr<FilterColumn>, grooves::numColumns> filterColumns;
    grooves::Filters filters;
    juce::TextButton clearFiltersButton { "CLEAR" };
    juce::Label toSequencerLabel { {}, "TO SEQUENCER" };
    std::array<juce::TextButton, 4> patternButtons;
    juce::String message;            // a moment's note in the status line ("Verse 01 is pattern B now")
    double messageUntil = 0.0;
    ParagonX1AudioProcessor& processor;
    grooves::Library& library;

    juce::TextButton addFolder { "ADD FOLDER..." }, removeFolder { "REMOVE" }, subfolders { "SUBFOLDERS" },
                     favouritesOnly { "ONLY" }, stop { "STOP" };
    juce::ComboBox layout, sort, tempoSource, factor, dragAs;
    juce::Label layoutLabel { {}, "FILES' MAP" }, sortLabel { {}, "SORT" }, tempoLabel { {}, "TEMPO" }, velocityLabel { {}, "VELOCITY" },
                dragLabel { {}, "DRAG" }, status, playing;
    juce::Slider velocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextEditor search;
    juce::TreeView tree;
    std::unique_ptr<juce::TreeViewItem> treeRoot;
    juce::TableListBox table { "Grooves", this };
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<grooves::Entry> results;
    juce::File within;           // the folder chosen in the tree (nothing: all)
    bool scannedOnce = false;
    juce::File shownPlaying;
    double shownPosition = -2.0;
};
