# Paragon PR7-D: notes for Claude

This repository starts as a copy of **Paragon PR7-K** (the synthesizer, repo `ARYEZ/Paragon-PR7-K`) at PR7-K **0.24.0** (branch `claude/stoic-knuth-wx6p7m`, commit `0974467`: no `v0.24.0` tag was ever pushed), full history included, to become **Paragon PR7-D**, the **drum instrument** of the PR7 family: a VST3 instrument that takes PR7-K's drum section (its stage 6) further, inspired by the groove and kit side of Toontrack's Superior Drummer 3 (SD3). PR7-K keeps its drum tab as it is (Avenger's drum tab). Siblings: **PR7-G** (guitar, `ARYEZ/Paragon-PR7-G`), **PR7-B** (bass), **PR7-V** (vocal), **PR7-C**, **PR7-E** (utility), **PR7-K** (synth). Everything below describes how the project is built, tested and released, and how the owner likes to work.

## First: make it its own plugin (done in 0.1.0)

All of this was done in 0.1.0; it's kept as the record of what makes PR7-D its own plugin.

1. **CMakeLists.txt** (`juce_add_plugin`): `PRODUCT_NAME` "Paragon PR7-D", `BUNDLE_ID` `com.paragon.paragonpr7d`, `PLUGIN_CODE` `Pr7d` (never another sibling's: `Px1a`, `Pr7b`, `Pr7v`, `Pr7c`, `Pr7e`, `Pr7k`), `PLUGIN_MANUFACTURER_CODE Prgn`; an instrument (`IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `VST3_CATEGORIES Instrument Drum`, no input bus); version restarted at 0.1.0 (the CMake target name stays `ParagonX1`).
2. **Presets folder** (`source/rigs/RigLibrary.cpp`): "Paragon PR7-D" (tested in `tests/RigTests.cpp`). Nothing carries over from PR7-K's folder.
3. **Settings file** (`source/ui/Finish.cpp`): `applicationName` "Paragon PR7-D".
4. **Logo** (`source/ui/Logo.cpp`): "-D", its D built from the P's parts (the bowl's bevelled corners carried down the whole height, 0.24 stem and side, 0.2 bars, the counter a hole: the path fills even-odd), in the same slanted, speed-cut lettering.
5. **Text:** About (SETTINGS), README, rack faceplates ("PR7-D SERIES").
6. **Release workflow:** `Paragon PR7-D.vst3`, `ParagonPR7-D-${TAG}-...` zips, "Paragon PR7-D ${TAG}". The seed commit switched the release job off (`if: false`) so CI couldn't publish PR7-K's 0.24.0 under PR7-D's name; 0.1.0 switched it back on.
7. **CHANGELOG.md** starts at 0.1.0.
8. **The synth taken out:** `dsp/synth/` keeps only `Drums.*`, `DrumSequencer.h` and what they use (`MultiPoint.h` + `Envelope.h` for the drawn envelopes' `mod::Shape`, `StepSequencer.h` for `seq::` divisions and timings, `Noise.h`); the engine, oscillators, wavetables, FFT, freeform, shaper, filter, modulation, travel, arpeggiator and their pages (`SynthPanels`, `ModPage`, `ArpPage`, `ModSourceStrip`, the editors) are gone, with `ModDestinations` and the synth tests. `SynthDisplays` keeps only `DisplayStyle`; `SynthControls` keeps `ParamControl` / `SynthPanel` and a `ControlHost` with MIDI learn only (no modulation rings or drops). `MultiPointEditor` is drum-envelope only (`Kind::drumEnvelope`). `MidiExportButton` moved into `DrumSeqPage`.
9. **Parameters:** PR7-K's drum, sequencer, rack, send, MOD / LO-FI and insert IDs, ranges, defaults and version hints (1-23) unchanged, so PR7-K's `.pr7kit`, `.pr7seq`, `.pr7comp` and `.pr7dom` files load here; plus `outputGain`, `bypass`, `patchLevel`, `shuffle`. Gone: every synth parameter, `tune`, the `mw*` switches, and `drumsOn` (the kit always plays: `KitSettings::on` is always true). PR7-D's own new parameters start at version hint 24.
10. **Preset data:** `ModMatrix` (the name and its XML "Modulation" / "DrumPattern" / "DrumEnvelope" are PR7-K's) holds only `drumPatterns` and `drumEnvelopes`; `Rig` holds parameters, `drumSamples` and that. `Rig`'s `parameterVersion` 1 = PR7-D 0.1.0.
11. **Processor:** the kit is the instrument (0.3.0 added 15 more stereo outputs: see plan item a). `processBlock` hands the DAW's and the on-screen keyboard's MIDI to `drumKit` (it plays its 12 keys and, on KEY, the 4 pattern keys; it ignores the rest); `processChunk` clears the mix, the sends and the used insert buses (the synth engine used to refill them), renders the kit into them, then the inserts, sends, MOD / LO-FI, the rack, OUTPUT, the LIMITER, BYPASS, as in PR7-K.
12. **UI:** `MainView` pages DRUMS (first), FX, PRESETS, SETTINGS (`Page::drums, fx, presets, settings`; four tabs across PR7-K's tab group, `tabEdges`); `KitKeyboard` tints the kit's keys and, with the sequencer on KEY, the four pattern keys fainter. The FX page's send matrix, send pages and insert strip list the 12 drums (`slotDefaults[k].key`). SETTINGS: THE KEYS (the key map) where TUNE was.
13. **Factory presets** (`rigs/FactoryRigs.cpp`): A01 Init, A02 Room Kit (PR7-K's A40 without its bass), A03 Room Beat (A41's beats), A04 Tight Beat (A42), A05 Dirty Beat (kick, snare, clap on bus A: RABID + LO-FI cassette, no hiss).

## The plan after 0.1.0 (each a release, one stage at a time, with the owner)

**b + c were done in 0.2.0, a in 0.3.0** (the owner's order: a, then d in parts).

- **a) MULTIPLE OUTPUTS: done in 0.3.0.** 16 stereo output buses ("Main", "Out 2".."Out 16"; `makeOutputBuses`), the 15 aux ones *off by default* (hosts switch them on; Cubase would make a channel for each one that starts on). OUTPUT choices (`ParamIDs::outputNames`, hint 25, in `notInRigs` and skipped by kits: routing is the DAW project's): `drum<n>Output`, `ins<n>Output` (a drum on a bus goes where the bus goes), `delayOutput`, `reverbOutput`. The kit writes to `Outputs::auxLeft/auxRight` (null for an output the host has off: the drum plays in MAIN instead). OUT 2-16 get OUTPUT (+ patchLevel) and BYPASS, not MAIN's MOD / LO-FI, rack or LIMITER, and are delayed by `mainLatencySamples()` (`delayAuxOutputs`, rings in `Rack::auxDelay`) to stay sample-aligned with MAIN. Tests: `tests/OutputTests.cpp`. Not checked: how Ableton Live and FL Studio show aux outputs that start off (ask the owner which DAW they use).
- **b) THE BIGGER KIT: done in 0.2.0.** Slots 13-20: RD (ride, model RIDE), RB (ride bell, BELL), CR2, CHN, SPL (CYMBAL tuned apart), HH (half-open hat, OPEN HAT, choke group 1), TMB (TAMBOURINE), SHK (SHAKER); models appended: Ride, Bell, Tambourine, Shaker. Articulations are separate slots (rimshot = SD2, sidestick = RIM, pedal / closed / half / open hat), not layers inside one drum: per-piece articulation levels (e.g. ride edge, hat openness steps) are still open. What follows is the original plan text.
  The original plan (the owner chose more slots over reusing the clap): At least a ride (bow, bell, edge), a second crash, a china or splash; maybe tambourine and shaker. Articulations per piece (hat openness levels, ride bow / bell / edge, snare center / rimshot / sidestick), so Toontrack grooves play as written. Each piece our drum synths or the user's samples (never Toontrack's or Vengeance's). Appending slots means `numSlots`, `slotDefaults`, `ModMatrix::numDrumSlots`, `drumseq::numRows`, the page's rows and the stack test all grow: keep the 12 existing slots' IDs and order.
- **c) NOTE MAPPING: done in 0.2.0** (`dsp/synth/NoteMap.h`, SETTINGS' THE KEYS). Layouts ROOT (as 0.1.0), GROOVE LIBRARY (the owner's list below), GENERAL MIDI, CUSTOM (edited per note; kept in the project and, with SAVE AS DEFAULT, in "Note Map.xml" in the presets folder; never in presets: `drumsMap` is in `notInRigs`). The sequencer's KEY keys work under ROOT only; MIDI EXPORT writes each row on its drum's home note in the current map (`homeNote`). Checked against the owner's list and github.com/insomnimus/drum-mapper's SD3 table (article sites were blocked from the session); differences and guesses: **86 is a crash** there (the owner's list had it under ride), the ride's edge / mute notes all play RD, the half-open hat steps play OH or CH, crash mutes play CY; HH, CR2, CHN, SPL have no note in GROOVE LIBRARY. Still to do: **check against a real groove file** (ask the owner).
  The original plan: Toontrack grooves use the "Toontrack Standard" layout. The owner sent EZdrummer 3's map; assume SD3 packs use it too, but **check against a real groove file** before relying on it (ask the owner for one or two, in `ARYEZ/manuals`). Proposed mapping (editable in the plugin; MIDI note numbers): kick 34-36; snare center 6, 33, 38, 39, 66, 68-70, 125; rimshot 40, 71, 126; sidestick 37, 67, 76, 127; closed hat 7-9, 11, 18-20, 22, 42, 61-65, 119, 122; pedal hat 10, 21, 23, 44; open hat 12-17, 24-26, 46, 60, 120, 121, 123, 124; rack toms 47, 48, 79-82; floor tom 1: 43, 45, 74, 75, 77, 78; floor tom 2: 41, 72, 73; crashes and their mute hits 27, 28, 31, 32, 49, 50, 55-58, 94, 95, 106, 107; ride (bow, bell, edge, mute) 29, 30, 51-54, 59, 83-93, 96-105, 108-118; tambourine 3, shaker 2, one-shot 1. Also a General MIDI map, and the user's own maps. Note that 32-35 are the sequencer's KEY pattern keys under the default ROOT: the mapping has to decide how the two live together.
- **d) THE GROOVE PLAYER**, inspired by SD3's Grooves tab (the owner's SD3 manual extract, pp. 73-90 of Toontrack's online manual: in the owner's uploads, never commit it). Our own design, in parts. In our words:
  - a browser of MIDI grooves from the user's own folders (linked folders, with or without subfolders), by library, style / time signature, song and part (intro, verse, chorus, bridge, fill, ending); each folder also works as a filter;
  - filter columns that combine (genre, play style, type such as beat / fill / ending, time signature, "power hand" = what the riding hand plays: closed hat, open hat, ride, crash...), each can be excluded; text search;
  - preview: click to loop, double-click for once; at the song's tempo or the groove's own; tempo x1/2, x1, x2, x2/3 (4/4 to 6/8) and x3/2; a velocity scale for the whole groove;
  - drag a groove onto a DAW track, to the desktop, or into our song track / drum sequencer;
  - Tap2Find-style search (our own name for it): tap a rhythm on the kit or a MIDI controller over a 2-bar loop with a metronome, quantized (1/8 by default); results sorted by how close they are; dropping a MIDI file searches too;
  - groove parts: take one drum's part (e.g. the hats) from one groove and lay it over another;
  - favourites, sorting by the song's tempo / time signature;
  - later: a song track (grooves arranged into a song). It's in SD3's manual chapter 7, which the owner hasn't sent yet: **ask for it before designing that part.**
  - Tags: SD3's genre / style tags come from Toontrack's own database, which we can't read or ship. Ours come from the files (folder and file names, the MIDI's tempo, time signature, bar count) and the notes (power hand, how busy, how hard).

Seen in 0.1.0, for later (corrected in 0.2.0): a drum voice ends when its level falls below -80 dB (`Voice::finished`, `Drums.cpp`), so a long cymbal at full DECAY keeps a voice running about 11 s (`tests/RigTests.cpp` checks it's quiet, not that its voice ended). Ending hits sooner (e.g. relative to the hit's peak) would save CPU on busy beats; it changes what the drum tests compare against, so do it as its own change, with the owner.

## Rules (from the owner and the family's CLAUDE.md files)

- **Names:** our own designs, "inspired by"; no Toontrack, Superior Drummer, EZdrummer or Vengeance / Avenger names or branding on the plugin. Toontrack's grooves, sounds and tags are never shipped: the user's own MIDI packs are read from their computer when loaded. Never get round copy protection.
- The SD3 manual extract and Avenger's manual stay with the owner (uploads / `ARYEZ/manuals`): never commit them.
- Ask for source material (manual chapters, example files) rather than invent specifics; say when something is a guess or our own design; say what can't be checked here (listening).

## The project

- An AGPLv3 **JUCE VST3** instrument in the PR7 family's look, sharing the family's code.
- **Code layout:** the kit and sequencer in `source/dsp/synth/` (the folder name and `synth::` namespace are PR7-K's), the effects in `source/dsp/effects/`, `dsp/eq/`, `dsp/dominion/`; UI in `source/ui/`, parameters in `source/Parameters.*` and `source/RackParameters.cpp`, presets ("rigs" in the code, as across the family) in `source/rigs/`, tests in `tests/`.
- **Parameters:**
  - Saved by ID and choices by **index**: only ever *append* choices, never reorder (`drums::modelNames`, `chokeNames`, `drumseq::triggerNames`, `reverseNames`, `rollNames`, `seq::divisionNames`, `timingNames`, `ParamIDs::insertBusNames`, and the effects' lists).
  - New parameters get a new version hint number (PR7-D's start at 24).
  - Migrations for changed defaults live in `setStateInformation` and `Rig::fromXml`.
- **Shared DSP pieces:** `BypassFade` and `TailWatch` (EffectUtils.h), and `Circuit` (Circuit.h: models analog stages from their schematics, part by part).
- **The drum code, as PR7-K built it** (its CLAUDE.md's stage 6 has every detail; in short):
  - `dsp/synth/Drums.*`: `drums::Kit`, 20 slots since 0.2.0 (`slotDefaults`: C BD, C# RIM, D SD, D# CLP, E SD2, F LT, F# CH, G MT, G# PH, A HT, A# OH, B CY, then RD RB CR2 CHN SPL HH TMB SHK; the hats, HH too, in CHOKE group 1; drums 13-20's parameters have version hint 24); models KICK SNARE CLAP CLOSED HAT OPEN HAT TOM RIM COWBELL CYMBAL SAMPLE RIDE BELL TAMBOURINE SHAKER; `KitSettings::mapped` / `keys` route notes through the note map (`NoteMap.h`) instead of ROOT; `modelLevel` puts every model's default hit at -3 dBFS; up to 6 hits per slot, 48 voices; CHOKE fades the group over 5 ms; one-shots. `Outputs`: the mix, the sends (only while their effect is on and the drum isn't on a bus), the insert buses; the kit *adds* into them. `sampleFiles::load / encode / decode / findHits`. SAMPLE START / END, the sample editor (GAIN, MONO, DELAY with the anchor, LP / HP, the drawn envelope), reverse (`reverseSource`, the processor's `updateDrumReverses`).
  - `dsp/synth/DrumSequencer.h`: `drumseq::Note` / `Pattern` / `Settings`, `hitTime`, `forEachHit` (the one place hits are worked out), `toMidi`. SONG follows `ppq`; KEY: `Kit::patternForKey` (root-4..root-1).
  - Processor: `getKitSettings`, `getDrumSeqSettings`, samples as shared pointers handed to the audio thread as plain pointers (`samplesForAudio`, retired 2 s), kits and sequences (`captureDrumKit`, `applyDrumKit`, `loadDrumKit`, LINK...), `drumPatternToMidi`, `previewDrum`.
  - UI: `ui/DrumPage.*` (rows, `DrumHitDisplay`, `HitPicture`, the envelope strip, several chosen), `ui/DrumSeqPage.*` (`DrumSeqGrid`, `NoteField`, `DrumSeqPanel`).
- **The effects, as PR7-K ported them** (its CLAUDE.md's stage 7): PR7-E 0.18.1's ECHELON, DOMINION, CONSTRICTOR and LIMITER in one heap `Rack` (`source/Rack.h`); PR7-G 0.52.0's DELAY / REVERB as send returns, MOD, MOD 2, LO-FI, and the insert buses' DRIVE / GLITCH (with PR7-K's speed-ups, marked in the code). When PR7-E or PR7-G changes these effects, port the change.

## Build, test, check

- **Build:** `cmake -B build && cmake --build build -j8`. On a fresh Linux machine, install the packages listed in `.github/workflows/build.yml` first.
- **Tests (Catch2):** `xvfb-run -a build/tests/ParagonX1Tests`. All must pass. Windows gives a thread 1 MB of stack (Linux 8 MB): keep big data (patterns, tables) on the heap, never inline in `ModMatrix`, `Rig` or the kit's settings (`tests/StackSizeTests.cpp`); before a release run the tests once under `ulimit -s 1024` to catch what Linux hides. New features get tests that check what the feature really does to the sound (levels, timing to the sample, frequencies), not just that it runs.
  - Test tones: `tests/PluginTestUtils.h` (`testkit::sine`, `saw`, `useSample`, `keyFor`): a slot set to SAMPLE playing a generated tone comes out sample for sample at PITCH 0, so effect tests measure exactly. `tests/TestUtils.h`: `levelAt`, `rms`, `frequencyOf`, `Spectrum`...
  - Hits in tests go in as MIDI (at a sample position), **not** through `getKeyboardState().noteOn`: JUCE spreads the keyboard's presses across the block by the clock, so two presses can land at different samples on different runs (it made "identical" runs differ in 0.1.0).
- **pluginval:** `xvfb-run -a <pluginval> --strictness-level 10 --validate-in-process --validate "build/ParagonX1_artefacts/Release/VST3/Paragon PR7-D.vst3"`. Check its exit code. CI runs it too. Linux pluginval sometimes crashes in its own host at shutdown (CI retries once); if it does twice, reproduce under gdb before calling it that.
- **Screenshots** for checking the UI, via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with environment variables:
  - `PARAGON_SNAPSHOT` (output path), `PARAGON_SNAPSHOT_WIDTH` (default 1600; 3200 for 2x).
  - DRUMS (the default page, or `PARAGON_SNAPSHOT_PAGE=drums`): `PARAGON_SNAPSHOT_DRUM=n` chooses slot n, `PARAGON_SNAPSHOT_DRUMS=1` plays kick, snare and closed hat first, their rows lit; `=2` a file of nine kicks, the 4th chosen; `PARAGON_SNAPSHOT_DRUMSEQ=1` the SEQUENCER view, a beat playing with the song, the snares chosen; `PARAGON_SNAPSHOT_DRUMEDIT=1` the clap with an envelope, an anchor, GAIN and LP, `=2` with `DRUMS=2` a fade drawn on the chosen kick; `PARAGON_SNAPSHOT_DRUMCHOSEN=1` the hats and cymbal chosen together, the tom's playhead.
  - `PARAGON_SNAPSHOT_PAGE=fx` (the FX page; `PARAGON_SNAPSHOT_RACK=1`: everything at work on "Room Beat" playing with the song, kick and snare on bus A, the sends too; `PARAGON_SNAPSHOT_FX=eq|dominion|comp|limiter|sends|delay|reverb|modfx|mod|mod2|lofi|inserts|insdrive|insglitch|insmod|inslofi`: that view, `PARAGON_SNAPSHOT_BUS=1-4` the inserts' bus; `PARAGON_SNAPSHOT_MATCH=1` with `FX=eq`: EQ Match open).
  - `PARAGON_SNAPSHOT_PAGE=presets|settings`, `PARAGON_SNAPSHOT_PRESET=name` (a factory preset), `PARAGON_SNAPSHOT_PARAMS=id=value,...`, `PARAGON_SNAPSHOT_NOTES=1` (the kick, snare and hat keys held), `PARAGON_SNAPSHOT_MIDI=1` (demo MIDI links, a knob learning; with `PAGE=settings` the links list).
  - Menus open in their own window, so the editor snapshot can't show one: `PARAGON_SNAPSHOT=menu.png ... "[snapshot-menu]"` draws the drum models' menu as the plugin's look-and-feel does.
  - Render, look, crop and zoom before claiming a visual change is right.

## Releases

- Bump `project(ParagonX1 VERSION x.y.z)` in CMakeLists.txt and add a matching `## x.y.z` section at the top of CHANGELOG.md, then push.
- CI builds Windows, macOS and Linux and publishes a GitHub Release with the three zips, using that changelog section as the notes. It publishes from a push to **any** branch whose version isn't released yet.
- After pushing, check the release published (all three zips) about 15-20 minutes later, **and that its `target_commitish` is your commit**. If the version was already taken, CI skips silently and the release you'd link is someone else's build. Send the owner the link.
- **One chat per plugin at a time.** In October 2026 two chats released PR7-G and PR7-V under the same version numbers: one chat's releases were skipped, and features (and a UI fix) went missing from the builds the owner installed. Before choosing a version, check the repo's tags *and* other branches for unreleased work; if another branch is ahead, merge it first so every release contains everything.
- The owner installs from GitHub Releases on **Windows**.

## How the owner likes to work

- **Explain the why**, specifically: what was wrong, what changed, and the trade-offs. Plain language; the owner is a musician, not a DSP engineer. Offer choices with a recommendation.
- **Be honest about limits:**
  - Say when something is our design rather than a copy ("modelled on", "inspired by").
  - Say when a detail is a guess.
  - Say what can't be checked here (audio by ear).
  - Ask for source material, such as manuals, example groove files or chapters, rather than invent specifics.
- **No trademarked names or branding** on our models. "Inspired by" in descriptions is fine.
- **Show it:** send a rendered screenshot of any visual change.
- **"merge it"** means: open a PR from the working branch to main and merge it, then reset the working branch to the new main.
- Commit messages explain what and why.
