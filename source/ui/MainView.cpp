#include "MainView.h"

#include "Parameters.h"
#include "ui/Logo.h"
#include "dsp/effects/CompGate.h"
#include "dsp/effects/Modulation.h"

namespace
{
    // The mockup image is 1672 px wide; our canvas is 1600. Every position
    // below is measured off the mockup and scaled by this.
    constexpr float mockScale = (float) MainView::width / 1672.0f;

    juce::Rectangle<float> mockF (float left, float top, float right, float bottom)
    {
        return juce::Rectangle<float>::leftTopRightBottom (left, top, right, bottom) * mockScale;
    }

    juce::Rectangle<int> mock (float left, float top, float right, float bottom)
    {
        return mockF (left, top, right, bottom).toNearestInt();
    }

    juce::Point<float> mockPoint (float x, float y) { return juce::Point<float> (x, y) * mockScale; }

    // Header groups.
    const auto tabsGroup = mockF (336, 10, 795, 55);
    const auto rigGroup = mockF (838, 10, 1135, 55);
    const auto tempoGroup = mockF (1250, 13, 1413, 52);
    constexpr float tempoDividerX = 1338;

    // The pedal unit (component bounds include room for its glow).
    const auto pedalBounds = mock (126, 68, 1552, 564);

    // The 2 x 3 button cluster: column centres and row tops.
    constexpr float clusterX[] = { 918, 975, 1032 };
    constexpr float clusterLabelY[] = { 119, 160 };
    constexpr float clusterButtonY[] = { 136, 177 };

    // Footswitch pads' left edges (each is 154 wide) and each row's top, as
    // on PR7-V: the bottom row (stomps 4-7) is where PR7-B's four were; the
    // top row (1-3) sits in the gaps between them, three over four, as on
    // floor units, so a foot on one top switch can't catch the two below it.
    constexpr float footswitchX[] = { 372.5f, 662, 948.5f,      // 1-3
                                      228, 517, 807, 1090 };    // 4-7
    constexpr float footswitchY[] = { 254, 254, 254, 400, 400, 400, 400 };

    // A stomp whose name opens no block (not built yet, or no knobs).
    constexpr int noBlock = -1;

    void styleInkLabel (juce::Label& label, float size)
    {
        label.setFont (juce::FontOptions (size, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::ink);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
InkArrowButton::InkArrowButton (const juce::String& name, float direction) : juce::Button (name)
{
    path.addTriangle (0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
    path.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * direction, 0.5f, 0.5f));
}

void InkArrowButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto p = path;
    const auto offset = down ? 1.0f : 0.0f;
    p.applyTransform (path.getTransformToScaleToFit (offset, offset, (float) getWidth() - 3.0f, (float) getHeight() - 3.0f, false));
    juce::DropShadow (juce::Colours::black.withAlpha (0.3f), down ? 2 : 4, {}).drawForPath (g, p);
    g.setColour (Finish::ink());
    g.fillPath (p);
}

//==============================================================================
GainColumn::GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                        const juce::String& parameterId, LevelMeterSource& meterSource)
    : attachment (parameters, parameterId, knob),
      meter (meterSource)
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::FontOptions (14.5f, juce::Font::bold));

    // Set on the knob itself: the editor's look-and-feel is attached after the
    // knob's value box has already been created, so it wouldn't pick these up.
    knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    knob.setDoubleClickReturnValue (true, 0.0);   // double-click resets to 0 dB

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (knob);
    addAndMakeVisible (meter);
}

void GainColumn::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
}

void GainColumn::resized()
{
    // Proportions from the mockup: title, a compact knob, its value in
    // orange, then a tall slim meter.
    auto area = getLocalBounds();
    titleLabel.setBounds (area.withHeight (26).withY (14));
    knob.setBounds (juce::Rectangle<int> (76, 92).withCentre ({ area.getCentreX(), 86 }));
    meter.setBounds (juce::Rectangle<int> (46, 0).withX (area.getCentreX() - 26).withY (146).withBottom (area.getBottom() - 22));
}

//==============================================================================
MainView::MainView (ParagonX1AudioProcessor& processorToShow)
    : audioProcessor (processorToShow),
      inputColumn ("INPUT", processorToShow.getParameters(), ParamIDs::inputGain, processorToShow.getInputMeter()),
      outputColumn ("OUTPUT", processorToShow.getParameters(), ParamIDs::outputGain, processorToShow.getOutputMeter()),
      bypassAttachment (processorToShow.getParameters(), ParamIDs::bypass, bypassButton),
      tunerAttachment (processorToShow.getParameters(), ParamIDs::tunerOn, tunerPedal),
      rigLevelAttachment (processorToShow.getParameters(), ParamIDs::rigLevel, rigLevelKnob),
      presenceAttachment (processorToShow.getParameters(), ParamIDs::presence, presenceKnob),
      expressionPedal (processorToShow),
      tunerReader (processorToShow),
      selectedRig (processorToShow),
      expressionPanel (processorToShow.getParameters()),
      eqPanel (processorToShow.getParameters()),
      tunerPanel (processorToShow),
      signalChain (processorToShow.getParameters(), {
          { "PITCH", Icons::Type::pitch, ParagonColours::pitch, juce::String (ParamIDs::octOn) + "," + ParamIDs::freezeOn },
          { "COMP / GATE", Icons::Type::comp, ParagonColours::comp, ParamIDs::compOn },
          { "FILTER / PITCH", Icons::Type::filterPitch, ParagonColours::filterPitch, ParamIDs::fpOn },
          { "EQ", Icons::Type::eq, ParagonColours::eq, juce::String (ParamIDs::eqOn) + "," + ParamIDs::eqUltraLo + "," + ParamIDs::eqUltraHi },
          { "GLITCH", Icons::Type::glitch, ParagonColours::glitch, ParamIDs::glitchOn },
          { "MOD", Icons::Type::modulation, ParagonColours::modulation, ParamIDs::modOn },   // MODULATION: short, as on PR7-B
          { "DELAY", Icons::Type::delay, ParagonColours::delay, ParamIDs::delayOn },
          { "REVERB", Icons::Type::reverb, ParagonColours::reverb, ParamIDs::reverbOn },
          { "LO-FI", Icons::Type::loFi, ParagonColours::loFi, ParamIDs::lofiOn } }),
      rigBrowser (processorToShow),
      settingsPage (processorToShow)
{
    auto& state = processorToShow.getParameters();

    //==========================================================================
    // Header
    for (auto* tab : { &processorTab, &presetsTab, &settingsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (100);
        addAndMakeVisible (*tab);
    }
    processorTab.setToggleState (true, juce::dontSendNotification);
    processorTab.setComponentID ("processorTab");
    presetsTab.setComponentID ("presetsTab");
    settingsTab.setComponentID ("settingsTab");
    processorTab.onClick = [this] { showPage (Page::processor); };
    presetsTab.onClick = [this] { showPage (Page::presets); };
    settingsTab.onClick = [this] { showPage (Page::settings); };

    previousRig.onClick = [this] { audioProcessor.stepRig (-1); };
    nextRig.onClick = [this] { audioProcessor.stepRig (1); };
    rigNameButton.getProperties().set (ParagonLookAndFeel::plainTextProperty, 19.0f);
    rigNameButton.setColour (juce::TextButton::textColourOffId, ParagonColours::accent);
    rigNameButton.setTooltip ("The current rig. * means it has changed since it was loaded or saved. Click to browse rigs.");
    rigNameButton.onClick = [this] { showPage (Page::presets); };

    // Tempo: shows the DAW's tempo, or the tapped one (in orange) once TAP
    // has been used. Clicking a tapped tempo goes back to following the DAW.
    tempoButton.getProperties().set (ParagonLookAndFeel::plainTextProperty, 14.0f);
    tempoButton.onClick = [this] { audioProcessor.clearTappedTempo(); refreshPerformance(); };
    tapButton.onClick = [this] { if (! headerTapHold.consumeHold()) tap(); };
    tapButton.addMouseListener (&headerTapHold, false);
    headerTapHold.onHold = [this] { toggleTuner(); };
    undoButton.onClick = [this] { audioProcessor.undo(); };
    redoButton.onClick = [this] { audioProcessor.redo(); };
    saveButton.onClick = [this] { saveRig(); };
    settingsButton.onClick = [this] { showPage (page == Page::settings ? Page::processor : Page::settings); };

    for (auto* component : std::initializer_list<juce::Component*> { &previousRig, &nextRig, &rigNameButton, &tempoButton, &tapButton,
                                                                      &undoButton, &redoButton, &saveButton, &settingsButton,
                                                                      &inputColumn, &outputColumn })
        addAndMakeVisible (component);

    //==========================================================================
    // The pedal unit
    display.onClick = [this] { showPage (Page::presets); };
    rigUp.onClick = [this] { audioProcessor.stepRig (-1); };
    rigDown.onClick = [this] { audioProcessor.stepRig (1); };

    for (auto* button : { &pedalSave, &pedalSaveAs, &bypassButton, &undoPedal, &redoPedal, &tunerPedal, &resetPedal, &tapPedal })
        button->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
    pedalSave.onClick = [this] { saveRig(); };
    pedalSaveAs.onClick = [this] { rigBrowser.promptSaveAs(); };
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass the whole plugin (the same as your DAW's bypass button)");

    undoPedal.setTooltip ("Undo the last change (Ctrl+Z)");
    redoPedal.setTooltip ("Redo (Ctrl+Shift+Z)");
    undoPedal.onClick = [this] { audioProcessor.undo(); };
    redoPedal.onClick = [this] { audioProcessor.redo(); };
    tunerPedal.setClickingTogglesState (true);
    tunerPedal.setTooltip ("Tuner: shows the pitch of what's playing on the screen, and its trace in the TUNER panel");
    resetPedal.setTooltip ("Empty the tuner's trace and start its average again");
    resetPedal.onClick = [this] { audioProcessor.resetTuner(); };
    tapPedal.setTooltip ("Tap the tempo for the delay (its light flashes in time). Hold to open the tuner.");
    tapPedal.onClick = [this] { if (! pedalTapHold.consumeHold()) tap(); };
    tapPedal.addMouseListener (&pedalTapHold, false);
    pedalTapHold.onHold = [this] { toggleTuner(); };

    // The cluster's labels are printed in red, as in the mockup.
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel, &tunerLabel, &resetLabel, &tapLabel })
    {
        styleInkLabel (*label, 9.5f);
        label->setColour (juce::Label::textColourId, ParagonColours::glow.darker (0.15f));
    }
    styleInkLabel (holdLabel, 8.0f);
    holdLabel.setColour (juce::Label::textColourId, ParagonColours::glow.darker (0.15f));

    for (auto* knob : { &rigLevelKnob, &presenceKnob })
    {
        knob->getProperties().set (ParagonLookAndFeel::pedalKnobProperty, true);
        knob->setDoubleClickReturnValue (true, 0.0);
        knob->setPopupDisplayEnabled (true, true, this);   // shows the value while dragging
    }
    rigLevelKnob.setTooltip ("This rig's volume, saved with the rig: use it to balance rigs. "
                             "The OUTPUT knob on the right is the overall volume.");
    presenceKnob.setTooltip ("Cut or boost the top end (above about 3.5 kHz), like an amp's presence control.");
    styleInkLabel (rigLevelLabel, 12.5f);
    styleInkLabel (presenceLabel, 12.5f);

    tunerReader.onReading = [this] (const TunerReader::Reading& reading)
    {
        tunerState.hasNote = reading.hasNote;
        tunerState.note = reading.note.name;
        tunerState.octave = reading.note.octave;
        tunerState.cents = reading.note.cents;
        tunerState.frequency = reading.frequency;
        refreshRig();
    };

    // The seven stomps, from the plan in CLAUDE.md. Each press turns its
    // block on or off; clicking its name shows the block's knobs. Most aren't
    // built yet: those have no parameters (so nothing is saved under an ID we
    // might regret), a dark LED and a pad that does nothing.
    const auto addStomp = [this, &state] (int number, const char* name, juce::StringArray onIds, int block,
                                          std::function<juce::String()> describe)
    {
        auto& footswitch = footswitches[(size_t) number - 1];
        footswitch = std::make_unique<Footswitch> (number, name, state, std::move (onIds));
        footswitch->describe = std::move (describe);
        if (block != noBlock)
            footswitch->onSelect = [this, block] { selectBlock (block); };
        footswitch->refresh();
    };
    const auto planned = [] (const char* what) { return [what] { return juce::String (what); }; };

    // Top row. TUNER checks the pitch of recorded parts (the TUNER key and
    // holding TAP switch it too); its name opens its panel: the trace and the average.
    addStomp (1, "TUNER", { ParamIDs::tunerOn }, tunerBlock, [this]
    {
        const auto reference = audioProcessor.getTunerReferenceHz();
        auto text = "A4 = " + juce::String (reference, 1) + " Hz";
        if (const auto average = audioProcessor.getTuner().getAverage (reference))
            text << "  Average " << (average->cents >= 0.5 ? "+" : "") << juce::roundToInt (average->cents) << " cents";
        return text;
    });
    addStomp (2, "OPEN", {}, noBlock, planned ("To plan"));
    addStomp (3, "INSERTS", {}, noBlock, planned ("Plugin slots: to plan"));

    // Bottom row. EQ switches the graphic EQ for now, until the planned EQ
    // (inspired by FabFilter Pro-Q 4) replaces it.
    addStomp (4, "EQ", { ParamIDs::eqOn }, eqBlock, [this]
    {
        return "Graphic EQ  Level " + valueText (ParamIDs::eqLevel);
    });
    addStomp (5, "RESONANCE", {}, noBlock, planned ("Resonance suppression: to plan"));
    addStomp (6, "AUTO EQ", {}, noBlock, planned ("Adaptive EQ: to plan"));
    addStomp (7, "LIMITER", {}, noBlock, planned ("Output trim and limiter, from PR7-C: to port"));

    addAndMakeVisible (pedal);   // first, so everything on the pedal sits on top of it
    processorPage = { &pedal, &display, &rigUp, &rigDown, &pedalSave, &pedalSaveAs, &bypassButton,
                      &undoPedal, &redoPedal, &tunerPedal, &resetPedal, &tapPedal,
                      &undoLabel, &redoLabel, &bypassLabel, &tunerLabel, &resetLabel, &tapLabel, &holdLabel,
                      &rigLevelKnob, &presenceKnob, &rigLevelLabel, &presenceLabel,
                      &expressionPedal,
                      &rigNavigator, &selectedRig, &expressionPanel };
    for (auto& footswitch : footswitches)
        processorPage.push_back (footswitch.get());
    for (auto* component : processorPage)
        addAndMakeVisible (component);

    //==========================================================================
    // Edit panels, one per block, shown in place of the rig summary. Each
    // starts with the block's on switch.
    using Group = EffectPanel::Group;
    // The compressor's TYPE shows only its own knobs: Classic's THRESHOLD and
    // RATIO, or Daybreak's COMP, DETAIL, MIX and INTENSITY.
    panels[pitchBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "TUNING", { { ParamIDs::pitchTune, "TUNING" } } },
        { "OCTAVER", { { ParamIDs::octOn, "ON" }, { ParamIDs::octSubTwo, "2 DOWN" }, { ParamIDs::octSubOne, "1 DOWN" },
                       { ParamIDs::octDry, "DRY" }, { ParamIDs::octUp, "1 UP" } } },
        { "FREEZE", { { ParamIDs::freezeOn, "FREEZE" }, { ParamIDs::freezeLevel, "LEVEL" }, { ParamIDs::freezeAttack, "ATTACK" },
                      { ParamIDs::freezeRelease, "RELEASE" } } } });
    const auto compIs = [this] (effects::CompGate::Type type)
    {
        return [this, type] { return juce::roundToInt (rawValue (ParamIDs::compType)) == (int) type; };
    };
    const auto classic = compIs (effects::CompGate::Type::classic), daybreak = compIs (effects::CompGate::Type::daybreak);
    panels[compBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "COMPRESSOR", { { ParamIDs::compOn, "ON" }, { ParamIDs::compType, "TYPE" },
                          { ParamIDs::compThreshold, "THRESHOLD", classic, true }, { ParamIDs::compRatio, "RATIO", classic, true },
                          { ParamIDs::compAmount, "COMP", daybreak, true }, { ParamIDs::compDetail, "DETAIL", daybreak, true },
                          { ParamIDs::compMix, "MIX", daybreak, true }, { ParamIDs::compIntensity, "INTENSITY", daybreak, true },
                          { ParamIDs::compLevel, "LEVEL" } } },
        { "NOISE GATE", { { ParamIDs::gateThreshold, "THRESHOLD" } } } });
    panels[compBlock]->setMeter ([this] { return audioProcessor.getCompGainReductionDb(); });
    const auto fpIs = [this] (effects::FilterPitch::Type type)
    {
        return [this, type] { return juce::roundToInt (rawValue (ParamIDs::fpType)) == (int) type; };
    };
    panels[filterPitchBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "FILTER / PITCH", { { ParamIDs::fpOn, "ON" }, { ParamIDs::fpType, "TYPE" },
                              { ParamIDs::fpSensitivity, "SENSITIVITY", fpIs (effects::FilterPitch::Type::autoWah) },
                              { ParamIDs::fpResonance, "RESONANCE", [autoWah = fpIs (effects::FilterPitch::Type::autoWah),
                                                                      talkBox = fpIs (effects::FilterPitch::Type::talkBox)] { return autoWah() || talkBox(); } },
                              { ParamIDs::fpInterval, "INTERVAL", fpIs (effects::FilterPitch::Type::pitchShifter) },
                              { ParamIDs::fpXpProgram, "PROGRAM", fpIs (effects::FilterPitch::Type::whammyWah) },
                              { ParamIDs::fpMix, "MIX" } } } });
    // AEON (the flanger) adds its pedal's MANUAL and RES.
    const auto isAeon = [this] { return juce::roundToInt (rawValue (ParamIDs::modType)) == (int) effects::Modulation::Type::flanger; };
    panels[modBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "MODULATION", { { ParamIDs::modOn, "ON" }, { ParamIDs::modType, "TYPE" }, { ParamIDs::modSync, "SYNC" },
                          { ParamIDs::modManual, "MANUAL", isAeon, true },
                          { ParamIDs::modRate, "RATE", [this] { return rawValue (ParamIDs::modSync) < 0.5f; } },
                          { ParamIDs::modDepth, "DEPTH" },
                          { ParamIDs::modRes, "RES", isAeon, true },
                          { ParamIDs::modMix, "MIX", [this] {
                              return effects::Modulation::usesMix ((effects::Modulation::Type) juce::roundToInt (rawValue (ParamIDs::modType))); } } } } });
    panels[delayBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "DELAY", { { ParamIDs::delayOn, "ON" }, { ParamIDs::delayType, "TYPE" }, { ParamIDs::delaySync, "SYNC" },
                     { ParamIDs::delayTime, "TIME", [this] { return rawValue (ParamIDs::delaySync) < 0.5f; } },
                     { ParamIDs::delayFeedback, "FEEDBACK" }, { ParamIDs::delayTone, "TONE" }, { ParamIDs::delayMix, "MIX" }, { ParamIDs::delayDuck, "DUCK" },
                     { ParamIDs::delayPingPong, "PING-PONG" } } } });
    panels[reverbBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "REVERB", { { ParamIDs::reverbOn, "ON" }, { ParamIDs::reverbType, "TYPE" }, { ParamIDs::reverbSize, "SIZE" },
                      { ParamIDs::reverbPreDelay, "PRE-DELAY" }, { ParamIDs::reverbTone, "TONE" }, { ParamIDs::reverbMix, "MIX" },
                      { ParamIDs::reverbDuck, "DUCK" } } } });
    // GLITCH's LENGTH is a time, or a note value while synced: the one that
    // applies shows.
    const auto glitchSynced = [this] { return rawValue (ParamIDs::glitchSync) > 0.5f; };
    const auto glitchFree = [this] { return rawValue (ParamIDs::glitchSync) <= 0.5f; };
    panels[glitchBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "GLITCH", { { ParamIDs::glitchOn, "ON" }, { ParamIDs::glitchMode, "MODE" }, { ParamIDs::glitchPosition, "POSITION" },
                      { ParamIDs::glitchSync, "SYNC" },
                      { ParamIDs::glitchTime, "LENGTH", glitchFree, true }, { ParamIDs::glitchDivision, "LENGTH", glitchSynced, true },
                      { ParamIDs::glitchDensity, "DENSITY" }, { ParamIDs::glitchPitch, "PITCH" }, { ParamIDs::glitchChaos, "CHAOS" },
                      { ParamIDs::glitchMix, "MIX" } } } });
    panels[loFiBlock] = std::make_unique<EffectPanel> (state, std::vector<Group> {
        { "LO-FI", { { ParamIDs::lofiOn, "ON" }, { ParamIDs::lofiType, "TYPE" }, { ParamIDs::lofiAmount, "AMOUNT" },
                     { ParamIDs::lofiNoise, "NOISE" }, { ParamIDs::lofiMix, "MIX" }, { ParamIDs::lofiLevel, "LEVEL" } } } });
    const std::pair<Icons::Type, juce::Colour> headers[] = {
        { Icons::Type::pitch, ParagonColours::pitch }, { Icons::Type::comp, ParagonColours::comp }, { Icons::Type::filterPitch, ParagonColours::filterPitch },
        { Icons::Type::eq, ParagonColours::eq }, { Icons::Type::glitch, ParagonColours::glitch },
        { Icons::Type::modulation, ParagonColours::modulation }, { Icons::Type::delay, ParagonColours::delay },
        { Icons::Type::reverb, ParagonColours::reverb }, { Icons::Type::loFi, ParagonColours::loFi } };
    const char* titles[] = { "PITCH", "COMP / GATE", "FILTER / PITCH", "GRAPHIC EQ", "GLITCH", "MODULATION", "DELAY", "REVERB", "LO-FI" };
    for (size_t i = 0; i < panels.size(); ++i)
    {
        if (panels[i] == nullptr)
            continue;
        panels[i]->setHeader (headers[i].first, headers[i].second, titles[i]);
        addChildComponent (*panels[i]);
    }
    addChildComponent (eqPanel);
    addChildComponent (tunerPanel);

    closePanel.setTooltip ("Back to the graphic EQ");
    closePanel.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    closePanel.onClick = [this] { selectBlock (eqBlock); };
    addChildComponent (closePanel);

    rigNavigator.onShow = [this] (int filter, int bank)
    {
        rigBrowser.showFilter (filter, bank);
        showPage (Page::presets);
    };
    selectedRig.onSave = [this] { saveRig(); };
    selectedRig.onSaveAs = [this] { rigBrowser.promptSaveAs(); };

    signalChain.onSelect = [this] (int block) { selectBlock (block); };
    addAndMakeVisible (signalChain);
    addChildComponent (rigBrowser);
    addChildComponent (settingsPage);

    selectBlock (eqBlock);   // the dock shows the graphic EQ until another block is picked

    audioProcessor.rigBroadcaster.addChangeListener (this);
    changeListenerCallback (nullptr);
    Finish::changes().addChangeListener (this);
    applyFinish();

    // The modified marker, tempo and undo state can change from the audio
    // thread or the host, so poll them. 30 Hz for the TAP light's flash.
    startTimerHz (30);
    timerCallback();

    setWantsKeyboardFocus (true);
}

MainView::~MainView()
{
    Finish::changes().removeChangeListener (this);
    audioProcessor.rigBroadcaster.removeChangeListener (this);
}

juce::String MainView::valueText (const char* parameterId) const
{
    return audioProcessor.getParameters().getParameter (parameterId)->getCurrentValueAsText();
}

float MainView::rawValue (const char* parameterId) const
{
    return audioProcessor.getParameters().getRawParameterValue (parameterId)->load();
}

void MainView::selectBlock (int block)
{
    // The selected block's knobs fill the middle of the dock; the graphic EQ is the default.
    selectedBlock = block;
    const auto onProcessorPage = page == Page::processor;
    for (size_t i = 0; i < panels.size(); ++i)
        if (panels[i] != nullptr)
            panels[i]->setVisible ((int) i == block && onProcessorPage);
    eqPanel.setVisible (block == eqBlock && onProcessorPage);
    tunerPanel.setVisible (block == tunerBlock && onProcessorPage);
    closePanel.setVisible (block != eqBlock && onProcessorPage);
    if (block != eqBlock)
        closePanel.toFront (false);

    const int footswitchBlocks[] = { tunerBlock, noBlock, noBlock, eqBlock, noBlock, noBlock, noBlock };
    static_assert (std::size (footswitchBlocks) == std::tuple_size_v<decltype (footswitches)>);
    for (size_t i = 0; i < footswitches.size(); ++i)
        footswitches[i]->setSelected (footswitchBlocks[i] == block);

    signalChain.setSelected (block);
}

void MainView::showPage (Page newPage)
{
    page = newPage;
    auto& tab = newPage == Page::processor ? processorTab : newPage == Page::presets ? presetsTab : settingsTab;
    tab.setToggleState (true, juce::dontSendNotification);

    for (auto* component : processorPage)
        component->setVisible (newPage == Page::processor);
    rigBrowser.setVisible (newPage == Page::presets);
    settingsPage.setVisible (newPage == Page::settings);
    selectBlock (selectedBlock);
}

void MainView::refreshRig()
{
    const auto rig = audioProcessor.getCurrentRig();
    const auto* entry = audioProcessor.findCurrentRigEntry();
    const auto modified = audioProcessor.isRigModified();
    const auto bypassed = audioProcessor.getParameters().getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;

    const auto headerText = rig.name + (modified ? " *" : "");
    if (rigNameButton.getButtonText() != headerText)
        rigNameButton.setButtonText (headerText);

    display.setContents ({ entry != nullptr ? entry->rig.number : 0, rig.name,
                           entry != nullptr ? Rig::bankName (entry->rig.bank) : juce::String(), modified, bypassed, tunerState });
    selectedRig.refresh();
}

void MainView::timerCallback()
{
    // The rig details change rarely: 5 times a second is plenty for them.
    if (ticks++ % 6 == 0)
        refreshRig();

    refreshPerformance();
}

void MainView::refreshPerformance()
{
    // Tempo: the tapped tempo in orange, or the DAW's.
    const auto tapped = audioProcessor.getTappedBpm() > 0.0;
    const auto tempo = juce::String (audioProcessor.getTempoBpm(), 1) + " BPM";
    if (tempoButton.getButtonText() != tempo)
        tempoButton.setButtonText (tempo);
    tempoButton.setColour (juce::TextButton::textColourOffId, tapped ? ParagonColours::accent : ParagonColours::text);
    tempoButton.setTooltip (tapped ? "Tapped tempo, used by the delay when it's synced. Click to follow your DAW's tempo again."
                                   : "Your DAW's tempo, used by the delay when it's synced. Use TAP to set your own.");

    // TAP's light flashes on each beat, like a hardware unit's.
    const auto beatMs = 60000.0 / audioProcessor.getTempoBpm();
    tapPedal.setToggleState (std::fmod (juce::Time::getMillisecondCounterHiRes(), beatMs) < beatMs * 0.25,
                             juce::dontSendNotification);

    const auto canUndo = audioProcessor.canUndo(), canRedo = audioProcessor.canRedo();
    undoButton.setEnabled (canUndo);
    redoButton.setEnabled (canRedo);
    undoPedal.setToggleState (canUndo, juce::dontSendNotification);   // lit while there's something to undo
    redoPedal.setToggleState (canRedo, juce::dontSendNotification);

    // The tuner runs only while it's switched on.
    auto& parameters = audioProcessor.getParameters();
    const auto tunerOn = parameters.getRawParameterValue (ParamIDs::tunerOn)->load() > 0.5f;
    tunerReader.setActive (tunerOn);
    if (tunerState.active != tunerOn)
    {
        tunerState.active = tunerOn;
        if (! tunerOn)
            tunerState.hasNote = false;
        refreshRig();
    }
}

void MainView::tap()
{
    audioProcessor.tapTempo();
    refreshPerformance();
}

void MainView::toggleTuner()
{
    if (auto* tuner = audioProcessor.getParameters().getParameter (ParamIDs::tunerOn))
    {
        tuner->beginChangeGesture();
        tuner->setValueNotifyingHost (tuner->getValue() > 0.5f ? 0.0f : 1.0f);
        tuner->endChangeGesture();
    }
    refreshPerformance();
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y (Cmd on a Mac). DAWs often keep the
    // keyboard for themselves, so these work when the plugin window has focus.
    const auto modifiers = key.getModifiers();
    if (! modifiers.isCommandDown())
        return false;

    const auto keyCode = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (keyCode == 'Z')
    {
        if (modifiers.isShiftDown())
            audioProcessor.redo();
        else
            audioProcessor.undo();
        return true;
    }
    if (keyCode == 'Y')
    {
        audioProcessor.redo();
        return true;
    }
    return false;
}

void MainView::saveRig()
{
    // Factory rigs and never-saved sounds need a name first.
    if (! audioProcessor.saveCurrentRig())
        rigBrowser.promptSaveAs();
    refreshRig();
}

void MainView::applyFinish()
{
    for (auto* label : { &rigLevelLabel, &presenceLabel })
        label->setColour (juce::Label::textColourId, Finish::ink());
    // The cluster's names: red over black keys; over neon keys red was
    // hard to read, so they're printed in the unit's ink (white on pearl
    // black, dark on pearl white). HOLD TO TUNER stays red.
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel, &tunerLabel, &resetLabel, &tapLabel })
        label->setColour (juce::Label::textColourId, Finish::neonKeys() ? Finish::ink() : Finish::printedRed());
    holdLabel.setColour (juce::Label::textColourId, Finish::printedRed());
    pedal.refinish();
    expressionPedal.refinish();
    repaint();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &Finish::changes())
    {
        applyFinish();
        return;
    }

    refreshRig();
}

void MainView::paint (juce::Graphics& g)
{
    // Background: near black, with a faint red warmth behind the unit.
    g.fillAll (ParagonColours::background);
    const auto centre = pedalBounds.getCentre().toFloat();
    g.setGradientFill (juce::ColourGradient (ParagonColours::glow.withAlpha (0.07f), centre,
                                             ParagonColours::glow.withAlpha (0.0f), centre.translated (760.0f, 0.0f), true));
    g.fillRect (getLocalBounds());

    // Header: emblem and name, as in the mockup's top bar.
    auto emblem = mockF (33, 13, 73, 50);
    juce::Path triangle;
    triangle.addTriangle (emblem.getTopLeft(), emblem.getTopRight(), { emblem.getCentreX(), emblem.getBottom() });
    g.setColour (ParagonColours::glow.withAlpha (0.3f));
    g.strokePath (triangle, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ParagonColours::glow);
    g.strokePath (triangle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    triangle.applyTransform (juce::AffineTransform::scale (0.5f, 0.5f, emblem.getCentreX(), emblem.getCentreY() - 3.0f));
    g.strokePath (triangle, juce::PathStrokeType (1.5f));

    const auto nameArea = mockF (84, 10, 330, 55);
    const auto nameFont = juce::FontOptions (21.0f);
    g.setFont (nameFont);
    g.setColour (ParagonColours::text);
    g.drawText ("PARAGON", nameArea, juce::Justification::centredLeft, false);
    const auto nameWidth = (float) juce::GlyphArrangement::getStringWidthInt (nameFont, "PARAGON ");
    Logo::drawMark (g, nameArea.withTrimmedLeft (nameWidth).withSizeKeepingCentre (nameArea.getWidth() - nameWidth, 17.0f),
                   juce::Justification::centredLeft, { ParagonColours::glow, ParagonColours::glow, {}, {}, 1.0f, true });

    // Framed groups: the tabs, the rig selector and the tempo.
    for (const auto& group : { tabsGroup, rigGroup, tempoGroup })
    {
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (group, 5.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (group.reduced (0.5f), 5.0f, 1.0f);
    }
    g.drawVerticalLine (juce::roundToInt (tempoDividerX * mockScale), tempoGroup.getY() + 6.0f, tempoGroup.getBottom() - 6.0f);
    for (auto x : { 505.0f, 650.0f })
        g.drawVerticalLine (juce::roundToInt (x * mockScale), tabsGroup.getY() + 8.0f, tabsGroup.getBottom() - 8.0f);

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (64.0f * mockScale), 0.0f, (float) getWidth());
}

void MainView::resized()
{
    // Header
    processorTab.setBounds (mock (338, 12, 505, 53));
    presetsTab.setBounds (mock (507, 12, 650, 53));
    settingsTab.setBounds (mock (652, 12, 793, 53));
    previousRig.setBounds (mock (846, 14, 886, 51));
    nextRig.setBounds (mock (1087, 14, 1127, 51));
    rigNameButton.setBounds (mock (888, 14, 1085, 51));
    tempoButton.setBounds (mock (1252, 15, 1336, 50));
    tapButton.setBounds (mock (1340, 15, 1411, 50));
    undoButton.setBounds (mock (1444, 14, 1482, 50));
    redoButton.setBounds (mock (1496, 14, 1534, 50));
    saveButton.setBounds (mock (1551, 14, 1589, 50));
    settingsButton.setBounds (mock (1611, 14, 1649, 50));

    // Side strips
    inputColumn.setBounds (mock (12, 72, 118, 556));
    outputColumn.setBounds (mock (1558, 72, 1662, 556));

    // The pedal unit. Its own drawing is positioned relative to its bounds.
    pedal.setBounds (pedalBounds);
    const auto origin = pedalBounds.getPosition().toFloat();
    const auto local = [origin] (juce::Rectangle<float> r) { return r - origin; };

    PedalBody::Geometry geometry;
    // The face plate sits inside the rim with about as much rim showing
    // below it as above (the rim's top ends level with the pedal bay's bottom, 547).
    geometry.face = local (mockF (164, 94, 1338, 534));
    geometry.pedalBay = local (mockF (1347, 86, 1535, 547));
    geometry.sections = { local (mockF (174, 240, 1325, 388)), local (mockF (174, 396, 1325, 524)) };
    geometry.badge = local (mockF (212, 117, 410, 207));

    // Printed lines: SAVE ... SAVE AS, and the bracket tying TUNER and
    // RESET to "HOLD TO TUNER", like the mockup's BACK / CTRL bracket.
    geometry.printedInk.startNewSubPath (mockPoint (551, 218) - origin);
    geometry.printedInk.lineTo (mockPoint (637, 218) - origin);
    geometry.printedRed.startNewSubPath (mockPoint (clusterX[0], 199) - origin);
    geometry.printedRed.lineTo (mockPoint (clusterX[0], 210) - origin);
    geometry.printedRed.lineTo (mockPoint (1000, 210) - origin);
    geometry.printedRed.startNewSubPath (mockPoint (clusterX[1], 199) - origin);
    geometry.printedRed.lineTo (mockPoint (clusterX[1], 210) - origin);
    geometry.printedRed.addEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (mockPoint (clusterX[2], 203) - origin));
    pedal.setGeometry (std::move (geometry));

    display.setBounds (mock (425, 108, 800, 202));
    rigUp.setBounds (mock (833, 130, 855, 150));
    rigDown.setBounds (mock (833, 168, 855, 188));
    pedalSave.setBounds (mock (485, 209, 551, 227));
    pedalSaveAs.setBounds (mock (637, 209, 721, 227));

    // The cluster: a red label printed above each small button.
    const auto placeInCluster = [] (juce::Label& label, juce::Button& button, int column, int row)
    {
        const auto x = clusterX[column];
        label.setBounds (mock (x - 30, clusterLabelY[row], x + 30, clusterLabelY[row] + 14));
        button.setBounds (mock (x - 18, clusterButtonY[row], x + 18, clusterButtonY[row] + 19));
    };
    placeInCluster (undoLabel, undoPedal, 0, 0);
    placeInCluster (redoLabel, redoPedal, 1, 0);
    placeInCluster (bypassLabel, bypassButton, 2, 0);
    placeInCluster (tunerLabel, tunerPedal, 0, 1);
    placeInCluster (resetLabel, resetPedal, 1, 1);
    placeInCluster (tapLabel, tapPedal, 2, 1);
    holdLabel.setBounds (mock (996, 207, 1070, 220));

    rigLevelKnob.setBounds (mock (1080, 98, 1194, 212));
    rigLevelLabel.setBounds (mock (1077, 196, 1197, 214));
    presenceKnob.setBounds (mock (1203, 98, 1317, 212));
    presenceLabel.setBounds (mock (1200, 196, 1320, 214));

    for (size_t i = 0; i < footswitches.size(); ++i)
        footswitches[i]->setBounds (mock (footswitchX[i] - 48, footswitchY[i], footswitchX[i] + 164, footswitchY[i] + 120));

    // Exactly where the artwork was cut from the mockup, plus headroom above
    // for the toe to rise into.
    expressionPedal.setBounds (mock (1342, 46, 1553, 562));

    // Dock
    rigNavigator.setBounds (mock (12, 568, 246, 822));
    selectedRig.setBounds (mock (256, 568, 652, 822));
    const auto summaryArea = mock (662, 568, 1334, 822);
    for (auto& panel : panels)
        if (panel != nullptr)
            panel->setBounds (summaryArea);
    eqPanel.setBounds (summaryArea);
    tunerPanel.setBounds (summaryArea);
    closePanel.setBounds (summaryArea.withSizeKeepingCentre (0, 0).withX (summaryArea.getRight() - 130).withY (summaryArea.getY() + 10)
                              .withSize (118, 24));
    expressionPanel.setBounds (mock (1344, 568, 1662, 822));

    signalChain.setBounds (mock (12, 832, 1662, 930));

    // Other pages cover the pedal and the dock.
    const auto pageArea = mock (126, 72, 1548, 822);   // between the INPUT and OUTPUT strips
    rigBrowser.setBounds (pageArea);
    settingsPage.setBounds (pageArea);
}
