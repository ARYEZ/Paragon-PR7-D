# Changelog

Each version's notes appear on its GitHub Release page.

## 0.2.0

Stage 2: **unison and the chorder**, modelled on Avenger's V-Saw and Chorder (our own design and code). Each oscillator has a new **VOICES** page: switch between **WAVE** and **VOICES** at the right of the oscillator tabs.

- **UNISON** (the "supersaw"): up to 7 copies of each note.
  - **VOICES** (2 to 7, the note itself included) and **MIX** (the extra copies' level). At MIX 0 it's off and costs nothing: the sound is exactly as before (tested sample for sample).
  - **DETUNE**: how far apart the copies are tuned; the value shows how far the outer copies are from the note, up to a semitone. It's squared, so the low end of the knob gives fine control for subtle thickening. The copies sit evenly either side of the note (for 7: a third, two thirds and all of the detune, up and down).
  - **SPREAD**: how wide across the stereo field, the sharp copies one way and the flat ones the other.
  - **OCTAVES**: stacks the extra copies up to 3 octaves above the note, in turn.
  - Every extra copy starts at a random point in its wave, which keeps them from lining up and sounding hollow. The sub and noise stay with the note itself, not copied.
- **VIBRATO** on every copy: **GLOBAL** (all together) or **INDEP.** (each copy on its own, a little faster or slower, for a wider, livelier sound); **SHAPE** (sine, triangle, square, saw, random), **RATE**, **AMOUNT** (cents), **FADE IN** and its **CURVE**.
- **CHORDER**: up to 4 extra notes for every key, each with **ON**, **COARSE** (±2 octaves), **FINE**, **PAN** and **VOL**. Each chord note gets its own unison copies too, as on Avenger (notes x copies).
- **The copies display**, left of the VOICES page: every copy of the key and of each chord note, placed by where it sits in the stereo field and by its pitch, brighter the louder, and how many copies that makes per key.
- **Three new factory presets**: Supersaw Lead (A09), Unison Strings (A10, independent vibrato) and Fifth Stab (A11, the chorder). Our own, not tuned by ear.
- **Faster, also for stage 1's sounds:** each copy now works out its table and samples in a way the computer can do several at a time. In the tests: a 7-copy supersaw chord (8 keys, 56 copies) takes about 2.6% of one core; the worst case of 4 oscillators x 3 chord notes x 7 copies x 8 keys (672 copies) about 17%; stage 1's 8-notes-of-4-saws test went from about 3.5% to 2.9%. Your computer will differ; the tests fail if these ever reach 25% and 60%.
- Our choices, where Avenger's manual gives no numbers: the detune range and curve, what OCTAVES does exactly, the vibrato shapes and how much independent copies differ in speed (up to 15% either way).
- In the tests: copies at the pitches and levels DETUNE and MIX say, SPREAD placing them, OCTAVES stacking them, each chord note at its interval, level and pan with its own copies, vibrato's depth, rate and fade-in, global copies moving together and independent ones apart, and the CPU figures above. Not checked here: how it sounds (I can't listen).

## 0.1.0

The synthesizer of the PR7 family, based on Paragon PR7-E 0.15.0. Stage 1 of the plan: the core synth. Inspired by VPS Avenger (our own design and code: none of Avenger's presets, samples or wavetables), and built to be light on CPU.

- **An instrument:** PR7-K loads as a VST3 instrument (MIDI in, stereo out), installs beside the other PR7 plugins and keeps its own presets (in a "Paragon PR7-K" folder) and settings.
- **8 oscillators per note**, each on its own tab with an on/off LED, as on Avenger:
  - **Waves:** saw, pulse (with WIDTH), triangle and sine, all band-limited: nothing folds back below 20 kHz, on any note, without oversampling (measured: more than 110 dB down on a high saw, where a plain one is 21 dB down).
  - **Pitch:** TRANSPOSE (±4 octaves), FINE, and RANDOM (each note a little out of tune, at random or alternately sharp and flat).
  - **Start:** a fixed PHASE (the same attack every time) or a random one, a tiny FADE IN against clicks, and INVERT.
  - **NOISE** (white, pink, brown or between; stereo, half or mono) and a **SUB** (sine, triangle, square or saw, one or two octaves down, or at the note).
  - VOLUME, PAN, its own VOICES limit (1 keeps just that oscillator to one note, e.g. a bass under chords) and TO FILTER.
- **FILTER:** low-pass, high-pass, band-pass and band-stop at 6, 12, 18 and 24 dB per octave (a zero-delay state-variable design that stays clean when swept fast; our own, not a copy of any of Avenger's filter types). CUTOFF, RESONANCE (none on the 6 dB types), DRIVE (smooth saturation going in, with anti-aliasing: about 10 dB less harshness than a plain clipper on high notes), KEYTRACK, its envelope's amount (up or down), and velocity for the cutoff, the resonance and the envelope. Its display shows the response, and the tests check the sound matches it (within 0.75 dB).
- **Envelopes** for the filter and the amp: attack, hold, decay, sustain, release, with a curve for each moving stage. A retriggered note starts from where it was, never from silence, so it doesn't click.
- **AMP:** volume, velocity, pan, pan keytrack, spread (alternating or random per note) and SPIKE (an extra click of level for the first 20 ms).
- **Playing:** poly up to 64 notes (VOICES sets how many; over it, the quietest fading note makes way, with a 3 ms fade), or mono (last, lowest or highest key) with legato; glide (always, or only between overlapping notes); pitch wheel range up and down; the sustain pedal.
- **Light on CPU:** a switched-off oscillator, sub, noise, drive or filter costs nothing, and so does silence. A note ends when it decays to zero sustain, when every oscillator in it has been taken by newer notes, or when its release falls below -90 dB (that last one saves only the end of a long, curved release; most of the saving is from the others). In the tests, 8 notes of 4 saws each through a driven 24 dB filter take about 3.5% of one core on the build machine; the tests fail if it ever passes 25%.
- **The page**, in the PR7 look: oscillators, filter, the two envelopes, amp, play, OUTPUT and its meter, and a keyboard you can play with the mouse (C1 to B7). Two-way knobs (pan, envelope amount, curves) light from the centre.
- **Presets:** the family's preset browser, undo and redo. A new PR7-K is the **Init** preset (one saw, no filter). Eight starting points in bank A: Init, Classic Bass, Saw Pluck, Square Lead, Warm Pad, Sub Bass, Noise Sweep and Saw Brass. Our own, from common synth recipes, and not tuned by ear: the full factory set comes later, once there's unison, wavetables, modulation and effects to build them with.
- **OUTPUT** starts at -6 dB, for headroom when chords stack up. **TUNE** (SETTINGS) tunes the whole synth in cents; it and OUTPUT stay put when you change presets.
- PR7-E's EQ, compressor, resonance control (DOMINION) and limiter are kept in the code, still tested, for the effects rack (stage 7). They aren't in the sound yet.
- Not checked here: how it sounds (I can't listen). Everything above is measured in the tests: pitch, levels, harmonics, folded tones, envelope timing, filter response, voice counts and CPU.
