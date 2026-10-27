# Changelog

Each version's notes appear on its GitHub Release page.

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
