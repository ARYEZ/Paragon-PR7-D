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

The owner's layout is PR7-V's: **three stomps on the top row, four on the bottom**. Done in 0.1.0: the face came from PR7-V, and the bass-specific blocks (NAM amp, cab IR, Obsidian, Gotham, the bass rigs) are gone. Factory rigs (0.6.0, `source/rigs/FactoryRigs.cpp`, asked for by the owner): EQ starting points by bank, A vocals and voice (Init at A01), B drums, C bass/guitars/keys, D full mix, E mastering. Common mixing moves, our own; dynamic bands for intermittent problems; at most +4.5 dB anywhere and ±2 dB for mastering (tested). Add to them as stomps are built (e.g. the limiter in the mastering rigs). In 0.3.0 the owner had PR7-B's general effects (pitch, comp / gate, filter / pitch, glitch, modulation with AEON, delay, reverb, lo-fi) removed: they were mono and lost the right channel. PR7-E is **stereo throughout**: every new stomp must process both channels (or one copy per channel) and keep left and right apart. The `Circuit` solver stays (tested) and may be useful later.

Stomps not built yet are placeholders: a `Footswitch` with no parameter IDs (dark LED, the pad does nothing, the name opens nothing; see `addStomp` in `MainView.cpp`). Give a stomp its parameters only when it's built: once released, IDs are saved forever. Stomp 4 (EQ) switches the graphic EQ until the planned EQ replaces it. Planning order: one stomp at a time, with the owner.

- **Top row**
  1. **Tuner.** Done in 0.2.0, for **checking the pitch of recorded parts** (the owner's main use), not tuning an instrument: single parts (vocals, bass lines) up to 1,500 Hz, both channels, no mute (it never changes the sound), a 30-second trace and an average in cents (`dsp/Tuner.*`, `ui/TunerPanel.*`). The processor runs it from its timer, so it keeps listening with the window closed. Not built (offered, not asked for): chords or full mixes.
  2. **Open: to plan with the owner.** Ideas offered so far: a loudness/analysis stomp (LUFS, spectrum, stereo correlation), a channel utility (gain, polarity, mono/width, mid-side, swap), an A/B reference player.
  3. **Insert launcher.** A plugin can't open the other plugins on the DAW's track: hosts don't allow it. What it *can* do is host plugins itself (JUCE `AudioPluginFormatManager` / `KnownPluginList`, VST3 on Windows): load plugins into slots inside PR7-E and open their editors from the stomp, a mini rack. Confirm with the owner which platform it's for: the owner works in BeatMaker, and on iPad plugins are AUv3, which works differently.
- **Bottom row**
  1. **EQ inspired by FabFilter Pro-Q 4.** The owner supplied its manual (don't commit it: it's FabFilter's). Agreed: it **replaces the graphic EQ**, opens on a **big EQ page** of its own (over the pedal and dock, like PRESETS), and is built in stages: (1) the core EQ: **done in 0.4.0** (`dsp/eq/EqDesign.*`: shapes, slopes and the matched filters; `dsp/eq/ParametricEq.*`: the processing; `ui/EqDisplay.*`, `ui/EqPage.*`; tests in `tests/EqTests.cpp`). Band parameters are `eqB{n}{Used,On,Shape,Freq,Gain,Q,Slope,Place}` (version hint 30) and `eqB{n}{Dyn,DynAuto,Thresh,Attack,Release}` (31) for n = 1 to 24: never reorder `eq::shapeNames`, `slopeNames` or `placementNames`. (2) **dynamic EQ: done in 0.5.0** (`dsp/eq/Dynamics.h`: the gain curve, knee, AUTO threshold and timing, all our own numbers; the detector is in `ParametricEq`). Owner's choices: auto by default with manual threshold/attack/release; cut and boost (negative and positive range); no sidechain for now (BeatMaker on iPad may not route one). (3) **EQ Match: done in 0.7.0** (`dsp/eq/EqMatch.*`: long-term spectra, the target and the band fitting; `dsp/eq/EqMatchSession.*`: listening from the processor's timer, file loading, saved references in the rigs folder's "Match References" as `.pr7ref`; `ui/EqMatchPanel.*`). Our own analysis and fitting; references from the input, a file or saved, no sidechain. Adds ordinary bands (no new parameters). Not wanted: linear phase, character modes. Skipped: instance list, surround, MIDI learn, spectral dynamics (that's the RESONANCE stomp's job). "Natural Phase" is FabFilter's own, unpublished.
  2. **Inspired by oeksound soothe 3** (adaptive resonance suppression). The algorithm is secret: ours is our own design from the manual and how it behaves. Ask for the manual.
  3. **Inspired by Soundtheory Gullfoss** ("maybe", per the owner). Also a secret, adaptive algorithm: our own design. Ask for the manual.
  4. **The output trim / limiter from PR7-C** (`ARYEZ/Paragon-PR7-C`): **done in 0.8.0**, ported from PR7-C 0.4.1 (`dsp/effects/Limiter.*`, unchanged but for `process()` taking a block; PR7-C's limiter tests ported in `tests/LimiterTests.cpp`). Parameters `limitOn/Threshold/MakeUp/Mode/Mix/Link/Trim` (version hint 32; modes Brick, Soft, Auto by index). Last in the chain, after OUTPUT, RIG LEVEL and the volume pedal; OUTPUT TRIM after it, only while it's on. Dock panel: `EffectPanel` with its gain-reduction meter. The mastering factory rigs end with it at -1 dB, Brick.
- **Names:** none of those products' names or branding on the plugin: our own names (the owner names them, as with Obsidian, Gotham, AEON). "Inspired by" in descriptions is fine.

## The project

- An AGPLv3 **JUCE VST3** plugin, drawn as a floor processor with an expression pedal, sharing the PR7 family's code.
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
  - `PARAGON_SNAPSHOT_EXPRESSION`, `PARAGON_SNAPSHOT_PAGE=presets|settings|eq`, `PARAGON_SNAPSHOT_FINISH=black|neon`.
  - `PARAGON_SNAPSHOT_BLOCK=0` (the dock's EQ overview), `1` (the LIMITER panel) or `2` (the TUNER panel); `PARAGON_SNAPSHOT_TUNER=1` (a sung phrase in the tuner's trace).
  - `PARAGON_SNAPSHOT_EQ=1` (a mix-bus set of EQ bands with noise through the analyser; band 3 is dynamic), `PARAGON_SNAPSHOT_EQ_BAND=n` (select band n), `PARAGON_SNAPSHOT_MATCH=1` (with `PAGE=eq`: EQ Match open, having heard a track and a brighter reference).
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
