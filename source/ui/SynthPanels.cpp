#include "SynthPanels.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    void drawSectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour (ParagonColours::text.withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, area, juce::Justification::centredLeft, false);
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (11.0f, juce::Font::bold), text);
        g.setColour (ParagonColours::panelEdge);
        g.drawHorizontalLine (area.getCentreY(), (float) (area.getX() + textWidth + 8), (float) area.getRight());
    }
}

//==============================================================================
OscillatorPanel::Tab::Tab (juce::AudioProcessorValueTreeState& parameters, int tabIndex)
    : juce::Button ("OSC " + juce::String (tabIndex + 1)),
      index (tabIndex),
      powerAttachment (parameters, ParamIDs::osc (tabIndex, "On"), power)
{
    setRadioGroupId (701);
    setClickingTogglesState (true);
    power.setTooltip ("Switch oscillator " + juce::String (tabIndex + 1) + " on or off");
    addAndMakeVisible (power);
}

void OscillatorPanel::Tab::resized()
{
    power.setBounds (getLocalBounds().removeFromLeft (26).withSizeKeepingCentre (22, 22));
}

void OscillatorPanel::Tab::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const auto isSelected = getToggleState();
    g.setColour (isSelected ? juce::Colour (0xff1c1418) : juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (area, 4.0f);
    if (isSelected)
    {
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
        g.fillRect (area.withTop (area.getBottom() - 2.0f));
    }
    g.setColour (isSelected ? ParagonColours::accent.withAlpha (0.6f) : ParagonColours::panelEdge);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
    g.setColour (isSelected || highlighted ? ParagonColours::text : ParagonColours::textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (getName(), area.withTrimmedLeft (22.0f), juce::Justification::centred, false);
}

WavetableChooser::WavetableChooser (ParagonX1AudioProcessor& processorToUse) : processor (processorToUse)
{
    label.setText ("TABLE", juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, ParagonColours::textDim);
    label.setBorderSize ({});
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    box.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    box.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    box.setTooltip ("The wavetable: a row of waves that INDEX moves through. Our own factory tables, or load your own "
                    "WAV file (or drop one on the oscillator). A loaded table is saved inside the preset and the project.");
    box.onChange = [this]
    {
        const auto id = box.getSelectedId();
        if (id == loadFileId)
        {
            refresh();   // back to what plays, until a file is chosen
            browse();
        }
        else if (id >= 1 && id <= synth::factoryTables::names.size())
        {
            processor.chooseFactoryWavetable (oscillator, id - 1);
            refresh();
        }
    };
    addAndMakeVisible (box);
    refresh();
}

void WavetableChooser::setOscillator (int index)
{
    oscillator = index;
    refresh();
}

void WavetableChooser::refresh()
{
    // The factory's tables, the loaded one (if any), and loading another.
    box.clear (juce::dontSendNotification);
    box.addItemList (synth::factoryTables::names, 1);
    box.addSeparator();
    if (processor.hasLoadedWavetable (oscillator))
        box.addItem (processor.getWavetable (oscillator)->getName(), loadedId);
    box.addItem ("Load WAV file...", loadFileId);

    if (processor.hasLoadedWavetable (oscillator))
        box.setSelectedId (loadedId, juce::dontSendNotification);
    else if (const auto* table = processor.getParameters().getRawParameterValue (ParamIDs::osc (oscillator, "Table")))
        box.setSelectedId (juce::roundToInt (table->load()) + 1, juce::dontSendNotification);
}

void WavetableChooser::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a wavetable (WAV, AIFF or FLAC)", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    const auto target = oscillator;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, target] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile() && ! processor.loadWavetableFile (target, file))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't load that file",
                                                                          file.getFileName() + " couldn't be read as audio.");
                              refresh();
                          });
}

void WavetableChooser::resized()
{
    // One line: TABLE, then the menu filling the rest (long table names fit).
    auto area = getLocalBounds();
    label.setJustificationType (juce::Justification::centredLeft);
    label.setBounds (area.removeFromLeft (46));
    box.setBounds (area.withSizeKeepingCentre (area.getWidth(), juce::jmin (26, area.getHeight())));
}

//==============================================================================
OscillatorPanel::OscillatorPanel (ParagonX1AudioProcessor& processorToUse)
    : SynthPanel ("OSCILLATORS"), processor (processorToUse), state (processorToUse.getParameters()),
      tableChooser (processorToUse), wave (state), voicesDisplay (state), shapeDisplay (state)
{
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        auto tab = std::make_unique<Tab> (state, i);
        tab->onClick = [this, i] { selectOscillator (i); };
        addAndMakeVisible (*tab);
        tabs.push_back (std::move (tab));
    }
    wave.tableFor = [this] (int osc) { return processor.getWavetable (osc); };
    shapeDisplay.tableFor = wave.tableFor;
    addAndMakeVisible (wave);
    addChildComponent (voicesDisplay);
    addChildComponent (shapeDisplay);
    addChildComponent (tableChooser);

    // WAVE / SHAPE / FM / VOICES: which of the oscillator's pages shows.
    for (auto* button : { &wavePageButton, &shapePageButton, &fmPageButton, &voicesPageButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (702);
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        button->setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
        button->setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        button->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
        addAndMakeVisible (*button);
    }
    wavePageButton.setTooltip ("The oscillator's wave, pitch, start, noise, sub and level");
    voicesPageButton.setTooltip ("The oscillator's unison copies (supersaw), vibrato and chorder");
    shapePageButton.setTooltip ("Bend the oscillator's wave: SKEW, PINCH, FORMANT and CRUSH");
    wavePageButton.setToggleState (true, juce::dontSendNotification);
    wavePageButton.onClick = [this] { showPage (Page::wave); };
    shapePageButton.onClick = [this] { showPage (Page::shape); };
    fmPageButton.setTooltip ("The oscillator's SYNC, FM and AM");
    fmPageButton.onClick = [this] { showPage (Page::fm); };
    voicesPageButton.onClick = [this] { showPage (Page::voices); };

    // Each control, labelled as on the panel, with a tooltip saying what it does.
    const std::pair<const char*, std::pair<const char*, const char*>> items[] = {
        { "Wave", { "WAVE", "The oscillator's wave. PULSE's width is set by WIDTH. WAVETABLE plays a row of waves (chosen under TABLE) that INDEX moves through." } },
        { "Volume", { "VOLUME", "The whole oscillator: wave, noise and sub together." } },
        { "Pan", { "PAN", "Where the oscillator sits, left to right." } },
        { "Gain", { "GAIN", "The wave alone: turn it down to hear just the noise or sub." } },
        { "Width", { "WIDTH", "PULSE's width: 50% is a square; narrower is thinner and nasal." } },
        { "Transpose", { "TRANSPOSE", "The oscillator's pitch, in semitones (12 = an octave)." } },
        { "Fine", { "FINE", "Fine tuning, in cents (hundredths of a semitone): a few cents between two oscillators makes them beat and thicken." } },
        { "Random", { "RANDOM", "Each note slightly out of tune, as on an old analog synth. Left of centre: a random amount each note. Right: alternately sharp and flat." } },
        { "Phase", { "PHASE", "Where in its wave each note starts (when RND PHASE is off): the same start every time gives the same attack every time." } },
        { "PhaseRand", { "RND PHASE", "Start each note at a random point in the wave, like a free-running analog oscillator." } },
        { "FadeIn", { "FADE IN", "A tiny fade at the start of each note (up to 5 ms), against clicks." } },
        { "Invert", { "INVERT", "The wave (and sub) upside down: against a second oscillator, it can cancel or hollow out the sound." } },
        { "Filter", { "TO FILTER", "Through the FILTER (lit) or straight to the AMP." } },
        { "Voices", { "VOICES", "This oscillator's own limit on notes at once. 1 makes just this oscillator mono, e.g. a bass under chords." } },
        { "Noise", { "NOISE", "Noise mixed in with the wave." } },
        { "NoiseColour", { "COLOUR", "From white (bright hiss) through pink to brown (a deep rumble)." } },
        { "NoiseWidth", { "STEREO", "Noise different in each speaker (Stereo), half that, or the same in both (Mono)." } },
        { "Sub", { "SUB", "A sub oscillator below the note: weight without another full oscillator." } },
        { "SubWave", { "WAVE", "The sub's wave." } },
        { "SubOct", { "OCTAVE", "How far below the note the sub plays." } },
        { "SubInvert", { "INVERT", "The sub upside down, against the main wave." } },
        { "WtIndex", { "INDEX", "Where in the wavetable: each frame is a different wave, and moving INDEX morphs from one to the next." } },
        { "WtMode", { "MODE", "SMOOTH blends between neighbouring frames as INDEX moves; STEPPY jumps from one to the next, for a stepped, digital movement." } },
        { "WtFrames", { "FRAMES", "How many of the table's frames INDEX moves through, spread evenly from the first to the last: fewer makes broader, simpler steps." } },

        // The SHAPE page (stage 3b).
        { "Skew", { "SKEW", "Squashes one side of the wave and stretches the other: right of centre the left side, left of centre the right side. Brighter, more nasal, the further you go." } },
        { "Pinch", { "PINCH", "Zooms into the middle of the wave (right), smoothing it there and sharpening its edges, or out of it (left), squeezing the middle." } },
        { "Formant", { "FORMANT", "Left: the wave plays up to 8 times faster with a gap after it, a vocal, \"formant\" sound whose pitch stays put. Right: up to 4 times slower, so only its start fits in a cycle." } },
        { "FormantMode", { "MODE", "What FORMANT does at the edges of the wave it plays: CUT leaves a hard edge (a buzzy click), FADE fades its end to silence, X-FADE fades both ends, X-FADE 2 fades its end into its start, so it never jumps." } },
        { "Crush", { "CRUSH", "Lo-fi: at full, RATE holds the wave in 3 steps a cycle and BIT rounds it to a square." } },
        { "CrushMode", { "MODE", "RATE: the wave held in steps (fewer, the more CRUSH). BIT: rounded to fewer levels. BOTH: both at once." } },

        // The FM page (stage 3c).
        { "SyncAmount", { "SYNC", "A copy of the wave restarted at the start of every cycle, running faster: up to 5 octaves above the note. The classic tearing \"sync\" sound, especially swept." } },
        { "SyncMode", { "MODE", "PURE: the synced copy alone. ADD: with the wave. WIN: the copy fades in and out each cycle, so there's no edge (smoother)." } },
        { "SyncEnv", { "ENV", "SYNC's own envelope: how far it sweeps SYNC up (right) or down (left) from each note's start, falling back over LENGTH." } },
        { "SyncEnvTime", { "LENGTH", "How long SYNC's envelope takes to fall back." } },
        { "SyncEnvCurve", { "CURVE", "The envelope's shape: left falls fast at first, right starts slow." } },
        { "FmAmount", { "FM", "Frequency modulation: the wave read ahead and behind as the modulator swings. A little thickens, more makes bells, basses and metallic tones." } },
        { "FmSource", { "SOURCE", "The modulator: a shape at RATE times the note, or another oscillator (which is then only used to modulate, not heard itself)." } },
        { "AmAmount", { "AM", "Amplitude modulation: the wave's level swung by the modulator. At 100%, ring modulation." } },
        { "AmShape", { "SHAPE", "AM's modulator shape (at RATE times the note)." } },
        { "ModRate", { "RATE", "How fast FM and AM's modulator runs against the note; GRID decides which rates it can land on. The display shows the ratio." } },
        { "ModGrid", { "GRID", "Which rates RATE allows: FREE any (1/16 to 16 times the note); the others step through octaves and an interval, which sound cleaner." } },
        { "ModLock", { "LOCK", "The modulator starts in step with the wave on every note (lit), so each note sounds the same, or anywhere." } },

        // The VOICES page (stage 2).
        { "UniVoices", { "VOICES", "How many copies of each note, the note itself included (2 to 7)." } },
        { "UniMix", { "MIX", "The extra copies' level. At 0 unison is off and costs nothing; at 100% the copies are as loud as the note." } },
        { "UniDetune", { "DETUNE", "How far apart the copies are tuned: a few cents thickens, more makes the classic supersaw. The value is how far the outer copies are from the note." } },
        { "UniSpread", { "SPREAD", "How wide the copies spread across the stereo field." } },
        { "UniOctaves", { "OCTAVES", "Stacks the extra copies up to this many octaves above the note, in turn: a bigger, brighter stack." } },
        { "VibMode", { "MODE", "GLOBAL: every copy wobbles together. INDEPENDENT: each on its own, a little faster or slower, for a wider, livelier sound." } },
        { "VibWave", { "SHAPE", "The vibrato's shape. RANDOM jumps to a new pitch each cycle." } },
        { "VibRate", { "RATE", "How fast the pitch wobbles." } },
        { "VibAmount", { "AMOUNT", "How far the pitch wobbles either side, in cents. 0 is off." } },
        { "VibFade", { "FADE IN", "The vibrato grows in over this long after each note starts, as a singer's or a violinist's does." } },
        { "VibCurve", { "CURVE", "The fade's shape: left grows fast at first, right starts slow." } },
    };
    for (const auto& [what, text] : items)
    {
        controls.push_back (std::make_unique<ParamControl> (state, ParamIDs::osc (0, what), text.first, text.second));
        settingNames.push_back (what);
        addAndMakeVisible (*controls.back());
    }

    // The chorder's four notes: ON, COARSE, FINE, PAN, VOL each.
    static const juce::StringArray chordNames { "Ch1On", "Ch1Coarse", "Ch1Fine", "Ch1Pan", "Ch1Vol", "Ch2On", "Ch2Coarse", "Ch2Fine", "Ch2Pan", "Ch2Vol",
                                                "Ch3On", "Ch3Coarse", "Ch3Fine", "Ch3Pan", "Ch3Vol", "Ch4On", "Ch4Coarse", "Ch4Fine", "Ch4Pan", "Ch4Vol" };
    const std::pair<const char*, const char*> chordLabels[] = {
        { "ON", "Plays this extra note with every key." },
        { "COARSE", "How far from the key, in semitones: +12 an octave up, +7 a fifth, -12 an octave down." },
        { "FINE", "Fine tuning, in cents." },
        { "PAN", "Where this note sits, left to right (added to the oscillator's PAN)." },
        { "VOL", "This note's level." } };
    for (int i = 0; i < chordNames.size(); ++i)
    {
        controls.push_back (std::make_unique<ParamControl> (state, ParamIDs::osc (0, chordNames[i].toRawUTF8()),
                                                            chordLabels[i % 5].first, chordLabels[i % 5].second));
        settingNames.push_back (chordNames.getReference (i).toRawUTF8());   // static: the text stays put
        addAndMakeVisible (*controls.back());
    }
    selectOscillator (0);
    showPage (Page::wave);
    startTimerHz (10);
}

bool OscillatorPanel::isWavetable() const
{
    const auto* waveParameter = state.getRawParameterValue (ParamIDs::osc (selected, "Wave"));
    return waveParameter != nullptr && juce::roundToInt (waveParameter->load()) == (int) synth::OscWave::wavetable;
}

bool OscillatorPanel::isShown (const char* what) const
{
    const juce::String name (what);
    const auto leftColumn = name == "Wave" || name == "Width" || name == "Gain";   // under the wave picture, on SHAPE and FM too
    if (pageOf (what) != page && ! (leftColumn && (page == Page::shape || page == Page::fm)))
        return false;
    if (name.startsWith ("Wt"))
        return showingWavetable;
    if (name == "Width")
        return ! showingWavetable;
    return true;
}

void OscillatorPanel::updateVisibility()
{
    showingWavetable = isWavetable();
    for (size_t i = 0; i < controls.size(); ++i)
        controls[i]->setVisible (isShown (settingNames[i]));
    tableChooser.setVisible (page != Page::voices && showingWavetable);
    resized();
    repaint();
}

void OscillatorPanel::timerCallback()
{
    // The WAVE menu (or automation, a preset, undo) can switch WAVETABLE on or off.
    if (isWavetable() != showingWavetable)
        updateVisibility();
    if (showingWavetable)
        tableChooser.refresh();
}

bool OscillatorPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;aif;aiff;flac");
}

void OscillatorPanel::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;
    const juce::File file (files[0]);
    if (! processor.loadWavetableFile (selected, file))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't load that file",
                                                file.getFileName() + " couldn't be read as audio.");
    updateVisibility();
    tableChooser.refresh();
}

OscillatorPanel::Page OscillatorPanel::pageOf (const char* what) const
{
    const juce::String name (what);
    if (name.startsWith ("Uni") || name.startsWith ("Vib") || name.startsWith ("Ch"))
        return Page::voices;
    if (juce::StringArray { "Skew", "Pinch", "Formant", "FormantMode", "Crush", "CrushMode" }.contains (name))
        return Page::shape;
    if (name.startsWith ("Sync") || name.startsWith ("Fm") || name.startsWith ("Am") || name.startsWith ("Mod"))
        return Page::fm;
    return Page::wave;
}

void OscillatorPanel::showPage (Page newPage)
{
    page = newPage;
    (page == Page::voices ? voicesPageButton : page == Page::shape ? shapePageButton : page == Page::fm ? fmPageButton : wavePageButton)
        .setToggleState (true, juce::dontSendNotification);
    wave.setVisible (page != Page::voices);
    voicesDisplay.setVisible (page == Page::voices);
    shapeDisplay.setVisible (page == Page::shape || page == Page::fm);
    updateVisibility();
}

ParamControl& OscillatorPanel::control (const char* what)
{
    for (size_t i = 0; i < settingNames.size(); ++i)
        if (juce::String (settingNames[i]) == what)
            return *controls[i];
    jassertfalse;
    return *controls.front();
}

void OscillatorPanel::selectOscillator (int index)
{
    selected = juce::jlimit (0, ParamIDs::numOscillators - 1, index);
    tabs[(size_t) selected]->setToggleState (true, juce::dontSendNotification);
    for (size_t i = 0; i < controls.size(); ++i)
        controls[i]->attach (ParamIDs::osc (selected, settingNames[i]));
    wave.setOscillator (selected);
    voicesDisplay.setOscillator (selected);
    shapeDisplay.setOscillator (selected);
    tableChooser.setOscillator (selected);
    updateVisibility();
}

void OscillatorPanel::paint (juce::Graphics& g)
{
    SynthPanel::paint (g);
    for (const auto& [area, text] : sectionTitles)
        drawSectionTitle (g, area, text);
}

void OscillatorPanel::resized()
{
    // The tabs run along the title strip, right of the title; WAVE and VOICES at its end.
    auto tabStrip = getTitleArea().withTrimmedLeft (110).reduced (0, 5).withTrimmedTop (2);
    voicesPageButton.setBounds (tabStrip.removeFromRight (66));
    tabStrip.removeFromRight (4);
    fmPageButton.setBounds (tabStrip.removeFromRight (40));
    tabStrip.removeFromRight (4);
    shapePageButton.setBounds (tabStrip.removeFromRight (62));
    tabStrip.removeFromRight (4);
    wavePageButton.setBounds (tabStrip.removeFromRight (56));
    tabStrip.removeFromRight (12);
    const auto tabWidth = tabStrip.getWidth() / (int) tabs.size();
    for (auto& tab : tabs)
        tab->setBounds (tabStrip.removeFromLeft (tabWidth).reduced (2, 0));

    auto area = getContentArea().withTrimmedTop (6);
    const auto h = ParamControl::knobHeight;
    sectionTitles.clear();

    // A row of titled sections on a grid of `columns` across `row`.
    const auto placeRow = [&] (juce::Rectangle<int> row, int columns, std::vector<std::pair<juce::String, std::vector<const char*>>> sections)
    {
        const auto column = row.getWidth() / columns;
        auto titles = row.removeFromTop (18);
        auto x = row.getX();
        for (const auto& [text, items] : sections)
        {
            const auto width = column * (int) items.size();
            sectionTitles.push_back ({ titles.withX (x).withWidth (width - 10), text });
            for (size_t i = 0; i < items.size(); ++i)
                control (items[i]).setBounds (x + column * (int) i, row.getY() + 2, column - 4, h);
            x += width;
        }
    };

    auto left = area.removeFromLeft (264);
    area.removeFromLeft (16);
    const auto rowHeight = (area.getHeight() - 2 * 6) / 3;

    if (page == Page::voices)
    {
        // Left: the copies' display, then UNISON's knobs.
        voicesDisplay.setBounds (left.removeFromTop (left.getHeight() - 2 * h - 16));
        left.removeFromTop (10);
        SynthPanel::layoutRow (left.removeFromTop (h), { &control ("UniVoices"), &control ("UniMix"), &control ("UniDetune") }, 84);
        left.removeFromTop (6);
        SynthPanel::layoutRow (left.removeFromTop (h), { &control ("UniSpread"), &control ("UniOctaves") }, 84);

        // Right: VIBRATO, then the chorder's four notes, two to a row.
        placeRow (area.removeFromTop (rowHeight), 7, { { "VIBRATO", { "VibMode", "VibWave", "VibRate", "VibAmount", "VibFade", "VibCurve" } } });
        area.removeFromTop (6);
        placeRow (area.removeFromTop (rowHeight), 10, { { "CHORD 1", { "Ch1On", "Ch1Coarse", "Ch1Fine", "Ch1Pan", "Ch1Vol" } },
                                                        { "CHORD 2", { "Ch2On", "Ch2Coarse", "Ch2Fine", "Ch2Pan", "Ch2Vol" } } });
        area.removeFromTop (6);
        placeRow (area.removeFromTop (rowHeight), 10, { { "CHORD 3", { "Ch3On", "Ch3Coarse", "Ch3Fine", "Ch3Pan", "Ch3Vol" } },
                                                        { "CHORD 4", { "Ch4On", "Ch4Coarse", "Ch4Fine", "Ch4Pan", "Ch4Vol" } } });
        return;
    }

    // Left: the wave, with its WAVE menu, WIDTH and GAIN under it. A
    // wavetable's TABLE gets a line of its own between them (names are long).
    wave.setBounds (left.removeFromTop (left.getHeight() - h - 10 - (showingWavetable ? 34 : 0)));
    if (showingWavetable)
    {
        left.removeFromTop (6);
        tableChooser.setBounds (left.removeFromTop (28));
    }
    left.removeFromTop (10);
    if (showingWavetable)
        SynthPanel::layoutRow (left.removeFromTop (h), { &control ("Wave"), &control ("Gain") }, 96, 40);
    else
        SynthPanel::layoutRow (left.removeFromTop (h), { &control ("Wave"), &control ("Width"), &control ("Gain") }, 84);

    if (page == Page::shape)
    {
        // Right: the controls in a row, and the plain and bent waves, large, under them.
        placeRow (area.removeFromTop (rowHeight), 7, { { "BEND", { "Skew", "Pinch" } },
                                                       { "FORMANT", { "Formant", "FormantMode" } },
                                                       { "CRUSH", { "Crush", "CrushMode" } } });
        area.removeFromTop (12);
        shapeDisplay.setBounds (area);
        return;
    }
    if (page == Page::fm)
    {
        // Right: SYNC and its envelope, then FM, AM and their modulator, and the result under them.
        placeRow (area.removeFromTop (rowHeight), 7, { { "SYNC", { "SyncAmount", "SyncMode" } },
                                                       { "SYNC ENVELOPE", { "SyncEnv", "SyncEnvTime", "SyncEnvCurve" } } });
        area.removeFromTop (6);
        placeRow (area.removeFromTop (rowHeight), 7, { { "FM", { "FmAmount", "FmSource" } },
                                                       { "AM", { "AmAmount", "AmShape" } },
                                                       { "MODULATOR", { "ModRate", "ModGrid", "ModLock" } } });
        area.removeFromTop (10);
        shapeDisplay.setBounds (area);
        return;
    }

    // Right: three rows of titled sections on one grid of columns.
    placeRow (area.removeFromTop (rowHeight), 7, { { "PITCH", { "Transpose", "Fine", "Random" } },
                                                   { "START", { "Phase", "PhaseRand", "FadeIn", "Invert" } } });
    area.removeFromTop (6);
    placeRow (area.removeFromTop (rowHeight), 7, { { "NOISE", { "Noise", "NoiseColour", "NoiseWidth" } },
                                                   { "SUB", { "Sub", "SubWave", "SubOct", "SubInvert" } } });
    area.removeFromTop (6);
    if (showingWavetable)
        placeRow (area.removeFromTop (rowHeight), 7, { { "LEVEL", { "Volume", "Pan" } },
                                                       { "NOTES", { "Voices", "Filter" } },
                                                       { "WAVETABLE", { "WtIndex", "WtMode", "WtFrames" } } });
    else
        placeRow (area.removeFromTop (rowHeight), 7, { { "LEVEL", { "Volume", "Pan" } },
                                                       { "NOTES", { "Voices", "Filter" } } });
}

//==============================================================================
FilterPanel::FilterPanel (juce::AudioProcessorValueTreeState& state)
    : SynthPanel ("FILTER"), display (state),
      on (state, ParamIDs::fltOn, "ON", "Switch the filter on. Each oscillator chooses whether it goes through it (TO FILTER)."),
      type (state, ParamIDs::fltType, "TYPE", "Low-pass (LP) keeps the lows, high-pass (HP) the highs, band-pass (BP) a band, band-stop (BS) cuts one. "
                                              "The number is the slope in dB per octave: higher is steeper."),
      cutoff (state, ParamIDs::fltCutoff, "CUTOFF", "Where the filter starts to cut."),
      resonance (state, ParamIDs::fltRes, "RESONANCE", "A peak at the cutoff: from none to a whistle. (The 6 dB types have none.)"),
      drive (state, ParamIDs::fltDrive, "DRIVE", "Saturation going into the filter, for grit and warmth."),
      keytrack (state, ParamIDs::fltKey, "KEYTRACK", "How far the cutoff follows the note: at 100% it moves an octave with each octave you play."),
      envelope (state, ParamIDs::fltEnv, "ENVELOPE", "How far the filter envelope moves the cutoff, up (right) or down (left)."),
      velocity (state, ParamIDs::fltVel, "VELOCITY", "Softer notes close the cutoff: how much."),
      resonanceVelocity (state, ParamIDs::fltResVel, "RES VEL", "Softer notes have less resonance: how much."),
      envelopeVelocity (state, ParamIDs::fltEnvVel, "ENV VEL", "Softer notes move the filter less: how much.")
{
    addAndMakeVisible (display);
    for (auto* control : { &on, &type, &cutoff, &resonance, &drive, &keytrack, &envelope, &velocity, &resonanceVelocity, &envelopeVelocity })
        addAndMakeVisible (control);
    on.setCompact (true);
    type.setCompact (true);
}

void FilterPanel::resized()
{
    // ON and TYPE beside the title.
    auto titleArea = getTitleArea().withTrimmedTop (4);
    type.setBounds (titleArea.removeFromRight (96));
    titleArea.removeFromRight (10);
    on.setBounds (titleArea.removeFromRight (56));

    auto area = getContentArea().withTrimmedTop (6);
    display.setBounds (area.removeFromTop (136));
    area.removeFromTop (8);
    const auto h = ParamControl::knobHeight;
    SynthPanel::layoutRow (area.removeFromTop (h), { &cutoff, &resonance, &drive, &keytrack, &envelope }, 86);
    area.removeFromTop (4);
    SynthPanel::layoutRow (area.removeFromTop (h), { &velocity, &resonanceVelocity, &envelopeVelocity }, 86);
}

//==============================================================================
EnvelopePanel::EnvelopePanel (juce::AudioProcessorValueTreeState& state, const juce::String& panelTitle, std::array<juce::String, 8> ids)
    : SynthPanel (panelTitle), display (state, ids)
{
    addAndMakeVisible (display);
    const std::pair<const char*, const char*> labels[] = {
        { "ATTACK", "How long the note takes to reach full level." },
        { "HOLD", "How long it stays at full level before the decay." },
        { "DECAY", "How long it takes to fall to the SUSTAIN level." },
        { "SUSTAIN", "The level it holds while the key is down." },
        { "RELEASE", "How long it takes to fade after the key is let go." },
        { "A CURVE", "The attack's shape: left rises fast then slows, right starts slow then speeds up." },
        { "D CURVE", "The decay's shape: left falls fast then slows (as most real sounds do)." },
        { "R CURVE", "The release's shape: left falls fast then slows (as most real sounds do)." } };
    for (size_t i = 0; i < ids.size(); ++i)
    {
        controls.push_back (std::make_unique<ParamControl> (state, ids[i], labels[i].first, labels[i].second));
        addAndMakeVisible (*controls.back());
    }
}

void EnvelopePanel::resized()
{
    auto area = getContentArea().withTrimmedTop (4);
    display.setBounds (area.removeFromTop (64));
    area.removeFromTop (6);
    const auto h = ParamControl::knobHeight;
    SynthPanel::layoutRow (area.removeFromTop (h), { controls[0].get(), controls[1].get(), controls[2].get(), controls[3].get(), controls[4].get() }, 68);
    area.removeFromTop (2);
    SynthPanel::layoutRow (area.removeFromTop (h), { controls[5].get(), controls[6].get(), controls[7].get() }, 68);
}

//==============================================================================
AmpPanel::AmpPanel (juce::AudioProcessorValueTreeState& state)
    : SynthPanel ("AMP"),
      volume (state, ParamIDs::ampVolume, "VOLUME", "The sound's level after the filter."),
      velocity (state, ParamIDs::ampVel, "VELOCITY", "How much playing harder makes it louder: 0% ignores how hard you play."),
      pan (state, ParamIDs::ampPan, "PAN", "Where the whole sound sits, left to right."),
      panKeytrack (state, ParamIDs::ampPanKey, "PAN KEY", "Low notes to one side and high notes to the other, as on a piano. Right: low notes left."),
      spread (state, ParamIDs::ampSpread, "SPREAD", "Each note somewhere else in the stereo field. Right: alternating left and right. Left: anywhere, at random."),
      spike (state, ParamIDs::ampSpike, "SPIKE", "An extra kick of level at the very start of each note (20 ms), for a sharper attack."),
      spikeCurve (state, ParamIDs::ampSpikeCurve, "SPK CURVE", "The spike's shape: left dies away fast, right holds then drops."),
      spikeVelocity (state, ParamIDs::ampSpikeVel, "SPK VEL", "Softer notes get less spike: how much.")
{
    for (auto* control : { &volume, &velocity, &pan, &panKeytrack, &spread, &spike, &spikeCurve, &spikeVelocity })
        addAndMakeVisible (control);
}

void AmpPanel::resized()
{
    auto area = getContentArea().withTrimmedTop (8);
    const auto h = ParamControl::knobHeight;
    SynthPanel::layoutRow (area.removeFromTop (h), { &volume, &velocity, &pan, &panKeytrack }, 78);
    area.removeFromTop (14);
    SynthPanel::layoutRow (area.removeFromTop (h), { &spread, &spike, &spikeCurve, &spikeVelocity }, 78);
}

//==============================================================================
PlayPanel::PlayPanel (ParagonX1AudioProcessor& p)
    : SynthPanel ("PLAY"), processor (p),
      mode (p.getParameters(), ParamIDs::playMode, "MODE", "POLY plays chords. MONO plays one note at a time (the last key); MONO LOW and HIGH play the lowest or highest key held."),
      legato (p.getParameters(), ParamIDs::legato, "LEGATO", "Mono: a note played before the last is let go carries on, without restarting its envelopes."),
      glide (p.getParameters(), ParamIDs::glide, "GLIDE", "Slides from one note's pitch to the next, taking this long."),
      glideMode (p.getParameters(), ParamIDs::glideMode, "GLIDE ON", "ALWAYS: every note slides from the last. LEGATO: only notes played while another is held."),
      bendUp (p.getParameters(), ParamIDs::bendUp, "BEND UP", "How far the pitch wheel bends up, in semitones."),
      bendDown (p.getParameters(), ParamIDs::bendDown, "BEND DOWN", "How far the pitch wheel bends down, in semitones."),
      voices (p.getParameters(), ParamIDs::voices, "VOICES", "Notes at once. Fewer saves CPU (long releases keep notes going); beyond it, the quietest fading note makes way."),
      level (p.getParameters(), ParamIDs::patchLevel, "LEVEL", "This preset's own level, saved with it: for balancing presets. OUTPUT on the right is the overall level.")
{
    for (auto* control : { &mode, &legato, &glide, &glideMode, &bendUp, &bendDown, &voices, &level })
        addAndMakeVisible (control);
    startTimerHz (10);
}

void PlayPanel::timerCallback()
{
    if (const auto now = processor.getActiveVoices(); now != shownVoices)
    {
        shownVoices = now;
        repaint (getTitleArea());
    }
}

void PlayPanel::paint (juce::Graphics& g)
{
    SynthPanel::paint (g);
    // How many notes are sounding, as on Avenger's info screen.
    const auto playing = processor.getActiveVoices();
    g.setColour (playing > 0 ? ParagonColours::accent : ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String (playing) + (playing == 1 ? " NOTE" : " NOTES") + " PLAYING",
                getTitleArea(), juce::Justification::centredRight, false);
}

void PlayPanel::resized()
{
    auto area = getContentArea().withTrimmedTop (8);
    const auto h = ParamControl::knobHeight;
    SynthPanel::layoutRow (area.removeFromTop (h), { &mode, &legato, &glide, &glideMode }, 82);
    area.removeFromTop (14);
    SynthPanel::layoutRow (area.removeFromTop (h), { &bendUp, &bendDown, &voices, &level }, 82);
}
