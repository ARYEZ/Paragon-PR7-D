#include "SongTrack.h"

#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int barHeight = 24, rulerHeight = 16;
    const juce::String groovePrefix = "groove:", creatorPrefix = "creator:";

    bool isMidiFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("mid;midi");
    }
}

SongTrack::SongTrack (ParagonX1AudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    play.setClickingTogglesState (false);
    play.setTooltip ("Play the song with your DAW stopped, from the place marked on the bar numbers (click them to move it). "
                     "While your DAW plays, the song plays with it.");
    play.onClick = [this]
    {
        if (processor.isSongPlaying())
            processor.stopSong();
        else
            processor.playSong (startBeat);
    };
    addAndMakeVisible (play);
    exportButton.setTooltip ("The whole song as one MIDI file on the kit's notes: click to save it, drag it onto a DAW track.");
    exportButton.fileName = "PR7-D Song";
    exportButton.makeDragFile = [this] { return processor.songDragFile(); };
    exportButton.exportTo = [this] (const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        return out.openedOk() && processor.songToMidi().writeTo (out);
    };
    addAndMakeVisible (exportButton);
    creator.setTooltip ("The song creator: a whole song from one groove and the grooves round it, by song structure.");
    creator.onClick = [this] { if (onOpenCreator != nullptr) onOpenCreator(); };
    addAndMakeVisible (creator);
    status.setFont (juce::FontOptions (11.0f));
    status.setColour (juce::Label::textColourId, ParagonColours::textDim);
    status.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (status);
    processor.songBroadcaster.addChangeListener (this);
    updateStatus();
    startTimerHz (30);
}

SongTrack::~SongTrack()
{
    processor.songBroadcaster.removeChangeListener (this);
}

//==============================================================================
double SongTrack::beatsShown() const noexcept
{
    // At least 16 bars; the song and two bars after it when it's longer (the strip fits it all, no scrolling).
    const auto& s = processor.getSong();
    const auto bar = s.beatsPerBar();
    return juce::jmax (16.0, std::ceil (s.endBeat() / bar) + 2.0) * bar;
}

double SongTrack::beatAt (float x) const noexcept
{
    return juce::jmax (0.0, (double) (x - (float) lane.getX()) / juce::jmax (1, lane.getWidth()) * beatsShown());
}

float SongTrack::xOf (double beat) const noexcept
{
    return (float) lane.getX() + (float) (beat / beatsShown()) * (float) lane.getWidth();
}

void SongTrack::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (barHeight);
    top.removeFromLeft (52);   // "SONG"
    play.setBounds (top.removeFromLeft (64).reduced (2, 2));
    exportButton.setBounds (top.removeFromLeft (104).reduced (2, 2));
    creator.setBounds (top.removeFromLeft (96).reduced (2, 2));
    status.setBounds (top.reduced (8, 0));
    area.removeFromTop (2);
    ruler = area.removeFromTop (rulerHeight);
    lane = area;
}

void SongTrack::visibilityChanged()
{
    // Edits made while it was hidden (from the GROOVES page, undo, a project loaded) reached it as change messages; shown
    // now, it reads the song as it is.
    if (isVisible())
    {
        updateStatus();
        repaint();
    }
}

void SongTrack::updateStatus()
{
    const auto& s = processor.getSong();
    if (s.isEmpty())
    {
        status.setText ("Drag grooves here from the GROOVES list (or MIDI files), or right-click a groove: ADD TO SONG.", juce::dontSendNotification);
        return;
    }
    const auto bars = s.endBeat() / s.beatsPerBar();
    const auto whole = std::abs (bars - std::round (bars)) < 1.0e-6;
    status.setText (juce::String ((int) s.blocks.size()) + (s.blocks.size() == 1 ? " block, " : " blocks, ")
                        + (whole ? juce::String (juce::roundToInt (bars)) : juce::String (bars, 2)) + " bars in "
                        + juce::String (s.numerator) + "/" + juce::String (s.denominator)
                        + ". Drag a block to move it, its right edge to lengthen it; right-click for more.",
                    juce::dontSendNotification);
}

//==============================================================================
void SongTrack::paint (juce::Graphics& g)
{
    const auto& s = processor.getSong();
    const auto bar = s.beatsPerBar();
    const auto shown = beatsShown();
    const auto pxPerBar = (float) lane.getWidth() * (float) (bar / shown);

    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SONG", 2, 0, 50, barHeight, juce::Justification::centredLeft, false);

    // The lane and the bar lines, numbered (every 2nd or 4th when they're close).
    g.setColour (juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (lane.toFloat(), 4.0f);
    const auto every = pxPerBar < 14.0f ? 4 : pxPerBar < 28.0f ? 2 : 1;
    g.setFont (juce::FontOptions (10.0f));
    for (int b = 0; b * bar < shown + 1.0e-9; ++b)
    {
        const auto x = xOf (b * bar);
        g.setColour (ParagonColours::panelEdge.withAlpha (b % every == 0 ? 1.0f : 0.45f));
        g.drawVerticalLine (juce::roundToInt (x), (float) lane.getY(), (float) lane.getBottom());
        if (b % every == 0 && b * bar < shown - 1.0e-9)
        {
            g.setColour (ParagonColours::textDim);
            g.drawText (juce::String (b + 1), juce::roundToInt (x) + 3, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft, false);
        }
    }

    // The blocks: their SONG PART's colour, their name, their hits (a tick each: kicks low, cymbals high), where the
    // groove starts again (a faint line), MUTED greyed.
    for (int i = 0; i < (int) s.blocks.size(); ++i)
    {
        const auto& b = s.blocks[(size_t) i];
        const auto r = juce::Rectangle<float> (xOf (b.start), (float) lane.getY() + 3.0f, xOf (b.end()) - xOf (b.start), (float) lane.getHeight() - 6.0f)
                           .reduced (1.0f, 0.0f);
        const auto colour = b.muted ? juce::Colour (0xff5a5559) : song::colourOf (b.part);
        g.setColour (colour.withAlpha (0.28f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colour.withAlpha (0.8f));
        for (auto repeat = 0.0; repeat < b.length - 1.0e-9; repeat += b.loopLength)
        {
            if (repeat > 0.0)
            {
                g.setColour (colour.withAlpha (0.35f));
                g.drawVerticalLine (juce::roundToInt (xOf (b.start + repeat)), r.getY() + 16.0f, r.getBottom());
                g.setColour (colour.withAlpha (0.8f));
            }
            for (const auto& h : b.hits)
            {
                if (repeat + h.beat >= b.length - 1.0e-9)
                    break;
                const auto part = juce::jmax (0, grooves::drumPartOf (h.slot));
                const auto y = r.getBottom() - 4.0f - (r.getHeight() - 22.0f) * (float) part / (float) (grooves::numDrumParts - 1);
                g.fillRect (xOf (b.start + repeat + h.beat), y - 2.0f, 2.0f, 4.0f);
            }
        }
        g.setColour (i == chosen ? ParagonColours::text : colour);
        g.drawRoundedRectangle (r, 4.0f, i == chosen ? 2.0f : 1.0f);
        g.setColour (b.muted ? ParagonColours::textDim : ParagonColours::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ((b.muted ? "MUTED  " : "") + b.name, r.reduced (5.0f, 2.0f).removeFromTop (14.0f), juce::Justification::centredLeft, true);
    }

    // A block being moved or lengthened: where it would go.
    if (drag != Drag::none && chosen >= 0 && chosen < (int) s.blocks.size())
    {
        const auto& b = s.blocks[(size_t) chosen];
        const auto from = drag == Drag::move ? s.snap (dragBeat) : b.start;
        const auto to = drag == Drag::move ? from + b.length : b.start + juce::jmax (bar, std::round ((dragBeat - b.start) / bar) * bar);
        g.setColour (ParagonColours::accent);
        g.drawRoundedRectangle ({ xOf (from), (float) lane.getY() + 1.0f, xOf (to) - xOf (from), (float) lane.getHeight() - 2.0f }, 4.0f, 2.0f);
    }
    // A groove or a file over the lane: where it would go.
    if (dropBeat >= 0.0)
    {
        auto trial = s;
        song::Block probe;
        probe.length = bar;
        const auto at = trial.blocks[(size_t) trial.place (probe, dropBeat)].start;
        g.setColour (ParagonColours::accent);
        g.fillRect (xOf (at) - 1.0f, (float) lane.getY(), 3.0f, (float) lane.getHeight());
    }

    // PLAY's start (a mark on the bar numbers) and where the song is playing.
    g.setColour (ParagonColours::accent);
    const auto sx = xOf (startBeat);
    juce::Path mark;
    mark.addTriangle (sx, (float) ruler.getBottom(), sx - 5.0f, (float) ruler.getY() + 3.0f, sx + 5.0f, (float) ruler.getY() + 3.0f);
    g.fillPath (mark);
    if (const auto at = processor.getSongPosition(); at >= 0.0 && at <= shown)
    {
        g.setColour (ParagonColours::glow);
        g.fillRect (xOf (at) - 0.75f, (float) ruler.getY(), 1.5f, (float) (lane.getBottom() - ruler.getY()));
    }
}

//==============================================================================
bool SongTrack::nearRightEdge (int block, juce::Point<float> p) const
{
    const auto& s = processor.getSong();
    return block >= 0 && block < (int) s.blocks.size() && std::abs (p.x - xOf (s.blocks[(size_t) block].end())) <= 6.0f;
}

void SongTrack::edit (const std::function<void (song::Song&)>& f)
{
    auto s = processor.getSong();
    f (s);
    processor.setSong (std::move (s));
}

void SongTrack::mouseMove (const juce::MouseEvent& e)
{
    const auto& s = processor.getSong();
    auto block = s.blockAt (beatAt (e.position.x));
    // Just past a block's end counts too (its edge is grabbed from either side).
    if (block < 0)
        block = s.blockAt (beatAt (e.position.x - 6.0f));
    setMouseCursor (lane.contains (e.getPosition()) && nearRightEdge (block, e.position) ? juce::MouseCursor::LeftRightResizeCursor
                                                                                       : juce::MouseCursor::NormalCursor);
}

void SongTrack::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto& s = processor.getSong();
    if (ruler.contains (e.getPosition()))
    {
        startBeat = s.snap (beatAt (e.position.x));
        if (processor.isSongPlaying())
            processor.playSong (startBeat);
        repaint();
        return;
    }
    if (! lane.contains (e.getPosition()))
        return;
    auto block = s.blockAt (beatAt (e.position.x));
    if (block < 0 && nearRightEdge (s.blockAt (beatAt (e.position.x - 6.0f)), e.position))
        block = s.blockAt (beatAt (e.position.x - 6.0f));
    chosen = block;
    repaint();
    if (block < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        showMenuFor (block, beatAt (e.position.x));
        return;
    }
    const auto& b = s.blocks[(size_t) block];
    drag = nearRightEdge (block, e.position) ? Drag::resize : Drag::move;
    grabOffset = beatAt (e.position.x) - b.start;
    dragBeat = drag == Drag::move ? b.start : b.end();
}

void SongTrack::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || e.getDistanceFromDragStart() < 3)
        return;
    dragBeat = drag == Drag::move ? beatAt (e.position.x) - grabOffset : beatAt (e.position.x);
    repaint();
}

void SongTrack::mouseUp (const juce::MouseEvent& e)
{
    const auto was = drag;
    drag = Drag::none;
    if (was == Drag::none || e.getDistanceFromDragStart() < 3 || chosen < 0)
    {
        repaint();
        return;
    }
    const auto index = chosen;
    const auto to = dragBeat;
    if (was == Drag::move)
        edit ([&] (song::Song& s) { chosen = s.move (index, to); });
    else
        edit ([&] (song::Song& s) { s.resize (index, to - s.blocks[(size_t) index].start); });
    repaint();
}

bool SongTrack::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && chosen >= 0 && chosen < (int) processor.getSong().blocks.size())
    {
        const auto index = chosen;
        chosen = -1;
        edit ([index] (song::Song& s) { s.remove (index); });
        return true;
    }
    return false;
}

juce::PopupMenu SongTrack::menuFor (int block, double atBeat)
{
    juce::PopupMenu menu;
    const auto& s = processor.getSong();
    if (block < 0 || block >= (int) s.blocks.size())
        return menu;
    const auto& b = s.blocks[(size_t) block];
    menu.addSectionHeader (b.name);
    menu.addItem ("Duplicate", [this, block] { edit ([&] (song::Song& song) { chosen = song.duplicate (block); }); });
    menu.addItem ("Remove", [this, block]
    {
        chosen = -1;
        edit ([block] (song::Song& song) { song.remove (block); });
    });
    menu.addItem ("Mute", true, b.muted, [this, block] { edit ([block] (song::Song& song) { song.blocks[(size_t) block].muted = ! song.blocks[(size_t) block].muted; }); });
    juce::PopupMenu parts;
    for (int k = 0; k < grooves::partNames.size(); ++k)
    {
        juce::PopupMenu::Item item (grooves::partNames[k]);
        item.colour = song::colourOf ((grooves::Part) k);
        item.isTicked = (int) b.part == k;
        item.action = [this, block, k] { edit ([block, k] (song::Song& song) { song.blocks[(size_t) block].part = (grooves::Part) k; }); };
        parts.addItem (item);
    }
    menu.addSubMenu ("Song Part", parts);
    // CHANGE TEMPO: the block's notes closer together or further apart (its length too, so the blocks after it move).
    juce::PopupMenu tempo;
    for (const auto f : { grooves::TempoFactor::half, grooves::TempoFactor::twoThirds, grooves::TempoFactor::threeHalves, grooves::TempoFactor::twice })
        tempo.addItem (grooves::tempoFactorNames[(int) f], [this, block, f]
        {
            edit ([block, f] (song::Song& song)
            {
                song::changeTempo (song.blocks[(size_t) block], f);
                song.resize (block, song.blocks[(size_t) block].length);   // to whole bars, the blocks after it moved along
            });
        });
    menu.addSubMenu ("Change Tempo", tempo);

    // EDITING INSIDE IT (0.9.0): its drums by GROOVE PARTS group (each with how many notes it has there).
    const auto partsMenu = [] (const song::Block& of, const std::function<void (unsigned slots)>& act, bool withAll)
    {
        juce::PopupMenu m;
        std::array<int, grooves::numDrumParts> counts {};
        for (const auto& h : of.hits)
            if (const auto part = grooves::drumPartOf (h.slot); part >= 0)
                ++counts[(size_t) part];
        if (withAll)
            m.addItem ("All Its Drums", ! of.hits.empty(), false, [act] { act (song::allSlots); });
        for (int k = 0; k < grooves::numDrumParts; ++k)
            if (counts[(size_t) k] > 0)
                m.addItem (grooves::drumPartNames[k] + " (" + juce::String (counts[(size_t) k]) + ")", [act, k] { act (song::slotsOf (1u << k)); });
        return m;
    };
    menu.addSeparator();
    menu.addItem ("Edit Block...", onEditBlock != nullptr, false, [this, block] { if (onEditBlock != nullptr) onEditBlock (block); });
    menu.addItem ("Use with Song Creator", onUseWithCreator != nullptr && b.source.existsAsFile(), false,
                  [this, file = b.source] { onUseWithCreator (file); });
    menu.addItem ("Copy", [this, b] { processor.setSongClipboard (b); });
    const auto& clip = processor.getSongClipboard();
    menu.addItem ("Paste After", clip.has_value(), false, [this, block]
    {
        if (const auto& c = processor.getSongClipboard())
            edit ([&] (song::Song& song) { chosen = song.place (*c, song.blocks[(size_t) block].end()); });
    });
    if (clip.has_value())
        menu.addSubMenu ("Paste Drums Onto It", partsMenu (*clip, [this, block] (unsigned slots)
        {
            if (const auto& c = processor.getSongClipboard())
                edit ([&] (song::Song& song) { song::pasteDrums (song.blocks[(size_t) block], *c, slots); });
        }, true));
    else
        menu.addItem ("Paste Drums Onto It", false, false, nullptr);
    menu.addSubMenu ("Remove Drums", partsMenu (b, [this, block] (unsigned slots)
    {
        edit ([&] (song::Song& song) { song::removeDrums (song.blocks[(size_t) block], slots); });
    }, false), ! b.hits.empty());
    juce::PopupMenu grid;
    for (int q = 0; q < grooves::quantizeNames.size(); ++q)
        grid.addItem (grooves::quantizeNames[q], [this, block, q] { edit ([&] (song::Song& song) { song::quantize (song.blocks[(size_t) block], grooves::gridOf (q)); }); });
    menu.addSubMenu ("Quantize", grid, ! b.hits.empty());
    juce::PopupMenu velocity;
    for (const auto percent : { 50, 75, 90, 110, 125, 150 })
        velocity.addItem (juce::String (percent) + "%", [this, block, percent]
        {
            edit ([&] (song::Song& song) { song::scaleVelocity (song.blocks[(size_t) block], (float) percent / 100.0f); });
        });
    menu.addSubMenu ("Velocity", velocity, ! b.hits.empty());
    const auto splitAt = atBeat >= 0.0 ? atBeat : b.start + b.length * 0.5;
    auto trial = s;
    menu.addItem ("Split Here", trial.split (block, splitAt) >= 0, false, [this, block, splitAt]
    {
        edit ([&] (song::Song& song) { song.split (block, splitAt); });
    });
    menu.addItem ("Merge With Next", block + 1 < (int) s.blocks.size(), false, [this, block]
    {
        edit ([&] (song::Song& song) { chosen = song.mergeWithNext (block); });
    });
    return menu;
}

void SongTrack::showMenuFor (int block, double atBeat)
{
    menuFor (block, atBeat).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void SongTrack::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! lane.contains (e.getPosition()))
        return;
    if (const auto block = processor.getSong().blockAt (beatAt (e.position.x)); block >= 0 && onEditBlock != nullptr)
    {
        chosen = block;
        onEditBlock (block);
    }
}

//==============================================================================
bool SongTrack::isInterestedInDragSource (const SourceDetails& details)
{
    const auto d = details.description.toString();
    return d.startsWith (groovePrefix) || d.startsWith (creatorPrefix);
}

void SongTrack::itemDragMove (const SourceDetails& details)
{
    dropBeat = lane.contains (details.localPosition) ? beatAt ((float) details.localPosition.x) : -1.0;
    repaint();
}

void SongTrack::itemDragExit (const SourceDetails&)
{
    dropBeat = -1.0;
    repaint();
}

void SongTrack::itemDropped (const SourceDetails& details)
{
    dropBeat = -1.0;
    const auto d = details.description.toString();
    const auto beat = beatAt ((float) details.localPosition.x);
    if (d.startsWith (creatorPrefix))
    {
        const auto f = juce::StringArray::fromTokens (d.fromFirstOccurrenceOf (creatorPrefix, false, false), ":", {});
        if (onCreatorDropped != nullptr && f.size() == 2)
            onCreatorDropped (f[0].getIntValue(), f[1].getIntValue(), beat);
    }
    else if (onGrooveDropped != nullptr)
        onGrooveDropped (d.fromFirstOccurrenceOf (groovePrefix, false, false).getIntValue(), beat);
    repaint();
}

bool SongTrack::isInterestedInFileDrag (const juce::StringArray& files)
{
    return std::any_of (files.begin(), files.end(), isMidiFile);
}

void SongTrack::fileDragMove (const juce::StringArray&, int x, int y)
{
    dropBeat = lane.contains (x, y) ? beatAt ((float) x) : -1.0;
    repaint();
}

void SongTrack::fileDragExit (const juce::StringArray&)
{
    dropBeat = -1.0;
    repaint();
}

void SongTrack::filesDropped (const juce::StringArray& files, int x, int)
{
    // Each MIDI file a block (its notes through the kit's own map: a file from outside the groove folders has no
    // FILES' MAP), one after another from the drop.
    dropBeat = -1.0;
    auto beat = beatAt ((float) x);
    const auto map = processor.getNoteMap();
    for (const auto& path : files)
        if (isMidiFile (path))
            if (const auto g = grooves::read (juce::File (path), map))
            {
                const auto index = processor.addBlockToSong (song::blockFrom (*g, map), beat);
                chosen = index;
                beat = processor.getSong().blocks[(size_t) index].end();
            }
    repaint();
}

//==============================================================================
void SongTrack::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (chosen >= (int) processor.getSong().blocks.size())
        chosen = -1;
    updateStatus();
    repaint();
}

void SongTrack::timerCallback()
{
    play.setButtonText (processor.isSongPlaying() ? "STOP" : "PLAY");
    if (const auto at = processor.getSongPosition(); at != shownPosition)
    {
        shownPosition = at;
        repaint();
    }
}
