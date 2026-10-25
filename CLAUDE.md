# Paragon PR7-E: notes for Claude

This repository starts as a copy of **Paragon PR7-B** (the bass processor, repo `ARYEZ/Paragon-PR7-B`) at PR7-B **0.11.0**, full history included, to become **Paragon PR7-E**, the **utility** processor of the PR7 family. Its siblings: **PR7-G** (guitar, repo `ARYEZ/Paragon-PR7-G`), **PR7-B** (bass), **PR7-V** (vocal, `ARYEZ/Paragon-PR7-V`) and **PR7-C** (`ARYEZ/Paragon-PR7-C`). Everything below describes how the project is built, tested and released, and how the owner likes to work.

## First: make it its own plugin (done in 0.1.0)

All of this was done in 0.1.0; it's kept as the record of what makes PR7-E its own plugin. (The loader thread named in item 5 went with the NAM amp, so there's no thread to rename.)

1. **CMakeLists.txt** (`juce_add_plugin`):
   - `PRODUCT_NAME` becomes "Paragon PR7-E".
   - `BUNDLE_ID` becomes `com.paragon.paragonpr7e`.
   - `PLUGIN_CODE` becomes a new code: four characters with exactly one upper case, e.g. `Pr7e`. Never PR7-G's `Px1a`, PR7-B's `Pr7b`, PR7-V's `Pr7v` or PR7-C's (check its CMakeLists.txt).
   - Keep `PLUGIN_MANUFACTURER_CODE Prgn` (same maker).
   - Restart the version at `0.1.0` (`project(ParagonX1 VERSION ...)`; the CMake target name can stay).
2. **Rigs folder** (`source/rigs/RigLibrary.cpp`): "Paragon PR7-E". Remove any carry-over from older folders, and update the rename test in `tests/RigTests.cpp` to match.
3. **Settings file** (`source/ui/Finish.cpp`): `applicationName` becomes "Paragon PR7-E", so PR7-E remembers its own finish.
4. **Logo** (`source/ui/Logo.cpp`): the mark's "-B" becomes "-E", drawn in the same slanted, speed-cut lettering.
5. **Text:** the About text and MIDI hint in `source/ui/DockPanels.cpp`, the loader thread name in `PluginProcessor.h`, and `README.md` all say PR7-E.
6. **Release workflow** (`.github/workflows/build.yml`, `.github/release-install.md`):
   - Bundle paths become `Paragon PR7-E.vst3`.
   - Zips become `ParagonPR7-E-${TAG}-...`.
   - The release title becomes "Paragon PR7-E ${TAG}".
7. **CHANGELOG.md:** start fresh at 0.1.0 ("The utility processor, based on Paragon PR7-B 0.11.0").
8. **Re-enable releases.** The `release` job's `if:` was set to `false && ...` in the seed commit so this repo didn't publish a bass release. Put it back to `github.event_name == 'push'` in the same commit as the version bump.

## The plan: seven stomps, each modelled on a specific plugin

The owner's layout is PR7-V's: **three stomps on the top row, four on the bottom**. Done in 0.1.0: the face came from PR7-V, and the bass-specific blocks (NAM amp, cab IR, Obsidian, Gotham, the bass rigs) are gone; one factory rig, Init, remains. AEON and the `Circuit` solver came along from PR7-B and may be useful later. PR7-B's general effects (pitch, comp / gate, filter / pitch, glitch, modulation, delay, reverb, lo-fi) are still in the signal chain, with no stomps: whether they stay is for the owner.

Stomps not built yet are placeholders: a `Footswitch` with no parameter IDs (dark LED, the pad does nothing, the name opens nothing; see `addStomp` in `MainView.cpp`). Give a stomp its parameters only when it's built: once released, IDs are saved forever. Stomp 4 (EQ) switches the graphic EQ until the planned EQ replaces it. Planning order: the tuner first, then one stomp at a time.

- **Top row**
  1. **Tuner.**
  2. **Open: to plan with the owner.** Ideas offered so far: a loudness/analysis stomp (LUFS, spectrum, stereo correlation), a channel utility (gain, polarity, mono/width, mid-side, swap), an A/B reference player.
  3. **Insert launcher.** A plugin can't open the other plugins on the DAW's track: hosts don't allow it. What it *can* do is host plugins itself (JUCE `AudioPluginFormatManager` / `KnownPluginList`, VST3 on Windows): load plugins into slots inside PR7-E and open their editors from the stomp, a mini rack. Confirm with the owner which platform it's for: the owner works in BeatMaker, and on iPad plugins are AUv3, which works differently.
- **Bottom row**
  1. **EQ inspired by FabFilter Pro-Q 4.** Pro-Q's features are well documented; ask the owner for its manual.
  2. **Inspired by oeksound soothe 3** (adaptive resonance suppression). The algorithm is secret: ours is our own design from the manual and how it behaves. Ask for the manual.
  3. **Inspired by Soundtheory Gullfoss** ("maybe", per the owner). Also a secret, adaptive algorithm: our own design. Ask for the manual.
  4. **The output trim / limiter from PR7-C** (`ARYEZ/Paragon-PR7-C`): port that section rather than writing a new one.
- **Names:** none of those products' names or branding on the plugin: our own names (the owner names them, as with Obsidian, Gotham, AEON). "Inspired by" in descriptions is fine.

## The project

- An AGPLv3 **JUCE VST3** plugin, drawn as a floor processor with an expression pedal, sharing the PR7 family's code.
- **Code layout:** DSP in `source/dsp/effects/`, UI in `source/ui/`, parameters in `source/Parameters.*`, rigs in `source/rigs/`, tests in `tests/`.
- **Parameters:**
  - Saved by ID and choices by **index**: only ever *append* choices, never reorder.
  - New parameters get a new version hint number.
  - Migrations for changed defaults live in `setStateInformation` and `Rig::fromXml`.
- **Shared DSP pieces:** `BypassFade` and `TailWatch` (EffectUtils.h), `PitchShifter`, and `Circuit` (Circuit.h: models analog stages from their schematics, part by part).

## Build, test, check

- **Build:** `cmake -B build && cmake --build build -j8`.
- **Tests (Catch2):** `xvfb-run -a build/tests/ParagonX1Tests`. All must pass. New features get tests that check what the feature really does to the sound (levels, frequencies, harmonics), not just that it runs.
- **pluginval:** `xvfb-run -a <pluginval> --strictness-level 10 --validate-in-process "build/ParagonX1_artefacts/Release/VST3/<name>.vst3"`. Check its exit code. CI runs it too.
- **Screenshots** for checking the UI, via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with environment variables:
  - `PARAGON_SNAPSHOT` (output path), `PARAGON_SNAPSHOT_WIDTH` (e.g. 2560 for 2x).
  - `PARAGON_SNAPSHOT_BLOCK=n` (open block n's panel), `PARAGON_SNAPSHOT_PARAMS=id=value,...`.
  - `PARAGON_SNAPSHOT_EXPRESSION`, `PARAGON_SNAPSHOT_PAGE=presets|settings`, `PARAGON_SNAPSHOT_FINISH=black|neon`.
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
