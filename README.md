# Paragon PR7-D

[![Build](https://github.com/ARYEZ/Paragon-PR7-D/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-D/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-D)](https://github.com/ARYEZ/Paragon-PR7-D/releases/latest)

The drum instrument of the Paragon PR7 family: a VST3 instrument with a kit of drum synths and your own samples, a
drum sequencer, and effects. Built with [JUCE](https://juce.com).

Paragon PR7-D starts from [Paragon PR7-K](https://github.com/ARYEZ/Paragon-PR7-K) 0.24.0, the family's synthesizer,
whose drum section it takes further as an instrument of its own. It shares the family's look, preset browser and
undo. Its siblings are [PR7-G](https://github.com/ARYEZ/Paragon-PR7-G) (guitar),
[PR7-B](https://github.com/ARYEZ/Paragon-PR7-B) (bass), [PR7-V](https://github.com/ARYEZ/Paragon-PR7-V) (vocal),
[PR7-C](https://github.com/ARYEZ/Paragon-PR7-C), [PR7-E](https://github.com/ARYEZ/Paragon-PR7-E) (utility) and PR7-K
(synth). It installs alongside all of them and keeps its own presets.

## Status

PR7-D is built in stages, each one released:

| Stage | What | Now |
|---|---|---|
| **1** | Its own plugin: PR7-K's kit, drum sequencer, sample editor and effects, without the synth | **Works (0.1.0)** |
| **2** | The bigger kit: 20 drums, with ride, ride bell, second crash, china, splash, half-open hat, tambourine, shaker | **Works (0.2.0)** |
| **3** | Note maps: Root, a groove-library layout, General MIDI, and your own | **Works (0.2.0)** |
| **4** | Multiple outputs: 16 stereo outputs; each drum, insert bus and send return chooses one | **Works (0.3.0)** |
| **5** | The groove player: browse, filter, preview and drag MIDI grooves from your own folders | **Parts 1-4 work (0.7.0)**: folders, list, search, filter columns, preview, drag out, into the sequencer, tap to find, groove parts |
| **6** | The song track: grooves laid out as a song, played with your DAW | **Parts 1-3 work (0.10.0)**: blocks from grooves or MIDI files, move, lengthen, duplicate, mute, song parts, change tempo, PLAY, EXPORT MIDI; editing inside a block (paste or remove drums, quantize, velocity, split, merge, EDIT BLOCK); the SONG CREATOR |

## Using it

- **DRUMS** (the page it opens on):
  - **The kit:** 20 drums: kick, rim, snare, clap, a second snare, three toms, closed, pedal and open hat, cymbal,
    ride, ride bell, second crash, china, splash, half-open hat, tambourine, shaker. Each is one of our own drum synths
    (kick, snare, clap, hats, tom, rim, cowbell, cymbal, ride, bell, tambourine, shaker) or a **SAMPLE** you load (drop
    an audio file on its row).
  - **Which notes play them:** the MAP in SETTINGS. **Root** puts the 20 on the keys from **ROOT** (C2 at first);
    **Groove Library** and **General MIDI** follow those layouts, so grooves made for them play as written; **Custom**
    is your own, note by note.
  - **OUTPUT** sends a drum to one of the plugin's 16 stereo outputs (MAIN, OUT 2 to 16), to mix it on its own
    channel in your DAW (switch the output on there; until then it plays in MAIN). Insert buses and the DELAY and
    REVERB returns have their own OUTPUT. The routing is kept in your project, not in presets.
  - Click a row to choose and hear it; CTRL / SHIFT choose several. **MUTE**, **SOLO**, **CHOKE** groups (the
    closed hat stops the open one).
  - **The chosen drum:** PITCH, DECAY, TONE, SNAP, VOLUME, PAN, GAIN; where it goes (straight to the mix, or an
    insert bus; its DELAY and REVERB sends); MONO / POLY, DELAY (with an anchor for the sequencer), LP, HP; and a
    drawn **ENVELOPE** across its hit. A sample file shows its hits: click one to play just it, or drag S and E.
  - **KITS** and **SEQUENCES** save and load kits and beats (PR7-K's `.pr7kit` and `.pr7seq` files load here too).
- **GROOVES:** your own MIDI grooves. ADD FOLDER... links a folder (and its subfolders); choose a folder on the left
  to list its grooves, with their time signature, bars, tempo, power hand (what the riding hand plays) and intensity.
  Click one to play it through the kit (double-click: once), at your song's tempo or its own, 1/2x to 2x, its velocity
  scaled; drag it onto a DAW track or to a folder. Each folder's FILES' MAP says which drum each note is. Filter
  columns (TYPE, FEEL, TIME, POWER HAND, TEMPO: click to choose, right-click to exclude) narrow the list; TO SEQUENCER
  A-D makes the chosen groove a drum sequencer pattern. TAP TO FIND...: tap a rhythm on six pads (or your controller)
  over a two-bar loop with a metronome, and the closest grooves come first (MATCH); drop a MIDI file on the page, or
  right-click a groove (FIND SIMILAR), to search with theirs. GROOVE PARTS: tick the parts of the chosen groove (kick,
  snare, hi-hat...) to hear, drag or LAYER ONTO a pattern just those, over the pattern's other drums.
- **SONG** (beside the on-screen keyboard: KEYS | SONG): grooves as blocks in a song. Drag a groove from the list (or a
  MIDI file) onto it, or right-click a groove: ADD TO SONG. Drag blocks to move them, their right edge to repeat the
  groove; right-click for DUPLICATE, REMOVE, MUTE, SONG PART, CHANGE TEMPO, COPY / PASTE (whole blocks, or some drums
  onto a block), REMOVE DRUMS, QUANTIZE, VELOCITY, SPLIT HERE and MERGE WITH NEXT. EDIT BLOCK... (or double-click a
  block): its notes drum by drum, to click in, drag louder or softer, remove, quantize, copy, paste or move to another
  drum. A groove's right-click COPY FOR SONG copies it (or its ticked parts) to paste onto blocks. CREATOR... (or a
  groove's USE WITH SONG CREATOR): the grooves of its song folder in columns by song part, and SONG STRUCTURES (ours,
  and yours saved from the song track): MAKE SONG builds a whole drum track from them. It plays with your DAW (bar 1 is the
  DAW's bar 1), or with PLAY when the DAW is stopped. EXPORT MIDI: the whole song as one file. Saved with the project.
- **SEQUENCER** (beside DRUM): four patterns, **A** to **D**, drawn on a grid level with the kit's rows. Each note
  has its own velocity, pan, pitch, gate, roll and reverse. **SONG** plays with your DAW; **KEY** plays a pattern while
  one of the four keys under the kit is held (with the Root map). **EXPORT MIDI**: click to save, or drag onto a track.
- **FX:** **INSERTS** (buses A to D: DRIVE, GLITCH, MOD, LO-FI, on the drums you route to them), **SENDS** (DELAY and
  REVERB, fed by each drum's sends), **MOD / LO-FI** on the whole kit, and **MASTER** (ECHELON EQ, DOMINION,
  CONSTRICTOR, the LIMITER).
- **The keyboard** along the bottom plays the kit with the mouse; the keys that play a drum are tinted. Your DAW's MIDI
  plays it too. Notes the map doesn't use do nothing.
- **Presets:** SAVE in the header; **<** and **>** step through them; a `*` means changed since loading. The
  **PRESETS** tab browses, files (banks A-E), tags and favourites them. **OUTPUT** stays put when you change presets.
- **MIDI learn:** right-click a knob. The links are listed in SETTINGS.
- **Undo / redo:** the header arrows, or Ctrl+Z / Ctrl+Shift+Z (Cmd on a Mac).

## Download

Get the latest version from the **[Releases](https://github.com/ARYEZ/Paragon-PR7-D/releases/latest)** page: download
the zip for your computer. (Builds of every push also appear under **Actions** → a run → **Artifacts**.)

Unzip it and copy `Paragon PR7-D.vst3` to your VST3 folder:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` |
| Linux | `~/.vst3` |

**macOS:** the build isn't code-signed yet, so macOS will block it. After copying, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Paragon PR7-D.vst3"` once.

Then rescan plugins in your DAW: PR7-D shows up as an instrument (a drum instrument, where your DAW sorts them).

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(ParagonX1 VERSION x.y.z)`).
2. Add a `## x.y.z` section to `CHANGELOG.md`.
3. Commit and push.

When a push carries a version that hasn't been released yet, GitHub Actions builds all three
platforms, tags the commit `vx.y.z` and publishes the release with notes from the changelog.

## Building from source

Requirements: CMake 3.24+, a C++20 compiler (Visual Studio 2022, Xcode 15+, or GCC 11+/Clang 14+), and Git.
JUCE and Catch2 are downloaded automatically at pinned versions.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The plugin is written to `build/ParagonX1_artefacts/Release/VST3/` (the build target keeps its family name).

On Linux, install JUCE's dependencies first (the list is in `.github/workflows/build.yml`).

To render the editor to an image without a DAW:

```bash
PARAGON_SNAPSHOT=editor.png ./build/tests/ParagonX1Tests "[snapshot]"
```

## License

Paragon PR7-D is free software under the [GNU Affero General Public License v3](LICENSE.md),
which is required by JUCE's open-source licence.

It uses:
- [JUCE](https://github.com/juce-framework/JUCE): AGPLv3
- The Inter typeface (in `resources/fonts`) by Rasmus Andersson, under the SIL Open Font License

PR7-D's drums, sequencer and effects are our own designs and code. Its groove player is inspired by the groove
browsers of commercial drum software, and reads only your own MIDI files from your computer: no third-party grooves,
sounds or tags ship with it.
