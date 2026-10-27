#include "SynthPanels.h"

#include "ui/DockPanels.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    // The knob cards' width.
    constexpr int cardWidth = 70;

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
OscillatorPanel::Tab::Tab (juce::AudioProcessorValueTreeState& state, int tabIndex)
    : juce::Button ("OSC " + juce::String (tabIndex + 1)),
      index (tabIndex),
      powerAttachment (state, ParamIDs::osc (tabIndex, "On"), power)
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
    const auto selected = getToggleState();
    g.setColour (selected ? juce::Colour (0xff1c1418) : juce::Colour (0xff0d0a0e));
    g.fillRoundedRectangle (area, 4.0f);
    if (selected)
    {
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
        g.fillRect (area.withTop (area.getBottom() - 2.0f));
    }
    g.setColour (selected ? ParagonColours::accent.withAlpha (0.6f) : ParagonColours::panelEdge);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
    g.setColour (selected || highlighted ? ParagonColours::text : ParagonColours::textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (getName(), area.withTrimmedLeft (22.0f), juce::Justification::centred, false);
}

OscillatorPanel::OscillatorPanel (juce::AudioProcessorValueTreeState& stateToUse)
    : SynthPanel ("OSCILLATORS"), state (stateToUse), wave (stateToUse)
{
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        auto tab = std::make_unique<Tab> (state, i);
        tab->onClick = [this, i] { selectOscillator (i); };
        addAndMakeVisible (*tab);
        tabs.push_back (std::move (tab));
    }
    addAndMakeVisible (wave);

    // Each control, labelled as on the panel, with a tooltip saying what it does.
    const std::pair<const char*, std::pair<const char*, const char*>> items[] = {
        { "Wave", { "WAVE", "The oscillator's wave. PULSE's width is set by WIDTH." } },
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
    };
    for (const auto& [what, text] : items)
    {
        controls.push_back (std::make_unique<ParamControl> (state, ParamIDs::osc (0, what), text.first, text.second));
        settingNames.push_back (what);
        addAndMakeVisible (*controls.back());
    }
    selectOscillator (0);
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
}

void OscillatorPanel::paint (juce::Graphics& g)
{
    SynthPanel::paint (g);
    for (const auto& [area, text] : sectionTitles)
        drawSectionTitle (g, area, text);
}

void OscillatorPanel::resized()
{
    // The tabs run along the title strip, right of the title.
    auto tabStrip = getTitleArea().withTrimmedLeft (110).reduced (0, 5).withTrimmedTop (2);
    const auto tabWidth = tabStrip.getWidth() / (int) tabs.size();
    for (auto& tab : tabs)
        tab->setBounds (tabStrip.removeFromLeft (tabWidth).reduced (2, 0));

    auto area = getContentArea().withTrimmedTop (6);
    const auto h = ParamControl::knobHeight;

    // Left: the wave, with its WAVE menu, WIDTH and GAIN under it.
    auto left = area.removeFromLeft (264);
    wave.setBounds (left.removeFromTop (left.getHeight() - h - 10));
    left.removeFromTop (10);
    SynthPanel::layoutRow (left.removeFromTop (h), { &control ("Wave"), &control ("Width"), &control ("Gain") }, 84);

    // Right: three rows of titled sections on one grid of columns.
    area.removeFromLeft (16);
    const auto column = area.getWidth() / 7;
    sectionTitles.clear();
    const auto placeRow = [&] (juce::Rectangle<int> row, std::vector<std::pair<juce::String, std::vector<const char*>>> sections)
    {
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
    const auto rowHeight = (area.getHeight() - 2 * 6) / 3;
    placeRow (area.removeFromTop (rowHeight), { { "PITCH", { "Transpose", "Fine", "Random" } },
                                                { "START", { "Phase", "PhaseRand", "FadeIn", "Invert" } } });
    area.removeFromTop (6);
    placeRow (area.removeFromTop (rowHeight), { { "NOISE", { "Noise", "NoiseColour", "NoiseWidth" } },
                                                { "SUB", { "Sub", "SubWave", "SubOct", "SubInvert" } } });
    area.removeFromTop (6);
    placeRow (area.removeFromTop (rowHeight), { { "LEVEL", { "Volume", "Pan" } },
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
