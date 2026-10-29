#include "GroovesPage.h"

#include "ui/DockPanels.h"
#include "ui/Icons.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int rowHeight = 26;
    const juce::String dragPrefix = "groove:";

    juce::String tempoText (const grooves::Entry& e)
    {
        return e.bpmKnown ? juce::String (e.bpm, e.bpm == std::round (e.bpm) ? 0 : 1) : juce::String ("-");
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
    table.setMultipleSelectionEnabled (false);

    for (juce::Component* c : { (juce::Component*) &addFolder, (juce::Component*) &removeFolder, (juce::Component*) &subfolders,
                                (juce::Component*) &layoutLabel, (juce::Component*) &layout, (juce::Component*) &search,
                                (juce::Component*) &favouritesOnly, (juce::Component*) &sortLabel, (juce::Component*) &sort,
                                (juce::Component*) &status, (juce::Component*) &tempoLabel, (juce::Component*) &tempoSource,
                                (juce::Component*) &factor, (juce::Component*) &velocityLabel, (juce::Component*) &velocity,
                                (juce::Component*) &stop, (juce::Component*) &dragLabel, (juce::Component*) &dragAs,
                                (juce::Component*) &playing, (juce::Component*) &tree, (juce::Component*) &table })
        addAndMakeVisible (c);

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
    const auto p = library.getPreview();
    results = library.query (within, search.getText(), favouritesOnly.getToggleState(), p.sort, processor.getHostBpm(),
                             processor.getHostNumerator(), processor.getHostDenominator());
    table.updateContent();
    table.repaint();
    timerCallback();
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
    if (library.isScanning())
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

void GroovesPage::playRow (int row, bool loop)
{
    if (const auto g = readRow (row))
        processor.playGroove (*g, results[(size_t) row].folder, loop);
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
        default: return "Click: play it, round and round. Double-click: once. Drag it onto a track in your DAW, or to a folder.\n"
                        + e.file.getFullPathName();
    }
}

void GroovesPage::cellClicked (int row, int column, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) results.size())
        return;
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
    const auto description = details.description.toString();
    if (! description.startsWith (dragPrefix))
        return false;
    const auto row = description.fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
    const auto g = readRow (row);
    if (! g.has_value())
        return false;
    const auto file = processor.grooveDragFile (*g, results[(size_t) row].folder);
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
    playing.setBounds (second);
    area.removeFromTop (38);

    auto left = area.removeFromLeft (300);
    tree.setBounds (left.reduced (4));
    area.removeFromLeft (16);
    table.setBounds (area.reduced (4));
}
