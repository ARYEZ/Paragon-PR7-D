#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/synth/SynthSettings.h"
#include "dsp/synth/Wavetable.h"

// The synth page's pictures, each redrawn from the parameters as they change.

// A dark inset screen with a faint grid, shared by the displays.
namespace DisplayStyle
{
    void drawScreen (juce::Graphics&, juce::Rectangle<float> area);
    // A curve in the plugin's red-pink-orange, with a soft glow and a faint fill down to `baseline`.
    void drawCurve (juce::Graphics&, const juce::Path& curve, juce::Rectangle<float> area, float baseline, bool lit = true);
}

// Base for the displays: polls the parameters 30 times a second and repaints when they change.
class ParameterDisplay : public juce::Component,
                         private juce::Timer
{
public:
    explicit ParameterDisplay (juce::AudioProcessorValueTreeState&);

protected:
    float value (const juce::String& id) const;
    // The parameters' values that the picture depends on; a repaint when any changes.
    virtual std::vector<float> snapshot() const = 0;

    juce::AudioProcessorValueTreeState& state;

private:
    void timerCallback() override;
    std::vector<float> last;
};

// One cycle of the selected oscillator: its wave (with PULSE WIDTH, GAIN and
// INVERT), its SUB underneath (dimmer), and a haze if it has NOISE. A
// WAVETABLE shows its frames stacked from front to back, the one INDEX
// points at (blended, as heard) drawn bright at its place in the stack.
class WaveDisplay final : public ParameterDisplay
{
public:
    explicit WaveDisplay (juce::AudioProcessorValueTreeState&);
    void setOscillator (int index);
    void paint (juce::Graphics&) override;

    // Where to find the table an oscillator plays, and its FFT-edited copy (set by the panel).
    std::function<std::shared_ptr<const synth::Wavetable> (int oscillator)> tableFor, editedFor;

private:
    std::vector<float> snapshot() const override;
    void paintWavetable (juce::Graphics&, juce::Rectangle<float> area, bool on) const;
    juce::String id (const char* what) const;
    int oscillator = 0;
};

// The selected oscillator's wave over two cycles, large: as it is (dim) and
// as it comes out (bright) after SHAPE (SKEW, PINCH, FORMANT, CRUSH) and
// SYNC, FM and AM. On the SHAPE and FM pages.
class ShapeDisplay final : public ParameterDisplay
{
public:
    explicit ShapeDisplay (juce::AudioProcessorValueTreeState&);
    void setOscillator (int index);
    void paint (juce::Graphics&) override;

    std::function<std::shared_ptr<const synth::Wavetable> (int oscillator)> tableFor;

private:
    std::vector<float> snapshot() const override;
    int oscillator = 0;
};

// The FILTER's response, worked out from its type, CUTOFF and RESONANCE: how
// much each frequency is cut or boosted, 20 Hz to 20 kHz. Dimmed while it's off.
class FilterDisplay final : public ParameterDisplay
{
public:
    explicit FilterDisplay (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;

    // The response in dB at `hz`, as the filter is set. For tests too.
    static double responseDb (synth::FilterType, double cutoffHz, double resonance, double hz);

private:
    std::vector<float> snapshot() const override;
};

// A SHAPER (stage 3e): its curve at the DRIVE set (left: what comes out for
// each level going in; the faint diagonal is no change), and its two EQ
// bands (right; with EQ SPLIT, band 1 is drawn as the left channel's and
// band 2 the right's).
class ShaperDisplay final : public ParameterDisplay
{
public:
    explicit ShaperDisplay (juce::AudioProcessorValueTreeState&);
    void setShaper (int index);
    void paint (juce::Graphics&) override;

    // An EQ band's response in dB at `hz`. For tests too.
    static double bellDb (double gainDb, double freqHz, double q, double hz);

private:
    std::vector<float> snapshot() const override;
    float shaperValue (const char* what) const;
    int shaper = 0;
};

// An envelope's shape: attack, hold, decay, a stretch of sustain, release,
// with their curves. Time is drawn squeezed (long stages take less room than
// their length) so a 10 ms attack and a 5 s release are both readable.
class EnvelopeDisplay final : public ParameterDisplay
{
public:
    // The envelope's eight parameter IDs, in the order A H D S R, A curve, D curve, R curve.
    EnvelopeDisplay (juce::AudioProcessorValueTreeState&, std::array<juce::String, 8> ids);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> snapshot() const override;
    std::array<juce::String, 8> ids;
};

// The selected oscillator's copies (stage 2): every unison copy of the key
// and of each chord note, placed left to right by where it sits in the stereo
// field and up or down by its pitch against the key; brighter the louder it
// is. Under it, how many copies that is per note.
class VoicesDisplay final : public ParameterDisplay
{
public:
    explicit VoicesDisplay (juce::AudioProcessorValueTreeState&);
    void setOscillator (int index);
    void paint (juce::Graphics&) override;

    struct Copy { float pan, semitones, level; bool isKey; };
    // The copies as the engine plays them, from the oscillator's settings.
    static std::vector<Copy> copiesFor (const synth::OscSettings&);

private:
    std::vector<float> snapshot() const override;
    synth::OscSettings settings() const;
    juce::String id (const char* what) const;
    int oscillator = 0;
};
