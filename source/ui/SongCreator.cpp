#include "SongCreator.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int headerHeight = 40, structureRow = 44, grooveRow = 24, columnHeader = 26;
    const juce::String creatorPrefix = "creator:", groovePrefix = "groove:";
}

const std::array<grooves::Part, 8>& SongCreator::shownParts()
{
    using grooves::Part;
    static const std::array<Part, 8> parts { Part::intro, Part::verse, Part::preChorus, Part::chorus, Part::bridge, Part::fill,
                                             Part::ending, Part::beat };
    return parts;
}

SongCreator::SongCreator (ParagonX1AudioProcessor& p) : processor (p)
{
    close.setTooltip ("Close the song creator.");
    close.onClick = [this]
    {
        processor.stopGroove();
        setVisible (false);
        if (onClose != nullptr)
            onClose();
    };
    make.setTooltip ("A whole song from the chosen structure, each section a groove from its column (in place of the song "
                     "there: undo brings it back).");
    make.onClick = [this] { makeSong (false); };
    addEnd.setTooltip ("The chosen structure's song after the song there.");
    addEnd.onClick = [this] { makeSong (true); };
    save.setTooltip ("The song track's parts and lengths (no notes) as a structure of your own, to make songs from any groove.");
    save.onClick = [this]
    {
        askName ("Save the song's structure", "My Structure", [this] (juce::String name) { saveStructure (name); });
    };
    for (auto* b : { &close, &make, &addEnd, &save })
        addAndMakeVisible (b);
    processor.getGrooveLibrary().addChangeListener (this);
    refreshStructures();
}

SongCreator::~SongCreator()
{
    processor.getGrooveLibrary().removeChangeListener (this);
}

void SongCreator::open()
{
    refreshStructures();
    setVisible (true);
    toFront (false);
}

void SongCreator::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The library read again: the source's columns as they are now.
    if (source != juce::File())
        columns = processor.songCreatorColumns (source);
    repaint();
}

void SongCreator::refreshStructures()
{
    structures = processor.getSongStructures();
    chosenStructure = juce::jlimit (0, juce::jmax (0, (int) structures.size() - 1), chosenStructure);
    repaint();
}

void SongCreator::setSource (const juce::File& file)
{
    source = file;
    columns = processor.songCreatorColumns (file);
    chosenGroove = { -1, 0 };
    repaint();
}

const grooves::Entry* SongCreator::entry (int part, int index) const
{
    if (part < 0 || part >= song::numSongParts || index < 0 || index >= (int) columns[(size_t) part].size())
        return nullptr;
    return &columns[(size_t) part][(size_t) index];
}

//==============================================================================
void SongCreator::chooseStructure (int index)
{
    chosenStructure = juce::jlimit (0, juce::jmax (0, (int) structures.size() - 1), index);
    repaint();
}

void SongCreator::makeSong (bool addToEnd)
{
    if (chosenStructure < 0 || chosenStructure >= (int) structures.size())
        return;
    auto made = processor.songFromStructure (structures[(size_t) chosenStructure].sections, columns);
    if (made.isEmpty())
        return;
    if (! addToEnd || processor.getSong().isEmpty())
    {
        processor.setSong (std::move (made));
        return;
    }
    auto s = processor.getSong();
    const auto from = s.snap (s.endBeat() + s.beatsPerBar() * 0.49);   // the next bar line
    for (auto b : made.blocks)
    {
        b.start += from;
        s.blocks.push_back (std::move (b));
    }
    processor.setSong (std::move (s));
}

bool SongCreator::saveStructure (const juce::String& name)
{
    const auto sections = song::structureOf (processor.getSong());
    if (! processor.saveSongStructure (name, sections))
        return false;
    refreshStructures();
    for (int i = 0; i < (int) structures.size(); ++i)
        if (structures[(size_t) i].user && structures[(size_t) i].name == name.trim())
            chosenStructure = i;
    repaint();
    return true;
}

juce::PopupMenu SongCreator::structureMenu (int index)
{
    juce::PopupMenu menu;
    if (index < 0 || index >= (int) structures.size())
        return menu;
    const auto s = structures[(size_t) index];
    menu.addSectionHeader (s.name);
    menu.addItem ("Make Song", [this, index] { chooseStructure (index); makeSong (false); });
    menu.addItem ("Add to End", [this, index] { chooseStructure (index); makeSong (true); });
    if (s.user)
    {
        menu.addItem ("Rename...", [this, name = s.name]
        {
            askName ("Rename the structure", name, [this, name] (juce::String to)
            {
                processor.renameSongStructure (name, to);
                refreshStructures();
            });
        });
        menu.addItem ("Remove", [this, name = s.name]
        {
            processor.removeSongStructure (name);
            refreshStructures();
        });
    }
    return menu;
}

void SongCreator::askName (const juce::String& title, const juce::String& start, std::function<void (juce::String)> then)
{
    nameWindow = std::make_unique<juce::AlertWindow> (title, "Its name:", juce::MessageBoxIconType::NoIcon, this);
    nameWindow->addTextEditor ("name", start);
    nameWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, then] (int result)
    {
        if (result == 1 && nameWindow != nullptr)
            then (nameWindow->getTextEditorContents ("name"));
    }));
}

//==============================================================================
void SongCreator::audition (int part, int index)
{
    chosenGroove = { part, index };
    if (const auto* e = entry (part, index))
        if (const auto g = grooves::read (e->file, processor.grooveLayout (e->folder)))
            processor.playGroove (*g, e->folder, true);
    repaint();
}

int SongCreator::addToSong (int part, int index, double beat)
{
    const auto* e = entry (part, index);
    if (e == nullptr)
        return -1;
    if (auto b = processor.songBlockFor (*e))
        return processor.addBlockToSong (std::move (*b), beat);
    return -1;
}

int SongCreator::replaceAll (int part, int index)
{
    const auto* e = entry (part, index);
    const auto with = e != nullptr ? processor.songBlockFor (*e) : std::nullopt;
    if (! with.has_value())
        return 0;
    auto s = processor.getSong();
    auto n = 0;
    for (auto& b : s.blocks)
        if ((int) b.part == part)
        {
            song::replaceGroove (b, *with);
            ++n;
        }
    if (n > 0)
        processor.setSong (std::move (s));
    return n;
}

bool SongCreator::replaceChosenBlock (int part, int index)
{
    const auto block = chosenBlock != nullptr ? chosenBlock() : -1;
    const auto* e = entry (part, index);
    const auto with = e != nullptr ? processor.songBlockFor (*e) : std::nullopt;
    auto s = processor.getSong();
    if (! with.has_value() || block < 0 || block >= (int) s.blocks.size())
        return false;
    song::replaceGroove (s.blocks[(size_t) block], *with);
    processor.setSong (std::move (s));
    return true;
}

juce::PopupMenu SongCreator::grooveMenu (int part, int index)
{
    juce::PopupMenu menu;
    const auto* e = entry (part, index);
    if (e == nullptr)
        return menu;
    menu.addSectionHeader (e->name);
    menu.addItem ("Add to Song", [this, part, index] { addToSong (part, index); });
    menu.addItem ("Copy", [this, part, index]
    {
        if (const auto* x = entry (part, index))
            if (auto b = processor.songBlockFor (*x))
                processor.setSongClipboard (std::move (*b));
    });
    auto onTrack = 0;
    for (const auto& b : processor.getSong().blocks)
        onTrack += (int) b.part == part;
    menu.addItem ("Replace All \"" + grooves::partNames[part] + "\" on Track (" + juce::String (onTrack) + ")", onTrack > 0, false,
                  [this, part, index] { replaceAll (part, index); });
    const auto block = chosenBlock != nullptr ? chosenBlock() : -1;
    menu.addItem ("Replace the Chosen Block", block >= 0, false, [this, part, index] { replaceChosenBlock (part, index); });
    menu.addItem ("Use as Source", [this, file = e->file] { setSource (file); });
    return menu;
}

//==============================================================================
void SongCreator::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    auto top = area.removeFromTop (headerHeight);
    close.setBounds (top.removeFromRight (90).reduced (0, 6));
    structureArea = area.removeFromLeft (290);
    auto buttons = structureArea.removeFromBottom (78);
    auto row = buttons.removeFromTop (36).reduced (0, 4);
    make.setBounds (row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (4));
    addEnd.setBounds (row.withTrimmedLeft (4));
    save.setBounds (buttons.reduced (0, 4));
    structureArea.removeFromTop (22);   // its title
    area.removeFromLeft (16);
    sourceArea = area.removeFromTop (30);
    area.removeFromTop (6);
    columnArea = area;
}

juce::Rectangle<int> SongCreator::cellOf (int part, int index) const
{
    const auto& parts = shownParts();
    const auto at = std::find (parts.begin(), parts.end(), (grooves::Part) part);
    if (at == parts.end())
        return {};
    const auto column = (int) std::distance (parts.begin(), at);
    const auto width = columnArea.getWidth() / (int) parts.size();
    return { columnArea.getX() + column * width + 2, columnArea.getY() + columnHeader + index * grooveRow, width - 4, grooveRow - 2 };
}

std::pair<int, int> SongCreator::grooveAt (juce::Point<int> p) const
{
    for (const auto part : shownParts())
        for (int i = 0; i < (int) columns[(size_t) part].size(); ++i)
            if (cellOf ((int) part, i).contains (p))
                return { (int) part, i };
    return { -1, 0 };
}

int SongCreator::structureAt (juce::Point<int> p) const
{
    if (! structureArea.contains (p))
        return -1;
    const auto i = (p.y - structureArea.getY()) / structureRow;
    return i >= 0 && i < (int) structures.size() ? i : -1;
}

void SongCreator::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("SONG CREATOR", 12, 8, 300, headerHeight, juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (ParagonColours::textDim);
    g.drawText ("SONG STRUCTURES", structureArea.getX(), structureArea.getY() - 22, 200, 18, juce::Justification::centredLeft, false);

    // The structures: each its name, how long, and its sections as coloured blocks.
    for (int i = 0; i < (int) structures.size(); ++i)
    {
        const auto& s = structures[(size_t) i];
        const auto r = juce::Rectangle<int> (structureArea.getX(), structureArea.getY() + i * structureRow, structureArea.getWidth(), structureRow - 4);
        if (r.getBottom() > structureArea.getBottom())
            break;
        g.setColour (i == chosenStructure ? ParagonColours::accent.withAlpha (0.14f) : juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        if (i == chosenStructure)
        {
            g.setColour (ParagonColours::accent);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
        }
        g.setColour (i == chosenStructure ? ParagonColours::accent : ParagonColours::text);
        g.setFont (juce::FontOptions (12.0f, i == chosenStructure ? juce::Font::bold : juce::Font::plain));
        g.drawText (s.name + (s.user ? "  (yours)" : ""), r.getX() + 8, r.getY() + 3, r.getWidth() - 70, 18, juce::Justification::centredLeft, true);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (s.totalBars()) + " bars", r.getRight() - 64, r.getY() + 3, 56, 18, juce::Justification::centredRight, false);
        auto strip = r.reduced (8, 0).withTrimmedTop (24).withHeight (10).toFloat();
        const auto total = (float) juce::jmax (1, s.totalBars());
        auto x = strip.getX();
        for (const auto& section : s.sections)
        {
            const auto w = strip.getWidth() * (float) section.bars / total;
            g.setColour (song::colourOf (section.part));
            g.fillRect (x, strip.getY(), juce::jmax (1.0f, w - 1.0f), strip.getHeight());
            x += w;
        }
    }

    // The source.
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (sourceArea.toFloat(), 4.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (sourceArea.toFloat().reduced (0.5f), 4.0f, 1.0f);
    auto counted = 0;
    for (const auto& c : columns)
        counted += (int) c.size();
    juce::String sourceText;
    if (source == juce::File())
        sourceText = "Drop a groove here (from the GROOVES list, or a MIDI file in one of your linked folders), or right-click a "
                     "groove: USE WITH SONG CREATOR.";
    else if (counted == 0)
        sourceText = "SOURCE: " + source.getFileNameWithoutExtension() + "   (not in your linked folders: link its folder on "
                     "GROOVES to see the grooves around it)";
    else
        sourceText = "SOURCE: " + source.getFileNameWithoutExtension() + "  in  " + source.getParentDirectory().getFileName() + "   ("
                   + juce::String (counted) + " grooves round it)";
    g.setColour (source == juce::File() ? ParagonColours::textDim : ParagonColours::text);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (sourceText, sourceArea.reduced (10, 0), juce::Justification::centredLeft, true);

    // The columns: each song part's grooves, the source marked, the one being heard lit.
    const auto& parts = shownParts();
    const auto width = columnArea.getWidth() / (int) parts.size();
    const auto playing = processor.getGrooveFile();
    for (size_t c = 0; c < parts.size(); ++c)
    {
        const auto part = parts[c];
        const auto head = juce::Rectangle<int> (columnArea.getX() + (int) c * width + 2, columnArea.getY(), width - 4, columnHeader - 4);
        g.setColour (song::colourOf (part).withAlpha (0.3f));
        g.fillRoundedRectangle (head.toFloat(), 3.0f);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (grooves::partNames[(int) part].toUpperCase() + "  " + juce::String ((int) columns[(size_t) part].size()), head,
                    juce::Justification::centred, true);
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillRect (head.getX(), head.getBottom() + 4, head.getWidth(), columnArea.getBottom() - head.getBottom() - 4);
        for (int i = 0; i < (int) columns[(size_t) part].size(); ++i)
        {
            const auto cell = cellOf ((int) part, i);
            if (cell.getBottom() > columnArea.getBottom())
                break;
            const auto& e = columns[(size_t) part][(size_t) i];
            const auto chosen = chosenGroove == std::make_pair ((int) part, i);
            if (chosen)
            {
                g.setColour (song::colourOf (part).withAlpha (0.35f));
                g.fillRoundedRectangle (cell.toFloat(), 3.0f);
            }
            g.setColour (e.file == playing ? ParagonColours::accent : e.file == source ? ParagonColours::glow : ParagonColours::text);
            g.setFont (juce::FontOptions (11.5f, e.file == source ? juce::Font::bold : juce::Font::plain));
            g.drawText ((e.file == playing ? "> " : "") + e.name, cell.reduced (6, 0), juce::Justification::centredLeft, true);
        }
    }
}

//==============================================================================
void SongCreator::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    if (const auto s = structureAt (e.getPosition()); s >= 0)
    {
        chooseStructure (s);
        if (e.mods.isPopupMenu())
            structureMenu (s).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    const auto [part, index] = grooveAt (e.getPosition());
    if (part < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        chosenGroove = { part, index };
        repaint();
        grooveMenu (part, index).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    audition (part, index);
}

void SongCreator::mouseDrag (const juce::MouseEvent& e)
{
    // A groove dragged out: onto the song track.
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    const auto [part, index] = grooveAt (e.getMouseDownPosition());
    if (part < 0)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        container->startDragging (creatorPrefix + juce::String (part) + ":" + juce::String (index), this);
    }
}

void SongCreator::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto [part, index] = grooveAt (e.getPosition()); part >= 0)
        addToSong (part, index);
}

bool SongCreator::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith (groovePrefix);
}

void SongCreator::itemDropped (const SourceDetails& details)
{
    if (onGrooveRowDropped != nullptr)
        onGrooveRowDropped (details.description.toString().fromFirstOccurrenceOf (groovePrefix, false, false).getIntValue());
}

bool SongCreator::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() > 0 && juce::File (files[0]).hasFileExtension ("mid;midi");
}

void SongCreator::filesDropped (const juce::StringArray& files, int, int)
{
    setSource (juce::File (files[0]));
}

bool SongCreator::toPattern (int part, int index, int pattern)
{
    const auto* e = entry (part, index);
    if (e == nullptr)
        return false;
    if (const auto g = grooves::read (e->file, processor.grooveLayout (e->folder)))
        return processor.grooveToPattern (*g, e->folder, pattern).placed > 0;
    return false;
}
