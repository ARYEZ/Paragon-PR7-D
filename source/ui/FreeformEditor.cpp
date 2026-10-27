#include "FreeformEditor.h"

#include "ui/ParagonLookAndFeel.h"
#include "ui/SynthDisplays.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr int points = synth::FreeformDrawing::points;
}

FreeformEditor::FreeformEditor (ParagonX1AudioProcessor& p) : processor (p)
{
    setTooltip ("Drag to draw one cycle of the wave (SHIFT: a straight line). The thin line is what you drew, the bright one what plays. "
                "Sharp corners sound bright and buzzy; SMOOTH rounds them.");
    startTimerHz (15);
}

void FreeformEditor::setOscillator (int index)
{
    oscillator = index;
    repaint();
}

juce::Rectangle<float> FreeformEditor::getPlotArea() const { return getLocalBounds().toFloat().reduced (10.0f, 10.0f); }

int FreeformEditor::pointAt (float x) const
{
    const auto area = getPlotArea();
    return juce::jlimit (0, points - 1, (int) ((x - area.getX()) / area.getWidth() * (float) points));
}

float FreeformEditor::valueAt (float y) const
{
    const auto area = getPlotArea();
    return juce::jlimit (-1.0f, 1.0f, juce::jmap (y, area.getY(), area.getBottom(), 1.0f, -1.0f));
}

void FreeformEditor::timerCallback()
{
    // Undo, a preset or the controls can change the drawing.
    if (! (processor.getDrawing (oscillator) == shown))
        repaint();
}

void FreeformEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    DisplayStyle::drawScreen (g, bounds);
    const auto area = getPlotArea();
    shown = processor.getDrawing (oscillator);

    g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * (float) i / 4.0f), area.getY(), area.getBottom());

    // SPLIT, when B is in use, and the parts EDGE works on.
    if (shown.split < 0.999f)
    {
        const auto x = area.getX() + area.getWidth() * shown.split;
        g.setColour (ParagonColours::text.withAlpha (0.5f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("A", juce::Rectangle<float> (x - 16.0f, area.getY(), 12.0f, 12.0f), juce::Justification::centred, false);
        g.drawText ("B", juce::Rectangle<float> (x + 4.0f, area.getY(), 12.0f, 12.0f), juce::Justification::centred, false);
    }
    if (shown.edge > 0.0f)
    {
        const auto width = area.getWidth() * shown.edge / 4.0f;
        g.setColour (ParagonColours::accent.withAlpha (0.06f));
        g.fillRect (area.withWidth (width));
        g.fillRect (area.withLeft (area.getRight() - width));
    }

    const auto y = [&] (float v) { return area.getCentreY() - juce::jlimit (-1.1f, 1.1f, v) * area.getHeight() * 0.5f; };

    // What's drawn: thin.
    juce::Path drawn;
    for (int i = 0; i < points; ++i)
    {
        const juce::Point<float> point (area.getX() + area.getWidth() * ((float) i + 0.5f) / (float) points, y (shown.values[(size_t) i]));
        if (i == 0) drawn.startNewSubPath (point); else drawn.lineTo (point);
    }
    g.setColour (ParagonColours::textDim.withAlpha (0.8f));
    g.strokePath (drawn, juce::PathStrokeType (1.0f));

    // What plays: the rendered cycle, scaled as NORMALIZE would.
    const auto wave = shown.render();
    auto peak = 0.0f;
    for (const auto v : wave)
        peak = juce::jmax (peak, std::abs (v));
    const auto scale = shown.normalise && peak > 1.0e-6f ? 1.0f / peak : 1.0f;
    juce::Path played;
    const auto count = juce::jmax (2, (int) area.getWidth());
    for (int i = 0; i < count; ++i)
    {
        const auto index = (size_t) juce::jmin ((int) wave.size() - 1, i * (int) wave.size() / count);
        const juce::Point<float> point (area.getX() + (float) i, y (wave[index] * scale));
        if (i == 0) played.startNewSubPath (point); else played.lineTo (point);
    }
    DisplayStyle::drawCurve (g, played, area, area.getCentreY(), true);

    // A hint when it isn't what the oscillator plays.
    if (juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::osc (oscillator, "Wave"))->load()) != (int) synth::OscWave::freeform)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("Draw here: the oscillator switches to FREEFORM", bounds.reduced (12.0f, 8.0f), juce::Justification::topRight, false);
    }
}

void FreeformEditor::drawTo (juce::Point<float> point)
{
    auto drawing = processor.getDrawing (oscillator);
    const auto set = [&] (int from, int to, float a, float b)
    {
        if (from > to)
        {
            std::swap (from, to);
            std::swap (a, b);
        }
        for (int i = from; i <= to; ++i)
            drawing.values[(size_t) i] = to == from ? b : a + (b - a) * (float) (i - from) / (float) (to - from);
    };
    if (juce::ModifierKeys::currentModifiers.isShiftDown())
    {
        drawing.values = startDrawing.values;
        set (pointAt (dragStart.x), pointAt (point.x), valueAt (dragStart.y), valueAt (point.y));
    }
    else
        set (pointAt (lastPoint.x), pointAt (point.x), valueAt (lastPoint.y), valueAt (point.y));
    lastPoint = point;
    processor.setDrawing (oscillator, drawing);
    repaint();
}

void FreeformEditor::mouseDown (const juce::MouseEvent& e)
{
    // Drawing means wanting to hear it: the oscillator plays FREEFORM.
    if (auto* wave = processor.getParameters().getParameter (ParamIDs::osc (oscillator, "Wave")))
        if (juce::roundToInt (wave->convertFrom0to1 (wave->getValue())) != (int) synth::OscWave::freeform)
            wave->setValueNotifyingHost (wave->convertTo0to1 ((float) synth::OscWave::freeform));
    dragStart = lastPoint = e.position;
    startDrawing = processor.getDrawing (oscillator);
    drawTo (e.position);
}

void FreeformEditor::mouseDrag (const juce::MouseEvent& e) { drawTo (e.position); }

void FreeformEditor::mouseUp (const juce::MouseEvent&) { processor.drawingEditFinished(); }

//==============================================================================
FreeformControls::Item::Item (const juce::String& name, juce::Component& c) : control (c)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, ParagonColours::textDim);
    label.setBorderSize ({});
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
    addAndMakeVisible (control);
}

void FreeformControls::Item::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (14));
    area.removeFromTop (2);
    if (dynamic_cast<juce::Slider*> (&control) != nullptr)
        control.setBounds (area);
    else if (dynamic_cast<juce::ToggleButton*> (&control) != nullptr)
        control.setBounds (area.withSizeKeepingCentre (24, 24).withY (area.getY() + 4));
    else
        control.setBounds (area.withSizeKeepingCentre (area.getWidth() - 2, juce::jmin (26, area.getHeight())).withY (area.getY() + 4));
}

FreeformControls::FreeformControls (ParagonX1AudioProcessor& p) : processor (p)
{
    for (auto* box : { &start, &edgeMode })
    {
        box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
        box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box->setColour (juce::ComboBox::textColourId, ParagonColours::accent);
        box->setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
    }
    start.addItemList (synth::FreeformDrawing::startingShapes, 1);
    start.setTextWhenNothingSelected ("New");
    start.setTooltip ("Start the drawing again from a flat line or a basic wave (the controls stay)");
    start.onChange = [this]
    {
        if (refreshing || start.getSelectedId() == 0)
            return;
        const auto shape = synth::FreeformDrawing::startingShape (start.getSelectedId() - 1);
        change ([&shape] (synth::FreeformDrawing& d) { d.values = shape.values; });
        start.setSelectedId (0, juce::dontSendNotification);
    };
    edgeMode.addItemList (synth::freeformEdgeNames, 1);
    edgeMode.setTooltip ("ADAPT moves the cycle's two ends to meet in the middle; ZERO fades them to silence; SYMMETRY blends the wave with its mirror image");
    edgeMode.onChange = [this] { change ([this] (synth::FreeformDrawing& d) { d.edgeMode = (synth::FreeformEdge) (edgeMode.getSelectedId() - 1); }); };

    const std::pair<juce::Slider*, const char*> sliders[] = {
        { &smoothA, "Rounds sharp corners (which sound bright and clicky): before SPLIT, or everywhere if SPLIT is at 100%." },
        { &smoothB, "Rounds sharp corners after SPLIT." },
        { &split, "Where SMOOTH B takes over from SMOOTH A. At 100%, A everywhere." },
        { &edge, "Joins the end of the cycle to its start, so it doesn't jump there (a click on every cycle): how much, by EDGE's mode." } };
    for (const auto& [slider, tip] : sliders)
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        slider->setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setRange (0.0, 1.0);
        slider->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        slider->setTooltip (tip);
    }
    smoothA.onValueChange = [this] { change ([this] (synth::FreeformDrawing& d) { d.smoothA = (float) smoothA.getValue(); }); };
    smoothB.onValueChange = [this] { change ([this] (synth::FreeformDrawing& d) { d.smoothB = (float) smoothB.getValue(); }); };
    split.onValueChange = [this] { change ([this] (synth::FreeformDrawing& d) { d.split = (float) split.getValue(); }); };
    edge.onValueChange = [this] { change ([this] (synth::FreeformDrawing& d) { d.edge = (float) edge.getValue(); }); };
    for (auto* slider : { &smoothA, &smoothB, &split, &edge })
        slider->onDragEnd = [this] { processor.drawingEditFinished(); };
    normalise.setTooltip ("As loud as it can be without going over full scale (off: as loud as drawn)");
    normalise.onClick = [this] { change ([this] (synth::FreeformDrawing& d) { d.normalise = normalise.getToggleState(); }); processor.drawingEditFinished(); };
    exportButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff0d0a0e));
    exportButton.setColour (juce::TextButton::textColourOffId, ParagonColours::textDim);
    exportButton.setTooltip ("Save the cycle as it plays as a WAV file (2048 samples), to load anywhere as a wavetable");
    exportButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save the cycle as a WAV file", juce::File(), "*.wav");
        const auto drawing = processor.getDrawing (oscillator);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [drawing] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  file = file.withFileExtension ("wav");
                                  auto wave = drawing.render();
                                  auto peak = 0.0f;
                                  for (const auto v : wave)
                                      peak = juce::jmax (peak, std::abs (v));
                                  const auto scale = drawing.normalise && peak > 1.0e-6f ? 1.0f / peak : 1.0f;
                                  juce::AudioBuffer<float> buffer (1, (int) wave.size());
                                  for (int i = 0; i < (int) wave.size(); ++i)
                                      buffer.setSample (0, i, wave[(size_t) i] * scale);
                                  file.deleteFile();
                                  std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
                                  if (stream == nullptr)
                                      return;
                                  juce::WavAudioFormat wav;
                                  if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0)
                                                                                                                 .withNumChannels (1)
                                                                                                                 .withBitsPerSample (24)))
                                      writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                              });
    };

    const std::pair<const char*, juce::Component*> layout[] = { { "START", &start }, { "SMOOTH A", &smoothA }, { "SMOOTH B", &smoothB },
                                                                { "SPLIT", &split }, { "NORMALIZE", &normalise }, { "EDGE", &edge },
                                                                { "EDGE MODE", &edgeMode }, { "WAV", &exportButton } };
    for (const auto& [name, control] : layout)
    {
        items.push_back (std::make_unique<Item> (name, *control));
        addAndMakeVisible (*items.back());
    }
    refresh();
    startTimerHz (10);
}

void FreeformControls::setOscillator (int index)
{
    oscillator = index;
    refresh();
}

void FreeformControls::change (const std::function<void (synth::FreeformDrawing&)>& edit)
{
    if (refreshing)
        return;
    auto drawing = processor.getDrawing (oscillator);
    edit (drawing);
    processor.setDrawing (oscillator, drawing);
}

void FreeformControls::refresh()
{
    // Show the drawing's settings (after undo, a preset, another oscillator).
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    const auto& d = processor.getDrawing (oscillator);
    smoothA.setValue (d.smoothA, juce::dontSendNotification);
    smoothB.setValue (d.smoothB, juce::dontSendNotification);
    split.setValue (d.split, juce::dontSendNotification);
    edge.setValue (d.edge, juce::dontSendNotification);
    normalise.setToggleState (d.normalise, juce::dontSendNotification);
    edgeMode.setSelectedId ((int) d.edgeMode + 1, juce::dontSendNotification);
}

void FreeformControls::timerCallback()
{
    if (! isMouseButtonDownAnywhere())
        refresh();
}

void FreeformControls::resized()
{
    auto area = getLocalBounds();
    const auto width = area.getWidth() / (int) items.size();
    for (auto& item : items)
        item->setBounds (area.removeFromLeft (width).reduced (2, 0));
}
