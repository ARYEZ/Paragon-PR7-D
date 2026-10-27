# Paragon PR7-K: notes for Claude

This repository starts as a copy of **Paragon PR7-E** (the utility processor, repo `ARYEZ/Paragon-PR7-E`) at PR7-E **0.15.0**, full history included, to become **Paragon PR7-K**, the **synthesizer** of the PR7 family: a VST3 instrument, inspired by VPS Avenger (which the owner has, but finds too heavy on CPU). Its siblings: **PR7-G** (guitar, repo `ARYEZ/Paragon-PR7-G`), **PR7-B** (bass, `ARYEZ/Paragon-PR7-B`), **PR7-V** (vocal, `ARYEZ/Paragon-PR7-V`), **PR7-C** (`ARYEZ/Paragon-PR7-C`) and **PR7-E** (utility). Everything below describes how the project is built, tested and released, and how the owner likes to work.

## First: make it its own plugin (done in 0.1.0)

All of this was done in 0.1.0; it's kept as the record of what makes PR7-K its own plugin.

1. **CMakeLists.txt** (`juce_add_plugin`): `PRODUCT_NAME` "Paragon PR7-K", `BUNDLE_ID` `com.paragon.paragonpr7k`, `PLUGIN_CODE` `Pr7k` (never PR7-G's `Px1a`, PR7-B's `Pr7b`, PR7-V's `Pr7v`, PR7-C's `Pr7c` or PR7-E's `Pr7e`), `PLUGIN_MANUFACTURER_CODE Prgn`; an instrument (`IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, `VST3_CATEGORIES Instrument Synth`, no input bus); version restarted at 0.1.0 (the CMake target name stays `ParagonX1`).
2. **Presets folder** (`source/rigs/RigLibrary.cpp`): "Paragon PR7-K" (tested in `tests/RigTests.cpp`). Nothing carries over from PR7-E's folder.
3. **Settings file** (`source/ui/Finish.cpp`): `applicationName` "Paragon PR7-K".
4. **Logo** (`source/ui/Logo.cpp`): "-K", built from the P's parts in the same slanted, speed-cut lettering (one outline: the path fills even-odd).
5. **Text:** About (SETTINGS), README, the preset browser (says presets, not rigs).
6. **Release workflow:** `Paragon PR7-K.vst3`, `ParagonPR7-K-${TAG}-...` zips, "Paragon PR7-K ${TAG}".
7. **CHANGELOG.md** starts at 0.1.0.
8. **Releases re-enabled** in the 0.1.0 commit.

## The plan: a synth inspired by VPS Avenger, built in stages

The owner supplied Avenger's manual (2.3.1, in the owner's private repo `ARYEZ/manuals`: never commit it here, it's Vengeance's). The owner uses all of it (supersaw leads, pads, basses, wavetables, arpeggiator, drum sequencer, effects), so nothing is dropped: the CPU savings must come from structure, not from leaving things out. From the manual, Avenger's load is the product of up to 8 oscillator tabs x (notes + chorder voices) x V-Saw voices, plus long release tails that "still use CPU", oversampled FM and up to 48 effects. Ours: voices that have faded below audibility end, unison/chorder copies are computed together, oscillators read band-limited tables (no oversampling needed), switched-off parts cost nothing. CPU is a test like any other.

Owner's choices: a **synth panel in the PR7 look** (not the floor-unit face); PR7-E's **EQ, COMP, DOMINION and LIMITER DSP are kept** for the effects rack (stage 7), their stomp pages go; **Windows VST3**.

Stages (each a released plugin):
1. **Core synth: done in 0.1.0.** `source/dsp/synth/`: `WaveTables.*` (band-limited saw, triangle, sine at 28 levels a third of an octave apart, up to 512 harmonics, 4096 samples, straight interpolation; harmonics allowed up to `sampleRate - 20 kHz` so anything that folds lands above 20 kHz; pulse = saw minus a saw shifted by WIDTH), `Envelope.h` (AHDSR, curves `bend()`: k = curve x 10, negative fast first; `step(n)` for the filter envelope once per 16 samples), `Filter.h` (TPT state-variable filter; 24 dB = resonant SVF + Butterworth SVF, 18 = SVF + one-pole, 6 = one-pole without resonance; DRIVE = tanh with first-order antiderivative anti-aliasing, in double), `Noise.h`, `SynthSettings.h` (everything in real units; the processor fills it each block), `Engine.*` (voices = notes, each running up to 8 oscillators; pool of 64 + 16 for stolen notes fading over 3 ms; per-oscillator VOICES limit fades the oldest note's copy over 5 ms; mono with a held-note stack; glide constant-time; slow things every 16 samples). Parameters (version hint 1): `osc{n}{On,Wave,Volume,Pan,Transpose,Fine,Random,PhaseRand,Phase,Gain,Width,FadeIn,Invert,Noise,NoiseColour,NoiseWidth,Sub,SubWave,SubOct,SubInvert,Filter,Voices}` for n = 1 to 8, `flt1*` and `amp1*` (numbered: Avenger has four of each; `osc{n}Filter` is a bool now and can become a choice of filters later, its 0/1 reading the same), `playMode legato glide glideMode bendUp bendDown voices`, `patchLevel`; global `outputGain` (default -6 dB) `bypass` `tune`. Never reorder `oscWaveNames`, `subWaveNames`, `subOctaveNames`, `noiseWidthNames`, `playModeNames`, `glideModeNames`, `filterTypeNames`. UI: `ui/SynthPanels.*` (OSCILLATORS with tabs re-pointing one set of `ParamControl`s, FILTER, two envelopes, AMP, PLAY), `ui/SynthDisplays.*` (wave, filter response from the analog prototypes, envelope shape), `ui/SynthControls.*`; an on-screen `MidiKeyboardComponent` (C1-B7). Factory presets: starting points in bank A (`rigs/FactoryRigs.cpp`: 8 in 0.1.0, 3 more in 0.2.0). Tests: `tests/SynthTests.cpp` (pitch, aliasing vs a plain ramp, pulse harmonics, sub, noise colour, envelope timing and curves, filter response vs its display for every type, slopes, resonance, keytrack, velocity, drive aliasing vs plain tanh, voice stealing, mono modes, legato and glide, oscillator voice limit, sustain pedal, pitch wheel, CPU: 8 notes x 4 saws with a driven filter under 25% of a core, about 3.5% here).
2. **Unison, vibrato and chorder: done in 0.2.0** (Avenger's V-Saw and Chorder). Per oscillator, version hint 2: `osc{n}{UniVoices,UniMix,UniDetune,UniSpread,UniOctaves,VibMode,VibWave,VibRate,VibAmount,VibFade,VibCurve}` and `osc{n}Ch{k}{On,Coarse,Fine,Pan,Vol}` for k = 1 to 4 (appended to `ParamIDs::oscSettings`; the processor finds each by `ParamIDs::oscSetting ("name")`, worked out at compile time). Engine: up to 35 copies per oscillator per note (`Copy`, numbered note x 7 + unison copy; copy 0 of the key uses the oscillator's own phase, with the sub and noise); `Engine::unisonPosition` (for 7: +-1/3, +-2/3, +-1), detune = DETUNE squared x 100 cents (our choice), OCTAVES stacks extra copy u at ((u - 1) mod (octaves + 1)) octaves; extra copies start at random phases; vibrato per interval (global: one phase per oscillator; independent: per copy, speed x 0.85-1.15). MIX 0 is bit-exact with stage 1 (tested). `addCopy` works out all positions first, then reads (lets the compiler vectorise); `tables::levelFor` searches a precomputed count list (it ran 28 exp2s per call: half the cost of 672 copies). Never reorder `vibratoModeNames`, `vibratoWaveNames`. UI: the oscillator panel's WAVE / VOICES pages (`OscillatorPanel::Page`), `VoicesDisplay` (copies by pan and pitch). Presets A09-A11. CPU in the tests: supersaw chord (56 copies) about 2.6%, 672 copies about 17% (limits 25% and 60%).
3. **Wavetables**, FFT editing, sync, FM/AM, the shaper (reuse the table engine).
4. **Modulation**: mod matrix, LFOs, mod and pitch envelopes, step sequencer, macros.
5. **Arpeggiator.**
6. **Drum kits and drum sequencer** (our own synthesised drums and/or the user's samples: never Vengeance's).
7. **Effects racks** (inserts, sends, master), starting from PR7-E's EQ, COMP, DOMINION, LIMITER (kept in `source/dsp/eq`, `dsp/effects`, `dsp/dominion`, built and tested directly in `tests/EqTests.cpp` etc. through small stand-ins for PR7-E's processor; they have no parameters yet). Ask the owner for the VPS FX Bundle 1 & 2 manuals: Avenger's manual refers to them for the effects.
8. **Presets**: our own factory presets; Avenger's presets, samples and wavetables can't be used or loaded.

- **Names:** no Vengeance or Avenger names or branding on the plugin; our own names (the owner names them). "Inspired by" in descriptions is fine.

## The project

- An AGPLv3 **JUCE VST3** instrument, a synthesizer in the PR7 family's look, sharing the family's code.
- **Code layout:** the synth in `source/dsp/synth/`, PR7-E's effects (for stage 7) in `source/dsp/effects/`, `dsp/eq/`, `dsp/dominion/`; UI in `source/ui/`, parameters in `source/Parameters.*`, presets ("rigs" in the code, as across the family) in `source/rigs/`, tests in `tests/`.
- **Parameters:**
  - Saved by ID and choices by **index**: only ever *append* choices, never reorder.
  - New parameters get a new version hint number.
  - Migrations for changed defaults live in `setStateInformation` and `Rig::fromXml`.
- **Shared DSP pieces:** `BypassFade` and `TailWatch` (EffectUtils.h), and `Circuit` (Circuit.h: models analog stages from their schematics, part by part).

## Build, test, check

- **Build:** `cmake -B build && cmake --build build -j8`. On a fresh Linux machine, install the packages listed in `.github/workflows/build.yml` first.
- **Tests (Catch2):** `xvfb-run -a build/tests/ParagonX1Tests`. All must pass. New features get tests that check what the feature really does to the sound (levels, frequencies, harmonics), not just that it runs. The synth's test harness (`Player` in `tests/SynthTests.cpp`) uses `synth::Settings`' defaults, so those must match the parameters' defaults.
- **pluginval:** `xvfb-run -a <pluginval> --strictness-level 10 --validate-in-process "build/ParagonX1_artefacts/Release/VST3/Paragon PR7-K.vst3"`. Check its exit code. CI runs it too.
- **Screenshots** for checking the UI, via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with environment variables:
  - `PARAGON_SNAPSHOT` (output path), `PARAGON_SNAPSHOT_WIDTH` (default 1600; 3200 for 2x).
  - `PARAGON_SNAPSHOT_PAGE=presets|settings`, `PARAGON_SNAPSHOT_PRESET=name` (a factory preset), `PARAGON_SNAPSHOT_OSC=n` (oscillator tab n), `PARAGON_SNAPSHOT_OSCPAGE=voices` (its VOICES page), `PARAGON_SNAPSHOT_PARAMS=id=value,...`, `PARAGON_SNAPSHOT_NOTES=1` (a chord held).
  - Menus open in their own window, so the editor snapshot can't show one: `PARAGON_SNAPSHOT=menu.png ... "[snapshot-menu]"` draws the FILTER's type menu as the plugin's look-and-feel does.
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
