# Changelog

Each version's notes appear on its GitHub Release page.

## 0.4.0

- **The EQ stomp: a new EQ, inspired by FabFilter Pro-Q 4.** It replaces the 9-band graphic EQ. Our own design, from Pro-Q's manual and published filter methods (not FabFilter's code). Click stomp 4's name (or OPEN EQ in the dock) for its page.
  - **Up to 24 bands**, each a **Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt or All Pass**, with **slopes from 6 to 96 dB/oct** for the cuts and shelves.
  - **Stereo, Left, Right, Mid or Side, per band:** for example, lift only the sides' top end, or cut the middle's low mud.
  - **Analog-matched, zero latency.** Most digital EQs squash curves near the top: a +12 dB bell at 15 kHz comes out up to 7.5 dB off its analog shape. These stay within about 1 dB, and exact at each band's own frequency (within 0.3 dB at 96 kHz). Bands can sit above 20 kHz, up to 30 kHz, where only their slope reaches the audible range.
  - **The page:** a big display with the curve, each band's shape, and a spectrum of what goes in (shaded) and comes out (a line). Double-click to add a band (a bell, or a cut at either end); drag its dot for frequency and gain; mouse wheel for Q (or the slope of a cut); Alt-click to switch it off; right-click for its menu; Delete to remove it. Below, the selected band's controls, and **SOLO** (hold it to hear only what that band works on).
  - **OUTPUT, GAIN SCALE** (0% to 200% of every band's gain) and **AUTO GAIN** (turns the level down by about as much as the curve turns it up, so you can judge the EQ by ear, not by loudness; an estimate from the curve, as in Pro-Q).
  - **No clicks or zipper noise:** moving a band glides, and changing what a band is (its shape, slope or placement, or switching it) crossfades.
  - The dock shows the EQ's curve in place of the graphic EQ.
- **Projects from 0.3.0** with the graphic EQ on come back with the new EQ on and flat (the graphic EQ's settings are gone).
- **Not yet:** dynamic EQ and EQ Match come next. Not planned: linear phase and the character modes.

## 0.3.0

- **Stereo stays stereo.** Until now PR7-E played a stereo track's left channel on both sides, even with everything switched off: the effects that came from PR7-B were built for a mono instrument. Now both channels pass through exactly as they came in, apart from what the stomps do to them, and BYPASS keeps both channels too. A mono track still plays on both speakers.
- **PR7-B's old effects are gone:** pitch, comp / gate, filter / pitch, glitch, modulation (AEON), delay, reverb and lo-fi. PR7-E is a utility processor: input, its seven stomps, output.
  - The signal chain at the bottom now shows just the EQ, and grows as the stomps are built.
  - The graphic EQ (stomp 4, for now) and PRESENCE work on each channel separately.
  - The expression pedal can be a **VOLUME** pedal. The effects it could also control are gone, and a project or rig that had the pedal on one of them now loads with the pedal unassigned (rather than as a volume pedal sitting at the heel, which would be silent).
- **Next:** the new EQ, inspired by FabFilter Pro-Q 4, on a big EQ page of its own, replacing the graphic EQ.

## 0.2.0

- **The TUNER stomp checks the pitch of recorded parts.** Built for checking a vocal or a bass line as it plays back, rather than for tuning an instrument. One part at a time, up to 1,500 Hz (a bass's lowest notes to a high vocal).
- **Its panel:** click TUNER's name (stomp 1) and the dock shows:
  - **The current note**, how many cents off it is, and its frequency.
  - **A trace of the last 30 seconds:** the pitch drawn against the notes, so you can see where a note goes flat, slides in, wavers or drifts. Each note is coloured by where it centres: green within 5 cents, orange within 15, red beyond.
  - **The average:** how far the notes in the trace sit from the nearest note, e.g. "+12 cents, sharp", and over how many seconds of notes. It answers "is this part in tune overall?" A part sitting halfway between two notes reads as 50 cents out, not as "in tune".
  - **RESET** empties the trace and starts the average again, e.g. just before the passage you want to check.
- **It listens to both channels.** A stereo track is heard as left and right together, so a part panned anywhere is measured. (It used to hear only the left channel.)
- **It never changes the sound.** The tuner's MUTE is gone, from SETTINGS too: when checking a recording you want to hear it. On the unit, the MUTE key is now **RESET**. A project saved with 0.1.0 with the tuner on and muting now plays out loud.
- **It keeps listening with the plugin window closed:** play the passage, then open the window and the trace is there. It follows the audio's own time, so when the DAW stops, the trace stops too, instead of filling with the last note.
- The reference pitch (A4 = 440 Hz unless you change it in SETTINGS) applies to everything above: a recording tuned to A = 432 Hz reads 32 cents flat against 440, or in tune against 432.

## 0.1.0

- **The utility processor, based on Paragon PR7-B 0.11.0.** The first release of Paragon PR7-E, the PR7 family's utility processor.
- **Its own plugin.** Paragon PR7-E has its own name, ID and plugin code, so your DAW sees it as a separate plugin: it installs alongside PR7-G, PR7-B, PR7-V and PR7-C instead of replacing any of them. It keeps its own rigs (in a "Paragon PR7-E" folder) and remembers its own finish. DAWs file it under Tools.
- **The -E mark.** The logo reads PR7-E, in the same slanted, speed-cut lettering as its siblings, and the badge says UTILITY PROCESSOR.
- **Seven stomps, three over four.** The top row is **1 Tuner, 2 Open, 3 Inserts**; the bottom row **4 EQ, 5 Resonance, 6 Auto EQ, 7 Limiter**, laid out like PR7-V, with each top stomp sitting between two below it.
  - **TUNER** works now: it's the tuner PR7-B had.
  - **EQ** switches the 9-band graphic EQ for now, until the planned EQ (inspired by FabFilter Pro-Q 4) takes its place.
  - **OPEN, INSERTS, RESONANCE, AUTO EQ and LIMITER** are still being planned. Their lights stay off and pressing them does nothing yet. Their names are placeholders too.
- **No bass blocks.** The NAM amp, the cabinet IR loader, the AMP, CABINET and RIG panels, and the OD / DISTORTION stomp (OBSIDIAN and GOTHAM) are gone, along with PR7-B's bass factory rigs. One factory rig, **Init** (everything off), is the starting point until PR7-E has its own.
  - Nothing in the chain looks ahead any more, so PR7-E adds no latency, and it uses about a quarter of the CPU PR7-B did.
- **Still here from PR7-B:** pitch, comp / gate, filter / pitch, graphic EQ, glitch, modulation (with AEON), delay, reverb and lo-fi, in the signal chain at the bottom (click a block for its knobs), plus the expression pedal, tap tempo, undo and rigs.
