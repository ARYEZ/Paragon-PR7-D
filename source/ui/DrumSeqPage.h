#pragma once

#include "PluginProcessor.h"
#include "ui/ArpPage.h"
#include "ui/SynthControls.h"

// THE DRUM SEQUENCER's view on the DRUMS page (stage 6b): the beat as a grid whose 12 rows line up with the kit's
// rows on the left (so each row's drum reads off the kit), its pattern A-D, the sequencer's settings, the chosen
// notes' own settings, and MIDI export.
//
// The grid: PEN (click an empty cell to add a note, drag along to add more; click a note to choose it; drag notes to
// move them, ALT for off the grid; right-click removes). SELECT: drag a frame round notes. SHIFT adds to the choice
// either way; DELETE removes the chosen notes, CTRL+A chooses all. The line across it is where the beat is playing.
class DrumSeqGrid final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    explicit DrumSeqGrid (ParagonX1AudioProcessor&);
    enum class Tool { pen, select };
    Tool tool = Tool::pen;
    std::function<void (int row)> onRowTouched;   // a note added or picked: its drum chosen on the left
    std::function<void()> onSelectionChanged;

    int getPattern() const;            // the pattern shown (PATTERN)
    double getLengthSteps() const;
    const synth::drumseq::Pattern& getShown() const noexcept { return shown; }
    void setShown (const synth::drumseq::Pattern&, bool finished);
    std::vector<int> getSelection() const;
    void setSelection (const std::vector<int>&);
    void selectAll();
    void deleteSelected();
    void setHighlightedRow (int row);
    void refreshShown();   // the pattern again from the preset (after PATTERN changes, undo...)
    juce::Rectangle<float> rowArea (int row) const;
    juce::Rectangle<float> cell (int row, double step) const;   // where a note there is drawn
    float playheadX() const;                                     // where the playing line is (-1: none)

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    int noteAt (juce::Point<float>) const;
    double stepAt (float x, bool snap) const;
    int rowAt (float y) const;
    int noteIn (int row, double step) const;   // a note starting in that step of that row (-1: none)

    ParagonX1AudioProcessor& processor;
    synth::drumseq::Pattern shown;
    int shownPattern = -1;
    std::vector<bool> selected;
    enum class Drag { none, move, paint, frame };
    Drag drag = Drag::none;
    std::vector<synth::drumseq::Note> dragOriginals;
    juce::Point<float> downAt;
    juce::Rectangle<float> frame;
    bool changed = false;
    int paintRow = 0, highlightedRow = -1;
    float lastPlayhead = -2.0f;
};

// A chosen note's setting: drag up / down (SHIFT finer), double-click for its default; a choice opens a menu.
class NoteField final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    NoteField (juce::String label, float min, float max, float step, float defaultValue, std::function<juce::String (float)> format);
    juce::StringArray choices;                       // not empty: a menu of these (the value is the index)
    std::function<float()> get;                      // NaN: no note chosen
    std::function<void (float, bool finished)> set;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::String label;
    float min, max, step, defaultValue;
    std::function<juce::String (float)> format;
    float dragStart = 0.0f, dragValue = 0.0f;
    bool dragging = false;
};

class DrumSeqPanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit DrumSeqPanel (ParagonX1AudioProcessor&);
    DrumSeqGrid& getGrid() noexcept { return grid; }
    juce::TextButton& getPatternButton (int pattern) { return *patternButtons[(size_t) pattern]; }
    NoteField& getField (int index) { return *fields[(size_t) index]; }   // VELOCITY, PAN, PITCH, GATE, ROLL, REVERSE
    // Where the grid goes (this panel's space), lined up with the kit's rows; the rest is laid out above it.
    void setGridArea (juce::Rectangle<int>);
    void selectPattern (int pattern);

    void paint (juce::Graphics&) override;
    void resized() override;

    static void style (juce::TextButton&);

private:
    void timerCallback() override;

    ParagonX1AudioProcessor& processor;
    DrumSeqGrid grid;
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<std::unique_ptr<juce::TextButton>> patternButtons;
    juce::TextButton pen { "PEN" }, frameTool { "SELECT" };
    MidiExportButton exportButton;
    std::vector<std::unique_ptr<NoteField>> fields;
    juce::Rectangle<int> gridArea, notesArea;
};
