# Changelog

Each version's notes appear on its GitHub Release page.

## 0.1.0

**The first Paragon PR7-D: the PR7 family's drum instrument.** It starts from PR7-K 0.24.0's drum section (the kit, the drum sequencer, the sample editor, kits and sequences) and its effects, as an instrument of its own. PR7-K keeps its drum tab as it is. PR7-D installs beside PR7-K and the other PR7 plugins, with its own name, plugin ID and presets folder.

- **The synth is gone.** No oscillators, filter, envelopes, modulation, arpeggiator or step sequencer, and none of their pages, settings or presets. What's left is all drums.
- **DRUMS is the main page.** The window opens on it. The header has four tabs: DRUMS, FX, PRESETS, SETTINGS.
- **The keyboard plays the kit.** The 12 keys from ROOT (C2 at first) play the drums, C to B, as on General MIDI kits. There's no DRUMS ON switch any more: the kit always plays.
  - With the sequencer's TRIGGER on KEY, the four keys below the kit start patterns A to D. The on-screen keyboard tints them faintly while KEY is on.
  - Keys outside the kit do nothing (in PR7-K they played the synth).
- **The effects work on the drums.** Everything from PR7-K's FX page is here:
  - **INSERTS:** buses A to D, with DRIVE, GLITCH, MOD and LO-FI. The bus strip shows the 12 drums by name (BD, SD, CH...): click one to route it to the bus shown.
  - **SENDS:** DELAY and REVERB. The send matrix and each effect's own page have a knob for each of the 12 drums.
  - **MOD / LO-FI** on the whole kit, and **MASTER:** ECHELON, DOMINION, CONSTRICTOR, the LIMITER.
  - EQ Match's button to record your own sound is now RECORD KIT.
- **Your PR7-K files load here.** Every setting kept from PR7-K has the same name inside the plugin, so files saved in PR7-K open in PR7-D:
  - kits (.pr7kit) and sequences (.pr7seq): copy them from PR7-K's "Drum Kits" and "Drum Sequences" folders into PR7-D's (SETTINGS shows where PR7-D's presets folder is);
  - CONSTRICTOR's and DOMINION's own presets.
  - PR7-K's presets (.pr7rig) aren't read: PR7-D keeps its own presets folder, "Paragon PR7-D".
- **New factory presets:** our own kits and beats, grown from PR7-K's A40-A42 with their bass taken out. Not tuned by ear yet.
  - **A01 Init:** the drum synths, dry, no beat.
  - **A02 Room Kit:** the snare, clap, toms, hats and cymbal in a small room; the kick dry.
  - **A03 Room Beat:** the room kit with four beats (straight, busy, half time, a fill) that play with your song.
  - **A04 Tight Beat:** the room beat tightened with the sample editor: the clap's slap on the beat, a shorter open hat, a darker kick, hats thinned.
  - **A05 Dirty Beat:** the tight beat with the kick, snare and clap through insert bus A, driven (RABID) and worn (LO-FI cassette), the hats clean.
- **Logo:** "PR7-D", its D built from the P's parts in the same slanted, speed-cut lettering.
- **SETTINGS:** TUNE is gone (it tuned the synth). In its place, THE KEYS says which keys play the kit. MIDI links work as before.
- **Your DAW** sees PR7-D as an instrument in the "Drum" category.
- **Tests:** the effect and plugin tests that played synth notes now play steady test tones loaded as drum samples, so levels and pitches are still measured exactly. New tests check:
  - each of the 12 keys playing its own drum, ROOT moving them, keys outside the kit silent;
  - hits as one-shots (a note-off doesn't stop them);
  - each drum's sends, the Room Kit's reverb on the snare and none on the kick, and Dirty Beat's bus;
  - every factory preset sounding, staying clean, going quiet, and playing its beat with the song only if it has one;
  - the window's four tabs and pages, and the keyboard's tinted keys.
  - Every test also passes with Windows' smaller stack (1 MB), and pluginval passes at strictness 10.

What can't be checked here: how it sounds. The drums are PR7-K 0.24.0's drums, unchanged.

Next, your choice of stage 2 (see the plan): the bigger kit and the note mapping first is my suggestion, since the groove player needs both.
