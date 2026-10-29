#include "ArpPage.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    constexpr float gutter = 40.0f, velocityLane = 54.0f, smallLane = 18.0f, laneGap = 4.0f;
    const juce::Colour amber { 0xffffc23a };

    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        b.setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
        b.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        b.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    }

    const juce::StringArray startNames { "Every step", "Eighths", "Octave bounce", "Up the scale", "Trance (off-beat)", "Slides",
                                         "Ratchets", "Long notes", "Clear" };

    synth::arp::Pattern startPattern (int choice)
    {
        synth::arp::Pattern p;
        static const int scale[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        for (int k = 0; k < 16; ++k)
        {
            auto& b = p.blocks[(size_t) k];
            switch (choice)
            {
                case 0: b.on = true; break;
                case 1: b.on = k % 2 == 0; b.length = 2; break;
                case 2: b.on = true; b.transpose = k % 2 == 0 ? 0 : 12; break;
                case 3: b.on = true; b.transpose = scale[k % 8]; break;
                case 4: b.on = k % 4 != 0; b.velocity = k % 4 == 2 ? 1.0f : 0.7f; break;
                case 5: b.on = k % 4 < 2; b.slide = k % 4 == 0; b.transpose = k % 4 == 1 ? 7 : 0; break;
                case 6: b.on = true; b.ratchet = k % 4 == 3 ? 3 : (k % 4 == 1 ? 2 : 1); break;
                case 7: b.on = k % 4 == 0; b.length = 4; b.transpose = (k / 4) % 2 == 0 ? 0 : -5; break;
                default: break;
            }
        }
        return p;
    }
}

//==============================================================================
ArpPatternEditor::ArpPatternEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Click to put a block on a step (at that transposition); drag it up or down to transpose it, its right edge to "
                "lengthen it; right-click it to delete it. Under the grid: VELOCITY (drag), SLIDE (click: glide into the next "
                "block, with PLAY's GLIDE time) and RATCHET (click for more repeats, right-click for fewer).");
    startTimerHz (15);
}

void ArpPatternEditor::setTarget (int newArp, int newPattern)
{
    arp = juce::jlimit (0, synth::arp::numArps - 1, newArp);
    pattern = juce::jlimit (0, synth::arp::numPatterns - 1, newPattern);
    shown = getShown();
    repaint();
}

synth::arp::Pattern ArpPatternEditor::getShown() const
{
    return processor.getModMatrix().arps[(size_t) arp].patterns[(size_t) pattern];
}

void ArpPatternEditor::setShown (const synth::arp::Pattern& p, bool finished)
{
    auto matrix = processor.getModMatrix();
    matrix.arps[(size_t) arp].patterns[(size_t) pattern] = p;
    processor.setModMatrix (matrix);
    shown = p;
    if (finished)
        processor.modMatrixEditFinished();
    repaint();
}

int ArpPatternEditor::getLength() const
{
    return juce::jlimit (2, synth::arp::maxSteps,
                         juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::arp (arp, "Length"))->load()));
}

juce::Rectangle<float> ArpPatternEditor::gridArea() const
{
    auto area = getLocalBounds().toFloat().reduced (6.0f, 6.0f).withTrimmedLeft (gutter);
    area.removeFromBottom (velocityLane + 2.0f * smallLane + 3.0f * laneGap + (showsChords() ? 3.0f * (smallLane + laneGap) : 0.0f));
    return area;
}

juce::Rectangle<float> ArpPatternEditor::cell (int step, int transpose) const
{
    const auto grid = gridArea();
    const auto rows = (float) (highestTranspose - lowestTranspose + 1);
    const auto w = grid.getWidth() / (float) getLength(), h = grid.getHeight() / rows;
    return { grid.getX() + w * (float) step, grid.getY() + h * (float) (highestTranspose - transpose), w, h };
}

juce::Rectangle<float> ArpPatternEditor::velocityArea (int step) const
{
    const auto grid = gridArea();
    const auto w = grid.getWidth() / (float) getLength();
    return { grid.getX() + w * (float) step, grid.getBottom() + laneGap, w, velocityLane };
}

juce::Rectangle<float> ArpPatternEditor::slideArea (int step) const
{
    return velocityArea (step).translated (0.0f, velocityLane + laneGap).withHeight (smallLane);
}

juce::Rectangle<float> ArpPatternEditor::ratchetArea (int step) const
{
    return slideArea (step).translated (0.0f, smallLane + laneGap);
}

juce::Rectangle<float> ArpPatternEditor::chordArea (int step) const
{
    return ratchetArea (step).translated (0.0f, smallLane + laneGap);
}

juce::Rectangle<float> ArpPatternEditor::tonesArea (int step) const
{
    return chordArea (step).translated (0.0f, smallLane + laneGap);
}

juce::Rectangle<float> ArpPatternEditor::toneArea (int step, int tone) const
{
    const auto area = tonesArea (step).reduced (3.0f, 3.0f);
    const auto w = area.getWidth() / 5.0f;
    return { area.getX() + w * (float) tone, area.getY(), w, area.getHeight() };
}

juce::Rectangle<float> ArpPatternEditor::subArea (int step) const
{
    return tonesArea (step).translated (0.0f, smallLane + laneGap);
}

bool ArpPatternEditor::showsChords() const
{
    return juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::arp (arp, "Mode"))->load()) == (int) synth::arp::Mode::chords;
}

int ArpPatternEditor::stepAt (float x) const
{
    const auto grid = gridArea();
    return juce::jlimit (0, getLength() - 1, (int) std::floor ((x - grid.getX()) / (grid.getWidth() / (float) getLength())));
}

int ArpPatternEditor::transposeAt (float y) const
{
    const auto grid = gridArea();
    const auto rows = (float) (highestTranspose - lowestTranspose + 1);
    return juce::jlimit (lowestTranspose, highestTranspose, highestTranspose - (int) std::floor ((y - grid.getY()) / (grid.getHeight() / rows)));
}

void ArpPatternEditor::timerCallback()
{
    const std::vector<float> settings { (float) getLength(), showsChords() ? 1.0f : 0.0f };
    if (dragStep < 0 && ! (getShown() == shown))
    {
        shown = getShown();
        repaint();
    }
    if (settings != lastSettings)
    {
        lastSettings = settings;
        repaint();
    }
}

void ArpPatternEditor::paint (juce::Graphics& g)
{
    if (dragStep < 0)
        shown = getShown();
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    const auto grid = gridArea();
    const auto length = getLength();

    // Rows: a line each semitone, brighter on the octaves (labelled).
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (int t = lowestTranspose; t <= highestTranspose; ++t)
    {
        const auto row = cell (0, t).withWidth (grid.getWidth());
        const auto octave = t % 12 == 0;
        g.setColour (ParagonColours::panelEdge.withAlpha (octave ? 0.9f : 0.25f));
        g.drawHorizontalLine (juce::roundToInt (row.getBottom()), grid.getX(), grid.getRight());
        if (octave)
        {
            g.setColour (t == 0 ? ParagonColours::text : ParagonColours::textDim);
            g.drawText ((t > 0 ? "+" : "") + juce::String (t), juce::Rectangle<float> (grid.getX() - gutter, row.getY() - 4.0f, gutter - 6.0f, row.getHeight() + 8.0f),
                        juce::Justification::centredRight, false);
        }
    }
    // Steps: a line each, brighter each beat (4 steps).
    for (int k = 0; k <= length; ++k)
    {
        const auto x = juce::roundToInt (grid.getX() + grid.getWidth() * (float) k / (float) length);
        g.setColour (ParagonColours::panelEdge.withAlpha (k % 4 == 0 ? 0.9f : 0.35f));
        g.drawVerticalLine (x, grid.getY(), velocityArea (0).getBottom());
    }

    // The blocks.
    for (int k = 0; k < length; ++k)
    {
        const auto& b = shown.blocks[(size_t) k];
        if (! b.on)
            continue;
        const auto start = cell (k, b.transpose);
        const auto end = cell (juce::jmin (length - 1, k + b.length - 1), b.transpose);
        auto rect = start.withRight (end.getRight()).reduced (1.5f, 0.5f);
        rect = rect.withHeight (juce::jmax (rect.getHeight(), 10.0f)).withCentre (rect.getCentre());   // (rows are thin: blocks stand out)
        const auto colour = b.transpose % 12 == 0 ? ParagonColours::accent : amber;
        g.setColour (colour.withAlpha (0.35f + 0.6f * b.velocity));
        g.fillRoundedRectangle (rect, 2.5f);
        g.setColour (colour);
        g.drawRoundedRectangle (rect, 2.5f, 1.0f);
        if (rect.getHeight() >= 9.0f && rect.getWidth() >= 18.0f && b.transpose != 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ((b.transpose > 0 ? "+" : "") + juce::String (b.transpose), rect, juce::Justification::centred, false);
        }
        if (b.slide)
        {
            g.setColour (colour);
            g.drawLine (rect.getRight(), rect.getCentreY(), rect.getRight() + 6.0f, rect.getCentreY(), 2.0f);
        }

        // Its lanes: velocity, slide, ratchet.
        const auto v = velocityArea (k).reduced (3.0f, 2.0f);
        g.setColour (ParagonColours::textDim.withAlpha (0.5f));
        g.fillRect (v.withTop (v.getBottom() - v.getHeight() * b.velocity));
        const auto s = slideArea (k).reduced (3.0f, 3.0f);
        g.setColour (b.slide ? ParagonColours::accent : ParagonColours::panelEdge);
        g.fillRoundedRectangle (s, 2.0f);
        g.setColour (b.ratchet > 1 ? ParagonColours::accent : ParagonColours::textDim.withAlpha (0.5f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String (b.ratchet), ratchetArea (k), juce::Justification::centred, false);

        // CHORDS mode: the chord, its tones (lit: sounding) and the sub note.
        if (showsChords())
        {
            g.setColour (amber);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawFittedText (synth::arp::chordNames[b.chord], chordArea (k).reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
            const auto& shape = synth::arp::chordShape (b.chord);
            for (int t = 0; t < shape.count; ++t)
            {
                const auto r = toneArea (k, t).reduced (1.0f, 0.0f);
                g.setColour ((b.chordMask >> t) & 1 ? (t == 0 ? ParagonColours::accent : amber) : ParagonColours::panelEdge);
                g.fillRoundedRectangle (r, 1.5f);
            }
            g.setColour (b.sub ? ParagonColours::accent : ParagonColours::textDim.withAlpha (0.4f));
            g.drawText (b.sub ? juce::String (b.subTranspose) : juce::String ("-"), subArea (k), juce::Justification::centred, false);
        }
    }

    // The lanes' names.
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    const auto label = [&] (juce::Rectangle<float> lane, const char* text)
    {
        g.drawText (text, lane.withX (6.0f).withWidth (gutter - 2.0f), juce::Justification::centredLeft, false);
    };
    label (velocityArea (0), "VEL");
    label (slideArea (0), "SLIDE");
    label (ratchetArea (0), "RATCH");
    if (showsChords())
    {
        label (chordArea (0), "CHORD");
        label (tonesArea (0), "TONES");
        label (subArea (0), "SUB");
    }
}

void ArpPatternEditor::mouseDown (const juce::MouseEvent& e)
{
    shown = getShown();
    dragStep = -1;
    const auto at = e.position;
    const auto grid = gridArea();
    if (at.x < grid.getX() || at.x > grid.getRight())
        return;
    const auto k = stepAt (at.x);
    auto& b = shown.blocks[(size_t) k];

    if (grid.contains (at))
    {
        // A block covering this step at this transposition? (It may have started earlier.)
        const auto t = transposeAt (at.y);
        for (int start = k; start >= 0; --start)
        {
            const auto& other = shown.blocks[(size_t) start];
            if (other.on && start + other.length > k && other.transpose == t)
            {
                if (e.mods.isPopupMenu())
                {
                    shown.blocks[(size_t) start].on = false;
                    setShown (shown, true);
                    return;
                }
                const auto end = cell (juce::jmin (getLength() - 1, start + other.length - 1), t);
                dragStep = start;
                dragWhat = at.x > end.getRight() - 7.0f ? 1 : 0;
                return;
            }
        }
        if (e.mods.isPopupMenu())
            return;
        // A new block here (or this step's block moved to this transposition).
        if (! b.on)
            b = {};
        b.on = true;
        b.transpose = t;
        dragStep = k;
        dragWhat = 0;
        setShown (shown, false);
        return;
    }
    if (! b.on)
        return;
    if (velocityArea (k).contains (at))
    {
        dragStep = k;
        dragWhat = 2;
        mouseDrag (e);
    }
    else if (slideArea (k).contains (at))
    {
        b.slide = ! b.slide;
        setShown (shown, true);
    }
    else if (ratchetArea (k).contains (at))
    {
        b.ratchet = e.mods.isPopupMenu() ? (b.ratchet <= 1 ? synth::arp::maxRatchet : b.ratchet - 1)
                                          : (b.ratchet >= synth::arp::maxRatchet ? 1 : b.ratchet + 1);
        setShown (shown, true);
    }
    else if (! showsChords())
        return;
    else if (chordArea (k).contains (at))
    {
        juce::PopupMenu menu;
        for (int c = 0; c < synth::arp::chordNames.size(); ++c)
            menu.addItem (c + 1, synth::arp::chordNames[c], true, c == b.chord);
        juce::Component::SafePointer<ArpPatternEditor> self (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chordArea (k).toNearestInt())),
                            [self, k] (int result)
                            {
                                if (self == nullptr || result <= 0)
                                    return;
                                auto p = self->getShown();
                                p.blocks[(size_t) k].chord = result - 1;
                                self->setShown (p, true);
                            });
    }
    else if (tonesArea (k).contains (at))
    {
        for (int t = 0; t < synth::arp::chordShape (b.chord).count; ++t)
            if (toneArea (k, t).getHorizontalRange().contains (at.x))
            {
                b.chordMask ^= 1 << t;
                setShown (shown, true);
            }
    }
    else if (subArea (k).contains (at))
    {
        if (! e.mods.isPopupMenu())
        {
            b.sub = ! b.sub;
            setShown (shown, true);
            return;
        }
        juce::PopupMenu menu;
        for (const auto semitones : { -5, -7, -12, -17, -19, -24 })
            menu.addItem (semitones + 100, juce::String (semitones) + " semitones" + (semitones == -12 ? " (an octave)" : semitones == -24 ? " (two octaves)" : ""),
                          true, b.sub && b.subTranspose == semitones);
        juce::Component::SafePointer<ArpPatternEditor> self (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (subArea (k).toNearestInt())),
                            [self, k] (int result)
                            {
                                if (self == nullptr || result <= 0)
                                    return;
                                auto p = self->getShown();
                                p.blocks[(size_t) k].sub = true;
                                p.blocks[(size_t) k].subTranspose = result - 100;
                                self->setShown (p, true);
                            });
    }
}

void ArpPatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0)
        return;
    auto& b = shown.blocks[(size_t) dragStep];
    switch (dragWhat)
    {
        case 0: b.transpose = transposeAt (e.position.y); break;
        case 1: b.length = juce::jlimit (1, synth::arp::maxSteps, stepAt (e.position.x) - dragStep + 1); break;
        default:
        {
            const auto lane = velocityArea (dragStep).reduced (3.0f, 2.0f);
            b.velocity = (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, (lane.getBottom() - e.position.y) / lane.getHeight()) * 100.0f) / 100.0f;
            break;
        }
    }
    setShown (shown, false);
}

void ArpPatternEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragStep >= 0)
        setShown (shown, true);
    dragStep = -1;
}

//==============================================================================
ChanceBars::ChanceBars (const juce::String& t, const juce::StringArray& n)
    : values ((size_t) n.size(), 0.0f), dimmed ((size_t) n.size(), false), title (t), names (n)
{
}

juce::Rectangle<float> ChanceBars::barArea (int index) const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromTop (13.0f);
    area.removeFromBottom (12.0f);
    const auto w = area.getWidth() / (float) juce::jmax (1, names.size());
    return { area.getX() + w * (float) index, area.getY(), w, area.getHeight() };
}

void ChanceBars::paint (juce::Graphics& g)
{
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().removeFromTop (12), juce::Justification::centredLeft, false);
    for (int i = 0; i < names.size(); ++i)
    {
        const auto area = barArea (i).reduced (1.5f, 0.0f);
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillRect (area);
        const auto dim = dimmed[(size_t) i];
        g.setColour ((dim ? ParagonColours::textDim.withAlpha (0.25f) : ParagonColours::accent.withAlpha (0.85f)));
        g.fillRect (area.withTop (area.getBottom() - area.getHeight() * values[(size_t) i]));
        g.setColour (dim ? ParagonColours::textDim.withAlpha (0.4f) : ParagonColours::textDim);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (names[i], area.withY (area.getBottom() + 1.0f).withHeight (11.0f).expanded (3.0f, 0.0f), juce::Justification::centred, false);
    }
}

void ChanceBars::setFrom (juce::Point<float> at, bool finished)
{
    if (dragging < 0)
        return;
    const auto area = barArea (dragging);
    const auto value = (float) juce::roundToInt (juce::jlimit (0.0f, 1.0f, (area.getBottom() - at.y) / area.getHeight()) * 20.0f) / 20.0f;
    values[(size_t) dragging] = value;
    repaint();
    if (onChange)
        onChange (dragging, value, finished);
}

void ChanceBars::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    for (int i = 0; i < names.size(); ++i)
        if (barArea (i).getHorizontalRange().contains (e.position.x))
            dragging = i;
    setFrom (e.position, false);
}

void ChanceBars::mouseDrag (const juce::MouseEvent& e) { setFrom (e.position, false); }

void ChanceBars::mouseUp (const juce::MouseEvent& e)
{
    setFrom (e.position, true);
    dragging = -1;
}

//==============================================================================
MidiExportButton::MidiExportButton (ParagonX1AudioProcessor& p) : juce::TextButton ("EXPORT MIDI"), processor (p)
{
    setTooltip ("Click to save this arp's notes as a MIDI file; or drag this button onto a track in your song. "
                "It plays the keys held on the keyboard below (none: C4), one time through the pattern (CHAIN: each pattern; "
                "AUTODICE: four rolls), at the song's tempo.");
}

void MidiExportButton::mouseDrag (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDrag (e);
    if (dragged || e.getDistanceFromDragStart() < 6)
        return;
    dragged = true;
    const auto file = makeDragFile ? makeDragFile() : processor.arpMidiDragFile (arp);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void MidiExportButton::clicked()
{
    if (std::exchange (dragged, false))
        return;
    const auto name = fileName.isNotEmpty() ? fileName : "Arp " + juce::String (arp + 1);
    chooser = std::make_unique<juce::FileChooser> ("Save " + name + " as MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".mid"),
                                                   "*.mid");
    juce::Component::SafePointer<MidiExportButton> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [self] (const juce::FileChooser& c)
                          {
                              if (self == nullptr || c.getResult() == juce::File())
                                  return;
                              const auto file = c.getResult().withFileExtension ("mid");
                              if (self->exportTo)
                                  self->exportTo (file);
                              else
                                  self->processor.exportArpMidi (self->arp, file);
                          });
}

//==============================================================================
ArpNotesPanel::ArpNotesPanel (ParagonX1AudioProcessor& p)
    : processor (p),
      noteSource (p.getParameters(), ParamIDs::arp (0, "NoteSource"), "NOTE SOURCE",
                  "What the arp plays from: the KEYS you hold; FIXED NOTE: a held chord's root alone (the pattern's blocks make the tune); "
                  "FIXED NOTE +: the same, and the pattern follows the chord: written over a minor chord, its minor third, sixth and seventh "
                  "become major over a major one."),
      together (p.getParameters(), ParamIDs::arpsTogether, "ARPS TOGETHER",
                "All the arps counted from the first key held: one started later waits for the next step, so they stay in step "
                "(off: each runs from its own first key)."),
      notes ("NOTE CHANCES (above the key)", { "R", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7" }),
      octaves ("OCTAVES", { "-2", "-1", "0", "+1", "+2" }),
      lengths ("LENGTH (STEPS)", { "1", "2", "3", "4" }),
      more ("CHANCES", { "REST", "SLIDE", "RATCH", "VEL LO", "VEL HI" }),
      exportMidi (p)
{
    for (auto* label : { &sourceLabel, &randomLabel, &scaleLabel, &seedLabel, &rollLabel })
    {
        label->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, ParagonColours::textDim);
        addAndMakeVisible (*label);
    }
    sourceLabel.setText ("NOTE SOURCE", juce::dontSendNotification);
    randomLabel.setText ("RANDOMIZER", juce::dontSendNotification);
    randomLabel.setColour (juce::Label::textColourId, ParagonColours::text);
    scaleLabel.setText ("SCALE", juce::dontSendNotification);
    rollLabel.setText ("ROLLS", juce::dontSendNotification);
    noteSource.setCompact (true);
    together.setCompact (true);
    addAndMakeVisible (noteSource);
    addAndMakeVisible (together);

    scale.addItemList (synth::arp::scaleNames, 1);
    scale.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    scale.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    scale.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    scale.setTooltip ("The notes a roll may use (the others get no chance, and show dim)");
    scale.onChange = [this]
    {
        auto r = get();
        r.scale = juce::jmax (0, scale.getSelectedId() - 1);
        set (r, true);
    };
    addAndMakeVisible (scale);

    const auto style = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        b.setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
        b.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        b.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    };
    for (auto* b : { &roll, &seedDown, &seedUp, &autoDice, &symmetry, static_cast<juce::TextButton*> (&exportMidi) })
    {
        style (*b);
        addAndMakeVisible (*b);
    }
    roll.setColour (juce::TextButton::textColourOffId, ParagonColours::accent);
    roll.setTooltip ("A new pattern from the chances below (a new SEED each time; undo brings the last one back). "
                     "Only what's lit under ROLLS changes: the rest of the pattern stays.");
    roll.onClick = [this] { if (onRoll) onRoll(); };
    seedDown.setTooltip ("The SEED before: the pattern a ROLL would make with it");
    seedUp.setTooltip ("The SEED after");
    seedDown.onClick = [this] { auto r = get(); r.seed = r.seed <= 1 ? 9999 : r.seed - 1; set (r, true); };
    seedUp.onClick = [this] { auto r = get(); r.seed = r.seed >= 9999 ? 1 : r.seed + 1; set (r, true); };
    autoDice.setTooltip ("A new pattern rolled at the start of every time through (from this one and the chances), "
                         "the same each time you play the song. Not with CHAIN.");
    autoDice.onClick = [this] { auto r = get(); r.autoDice = ! r.autoDice; set (r, true); };
    symmetry.setTooltip ("The second half of each roll is the first half backwards");
    symmetry.onClick = [this] { auto r = get(); r.symmetry = ! r.symmetry; set (r, true); };
    const char* rollNames[] = { "NOTES", "OCT", "LEN", "VEL", "REST" };
    for (int i = 0; i < 5; ++i)
    {
        auto& b = rolls[(size_t) i];
        b.setButtonText (rollNames[i]);
        style (b);
        b.setTooltip ("Lit: ROLL changes each block's " + juce::String (rollNames[i]).toLowerCase() + "; dark: it's kept");
        b.onClick = [this, i]
        {
            auto r = get();
            auto* flags = std::array<bool*, 5> { &r.rollNotes, &r.rollOctaves, &r.rollLengths, &r.rollVelocity, &r.rollRests }[(size_t) i];
            *flags = ! *flags;
            set (r, true);
        };
        addAndMakeVisible (b);
    }

    notes.setTooltip ("How likely each note is, by semitones above the key you hold (R: the key itself)");
    octaves.setTooltip ("How likely each octave is");
    lengths.setTooltip ("How likely a block is to be 1, 2, 3 or 4 steps long");
    more.setTooltip ("REST: how likely a step is silent; SLIDE: glides into the next; RATCH: repeats 2-4 times; "
                     "VEL LO / HI: the softest and loudest a block can be");
    notes.onChange = [this] (int i, float v, bool finished) { auto r = get(); r.noteChances[(size_t) i] = v; set (r, finished); };
    octaves.onChange = [this] (int i, float v, bool finished) { auto r = get(); r.octaveChances[(size_t) i] = v; set (r, finished); };
    lengths.onChange = [this] (int i, float v, bool finished) { auto r = get(); r.lengthChances[(size_t) i] = v; set (r, finished); };
    more.onChange = [this] (int i, float v, bool finished)
    {
        auto r = get();
        float* fields[] = { &r.restChance, &r.slideChance, &r.ratchetChance, &r.velocityLow, &r.velocityHigh };
        *fields[i] = v;
        if (i == 3)
            r.velocityHigh = juce::jmax (r.velocityHigh, v);
        if (i == 4)
            r.velocityLow = juce::jmin (r.velocityLow, v);
        set (r, finished);
    };
    for (auto* bars : { &notes, &octaves, &lengths, &more })
        addAndMakeVisible (*bars);

    refresh();
    startTimerHz (8);
}

void ArpNotesPanel::setArp (int index)
{
    arp = juce::jlimit (0, synth::arp::numArps - 1, index);
    noteSource.attach (ParamIDs::arp (arp, "NoteSource"));
    exportMidi.arp = arp;
    shown = get();
    refresh();
}

synth::arp::Randomizer ArpNotesPanel::get() const
{
    return processor.getModMatrix().arpRandomizers[(size_t) arp];
}

void ArpNotesPanel::set (const synth::arp::Randomizer& r, bool finished)
{
    auto matrix = processor.getModMatrix();
    matrix.arpRandomizers[(size_t) arp] = r;
    processor.setModMatrix (matrix);
    if (finished)
        processor.modMatrixEditFinished();
    shown = r;
    refresh();
}

void ArpNotesPanel::refresh()
{
    const auto& r = shown;
    scale.setSelectedId (r.scale + 1, juce::dontSendNotification);
    seedLabel.setText ("SEED " + juce::String (r.seed), juce::dontSendNotification);
    autoDice.setToggleState (r.autoDice, juce::dontSendNotification);
    symmetry.setToggleState (r.symmetry, juce::dontSendNotification);
    const bool flags[] = { r.rollNotes, r.rollOctaves, r.rollLengths, r.rollVelocity, r.rollRests };
    for (size_t i = 0; i < 5; ++i)
        rolls[i].setToggleState (flags[i], juce::dontSendNotification);
    for (size_t i = 0; i < 12; ++i)
    {
        notes.values[i] = r.noteChances[i];
        notes.dimmed[i] = ! synth::arp::inScale (r.scale, (int) i);
    }
    for (size_t i = 0; i < 5; ++i)
        octaves.values[i] = r.octaveChances[i];
    for (size_t i = 0; i < 4; ++i)
        lengths.values[i] = r.lengthChances[i];
    more.values = { r.restChance, r.slideChance, r.ratchetChance, r.velocityLow, r.velocityHigh };
    for (auto* bars : { &notes, &octaves, &lengths, &more })
        bars->repaint();
}

void ArpNotesPanel::timerCallback()
{
    // Undo, presets and projects change it from outside.
    if (! (get() == shown))
    {
        shown = get();
        refresh();
    }
}

void ArpNotesPanel::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (randomLabel.getY() - 6.0f), 8.0f, (float) getWidth() - 8.0f);
    g.drawHorizontalLine (juce::roundToInt (exportMidi.getY() - 8.0f), 8.0f, (float) getWidth() - 8.0f);
}

void ArpNotesPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    auto row = area.removeFromTop (24);
    sourceLabel.setBounds (row.removeFromLeft (92));
    noteSource.setBounds (row);
    area.removeFromTop (14);

    row = area.removeFromTop (24);
    randomLabel.setBounds (row.removeFromLeft (100));
    roll.setBounds (row.removeFromRight (70));
    area.removeFromTop (6);
    row = area.removeFromTop (24);
    scaleLabel.setBounds (row.removeFromLeft (52));
    scale.setBounds (row);
    area.removeFromTop (6);
    row = area.removeFromTop (22);
    seedLabel.setBounds (row.removeFromLeft (76));
    seedDown.setBounds (row.removeFromLeft (24));
    row.removeFromLeft (2);
    seedUp.setBounds (row.removeFromLeft (24));
    row.removeFromLeft (8);
    const auto half = (row.getWidth() - 4) / 2;
    autoDice.setBounds (row.removeFromLeft (half));
    symmetry.setBounds (row.removeFromRight (half));

    // At the bottom: EXPORT MIDI and ARPS TOGETHER.
    auto bottom = area.removeFromBottom (26);
    exportMidi.setBounds (bottom.removeFromLeft ((bottom.getWidth() - 6) / 2));
    together.setBounds (bottom.removeFromRight ((bottom.getWidth() - 6)));
    area.removeFromBottom (16);

    area.removeFromTop (8);
    row = area.removeFromBottom (22);
    rollLabel.setBounds (row.removeFromLeft (46));
    const auto w = row.getWidth() / 5;
    for (auto& b : rolls)
        b.setBounds (row.removeFromLeft (w).reduced (1, 0));
    area.removeFromBottom (8);

    // The chances share what's left: notes, octaves | lengths, more.
    const auto h = (area.getHeight() - 16) / 3;
    notes.setBounds (area.removeFromTop (h));
    area.removeFromTop (8);
    row = area.removeFromTop (h);
    octaves.setBounds (row.removeFromLeft (row.getWidth() * 5 / 9 - 6));
    row.removeFromLeft (12);
    lengths.setBounds (row);
    area.removeFromTop (8);
    more.setBounds (area);
}

//==============================================================================
void ArpPage::ArpTab::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::TextButton::paintButton (g, highlighted, down);
    // A light when that arp is on.
    g.setColour (lit ? ParagonColours::glow : ParagonColours::panelEdge);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ 8.0f, (float) getHeight() * 0.5f }));
}

ArpPage::ArpPage (ParagonX1AudioProcessor& p)
    : processor (p), state (p.getParameters()), editor (p), notesPanel (p),
      on (state, ParamIDs::arp (0, "On"), "ON", "This arp on: the oscillators routed to it (ticked above) play its notes, not the keys'."),
      mode (state, ParamIDs::arp (0, "Mode"), "MODE", "How it walks through the keys you hold: UP, DOWN, UP/DOWN, DOWN/UP, AS PLAYED (the order you pressed them), "
                                                    "RANDOM; POLY plays them all together on each block; CHORDS plays each block's chord (its "
                                                    "CHORD, TONES and SUB lanes) on the next key."),
      speed (state, ParamIDs::arp (0, "Division"), "SPEED", "A step's length, in notes at the song's tempo (1/16: a sixteenth)."),
      timing (state, ParamIDs::arp (0, "Timing"), "TIMING", "Straight, triplet or dotted steps."),
      octaves (state, ParamIDs::arp (0, "Octaves"), "OCTAVES", "How many octaves the keys you hold are spread over (up to 4)."),
      gate (state, ParamIDs::arp (0, "Gate"), "GATE", "How much of its length each block sounds (shorter: more staccato). A SLIDE block always sounds to the next."),
      length (state, ParamIDs::arp (0, "Length"), "LENGTH", "How many steps before the pattern starts over: 2 to 32."),
      velocity (state, ParamIDs::arp (0, "Velocity"), "VELOCITY", "Which key's velocity the notes take: EACH NOTE its own, the FIRST key's, the LAST key's, "
                                                                  "or the LOWEST key's (times each block's VEL)."),
      lowKey (state, ParamIDs::arp (0, "LowKey"), "START NOTE", "The lowest key it takes. Keys outside START to END don't play this arp's oscillators."),
      highKey (state, ParamIDs::arp (0, "HighKey"), "END NOTE", "The highest key it takes."),
      humanize (state, ParamIDs::arp (0, "Humanize"), "HUMANIZE", "Each block a little late, at random (up to a quarter of a step): less machine-like."),
      strum (state, ParamIDs::arp (0, "Strum"), "STRUM", "POLY: the chord's notes one after another, low to high, over up to half a step, as a strummed guitar."),
      trigger (state, ParamIDs::arp (0, "Trigger"), "TRIGGER", "SONG: in step with your song while it plays (from the first key when it's stopped). KEY: from the first key, always."),
      shuffle (state, ParamIDs::shuffle, "SHUFFLE", "Swing: every second step late, up to half a step. The synth's own: shared with the step sequencer (and the drums later)."),
      chain (state, ParamIDs::arp (0, "Chain"), "CHAIN", "Play the patterns with blocks one after another (A, B, C, D), rather than just the one chosen.")
{
    for (int k = 0; k < synth::arp::numArps; ++k)
    {
        auto tab = std::make_unique<ArpTab> ("  " + juce::String (k + 1));
        styleButton (*tab);
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (951);
        tab->setTooltip ("ARP " + juce::String (k + 1) + " (its light is on when it is)");
        tab->onClick = [this, k] { selectArp (k); };
        addAndMakeVisible (*tab);
        arpTabs.push_back (std::move (tab));
    }
    for (int k = 0; k < synth::arp::numPatterns; ++k)
    {
        auto button = std::make_unique<juce::TextButton> (synth::arp::patternNames[k]);
        styleButton (*button);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (952);
        button->setTooltip ("Pattern " + synth::arp::patternNames[k] + ": edit it, and play it (unless CHAIN plays them all in turn)");
        button->onClick = [this, k] { selectPattern (k); };
        addAndMakeVisible (*button);
        patternButtons.push_back (std::move (button));
    }
    for (int i = 0; i < synth::numOscillators; ++i)
    {
        auto button = std::make_unique<juce::TextButton> (juce::String (i + 1));
        styleButton (*button);
        button->setTooltip ("Oscillator " + juce::String (i + 1) + "'s notes come from this arp (lit) or straight from the keys");
        button->onClick = [this, i] { route (i, ! isRouted (i)); };
        addAndMakeVisible (*button);
        oscillatorButtons.push_back (std::move (button));
    }
    for (auto* b : { &down, &up })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    down.setTooltip ("Every block of this pattern a semitone down");
    up.setTooltip ("Every block of this pattern a semitone up");
    down.onClick = [this] { shift (-1); };
    up.onClick = [this] { shift (1); };
    startFromBox.addItemList (startNames, 1);
    startFromBox.setTextWhenNothingSelected ("Choose...");
    startFromBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    startFromBox.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    startFromBox.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    startFromBox.setTooltip ("Start this pattern again from a ready-made one (undo brings yours back)");
    startFromBox.onChange = [this]
    {
        if (startFromBox.getSelectedId() > 0)
            startFrom (startFromBox.getSelectedId() - 1);
        startFromBox.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (startFromBox);
    for (auto* label : { &patternLabel, &startLabel, &oscillatorLabel, &hint })
    {
        label->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, ParagonColours::textDim);
        addAndMakeVisible (*label);
    }
    patternLabel.setText ("PATTERN", juce::dontSendNotification);
    startLabel.setText ("START FROM", juce::dontSendNotification);
    oscillatorLabel.setText ("OSCILLATORS", juce::dontSendNotification);
    hint.setText ("Route oscillators to the arp (OSCILLATORS, or each oscillator's ARP), switch it ON and hold some keys.", juce::dontSendNotification);
    hint.setJustificationType (juce::Justification::centredLeft);
    chain.setCompact (true);
    addAndMakeVisible (editor);
    addAndMakeVisible (notesPanel);
    notesPanel.onRoll = [this] { rollPattern(); };
    for (auto* c : controls())
        addAndMakeVisible (c);
    addAndMakeVisible (chain);
    selectArp (0);
    startTimerHz (8);
}

std::vector<ParamControl*> ArpPage::controls()
{
    return { &on, &mode, &speed, &timing, &octaves, &gate, &length, &velocity, &lowKey, &highKey, &humanize, &strum, &trigger, &shuffle };
}

void ArpPage::selectArp (int index)
{
    selected = juce::jlimit (0, synth::arp::numArps - 1, index);
    arpTabs[(size_t) selected]->setToggleState (true, juce::dontSendNotification);
    const std::pair<ParamControl*, const char*> list[] = { { &on, "On" }, { &mode, "Mode" }, { &speed, "Division" }, { &timing, "Timing" },
                                                           { &octaves, "Octaves" }, { &gate, "Gate" }, { &length, "Length" },
                                                           { &velocity, "Velocity" }, { &lowKey, "LowKey" }, { &highKey, "HighKey" },
                                                           { &humanize, "Humanize" }, { &strum, "Strum" }, { &trigger, "Trigger" }, { &chain, "Chain" } };
    for (const auto& [control, what] : list)
        control->attach (ParamIDs::arp (selected, what));
    const auto pattern = juce::roundToInt (state.getRawParameterValue (ParamIDs::arp (selected, "Pattern"))->load());
    patternButtons[(size_t) juce::jlimit (0, 3, pattern)]->setToggleState (true, juce::dontSendNotification);
    editor.setTarget (selected, pattern);
    notesPanel.setArp (selected);
    timerCallback();
}

void ArpPage::selectPattern (int index)
{
    if (auto* parameter = state.getParameter (ParamIDs::arp (selected, "Pattern")))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
    patternButtons[(size_t) index]->setToggleState (true, juce::dontSendNotification);
    editor.setTarget (selected, index);
}

void ArpPage::shift (int semitones)
{
    auto pattern = editor.getShown();
    for (auto& b : pattern.blocks)
        b.transpose = juce::jlimit (-36, 36, b.transpose + semitones);
    editor.setShown (pattern, true);
}

void ArpPage::startFrom (int choice)
{
    editor.setShown (startPattern (choice), true);
}

void ArpPage::rollPattern()
{
    auto matrix = processor.getModMatrix();
    auto& r = matrix.arpRandomizers[(size_t) selected];
    r.seed = r.seed >= 9999 ? 1 : r.seed + 1;
    const auto length = juce::roundToInt (state.getRawParameterValue (ParamIDs::arp (selected, "Length"))->load());
    auto& pattern = matrix.arps[(size_t) selected].patterns[(size_t) editor.getPattern()];
    pattern = synth::arp::roll (r, length, pattern, r.seed);
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    editor.setTarget (selected, editor.getPattern());
}

bool ArpPage::isRouted (int oscillator) const
{
    return juce::roundToInt (state.getRawParameterValue (ParamIDs::osc (oscillator, "Arp"))->load()) == selected + 1;
}

void ArpPage::route (int oscillator, bool toThisArp)
{
    if (auto* parameter = state.getParameter (ParamIDs::osc (oscillator, "Arp")))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (toThisArp ? (float) (selected + 1) : 0.0f));
        parameter->endChangeGesture();
    }
    timerCallback();
}

void ArpPage::timerCallback()
{
    // The tabs' lights, the routing buttons and the pattern chosen (automation, presets, undo move them).
    for (int k = 0; k < synth::arp::numArps; ++k)
    {
        const auto lit = state.getRawParameterValue (ParamIDs::arp (k, "On"))->load() > 0.5f;
        if (arpTabs[(size_t) k]->lit != lit)
        {
            arpTabs[(size_t) k]->lit = lit;
            arpTabs[(size_t) k]->repaint();
        }
    }
    for (int i = 0; i < synth::numOscillators; ++i)
        oscillatorButtons[(size_t) i]->setToggleState (isRouted (i), juce::dontSendNotification);
    const auto pattern = juce::jlimit (0, 3, juce::roundToInt (state.getRawParameterValue (ParamIDs::arp (selected, "Pattern"))->load()));
    if (pattern != editor.getPattern())
    {
        patternButtons[(size_t) pattern]->setToggleState (true, juce::dontSendNotification);
        editor.setTarget (selected, pattern);
    }
}

void ArpPage::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("ARP", juce::Rectangle<int> (14, 8, 40, 26), juce::Justification::centredLeft, false);
}

void ArpPage::resized()
{
    auto area = getLocalBounds().reduced (14, 8);
    auto strip = area.removeFromTop (26);
    strip.removeFromLeft (38);
    for (auto& tab : arpTabs)
    {
        tab->setBounds (strip.removeFromLeft (44));
        strip.removeFromLeft (3);
    }
    strip.removeFromLeft (24);
    patternLabel.setBounds (strip.removeFromLeft (62));
    for (auto& button : patternButtons)
    {
        button->setBounds (strip.removeFromLeft (30));
        strip.removeFromLeft (3);
    }
    strip.removeFromLeft (6);
    chain.setBounds (strip.removeFromLeft (84));
    strip.removeFromLeft (14);
    down.setBounds (strip.removeFromLeft (34));
    strip.removeFromLeft (3);
    up.setBounds (strip.removeFromLeft (34));
    strip.removeFromLeft (20);
    startLabel.setBounds (strip.removeFromLeft (76));
    startFromBox.setBounds (strip.removeFromLeft (170));
    for (auto it = oscillatorButtons.rbegin(); it != oscillatorButtons.rend(); ++it)
    {
        (*it)->setBounds (strip.removeFromRight (28));
        strip.removeFromRight (3);
    }
    oscillatorLabel.setBounds (strip.removeFromRight (86).withTrimmedLeft (4));

    area.removeFromTop (8);
    const auto h = ParamControl::knobHeight;
    const auto all = controls();
    SynthPanel::layoutRow (area.removeFromBottom (h), { all.begin(), all.end() });
    area.removeFromBottom (4);
    hint.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (4);
    notesPanel.setBounds (area.removeFromRight (320));
    area.removeFromRight (8);
    editor.setBounds (area);
}
