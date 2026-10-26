# Changelog

Each version's notes appear on its GitHub Release page.

## 0.10.3

- **DOMINION's own presets.** A **PRESET** button on DOMINION's page (top right, before BACK TO PEDAL) opens a menu of presets for DOMINION alone, apart from PR7-E's rigs. Loading one changes only DOMINION's settings (mode, depth, detail, timing, tilts, stereo, output and its depth-curve bands); it leaves the EQ, COMP, the LIMITER, everything else in the rig, and whether DOMINION is switched on, alone. One undo takes it back.
  - **Factory presets**, by category: *Vocals* (Vocal De-Harsh, Vocal Sibilance, Vocal Mud & Boom), *Drums* (Overheads Ring, Snare Ring, Kick Boom), *Instruments* (Acoustic Guitar, Electric Guitar Fizz, Bass Smoother, Piano & Keys), *Mix & Master* (Mix Bus Gentle, Master Polish) and *Creative* (Hard Carve), plus Default. Our own starting points: the depth curve steers each one to where that kind of problem usually lives (sibilance at 4 kHz and up, boom under 250 Hz on a kick...). They're settings chosen on paper and checked in the tests, not tuned by ear on your material, so treat them as places to start.
  - **Your own:** *Save as...* keeps DOMINION's settings as a preset under "Your presets"; *Delete* removes one of yours; *Show the presets folder* opens "Dominion Presets", beside your rigs (small `.pr7dom` files you can copy to another computer).
  - The button shows the preset's name, with a `*` once you've changed something.
- **DOMINION's band dots and title in the gradient.** The depth-curve band dots (and the dot by the selected band's name) take the red to pink to orange gradient at their frequency, red in the lows to orange in the highs, as the cuts do; the DOMINION title on its page is filled with the gradient too. They were violet.
- **0.10.2 wasn't published:** its Linux build crashed in one of the plugin checks (pluginval's parameter fuzzing), and the same check with the same random seed passes here every time. Its change (the cuts in the gradient) is in this release. A new test now does the same kind of fuzzing inside our own test suite (random settings on half the parameters, changed as audio plays, mono and stereo, 1,500 rounds) and the output stays clean. A build with memory and undefined-behaviour checking switched on (which stops at the first bad memory access) also passes CI's whole check, with CI's own random seed. So the crash isn't something wrong with our code that we could find; it may be in the checker itself or in the CI machine. If it comes back, we'll look again.

## 0.10.2

- **DOMINION's cuts in the menus' colours.** On its graph, what DOMINION is cutting is now drawn in the same red to pink to orange gradient as the menus' highlight (left to right across the spectrum: red in the lows, pink in the middle, orange in the highs) instead of violet. With stereo or mid/side unlinked, the second channel's cuts show fainter behind.

## 0.10.1

- **Menus in the PR7 family's colours.** Every drop-down and right-click menu (DOMINION's band shapes, the EQ's, MODE, CHARACTER, the rig menus...) is now black with white text, as on PR7-C, and the item under the mouse lights up with a red to pink to orange gradient. They were the plain grey menus before.

## 0.10.0

- **DOMINION (stomp 5): dynamic resonance suppression, inspired by oeksound soothe 3.** It finds the parts of the sound that ring, whistle or build up as the track plays, and turns just those down, moment by moment, then lets go. Our own design from soothe's manual (soothe's algorithm isn't published, and the numbers in ours are our choices). Press stomp 5 to switch it on; click its name (or DOMINION in the signal chain) for its page.
  - **How it listens:** a bank of filters 1/12 octave apart, 20 Hz to 20 kHz, each measuring its part of the spectrum. In the tests, a ringing tone 12 dB above pink noise is turned down by about 10 dB at the default settings while the noise around it moves less than 0.2 dB.
  - **MODE:** **Soft** finds what sticks out above the sound around it, so it works the same whatever the level (vocals with loud and quiet phrases). **Hard** works on what's loud (over a level set by DEPTH): firmer, level-dependent, and a sound-design tool when pushed.
  - **DEPTH** (how much), **DETAIL** (broad build-ups like boom and mud, up to narrow ringing), **ATTACK** and **RELEASE** (frequency-dependent: quicker up high, slower down low).
  - **The graph** shows what it's cutting right now, across the spectrum. Over it, the **depth curve**: up to 8 bands (Low Cut, Low Shelf, High Shelf, High Cut, Bell, Band Pass, Band Reject, Tilt) that make it work harder where you raise the curve and leave alone where you lower it. Double-click to add a band (a cut at either end), drag its dot, mouse wheel for Q, double-click a dot to switch it off. Band passes work in parallel: only their parts are processed.
  - **Each band's controls:** SHAPE, FREQ, DEPTH, Q, SLOPE, FOCUS, and **LISTEN** (hold it to hear only what DOMINION takes away in that band's part of the spectrum).
  - **STEREO:** L/R or M/S, **LINK** (100%: both channels listened to together and cut alike; 0%: each on its own) and **FOCUS** (more on one channel).
  - **TILT:** DETAIL, ATTACK and RELEASE for the lows and the highs separately (e.g. a slower attack in the bass to keep its punch).
  - **SCALE:** **MAX CUT** (the most it may cut anywhere) and **WET TRIM**. **OUTPUT:** **DELTA** (hear only what it takes away), **MIX** and **OUT**.
- **Zero latency**, like soothe's low-latency mode: no look-ahead, so it reacts a moment later than it could with one (the trade-off for no delay). Not included: soothe's higher-quality modes (they add delay), sidechain, and multichannel.
- **In the chain:** after the EQ, before COMP. It uses about 11% of one core in stereo.
- **Factory rigs with DOMINION:** Lead Vocal, Overheads, Acoustic Guitar and Mix Harshness Control (gentle settings, with a low cut on the depth curve so the low end is left alone).

## 0.9.0

- **The COMP stomp (2): PR7-C's compressor, ported.** Stomp 2 was the open slot; it's now a full channel compressor, the same as on PR7-C (its code and tests), so it behaves the same. Press stomp 2 to switch it on; click its name for its knobs in the dock.
  - **CHARACTER:** **VCA**, clean and precise (follows loudness, near-hard knee); **OPTO**, smooth, a wide soft knee, gentler attack and a release that stretches the longer it works; **FET**, fast and aggressive (catches every transient), with a touch of grit that grows as it works harder.
  - **THRESHOLD** (-50 to 0 dB), **RATIO** (Soft, then 1.5:1 up to infinite; **Soft** is a curve whose ratio rises with level: about 3:1 at 8 dB over, 6:1 at 20 dB over), **ATTACK** (0.1 to 30 ms), **RELEASE** (50 ms to 3 s), **AUTO** release (quick after a short peak, slower after long compression, so it doesn't pump), **MAKE UP** (up to 20 dB) and **LINK** (both sides together, so the stereo picture stays put).
  - The panel's **GAIN REDUCTION** meter shows how much it's turning down.
- **It comes after the EQ**, so what the EQ cuts doesn't drive it (and a boost does); then OUTPUT and the LIMITER. The signal chain at the bottom now shows EQ, COMP, LIMITER. It listens to the sound itself: PR7-C's sidechain filters aren't part of PR7-E.
- **Factory rigs with COMP:** Lead Vocal (OPTO), Rap Vocal (VCA), Drum Bus (VCA, slow attack, AUTO release) and Mix Bus Polish (VCA, 1.5:1). Their thresholds suit recordings peaking around -10 dBFS: move THRESHOLD for louder or quieter tracks.
- No delay: still zero latency.

## 0.8.0

- **The LIMITER stomp (7): PR7-C's limiter and output trim, ported.** The same limiter as on PR7-C, code and tests, so it behaves the same. Press stomp 7 to switch it on; click its name for its knobs in the dock.
  - **THRESHOLD** (-20 to 0 dB, -1 to start): the ceiling. **MAKE UP** (0 to 12 dB) turns up the limited sound, raising the ceiling with it. **MIX** blends in the unlimited sound (below 100%, some of the original peaks come back). **LINK** turns both sides down together, so the stereo picture stays put.
  - **MODE:** **Brick**, a hard ceiling; **Soft**, which eases in from 3 dB below the threshold and lets go more gently; **Auto**, Brick's ceiling, letting go quickly after short peaks and slowly under steady limiting.
  - **OUTPUT TRIM** (+-12 dB) after it, as on PR7-C: it moves the ceiling with it. It only applies while the stomp is on.
  - The panel's **GAIN REDUCTION** meter shows how much it's turning down, and the stomp shows the ceiling (threshold + make up + trim).
- **It's last in the chain**, after OUTPUT, RIG LEVEL and the volume pedal, so nothing after it can push a peak past the ceiling. Turning OUTPUT up drives into it, as into a mastering limiter.
- **No look-ahead, so no delay** (the same trade-off as on PR7-C): when it limits hard, the very start of each peak is squared off a little. It limits the samples, not the peaks between them, so a converter can still see a peak a fraction of a dB over (the reason for -1 dB rather than 0).
- **The mastering factory rigs** (bank E) now end with it: Brick, a -1 dB ceiling, as a safety net against overs rather than to make them louder.
- The signal chain at the bottom now shows EQ, then LIMITER.

## 0.7.0

- **EQ Match: make your track's tone like a reference's.** Click **MATCH** at the top of the EQ page. It listens to your track and to a reference, compares their overall tone (how much low end, middle and top each has, averaged over time), and proposes EQ bands that make your track sound like the reference. Inspired by Pro-Q 4's EQ Match, as its manual describes it; how ours analyses and picks bands is our own design.
  - **YOUR TRACK:** opening MATCH starts listening (RECORDING). Play the part you want to match for a few seconds or more (longer is steadier). CLEAR starts again. Pauses and silence don't count.
  - **REFERENCE**, three ways: **RECORD INPUT** (move to another part of your song and play it, e.g. last week's vocal take to match today's punch-ins), **LOAD FILE...** (a song: WAV, AIFF, FLAC, Ogg or MP3), or **SAVED...** (a reference saved before). SAVED also saves your track or the reference for another time; they're kept beside your rigs, in "Match References".
  - **On the display:** a thick line where the EQ would need to be to match, and a dashed line for the bands it proposes. **DETAIL** chooses a few broad bands (the overall tone) or more, finer ones. **AMOUNT** chooses how much of the difference to make up: 100% all of it, 50% halfway.
  - **ADD BANDS** adds them as ordinary bands, to change or delete like any others (and switches the EQ on if it was off). One undo takes them all back.
  - It matches tone, not loudness, and it allows for the bands you already have. It doesn't follow the reference where either one has next to nothing (a reference with no deep bass doesn't cut yours away), and no band it proposes goes past 12 dB.
- **In the tests:** noise matched to a file of the same noise through a known EQ comes out within 0.7 dB of the reference's tone, from 50 Hz to 15 kHz.
- **Not included:** listening to another track live (that would need a sidechain, left out for now) and dragging a file onto the plugin.

## 0.6.0

- **Factory rigs: 30 starting points for the EQ**, in the PRESETS tab, filed by what they're for (search their tags, e.g. "vocal" or "dynamic"). Each says what it does in its notes.
  - **Bank A, vocals and voice:** Lead Vocal, Vocal De-Esser, Rap Vocal, Backing Vocals, Spoken Voice (podcasts, voice-overs), Vocal Mud Control. Init stays at A01.
  - **Bank B, drums:** Kick Punch, Kick Boom Control, Snare Crack, Hi-Hat Tame, Overheads, Drum Bus.
  - **Bank C, bass, guitars and keys:** Bass DI, 808 Control, Electric Guitar, Acoustic Guitar, Piano, Pad, Make Room.
  - **Bank D, the full mix:** Mix Bus Polish, Mix Mud Control, Mix Harshness Control, Mix Wider Top, Mix Warmer, Mix Brighter.
  - **Bank E, mastering:** Master Gentle Polish, Master Low-End Control, Master De-Harsh, Master Width, Master Warm Tilt, Master Bright Tilt.
- **What they are:** common mixing moves (cutting rumble and mud, adding presence and air, keeping the bass centred), not copies of anyone's presets. Problems that come and go (sibilance, boomy notes, harsh moments) use dynamic bands with AUTO, so they only act when the problem is there. No rig boosts more than 4.5 dB anywhere; the mastering rigs move 2 dB at most. Every track is different: load one, then adjust by ear (and save your version with SAVE).
- **Not yet:** the mastering rigs are EQ only. A limiter comes with stomp 7.
- Your own rigs in bank A now file from A08 (Init and the vocal rigs take A01 to A07). Rigs you've already saved keep their places; a factory rig shows first if one shares a slot.

## 0.5.0

- **Dynamic EQ.** Any bell, shelf or tilt band can now be **dynamic**: its gain moves only when its own part of the sound gets loud, like a compressor working on just that part of the spectrum. Inspired by Pro-Q 4's dynamic EQ, as its manual describes it; how ours listens and reacts is our own design (FabFilter doesn't publish theirs).
  - **DYN RANGE** (beside GAIN): how far the band moves, from -30 to +30 dB. **Below 0 it cuts** when its part gets loud: tame a harsh note, sibilance, a boomy bass note, only when they happen. **Above 0 it boosts**: bring out a snare's crack or a pick's attack. 0 is an ordinary band. A band can have both: say +6 dB of gain that falls back to level when its part gets loud.
  - **It listens to its own part of the spectrum** (the same part SOLO plays), on the channel it works on: a loud bass note doesn't set off a 5 kHz de-esser, and a left-only band listens to the left.
  - **AUTO** (on by default): the threshold sits 6 dB above the band's usual level over the last few seconds, so it acts on what stands out, whatever the track's level. Switch AUTO off to set **THRESHOLD** yourself.
  - **ATTACK** and **RELEASE**: at 50% they're set from the band's frequency (low bands need longer to measure a note: 2 ms high up, up to 50 ms in the lows; release 15 times that). Lower is faster, higher slower, from a quarter to four times.
  - **On the display:** a dynamic band shows a dashed curve where its whole range would take it, a handle there to drag, and a white ring where its gain is right now; the curves move as it works. **Alt+double-click** makes a dynamic band, **Alt + mouse wheel** sets the range, and the right-click menu has **Make dynamic / Make static**. While a band is dynamic its controls get a second row: AUTO, THRESHOLD, ATTACK, RELEASE, and a meter of what it hears (against its threshold) and how far it's moving.
  - Moving it to and from dynamic glides, without clicks. Cutting a held low note adds no audible distortion (the harmonics measure 87 dB down at the fastest attack).
- **Not yet:** a sidechain input (a band listening to another track) isn't included. AUTO GAIN works from the bands' set gains, not their dynamic moves. EQ Match comes next.

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
