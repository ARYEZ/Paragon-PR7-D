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
    area.removeFromBottom (velocityLane + 2.0f * smallLane + 3.0f * laneGap);
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
    const std::vector<float> settings { (float) getLength() };
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
void ArpPage::ArpTab::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::TextButton::paintButton (g, highlighted, down);
    // A light when that arp is on.
    g.setColour (lit ? ParagonColours::glow : ParagonColours::panelEdge);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ 8.0f, (float) getHeight() * 0.5f }));
}

ArpPage::ArpPage (ParagonX1AudioProcessor& p)
    : processor (p), state (p.getParameters()), editor (p),
      on (state, ParamIDs::arp (0, "On"), "ON", "This arp on: the oscillators routed to it (ticked above) play its notes, not the keys'."),
      mode (state, ParamIDs::arp (0, "Mode"), "MODE", "How it walks through the keys you hold: UP, DOWN, UP/DOWN, DOWN/UP, AS PLAYED (the order you pressed them), "
                                                    "RANDOM; POLY plays them all together on each block."),
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
    editor.setBounds (area);
}
