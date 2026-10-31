# Changelog

Each version's notes appear on its GitHub Release page.

## 0.15.2

**Fixed: every PR7-D added to a song read your whole groove library again** (10,000 files in your folders). Each PR7-D keeps its own list of grooves, and the first time its GROOVES page opened it checked every file in your linked folders against the saved list, though the PR7-D before it had just done the same. It also loaded that saved list on the window's own thread, so the window could freeze while it did.

- **Your folders are checked once each time your DAW starts**, by the first PR7-D whose GROOVES page opens, in the background, with the list shown at once from what was read before. Every PR7-D you add after that lists the grooves straight away and checks nothing.
  - Measured here with 10,000 grooves: a new PR7-D used to hold the window for about 0.2 s and then check for about a second (several times that on a Windows disk with a virus scanner looking at each file); now it takes under a millisecond of the window's time and lists all 10,000 a quarter of a second later, reading none.
- **Only new or changed files are read** (as before), and now the status line says so: "10000 grooves (checking for new ones)", instead of counting all 10,000 as if reading them.
- **A groove database that hasn't changed isn't even opened** during the check: its grooves come from the saved list.
- **The saved list isn't rewritten when nothing changed** (with 10,000 grooves it's megabytes).
- **A first read cut short isn't lost:** if a PR7-D is removed while reading your folders for the first time, what it read is kept, and the next one carries on from there.
- **New: REFRESH** (beside SUBFOLDERS): check your folders now, if you add grooves while your DAW is open. Adding or removing a folder, or changing its map, checks as before.

Not checked here: the time on your computer (it depends on your disk and virus scanner). The first PR7-D after starting your DAW still checks once, in the background, while the list is already there to use.

## 0.15.1

**Fixed: the plugin's window used far too much CPU while drums played** (since 0.14.0's KIT VIEW). The sound was never the problem (measured: well under 3% of real time for a beat on the default kit); the drawing was.

- **The kit view** shrank its 1280-pixel picture and each glowing piece's picture to fit the window, with high-quality smoothing, **every time it redrew**: 30 times a second for each piece glowing, more pixels again with Windows' display scaling. It also had the window's background painted underneath it each time, though it covers it.
  - Now the pictures are shrunk **once**, to the window's real size in the screen's own pixels (150% or 200% display scaling included), and only copied after that, lined up on whole pixels (between two, every pixel would be smoothed again). The view tells the window it covers what's behind it.
  - Measured here: redrawing a glowing piece went from about 1 ms to under 0.1 ms (0.17 ms at 150% scaling), the whole kit view from 10 ms to under 1 ms, the whole window on the KIT VIEW from 36 ms to 19 ms. A beat lighting four drums cost about 12% of a core in drawing; now about 1%.
  - It looks the same: a test compares it, at 100%, 150% and 200% scaling, with the pictures drawn the old way (a half-pixel shift, now on whole pixels, is the only change).
- **The GROOVES list** redrew every row 15 times a second while a groove played; now only the playing row (the whole list when another groove starts).
- **The OUTPUT meter** redraws only its column of lights, not its numbers.

Not checked here: the real figure on your computer. If your DAW's or Windows' CPU meter still runs high, tell me which (the DAW's audio meter, or Task Manager) and with which page showing, and whether it drops when the plugin's window is closed.

## 0.15.0

**GROOVES reads groove databases.** Some drum software installs its MIDI grooves not as .mid files but inside one database file per pack, named `midiDB`. Until now GROOVES only looked for .mid files, so a folder of those packs listed nothing. It reads them now, from your own computer when you link the folder; nothing of them is copied into the plugin or your presets.

- **Link the folder that holds the packs** (ADD FOLDER...): each `midiDB` in it, or in any folder below, shows up in the tree as its pack, with the pack's own folders inside, their sorting codes taken off and the song's name added where they don't say it. For example: **Heavy Rock Grooves > Straight 4-4 > Ordinary Man 72 BPM > Fills > Fills 01**.
- **Everything works on them as on .mid files:** preview, the filter columns, search, favourites, tap to find, groove parts, to the sequencer (buttons or dragged onto its grid), the song track and song creator, and dragging a groove out to your DAW (as a real MIDI file).
- **Tempo, time signature and length** come from the database; power hand, feel and the rest are worked out from the notes as for any groove.
- **Velocities are our guess.** The databases keep each note's time and drum but no velocity. They do mark the strong hits of each groove's first two bars, so a hit on a marked step plays at **112** and the others at **80** (the two bars' marks repeat through longer grooves); a groove with no marks plays at 100. Use the preview's VELOCITY to scale them.
- **Electronic packs** that use their own near-General-MIDI note layout want the folder's map set to **General MIDI** (FILES' MAP), as found when we checked your EZX Electronic database for 0.8.1.
- **Checked against your two databases** (Heavy Rock Grooves, EZX Electronic; privately, nothing of them committed): all 495 and 267 grooves listed, every note read (29,458 and 10,902), every length right, the same as a standard database reader gets from the same files.
- **How:** a small reader of the database file format of our own (read-only; it never changes your files), so no extra library goes into the plugin. A damaged or unreadable file lists fewer grooves or none, never a crash.
- **Fixed: tempos read from MIDI files** came back a hair off (90 BPM as 89.99996, as MIDI files keep microseconds a beat): rounded to a thousandth now.
- **Tests:** a small database made for the tests (laid out like a real one, its grooves invented) read through its tables, several pages deep and spread over overflow pages; the accent velocities; 43 grooves listed in their folders with tempo, length, time signature (a 3/4 one) and FILL from the folder; a groove's 26 notes and their velocities; a 64-bar groove's 576 notes; the folder tree; preview through the kit and a dragged MIDI file; not-a-database, cut-short and scrambled files listing fewer or none without crashing.

Not checked here: where your packs keep their `midiDB` files on Windows (link the folder you found them in, or one above it), and how the guessed velocities sound to you.

## 0.14.0

**The KIT VIEW: a drawn kit to click and play.** The first step towards a kit page like the drum software's (our own artwork, look and design: only the idea of a clickable kit is shared).

- **A new KIT VIEW tab on DRUMS** (first in the right-hand panel, beside DRUM and SEQUENCER; the page opens on it): the kit seen from the drummer's seat.
  - It's **our own 3D kit**, modelled and rendered for the plugin: gloss-black drums and black hardware, each hoop, cymbal edge and bell, pedal and stand lit by a glowing ring, the colour running across the kit from pink (hi-hat side) through red to orange (ride side), on a dark red grid floor. Only the PR7 family's colours (a test checks every coloured pixel as the plugin draws it).
  - 11 pieces: kick (with its pedal), snare, two rack toms, floor tom, hi-hat (with its pedal), two crashes, ride, china, splash, and a tambourine on the hi-hat stand.
- **Click a drum to choose it and hear it**, as clicking its row does (SHIFT / CTRL choose several). Where you click picks the articulation:
  - snare: the head plays **SD**, the hoop **SD2** (the rimshot), the shell **RIM** (the sidestick);
  - ride: the bow **RD**, the bell **RB**;
  - hi-hat: the cymbals **CH**, the pedal **PH** (the open and half-open hats are on their rows);
  - the clap and the shaker have no piece: they're on the rows only.
  - The piece under the mouse lights a little and its drum, model and note show at the bottom; the chosen drum's piece stays lit a little.
- **Each piece glows as it plays,** whatever plays it (your DAW, the keyboard, the sequencer, a groove preview, the song), brightest on the hit and fading over its first second.
- **How it's drawn:** the kit was rendered once (Blender, from our own model) into pictures built into the plugin: the kit, each piece glowing, and a hidden click map with each part in its own value, so a click is matched to the exact part under it. The scripts that make them are in `resources/kit/`. It adds about 2.4 MB to the plugin.
- **Tests:** the pictures load and fit together; all 16 drums on the kit have a part where a click lands on them, the clap and shaker none, the empty floor nothing; a click on the snare's head chooses SD and plays it, CTRL-click on the ride's bell adds RB; the china and the snare (from SD2) glow on their hits, nothing else, a quarter as bright half a second later, dark after a second; the pictures' colours.

Not checked here: how smoothly the glow fades in your DAW on Windows (it redraws only the pieces that change, 30 times a second).

## 0.13.0

**Drag a groove straight onto the drum sequencer.** The last piece of the groove player's plan.

- **Drop a groove on the SEQUENCER's grid** (DRUMS page): it becomes the pattern shown (A to D), as TO SEQUENCER does: its notes on the kit's drums at their steps, the pattern's LENGTH set to it, the sequencer on.
  - **From the GROOVES list.** With GROOVE PARTS ticked, only those parts are laid onto the pattern (its other drums kept), as LAYER ONTO does.
  - **A GROOVE PARTS row** (say HI-HAT) dragged onto the grid: just that part of the chosen groove, laid onto the pattern.
  - **From the SONG CREATOR's columns.**
  - **A MIDI file from your computer**, through the kit's note map.
  - While a groove is over the grid, it's framed orange and says what dropping it will do ("DROP: THE GROOVE REPLACES PATTERN A", "... HI-HAT LAID ONTO PATTERN A"). Each drop is one undo step.
- **The DRUMS tab opens for a drag:** hold a groove over it for half a second and the DRUMS page comes up with the SEQUENCER showing, to drop it on the grid. (The page you dragged from stays underneath until the drag ends, then the window settles on DRUMS; hiding it during the drag would cancel the drag.)
- **Tests:** a groove dropped as the pattern (kicks on steps 0 and 8, snares on 4 and 12, eight hats); ticked KICK laid onto it, the snares and hats kept; undo back to the pattern before; a HI-HAT row laid onto another groove's pattern, its kicks kept; a song creator groove; a MIDI file through the kit's map; drags it doesn't take refused; the DRUMS tab: nothing at 0.2 s, the sequencer in front at 0.5 s with the GROOVES page still under it, the window on DRUMS when the drag ends, nothing if the drag moved off the tab first.

Not checked here: how a real drag feels in your DAW's window (the tests drive the drop and the tab's timer directly).

## 0.12.0

**The song track, part 4b: RECORD and BOUNCE.** Our own design, inspired by the transport's record button and the bounce in chapter 7 of the drum software manual you sent. That completes the song track as planned.

**0.11.0 was never published** (its macOS build crashed in a test, so no release was made). **This release is the first with 0.11.0's changes too:** tempo and time signature changes (the TEMPO and TIME views), TRACK... > IMPORT MIDI FILE..., and **only the PR7 family's colours** everywhere (song parts, sliders, highlights and the rest: see 0.11.0 below).

- **Fixed: the crash.** Adding a tempo change worked out its place in the song's list of tempo changes in the same step that inserted it. Which half a compiler does first is up to the compiler: the Mac's measured from where the list had been before the insert moved it in memory, read a nonsense place and crashed. (Linux's did it the other way round, so the tests there passed.) It's two steps now, the same on every computer. Checked by building the tests with the compiler macOS uses and a memory checker on (clang with AddressSanitizer): the song tests pass with no memory errors.

- **REC** (beside PLAY): armed, the notes you play while the song plays go into it, at the beats you play them.
  - From your DAW's MIDI (a controller through it too) or the on-screen keyboard, each through the kit's note map. Notes the kit doesn't play are left out.
  - With your DAW playing, or with PLAY (through the song's tempo changes too).
  - **On top of the blocks there** (overdub): a block that repeats its groove is written out first, so a note lands only where you played it. Where there's no block, a **RECORDED** block is made, growing bar by bar as you play on; after a gap, a new one.
  - Click REC again to stop: **the take is one undo step.** Then QUANTIZE, EDIT BLOCK and the rest work on it like any block.
  - Armed, REC is red and the song lane is framed red.
- **FOLLOW DAW** (on the song strip, lit when on): the song plays with your DAW (as before), or, switched off, **only with PLAY**, your DAW playing or not.
  - Switch it off when the song's MIDI is on a DAW track too (EXPORT MIDI dragged in), or every hit plays twice (the manual warns about the same thing). After EXPORT MIDI with it on, the status line reminds you.
  - Kept with the song in your project; undo takes it back.
- **TRACK... > BOUNCE TO AUDIO...:** the song rendered to WAV files, faster than real time, while the plugin plays on (the status line shows how far).
  - **One file: the whole kit**, or **a file per output, as routed** (MAIN, and each of OUT 2-16 a drum, an insert bus or a send return is routed to).
  - 24-bit, 16-bit or 32-bit float, at the plugin's sample rate; a gain (dB).
  - From bar 1 through the song's tempo changes (none: your DAW's tempo) to its end, then on until the drums and effects have died away (silence, or 10 s).
  - **The plugin's own delay** (the limiter's and CONSTRICTOR's look-ahead) **is taken off**, so the files start on bar 1 and line up in your DAW.
  - It's rendered by a copy of the plugin made from your settings as they are, so the one you're playing isn't touched.
- **Tests:**
  - RECORD with the DAW playing: notes at their exact beats, a take's block growing bar by bar to 3 bars, a new one after a gap, a note the kit doesn't play left out, nothing recorded when not armed, the take undone in one step;
  - on top of a repeating block (its repeats written out, the note only where played), and with PLAY through a tempo change (a note 2.5 s in at beat 4.5: 4 beats at 120, half a beat at 60);
  - BOUNCE: the hits on their samples (0, 36000, 96000) with the limiter's look-ahead on, the file ending quiet, 24-bit at 48 kHz; +6 dB exactly twice as loud (32-bit float); a file per output (the snare on OUT 3: in its file only); an empty song: no files;
  - REC and the TRACK menu on the strip;
  - FOLLOW DAW off: your DAW playing plays nothing of the song; PLAY plays it on its own clock with the DAW playing (each hit on its sample); kept in the project; undone; the button and the reminder after EXPORT MIDI.

Not checked here: how it sounds, recording from your controller and DAW live, and the files in your DAW. Left out (that software's, needing its microphones): bouncing microphone channels and bleed; a loop area.

## 0.11.0

**The song track, part 4a: tempo and time signature changes, and IMPORT MIDI FILE. And the family's colours only.** Our own design, inspired by the track menu in chapter 7 of the drum software manual you sent.

- **The family's colours only** (as you asked): every colour in the plugin is now in the PR7 red / pink / orange range, or pearl white and onyx.
  - **Song parts:** Intro pearl, Verse orange, Pre-Chorus amber, Chorus hot pink, Bridge light pink, Fill red, Ending oxblood, Count-In onyx, Beat salmon. They were cyan, yellow, purple, green and blue.
  - **Sliders, toggles, selected text, scroll bars:** were JUCE's built-in blue (the VELOCITY sliders on GROOVES and EDIT BLOCK, the limiter's), now orange.
  - **The limiter's "under the ceiling" readout and the sample editor's anchor:** were green, now pearl white. The level meter's warning segment and the lit key's text: amber and warm pearl instead of yellow.
  - Checked on screenshots of every page (no pixel outside the range), and a test keeps it so.
- **The song strip's views: BLOCKS, TEMPO and TIME** (beside TRACK...).
  - **TEMPO:** the song's tempo as a line over the blocks. **Double-click** to add a tempo change (on the nearest beat, at the height you click), **drag** one to move it, **right-click** it: RAMP TO THE NEXT (a smooth glide to the next one's tempo), a TEMPO from 60 to 180, REMOVE.
  - **TIME:** **double-click or right-click a bar:** BEATS A BAR (1 to 15) and BEAT LENGTH (2, 4, 8, 16), REMOVE THIS CHANGE. The bar numbers follow (a change shows its time signature by its bar's number), and blocks snap, lengthen and split on those bars. As in the manual, a time signature change doesn't change the grooves' notes.
  - **Where the tempo changes count:** PLAY (with your DAW stopped) plays through them, each hit on its sample, a glide too; EXPORT MIDI writes them (a glide as steps a 16th note apart) with the time signatures. **While your DAW plays, its tempo leads** (draw tempo changes in your DAW for that).
- **TRACK... > IMPORT MIDI FILE...:** a whole song's MIDI (all its tracks) through the kit's note map. It asks whether to take the file's **tempo and time signatures** too.
  - **Markers** in the file (the "Verse", "Chorus"... a DAW writes) cut it into blocks, each named and coloured by its part; without markers it's one block (SPLIT HERE cuts it).
  - An empty song becomes the file's song; otherwise it goes after the song, from the next bar line (its tempo changes moved along with it).
  - TRACK... also has REMOVE ALL TEMPO CHANGES (back to your DAW's tempo) and REMOVE ALL TIME SIGNATURE CHANGES.
- **Fixed: a song hit exactly on the line between two of the DAW's audio blocks** could be played one sample early (on the earlier block's last sample) when the DAW's position, rounded, landed a hair before it. Each hit is now placed by its own sample, in the block that sample is in.
- **Tests:**
  - time signature changes: bar lines, snapping, a change put on the nearest bar line, a change repeating the one before left out, later changes moved onto bar lines when the first bars change; resize and split on them;
  - tempo: 8 beats at 120 and 4 at 60 = 8 s exactly; a glide 60 to 120 over 4 beats = 4 ln 2 s, half way 90 BPM, beats to seconds and back; saved and read back; in the MIDI file;
  - heard: PLAY's kicks at 0, 2 s and 4 s through a step from 120 to 60; the glide's second kick at 4 ln 2 s; from bar 2; the DAW's 120 leading while it plays;
  - import: markers to named, coloured blocks; with and without the tempo and time signatures; no markers; a file timed in SMPTE frames refused; after a song, its tempo moved along;
  - the strip: a tempo change double-clicked in, dragged, ramped, set, removed; a time signature set from the menu and removed; the TRACK menu;
  - the colours.

Not checked here: how it sounds and feels; DAWs' MIDI files beyond ours (their markers, tempo maps). Next, part 4b: recording into the song, and bouncing it to audio.

## 0.10.0

**The song track, part 3: the SONG CREATOR.** A whole drum track from one groove, in a couple of clicks. Our own design, inspired by the song creator in chapter 7 of the drum software manual you sent. That one finds a groove's "variations" in its makers' own database, which we can't read or ship; ours come from your linked folders.

- **CREATOR...** on the song track opens it over the page (the song track stays below). Also: right-click a groove in GROOVES, or a block in the song: **USE WITH SONG CREATOR**; or drop a groove from the GROOVES list (or a MIDI file in one of your linked folders) on it.
- **The source's song, in columns:** the grooves in the source's folder, by song part: INTRO, VERSE, PRE-CHORUS, CHORUS, BRIDGE, FILL, ENDING, BEAT (from the names, as GROOVES' TYPE). If that folder has only one kind (a pack keeping its fills in "Fills" beside "Grooves"), it looks one folder up, never past your linked folder.
  - **Click** a groove to hear it, **double-click** to add it to the song, or **drag** it onto the song track.
  - **Right-click:** ADD TO SONG, COPY, **REPLACE ALL "CHORUS" ON TRACK** (every chorus block plays it now, each keeping its length), **REPLACE THE CHOSEN BLOCK**, USE AS SOURCE.
- **SONG STRUCTURES** (left), each drawn as its sections in their colours. Ours: Verse / Chorus (ABAB), With Bridge (ABABCB), With Pre-Chorus, AABA, Chorus First (BABAB), 12-Bar Three Times (A the verse, B the chorus, C the bridge; an intro, a fill's bar before each chorus, an ending).
  - **MAKE SONG:** the song made from the chosen structure, in place of the one there (undo brings it back). **ADD TO END:** after it.
  - Each section takes the next groove in its column (two verses: Verse 01, then Verse 02). A part the folder hasn't is played by the nearest kind, coloured as the part it plays (a pre-chorus by a verse, a bridge by a chorus, an ending by a fill, an intro by a verse). A missing **fill** isn't played by another groove: the groove before it plays on through its bar.
  - **SAVE SONG'S STRUCTURE...:** your song track's parts and lengths (no notes) as a structure of your own, to make songs from any groove. Right-click yours to RENAME or REMOVE them. They're kept in "Song Structures.xml" in your presets folder.
- **Tests:**
  - structures written and read; ours each whole four-bar phrases and an ending's bar; a song's structure read off it;
  - the stand-ins and the next groove each time (a fill missing: none);
  - the grooves round a source: its folder only (not the next song's), one up for fills kept apart, never past the linked folder;
  - a song made from real files: its blocks, places, lengths and colours, the missing fill's bar given to the groove before; heard: the chorus's snare on its sample;
  - your structures saved (the same name replaced), renamed, removed, in the presets folder;
  - the panel: a click playing a groove, MAKE SONG and ADD TO END (undone one at a time), REPLACE ALL, REPLACE THE CHOSEN BLOCK, ADD TO SONG, COPY, SAVE SONG'S STRUCTURE.

Our own choices (tell me if you'd like them otherwise): which part stands in for a missing one; the fill's bar before each chorus in our structures; the structures themselves. Not checked here: how the made songs sound, and real packs' folders (which grooves count as one song's comes from the folders).

## 0.9.0

**The song track, part 2: editing inside a block.** Change a block's drums, notes and feel without touching the groove file it came from. Our own design, inspired by the block editing in chapter 7 of the drum software manual you sent (its block menu, play-style editor and grid editor, in one simpler panel).

- **Right-click a block**, new:
  - **Edit Block...** (or double-click the block): the block editor, below.
  - **Copy**, then **Paste After** (a copy after this block, the ones after it moved along).
  - **Paste Drums Onto It:** the copied block's drums, all or one group at a time (KICK, SNARE, HI-HAT...), in place of this block's own on those drums. A shorter part repeats to fill.
  - **Remove Drums:** a group of drums taken out (each listed with how many notes it has).
  - **Quantize** (1/4, 1/8, 1/16, 1/8 or 1/16 triplets) and **Velocity** (50% to 150%, the balance between hits kept).
  - **Split Here** (at the bar line nearest where you clicked) and **Merge With Next** (a gap between them kept as rests).
- **GROOVES: right-click a groove, Copy for Song.** With GROOVE PARTS ticked, only those parts: copy one groove's hats, then paste them onto any block (PASTE DRUMS ONTO IT).
- **EDIT BLOCK**, over the page (the song plays on below it): a row for each drum the block plays, its notes on a grid with the bars numbered, each note as tall as it's loud.
  - **Click** an empty place: a note there, on the GRID (1/4 to 1/16 triplets). **Drag a note up or down**: louder or softer. **Right-click** a note: gone.
  - **ADD DRUM:** a row for a drum the block doesn't play yet.
  - **Click a drum's name** to choose it (CTRL or SHIFT for more). The buttons work on the chosen drums, or on all of them when none is chosen:
    - VELOCITY (25% to 200%: let go of the slider and it's done; it goes back to 100% for the next change);
    - QUANTIZE (to the GRID), REMOVE, COPY and PASTE;
    - **MOVE TO:** one drum's notes played by another (the hats' pattern on the ride, say), merged with that drum's own.
  - PASTE puts each copied drum's notes on the same drum here. If you copied one drum and have one drum chosen, they go onto the chosen drum.
- A block's edits stay in the song (not the groove file), are saved with your project, and **undo** takes back each one.
- **Tests:**
  - split and merge (the song playing the same), removing and pasting drums (a one-bar part repeated over two), quantize (a note pushed past the end going round to the start; two on one beat becoming one, the louder), velocity (balance kept, never past full or silent), moving a drum (merged, no doubled hits), adding and removing notes;
  - heard: a late snare (1.06 beats) quantized to 1/16 lands exactly on beat 2 (sample 24000 at 120 BPM); a block at 50% VELOCITY plays sample for sample as the same notes written at half velocity;
  - the block menu's items, each doing what it says, and the seven edits undone one by one back to the song as it was;
  - the editor: a click adding a note on the grid, a drag setting its velocity, a right-click on its drawn edge removing it, and each button.

Not checked here: how it sounds and feels in your DAW. Not in this part: adding or removing hits "like a drummer would" (that software uses its makers' statistics from real performances, which we don't have), articulations, an opening hit, and moving single notes sideways in the editor (remove and click it in again).

## 0.8.1

**Groove TYPE: fills found by more names.** From a check against two real groove packs you sent (Heavy Rock Grooves and EZX Electronic: 762 grooves, compared with their own labels; the packs stay with you, nothing from them is in the plugin).

- **"Break" is a fill** ("909 Break 2", "606 Clean Break"), as a word of its own, with or without a number after it ("Break3"). "Breakdown", "Breakbeat", "Breaks" and "Jailbreak" aren't.
- **"Fill" wins over the song's sections:** "Introfill", "Verse Fill 02" and "Pre-Chorus Fill" are fills now (they were Intro, Verse, Pre-Chorus).
- TYPE is read from the names each time, so your linked folders show it at once: no rescan.
- In the check, TYPE now agrees with the packs' own Beat / Fill labels on every groove (it missed 23 of the electronic pack's fills before).

What the check also found (no change needed):

- **The GROOVE LIBRARY map is right** for the rock pack's notes: closed hat 22 and 61, open hat 24-26, pedal 21, ride 51, bell 53, crashes 49 and 57, kick, snare and toms. The power hand agreed on all 250 grooves labelled with a hat, ride or cymbal; the bar counts on all 762.
- **EZX Electronic uses its own layout,** close to General MIDI (52 china, 54 tambourine, 56 cowbell): set that folder's **FILES' MAP to General MIDI**.
- **FEEL** agreed on 91% (rock) and 98% (electronic). The rest are mostly triplet grooves the packs call straight; two stricter rules tried on your grooves did worse, so it stays as it is.
- Not checked: how hard a groove is (the packs keep no velocities we could compare), half / double time (not measured yet).

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
