# Changelog

Each version's notes appear on its GitHub Release page.

## 0.3.0

**Multiple outputs.** Stage 4 of the plan: each drum (or group of drums) can play out of its own stereo output, so your DAW mixes the kit like a recorded one, each drum on its own channel with its own plugins.

- **16 stereo outputs:** MAIN, then OUT 2 to OUT 16. Your DAW shows OUT 2 to 16 once you switch them on there (they start off, as most DAWs expect of multi-output instruments: Cubase, for one, would otherwise make 15 empty mixer channels the moment you load it).
- **OUTPUT on each drum** (DRUMS page, beside INSERT): MAIN or OUT 2 to 16. Choose several drums first (CTRL / SHIFT) to set them all at once: the three toms to OUT 5, say.
- **An insert bus has its own OUTPUT** (FX, INSERTS, beside its sends): a drum on a bus plays where the bus does, after its effects.
- **DELAY's and REVERB's returns have their own OUTPUT** too (FX, SENDS, at the end of each row): put the room on a channel of its own. The drums still feed DELAY and REVERB with their sends wherever they play.
- **What reaches OUT 2 to 16:** each drum as you've shaped it on the DRUMS page (VOLUME, PAN, the sample editor), its insert bus if it has one, and **OUTPUT** (the big knob: it's the whole plugin's volume, so a drum moved off MAIN keeps its level). Not MAIN's effects (MOD / LO-FI, the MASTER page's ECHELON, DOMINION, CONSTRICTOR) and not the LIMITER: those are MAIN's, and the other outputs are for your DAW to mix. BYPASS silences them all.
- **Nothing goes silent by mistake:** a drum, bus or return set to an output your DAW hasn't switched on plays in MAIN instead, so the kit always sounds whole.
- **In time:** CONSTRICTOR's LOOK-AHEAD and the LIMITER delay MAIN (your DAW makes up for it). OUT 2 to 16 are held back by the same amount, so a kick on OUT 2 and its room on MAIN stay together to the sample.
- **Presets and kits leave the outputs alone:** the routing belongs to your DAW project (it's kept there), so changing preset or loading a kit never moves drums between your mixer's channels.
- **Also:** ROOT's tip now says 19 keys above it (it still said 11).
- **Tests:** a drum on OUT 2 coming out there at its level and nowhere else; an output that's off sending its drum to MAIN, identical to MAIN; a bus's and the reverb's outputs; OUT 2 lined up with MAIN to the sample with CONSTRICTOR's and the LIMITER's look-ahead on (1149 samples of latency); OUTPUT turning every output down together; BYPASS; a mono MAIN with a stereo OUT 2; presets, kits and projects and the routing; the layouts the plugin accepts (MAIN mono or stereo, the others stereo or off).

What can't be checked here: how each Windows DAW shows the extra outputs (no DAW runs here). In Cubase you switch them on with the instrument's "Activate Outputs"; Reaper lists every output's channels. For Ableton Live and FL Studio we found no clear documentation of how they treat outputs that start off. Tell us which DAW you use, and whether OUT 2 to 16 show up: if one doesn't, the outputs can be made to start on.

## 0.2.0

**A bigger kit and note maps.** Stages 2 and 3 of the plan: 20 drums instead of 12, and a choice of which MIDI notes play which drum, so grooves written for other drum software (and General MIDI files) play on the right drums.

- **Eight new drums**, on the keys after the cymbal (C3 to G3 under ROOT C2):
  - **RD**, a ride (new RIDE model: a long, washy metal with a ping you set with SNAP);
  - **RB**, the ride's bell (new BELL model: four bell-like partials; TONE brings in the upper ones);
  - **CR2**, a second crash; **CHN**, a china (lower, trashier); **SPL**, a splash (higher, shorter): our CYMBAL model, tuned apart;
  - **HH**, a half-open hat (OPEN HAT, shorter), in the hats' CHOKE group, so the closed and pedal hats stop it;
  - **TMB**, a tambourine (new TAMBOURINE model: jingles in three quick strikes); **SHK**, a shaker (new SHAKER model: a soft swell; SNAP makes it sharper).
  - Like every drum, each can be a SAMPLE of your own instead. The sends, the insert bus strip and the sequencer's grid have all 20.
  - The 12 drums you had are unchanged, on the same keys, so your projects, kits and beats sound as before. Kits and sequences saved with 12 drums load; the new 8 start at their defaults.
- **Note maps** (SETTINGS, THE KEYS): MAP chooses which notes play which drum.
  - **Root:** as before, 20 keys from ROOT. With the sequencer on KEY, the four keys below still start the patterns (only in this map: the other maps use those notes for drums).
  - **Groove Library:** the layout most commercial drum-groove libraries use, so their MIDI grooves play as written: kick on 36 (and 34, 35), snare on 38, sidestick 37, rimshot 40, hats closed on 42 and 22, pedal 44 and 21, open 46 and 24, the toms, crashes on 49 and 57, ride on 51 and 29, bell 53, tambourine 3, shaker 2, and every variant note in between (about 110 notes in all).
  - **General MIDI:** the standard layout (35-36 kick, 38 snare, 42 / 44 / 46 hats, 49 / 57 crashes, 51 ride, 52 china, 53 bell, 54 tambourine, 55 splash, 69-70 shaker...).
  - **Custom:** the list below MAP shows every note (0 to 127) and the drum it plays. Change any, and the map you were on is copied into Custom with your change. **SAVE AS DEFAULT** keeps it for new instances; your project keeps it too. Presets don't change the map.
  - The DRUMS page shows each drum's note in the map you're on ("-" if it has none), and the keyboard tints the notes that play.
  - **EXPORT MIDI** writes each drum on its note in the map you're on, so the file plays back right in that map.
- **Not known yet about the Groove Library map** (our guess until we can check a real groove file): which of the ride's notes are its edge and mute hits (all play RD), which hat notes are the in-between openings (they play the closed or open hat), and which crash notes are mutes (they play CY). Note 86 plays a crash: the published tables we found say crash, where the list we started from had it under ride. The china, splash, second crash and half-open hat have no note in it yet. Custom lets you set all of these yourself.
- **Tests** check, by measurement:
  - each new drum: the ride's ping frequency and SNAP, the bell's partials and TONE, the tambourine's three strikes at 0, 6 and 14 ms, the shaker's swell time with SNAP, the half-open hat choked by the closed one, each new drum's level;
  - the maps: every note of the Groove Library list, General MIDI's notes, each map playing the right drum in the plugin, Custom edits kept by the project and the default file and untouched by presets, the KEY pattern keys only under Root, MIDI export on the map's notes, and 12-drum kits loading.

What can't be checked here: how the new drums sound. They're new synth models, set by measurement, not by ear yet.

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
