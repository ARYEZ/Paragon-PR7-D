# Paragon PR7-K

[![Build](https://github.com/ARYEZ/Paragon-PR7-K/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-K/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-K)](https://github.com/ARYEZ/Paragon-PR7-K/releases/latest)

The synthesizer of the Paragon PR7 family: a VST3 instrument, inspired by VPS Avenger (our own design and code), built
to be light on your CPU. Built with [JUCE](https://juce.com).

Paragon PR7-K starts from [Paragon PR7-E](https://github.com/ARYEZ/Paragon-PR7-E) 0.15.0, the utility processor, and
shares the family's look, preset browser and undo. Its siblings are [PR7-G](https://github.com/ARYEZ/Paragon-PR7-G)
(guitar), [PR7-B](https://github.com/ARYEZ/Paragon-PR7-B) (bass), [PR7-V](https://github.com/ARYEZ/Paragon-PR7-V)
(vocal), [PR7-C](https://github.com/ARYEZ/Paragon-PR7-C) and PR7-E. It installs alongside all of them and keeps its own
presets.

## Status

PR7-K is built in stages, each one released:

| Stage | What | Now |
|---|---|---|
| **1** | Core synth: 8 oscillators, filter, envelopes, amp, poly / mono, glide | **Works (0.1.0)** |
| **2** | Unison ("supersaw"), vibrato and the chorder | **Works (0.2.0)** |
| 3 | Wavetables, FFT editing, sync, FM / AM, the shaper | Planned |
| 4 | Modulation: mod matrix, LFOs, mod and pitch envelopes, step sequencer, macros | Planned |
| 5 | Arpeggiator | Planned |
| 6 | Drum kits and drum sequencer | Planned |
| 7 | Effects racks (starting from PR7-E's EQ, compressor, resonance control and limiter, kept for this) | Planned |
| 8 | Factory presets | Planned (a few starting points now) |

## Using it

- **OSCILLATORS:** eight tabs; each tab's LED switches that oscillator on. Each oscillator has:
  - its **WAVE** (saw, pulse with its **WIDTH**, triangle, sine) and **GAIN**;
  - **PITCH**: **TRANSPOSE** (±4 octaves), **FINE** (cents), **RANDOM** (each note slightly out of tune, at random
    or alternately sharp and flat, as on an old analog synth);
  - **START**: a fixed **PHASE** or **RND PHASE**, a tiny **FADE IN** against clicks, **INVERT**;
  - **NOISE** (white to pink to brown, stereo or mono) and a **SUB** (sine, triangle, square or saw, one or two octaves
    down);
  - **VOLUME**, **PAN**, its own **VOICES** limit (1 makes just that oscillator mono), and **TO FILTER**.
  The picture shows one cycle of the selected oscillator, with its sub (grey) and noise.
- **VOICES** (beside WAVE, right of the oscillator tabs): each oscillator's **UNISON** (2 to 7 detuned copies of each
  note: VOICES, MIX, DETUNE, SPREAD, OCTAVES; at MIX 0 it's off and costs nothing), **VIBRATO** (global or independent
  per copy; shape, rate, amount, fade-in) and **CHORDER** (up to 4 extra notes per key, each with its own copies). The
  display shows every copy by stereo position and pitch.
- **FILTER:** low-pass, high-pass, band-pass and band-stop at 6, 12, 18 and 24 dB per octave. **CUTOFF**, **RESONANCE**,
  **DRIVE** (saturation going in), **KEYTRACK**, **ENVELOPE** (its own envelope's amount, up or down), and how much
  softer notes close it (**VELOCITY**), lose resonance (**RES VEL**) or move it less (**ENV VEL**). The display shows
  its response.
- **FILTER ENVELOPE** and **AMP ENVELOPE:** attack, hold, decay, sustain, release, and a curve for each of the three
  moving stages (left: fast then slow, as natural sounds fade).
- **AMP:** **VOLUME**, **VELOCITY**, **PAN**, **PAN KEY** (low notes to one side), **SPREAD** (each note somewhere
  else), and **SPIKE** (an extra click at the start of each note) with its curve and velocity.
- **PLAY:** **POLY**, or **MONO** (last key), **MONO LOW** or **MONO HIGH**; **LEGATO**; **GLIDE** (always or only
  between overlapping notes); the pitch wheel's range; **VOICES** (notes at once); and the preset's own **LEVEL**. It
  shows how many notes are playing.
- **The keyboard** along the bottom plays the synth with the mouse (C1 to B7). Your DAW's MIDI plays it too, with the
  sustain pedal and pitch wheel.
- **Presets:** SAVE in the header; **<** and **>** step through them; a `*` means changed since loading. The
  **PRESETS** tab browses, files (banks A-E), tags and favourites them. **OUTPUT** and **TUNE** (in SETTINGS) stay put
  when you change presets.
- **Undo / redo:** the header arrows, or Ctrl+Z / Ctrl+Shift+Z (Cmd on a Mac).

## Light on CPU

Avenger's own manual explains where its CPU goes: up to 8 oscillators per note, each multiplied by its unison and
chorder voices, long release tails that "still use cpu", oversampled FM and up to 48 effects. PR7-K is built the
other way round:

- every oscillator reads band-limited tables, so it's clean (nothing folds back below 20 kHz) without oversampling;
- an oscillator, sub, noise, drive or filter that's switched off costs nothing, and so does silence;
- a note ends when it can't be heard: when it decays to a sustain of zero, when its release falls below -90 dB, or when
  all its oscillators have been taken by newer notes;
- slow things (the filter's movement, glide, the pitch wheel) are worked out every 16 samples, not every sample.

In the tests, on the build machine (yours will differ): 8 notes of 4 saws each through a driven 24 dB filter use about
2.9% of one core; a 7-copy supersaw chord about 2.6%; and the worst case of 4 oscillators x 3 chord notes x 7 unison
copies x 8 keys (672 copies at once) about 17%. The tests fail if these ever reach 25%, 25% and 60%.

## Download

Get the latest version from the **[Releases](https://github.com/ARYEZ/Paragon-PR7-K/releases/latest)** page: download
the zip for your computer. (Builds of every push also appear under **Actions** → a run → **Artifacts**.)

Unzip it and copy `Paragon PR7-K.vst3` to your VST3 folder:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` |
| Linux | `~/.vst3` |

**macOS:** the build isn't code-signed yet, so macOS will block it. After copying, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Paragon PR7-K.vst3"` once.

Then rescan plugins in your DAW: PR7-K shows up as an instrument.

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

Paragon PR7-K is free software under the [GNU Affero General Public License v3](LICENSE.md),
which is required by JUCE's open-source licence.

It uses:
- [JUCE](https://github.com/juce-framework/JUCE): AGPLv3
- The Inter typeface (in `resources/fonts`) by Rasmus Andersson, under the SIL Open Font License

VPS Avenger is Vengeance Sound's product. PR7-K is only inspired by how it's described and behaves; it isn't
affiliated with or endorsed by Vengeance Sound, and uses none of Avenger's presets, samples or wavetables.
