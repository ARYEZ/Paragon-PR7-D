#include "DrumPage.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

namespace
{
    using synth::drums::Model;
    constexpr double pictureRate = 24000.0;   // plenty for a picture, half the work
    constexpr double pictureMaxSeconds = 4.0;

    juce::String noteName (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4); }

    juce::String drumName (const synth::drums::SlotSettings& s, const ParagonX1AudioProcessor& p, int slot)
    {
        if (s.model != Model::sample)
            return synth::drums::modelNames[(int) s.model].toUpperCase();
        const auto sample = p.getDrumSample (slot);
        return sample != nullptr ? sample->name : juce::String ("SAMPLE (EMPTY)");
    }

    // The waveform mirrored about the middle, filled in the plugin's highlight.
    void drawPeaks (juce::Graphics& g, const std::vector<float>& peaks, juce::Rectangle<float> area, bool bright)
    {
        if (peaks.empty())
            return;
        juce::Path shape;
        const auto centre = area.getCentreY(), half = area.getHeight() * 0.5f;
        const auto x = [&] (size_t i) { return area.getX() + area.getWidth() * (float) i / (float) std::max<size_t> (1, peaks.size() - 1); };
        shape.startNewSubPath (x (0), centre);
        for (size_t i = 0; i < peaks.size(); ++i)
            shape.lineTo (x (i), centre - half * juce::jmin (1.0f, peaks[i]));
        for (size_t i = peaks.size(); i-- > 0;)
            shape.lineTo (x (i), centre + half * juce::jmin (1.0f, peaks[i]));
        shape.closeSubPath();
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
        g.setOpacity (bright ? 1.0f : 0.55f);
        g.fillPath (shape);
        g.setOpacity (1.0f);
    }
}

//==============================================================================
HitPicture HitPicture::make (const synth::drums::SlotSettings& settings, int columns)
{
    HitPicture picture;
    columns = juce::jmax (2, columns);
    const auto hit = synth::drums::Kit::renderHit (settings, 1.0f, pictureRate, pictureMaxSeconds);
    const auto* left = hit.getReadPointer (0);
    const auto* right = hit.getReadPointer (1);
    auto peak = 0.0f;
    for (int i = 0; i < hit.getNumSamples(); ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));
    if (peak <= 0.0f)
        return picture;
    auto last = 0;
    for (int i = hit.getNumSamples(); --i > 0;)
        if (juce::jmax (std::abs (left[i]), std::abs (right[i])) > peak * 0.001f)   // -60 dB under its peak
        {
            last = i;
            break;
        }
    last = juce::jmax (last, (int) (0.05 * pictureRate));
    picture.seconds = last / pictureRate;
    picture.peaks.assign ((size_t) columns, 0.0f);
    for (int i = 0; i <= last; ++i)
    {
        auto& column = picture.peaks[(size_t) juce::jmin (columns - 1, i * columns / (last + 1))];
        column = juce::jmax (column, std::abs (left[i]), std::abs (right[i]));
    }
    return picture;
}

//==============================================================================
DrumRow::DrumRow (ParagonX1AudioProcessor& p, int s)
    : processor (p), slot (s),
      mute (p.getParameters(), ParamIDs::drum (s, "Mute"), "M", "MUTE: this drum silent."),
      solo (p.getParameters(), ParamIDs::drum (s, "Solo"), "S", "SOLO: only the soloed drums play.")
{
    for (auto* c : { &mute, &solo })
    {
        c->setCompact (true);
        addAndMakeVisible (*c);
    }
    setTooltip ("Click to choose this drum (and hear it). Drop an audio file here to load it into this slot.");
}

void DrumRow::setSelected (bool s)
{
    if (selected != s)
    {
        selected = s;
        repaint();
    }
}

void DrumRow::setPicture (HitPicture p)
{
    picture = std::move (p);
    repaint();
}

void DrumRow::setLit (float amount)
{
    if (std::abs (amount - lit) > 0.02f || (amount <= 0.0f) != (lit <= 0.0f))
    {
        lit = amount;
        repaint();
    }
}

void DrumRow::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (selected ? juce::Colour (0xff1b1418) : juce::Colour (0xff0f0c10));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (selected ? ParagonColours::accent : ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, selected ? 1.4f : 1.0f);

    // The hit light.
    const auto led = juce::Rectangle<float> (10.0f, bounds.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (juce::Colour (0xff2a2226));
    g.fillEllipse (led);
    if (lit > 0.0f)
    {
        g.setColour (ParagonColours::glow.withAlpha (lit));
        g.fillEllipse (led);
        g.setColour (ParagonColours::glow.withAlpha (0.35f * lit));
        g.fillEllipse (led.expanded (4.0f));
    }

    const auto kit = processor.getKitSettings();
    const auto& s = kit.slots[(size_t) slot];
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (ParagonColours::textDim);
    g.drawText (noteName (kit.rootKey + slot), juce::Rectangle<int> (28, 0, 36, getHeight()), juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (selected ? ParagonColours::text : ParagonColours::text.withAlpha (0.85f));
    g.drawText (synth::drums::slotDefaults[(size_t) slot].key, juce::Rectangle<int> (66, 0, 44, getHeight()), juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (s.model == Model::sample && s.sample == nullptr ? ParagonColours::textDim : ParagonColours::accent);
    g.drawFittedText (drumName (s, processor, slot), juce::Rectangle<int> (112, 0, 112, getHeight()), juce::Justification::centredLeft, 1, 0.8f);

    // The hit's picture: its first second (longer hits run off the end).
    auto thumb = juce::Rectangle<float> (230.0f, 6.0f, (float) getWidth() - 230.0f - 96.0f, (float) getHeight() - 12.0f);
    g.setColour (juce::Colour (0xff09070a));
    g.fillRoundedRectangle (thumb, 3.0f);
    if (! picture.peaks.empty())
    {
        const auto shown = juce::jmin (1.0, picture.seconds);
        const auto columns = (size_t) juce::jmax (2.0, picture.peaks.size() * shown / picture.seconds);
        const std::vector<float> first (picture.peaks.begin(), picture.peaks.begin() + (std::ptrdiff_t) juce::jmin (columns, picture.peaks.size()));
        drawPeaks (g, first, thumb.reduced (2.0f, 1.0f).withWidth ((thumb.getWidth() - 4.0f) * (float) (shown / 1.0)), ! s.mute);
    }
}

void DrumRow::resized()
{
    auto area = getLocalBounds().reduced (4, 4);
    solo.setBounds (area.removeFromRight (44));
    mute.setBounds (area.removeFromRight (44));
}

void DrumRow::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect (slot);
    processor.previewDrum (slot);
}

bool DrumRow::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && DrumPage::isAudioFile (files[0]);
}

void DrumRow::filesDropped (const juce::StringArray& files, int, int)
{
    if (processor.loadDrumSample (slot, juce::File (files[0])))
    {
        if (onSelect)
            onSelect (slot);
        processor.previewDrum (slot);
    }
}

//==============================================================================
DrumHitDisplay::DrumHitDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("The chosen drum's hit, from a copy of it at these settings (both sides, peak level). Drop an audio file "
                "here to load it into this slot.");
}

void DrumHitDisplay::setSlot (int s)
{
    slot = s;
    repaint();
}

void DrumHitDisplay::setPicture (HitPicture p)
{
    picture = std::move (p);
    repaint();
}

juce::Rectangle<float> DrumHitDisplay::waveArea() const
{
    auto area = getLocalBounds().toFloat().reduced (20.0f, 16.0f);
    area.removeFromTop (18.0f);
    area.removeFromBottom (20.0f);
    return area.withTrimmedTop (16.0f);   // room for the hits' numbers
}

bool DrumHitDisplay::showsFile() const
{
    const auto kit = processor.getKitSettings();
    return kit.slots[(size_t) slot].model == Model::sample && processor.getDrumSample (slot) != nullptr;
}

void DrumHitDisplay::updateFile()
{
    const auto sample = processor.getDrumSample (slot);
    const auto columns = juce::jmax (2, (int) waveArea().getWidth());
    if (sample == file && (int) filePeaks.size() == columns)
        return;
    file = sample;
    filePeaks.assign ((size_t) columns, 0.0f);
    hits.clear();
    if (file == nullptr || file->length() < 2)
        return;
    const auto length = file->length();
    for (const auto& channel : file->channels)
        for (int i = 0; i < length; ++i)
        {
            auto& column = filePeaks[(size_t) juce::jmin (columns - 1, (int) ((juce::int64) i * columns / length))];
            column = juce::jmax (column, std::abs (channel[(size_t) i]));
        }
    auto peak = *std::max_element (filePeaks.begin(), filePeaks.end());
    if (peak > 0.0f)
        for (auto& p : filePeaks)
            p /= peak;
    hits = synth::drums::sampleFiles::findHits (*file);
}

const std::vector<int>& DrumHitDisplay::getHits()
{
    updateFile();
    return hits;
}

float DrumHitDisplay::share (const char* what) const
{
    const auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, what));
    return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : 0.0f;
}

void DrumHitDisplay::setShare (const char* what, float value, bool gesture)
{
    if (auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, what)))
    {
        if (gesture)
            parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (0.0f, 1.0f, value)));
        if (gesture)
            parameter->endChangeGesture();
    }
    repaint();
}

void DrumHitDisplay::pickHit (int index)
{
    updateFile();
    if (file == nullptr || index < 0 || index >= (int) hits.size())
        return;
    // From this hit's start to just before the next one's (1 ms short), or to the end of the file.
    const auto length = (float) file->length();
    const auto next = index + 1 < (int) hits.size() ? (float) hits[(size_t) index + 1] - (float) (0.001 * file->sampleRate) : length;
    setShare ("SampleStart", (float) hits[(size_t) index] / length, true);
    setShare ("SampleEnd", juce::jmax ((float) hits[(size_t) index] + 1.0f, next) / length, true);
    processor.previewDrum (slot);
}

void DrumHitDisplay::playWholeFile()
{
    setShare ("SampleStart", 0.0f, true);
    setShare ("SampleEnd", 1.0f, true);
    processor.previewDrum (slot);
}

DrumHitDisplay::Handle DrumHitDisplay::handleAt (float x) const
{
    const auto area = waveArea();
    const auto startX = area.getX() + area.getWidth() * share ("SampleStart");
    const auto endX = area.getX() + area.getWidth() * share ("SampleEnd");
    if (std::abs (x - endX) <= 7.0f && std::abs (x - endX) <= std::abs (x - startX))
        return Handle::end;
    if (std::abs (x - startX) <= 7.0f)
        return Handle::start;
    return Handle::none;
}

void DrumHitDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (showsFile() && handleAt ((float) e.x) != Handle::none ? juce::MouseCursor::LeftRightResizeCursor
                                                                          : juce::MouseCursor::NormalCursor);
}

void DrumHitDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! showsFile())
        return;
    updateFile();
    dragging = handleAt ((float) e.x);
    if (dragging != Handle::none)
    {
        if (auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, dragging == Handle::start ? "SampleStart" : "SampleEnd")))
            parameter->beginChangeGesture();
        return;
    }
    // A click elsewhere chooses the hit under it (the last one starting at or before it).
    const auto area = waveArea();
    const auto at = (double) (e.x - area.getX()) / area.getWidth() * file->length();
    auto index = 0;
    for (int i = 0; i < (int) hits.size(); ++i)
        if (hits[(size_t) i] <= at)
            index = i;
    pickHit (index);
}

void DrumHitDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;
    const auto area = waveArea();
    const auto value = juce::jlimit (0.0f, 1.0f, ((float) e.x - area.getX()) / area.getWidth());
    const auto least = 0.002f;   // START stays before END
    if (dragging == Handle::start)
        setShare ("SampleStart", juce::jmin (value, share ("SampleEnd") - least), false);
    else
        setShare ("SampleEnd", juce::jmax (value, share ("SampleStart") + least), false);
}

void DrumHitDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Handle::none)
        return;
    if (auto* parameter = processor.getParameters().getParameter (ParamIDs::drum (slot, dragging == Handle::start ? "SampleStart" : "SampleEnd")))
        parameter->endChangeGesture();
    dragging = Handle::none;
    processor.previewDrum (slot);
}

void DrumHitDisplay::paint (juce::Graphics& g)
{
    DisplayStyle::drawScreen (g, getLocalBounds().toFloat());
    auto area = getLocalBounds().toFloat().reduced (20.0f, 16.0f);
    const auto kit = processor.getKitSettings();
    const auto& s = kit.slots[(size_t) slot];

    if (showsFile())
    {
        // The whole file: the part played bright, the rest dim; its hits numbered along the top.
        updateFile();
        const auto wave = waveArea();
        const auto seconds = file->length() / file->sampleRate;
        const auto startShare = juce::jlimit (0.0f, 1.0f, s.start), endShare = juce::jlimit (startShare, 1.0f, s.end);
        const auto startX = wave.getX() + wave.getWidth() * startShare, endX = wave.getX() + wave.getWidth() * endShare;
        auto chosen = -1;
        for (int i = 0; i < (int) hits.size(); ++i)
            if (std::abs ((float) hits[(size_t) i] / (float) file->length() - startShare) < 0.0005f)
                chosen = i;

        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (ParagonColours::accent);
        auto title = file->name + "   " + juce::String ((endShare - startShare) * seconds, 2) + " s of " + juce::String (seconds, 1) + " s";
        if (chosen >= 0)
            title << "   (hit " << (chosen + 1) << " of " << (int) hits.size() << ")";
        g.drawText (title, area.removeFromTop (18.0f), juce::Justification::topLeft, false);

        g.setColour (ParagonColours::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (wave.getCentreY()), wave.getX(), wave.getRight());
        drawPeaks (g, filePeaks, wave, false);
        g.setColour (juce::Colour (0xff060407).withAlpha (0.55f));   // the part not played, dimmed further
        g.fillRect (wave.withRight (startX));
        g.fillRect (wave.withLeft (endX));
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (wave.withLeft (startX).withRight (endX).toNearestInt());
            drawPeaks (g, filePeaks, wave, ! s.mute);
        }

        // The hits' numbers.
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (int i = 0; i < (int) hits.size(); ++i)
        {
            const auto x = wave.getX() + wave.getWidth() * (float) hits[(size_t) i] / (float) file->length();
            g.setColour (i == chosen ? ParagonColours::accent : ParagonColours::textDim);
            g.drawVerticalLine (juce::roundToInt (x), wave.getY() - 14.0f, wave.getY() - 2.0f);
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (x + 2.0f, wave.getY() - 16.0f, 24.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
        // START and END.
        for (const auto& [x, label] : { std::pair { startX, "S" }, std::pair { endX, "E" } })
        {
            g.setColour (ParagonColours::text);
            g.drawLine (x, wave.getY(), x, wave.getBottom(), 1.5f);
            const auto flag = juce::Rectangle<float> (label[0] == 'S' ? x : x - 14.0f, wave.getBottom() - 16.0f, 14.0f, 16.0f);
            g.fillRect (flag);
            g.setColour (juce::Colours::black);
            g.drawText (label, flag, juce::Justification::centred, false);
        }
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        const auto hint = hits.size() > 1 ? "Click a hit to play just that one; drag S and E to fine-tune"
                                          : "Drag S and E to choose the part played";
        g.drawText (hint, getLocalBounds().toFloat().reduced (20.0f, 14.0f).removeFromBottom (14.0f), juce::Justification::centredRight, false);
        return;
    }

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (ParagonColours::accent);
    auto title = drumName (s, processor, slot);
    if (! picture.peaks.empty())
        title << "   " << juce::String (picture.seconds, picture.seconds < 1.0 ? 2 : 1) << " s";
    g.drawText (title, area.removeFromTop (18.0f), juce::Justification::topLeft, false);
    auto axis = area.removeFromBottom (16.0f);
    area.removeFromBottom (4.0f);

    if (picture.peaks.empty())
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (s.model == Model::sample ? "No sample yet: LOAD SAMPLE, or drop an audio file here" : "Silent at these settings",
                    area, juce::Justification::centred, false);
        return;
    }

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    drawPeaks (g, picture.peaks, area, ! s.mute);

    // Time marks: 10, 4 or so along, at a round step.
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (ParagonColours::textDim);
    double step = 0.01;
    for (const auto candidate : { 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1.0 })
        if (picture.seconds / candidate <= 10.0)
        {
            step = candidate;
            break;
        }
    for (double t = step; t < picture.seconds - step * 0.25; t += step)
    {
        const auto x = area.getX() + area.getWidth() * (float) (t / picture.seconds);
        g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.setColour (ParagonColours::textDim);
        const auto label = step < 0.1 ? juce::String (juce::roundToInt (t * 1000.0)) + " ms" : juce::String (t, step < 1.0 ? 2 : 0) + " s";
        g.drawText (label, juce::Rectangle<float> (x - 30.0f, axis.getY(), 60.0f, axis.getHeight()), juce::Justification::centred, false);
    }
}

bool DrumHitDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && DrumPage::isAudioFile (files[0]);
}

void DrumHitDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    if (processor.loadDrumSample (slot, juce::File (files[0])))
        processor.previewDrum (slot);
}

//==============================================================================
DrumPage::DrumPage (ParagonX1AudioProcessor& p) : processor (p), display (p), sequencer (p)
{
    auto& state = p.getParameters();
    const auto kitControl = [&] (const char* id, const juce::String& label, const juce::String& tip)
    {
        kitControls.push_back (std::make_unique<ParamControl> (state, id, label, tip));
    };
    kitControl (ParamIDs::drumsOn, "ON", "The drums on: the 12 keys from ROOT play the kit instead of the synth. Off: every key plays the synth.");
    kitControl (ParamIDs::drumsRoot, "ROOT", "The kit's lowest key (its kick); the 11 keys above it play the rest, C to B as on General MIDI kits.");
    kitControl (ParamIDs::drumsLevel, "LEVEL", "The whole kit's level.");
    kitControl (ParamIDs::drumsVelocity, "VELOCITY", "How much the keys' velocity changes the level: 0 every hit the same, 100% a soft hit "
                                                     "(half velocity) 12 dB down.");

    const auto slotControl = [&] (auto& list, const char* what, const juce::String& label, const juce::String& tip)
    {
        list.push_back (std::make_unique<ParamControl> (state, ParamIDs::drum (0, what), label, tip));
    };
    slotControl (slotControls, "Model", "DRUM", "What this slot is: one of our own drum synths, or SAMPLE (a file you load). Our own "
                                                "designs from the classic analog ideas, not copies of any drum machine.");
    slotControl (slotControls, "Pitch", "PITCH", "Its pitch, in semitones (and hundredths).");
    slotControl (slotControls, "Decay", "DECAY", "How long it rings. SAMPLE: the whole sample from the middle up; below, it's cut shorter.");
    slotControl (slotControls, "Tone", "TONE", "Its brightness. KICK and TOM: how far the pitch drops at the hit. RIM: high ring against low. "
                                               "SAMPLE: darker below the middle, thinner above.");
    slotControl (slotControls, "Snap", "SNAP", "Its attack: the kick's beater click, the snare's wires, the clap's slaps, the hats' sizzle, "
                                               "the cowbell's strike. SAMPLE: a softer attack below the middle, a spike above.");
    slotControl (slotControls, "Level", "VOLUME", "This drum's level.");
    slotControl (slotControls, "Pan", "PAN", "Left to right.");
    slotControl (routingControls, "Choke", "CHOKE", "A hit stops the other drums in its group: the closed and pedal hats stop the open hat (group 1).");
    slotControl (routingControls, "Insert", "INSERT", "NONE: straight into the mix, through the sends. A to D: through that insert bus "
                                                      "(its DRIVE, GLITCH, MOD, LO-FI), whose own sends then feed DELAY and REVERB.");
    slotControl (routingControls, "SendDelay", "TO DELAY", "How much goes to DELAY (while DELAY is on, and the drum isn't on a bus).");
    slotControl (routingControls, "SendReverb", "TO REVERB", "How much goes to REVERB (while REVERB is on, and the drum isn't on a bus).");

    for (auto* list : { &kitControls, &slotControls, &routingControls })
        for (auto& c : *list)
            addAndMakeVisible (*c);

    for (int k = 0; k < synth::drums::numSlots; ++k)
    {
        rows.push_back (std::make_unique<DrumRow> (p, k));
        rows.back()->onSelect = [this] (int slot) { select (slot); };
        addAndMakeVisible (*rows.back());
    }
    addAndMakeVisible (display);

    play.setTooltip ("Hear the chosen drum.");
    play.onClick = [this] { processor.previewDrum (selected); };
    load.setTooltip ("Load an audio file (WAV, AIFF, FLAC...) into the chosen slot: it becomes a SAMPLE, kept with the preset.");
    load.onClick = [this] { chooseSample(); };
    wholeFile.setTooltip ("Play the whole file again (after choosing one hit out of it).");
    wholeFile.onClick = [this] { display.playWholeFile(); };
    addChildComponent (wholeFile);
    clear.setTooltip ("Take the chosen slot's sample out.");
    clear.onClick = [this] { processor.clearDrumSample (selected); };
    for (auto* b : { &play, &load, &clear })
        addAndMakeVisible (*b);

    addChildComponent (sequencer);
    sequencer.getGrid().onRowTouched = [this] (int row) { select (row); };
    for (auto* tab : { &drumTab, &sequencerTab })
    {
        DrumSeqPanel::style (*tab);
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (963);
        addAndMakeVisible (*tab);
    }
    drumTab.setTooltip ("The chosen drum's sound, its sample and where it goes.");
    sequencerTab.setTooltip ("The DRUM SEQUENCER: beats for the kit, played with your song or from keys.");
    drumTab.setToggleState (true, juce::dontSendNotification);
    drumTab.onClick = [this] { showView (View::drum); };
    sequencerTab.onClick = [this] { showView (View::sequencer); };

    select (0);
    refreshPictures();
    startTimerHz (30);
}

void DrumPage::showView (View v)
{
    view = v;
    const auto drum = v == View::drum;
    for (auto* list : { &slotControls, &routingControls })
        for (auto& c : *list)
            c->setVisible (drum);
    for (juce::Component* c : { (juce::Component*) &display, (juce::Component*) &play, (juce::Component*) &load, (juce::Component*) &clear })
        c->setVisible (drum);
    wholeFile.setVisible (drum && display.showsFile());
    sequencer.setVisible (! drum);
    (drum ? drumTab : sequencerTab).setToggleState (true, juce::dontSendNotification);
    drumTab.toFront (false);
    sequencerTab.toFront (false);
    repaint();
}

DrumPage::~DrumPage() = default;

bool DrumPage::isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

std::vector<ParamControl*> DrumPage::getSlotControls()
{
    std::vector<ParamControl*> result;
    for (auto* list : { &slotControls, &routingControls })
        for (auto& c : *list)
            result.push_back (c.get());
    return result;
}

void DrumPage::select (int slot)
{
    selected = juce::jlimit (0, synth::drums::numSlots - 1, slot);
    for (auto* list : { &slotControls, &routingControls })
        for (auto& c : *list)
        {
            const auto what = c->getParameterId().fromFirstOccurrenceOf ("drum", false, false).trimCharactersAtStart ("0123456789");
            c->attach (ParamIDs::drum (selected, what.toRawUTF8()));
        }
    for (auto& row : rows)
        row->setSelected (row->getSlot() == selected);
    sequencer.getGrid().setHighlightedRow (selected);
    display.setSlot (selected);
    pictureKeys[(size_t) selected] = {};   // the large picture for this drum
    refreshPictures();
    repaint();
}

juce::String DrumPage::keyFor (int slot) const
{
    const auto s = processor.getKitSettings().slots[(size_t) slot];
    return juce::String ((int) s.model) + ":" + juce::String (s.pitch) + ":" + juce::String (s.decay) + ":" + juce::String (s.tone) + ":"
         + juce::String (s.snap) + ":" + juce::String (s.gain) + ":" + juce::String ((juce::pointer_sized_int) s.sample)
         + ":" + juce::String ((int) s.mute) + ":" + juce::String (processor.getKitSettings().rootKey) + ":" + juce::String (s.start)
         + ":" + juce::String (s.end);
}

void DrumPage::refreshPictures()
{
    wholeFile.setVisible (view == View::drum && display.showsFile());
    const auto kit = processor.getKitSettings();
    for (int k = 0; k < synth::drums::numSlots; ++k)
    {
        const auto key = keyFor (k);
        if (key == pictureKeys[(size_t) k])
            continue;
        pictureKeys[(size_t) k] = key;
        auto& row = *rows[(size_t) k];
        row.setPicture (HitPicture::make (kit.slots[(size_t) k], juce::jmax (40, row.getWidth() - 330)));
        if (k == selected)
        {
            display.setPicture (HitPicture::make (kit.slots[(size_t) k], juce::jmax (100, display.getWidth() - 40)));
            repaint (drumArea);
        }
    }
}

void DrumPage::timerCallback()
{
    if (! isShowing())
        return;
    updateLights();
    if (++tick % 6 == 0)
        refreshPictures();
}

void DrumPage::updateLights()
{
    const auto& kit = processor.getDrumKit();
    for (auto& row : rows)
    {
        const auto age = kit.getHitAge (row->getSlot());
        row->setLit (age >= 0.0f ? juce::jmax (0.0f, 1.0f - age / 0.25f) : 0.0f);
    }
}

void DrumPage::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into " + juce::String (synth::drums::slotDefaults[(size_t) selected].key),
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    const auto slot = selected;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile() && processor.loadDrumSample (slot, file))
                                  processor.previewDrum (slot);
                          });
}

void DrumPage::paint (juce::Graphics& g)
{
    for (const auto& area : { kitArea, drumArea })
        DockStyle::drawPanel (g, area.toFloat());

    const auto title = [&g] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area.toFloat().withWidth (140.0f)));
        g.drawText (text, area, juce::Justification::centredLeft, false);
    };
    const auto kit = processor.getKitSettings();
    title ("KIT", kitArea.reduced (18, 0).withHeight (40));
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (kit.on ? "The keys " + noteName (kit.rootKey) + " to " + noteName (kit.rootKey + 11) + " play the drums"
                       : "Off: every key plays the synth (the page still plays the drums)",
                kitArea.reduced (18, 0).withHeight (40).withTrimmedLeft (60), juce::Justification::centredLeft, false);

    if (view == View::sequencer)
    {
        title ("SEQUENCER", drumArea.reduced (18, 0).withHeight (40));
        return;
    }
    const auto& slotKit = kit.slots[(size_t) selected];
    title ("DRUM", drumArea.reduced (18, 0).withHeight (40));
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (noteName (kit.rootKey + selected) + "  " + synth::drums::slotDefaults[(size_t) selected].key + "  "
                    + drumName (slotKit, processor, selected),
                drumArea.reduced (18, 0).withHeight (40).withTrimmedLeft (70), juce::Justification::centredLeft, false);
}

void DrumPage::resized()
{
    auto area = getLocalBounds();
    kitArea = area.removeFromLeft (590);
    area.removeFromLeft (10);
    drumArea = area;

    // KIT: the kit's own settings, then the 12 rows.
    auto k = kitArea.reduced (14, 10);
    k.removeFromTop (30);
    std::vector<juce::Component*> globals;
    for (auto& c : kitControls)
        globals.push_back (c.get());
    SynthPanel::layoutRow (k.removeFromTop (ParamControl::knobHeight), globals, 110);
    k.removeFromTop (6);
    const auto rowHeight = k.getHeight() / synth::drums::numSlots;
    const auto rowsTop = k.getY();
    for (auto& row : rows)
        row->setBounds (k.removeFromTop (rowHeight).reduced (0, 2));

    // The view switch at the right of the panel's title line; the SEQUENCER over the panel, its grid's rows level
    // with the kit's.
    auto tabs = drumArea.reduced (14, 10).removeFromTop (30);
    sequencerTab.setBounds (tabs.removeFromRight (104).withSizeKeepingCentre (100, 24));
    drumTab.setBounds (tabs.removeFromRight (74).withSizeKeepingCentre (70, 24));
    sequencer.setBounds (drumArea);
    const auto inner = drumArea.reduced (14, 0);
    sequencer.setGridArea ({ inner.getX() - drumArea.getX(), rowsTop - drumArea.getY(), inner.getWidth(), rowHeight * synth::drums::numSlots });

    // DRUM: the drum, its buttons, the picture, its shaping, its routing.
    auto d = drumArea.reduced (14, 10);
    d.removeFromTop (30);
    auto bar = d.removeFromTop (54);
    slotControls[0]->setBounds (bar.removeFromLeft (200));
    bar.removeFromLeft (16);
    play.setBounds (bar.removeFromLeft (90).withSizeKeepingCentre (90, 30).translated (0, 8));
    bar.removeFromLeft (10);
    wholeFile.setBounds (bar.removeFromLeft (120).withSizeKeepingCentre (120, 30).translated (0, 8));
    clear.setBounds (bar.removeFromRight (90).withSizeKeepingCentre (90, 30).translated (0, 8));
    bar.removeFromRight (10);
    load.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 30).translated (0, 8));
    d.removeFromTop (8);
    auto routing = d.removeFromBottom (ParamControl::knobHeight);
    d.removeFromBottom (14);
    auto knobs = d.removeFromBottom (ParamControl::knobHeight);
    d.removeFromBottom (10);
    display.setBounds (d);
    std::vector<juce::Component*> shaping, routes;
    for (size_t i = 1; i < slotControls.size(); ++i)
        shaping.push_back (slotControls[i].get());
    for (auto& c : routingControls)
        routes.push_back (c.get());
    SynthPanel::layoutRow (knobs, shaping, 110);
    SynthPanel::layoutRow (routing, routes, 140);

    std::fill (pictureKeys.begin(), pictureKeys.end(), juce::String());   // sized afresh
    refreshPictures();
}
