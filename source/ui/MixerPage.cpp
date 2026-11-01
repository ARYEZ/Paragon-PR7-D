#include "ui/MixerPage.h"

#include "dsp/synth/Instrument.h"
#include "ui/DockPanels.h"
#include "ui/DrumSeqPage.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    constexpr int propertiesWidth = 360, rowHeight = 26;
    using Attachment = juce::AudioProcessorValueTreeState;

    void styleToggle (juce::TextButton& b, const juce::String& tip)
    {
        DrumSeqPanel::style (b);
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
    }
}

//==============================================================================
// A channel's strip.
class MixerPage::Strip final : public juce::Component
{
public:
    Strip (MixerPage& p, int c) : page (p), channel (c)
    {
        auto& state = page.processor.getParameters();
        const auto id = [c] (const char* what) { return ParamIDs::mix (c, what); };
        styleToggle (bleedOn, "BLEED on: the other drums heard through this mic. Off: only the drums it's meant for.");
        bleedOn.setButtonText ("BLEED");
        bleed.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        bleed.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        bleed.setTooltip ("BLEED level: every other drum in this mic, together (their balance is in PROPERTIES).");
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setTooltip ("PAN: left to right (a balance).");
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        fader.setTooltip ("The channel's level.");
        styleToggle (mute, "MUTE this channel.");
        mute.setButtonText ("M");
        styleToggle (solo, "SOLO: only the soloed channels heard.");
        solo.setButtonText ("S");
        styleToggle (invert, "POLARITY: the channel's waveform flipped (to line a mic up with another).");
        invert.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        output.addItemList (ParamIDs::outputNames, 1);
        output.setTooltip ("Where the channel goes: MAIN, or OUT 2 to 16 (switch that output on in your DAW; until then MAIN). "
                           "Kept with the project, not presets.");
        for (juce::Component* comp : { (juce::Component*) &bleedOn, (juce::Component*) &bleed, (juce::Component*) &pan,
                                       (juce::Component*) &fader, (juce::Component*) &mute, (juce::Component*) &solo,
                                       (juce::Component*) &invert, (juce::Component*) &output })
            addAndMakeVisible (comp);
        attachments.push_back (std::make_unique<Attachment::SliderAttachment> (state, id ("Bleed"), bleed));
        attachments.push_back (std::make_unique<Attachment::SliderAttachment> (state, id ("Pan"), pan));
        attachments.push_back (std::make_unique<Attachment::SliderAttachment> (state, id ("Level"), fader));
        buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("BleedOn"), bleedOn));
        buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("Mute"), mute));
        buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("Solo"), solo));
        buttons.push_back (std::make_unique<Attachment::ButtonAttachment> (state, id ("Phase"), invert));
        outputAttachment = std::make_unique<Attachment::ComboBoxAttachment> (state, id ("Output"), output);
        fader.setDoubleClickReturnValue (true, 0.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        bleed.setDoubleClickReturnValue (true, 0.0);
    }

    void setMeter (float peak)
    {
        // Falls back over about a third of a second.
        const auto shown = juce::jmax (peak, meter * 0.8f);
        if (std::abs (shown - meter) > 1.0e-4f)
        {
            meter = shown;
            repaint (meterArea());
        }
    }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void setInUse (bool u) { if (u != used) { used = u; setAlpha (used ? 1.0f : 0.45f); } }

    juce::Rectangle<int> meterArea() const { return fader.getBounds().withWidth (6).translated (fader.getWidth() - 4, 0).withTrimmedBottom (20); }

    void resized() override
    {
        auto a = getLocalBounds().reduced (4, 6);
        auto top = a.removeFromTop (22);
        bleedOn.setBounds (top);
        a.removeFromTop (2);
        bleed.setBounds (a.removeFromTop (40).withSizeKeepingCentre (40, 40));
        a.removeFromTop (6);
        pan.setBounds (a.removeFromTop (40).withSizeKeepingCentre (40, 40));
        a.removeFromTop (4);
        auto name = a.removeFromBottom (24);
        juce::ignoreUnused (name);
        output.setBounds (a.removeFromBottom (22));
        a.removeFromBottom (4);
        auto buttonsRow = a.removeFromBottom (22);
        const auto w = buttonsRow.getWidth() / 3;
        mute.setBounds (buttonsRow.removeFromLeft (w).reduced (1, 0));
        solo.setBounds (buttonsRow.removeFromLeft (w).reduced (1, 0));
        invert.setBounds (buttonsRow.reduced (1, 0));
        a.removeFromBottom (4);
        fader.setBounds (a);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (selected ? ParagonColours::accent.withAlpha (0.12f) : juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
        g.setColour (selected ? ParagonColours::accent : ParagonColours::panelEdge);
        g.drawRoundedRectangle (r.reduced (1.5f), 4.0f, 1.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (selected ? ParagonColours::accent : ParagonColours::text);
        g.drawText (synth::mixer::channelNames[channel], getLocalBounds().removeFromBottom (28), juce::Justification::centred, false);
        // The meter beside the fader.
        const auto m = meterArea().toFloat();
        g.setColour (ParagonColours::panelEdge);
        g.fillRect (m);
        const auto db = juce::Decibels::gainToDecibels (meter, -60.0f);
        const auto h = m.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        g.setColour (db > -1.0f ? ParagonColours::meterRed : ParagonColours::accent);
        g.fillRect (m.withTop (m.getBottom() - h));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y > getHeight() - 30)
            page.selectChannel (channel);
    }

    int getChannel() const noexcept { return channel; }

private:
    MixerPage& page;
    int channel;
    juce::TextButton bleedOn, mute, solo, invert;
    juce::Slider bleed, pan, fader;
    juce::ComboBox output;
    std::vector<std::unique_ptr<Attachment::SliderAttachment>> attachments;
    std::vector<std::unique_ptr<Attachment::ButtonAttachment>> buttons;
    std::unique_ptr<Attachment::ComboBoxAttachment> outputAttachment;
    float meter = 0.0f;
    bool selected = false, used = true;
};

//==============================================================================
// A piece's row in PROPERTIES: on, its name, its level in this channel, polarity.
class MixerPage::PieceRow final : public juce::Component
{
public:
    PieceRow (MixerPage& p, int pc) : page (p), piece (pc)
    {
        styleToggle (on, "On: this drum heard through the channel. Off: not at all.");
        on.setButtonText ("ON");
        styleToggle (invert, "Polarity: this drum's sound in this mic flipped.");
        invert.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98")));
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        level.setRange (-60.0, 12.0, 0.1);
        level.setTextValueSuffix (" dB");
        level.setDoubleClickReturnValue (true, 0.0);
        level.setTooltip ("This drum's level in this channel (-60: off).");
        for (juce::Component* c : { (juce::Component*) &on, (juce::Component*) &invert, (juce::Component*) &level })
            addAndMakeVisible (c);
        const auto commit = [this] (bool final)
        {
            page.setBleed (piece, (float) level.getValue(), invert.getToggleState(), on.getToggleState());
            if (final)
                page.processor.modMatrixEditFinished();
        };
        on.onClick = [commit] { commit (true); };
        invert.onClick = [commit] { commit (true); };
        level.onValueChange = [this, commit] { if (! refreshing) commit (false); };
        level.onDragEnd = [commit] { commit (true); };
    }

    void refresh (int channel, bool direct, bool hasAudio)
    {
        refreshing = true;
        const auto& cell = page.processor.getModMatrix().bleed (channel, piece);
        on.setToggleState (cell.on, juce::dontSendNotification);
        invert.setToggleState (cell.invert, juce::dontSendNotification);
        level.setValue (cell.gainDb, juce::dontSendNotification);
        refreshing = false;
        isDirect = direct;
        audio = hasAudio;
        setAlpha (hasAudio ? 1.0f : 0.5f);
        repaint();
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced (0, 2);
        on.setBounds (a.removeFromLeft (38));
        a.removeFromLeft (110);   // the name
        invert.setBounds (a.removeFromRight (26));
        a.removeFromRight (4);
        level.setBounds (a);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::FontOptions (10.5f, isDirect ? juce::Font::bold : juce::Font::plain));
        g.setColour (isDirect ? ParagonColours::accent : ParagonColours::text);
        g.drawText (synth::drums::pieces[(size_t) piece].name, getLocalBounds().withTrimmedLeft (44).withWidth (106), juce::Justification::centredLeft, true);
    }

    bool isDirect = false;

private:
    MixerPage& page;
    int piece;
    juce::TextButton on, invert;
    juce::Slider level;
    bool refreshing = false, audio = true;
};

//==============================================================================
MixerPage::MixerPage (ParagonX1AudioProcessor& p) : processor (p)
{
    for (int c = 0; c < synth::mixer::numChannels; ++c)
    {
        strips.push_back (std::make_unique<Strip> (*this, c));
        addAndMakeVisible (*strips.back());
    }
    for (int pc = 0; pc < synth::mixer::numPieces; ++pc)
    {
        rows.push_back (std::make_unique<PieceRow> (*this, pc));
        addAndMakeVisible (*rows.back());
    }
    for (auto* s : { &offset, &release })
    {
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        addAndMakeVisible (*s);
    }
    offset.setTooltip ("TIME OFFSET: the channel later, up to 10 ms (to line overheads or room up with the close mics). On a "
                       "close mic it moves the hit off its beat.");
    release.setTooltip ("RELEASE: each hit in this channel faded after this long (OFF: as recorded). Shorter room tails, or a "
                        "gated sound.");
    selectChannel (selected);
    startTimerHz (30);
}

MixerPage::~MixerPage() { stopTimer(); }

juce::Array<int> MixerPage::getChannelsInUse() const
{
    juce::Array<int> channels;
    for (int k = 0; k < synth::drums::numSlots; ++k)
        if (const auto instrument = processor.getDrumInstrument (k); instrument != nullptr && instrument->hasMics())
            for (const auto c : instrument->micChannels())
                channels.addIfNotAlreadyThere (c >= 0 ? c : synth::drums::homeChannel (k));
    channels.sort();
    return channels;
}

void MixerPage::selectChannel (int channel)
{
    selected = juce::jlimit (0, synth::mixer::numChannels - 1, channel);
    for (auto& s : strips)
        s->setSelected (s->getChannel() == selected);
    offsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(), ParamIDs::mix (selected, "Offset"), offset);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(), ParamIDs::mix (selected, "Release"), release);
    refreshProperties();
    resized();
    repaint();
}

void MixerPage::setBleed (int piece, float gainDb, bool invert, bool on)
{
    auto m = processor.getModMatrix();
    m.bleed (selected, juce::jlimit (0, synth::mixer::numPieces - 1, piece)) = { juce::jlimit (-60.0f, 12.0f, gainDb), invert, on };
    processor.setModMatrix (m);
}

void MixerPage::refreshProperties()
{
    // Which pieces have audio in this channel (a loaded kit's mic), and which are its own.
    std::array<bool, synth::mixer::numPieces> has {};
    for (int k = 0; k < synth::drums::numSlots; ++k)
        if (const auto instrument = processor.getDrumInstrument (k); instrument != nullptr && instrument->hasMics())
            for (const auto c : instrument->micChannels())
                if ((c >= 0 ? c : synth::drums::homeChannel (k)) == selected)
                    if (const auto pc = synth::drums::pieceOfSlot (k); pc >= 0)
                        has[(size_t) pc] = true;
    for (int pc = 0; pc < synth::mixer::numPieces; ++pc)
    {
        const auto slot = synth::drums::pieces[(size_t) pc].articulations[0].slot;
        rows[(size_t) pc]->refresh (selected, synth::drums::isDirect (slot, selected), has[(size_t) pc]);
    }
}

void MixerPage::timerCallback()
{
    if (isShowing())
        update();
}

void MixerPage::update()
{
    for (auto& s : strips)
        s->setMeter (processor.takeMixerPeak (s->getChannel()));
    const auto used = getChannelsInUse();
    if (used != inUse)
    {
        inUse = used;
        for (auto& s : strips)
            s->setInUse (inUse.contains (s->getChannel()));
        refreshProperties();
        repaint();
    }
}

void MixerPage::resized()
{
    auto a = getLocalBounds();
    propertiesArea = a.removeFromRight (propertiesWidth);
    a.removeFromRight (12);
    stripsArea = a;
    auto inner = stripsArea.reduced (14, 0).withTrimmedTop (44).withTrimmedBottom (12);
    const auto w = inner.getWidth() / synth::mixer::numChannels;
    for (auto& s : strips)
        s->setBounds (inner.removeFromLeft (w).reduced (2, 0));

    auto p = propertiesArea.reduced (18, 0).withTrimmedTop (44);
    // CLOSE MIC AUDIO first, then BLEED FROM INSTRUMENTS (the order rows are drawn in, by kind).
    std::vector<PieceRow*> direct, bleeding;
    for (auto& r : rows)
        (r->isDirect ? direct : bleeding).push_back (r.get());
    p.removeFromTop (18);
    for (auto* r : direct)
        r->setBounds (p.removeFromTop (rowHeight));
    p.removeFromTop (22);
    for (auto* r : bleeding)
        r->setBounds (p.removeFromTop (rowHeight));
    p.removeFromTop (14);
    offset.setBounds (p.removeFromTop (24).withTrimmedLeft (100));
    p.removeFromTop (6);
    release.setBounds (p.removeFromTop (24).withTrimmedLeft (100));
}

void MixerPage::paint (juce::Graphics& g)
{
    for (const auto& area : { stripsArea, propertiesArea })
        DockStyle::drawPanel (g, area.toFloat());
    const auto title = [&g] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area.toFloat().withWidth (140.0f)));
        g.drawText (text, area, juce::Justification::centredLeft, false);
    };
    title ("MIXER", stripsArea.reduced (18, 0).withHeight (40));
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (ParagonColours::textDim);
    g.drawText (inUse.isEmpty() ? juce::String ("Load a multi-mic kit (DRUMS > LOAD...: an SFZ kit, or a folder with a folder per mic) to use the mixer. "
                                                "Synth drums and single samples go their own way (their INSERT, sends and OUTPUT).")
                                : juce::String ("The mic channels of your multi-mic drums. Click a channel's name for its properties."),
                stripsArea.reduced (18, 0).withHeight (40).withTrimmedLeft (90), juce::Justification::centredLeft, true);

    title ("PROPERTIES: " + synth::mixer::channelNames[selected], propertiesArea.reduced (18, 0).withHeight (40));
    auto p = propertiesArea.reduced (18, 0).withTrimmedTop (44);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (ParagonColours::text);
    int directCount = 0;
    for (auto& r : rows)
        directCount += r->isDirect ? 1 : 0;
    g.drawText (synth::mixer::hearsWholeKit (selected) ? "THE WHOLE KIT (NO BLEED HERE)" : "CLOSE MIC AUDIO", p.removeFromTop (18), juce::Justification::centredLeft, false);
    p.removeFromTop (directCount * rowHeight);
    g.drawText ("BLEED FROM INSTRUMENTS", p.removeFromTop (22).withTrimmedTop (4), juce::Justification::centredLeft, false);
    p.removeFromTop ((synth::mixer::numPieces - directCount) * rowHeight + 14);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("TIME OFFSET", p.removeFromTop (24), juce::Justification::centredLeft, false);
    p.removeFromTop (6);
    g.drawText ("RELEASE", p.removeFromTop (24), juce::Justification::centredLeft, false);
}
