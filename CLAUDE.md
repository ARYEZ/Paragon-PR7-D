# Paragon PR7-K: notes for Claude

This repository starts as a copy of **Paragon PR7-E** (the utility processor, repo `ARYEZ/Paragon-PR7-E`) at PR7-E **0.15.0**, full history included, to become **Paragon PR7-K**, the **synthesizer** of the PR7 family: a VST3 instrument, inspired by VPS Avenger (which the owner has, but finds too heavy on CPU). Its siblings: **PR7-G** (guitar, repo `ARYEZ/Paragon-PR7-G`), **PR7-B** (bass, `ARYEZ/Paragon-PR7-B`), **PR7-V** (vocal, `ARYEZ/Paragon-PR7-V`), **PR7-C** (`ARYEZ/Paragon-PR7-C`) and **PR7-E** (utility). Everything below describes how the project is built, tested and released, and how the owner likes to work.

## First: make it its own plugin (do this before anything else)

Until these are done, PR7-K *is* PR7-E as far as a DAW is concerned. The two would replace each other and share rigs and settings.

1. **CMakeLists.txt** (`juce_add_plugin`):
   - `PRODUCT_NAME` becomes "Paragon PR7-K".
   - `BUNDLE_ID` becomes `com.paragon.paragonpr7k`.
   - `PLUGIN_CODE` becomes a new code: four characters with exactly one upper case, e.g. `Pr7k`. Never PR7-G's `Px1a`, PR7-B's `Pr7b`, PR7-V's `Pr7v`, PR7-C's `Pr7c` or PR7-E's `Pr7e`.
   - It is an instrument: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, VST3 category `Instrument Synth`, no audio input bus.
   - Keep `PLUGIN_MANUFACTURER_CODE Prgn` (same maker).
   - Restart the version at `0.1.0` (`project(ParagonX1 VERSION ...)`; the CMake target name can stay).
2. **Rigs folder** (`source/rigs/RigLibrary.cpp`): "Paragon PR7-K". Remove any carry-over from older folders, and update the rename test in `tests/RigTests.cpp` to match.
3. **Settings file** (`source/ui/Finish.cpp`): `applicationName` becomes "Paragon PR7-K".
4. **Logo** (`source/ui/Logo.cpp`): the mark's "-E" becomes "-K", drawn in the same slanted, speed-cut lettering.
5. **Text:** the About text, `README.md` and anything else naming PR7-E say PR7-K.
6. **Release workflow** (`.github/workflows/build.yml`, `.github/release-install.md`):
   - Bundle paths become `Paragon PR7-K.vst3`.
   - Zips become `ParagonPR7-K-${TAG}-...`.
   - The release title becomes "Paragon PR7-K ${TAG}".
7. **CHANGELOG.md:** start fresh at 0.1.0 ("The synthesizer, based on Paragon PR7-E 0.15.0").
8. **Re-enable releases.** The `release` job's `if:` was set to `false && ...` in the seed commit so this repo didn't publish a PR7-E release. Put it back to `github.event_name == 'push'` in the same commit as the version bump.

## The plan: a synth inspired by VPS Avenger, built in stages

The owner supplied Avenger's manual (2.3.1, in the owner's private repo `ARYEZ/manuals`: never commit it here, it's Vengeance's). The owner uses all of it (supersaw leads, pads, basses, wavetables, arpeggiator, drum sequencer, effects), so nothing is dropped: the CPU savings must come from structure, not from leaving things out. From the manual, Avenger's load is the product of up to 8 oscillator tabs x (notes + chorder voices) x V-Saw voices, plus long release tails that "still use CPU", oversampled FM and up to 48 effects. Ours: voices that have faded below audibility end, unison/chorder copies are computed together, oscillators read band-limited tables (no oversampling needed), switched-off parts cost nothing. CPU is a test like any other.

Owner's choices: a **synth panel in the PR7 look** (not the floor-unit face); PR7-E's **EQ, COMP, DOMINION and LIMITER DSP are kept** for the effects rack (stage 7), their stomp pages go; **Windows VST3**.

Stages (each a released plugin):
1. **Core synth**: up to 8 oscillator tabs (band-limited saw, square/pulse, triangle, sine; noise; sub), a filter (LP/HP/BP/BS at 6/12/18/24 dB, drive, keytrack, its own envelope), the amp (attack, hold, decay, sustain, release with curves; velocity; spike), poly up to 64 / mono with priority, glide, pitch bend.
2. **Unison and chorder** (Avenger's V-Saw and chorder).
3. **Wavetables**, FFT editing, sync, FM/AM, the shaper.
4. **Modulation**: mod matrix, LFOs, mod and pitch envelopes, step sequencer, macros.
5. **Arpeggiator.**
6. **Drum kits and drum sequencer** (our own synthesised drums and/or the user's samples: never Vengeance's).
7. **Effects racks** (inserts, sends, master), starting from PR7-E's EQ, COMP, DOMINION, LIMITER. Ask the owner for the VPS FX Bundle 1 & 2 manuals: Avenger's manual refers to them for the effects.
8. **Presets**: our own factory presets; Avenger's presets, samples and wavetables can't be used or loaded.

- **Names:** no Vengeance or Avenger names or branding on the plugin; our own names (the owner names them). "Inspired by" in descriptions is fine.

## The project

- An AGPLv3 **JUCE VST3** plugin, a synthesizer (instrument) sharing the PR7 family's code.
- **Code layout:** DSP in `source/dsp/effects/`, UI in `source/ui/`, parameters in `source/Parameters.*`, rigs in `source/rigs/`, tests in `tests/`.
- **Parameters:**
  - Saved by ID and choices by **index**: only ever *append* choices, never reorder.
  - New parameters get a new version hint number.
  - Migrations for changed defaults live in `setStateInformation` and `Rig::fromXml`.
- **Shared DSP pieces:** `BypassFade` and `TailWatch` (EffectUtils.h), and `Circuit` (Circuit.h: models analog stages from their schematics, part by part).

## Build, test, check

- **Build:** `cmake -B build && cmake --build build -j8`.
- **Tests (Catch2):** `xvfb-run -a build/tests/ParagonX1Tests`. All must pass. New features get tests that check what the feature really does to the sound (levels, frequencies, harmonics), not just that it runs.
- **pluginval:** `xvfb-run -a <pluginval> --strictness-level 10 --validate-in-process "build/ParagonX1_artefacts/Release/VST3/<name>.vst3"`. Check its exit code. CI runs it too.
- **Screenshots** for checking the UI, via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with environment variables:
  - `PARAGON_SNAPSHOT` (output path), `PARAGON_SNAPSHOT_WIDTH` (e.g. 2560 for 2x).
  - `PARAGON_SNAPSHOT_BLOCK=n` (open block n's panel), `PARAGON_SNAPSHOT_PARAMS=id=value,...`.
  - `PARAGON_SNAPSHOT_PAGE=presets|settings|eq`, `PARAGON_SNAPSHOT_FINISH=black|neon`.
  - `PARAGON_SNAPSHOT_BLOCK=0` (the dock's EQ overview), `1` (DOMINION's page), `2` (COMP's page), or `3` (the LIMITER); `PARAGON_SNAPSHOT_TUNER=1` (the tuner on, after a sung phrase: the unit's screen shows the note).
  - `PARAGON_SNAPSHOT_PAGE=comp` (COMP's page) with `PARAGON_SNAPSHOT_COMP=1` (four bands working on pink noise with a loud 60 Hz and 3 kHz, band 3 selected); `PARAGON_SNAPSHOT_COMP_PRESET=name` loads one of its factory presets instead.
  - `PARAGON_SNAPSHOT_PAGE=oracle` (ORACLE's page) with `PARAGON_SNAPSHOT_ORACLE=1` (a sung line heard at 100 BPM from bar 2, one note selected).
  - `PARAGON_SNAPSHOT_PAGE=dominion` (DOMINION's page) with `PARAGON_SNAPSHOT_DOMINION=1` (on, three depth-curve bands, pink noise with resonances at 180 Hz, 2.5 kHz and 6.3 kHz through it); `PARAGON_SNAPSHOT_DOMINION_PRESET=name` loads one of its factory presets instead of the three bands.
  - `PARAGON_SNAPSHOT_EQ=1` (a mix-bus set of EQ bands with noise through the analyser; band 3 is dynamic), `PARAGON_SNAPSHOT_EQ_BAND=n` (select band n), `PARAGON_SNAPSHOT_MATCH=1` (with `PAGE=eq`: EQ Match open, having heard a track and a brighter reference).
  - Menus open in their own window, so the editor snapshot can't show one: `PARAGON_SNAPSHOT=menu.png ... "[snapshot-menu]"` draws a drop-down as the plugin's look-and-feel does (black, white text, the red-pink-orange highlight, as the owner asked in 0.10.1).
  - Render, look, crop and zoom before claiming a visual change is right.

## Releases

- Bump `project(ParagonX1 VERSION x.y.z)` in CMakeLists.txt and add a matching `## x.y.z` section at the top of CHANGELOG.md, then push.
- CI builds Windows, macOS and Linux and publishes a GitHub Release with the three zips, using that changelog section as the notes. It publishes from a push to **any** branch whose version isn't released yet.
- After pushing, check the release published (all three zips) about 15 minutes later, **and that its `target_commitish` is your commit**. If the version was already taken, CI skips silently and the release you'd link is someone else's build.
- **One chat per plugin at a time.** In October 2026 two chats released PR7-G and PR7-V under the same version numbers: one chat's releases were skipped, and features (and a UI fix) went missing from the builds the owner installed. Before choosing a version, check the repo's tags *and* other branches for unreleased work; if another branch is ahead, merge it first so every release contains everything.
- The owner installs from GitHub Releases on **Windows**.

## How the owner likes to work

- **Explain the why**, specifically: what was wrong, what changed, and the trade-offs. Plain language; the owner is a musician, not a DSP engineer.
- **Be honest about limits:**
  - Say when something is our design rather than a copy ("modelled on", "inspired by").
  - Say when a detail is a guess.
  - Say what can't be checked here (audio by ear).
  - Ask for source material, such as manuals, program charts or gear rundowns, rather than invent specifics.
- **No trademarked names or branding** on our models ("Daybreak", "GLITCH", "Obsidian", "Gotham", "AEON"). "Inspired by" in descriptions is fine.
- **Show it:** send a rendered screenshot of any visual change.
- **"merge it"** means: open a PR from the working branch to main and merge it, then reset the working branch to the new main.
- Commit messages explain what and why.
