#include "GroovesPage.h"

#include <map>

#include "ui/DockPanels.h"
#include "ui/Icons.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int rowHeight = 26;
    const juce::String dragPrefix = "groove:";

    juce::String tempoText (const grooves::Entry& e)
    {
        // Whole numbers as such: a file keeps its tempo in microseconds a beat, so 104 reads back as 104.0001.
        const auto whole = std::abs (e.bpm - std::round (e.bpm)) < 0.05;
        return e.bpmKnown ? (whole ? juce::String (juce::roundToInt (e.bpm)) : juce::String (e.bpm, 1)) : juce::String ("-");
    }
}

//==============================================================================
// The tree: ALL GROOVES, then each linked folder, its subfolders opened as you go.
class GroovesPage::FolderItem final : public juce::TreeViewItem
{
public:
    FolderItem (GroovesPage& p, juce::File f, juce::String label) : page (p), folder (std::move (f)), text (std::move (label)) {}

    bool mightContainSubItems() override
    {
        if (folder == juce::File())
            return false;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findDirectories))
        {
            juce::ignoreUnused (entry);
            return true;
        }
        return false;
    }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || getNumSubItems() > 0)
            return;
        auto children = folder.findChildFiles (juce::File::findDirectories, false);
        children.sort();
        for (const auto& child : children)
            if (! child.isHidden())
                addSubItem (new FolderItem (page, child, child.getFileName()));
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
        {
            g.setColour (ParagonColours::accentDim.withAlpha (0.6f));
            g.fillRect (0, 1, width, height - 2);
        }
        g.setColour (isSelected() ? ParagonColours::accent : folder == juce::File() ? ParagonColours::text : ParagonColours::text.withAlpha (0.85f));
        g.setFont (juce::FontOptions (13.0f, folder == juce::File() || getParentItem() == nullptr || getParentItem()->getParentItem() == nullptr
                                                 ? juce::Font::bold : juce::Font::plain));
        g.drawText (text, 4, 0, width - 6, height, juce::Justification::centredLeft, true);
    }

    void paintOpenCloseButton (juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour, bool isMouseOver) override
    {
        // A small triangle: right when shut, down when open.
        const auto box = area.withSizeKeepingCentre (8.0f, 8.0f);
        juce::Path arrow;
        if (isOpen())
            arrow.addTriangle (box.getX(), box.getY() + 1.5f, box.getRight(), box.getY() + 1.5f, box.getCentreX(), box.getBottom() - 1.0f);
        else
            arrow.addTriangle (box.getX() + 1.5f, box.getY(), box.getX() + 1.5f, box.getBottom(), box.getRight() - 1.0f, box.getCentreY());
        g.setColour (isMouseOver ? ParagonColours::accent : ParagonColours::textDim);
        g.fillPath (arrow);
    }

    void itemSelectionChanged (bool selected) override
    {
        if (selected)
            page.chooseFolder (folder);
    }

    juce::String getUniqueName() const override { return folder.getFullPathName(); }
    int getItemHeight() const override { return 24; }
    const juce::File& getFolder() const noexcept { return folder; }

private:
    GroovesPage& page;
    juce::File folder;
    juce::String text;
};

//==============================================================================
// A filter column: its values (those the grooves listed have, with how many), click to choose, right-click to exclude.
class GroovesPage::FilterColumn final : public juce::Component, private juce::ListBoxModel
{
public:
    FilterColumn (GroovesPage& p, grooves::Column c) : page (p), column (c)
    {
        list.setModel (this);
        list.setRowHeight (20);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTooltip ("Click: only grooves with this (several in a column: any of them). Right-click: none with this. "
                         "The columns together: grooves that pass them all.");
        addAndMakeVisible (list);
    }

    // The values to list and their counts (in the grooves the folder, search and favourites leave).
    void setValues (juce::StringArray v, juce::Array<int> c)
    {
        values = std::move (v);
        counts = std::move (c);
        list.updateContent();
        list.repaint();
        repaint();
    }
    juce::StringArray describe() const
    {
        juce::StringArray result;
        for (int i = 0; i < values.size(); ++i)
            result.add (values[i] + " (" + juce::String (counts[i]) + ")");
        return result;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& f = page.filters;
        const auto active = f.include[(size_t) column].size() + f.exclude[(size_t) column].size();
        g.setColour (active > 0 ? ParagonColours::accent : ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (grooves::columnNames[(int) column] + (active > 0 ? "  " + juce::String (active) : juce::String()), 4, 0,
                    getWidth() - 8, 16, juce::Justification::centredLeft, false);
    }
    void resized() override { list.setBounds (getLocalBounds().withTrimmedTop (18)); }

private:
    int getNumRows() override { return values.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (row < 0 || row >= values.size())
            return;
        const auto& f = page.filters;
        const auto included = f.include[(size_t) column].contains (values[row]);
        const auto excluded = f.exclude[(size_t) column].contains (values[row]);
        if (included)
        {
            g.setColour (ParagonColours::accentDim.withAlpha (0.7f));
            g.fillRoundedRectangle (1.0f, 1.0f, (float) width - 2.0f, (float) height - 2.0f, 3.0f);
        }
        const auto colour = excluded ? ParagonColours::meterRed : included ? ParagonColours::accent : ParagonColours::text;
        g.setColour (colour);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (values[row], 6, 0, width - 46, height, juce::Justification::centredLeft, true);
        if (excluded)
            g.drawHorizontalLine (height / 2, 6.0f, 6.0f + (float) juce::jmin (width - 52, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.5f), values[row])));
        g.setColour (colour.withAlpha (0.6f));
        g.drawText (juce::String (counts[row]), width - 40, 0, 34, height, juce::Justification::centredRight, false);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row >= 0 && row < values.size())
            page.toggleFilter (column, values[row], e.mods.isPopupMenu());
    }

    GroovesPage& page;
    grooves::Column column;
    juce::ListBox list;
    juce::StringArray values;
    juce::Array<int> counts;
};

//==============================================================================
// GROOVE PARTS: ALL, then each part with how many hits the chosen groove has in it; click ticks it, a drag takes it out.
class GroovesPage::PartsList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PartsList (GroovesPage& p) : page (p)
    {
        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTooltip ("The chosen groove's parts. Ticked: heard in the preview, dragged out with it, laid into a pattern with "
                         "LAYER ONTO. Drag a part to take just it out.");
        addAndMakeVisible (list);
    }
    void refresh() { list.updateContent(); list.repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("GROOVE PARTS", 4, 0, getWidth() - 8, 16, juce::Justification::centredLeft, false);
    }
    void resized() override { list.setBounds (getLocalBounds().withTrimmedTop (18)); }

private:
    int getNumRows() override { return 1 + grooves::numDrumParts; }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        const auto part = row - 1;
        const auto ticked = part < 0 ? page.chosenParts == grooves::allParts : (page.chosenParts & (1u << part)) != 0;
        const auto count = part < 0 ? 0 : page.partCounts[(size_t) part];
        const auto used = part < 0 || count > 0;
        // The tick box.
        const auto box = juce::Rectangle<float> (6.0f, (float) height * 0.5f - 6.0f, 12.0f, 12.0f);
        g.setColour (ticked ? ParagonColours::accent : ParagonColours::textDim.withAlpha (0.6f));
        if (ticked)
            g.fillRoundedRectangle (box, 2.0f);
        else
            g.drawRoundedRectangle (box, 2.0f, 1.0f);
        g.setColour (! used ? ParagonColours::textDim.withAlpha (0.5f) : ticked ? ParagonColours::text : ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.5f, part < 0 ? juce::Font::bold : juce::Font::plain));
        g.drawText (part < 0 ? juce::String ("ALL") : grooves::drumPartNames[part], 26, 0, width - 64, height, juce::Justification::centredLeft, true);
        if (part >= 0 && count > 0)
            g.drawText (juce::String (count), width - 40, 0, 34, height, juce::Justification::centredRight, false);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { page.toggleDrumPart (row - 1); }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        return rows.isEmpty() || rows[0] < 1 ? juce::var() : juce::var ("part:" + juce::String (rows[0] - 1));
    }

    GroovesPage& page;
    juce::ListBox list;
};

//==============================================================================
GroovesPage::GroovesPage (ParagonX1AudioProcessor& p) : processor (p), library (p.getGrooveLibrary())
{
    addFolder.setTooltip ("Link a folder of your own MIDI grooves (and its subfolders). They stay where they are: PR7-D reads "
                          "them from there. Grooves from other drum software: drag them out of it into a folder first.");
    addFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("A folder of MIDI grooves", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& c)
        {
            if (c.getResult().isDirectory() && library.addFolder (c.getResult()))
                refresh();
        });
    };
    removeFolder.setTooltip ("Unlink the chosen folder (its files stay on your computer).");
    removeFolder.onClick = [this]
    {
        if (const auto f = folderIndexOf (within); f >= 0)
        {
            library.removeFolder (f);
            within = juce::File();
            refresh();
        }
    };
    subfolders.setTooltip ("On: a folder lists its subfolders' grooves too. Off: only the files right in it.");
    subfolders.setClickingTogglesState (true);
    subfolders.setToggleState (library.getSubfolders(), juce::dontSendNotification);
    subfolders.onClick = [this] { library.setSubfolders (subfolders.getToggleState()); requery(); };
    favouritesOnly.setTooltip ("Only your favourites (click a groove's star to make it one).");
    favouritesOnly.setClickingTogglesState (true);
    favouritesOnly.onClick = [this] { requery(); };
    stop.setTooltip ("Stop the groove playing.");
    stop.onClick = [this] { processor.stopGroove(); };

    layout.addItemList (synth::drums::notemap::layoutNames, 1);
    layout.setTooltip ("Which drum each note in this folder's files is: GROOVE LIBRARY for most commercial groove packs, GENERAL MIDI "
                       "for GM files, CUSTOM for your own map (SETTINGS), ROOT for files made for this kit's keys.");
    layout.onChange = [this]
    {
        if (const auto f = folderIndexOf (within); f >= 0 && layout.getSelectedItemIndex() >= 0)
            library.setLayout (f, (synth::drums::notemap::Layout) layout.getSelectedItemIndex());
    };
    sort.addItemList (grooves::sortNames, 1);
    sort.setTooltip ("NAME: by folder, then name. NEAREST SONG TEMPO: the grooves closest to your song's tempo first. SONG TIME "
                     "SIGNATURE FIRST: those in your song's.");
    tempoSource.addItemList ({ "Song", "Groove's Own" }, 1);
    tempoSource.setTooltip ("The preview at your song's tempo, or at the tempo the groove was played at.");
    factor.addItemList (grooves::tempoFactorNames, 1);
    factor.setTooltip ("Half or double time, or 2/3 (a 4/4 groove felt in 6/8) and 3/2 (6/8 to 4/4): only how fast it plays; the "
                       "groove dragged out gets it too.");
    dragAs.addItemList ({ "For This Kit", "As Written" }, 1);
    dragAs.setTooltip ("FOR THIS KIT: a dragged groove's notes moved to this kit's map (SETTINGS), so it plays right here; kept as "
                       "they are if the folder's map is the kit's. AS WRITTEN: the file's own notes (for other drum software).");
    velocity.setRange (25.0, 175.0, 1.0);
    velocity.setTextValueSuffix ("%");
    velocity.setTooltip ("Every hit's velocity scaled together, so the playing keeps its accents: 100% as played.");
    const auto p0 = library.getPreview();
    sort.setSelectedItemIndex ((int) p0.sort, juce::dontSendNotification);
    tempoSource.setSelectedItemIndex (p0.songTempo ? 0 : 1, juce::dontSendNotification);
    factor.setSelectedItemIndex ((int) p0.factor, juce::dontSendNotification);
    dragAs.setSelectedItemIndex (p0.forThisKit ? 0 : 1, juce::dontSendNotification);
    velocity.setValue (p0.velocity * 100.0, juce::dontSendNotification);
    for (auto* box : { &sort, &tempoSource, &factor, &dragAs })
        box->onChange = [this] { savePreview(); };
    velocity.onDragEnd = [this] { savePreview(); };
    velocity.onValueChange = [this] { if (! velocity.isMouseButtonDown()) savePreview(); };

    search.setTextToShowWhenEmpty ("SEARCH", ParagonColours::textDim);
    search.setTooltip ("Words in a groove's name or its folder's (all of them).");
    search.onTextChange = [this] { requery(); };

    for (auto* l : { &layoutLabel, &sortLabel, &tempoLabel, &velocityLabel, &dragLabel })
    {
        l->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, ParagonColours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }
    status.setColour (juce::Label::textColourId, ParagonColours::textDim);
    status.setJustificationType (juce::Justification::centredRight);
    playing.setColour (juce::Label::textColourId, ParagonColours::accent);

    tree.setRootItemVisible (false);
    tree.setDefaultOpenness (false);
    tree.setColour (juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
    tree.setIndentSize (14);

    table.setRowHeight (rowHeight);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    auto& header = table.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, ParagonColours::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, ParagonColours::textDim);
    header.setColour (juce::TableHeaderComponent::outlineColourId, ParagonColours::panelEdge);
    const auto flags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable;
    header.addColumn ({}, favourite, 30, 30, 30, juce::TableHeaderComponent::visible);
    header.addColumn ("NAME", name, 250, 80, 600, flags);
    header.addColumn ("FOLDER", family, 250, 80, 600, flags);
    header.addColumn ("SIGN", signature, 60, 40, 100, flags);
    header.addColumn ("BARS", bars, 56, 40, 100, flags);
    header.addColumn ("TEMPO", tempo, 70, 50, 120, flags);
    header.addColumn ("POWER HAND", hand, 120, 60, 200, flags);
    header.addColumn ("INTENSITY", intensity, 120, 60, 240, flags);
    header.addColumn ("MATCH", matchScore, 80, 50, 140, flags);
    header.setStretchToFitActive (true);   // the columns share the list's width (no scrolling sideways)
    table.setMultipleSelectionEnabled (false);

    for (juce::Component* c : { (juce::Component*) &addFolder, (juce::Component*) &removeFolder, (juce::Component*) &subfolders,
                                (juce::Component*) &layoutLabel, (juce::Component*) &layout, (juce::Component*) &search,
                                (juce::Component*) &favouritesOnly, (juce::Component*) &sortLabel, (juce::Component*) &sort,
                                (juce::Component*) &status, (juce::Component*) &tempoLabel, (juce::Component*) &tempoSource,
                                (juce::Component*) &factor, (juce::Component*) &velocityLabel, (juce::Component*) &velocity,
                                (juce::Component*) &stop, (juce::Component*) &dragLabel, (juce::Component*) &dragAs,
                                (juce::Component*) &playing, (juce::Component*) &tree, (juce::Component*) &table })
        addAndMakeVisible (c);

    // THE FILTER COLUMNS and INTO THE SEQUENCER (0.5.0).
    for (int c = 0; c < grooves::numColumns; ++c)
    {
        filterColumns[(size_t) c] = std::make_unique<FilterColumn> (*this, (grooves::Column) c);
        addAndMakeVisible (*filterColumns[(size_t) c]);
    }
    clearFiltersButton.setTooltip ("Every filter column back to all.");
    clearFiltersButton.onClick = [this] { clearFilters(); };
    addAndMakeVisible (clearFiltersButton);
    toSequencerLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    toSequencerLabel.setColour (juce::Label::textColourId, ParagonColours::textDim);
    toSequencerLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (toSequencerLabel);
    for (int i = 0; i < (int) patternButtons.size(); ++i)
    {
        auto& b = patternButtons[(size_t) i];
        b.setButtonText (synth::drumseq::patternNames[i]);
        b.setTooltip ("The chosen groove as the drum sequencer's pattern " + synth::drumseq::patternNames[i] + " (replacing it): each "
                      "hit a note on its drum, where it was played, with its velocity, TEMPO's factor and VELOCITY. LENGTH becomes "
                      "the groove's (16 beats at most: a longer groove brings its first 16), and the sequencer plays it. Undo "
                      "takes it back.");
        b.onClick = [this, i] { toPattern (i); };
        addAndMakeVisible (b);
    }

    // TAP TO FIND (0.6.0).
    tapButton.setTooltip ("Tap a rhythm (on pads, your controller or the keyboard) over a two-bar loop, and list the grooves "
                          "closest to it first. Or drop a MIDI file on the page, or right-click a groove: FIND SIMILAR.");
    tapButton.onClick = [this] { openTapPanel(); };
    clearTapButton.setTooltip ("Back to the list as it was (sorted by SORT, no MATCH).");
    clearTapButton.onClick = [this] { clearTap(); };
    tapInfo.setColour (juce::Label::textColourId, ParagonColours::textDim);
    tapInfo.setFont (juce::FontOptions (11.5f));
    tapInfo.setJustificationType (juce::Justification::topLeft);
    tapInfo.setMinimumHorizontalScale (1.0f);
    for (juce::Component* c : { (juce::Component*) &tapButton, (juce::Component*) &clearTapButton, (juce::Component*) &tapInfo })
        addAndMakeVisible (c);
    tapPanel = std::make_unique<TapPanel> (processor);
    tapPanel->onFind = [this] (const grooves::Rhythm& r, double grid)
    {
        tapPanel->close();
        findByRhythm (r, grid, "your taps");
    };
    addChildComponent (*tapPanel);

    // GROOVE PARTS (0.7.0).
    partsList = std::make_unique<PartsList> (*this);
    addAndMakeVisible (*partsList);
    layerLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    layerLabel.setColour (juce::Label::textColourId, ParagonColours::textDim);
    addAndMakeVisible (layerLabel);
    for (int i = 0; i < (int) layerButtons.size(); ++i)
    {
        auto& b = layerButtons[(size_t) i];
        b.setButtonText (synth::drumseq::patternNames[i]);
        b.setTooltip ("Lay the ticked parts of the chosen groove into pattern " + synth::drumseq::patternNames[i] + ": its notes on "
                      "those drums replaced by the groove's (repeated to fill the pattern's LENGTH), its other drums kept. "
                      "Undo takes it back.");
        b.onClick = [this, i] { layerOnto (i); };
        addAndMakeVisible (b);
    }

    library.addChangeListener (this);
    refresh();
    startTimerHz (15);
}

GroovesPage::~GroovesPage()
{
    // The window closing: the preview stops (nothing would be left to stop it with).
    processor.stopGroove();
    library.removeChangeListener (this);
    tree.setRootItem (nullptr);
}

void GroovesPage::visibilityChanged()
{
    // The folders are read again the first time the page is shown (what was read last time is listed meanwhile).
    if (isShowing() && ! scannedOnce)
    {
        scannedOnce = true;
        library.loadIndex();
        refresh();
        if (! library.getFolders().empty())
            library.scan();
    }
}

void GroovesPage::refresh()
{
    rebuildTree();
    requery();
}

void GroovesPage::rebuildTree()
{
    tree.setRootItem (nullptr);
    treeRoot = std::make_unique<FolderItem> (*this, juce::File(), juce::String());
    treeRoot->addSubItem (new FolderItem (*this, juce::File(), "ALL GROOVES"));
    for (const auto& f : library.getFolders())
        treeRoot->addSubItem (new FolderItem (*this, f.folder, f.folder.getFileName()));
    // The folder chosen before, if it's still there (opening the way to it); else ALL GROOVES. Opened before the tree
    // gets them, so it lays them out once, as they'll be.
    auto* item = treeRoot->getSubItem (0);
    if (within != juce::File())
        for (int i = 1; i < treeRoot->getNumSubItems(); ++i)
        {
            auto* linked = dynamic_cast<FolderItem*> (treeRoot->getSubItem (i));
            if (within == linked->getFolder())
                item = linked;
            else if (within.isAChildOf (linked->getFolder()))
            {
                juce::TreeViewItem* at = linked;
                while (at != nullptr && dynamic_cast<FolderItem*> (at)->getFolder() != within)
                {
                    at->setOpen (true);
                    juce::TreeViewItem* next = nullptr;
                    for (int c = 0; c < at->getNumSubItems() && next == nullptr; ++c)
                        if (const auto& f = dynamic_cast<FolderItem*> (at->getSubItem (c))->getFolder(); within == f || within.isAChildOf (f))
                            next = at->getSubItem (c);
                    at = next;
                }
                if (at != nullptr)
                    item = at;
            }
        }
    tree.setRootItem (treeRoot.get());
    if (item != nullptr)
        item->setSelected (true, true, juce::dontSendNotification);
    if (item == treeRoot->getSubItem (0))
        within = juce::File();
    const auto f = folderIndexOf (within);
    layout.setEnabled (f >= 0);
    removeFolder.setEnabled (f >= 0);
    layout.setSelectedItemIndex (f >= 0 ? (int) library.getFolders()[(size_t) f].layout : -1, juce::dontSendNotification);
}

void GroovesPage::chooseFolder (const juce::File& folder)
{
    within = folder;
    const auto f = folderIndexOf (within);
    layout.setEnabled (f >= 0);
    removeFolder.setEnabled (f >= 0);
    layout.setSelectedItemIndex (f >= 0 ? (int) library.getFolders()[(size_t) f].layout : -1, juce::dontSendNotification);
    requery();
}

void GroovesPage::setSearch (const juce::String& text)
{
    search.setText (text, juce::sendNotificationSync);
    requery();
}

int GroovesPage::folderIndexOf (const juce::File& f) const
{
    const auto& folders = library.getFolders();
    for (int i = 0; i < (int) folders.size(); ++i)
        if (f == folders[(size_t) i].folder || f.isAChildOf (folders[(size_t) i].folder))
            return i;
    return -1;
}

void GroovesPage::requery()
{
    // The grooves the folder, words and favourites leave; the filter columns list their values (with how many have
    // each), and the list shows those that pass the filters.
    const auto p = library.getPreview();
    const auto scope = library.query (within, search.getText(), favouritesOnly.getToggleState(), p.sort, processor.getHostBpm(),
                                      processor.getHostNumerator(), processor.getHostDenominator(), {},
                                      tapSearch.has_value() ? &*tapSearch : nullptr);
    results.clear();
    for (const auto& e : scope)
        if (filters.passes (e))
            results.push_back (e);
    for (int c = 0; c < grooves::numColumns; ++c)
    {
        const auto column = (grooves::Column) c;
        std::map<juce::String, int> counts;
        for (const auto& e : scope)
            ++counts[grooves::valueOf (e, column)];
        auto order = grooves::valuesOf (column);
        if (order.isEmpty())   // TIME: the signatures there are, simplest first
        {
            for (const auto& [value, n] : counts)
                order.add (value);
            std::sort (order.begin(), order.end(), [] (const juce::String& a, const juce::String& b)
            {
                const auto da = a.fromFirstOccurrenceOf ("/", false, false).getIntValue(), db = b.fromFirstOccurrenceOf ("/", false, false).getIntValue();
                return da != db ? da < db : a.getIntValue() < b.getIntValue();
            });
        }
        juce::StringArray values;
        juce::Array<int> numbers;
        for (const auto& v : order)
        {
            const auto found = counts.find (v);
            const auto n = found != counts.end() ? found->second : 0;
            // Listed if some grooves have it, or it's chosen or excluded (so it can be let go).
            if (n > 0 || filters.include[(size_t) c].contains (v) || filters.exclude[(size_t) c].contains (v))
            {
                values.add (v);
                numbers.add (n);
            }
        }
        filterColumns[(size_t) c]->setValues (values, numbers);
    }
    clearFiltersButton.setEnabled (! filters.isEmpty());
    clearTapButton.setEnabled (tapSearch.has_value());
    tapInfo.setText (tapSearch.has_value() ? "Closest first to " + tapWhat + " (" + juce::String (tapSearch->rhythm.numHits()) + " hits)"
                                           : juce::String ("Or drop a MIDI file here, or right-click a groove"),
                     juce::dontSendNotification);
    table.updateContent();
    table.repaint();
    timerCallback();
}

void GroovesPage::toggleFilter (grooves::Column column, const juce::String& value, bool exclude)
{
    auto& include = filters.include[(size_t) column];
    auto& excluded = filters.exclude[(size_t) column];
    auto& into = exclude ? excluded : include;
    auto& other = exclude ? include : excluded;
    other.removeString (value);
    if (into.contains (value))
        into.removeString (value);
    else
        into.add (value);
    requery();
}

void GroovesPage::clearFilters()
{
    filters = {};
    requery();
}

juce::StringArray GroovesPage::getColumnValues (grooves::Column c) const
{
    return filterColumns[(size_t) c]->describe();
}

void GroovesPage::toPattern (int pattern)
{
    const auto row = table.getSelectedRow();
    const auto g = readRow (row);
    if (! g.has_value())
    {
        message = "Choose a groove first (click it in the list)";
    }
    else
    {
        const auto r = processor.grooveToPattern (*g, results[(size_t) row].folder, pattern);
        message = g->name + " is pattern " + synth::drumseq::patternNames[pattern] + " now (" + juce::String (r.lengthBeats) + " beats)";
        if (r.pastEnd > 0)
            message << ", its first 16 beats";
        if (r.noDrum > 0)
            message << "; " << r.noDrum << (r.noDrum == 1 ? " note" : " notes") << " no drum plays";
    }
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    timerCallback();
}

void GroovesPage::openTapPanel()
{
    processor.stopGroove();   // the loop and its metronome alone
    const auto perBar = processor.getHostNumerator() * 4.0 / juce::jmax (1, processor.getHostDenominator());
    tapPanel->open (processor.getHostBpm(), perBar);
    tapPanel->toFront (false);
}

void GroovesPage::findByRhythm (const grooves::Rhythm& rhythm, double grid, const juce::String& what)
{
    if (rhythm.isEmpty())
        return;
    tapSearch = grooves::Library::TapSearch { rhythm, grid };
    tapWhat = what;
    requery();
    table.scrollToEnsureRowIsOnscreen (0);
}

void GroovesPage::findSimilar (int row)
{
    if (row < 0 || row >= (int) results.size() || results[(size_t) row].rhythm == nullptr)
        return;
    const auto& e = results[(size_t) row];
    const auto perBar = e.numerator * 4.0 / juce::jmax (1, e.denominator);
    findByRhythm (grooves::firstBars (*e.rhythm, 2.0 * perBar), tapPanel->getGrid(), e.name);
}

bool GroovesPage::findLikeFile (const juce::File& file)
{
    // Its notes through the kit's map (a file made for this kit), or the groove libraries' layout under ROOT (where
    // dropped grooves most often come from).
    const auto layout = processor.getMapLayout() == synth::drums::notemap::Layout::root ? synth::drums::notemap::grooveLibrary()
                                                                                          : processor.getNoteMap();
    const auto g = grooves::read (file, layout);
    if (! g.has_value())
        return false;
    findByRhythm (grooves::firstBars (grooves::rhythmOf (*g, layout), 2.0 * g->beatsPerBar()), tapPanel->getGrid(),
                  file.getFileNameWithoutExtension());
    return true;
}

void GroovesPage::clearTap()
{
    tapSearch.reset();
    requery();
}

bool GroovesPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("mid;midi");
}

void GroovesPage::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty() && ! findLikeFile (juce::File (files[0])))
    {
        message = "That file has no notes to search with";
        messageUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    }
}

void GroovesPage::savePreview()
{
    auto p = library.getPreview();
    p.sort = (grooves::Sort) juce::jmax (0, sort.getSelectedItemIndex());
    p.songTempo = tempoSource.getSelectedItemIndex() != 1;
    p.factor = (grooves::TempoFactor) juce::jmax (0, factor.getSelectedItemIndex());
    p.forThisKit = dragAs.getSelectedItemIndex() != 1;
    p.velocity = (float) velocity.getValue() / 100.0f;
    const auto resort = p.sort != library.getPreview().sort;
    library.setPreview (p);
    processor.applyGroovePreview();
    if (resort)
        requery();
}

void GroovesPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Read further (or folders changed): the list afresh; the tree only if the folders did.
    if (treeRoot == nullptr || treeRoot->getNumSubItems() != (int) library.getFolders().size() + 1)
        rebuildTree();
    requery();
}

void GroovesPage::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() < messageUntil)
        status.setText (message, juce::dontSendNotification);
    else if (library.isScanning())
        status.setText ("Reading your grooves... " + juce::String (library.getNumScanned()) + " files", juce::dontSendNotification);
    else if (library.getFolders().empty())
        status.setText ("No groove folders yet: ADD FOLDER... links one of yours", juce::dontSendNotification);
    else
        status.setText (juce::String (results.size()) + (results.size() == 1 ? " groove" : " grooves") + (within != juce::File() ? " here" : ""),
                        juce::dontSendNotification);

    const auto file = processor.getGrooveFile();
    const auto position = processor.getGroovePosition();
    playing.setText (position >= 0.0 && file != juce::File() ? "PLAYING: " + file.getFileNameWithoutExtension() : juce::String(),
                     juce::dontSendNotification);
    if (file != shownPlaying || std::abs (position - shownPosition) > 1.0e-3)
    {
        shownPlaying = position >= 0.0 ? file : juce::File();
        shownPosition = position;
        table.repaint();
    }
}

std::optional<grooves::Groove> GroovesPage::readRow (int row) const
{
    if (row < 0 || row >= (int) results.size())
        return std::nullopt;
    const auto& e = results[(size_t) row];
    return grooves::read (e.file, processor.grooveLayout (e.folder));
}

bool GroovesPage::addRowToSong (int row, double beat)
{
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    processor.addToSong (*g, results[(size_t) row].folder, beat);
    message = g->name + " is in the song now";
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    if (onAddedToSong != nullptr)
        onAddedToSong();
    return true;
}

bool GroovesPage::copyRowForSong (int row)
{
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    processor.copyGrooveForSong (*g, results[(size_t) row].folder, chosenParts);
    message = g->name + " copied: right-click a block in the song, PASTE DRUMS ONTO IT";
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 4000.0;
    return true;
}

void GroovesPage::playRow (int row, bool loop)
{
    if (const auto g = readRow (row))
        processor.playGroove (*g, results[(size_t) row].folder, loop, chosenParts);
}

void GroovesPage::selectedRowsChanged (int row)
{
    // The parts' counts for the groove chosen.
    partCounts = {};
    partsOf = juce::File();
    if (const auto g = readRow (row))
    {
        partsOf = g->file;
        const auto map = processor.grooveLayout (results[(size_t) row].folder);
        for (const auto& h : g->hits)
            if (const auto part = grooves::drumPartOf (h.note >= 0 && h.note < 128 ? map[(size_t) h.note] : -1); part >= 0)
                ++partCounts[(size_t) part];
    }
    partsList->refresh();
}

void GroovesPage::toggleDrumPart (int part)
{
    if (part < 0)
        chosenParts = grooves::allParts;
    else if (chosenParts == grooves::allParts)
        chosenParts = 1u << part;   // from ALL, a click picks just that part
    else
    {
        chosenParts ^= 1u << part;
        if (chosenParts == 0)
            chosenParts = grooves::allParts;   // nothing ticked: back to ALL
    }
    partsList->refresh();
    // The chosen groove playing: heard with its new parts at once.
    if (const auto row = table.getSelectedRow(); row >= 0 && row < (int) results.size()
        && processor.getGroovePosition() >= 0.0 && processor.getGrooveFile() == results[(size_t) row].file)
        playRow (row, true);
}

void GroovesPage::layerOnto (int pattern)
{
    const auto row = table.getSelectedRow();
    const auto g = readRow (row);
    if (! g.has_value())
        message = "Choose a groove first (click it in the list)";
    else
    {
        processor.layerParts (*g, results[(size_t) row].folder, chosenParts, pattern);
        juce::StringArray names;
        for (int k = 0; k < grooves::numDrumParts; ++k)
            if ((chosenParts & (1u << k)) != 0)
                names.add (grooves::drumPartNames[k]);
        message = (chosenParts == grooves::allParts ? juce::String ("All of ") : names.joinIntoString (", ") + " of ") + g->name
                  + " laid into pattern " + synth::drumseq::patternNames[pattern];
    }
    messageUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    timerCallback();
}

//==============================================================================
int GroovesPage::getNumRows()
{
    return (int) results.size();
}

void GroovesPage::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
{
    const auto isPlaying = row < (int) results.size() && results[(size_t) row].file == shownPlaying;
    g.setColour (selected || isPlaying ? ParagonColours::accentDim.withAlpha (0.45f) : (row % 2 == 0 ? juce::Colour (0x00000000) : juce::Colour (0x0affffff)));
    g.fillRect (0, 0, width, height);
    g.setColour (ParagonColours::panelEdge.withAlpha (0.5f));
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

void GroovesPage::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= (int) results.size())
        return;
    const auto& e = results[(size_t) row];
    const auto isPlaying = e.file == shownPlaying;
    g.setColour (isPlaying ? ParagonColours::accent : ParagonColours::text);
    g.setFont (juce::FontOptions (13.0f));
    const auto text = [&] (const juce::String& s, juce::Justification j = juce::Justification::centredLeft)
    {
        g.drawText (s, 6, 0, width - 10, height, j, true);
    };
    switch (column)
    {
        case favourite:
        {
            auto star = Icons::get (Icons::Type::star);
            const auto box = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).withSizeKeepingCentre (14.0f, 14.0f);
            star.applyTransform (star.getTransformToScaleToFit (box, true));
            if (library.isFavourite (e.file))
            {
                g.setColour (ParagonColours::accent);
                g.fillPath (star);
            }
            else
            {
                g.setColour (ParagonColours::textDim.withAlpha (0.6f));
                g.strokePath (star, juce::PathStrokeType (1.0f));
            }
            break;
        }
        case name:
        {
            text (e.name);
            // The playing groove: how far through it is.
            if (isPlaying && shownPosition >= 0.0)
            {
                const auto length = e.bars * e.numerator * 4.0 / e.denominator;
                const auto share = juce::jlimit (0.0, 1.0, std::fmod (shownPosition, length) / length);
                g.setColour (ParagonColours::accent);
                g.fillRect (6.0f, (float) height - 4.0f, (float) (width - 12) * (float) share, 2.0f);
            }
            break;
        }
        case family: g.setColour (ParagonColours::textDim); text (e.family); break;
        case signature: text (e.signature(), juce::Justification::centred); break;
        case bars: text (juce::String (e.bars), juce::Justification::centred); break;
        case tempo: text (tempoText (e), juce::Justification::centred); break;
        case hand: text (grooves::powerHandNames[(int) e.hand]); break;
        case matchScore:
            text (e.match >= 0.0f ? juce::String (juce::roundToInt (e.match * 100.0f)) + "%" : juce::String ("-"), juce::Justification::centred);
            break;
        case intensity:
        {
            const auto bar = juce::Rectangle<float> (8.0f, (float) height * 0.5f - 3.0f, (float) width - 16.0f, 6.0f);
            g.setColour (ParagonColours::panelEdge);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (isPlaying ? ParagonColours::accent : ParagonColours::text.withAlpha (0.75f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, e.intensity)), 3.0f);
            break;
        }
        default: break;
    }
}

juce::String GroovesPage::getCellTooltip (int row, int column)
{
    if (row < 0 || row >= (int) results.size())
        return {};
    const auto& e = results[(size_t) row];
    switch (column)
    {
        case favourite: return "Click: a favourite (ONLY lists just those).";
        case tempo: return e.bpmKnown ? "The tempo it was played at (from the file, or its name)." : "The file doesn't say its tempo.";
        case hand: return "What the riding hand plays most (the hats, ride, a crash or percussion): worked out from its notes.";
        case intensity: return "How hard and how busy it's played (our own measure, from its notes).";
        case matchScore: return "How close its rhythm is to the one searched with (TAP TO FIND): the drums tapped, on QUANTIZE's grid.";
        default: return "Click: play it, round and round. Double-click: once. Drag it onto a track in your DAW, or to a folder.\n"
                        + e.file.getFullPathName();
    }
}

void GroovesPage::cellClicked (int row, int column, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) results.size())
        return;
    if (e.mods.isPopupMenu())
    {
        // Right-click: find more like it, or make it a favourite.
        juce::PopupMenu menu;
        const auto file = results[(size_t) row].file;
        menu.addItem ("Add to Song", [this, row] { addRowToSong (row); });
        menu.addItem (chosenParts == grooves::allParts ? "Copy for Song" : "Copy for Song (the ticked parts)", [this, row] { copyRowForSong (row); });
        menu.addItem ("Use with Song Creator", onUseWithCreator != nullptr, false, [this, file]
        {
            if (onUseWithCreator != nullptr)
                onUseWithCreator (file);
        });
        menu.addItem ("Find Similar (its rhythm)", [this, row] { findSimilar (row); });
        menu.addItem (library.isFavourite (file) ? "Not a Favourite" : "Favourite", [this, file]
        {
            library.setFavourite (file, ! library.isFavourite (file));
            requery();
        });
        menu.showMenuAsync (juce::PopupMenu::Options());
        return;
    }
    if (column == favourite)
    {
        const auto& file = results[(size_t) row].file;
        library.setFavourite (file, ! library.isFavourite (file));
        if (favouritesOnly.getToggleState())
            requery();
        table.repaintRow (row);
        return;
    }
    playRow (row, true);
}

void GroovesPage::cellDoubleClicked (int row, int column, const juce::MouseEvent&)
{
    if (column != favourite)
        playRow (row, false);
}

juce::var GroovesPage::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    return rows.isEmpty() ? juce::var() : juce::var (dragPrefix + juce::String (rows[0]));
}

bool GroovesPage::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& details, juce::StringArray& files,
                                                        bool& canMoveFiles)
{
    // A groove (its ticked parts), or one part of the chosen groove dragged from GROOVE PARTS.
    const auto description = details.description.toString();
    auto row = -1;
    auto parts = chosenParts;
    if (description.startsWith (dragPrefix))
        row = description.fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
    else if (description.startsWith ("part:"))
    {
        row = table.getSelectedRow();
        parts = 1u << juce::jlimit (0, grooves::numDrumParts - 1, description.fromFirstOccurrenceOf ("part:", false, false).getIntValue());
    }
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    const auto file = processor.grooveDragFile (*g, results[(size_t) row].folder, parts);
    if (! file.existsAsFile())
        return false;
    files.add (file.getFullPathName());
    canMoveFiles = false;
    return true;
}

//==============================================================================
void GroovesPage::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    // The tree's and the list's panels.
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (tree.getBounds().toFloat().expanded (4.0f), 5.0f);
    g.fillRoundedRectangle (table.getBounds().toFloat().expanded (4.0f), 5.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (tree.getBounds().toFloat().expanded (4.0f), 5.0f, 1.0f);
    g.drawRoundedRectangle (table.getBounds().toFloat().expanded (4.0f), 5.0f, 1.0f);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("GROOVES", 16, 10, 120, 16, juce::Justification::centredLeft, false);
    g.drawText ("FOLDERS", tree.getX(), tree.getY() - 26, 120, 16, juce::Justification::centredLeft, false);
}

void GroovesPage::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    area.removeFromTop (22);
    const auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int width, int gap = 8)
    {
        c.setBounds (row.removeFromLeft (width).withSizeKeepingCentre (width, 30));
        row.removeFromLeft (gap);
    };

    // The folders and the search.
    auto top = area.removeFromTop (34);
    place (top, addFolder, 130);
    place (top, removeFolder, 86);
    place (top, subfolders, 106);
    place (top, layoutLabel, 74, 6);
    place (top, layout, 150, 24);
    place (top, search, 230);
    auto star = top.removeFromLeft (70);
    favouritesOnly.setBounds (star.withSizeKeepingCentre (70, 30));
    top.removeFromLeft (16);
    place (top, sortLabel, 36, 6);
    place (top, sort, 196, 16);
    status.setBounds (top);
    area.removeFromTop (8);

    // The preview.
    auto second = area.removeFromTop (34);
    place (second, tempoLabel, 50, 6);
    place (second, tempoSource, 130);
    place (second, factor, 170, 20);
    place (second, velocityLabel, 64, 6);
    place (second, velocity, 200, 12);
    place (second, stop, 70, 20);
    place (second, dragLabel, 40, 6);
    place (second, dragAs, 130, 20);
    place (second, toSequencerLabel, 96, 6);
    for (auto& b : patternButtons)
        place (second, b, 34, 4);
    second.removeFromLeft (16);
    playing.setBounds (second);
    area.removeFromTop (38);

    auto left = area.removeFromLeft (300);
    tree.setBounds (left.reduced (4));
    area.removeFromLeft (16);
    // The filter columns above the list (TAP TO FIND first, as a way to find), CLEAR at their end.
    auto strip = area.removeFromTop (128).reduced (4, 0);
    {
        auto tap = strip.removeFromLeft (150);
        tapButton.setBounds (tap.removeFromTop (30));
        tap.removeFromTop (6);
        clearTapButton.setBounds (tap.removeFromTop (26));
        tap.removeFromTop (6);
        tapInfo.setBounds (tap);
        strip.removeFromLeft (12);
    }
    clearFiltersButton.setBounds (strip.removeFromRight (70).removeFromTop (26));
    strip.removeFromRight (10);
    const auto columnWidth = strip.getWidth() / grooves::numColumns;
    for (auto& c : filterColumns)
        c->setBounds (strip.removeFromLeft (columnWidth).reduced (4, 0));
    area.removeFromTop (14);
    // GROOVE PARTS at the list's right, LAYER ONTO under it.
    {
        auto parts = area.removeFromRight (180).reduced (4);
        auto buttons = parts.removeFromBottom (30);
        layerLabel.setBounds (parts.removeFromBottom (22));
        parts.removeFromBottom (4);
        partsList->setBounds (parts);
        const auto w = buttons.getWidth() / (int) layerButtons.size();
        for (auto& b : layerButtons)
            b.setBounds (buttons.removeFromLeft (w).reduced (2, 0));
        area.removeFromRight (10);
    }
    table.setBounds (area.reduced (4));
    // The tap panel over the filters and the list while open.
    tapPanel->setBounds (table.getBounds().getUnion (filterColumns[0]->getBounds().withX (tapButton.getX())).expanded (4));
}
