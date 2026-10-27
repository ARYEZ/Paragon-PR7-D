#include "ModPage.h"

#include "ModDestinations.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    void styleTab (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
        button.setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.6f));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
        button.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    }

    void styleBar (juce::Slider& slider)
    {
        slider.setColour (juce::Slider::trackColourId, ParagonColours::accent.withAlpha (0.55f));
        slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff151217));
        slider.setColour (juce::Slider::textBoxTextColourId, ParagonColours::text);
        slider.setColour (juce::Slider::textBoxOutlineColourId, ParagonColours::panelEdge);
    }

    juce::String destinationText (const juce::String& id)
    {
        if (id.isEmpty())
            return "Choose...";
        if (const auto* d = ModDestinations::find (id))
            return d->group + "  " + d->name;
        return "(not in this version) " + id;
    }
}

//==============================================================================
LfoDisplay::LfoDisplay (juce::AudioProcessorValueTreeState& s) : ParameterDisplay (s) {}

void LfoDisplay::setLfo (int index)
{
    lfo = juce::jlimit (0, ParamIDs::numLfos - 1, index);
    repaint();
}

float LfoDisplay::lfoValue (const char* what) const { return value (ParamIDs::lfo (lfo, what)); }

std::vector<float> LfoDisplay::snapshot() const
{
    std::vector<float> values { (float) lfo };
    for (const auto* what : ParamIDs::lfoSettings)
        values.push_back (lfoValue (what));
    return values;
}

void LfoDisplay::paint (juce::Graphics& g)
{
    using namespace synth::mod;
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = bounds.reduced (10.0f, 12.0f);
    g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    g.drawVerticalLine (juce::roundToInt (area.getCentreX()), area.getY(), area.getBottom());

    const auto wave = (LfoWave) juce::jlimit (0, lfoWaveNames.size() - 1, juce::roundToInt (lfoValue ("Wave")));
    const auto phase = lfoValue ("Phase"), offset = lfoValue ("Offset");
    const auto oneShot = lfoValue ("OneShot") > 0.5f;
    // The random waves drawn with a fixed set of values, so the picture holds still.
    const std::array<float, 4> randoms { 0.6f, -0.3f, 0.9f, -0.7f };
    juce::Path path;
    const auto count = juce::jmax (2, (int) area.getWidth());
    for (int i = 0; i < count; ++i)
    {
        const auto t = 2.0f * (float) i / (float) (count - 1);   // two cycles
        const auto position = phase + t;
        const auto cycle = (int) std::floor (position);
        auto local = position - (float) cycle;
        if (oneShot && position >= 1.0f + phase)
            local = 0.999999f;
        const auto held = randoms[(size_t) (cycle % 4)], previous = randoms[(size_t) ((cycle + 3) % 4)];
        const auto y = (lfoWave (wave, local, held, previous) + offset) / (1.0f + std::abs (offset));
        const juce::Point<float> point (area.getX() + (float) i, area.getCentreY() - y * area.getHeight() * 0.5f);
        if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
    }
    DisplayStyle::drawCurve (g, path, area, area.getCentreY(), true);

    // How fast: Hz, or the note length it's synced to.
    const auto sync = (LfoSync) juce::jlimit (0, lfoSyncNames.size() - 1, juce::roundToInt (lfoValue ("Sync")));
    juce::String speed;
    if (sync == LfoSync::free)
        speed = juce::String (lfoValue ("Rate"), 2) + " Hz";
    else
        speed = lfoDivisionNames[juce::jlimit (0, lfoDivisionNames.size() - 1, juce::roundToInt (lfoValue ("Division")))]
              + (sync == LfoSync::triplet ? " T" : sync == LfoSync::dotted ? " D" : "");
    const auto trigger = lfoTriggerNames[juce::jlimit (0, lfoTriggerNames.size() - 1, juce::roundToInt (lfoValue ("Trigger")))];
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (speed + "   " + trigger.toUpperCase() + (oneShot ? "   ONE SHOT" : ""), bounds.reduced (10.0f, 6.0f),
                juce::Justification::topRight, false);
}

//==============================================================================
LfoPanel::LfoPanel (juce::AudioProcessorValueTreeState& s)
    : SynthPanel ("LFO"), state (s), display (s),
      wave (s, ParamIDs::lfo (0, "Wave"), "WAVE", "The LFO's shape. S&H holds a random level for each cycle; SMOOTH RND glides between random levels."),
      sync (s, ParamIDs::lfo (0, "Sync"), "SYNC", "FREE runs at RATE in Hz; NOTE, TRIPLET and DOTTED follow the song's tempo at LENGTH."),
      rate (s, ParamIDs::lfo (0, "Rate"), "RATE", "How fast, in cycles a second."),
      division (s, ParamIDs::lfo (0, "Division"), "LENGTH", "One cycle's length in notes, at the song's tempo (1/4: a beat)."),
      trigger (s, ParamIDs::lfo (0, "Trigger"), "TRIGGER", "VOICE: every note has its own LFO, starting with it. GLOBAL: one LFO for all, always running "
                                                         "(synced to the song when it plays). FIRST NOTE: one, restarted when you play with no other key down. "
                                                         "LAST NOTE: one, restarted by every note."),
      phase (s, ParamIDs::lfo (0, "Phase"), "PHASE", "Where in its cycle it starts."),
      offset (s, ParamIDs::lfo (0, "Offset"), "OFFSET", "Moves the whole wave up or down, fitting it in: at +100% it runs from 0 to the top, so it only ever pushes up; at -100%, only down."),
      delay (s, ParamIDs::lfo (0, "Delay"), "DELAY", "How long after the note (or the start) before it begins."),
      fadeIn (s, ParamIDs::lfo (0, "FadeIn"), "FADE IN", "How long it takes to grow to its full depth, after DELAY."),
      oneShot (s, ParamIDs::lfo (0, "OneShot"), "ONE SHOT", "Once through, then it stays where it ended: an envelope rather than a wobble.")
{
    for (int k = 0; k < ParamIDs::numLfos; ++k)
    {
        auto tab = std::make_unique<juce::TextButton> (juce::String (k + 1));
        styleTab (*tab);
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (901);
        tab->setTooltip ("LFO " + juce::String (k + 1));
        tab->onClick = [this, k] { selectLfo (k); };
        addAndMakeVisible (*tab);
        tabs.push_back (std::move (tab));
    }
    addAndMakeVisible (display);
    for (auto* control : { &wave, &sync, &rate, &division, &trigger, &phase, &offset, &delay, &fadeIn, &oneShot })
        addAndMakeVisible (control);
    selectLfo (0);
    startTimerHz (8);
}

bool LfoPanel::isSynced() const
{
    return juce::roundToInt (state.getRawParameterValue (ParamIDs::lfo (selected, "Sync"))->load()) != (int) synth::mod::LfoSync::free;
}

void LfoPanel::selectLfo (int index)
{
    selected = juce::jlimit (0, ParamIDs::numLfos - 1, index);
    tabs[(size_t) selected]->setToggleState (true, juce::dontSendNotification);
    const std::pair<ParamControl*, const char*> controls[] = { { &wave, "Wave" }, { &sync, "Sync" }, { &rate, "Rate" }, { &division, "Division" },
                                                               { &trigger, "Trigger" }, { &phase, "Phase" }, { &offset, "Offset" },
                                                               { &delay, "Delay" }, { &fadeIn, "FadeIn" }, { &oneShot, "OneShot" } };
    for (const auto& [control, what] : controls)
        control->attach (ParamIDs::lfo (selected, what));
    display.setLfo (selected);
    showingSync = isSynced();
    rate.setVisible (! showingSync);
    division.setVisible (showingSync);
    resized();
}

void LfoPanel::timerCallback()
{
    // SYNC (or automation, a preset, undo) switches RATE for LENGTH.
    if (isSynced() != showingSync)
        selectLfo (selected);
}

void LfoPanel::resized()
{
    auto strip = getTitleArea().withTrimmedLeft (60).reduced (0, 5).withTrimmedTop (2);
    for (auto& tab : tabs)
    {
        tab->setBounds (strip.removeFromLeft (40));
        strip.removeFromLeft (4);
    }
    auto area = getContentArea().withTrimmedTop (6);
    display.setBounds (area.removeFromTop (120));
    area.removeFromTop (8);
    const auto h = ParamControl::knobHeight;
    SynthPanel::layoutRow (area.removeFromTop (h), { &wave, &sync, showingSync ? (juce::Component*) &division : &rate, &trigger, &oneShot }, 92);
    area.removeFromTop (4);
    SynthPanel::layoutRow (area.removeFromTop (h), { &phase, &offset, &delay, &fadeIn }, 92);
}

//==============================================================================
MacroPanel::MacroPanel (ParagonX1AudioProcessor& p) : SynthPanel ("MACROS"), processor (p)
{
    auto& state = processor.getParameters();
    for (int k = 0; k < 3; ++k)
    {
        knobs.push_back (std::make_unique<ParamControl> (state, ParamIDs::macros[k], "",
                                                         "A macro knob: give it routes in the matrix (source MACRO " + juce::String (k + 1)
                                                             + ") to move several things at once. Double-click its name to rename it."));
        addAndMakeVisible (*knobs.back());
        auto name = std::make_unique<juce::Label>();
        name->setJustificationType (juce::Justification::centred);
        name->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        name->setColour (juce::Label::textColourId, ParagonColours::text);
        name->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff151217));
        name->setColour (juce::Label::textWhenEditingColourId, ParagonColours::accent);
        name->setEditable (false, true, false);
        name->onTextChange = [this, k, label = name.get()]
        {
            auto matrix = processor.getModMatrix();
            auto text = label->getText().trim().substring (0, 24);
            if (text.equalsIgnoreCase ("MACRO " + juce::String (k + 1)))
                text = {};
            matrix.macroNames[(size_t) k] = text.toUpperCase();
            processor.setModMatrix (matrix);
            processor.modMatrixEditFinished();
        };
        addAndMakeVisible (*name);
        names.push_back (std::move (name));
    }
    for (int k = 0; k < 2; ++k)
    {
        buttons.push_back (std::make_unique<ParamControl> (state, ParamIDs::macroButtons[k], "BUTTON " + juce::String (k + 1),
                                                           "A macro button: on, it pushes its routes (source BUTTON " + juce::String (k + 1)
                                                               + ") all the way; off, not at all."));
        addAndMakeVisible (*buttons.back());
    }
    modWheel.setRange (0.0, 1.0);
    modWheel.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    modWheel.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff151217));
    modWheel.setTooltip ("An on-screen mod wheel (MIDI controller 1), for trying routes from MOD WHEEL without a keyboard. Your keyboard's own wheel works too.");
    modWheel.onValueChange = [this] { processor.setScreenModWheel ((float) modWheel.getValue()); };
    addAndMakeVisible (modWheel);
    modWheelLabel.setText ("MOD WHEEL", juce::dontSendNotification);
    modWheelLabel.setJustificationType (juce::Justification::centred);
    modWheelLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    modWheelLabel.setColour (juce::Label::textColourId, ParagonColours::textDim);
    addAndMakeVisible (modWheelLabel);
    timerCallback();
    startTimerHz (4);
}

void MacroPanel::timerCallback()
{
    // The names, as the preset (or undo) has them.
    const auto& matrix = processor.getModMatrix();
    for (int k = 0; k < 3; ++k)
        if (! names[(size_t) k]->isBeingEdited() && names[(size_t) k]->getText() != matrix.macroName (k))
            names[(size_t) k]->setText (matrix.macroName (k), juce::dontSendNotification);
}

void MacroPanel::resized()
{
    auto area = getContentArea().withTrimmedTop (4);
    auto wheel = area.removeFromRight (70);
    modWheelLabel.setBounds (wheel.removeFromTop (16));
    modWheel.setBounds (wheel.withSizeKeepingCentre (26, wheel.getHeight() - 8));
    auto row = area.removeFromTop (ParamControl::knobHeight + 18);
    const auto width = row.getWidth() / 5;
    for (int k = 0; k < 3; ++k)
    {
        auto cell = row.removeFromLeft (width);
        names[(size_t) k]->setBounds (cell.removeFromTop (18).reduced (2, 0));
        knobs[(size_t) k]->setBounds (cell);
    }
    for (int k = 0; k < 2; ++k)
        buttons[(size_t) k]->setBounds (row.removeFromLeft (width).withTrimmedTop (18));
}

//==============================================================================
MatrixPanel::Row::Row (MatrixPanel& o, int i) : owner (o), index (i)
{
    on.setTooltip ("Switch this route on or off");
    on.onClick = [this] { owner.change (index, [this] (ModRouteData& r) { r.on = on.getToggleState(); }, true); };
    addAndMakeVisible (on);

    for (int s = 1; s < synth::mod::numSources; ++s)
        source.addItem (synth::mod::sourceNames[(size_t) s], s + 1);
    source.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    source.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    source.setColour (juce::ComboBox::textColourId, ParagonColours::text);
    source.setTooltip ("What moves the knob: an LFO, a macro, a controller from your keyboard, how hard the note is played...");
    source.onChange = [this]
    {
        const auto id = source.getSelectedId() - 1;
        if (id >= 1)
            owner.change (index, [id] (ModRouteData& r) { r.source = synth::mod::sourceIds[(size_t) id]; }, true);
    };
    addAndMakeVisible (source);

    for (auto* button : { &destination, &mode, &remove })
    {
        styleTab (*button);
        addAndMakeVisible (*button);
    }
    destination.setTooltip ("The knob it moves");
    destination.onClick = [this] { owner.chooseDestination (index); };
    mode.setTooltip ("+ adds the amount to the knob (stopping at its ends); % moves it by a share of the travel it has left, so it always "
                     "reaches the end exactly at 100%");
    mode.onClick = [this] { owner.change (index, [] (ModRouteData& r) { r.percent = ! r.percent; }, true); };
    remove.setTooltip ("Delete this route");
    remove.onClick = [this]
    {
        auto matrix = owner.processor.getModMatrix();
        if (index < (int) matrix.routes.size())
            matrix.routes.erase (matrix.routes.begin() + index);
        owner.processor.setModMatrix (matrix);
        owner.processor.modMatrixEditFinished();
    };

    amount.setRange (-1.0, 1.0);
    amount.textFromValueFunction = [] (double v) { const auto p = juce::roundToInt (v * 100.0); return (p > 0 ? "+" : "") + juce::String (p) + "%"; };
    amount.setTooltip ("How far it moves the knob: at +-100%, its whole travel. Double-click: 0.");
    amount.setDoubleClickReturnValue (true, 0.0);
    lag.setRange (0.0, 2000.0);
    lag.setSkewFactorFromMidPoint (200.0);
    lag.textFromValueFunction = [] (double v) { return v < 0.5 ? juce::String ("no lag") : juce::String (juce::roundToInt (v)) + " ms"; };
    lag.setTooltip ("LAG slows the source down, smoothing jumps (a stepped LFO, a pressed button)");
    curve.setRange (-1.0, 1.0);
    curve.textFromValueFunction = [] (double v)
    {
        const auto p = juce::roundToInt (v * 100.0);
        return p == 0 ? juce::String ("linear") : (p > 0 ? "+" : "") + juce::String (p) + "%";
    };
    curve.setTooltip ("CURVE bends the source: below 0 it does most of its work early, above 0 late. Double-click: linear.");
    curve.setDoubleClickReturnValue (true, 0.0);
    for (auto* slider : { &amount, &lag, &curve })
    {
        styleBar (*slider);
        slider->setTextBoxStyle (juce::Slider::TextBoxLeft, false, slider == &amount ? 54 : 58, 22);
        slider->onDragEnd = [this] { owner.processor.modMatrixEditFinished(); };
        addAndMakeVisible (*slider);
    }
    amount.onValueChange = [this] { owner.change (index, [this] (ModRouteData& r) { r.amount = (float) amount.getValue(); }, false); };
    lag.onValueChange = [this] { owner.change (index, [this] (ModRouteData& r) { r.lagMs = (float) lag.getValue(); }, false); };
    curve.onValueChange = [this] { owner.change (index, [this] (ModRouteData& r) { r.curve = (float) curve.getValue(); }, false); };
}

void MatrixPanel::Row::show (const ModRouteData& r)
{
    on.setToggleState (r.on, juce::dontSendNotification);
    const auto s = (int) synth::mod::sourceFromId (r.source);
    source.setSelectedId (s >= 1 ? s + 1 : 0, juce::dontSendNotification);
    destination.setButtonText (destinationText (r.destination));
    mode.setButtonText (r.percent ? "%" : "+");
    amount.setValue (r.amount, juce::dontSendNotification);
    lag.setValue (r.lagMs, juce::dontSendNotification);
    curve.setValue (r.curve, juce::dontSendNotification);
}

void MatrixPanel::Row::resized()
{
    auto area = getLocalBounds().reduced (0, 3);
    on.setBounds (area.removeFromLeft (28));
    source.setBounds (area.removeFromLeft (128));
    area.removeFromLeft (20);   // the arrow, painted by the panel
    destination.setBounds (area.removeFromLeft (230));
    area.removeFromLeft (10);
    remove.setBounds (area.removeFromRight (28));
    area.removeFromRight (8);
    curve.setBounds (area.removeFromRight (130));
    area.removeFromRight (8);
    lag.setBounds (area.removeFromRight (130));
    area.removeFromRight (8);
    mode.setBounds (area.removeFromRight (30));
    area.removeFromRight (6);
    amount.setBounds (area);
}

//==============================================================================
MatrixPanel::MatrixPanel (ParagonX1AudioProcessor& p) : SynthPanel ("MOD MATRIX"), processor (p)
{
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    styleTab (addButton);
    addButton.setTooltip ("Add a route: a source moving a knob");
    addButton.onClick = [this] { addRoute(); };
    addAndMakeVisible (addButton);
    for (auto* label : { &countLabel, &emptyLabel })
    {
        label->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, ParagonColours::textDim);
        addAndMakeVisible (*label);
    }
    countLabel.setJustificationType (juce::Justification::centredRight);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setText ("No routes yet. + ADD ROUTE, then choose a source (an LFO, a macro, your mod wheel...) and the knob it moves.",
                        juce::dontSendNotification);
    rebuildRows();
    startTimerHz (10);
}

void MatrixPanel::addRoute()
{
    auto matrix = processor.getModMatrix();
    if ((int) matrix.routes.size() >= synth::mod::maxRoutes)
        return;
    ModRouteData route;
    route.amount = 0.5f;
    matrix.routes.push_back (route);
    processor.setModMatrix (matrix);
    processor.modMatrixEditFinished();
    rebuildRows();
    viewport.setViewPosition (0, list.getHeight());
}

void MatrixPanel::change (int index, const std::function<void (ModRouteData&)>& edit, bool finished)
{
    auto matrix = processor.getModMatrix();
    if (index < 0 || index >= (int) matrix.routes.size())
        return;
    edit (matrix.routes[(size_t) index]);
    processor.setModMatrix (matrix);
    shown = matrix;
    if (auto* row = getRow (index))
        row->show (matrix.routes[(size_t) index]);
    if (finished)
        processor.modMatrixEditFinished();
}

void MatrixPanel::chooseDestination (int index)
{
    // A menu for each group (Osc 1, Filter, Shaper 1, Amp...).
    juce::PopupMenu menu;
    juce::String group;
    juce::PopupMenu sub;
    const auto& all = ModDestinations::all();
    const auto current = index < (int) processor.getModMatrix().routes.size() ? processor.getModMatrix().routes[(size_t) index].destination : juce::String();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].group != group)
        {
            if (group.isNotEmpty())
                menu.addSubMenu (group, sub);
            sub = {};
            group = all[i].group;
        }
        sub.addItem ((int) i + 1, all[i].name, true, all[i].parameterId == current);
    }
    menu.addSubMenu (group, sub);
    auto* button = getRow (index) != nullptr ? &getRow (index)->destination : nullptr;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button),
                        [safe = juce::Component::SafePointer<MatrixPanel> (this), index] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            const auto id = ModDestinations::all()[(size_t) result - 1].parameterId;
                            safe->change (index, [id] (ModRouteData& r) { r.destination = id; }, true);
                        });
}

void MatrixPanel::rebuildRows()
{
    shown = processor.getModMatrix();
    rows.clear();
    for (int i = 0; i < (int) shown.routes.size(); ++i)
    {
        rows.push_back (std::make_unique<Row> (*this, i));
        rows.back()->show (shown.routes[(size_t) i]);
        list.addAndMakeVisible (*rows.back());
    }
    countLabel.setText (juce::String ((int) shown.routes.size()) + " / " + juce::String (synth::mod::maxRoutes) + " routes, "
                            + juce::String (processor.getNumPlayingRoutes()) + " playing",
                        juce::dontSendNotification);
    emptyLabel.setVisible (rows.empty());
    addButton.setEnabled ((int) shown.routes.size() < synth::mod::maxRoutes);
    resized();
}

void MatrixPanel::timerCallback()
{
    // Undo, a preset or another row changed the matrix: show it.
    if (juce::Component::isMouseButtonDownAnywhere())
        return;
    const auto& now = processor.getModMatrix();
    if (now.routes.size() != shown.routes.size())
        rebuildRows();
    else if (! (now == shown))
    {
        shown = now;
        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->show (now.routes[i]);
    }
    countLabel.setText (juce::String ((int) shown.routes.size()) + " / " + juce::String (synth::mod::maxRoutes) + " routes, "
                            + juce::String (processor.getNumPlayingRoutes()) + " playing",
                        juce::dontSendNotification);
}

void MatrixPanel::paint (juce::Graphics& g)
{
    SynthPanel::paint (g);
    // Column names, and an arrow from each source to its destination.
    const auto header = getContentArea().removeFromTop (20).toFloat();
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (ParagonColours::textDim);
    const auto x0 = header.getX() + 4.0f;
    g.drawText ("ON", juce::Rectangle<float> (x0, header.getY(), 28.0f, 20.0f), juce::Justification::centredLeft, false);
    g.drawText ("SOURCE", juce::Rectangle<float> (x0 + 28.0f, header.getY(), 128.0f, 20.0f), juce::Justification::centredLeft, false);
    g.drawText ("DESTINATION", juce::Rectangle<float> (x0 + 176.0f, header.getY(), 230.0f, 20.0f), juce::Justification::centredLeft, false);
    auto right = header.withTrimmedRight (12.0f);
    right.removeFromRight (36.0f);
    g.drawText ("CURVE", right.removeFromRight (130.0f), juce::Justification::centredLeft, false);
    right.removeFromRight (8.0f);
    g.drawText ("LAG", right.removeFromRight (130.0f), juce::Justification::centredLeft, false);
    right.removeFromRight (8.0f);
    g.drawText ("+/%", right.removeFromRight (30.0f), juce::Justification::centred, false);
    right.removeFromRight (6.0f);
    g.drawText ("AMOUNT", right.withLeft (x0 + 416.0f), juce::Justification::centredLeft, false);

    g.setColour (ParagonColours::accent.withAlpha (0.7f));
    for (const auto& row : rows)
    {
        const auto bounds = row->getBounds().translated (viewport.getX(), viewport.getY() - viewport.getViewPositionY());
        if (! viewport.getBounds().contains (bounds.getCentre()))
            continue;
        const auto y = (float) bounds.getCentreY();
        const auto x = (float) bounds.getX() + 28.0f + 128.0f + 4.0f;
        g.drawArrow ({ x, y, x + 12.0f, y }, 1.5f, 6.0f, 6.0f);
    }
}

void MatrixPanel::resized()
{
    auto area = getContentArea().withTrimmedTop (2);
    area.removeFromTop (20);   // the column names
    auto bottom = area.removeFromBottom (34);
    addButton.setBounds (bottom.removeFromLeft (140).reduced (0, 4));
    countLabel.setBounds (bottom.reduced (4, 0));
    viewport.setBounds (area);
    constexpr int rowHeight = 36;
    list.setSize (area.getWidth() - 10, juce::jmax (area.getHeight(), (int) rows.size() * rowHeight));
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (4, (int) i * rowHeight, list.getWidth() - 8, rowHeight);
    emptyLabel.setBounds (area.withSizeKeepingCentre (area.getWidth(), 40));
    repaint();
}

//==============================================================================
ModPage::ModPage (ParagonX1AudioProcessor& p) : lfos (p.getParameters()), macros (p), matrix (p)
{
    addAndMakeVisible (lfos);
    addAndMakeVisible (macros);
    addAndMakeVisible (matrix);
}

void ModPage::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft (530);
    lfos.setBounds (left.removeFromTop (390));
    left.removeFromTop (10);
    macros.setBounds (left);
    area.removeFromLeft (10);
    matrix.setBounds (area);
}
