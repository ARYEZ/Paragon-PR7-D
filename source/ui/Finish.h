#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// The unit's finish: Obsidian (a pearl black unit, the default) or Aurora
// (pearl white). On both the keys and knobs are black, in neon rings (pink to
// red to orange): the footswitches' and pads' bezels, the small keys'
// surrounds, the knobs' rings, THE DRUM MACHINE's fader slots. Only the unit
// itself changes (its chassis, the lettering printed on it); the screens, the
// neon and the dark panels round it stay as they are. (0.86.0 and before also
// had Pearl White, with chrome rings: retired in 0.87.0, from PR7-G 0.60.0 /
// PR7-B 0.24.0.)
//
// It's a preference of the person, not part of a project or a rig: kept in a
// small settings file in the user's application data folder, so every
// PR7-D opened afterwards, in any project, uses the same finish. Every open
// window repaints when it changes.
namespace Finish
{
    // Saved by number: only ever add to the end. retiredPearlWhite (0) is
    // only ever read from an old settings file, and opens as Obsidian; the old
    // Pearl Black (1) is Obsidian, the old Pearl White + Neon (2) Aurora.
    enum class Type { retiredPearlWhite, obsidian, aurora };
    // The finishes offered, in their menus' and buttons' order.
    inline constexpr Type choices[] { Type::obsidian, Type::aurora };
    inline juce::String nameOf (Type type) { return type == Type::aurora ? "AURORA" : "OBSIDIAN"; }
    // A saved number as a finish: Aurora if it says so, otherwise (nothing, the retired Pearl White, anything else) Obsidian.
    inline Type fromSaved (int saved) { return saved == (int) Type::aurora ? Type::aurora : Type::obsidian; }

    Type get();
    // 0.38.0: while set, everything drawn is in this finish instead (THE DRUM MACHINE's face is pearl black, as PR7-K's,
    // whatever the rest of the plugin is in): set in a component's paint, cleared in its paintOverChildren, so its
    // children (keys, knobs) draw in it too. Message thread only.
    void paintAs (std::optional<Type>);
    // `remember` saves it to the settings file (off for tests and snapshots).
    void set (Type, bool remember = true);
    // 0.43.0: THE DRUM MACHINE's own finish (its FINISH key; Obsidian at first, as PR7-K's), kept beside the plugin's
    // in the same settings file. Told through changes() too.
    Type getMachine();
    void setMachine (Type, bool remember = true);
    // 0.93.0: the KIT VIEW's holograms shown or hidden (its HOLOGRAMS key; shown at first), kept in the same settings
    // file, for every PR7-D window. Told through changes() too.
    bool getHolograms();
    void setHolograms (bool shown, bool remember = true);
    inline bool isBlack() { return get() != Type::aurora; }              // the chassis colour (Obsidian)

    // Told whenever the finish changes, to repaint.
    juce::ChangeBroadcaster& changes();

    // Colours that follow the finish.
    juce::Colour ink();          // lettering printed on the unit: near black on white, pearl white on black
    juce::Colour inkDim();
    juce::Colour stampEdge();    // the fine edge under stamped lettering: a light catch on white, a shadow on black
    juce::Colour printedRed();   // the red lettering and lines (a touch brighter on black, so it reads as clearly)

    // The satin grain laid over pearl surfaces: a fine speckle with no
    // direction to it (so no brushed lines) and the odd bright mica fleck.
    // On black the flecks show more, as they do in a real black pearl paint.
    juce::Image makeSatinGrain (int width, int height, int seed);

    // The neon of the rings round the keys and knobs and of the fader slots:
    // hot pink at the left through red to orange at the right (the reference swatch's
    // colours, 25% darker), across `left` to `right`. Lighting is drawn over it.
    juce::ColourGradient neonGradient (float left, float right);

    // AURORA's pearl is darkened by this much (the owner: full white was "too
    // bright, almost disorienting"; PR7-B 0.24.3): every pixel of the pearl
    // surfaces, their sheen and highlights included, is drawn at 90% of its old
    // value, so pure white becomes 230/255. The lettering, the badge, the neon,
    // the keys and the screens on it keep their own colours.
    inline constexpr float auroraDimming = 0.10f;

    // Darkens a pearl surface just drawn, inside `area`, by auroraDimming on
    // Aurora (black laid over it: each colour times 1 - auroraDimming); nothing on Obsidian.
    void dimPearl (juce::Graphics&, const juce::Path& area);
    // A colour of Aurora's pearl as drawn (times 1 - auroraDimming); unchanged on Obsidian.
    juce::Colour pearl (juce::Colour);

    // Picks the colour for the current finish.
    inline juce::Colour pick (juce::uint32 onWhite, juce::uint32 onBlack) { return juce::Colour (isBlack() ? onBlack : onWhite); }
}
