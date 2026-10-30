# Changelog

Each version's notes appear on its GitHub Release page.

## 0.8.0

**The song track, part 1.** Lay grooves out as a whole song, bar by bar, and it plays along with your DAW. Our own design, inspired by the song track in chapter 7 of the drum software manual you sent.

- **SONG** (left of the on-screen keyboard): **KEYS** shows the keyboard, **SONG** the song track in its place.
- **Putting grooves in:**
  - **Drag a groove** from the GROOVES list onto the song track: it becomes a block (its name, coloured by its kind).
  - Or **right-click a groove: ADD TO SONG**. It goes after the last block, and the song track opens.
  - Or **drop MIDI files** on it from your computer. They play through the kit's own note map.
  - A block is a copy of the groove as the kit's drums, at the GROOVES page's TEMPO factor (1/2x, 2x...) and VELOCITY. Changing the groove file later doesn't change the song.
  - The song takes your DAW's time signature when its first block goes in.
- **Arranging:**
  - Blocks snap to bars and never overlap. **Dropped on a block**, a groove goes before or after it (whichever edge is nearer), and the blocks after it move along to make room. **Past the end**, it stays where you drop it (a gap before it).
  - **Drag a block** to move it.
  - **Drag its right edge** to make it longer: the groove repeats (a faint line shows where it starts again), and the blocks after it move along. Shorter leaves a gap.
  - **Click a block** to choose it; **DELETE** removes it.
  - **Right-click a block:**
    - DUPLICATE (the copy right after it) and REMOVE;
    - MUTE (greyed, silent);
    - SONG PART (Intro, Verse, Chorus, Fill... each its own colour);
    - CHANGE TEMPO (1/2x, 2/3x, 3/2x, 2x: its notes closer together or further apart, its length too).
- **Playing:**
  - **While your DAW plays**, the song plays with it: bar 1 of the song is your DAW's bar 1, each hit on its exact sample, and it follows when you jump or loop.
  - With the DAW stopped, **PLAY** plays it from the mark on the bar numbers (click them to move it), at the DAW's last tempo; it stops at the song's end. Starting the DAW ends PLAY: the DAW leads.
  - The red line shows where it's playing. The drum sequencer and the groove preview can play at the same time (your choice).
- **EXPORT MIDI:** the whole song as one MIDI file, each drum on its note in the kit's map (as the sequencer's export). Click to save, or drag it onto a DAW track.
- **Saved with your project**, not in presets (loading a preset keeps the song). **Undo** takes back each edit.
- **Tests:**
  - the placing rules (dropped on a block's first or second half, past the end, longer, shorter, moved, duplicated, removed, in 3/4);
  - each block's groove repeated to fill it and cut at its end; MUTE; CHANGE TEMPO;
  - a groove as a block through the folder's map, with the factor and velocity, its kind from its name;
  - the song saved and read back exactly; its MIDI file (notes on the right ticks, tempo, time signature);
  - played with the DAW: a block at bar 3 heard exactly 4 s in at 120 BPM (sample 192000), the snare 1.5 beats later on its sample, and the DAW starting part way in at 90 BPM;
  - PLAY from bar 2 with the DAW stopped, each hit on its sample, stopping at the end; STOP; the DAW starting ending PLAY;
  - undo and redo; the project saved and loaded; a preset loaded keeping the song; EXPORT MIDI's notes;
  - the strip: a MIDI file dropped, DELETE, the menu's items each doing what it says, and ADD TO SONG from the GROOVES page.

What can't be checked here: how it sounds, how it feels to drag blocks in your DAW, and real groove packs.

Not in this part (next, if you want them, in this order): editing inside a block (its parts from other grooves, a drum taken out, velocity, quantize, cutting a block in two); making a song from one groove folder's verses, choruses and fills (a song made for you); tempo and time-signature changes within the song. The manual's chapter 7 describes all of these.

## 0.7.0

**The groove player, part 4: GROOVE PARTS.** Hear, drag or lay in just part of a groove: the hats of one over the kick and snare of another. Our own design, inspired by the groove parts of commercial drum software.

- **GROOVE PARTS** (right of the groove list): the chosen groove's parts, each with how many hits it has: KICK, SNARE, HI-HAT (closed, open and half-open), HI-HAT PEDAL, RIDE (and its bell), TOMS, CRASH (the crashes, china and splash), PERCUSSION (clap, tambourine, shaker). Parts the groove doesn't use are dimmed.
  - **Click a part to tick it.** From ALL, a click picks just that part; click more to add them; ALL ticks them all again.
  - **The preview plays only the ticked parts.** Tick or untick while it plays and you hear the change at once.
  - **Dragging a groove** out takes only its ticked parts. **Drag a part's row** to take just that part. Its file is named for it ("... Verse 01 HI-HAT").
- **LAYER ONTO A B C D:** the ticked parts go into that drum sequencer pattern.
  - Only the pattern's notes on those drums are replaced; its other drums stay as they are. A part shorter than the pattern repeats to fill its LENGTH.
  - LENGTH and the sequencer's other settings don't change. TEMPO's factor and VELOCITY apply, as with TO SEQUENCER.
  - For example: TO SEQUENCER B with one groove, then choose another groove, tick HI-HAT, LAYER ONTO B: its hats over the first groove's kick and snare.
  - **Undo** takes it back.
- **Fixed: undo after TO SEQUENCER.** Putting a second groove into the same pattern with nothing else changing (the same LENGTH, the same pattern, the sequencer already on) didn't make an undo step of its own, so undo took back both grooves. Each is its own undo step now.
- **The groove list** now fits its width (no scrolling sideways to see MATCH).
- **Tests:**
  - every drum in a part;
  - the preview playing only the ticked parts, and changing as you tick;
  - a dragged file with only those notes, named for them;
  - hats laid onto a pattern: its kick and snare kept, its old hats gone, a one-bar part repeated to fill 8 beats (32 sixteenths, the last at 7.75 beats), its velocity, LENGTH unchanged, and undo bringing the pattern back exactly;
  - TO SEQUENCER twice into the same pattern undoing one step;
  - the page's counts, ticking and LAYER ONTO.

What can't be checked here: how it sounds, and real groove packs' parts (which notes a pack uses for each drum comes from the folder's FILES' MAP).

Next: the song track (grooves arranged into a song). It's in the manual's chapter 7: please send it when you can, so it's designed from what it describes.

## 0.6.0

**The groove player, part 3: TAP TO FIND.** Tap a rhythm, and the grooves closest to it come first. Our own design, inspired by the tap-a-rhythm search of commercial drum software.

- **TAP TO FIND...** (GROOVES, left of the filter columns) opens the tap panel over the list:
  - **Six pads:** KICK, SNARE, HI-HAT, RIDE, TOMS, CRASH. Clicking one plays its drum and taps it. Your controller or the on-screen keyboard taps too: each note taps the drum it plays (through the kit's map).
  - **A two-bar loop** runs while the panel is open, with a **metronome** (a click on each beat, higher on the first of the bar) and a playhead. Tap one drum a time round if you like: every tap stays.
  - **TEMPO:** your song's when it opens; change it and the loop starts again. **QUANTIZE:** 1/4, 1/8 (as it opens: right for most beats), 1/16, 1/8 or 1/16 triplets (for swing and shuffle). Each tap is drawn where it lands on the grid, with a small mark where you played it.
  - **CLEAR** clears every tap, a row's **x** just that drum's. **CLOSE** puts it away (your taps stay for next time). **FIND** lists the grooves closest to your rhythm first.
- **MATCH** (a new column): how close each groove is, 0 to 100%. **CLEAR TAP** goes back to the list as it was. The folder, search, favourites and filter columns still narrow what's listed.
- **How grooves are compared** (our own measure):
  - **Only the drums you tapped count.** Tap kick and snare, and the hats don't matter.
  - Each drum scores by the hits both rhythms have on the QUANTIZE grid, against all the hits either has. The drums are weighted by how many hits you tapped on each.
  - **HI-HAT and RIDE stand in for each other** when a groove has only one of them, since the riding hand can be on either.
  - A groove is tried from **each of its bars** (looped if shorter than two bars), and its best two bars count, so a rhythm in bars 3-4 is found too.
- **Drop a MIDI file** on the GROOVES page: its first two bars are the rhythm searched with.
- **Right-click a groove: FIND SIMILAR** searches with its first two bars. FAVOURITE is there too.
- **Your library is read once more** the first time you open GROOVES after updating: the index now keeps each groove's rhythm, so a search never opens the files. Then it's quick again.
- **Tests:**
  - the comparison: the same rhythm 100%; drums not tapped not counting; closer rhythms higher (one kick different above four-on-the-floor above kick and snare swapped); QUANTIZE taking taps a little off (and one a hair early for beat 1); hi-hat and ride standing in; a rhythm found in bars 3-4; a one-bar groove looped;
  - the rhythm kept in the index;
  - the metronome on each beat to the sample at 120, higher on the bar, silent after;
  - notes as taps where they fall (1.25 beats at sample 30000, and round the loop), by drum group through the map; a pad's tap and drum;
  - the library sorted by match;
  - the page: tapping, FIND, FIND SIMILAR, a dropped file, CLEAR TAP.

What can't be checked here: how tapping feels in your DAW (a pad's tap is placed at the start of the next audio block, a few milliseconds; QUANTIZE takes that up), how the metronome sounds, and how well the comparison ranks real groove packs: tell us when a groove you'd expect near the top isn't.

Next, your choice: groove parts (take one drum's part from one groove and lay it over another), or the song track (needs the manual's chapter 7).

## 0.5.0

**The groove player, part 2: filter columns, and grooves into the drum sequencer.**

- **Filter columns** above the groove list: TYPE, FEEL, TIME, POWER HAND, TEMPO. Each lists the values the grooves you're browsing have (in the folder you've chosen, with your search and favourites), and how many have each.
  - **Click** a value: only grooves with it. Several in one column: grooves with any of them. **Right-click**: none with it (shown red, struck through). Click again to let it go.
  - The columns work together: a groove is listed if it passes every column. **CLEAR** lets them all go. A column's title shows how many of its values are in use.
  - **TYPE:** Intro, Verse, Pre-Chorus, Chorus, Bridge, Fill, Ending, Count-In, or Beat. Read from the groove's name, else its folder's, else the one above ("Fills/Var 2" is a fill), the way groove packs name them; Beat when none of those words is there.
  - **FEEL:** Straight or Swung. Our own measure, from the notes: whether the hits between the beats fall on the half and quarter beats (straight) or on the triplet grid (a swing or shuffle).
  - **TIME:** the time signatures there are. **POWER HAND:** as in the list. **TEMPO:** below 80, 80-99, 100-119, 120-139, 140-159, 160 and up, or Unknown (a file that doesn't say).
- **TO SEQUENCER A B C D:** the chosen groove becomes that drum sequencer pattern (replacing it).
  - Each hit is a note on its drum's row, where it was played (between the grid's steps too, so a laid-back snare stays laid back), with its velocity.
  - TEMPO's factor and VELOCITY go with it (as when you drag a groove out).
  - LENGTH becomes the groove's, and the sequencer comes on playing that pattern (with your song, on SONG).
  - A pattern holds 16 beats at most, so a longer groove brings its first 16. LENGTH is one setting for all four patterns.
  - Notes no drum plays in the folder's FILES' MAP are left out. The status line says what was made ("Verse 01 is pattern B now (8 beats)").
  - **Undo** takes it back.
  - SHUFFLE still applies to what's in the pattern: leave it at 0% to hear the groove as played.
- **Also:** tempos read from files show as whole numbers (104, not 104.0: a MIDI file keeps tempo in millionths of a second a beat).
- **Your library is read once more** the first time you open GROOVES after updating: the index now remembers FEEL. Then it's quick again.
- **Tests:** FEEL (straight and shuffle beats, a stray triplet note not making a beat swung); TYPE from names and folders (PRE-CHORUS not CHORUS, "Country" not a count-in); every column's values; choosing, several in a column, excluding, columns together, CLEAR; a groove put in pattern B and played with the song at 120 hitting the same samples as the groove (its off-grid snare 25440 samples in); the factor, velocity, a 32-beat groove cut to 16, notes with no drum; undo; the page's columns, counts and buttons.

What can't be checked here: real groove packs (TYPE depends on how a pack names its files: if one names them differently, tell us the words), and how it sounds.

Next, your choice: Tap-to-find (tap a rhythm, get the closest grooves), groove parts (one drum's part from one groove over another), or the song track (needs the manual's chapter 7).

## 0.4.0

**The groove player, part 1: your MIDI grooves, browsed, played through the kit and dragged into your DAW.** A new **GROOVES** tab (after DRUMS). Our own design, inspired by the groove browsers of commercial drum software. It reads only your own MIDI files, from where they are on your computer: no grooves, sounds or tags from anyone else ship with it.

- **Your folders:** ADD FOLDER... links a folder of MIDI grooves (and everything in its subfolders); REMOVE unlinks it (the files stay). The folders are listed on the left, opening as you go: choose one and the list shows only its grooves (with SUBFOLDERS on, its subfolders' too; off, just the files right in it). ALL GROOVES lists everything.
  - **FILES' MAP**, for each linked folder: which drum each note in its files is. GROOVE LIBRARY (the layout most commercial groove packs use: the map from 0.2.0), GENERAL MIDI, CUSTOM (your own map from SETTINGS), or ROOT (files made for this kit's keys).
  - The folders are read in the background the first time you open the page; what was read is remembered, so a big library comes up at once next time (only new or changed files are read again).
- **The list:** each groove's name, its folder, time signature, bars, tempo, **power hand** (what the riding hand plays most: closed hat, open hat, ride, crash or percussion) and **intensity** (how hard and how busy it's played).
  - These come from the file alone: its tempo (or "... 102 BPM" in its name or its folder's), its time signature, its notes. Power hand and intensity are our own measures, worked out from the notes through the folder's map.
  - **SEARCH**: words in a groove's name or its folder's (all of them). **ONLY**: just your favourites (click a groove's star). **SORT**: by name, nearest your song's tempo first, or your song's time signature first.
- **Preview:** click a groove to play it through the kit, round and round; double-click to play it once. **STOP** stops it. It plays your kit as it is (your sounds, effects and outputs), on the beat to the sample.
  - **TEMPO:** your song's, or the groove's own. Then **1/2x, 1x, 2x, 2/3x** (a 4/4 groove felt in 6/8) or **3/2x** (6/8 to 4/4): only how fast it plays, the playing stays as it was.
  - **VELOCITY:** every hit's velocity scaled together (25% to 175%), so the accents stay.
- **Drag a groove** onto a MIDI track in your DAW, or to a folder. The dragged file has the TEMPO factor and VELOCITY in it (at 2x the notes are twice as close, since your DAW places it at the song's tempo; at 2/3x a 4/4 groove is marked 6/8).
  - **DRAG: FOR THIS KIT** puts each note on this kit's note for that drum in its map (SETTINGS), so the groove plays right back through PR7-D; if the folder's map is already the kit's, the notes stay exactly as written. **AS WRITTEN** keeps the file's own notes (for other drum software).
- **Where they're kept:** your folders, favourites and these settings are in the presets folder (Grooves.xml, Groove Index.xml), shared by every PR7-D you open.
- **Grooves from other drum software:** PR7-D reads standard MIDI files. Grooves inside another program's own library: drag them out of that program into a folder first, then link the folder.
- **Tests:** reading a groove (notes, tempo from the file or a name, time signature, bars, a closing crash ending the last bar, power hand, intensity, files that aren't grooves); the library (folders as filters with and without subfolders, words, favourites, the three sorts, what's kept, a changed file read again, the background read); the preview (each hit on its sample at the song's tempo and its own, all five factors, looping and once, the velocity scale measured at -12 dB for half, STOP, a folder's map deciding which drum a note plays); the dragged file (this kit's notes, as written, velocity and factor in it); the page (the folder tree, filtering, search, playing).

What can't be checked here: how real groove packs' files read (none are here: only MIDI files made in the tests), and how the preview sounds. If a pack's tempo, bars or power hand look wrong in the list, send one or two of its files and we'll check them.

Next, part 2 (your choice): the filter columns (type: beat / fill / intro... from the names; time signature; power hand; tempo; each can be excluded) and dropping a groove into the drum sequencer, or Tap-to-find (tap a rhythm, get the closest grooves).

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
