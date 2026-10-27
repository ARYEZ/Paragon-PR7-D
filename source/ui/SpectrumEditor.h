#pragma once

#include "PluginProcessor.h"

// The FFT editor's drawing area (stage 3d): the selected oscillator's
// harmonics (faint bars: its wave before editing), and the gain drawn over
// them (the bright curve), from -48 dB (gone) to +24 dB. Drag to draw;
// with SHIFT held, a straight line from where the drag began. Three views,
// as Avenger's:
//   FREE      harmonics 1 to 256 on a log scale (each octave the same width).
//   HARMONIC  the first 24 harmonics as wide bars, each named by the interval
//             it makes with the note (root, octave, fifth...), and one bar
//             for all the rest (our layout).
//   BIN       all 256 side by side.
class SpectrumEditor final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
{
public:
    explicit SpectrumEditor (ParagonX1AudioProcessor&);

    enum class View { free, harmonic, bins };
    void setView (View);
    View getView() const noexcept { return view; }
    void setOscillator (int index);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Where a band sits across the plot, and back (for the views and the tests).
    juce::Rectangle<float> getPlotArea() const;
    float xForBand (int band) const;
    int bandAt (float x) const;
    float dbAt (float y) const;
    float yForDb (float db) const;

    // The interval harmonic h makes with the note ("Root", "8va", "5th"...).
    static juce::String intervalName (int harmonic);
    static constexpr int harmonicColumns = 24;   // HARMONIC view: harmonics 1 to 24, then the rest

private:
    void timerCallback() override;
    void refreshPlain();
    void drawTo (juce::Point<float>);

    ParagonX1AudioProcessor& processor;
    View view = View::free;
    int oscillator = 0;
    std::vector<float> plainDb;          // each harmonic of the unedited wave, dB below its loudest
    std::vector<float> lastKey;          // what the plain bars were made from
    juce::Point<float> dragStart, lastPoint;
    synth::SpectrumEdit startEdit;       // the curve when a SHIFT line began
};
