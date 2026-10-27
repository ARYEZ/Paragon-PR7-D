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
namespace
{
    // START FROM: shapes to begin an envelope or a drawn LFO shape from.
    const juce::StringArray envelopeStarts { "Attack, decay, sustain, release", "Ramp up", "Ramp down", "Pluck", "Swell", "Gate (1 beat)", "Wobble (loop)" };
    const juce::StringArray lfoShapeStarts { "Triangle", "Ramp up", "Ramp down", "Square", "Steps (4)", "Bounce", "Spike" };
    const juce::StringArray stepStarts { "Trance gate (every step)", "Off-beats", "On the beats", "3 + 3 + 2", "Accents", "Rise", "Fall",
                                         "Long and short (linked)", "Ping-pong (STEREO)" };

    synth::seq::Pattern stepStart (int choice)
    {
        synth::seq::Pattern pattern;
        for (int row = 0; row < 2; ++row)
            for (int k = 0; k < synth::seq::maxSteps; ++k)
            {
                auto& step = pattern.rows[(size_t) row][(size_t) k];
                const auto beat = k % 4, eighth = k % 8;
                switch (choice)
                {
                    case 1: step.on = (k & 1) != 0; break;
                    case 2: step.on = beat == 0; break;
                    case 3: step.on = eighth == 0 || eighth == 3 || eighth == 6; break;
                    case 4: step.level = beat == 0 ? 1.0f : beat == 2 ? 0.65f : 0.4f; break;
                    case 5: step.level = 0.1f + 0.9f * (float) (k % 16) / 15.0f; break;
                    case 6: step.level = 1.0f - 0.9f * (float) (k % 16) / 15.0f; break;
                    case 7: step.tied = beat == 0; step.on = beat != 3; break;
                    case 8: step.on = (k & 1) == row; break;
                    default: break;
                }
            }
        return pattern;
    }

    const juce::StringArray pitchStarts { "Drop (150 ms)", "Kick drop", "Rise", "Fall away", "Arpeggio steps", "Wobble (loop)", "Flat (none)" };

    ModShapeData pitchStart (int choice)
    {
        using P = synth::mod::ShapePoint;
        switch (choice)
        {
            case 1: return { { P { 0.0f, 1.0f, -0.7f }, P { 0.08f, 0.5f, 0.0f } }, -1, -1, -1 };
            case 2: return { { P { 0.0f, 0.25f, 0.4f }, P { 0.4f, 0.5f, 0.0f } }, -1, -1, -1 };
            case 3: return { { P { 0.0f, 0.5f, 0.0f }, P { 0.5f, 0.5f, 0.5f }, P { 1.5f, 0.0f, 0.0f } }, 1, -1, -1 };
            case 4: return { { P { 0.0f, 0.5f, 0.0f }, P { 0.1f, 1.0f, 0.0f }, P { 0.101f, 0.5f, 0.0f }, P { 0.2f, 0.5f, 0.0f }, P { 0.201f, 0.75f, 0.0f },
                               P { 0.3f, 0.75f, 0.0f } }, -1, 0, 5 };
            case 5: return { { P { 0.0f, 0.5f, 0.0f }, P { 0.1f, 0.6f, 0.0f }, P { 0.2f, 0.4f, 0.0f }, P { 0.3f, 0.5f, 0.0f } }, -1, 0, 3 };
            case 6: return { { P { 0.0f, 0.5f, 0.0f }, P { 1.0f, 0.5f, 0.0f } }, -1, -1, -1 };
            default: return ModShapeData::defaultPitchEnvelope();
        }
    }

    ModShapeData envelopeStart (int choice)
    {
        using P = synth::mod::ShapePoint;
        switch (choice)
        {
            case 1: return { { P { 0.0f, 0.0f, 0.0f }, P { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 };
            case 2: return { { P { 0.0f, 1.0f, 0.0f }, P { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
            case 3: return { { P { 0.0f, 0.0f, 0.0f }, P { 0.005f, 1.0f, 0.0f }, P { 0.6f, 0.0f, -0.6f } }, -1, -1, -1 };
            case 4: return { { P { 0.0f, 0.0f, 0.5f }, P { 2.0f, 1.0f, 0.0f }, P { 3.0f, 0.0f, -0.4f } }, 1, -1, -1 };
            case 5: return { { P { 0.0f, 1.0f, 0.0f }, P { 0.5f, 1.0f, 0.0f }, P { 0.501f, 0.0f, 0.0f }, P { 1.0f, 0.0f, 0.0f } }, -1, 0, 3 };
            case 6: return { { P { 0.0f, 0.0f, 0.0f }, P { 0.25f, 1.0f, -0.3f }, P { 0.5f, 0.0f, 0.3f } }, -1, 0, 2 };
            default: return ModShapeData::defaultEnvelope();
        }
    }

    ModShapeData lfoShapeStart (int choice)
    {
        using P = synth::mod::ShapePoint;
        switch (choice)
        {
            case 1: return { { P { 0.0f, 0.0f, 0.0f }, P { 0.999f, 1.0f, 0.0f }, P { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
            case 2: return { { P { 0.0f, 1.0f, 0.0f }, P { 0.999f, 0.0f, 0.0f }, P { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 };
            case 3: return { { P { 0.0f, 1.0f, 0.0f }, P { 0.5f, 1.0f, 0.0f }, P { 0.501f, 0.0f, 0.0f }, P { 0.999f, 0.0f, 0.0f }, P { 1.0f, 1.0f, 0.0f } }, -1, -1, -1 };
            case 4: return { { P { 0.0f, 0.0f, 0.0f }, P { 0.249f, 0.0f, 0.0f }, P { 0.25f, 0.33f, 0.0f }, P { 0.499f, 0.33f, 0.0f }, P { 0.5f, 0.67f, 0.0f },
                               P { 0.749f, 0.67f, 0.0f }, P { 0.75f, 1.0f, 0.0f }, P { 0.999f, 1.0f, 0.0f }, P { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
            case 5: return { { P { 0.0f, 0.0f, -0.6f }, P { 0.5f, 1.0f, 0.6f }, P { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
            case 6: return { { P { 0.0f, 0.0f, 0.0f }, P { 0.05f, 1.0f, -0.7f }, P { 1.0f, 0.0f, 0.0f } }, -1, -1, -1 };
            default: return ModShapeData::defaultLfoShape();
        }
    }
}

ModulatorPanel::ModulatorPanel (ParagonX1AudioProcessor& p)
    : SynthPanel ({}), state (p.getParameters()), processor (p), display (p.getParameters()), editor (p), stepEditor (p),
      wave (state, ParamIDs::lfo (0, "Wave"), "WAVE", "The LFO's shape. S&H holds a random level for each cycle; SMOOTH RND glides between random levels; "
                                                     "CUSTOM 1 to 3 are the shapes drawn on the SHAPE tabs."),
      sync (state, ParamIDs::lfo (0, "Sync"), "SYNC", "FREE runs at RATE in Hz; NOTE, TRIPLET and DOTTED follow the song's tempo at LENGTH."),
      rate (state, ParamIDs::lfo (0, "Rate"), "RATE", "How fast, in cycles a second."),
      division (state, ParamIDs::lfo (0, "Division"), "LENGTH", "One cycle's length in notes, at the song's tempo (1/4: a beat)."),
      trigger (state, ParamIDs::lfo (0, "Trigger"), "TRIGGER", "VOICE: every note has its own LFO, starting with it. GLOBAL: one LFO for all, always running "
                                                             "(synced to the song when it plays). FIRST NOTE: one, restarted when you play with no other key down. "
                                                             "LAST NOTE: one, restarted by every note."),
      phase (state, ParamIDs::lfo (0, "Phase"), "PHASE", "Where in its cycle it starts."),
      offset (state, ParamIDs::lfo (0, "Offset"), "OFFSET", "Moves the whole wave up or down, fitting it in: at +100% it runs from 0 to the top, so it only ever pushes up; at -100%, only down."),
      delay (state, ParamIDs::lfo (0, "Delay"), "DELAY", "How long after the note (or the start) before it begins."),
      fadeIn (state, ParamIDs::lfo (0, "FadeIn"), "FADE IN", "How long it takes to grow to its full depth, after DELAY."),
      oneShot (state, ParamIDs::lfo (0, "OneShot"), "ONE SHOT", "Once through, then it stays where it ended: an envelope rather than a wobble."),
      envMode (state, ParamIDs::env (0, "Mode"), "MODE", "ONE SHOT: once through, then it holds the last point. SUSTAIN: holds at the S point while the key is down "
                                                        "(let go earlier and it jumps there). SUS JUMP: the same, but jumps to where the level matches, so nothing jumps. "
                                                        "LOOP: round L1 to L2 for ever. LOOP+REL: round them while the key is down, then on to the end. "
                                                        "PING PONG: back and forth between them (+REL: then on to the end)."),
      envTrigger (state, ParamIDs::env (0, "Trigger"), "TRIGGER", "VOICE: every note has its own, starting with it. FIRST NOTE: one, started when you play with no "
                                                                "other key down. LAST NOTE: one, restarted by every note."),
      envSync (state, ParamIDs::env (0, "Sync"), "SYNC", "Times in beats of your song (the grid shows beats) rather than seconds."),
      envSpeed (state, ParamIDs::env (0, "Speed"), "SPEED", "How fast it runs through its points: x0.1 (ten times slower) to x10."),
      envSteps (state, ParamIDs::env (0, "Steps"), "STEPS", "Makes it move in steps (for arcade and FX sounds): how many levels. SMOOTH: none."),
      envFlip (state, ParamIDs::env (0, "Flip"), "FLIP", "Upside down."),
      envOffset (state, ParamIDs::env (0, "Offset"), "OFFSET", "The level counted as zero: above 0, it pushes both ways (at 50%, as far down as up)."),
      pitchOn (state, ParamIDs::pitchEnvOn, "ON", "Switch the pitch envelope on (for the oscillators ticked below). It's also a source in the matrix, PITCH ENV."),
      pitchMode (state, ParamIDs::pitchEnvMode, "MODE", "ONE SHOT: once through. SUSTAIN / SUS JUMP: holds at the S point while the key is down. "
                                                      "LOOP, PING PONG (+REL): round or back and forth between L1 and L2."),
      pitchSpeed (state, ParamIDs::pitchEnvSpeed, "SPEED", "How fast it runs through its points: x0.1 (ten times slower) to x10."),
      pitchRange (state, ParamIDs::pitchEnvRange, "RANGE", "How far the top and bottom of the picture move the pitch, in semitones (the middle is no change)."),
      pitchSteps (state, ParamIDs::pitchEnvSteps, "STEPS", "Moves the pitch in steps (arcade and FX sounds): how many levels. SMOOTH: none."),
      pitchFlip (state, ParamIDs::pitchEnvFlip, "FLIP", "Upside down: rises become falls."),
      keytrack (state, ParamIDs::pitchKeytrack, "KEYTRACK", "How far the pitch follows the keys: 100% as normal, 0% every key the same note (middle C), "
                                                          "-100% upside down (higher keys lower). The pitch envelope, bend and glide still move it."),
      bendLag (state, ParamIDs::bendLag, "BEND LAG", "Smooths the pitch wheel (and its automation), so steps in it glide."),
      glideCurve (state, ParamIDs::glideCurve, "GLIDE CURVE", "The glide's path: 0 straight; below 0 it gets most of the way at once and settles "
                                                             "(a siren's swoop); above 0 it starts slowly. GLIDE's time is on the PLAY panel."),
      glideSplit (state, ParamIDs::glideSplit, "GLIDE SPLIT", "Notes below this one never glide, nor are glided from: a bass line under a gliding lead. Off: every note."),
      seqOn (state, ParamIDs::seqOn, "ON", "The trance gate: the steps cut the volume (by VOL MIX). The step sequencer is also a source in the matrix, STEP SEQ, on or off."),
      seqTrigger (state, ParamIDs::seqTrigger, "TRIGGER", "SONG: in step with your song while it plays (from the first note when it's stopped). "
                                                         "VOICE: every note its own, from its start. FIRST NOTE: one, started when you play with no other key down. "
                                                         "LAST NOTE: one, restarted by every note."),
      seqSpeed (state, ParamIDs::seqDivision, "SPEED", "How long a step is, in notes at the song's tempo (1/16: a sixteenth)."),
      seqTiming (state, ParamIDs::seqTiming, "TIMING", "Straight, triplet (two thirds as long) or dotted (half as long again) steps."),
      seqMode (state, ParamIDs::seqMode, "MODE", "MONO: one row of steps. STEREO: two, the top for the left channel and the bottom for the right."),
      seqLength (state, ParamIDs::seqLength, "LENGTH", "How many steps before it starts over: 2 to 32."),
      seqGate (state, ParamIDs::seqGate, "GATE", "How much of each step it's open (with linked steps, of the last one)."),
      seqContour (state, ParamIDs::seqContour, "CONTOUR", "Softens the steps' edges, from hard (0%) to a fade of up to half a step, so nothing clicks."),
      seqDecay (state, ParamIDs::seqDecay, "DECAY", "Each block falls away after it opens: the time it takes to fade out. OFF: it holds."),
      seqVolMix (state, ParamIDs::seqVolMix, "VOL MIX", "How deep the gate cuts the volume: 100% to silence between steps, 0% not at all (the matrix source still runs)."),
      seqWidth (state, ParamIDs::seqWidth, "WIDTH", "In STEREO: the two rows fully apart (100%) to the same, their mix (0%)."),
      shuffle (state, ParamIDs::shuffle, "SHUFFLE", "Swing: every second step starts late, up to half a step (75% swing). Shared with the arpeggiator and drums when they come."),
      seqDelay (state, ParamIDs::seqDelay, "DELAY", "How long after a note starts before the gate comes in."),
      seqFadeIn (state, ParamIDs::seqFadeIn, "FADE IN", "How long it takes to come in fully, after DELAY."),
      seqLowKey (state, ParamIDs::seqLowKey, "START NOTE", "The lowest note it plays on: notes outside START NOTE to END NOTE are left alone."),
      seqHighKey (state, ParamIDs::seqHighKey, "END NOTE", "The highest note it plays on.")
{
    const auto addTab = [this] (Kind k, int i, const juce::String& text, const juce::String& tip)
    {
        auto button = std::make_unique<juce::TextButton> (text);
        styleTab (*button);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (901);
        button->setTooltip (tip);
        button->onClick = [this, k, i] { select (k, i); };
        addAndMakeVisible (*button);
        tabs.push_back ({ k, i, std::move (button) });
    };
    for (int k = 0; k < ParamIDs::numLfos; ++k)
        addTab (Kind::lfo, k, juce::String (k + 1), "LFO " + juce::String (k + 1));
    for (int k = 0; k < ParamIDs::numEnvelopes; ++k)
        addTab (Kind::envelope, k, juce::String (k + 1), "Modulation envelope " + juce::String (k + 1) + " (ENV " + juce::String (k + 1) + " in the matrix)");
    for (int k = 0; k < synth::mod::numLfoShapes; ++k)
        addTab (Kind::shape, k, juce::String (k + 1), "Drawn LFO shape " + juce::String (k + 1) + " (an LFO's WAVE: CUSTOM " + juce::String (k + 1) + ")");
    addTab (Kind::pitch, 0, "PITCH", "The PITCH module: a pitch envelope, keytrack, bend lag and the glide's curve and split");
    addTab (Kind::step, 0, "STEP", "The STEP SEQUENCER: a trance gate, and STEP SEQ in the matrix");
    addChildComponent (stepEditor);
    for (auto* control : seqControls())
        addChildComponent (control);
    for (int i = 0; i < ParamIDs::numOscillators; ++i)
    {
        pitchRoutes.push_back (std::make_unique<ParamControl> (state, ParamIDs::osc (i, "PitchEnv"), juce::String (i + 1),
                                                               "Oscillator " + juce::String (i + 1) + " follows the pitch envelope."));
        pitchRoutes.back()->setCompact (true);
        addChildComponent (*pitchRoutes.back());
    }
    pitchRouteLabel.setText ("FOLLOWING IT", juce::dontSendNotification);
    pitchRouteLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    pitchRouteLabel.setColour (juce::Label::textColourId, ParagonColours::textDim);
    addChildComponent (pitchRouteLabel);

    addAndMakeVisible (display);
    addChildComponent (editor);
    for (auto* control : { &wave, &sync, &rate, &division, &trigger, &phase, &offset, &delay, &fadeIn, &oneShot,
                           &envMode, &envTrigger, &envSync, &envSpeed, &envSteps, &envFlip, &envOffset,
                           &pitchOn, &pitchMode, &pitchSpeed, &pitchRange, &pitchSteps, &pitchFlip, &keytrack, &bendLag, &glideCurve, &glideSplit })
        addChildComponent (control);
    startFromBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
    startFromBox.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
    startFromBox.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
    startFromBox.setTextWhenNothingSelected ("Choose...");
    startFromBox.setTooltip ("Start again from a ready-made shape (your points are replaced; undo brings them back)");
    startFromBox.onChange = [this]
    {
        if (startFromBox.getSelectedId() > 0)
            startFrom (startFromBox.getSelectedId() - 1);
        startFromBox.setSelectedId (0, juce::dontSendNotification);
    };
    addChildComponent (startFromBox);
    for (auto* label : { &startFromLabel, &hint })
    {
        label->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, ParagonColours::textDim);
        addChildComponent (*label);
    }
    startFromLabel.setText ("START FROM", juce::dontSendNotification);
    startFromLabel.setJustificationType (juce::Justification::centred);
    hint.setJustificationType (juce::Justification::topLeft);
    selectLfo (0);
    startTimerHz (8);
}

std::vector<ParamControl*> ModulatorPanel::seqControls() noexcept
{
    return { &seqOn, &seqTrigger, &seqSpeed, &seqTiming, &seqMode, &seqLength, &seqGate, &seqContour,
             &seqDecay, &seqVolMix, &seqWidth, &shuffle, &seqDelay, &seqFadeIn, &seqLowKey, &seqHighKey };
}

bool ModulatorPanel::isSynced() const
{
    return kind == Kind::lfo
        && juce::roundToInt (state.getRawParameterValue (ParamIDs::lfo (selected, "Sync"))->load()) != (int) synth::mod::LfoSync::free;
}

void ModulatorPanel::select (Kind newKind, int index)
{
    kind = newKind;
    selected = index;
    for (auto& tab : tabs)
        if (tab.kind == kind && tab.index == selected)
            tab.button->setToggleState (true, juce::dontSendNotification);

    const auto lfo = kind == Kind::lfo, envelope = kind == Kind::envelope;
    if (lfo)
    {
        const std::pair<ParamControl*, const char*> controls[] = { { &wave, "Wave" }, { &sync, "Sync" }, { &rate, "Rate" }, { &division, "Division" },
                                                                   { &trigger, "Trigger" }, { &phase, "Phase" }, { &offset, "Offset" },
                                                                   { &delay, "Delay" }, { &fadeIn, "FadeIn" }, { &oneShot, "OneShot" } };
        for (const auto& [control, what] : controls)
            control->attach (ParamIDs::lfo (selected, what));
        display.setLfo (selected);
    }
    else if (envelope)
    {
        const std::pair<ParamControl*, const char*> controls[] = { { &envMode, "Mode" }, { &envTrigger, "Trigger" }, { &envSync, "Sync" }, { &envSpeed, "Speed" },
                                                                   { &envSteps, "Steps" }, { &envFlip, "Flip" }, { &envOffset, "Offset" } };
        for (const auto& [control, what] : controls)
            control->attach (ParamIDs::env (selected, what));
        editor.setTarget (MultiPointEditor::Kind::envelope, selected);
    }
    else if (kind == Kind::pitch)
        editor.setTarget (MultiPointEditor::Kind::pitchEnvelope, 0);
    else if (kind == Kind::shape)
        editor.setTarget (MultiPointEditor::Kind::lfoShape, selected);
    const auto pitch = kind == Kind::pitch;
    for (auto* control : { &pitchOn, &pitchMode, &pitchSpeed, &pitchRange, &pitchSteps, &pitchFlip, &keytrack, &bendLag, &glideCurve, &glideSplit })
        control->setVisible (pitch);
    for (auto& route : pitchRoutes)
        route->setVisible (pitch);
    pitchRouteLabel.setVisible (pitch);
    const auto step = kind == Kind::step;
    for (auto* control : seqControls())
        control->setVisible (step);
    stepEditor.setVisible (step);

    showingSync = isSynced();
    display.setVisible (lfo);
    for (auto* control : { &wave, &sync, &trigger, &phase, &offset, &delay, &fadeIn, &oneShot })
        control->setVisible (lfo);
    rate.setVisible (lfo && ! showingSync);
    division.setVisible (lfo && showingSync);
    for (auto* control : { &envMode, &envTrigger, &envSync, &envSpeed, &envSteps, &envFlip, &envOffset })
        control->setVisible (envelope);
    editor.setVisible (! lfo && ! step);
    startFromBox.setVisible (! lfo);
    startFromLabel.setVisible (! lfo);
    hint.setVisible (! lfo && ! pitch);
    startFromBox.clear (juce::dontSendNotification);
    startFromBox.addItemList (envelope ? envelopeStarts : pitch ? pitchStarts : step ? stepStarts : lfoShapeStarts, 1);
    hint.setText (step ? juce::String ("STEP SEQ is a source in the matrix too. Click a step to set its height (drag across to draw), "
                                       "right-click to delete it, click the arrow under it to link it to the next.")
                  : envelope ? "ENV " + juce::String (selected + 1) + " is a source in the matrix. Double-click to add points, drag lines to curve them, "
                                 "right-click a point for SUSTAIN and LOOP."
                           : "Choose CUSTOM " + juce::String (selected + 1) + " as an LFO's WAVE to play this shape. Double-click to add points, "
                                 "drag lines to curve them. The ends meet, so it loops without a click.",
                  juce::dontSendNotification);
    resized();
    repaint();
}

void ModulatorPanel::startFrom (int choice)
{
    if (kind == Kind::step)
    {
        stepEditor.setPattern (stepStart (choice), true);
        return;
    }
    editor.setShape (kind == Kind::envelope ? envelopeStart (choice) : kind == Kind::pitch ? pitchStart (choice) : lfoShapeStart (choice), true);
}

void ModulatorPanel::timerCallback()
{
    // SYNC (or automation, a preset, undo) switches RATE for LENGTH.
    if (isSynced() != showingSync)
        select (kind, selected);
}

void ModulatorPanel::paint (juce::Graphics& g)
{
    SynthPanel::paint (g);
    // The tab groups' names.
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (ParagonColours::text);
    for (const auto& [group, name] : { std::pair { Kind::lfo, "LFO" }, std::pair { Kind::envelope, "ENV" }, std::pair { Kind::shape, "SHAPE" },
                                       std::pair { Kind::pitch, "" }, std::pair { Kind::step, "" } })
        for (const auto& tab : tabs)
            if (tab.kind == group && tab.index == 0)
            {
                const auto b = tab.button->getBounds();
                g.drawText (name, juce::Rectangle<int> (b.getX() - 52, b.getY(), 48, b.getHeight()), juce::Justification::centredRight, false);
            }
}

void ModulatorPanel::resized()
{
    // The tabs along the title strip, in three groups.
    auto strip = getTitleArea().reduced (0, 5).withTrimmedTop (2);
    Kind last = Kind::lfo;
    strip.removeFromLeft (52);
    for (auto& tab : tabs)
    {
        if (tab.kind != last)
        {
            strip.removeFromLeft (tab.kind == Kind::step ? 0 : 64);
            last = tab.kind;
        }
        tab.button->setBounds (strip.removeFromLeft (tab.kind == Kind::pitch || tab.kind == Kind::step ? 64 : 32));
        strip.removeFromLeft (4);
    }

    auto area = getContentArea().withTrimmedTop (6);
    auto controls = area.removeFromRight (kind == Kind::step ? 656 : 520);
    area.removeFromRight (12);
    display.setBounds (area);
    editor.setBounds (area);
    stepEditor.setBounds (area);

    const auto h = ParamControl::knobHeight;
    controls.removeFromTop (4);
    if (kind == Kind::lfo)
    {
        SynthPanel::layoutRow (controls.removeFromTop (h), { &wave, &sync, showingSync ? (juce::Component*) &division : &rate, &trigger, &oneShot }, 98);
        controls.removeFromTop (12);
        SynthPanel::layoutRow (controls.removeFromTop (h), { &phase, &offset, &delay, &fadeIn }, 98);
        return;
    }
    if (kind == Kind::envelope)
    {
        SynthPanel::layoutRow (controls.removeFromTop (h), { &envMode, &envTrigger, &envSync, &envSpeed }, 112);
        controls.removeFromTop (12);
        auto row = controls.removeFromTop (h);
        auto startArea = row.removeFromRight (180);
        SynthPanel::layoutRow (row, { &envSteps, &envFlip, &envOffset }, 98);
        startFromLabel.setBounds (startArea.removeFromTop (16));
        startFromBox.setBounds (startArea.removeFromTop (30).reduced (4, 2));
        controls.removeFromTop (10);
        hint.setBounds (controls.reduced (6, 0));
        return;
    }
    if (kind == Kind::step)
    {
        SynthPanel::layoutRow (controls.removeFromTop (h), { &seqOn, &seqTrigger, &seqSpeed, &seqTiming, &seqMode, &seqLength, &seqGate, &seqContour }, 0);
        controls.removeFromTop (6);
        SynthPanel::layoutRow (controls.removeFromTop (h), { &seqDecay, &seqVolMix, &seqWidth, &shuffle, &seqDelay, &seqFadeIn, &seqLowKey, &seqHighKey }, 0);
        controls.removeFromTop (8);
        auto row = controls.removeFromTop (28);
        auto startArea = row.removeFromRight (260);
        startFromLabel.setBounds (startArea.removeFromLeft (80));
        startFromBox.setBounds (startArea.reduced (0, 1));
        hint.setBounds (row.withHeight (juce::jmax (row.getHeight(), controls.getHeight() + row.getHeight())).reduced (4, 0));
        return;
    }
    if (kind == Kind::pitch)
    {
        SynthPanel::layoutRow (controls.removeFromTop (h), { &pitchOn, &pitchMode, &pitchSpeed, &pitchRange, &pitchSteps, &pitchFlip }, 84);
        controls.removeFromTop (6);
        SynthPanel::layoutRow (controls.removeFromTop (h), { &keytrack, &bendLag, &glideCurve, &glideSplit }, 98);
        controls.removeFromTop (8);
        auto row = controls.removeFromTop (24);
        controls.removeFromTop (6);
        auto startArea = controls.removeFromTop (28).removeFromRight (230);
        pitchRouteLabel.setBounds (row.removeFromLeft (86));
        const auto width = row.getWidth() / (int) pitchRoutes.size();
        for (auto& route : pitchRoutes)
            route->setBounds (row.removeFromLeft (width));
        startFromLabel.setBounds (startArea.removeFromLeft (80));
        startFromBox.setBounds (startArea.reduced (0, 1));
        return;
    }
    auto startArea = controls.removeFromTop (60).withSizeKeepingCentre (200, 60);
    startFromLabel.setBounds (startArea.removeFromTop (16));
    startFromBox.setBounds (startArea.removeFromTop (30).reduced (4, 2));
    controls.removeFromTop (12);
    hint.setBounds (controls.reduced (6, 0));
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
    modWheel.setColour (juce::Slider::thumbColourId, ParagonColours::accent);
    modWheel.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    modWheel.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff221d24));
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

void MacroPanel::paint (juce::Graphics& g)
{
    SynthPanel::paint (g);
    // The wheel's groove (the bar draws only its lit part).
    const auto groove = modWheel.getBounds().toFloat().expanded (2.0f);
    g.setColour (juce::Colour (0xff050406));
    g.fillRoundedRectangle (groove, 4.0f);
    g.setColour (ParagonColours::panelEdge.brighter (0.2f));
    g.drawRoundedRectangle (groove, 4.0f, 1.0f);
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
        slider->updateText();   // in the formats just set
        addAndMakeVisible (*slider);
    }
    amount.getProperties().set (ParagonLookAndFeel::bipolarBarProperty, true);
    curve.getProperties().set (ParagonLookAndFeel::bipolarBarProperty, true);
    destination.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
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
ModPage::ModPage (ParagonX1AudioProcessor& p) : modulators (p), macros (p), matrix (p)
{
    addAndMakeVisible (modulators);
    addAndMakeVisible (macros);
    addAndMakeVisible (matrix);
}

void ModPage::resized()
{
    auto area = getLocalBounds();
    modulators.setBounds (area.removeFromTop (330));
    area.removeFromTop (10);
    macros.setBounds (area.removeFromLeft (420));
    area.removeFromLeft (10);
    matrix.setBounds (area);
}
