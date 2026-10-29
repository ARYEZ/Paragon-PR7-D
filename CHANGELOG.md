# Changelog

Each version's notes appear on its GitHub Release page.

## 0.24.0

**This release also brings everything in 0.23.0** (the sample editor: drawn envelopes, GAIN, MONO / POLY, DELAY with the anchor, LP / HP; preset A42 Tight Kit; and MIDI export on whole ticks). 0.23.0 was never published: its build passed every test, but GitHub's Linux plugin check (pluginval) crashed twice in a row while shutting down after its last test. It passed 12 out of 12 runs here under a debugger, and the crash is in pluginval's own Linux host, so it was released as part of this one instead. Its notes are below.

Stage 6, part 3 (second half): the rest of the drum section from Avenger's manual (page 67), as you chose. **Stage 6 is now complete.** Next come the bigger kit (your option 2: ride, a second crash, a china and more) and then the groove player.

- **Save and load kits and sequences:**
  - **KITS** on the kit's title line saves the whole kit as a file: every drum's settings, samples and drawn envelope, plus the kit's LEVEL and VELOCITY. It goes in the presets folder's "Drum Kits" folder. The same menu loads kits back and shows the folder.
  - **SEQUENCES** does the same for the drum sequencer: all four patterns and SPEED, TIMING, LENGTH and VEL AMOUNT, in "Drum Sequences".
  - **LINK** (on at first): saving or loading a kit takes the sequence of the same name with it, and the other way round, so a song's kit and beat travel together.
  - A load is one undo step. It leaves the synth, the effects and how the kit is played (DRUMS ON, ROOT) alone.
  - The buttons show the kit and sequence last loaded or saved.
  - SHUFFLE isn't saved with a sequence, because it's the synth's one swing setting, shared with the arpeggiator.
- **Several drums at once:**
  - CTRL-click rows to add or remove drums; SHIFT-click to choose a run of them. The chosen drums are outlined, and the one shown brightest.
  - A change to the bottom row (CHOKE, INSERT, TO DELAY, TO REVERB, MONO, DELAY, LP, HP) then goes to every chosen drum. For example, a high-pass on all the cymbals in one move.
  - Only changes you make on the page are copied. Automation from your song still moves just its own drum.
- **< >** next to LOAD SAMPLE load the previous or next audio file from the same folder, so you can run through a folder of kicks one by one.
  - Files go in number order ("Kick 2" before "Kick 10"), as sample packs number them.
  - Each new file plays whole.
  - Samples now remember which file they came from, in presets and projects too. Samples loaded before 0.24.0 don't know their file, so load them again once to use < >.
- **Playheads:** a line moves across each kit row's picture while that drum plays.
- **Tests:**
  - kits and sequences surviving saving and loading;
  - what a kit or sequence load changes and what it leaves alone;
  - LINK both ways;
  - undo;
  - names cleaned for the file system;
  - < > in number order, round and round;
  - several drums getting a page change but not automation;
  - the row playhead following a hit.

## 0.23.0

Stage 6, part 3 (first half): **THE SAMPLE EDITOR**, inspired by Avenger's drum sample editor (its manual's page 66). As you chose, it works on **every slot**, the drum synths as well as samples. Kit and sequence saving, choosing several drums at once, the < > sample arrows and a playhead in the rows come next, in 0.24.0.

- **Where:** the DRUMS page's DRUM view, for the chosen drum.
- **ENVELOPE:** the drum's level across its hit, drawn in a strip above the hit's picture, as on Avenger.
  - It works like the MOD page's envelopes: drag a point; drag a line up or down to curve it; double-click to add a point, or on a point to delete it.
  - Right-click for ready shapes: flat, back to front, upside down, fade in, fade out, a gate (cut halfway), a swell.
  - **A drum synth's** envelope spans its hit, from the start to where it has died away, lined up under the picture. Change DECAY and the hit gets longer or shorter, and the envelope stretches with it.
  - **A sample's** envelope spans START to END and follows the sample itself, so it stays put when you change the pitch. The file's picture draws the curve over that part.
  - New, it's flat at the top, which changes nothing (tested: exactly the same sound as before). Drawn down to nothing, the hit ends there.
- **GAIN:** more (or less) level than VOLUME gives, up to ±24 dB, for samples recorded very loud or very quiet.
- **MONO / POLY:** MONO cuts the drum's last hit short (a 2 ms fade) when a new one starts. POLY, the default, lets fast hits like a snare roll ring on over each other.
- **DELAY:**
  - **Right of centre:** the hit comes that much later, up to 1 s.
  - **Left of centre:** the **anchor**, a green line on the picture. The sequencer starts the hit that much early, so that point of it lands on the step: a clap's main slap after its flams, the peak of a swoosh.
  - **EXPORT MIDI** writes those drums early too, so played back from a track the anchor still lands on the beat.
  - As you chose, **keys you play** still start the hit at once (nothing can start in the past).
- **LP and HP:** a simple 6 dB-an-octave low-pass and high-pass per drum, as on Avenger: damp a kick, thin the hats.
  - My first version's HP came out 6 dB down at its own setting when set high (5 kHz), instead of the expected 3. The filters are now the kind that are exact at any setting (tested at 1 kHz and 5 kHz).
- **Not repeated from Avenger:** its fine PITCH (ours already goes to hundredths of a semitone) and SPIKE (our SNAP adds a spike above its middle on a sample).
- **New parameters** for each drum: GAIN, MONO, DELAY, LP, HP (automatable). The envelopes are kept in the preset, the project and undo, like the MOD page's.
- **A 0.22.0 fix:** a reversed drum synth had its VOLUME applied twice (the recording it plays backwards had VOLUME in it). Only noticeable with VOLUME away from 0 dB; fixed.
- **New preset A42, Tight Kit:**
  - A41's beat and bass;
  - the clap anchored 18 ms in, alongside the snare;
  - the open hat cut short by its envelope;
  - the kick MONO and a little darker;
  - the hats thinned with HP.
  - Our own settings, not tuned by ear.
- **Tests:**
  - what each control does to the sound:
    - the envelope's level a quarter of the way through a sample, and where it ends (also an octave up);
    - a drum synth's silent first half;
    - GAIN's level;
    - MONO against POLY on overlapping hits;
    - DELAY to the sample, and the anchor's start in the sequencer;
    - LP and HP at their settings and five times past them;
  - presets, the project and undo keeping the envelopes;
  - the page's envelope menu;
  - the strip lining up with the picture.
  - I can't listen here: how the shaped drums sound is for your ears.

## 0.22.0

Stage 6, part 2: **THE DRUM SEQUENCER**, beats for the kit, inspired by Avenger's drum sequencer (its manual's pages 81-82). Our own design and numbers where the manual gives none.

- **Where:** the DRUMS page's right-hand panel now switches between **DRUM** (the chosen drum, as before) and **SEQUENCER**.
- **The grid:** a row for each of the kit's 12 drums, level with the kit's rows on the left, so you read each row's drum off the kit list. Steps run left to right, lines brighter on each beat and each bar.
  - **PEN:** click to add a note; drag along a row to add several; drag notes to move them (hold ALT to place them off the grid, in 16ths of a step); right-click a note to remove it.
  - **SELECT:** drag a frame round notes to choose them. SHIFT adds to the choice; DELETE removes the chosen notes; CTRL+A chooses all.
  - Adding or clicking a note plays its drum and chooses it on the left.
  - A line shows where the beat is playing.
  - Notes are drawn brighter the harder they hit; rolls show as ticks; reversed notes are amber with a backwards arrow; a short gate is a line under the note.
- **Four patterns, A to D,** each up to 16 beats, polyphonic (any number of drums on one step).
- **The sequencer's settings:**
  - **TRIGGER SONG:** the chosen pattern plays while your song plays, in time with its bars (it counts from the song's start, so jumping around the song keeps it in place). Stopped, it's silent.
  - **TRIGGER KEY** (your choice): the four keys just below the kit play patterns A to D while held (with the kit on C2: G#1 A, A1 B, A#1 C, B1 D). The pattern starts from the key; pressing another while one is held switches to it on the same clock; letting go goes back to the one still held, or stops. Those four keys no longer play the synth while KEY is on. The kit's own 12 keys still play single drums over the beat.
  - **SPEED** (1/16 is four steps a beat) and **TIMING** (straight, triplet, dotted), **LENGTH** (1 to 16 beats).
  - **SHUFFLE:** every second step late, up to half a step. It's the synth's one SHUFFLE, shared with the arpeggiator and the step sequencer. The grid shows notes on their steps, not shuffled.
  - **VEL AMOUNT** (KEY): how much the key's velocity scales the pattern's.
- **Each note's own settings** (the chosen notes, in the CHOSEN NOTES box: drag up and down, double-click for the default):
  - **VELOCITY** (the level goes with its square: half is 12 dB down; drawn velocities always count, whatever the kit's VELOCITY knob says), **PAN** (from the drum's own place), **PITCH** (semitones from the drum's).
  - **GATE:** RING lets the hit ring out; lower cuts it after that much of a beat (an open hat closed early), with a 3 ms fade.
  - **ROLL:** the hit 2, 3 (a triplet), 4, 6 or 8 times within its step.
  - **REVERSE:** the hit backwards. **END SYNC** starts it early so that it ends where its step does (a swell into the beat). A SAMPLE slot plays its file (START to END) backwards; a drum synth plays a recording of its hit made when you set the note (with its pitch, decay, tone and snap in it; made again when you change them).
- **EXPORT MIDI** (your choice): click to save the pattern as a MIDI file, or drag the button onto a track. One time through, on the kit's keys, at the song's tempo. Velocities and rolls are written; pan, pitch and reverse can't be as MIDI notes, so they aren't.
- **Saved with the preset**, the project and undo, like the arpeggiator's patterns. New parameters: ON, TRIGGER, PATTERN, SPEED, TIMING, LENGTH, VEL AMOUNT (all automatable). Old presets load with the sequencer off and empty.
- **New preset A41, Beat And Bass:** A40's kit and bass with four beats: A straight, B busier (ghost snares, sixteenth hats), C half time, D a fill with a tom roll and a reversed cymbal into the next bar. Our own beats, not tuned by ear.
- **Tests** check that each hit lands on the exact sample it should (at 120 and 90 BPM, from any song position, with shuffle, rolls, triplet speed and end-synced reverses), each note setting's effect on the sound (level, left/right, pitch by counting the waveform, where a gate cuts, a reversed hit swelling to its end), the KEY keys starting, switching and stopping patterns without reaching the synth, the patterns kept by presets, the project and undo, the page's editing, and the MIDI file's notes and timing. I can't listen here: how the beats groove is for your ears.
- **Not built yet:** copying a pattern to another, and clearing one at a stroke (choose all, DELETE does it for now). Next: 6c, the sample editor (drawn envelope, filters, more).

## 0.21.1

**Choose one hit out of a sample** (your ask): many sample packs put several versions of a drum in one file, such as a kick played nine ways. Now you can play just the one you want.

- **The DRUM picture shows the whole file** when a slot is a SAMPLE, with each hit in it found and numbered (1, 2, 3...) along the top.
- **Click a hit to choose it:** the slot plays from that hit's start to just before the next one. The part that plays is bright, the rest dimmed, and the title says which it is ("0.45 s of 4.0 s (hit 4 of 9)"). You hear it straight away.
- **Fine-tune with the S (start) and E (end) handles:** drag them anywhere.
- **WHOLE FILE** plays the whole sample again.
- **A cut end fades over its last 3 ms**, so it doesn't click; the start isn't faded, so the hit's attack stays sharp (SNAP can still soften it).
- The choice is two new settings per slot, **SAMPLE START** and **SAMPLE END**. They're saved with the preset, can be undone, and can be automated. Loading a new file resets them to the whole file.
- **How the hits are found:** where the level jumps well above the quiet just before it, at least 40 ms after the last hit, moved back to where the hit's rise begins. In a test file of nine kicks, each was found within a millisecond of where it really starts.
  - Hits that run into each other with no gap may not all be found. Then drag S and E by hand.
- This is the START / END part of the sample editor (6c), brought forward because you need it now.

## 0.21.0

Stage 6, part 1: **THE DRUMS**, a kit of 12 drums on 12 keys, each one of our own drum synths or a sample you load, with its own page. (The drum sequencer is part 2, the sample editor part 3: the order you chose.)

- **How it plays:**
  - **DRUMS ON** (on the new **DRUMS** page) gives the 12 keys from **ROOT** (C2 to B2 at first) to the kit, in the General MIDI order Avenger uses: C the kick, C# the rim, D the snare, D# the clap, the toms on F, G and A, the hats on F#, G# and A#, the cymbal on B. Every other key still plays the synth, so a kit and a bass or lead share one preset.
  - The kit's keys are tinted on the on-screen keyboard while it's on.
  - **Off by default:** nothing changes until you switch it on. With the kit on, the notes outside its keys sound exactly as before (tested, sample for sample).
  - The hits are one-shots: letting go of the key doesn't cut them (as on a drum machine).
  - **VELOCITY** sets how much your playing changes the level: at 100%, a half-velocity hit is 12 dB softer; at 0 every hit is the same.
- **Each slot: one of nine drum synths, or SAMPLE.** The synths are our own designs from the classic analog ideas, not copies of any drum machine's circuit:
  - KICK: a sine whose pitch falls to 52 Hz, with a beater click;
  - SNARE: two drumhead tones and the wires (filtered noise);
  - CLAP: noise in three quick slaps and a ring;
  - CLOSED HAT, OPEN HAT and CYMBAL: six square waves at clashing pitches (that's the metal) plus noise, high-passed;
  - TOM: a falling sine with a stick click;
  - RIM: a short high and low ring;
  - COWBELL: two square waves through a band-pass.
- **Each slot's knobs:**
  - **PITCH** in semitones (and hundredths), **VOLUME**, **PAN**.
  - **DECAY:** how long it rings.
  - **TONE:** its brightness. On KICK and TOM, how far the pitch drops at the hit.
  - **SNAP:** its attack: the beater, the wires, the slaps, the sizzle.
  - **MUTE** and **SOLO** on each row.
  - **CHOKE:** a hit stops the other drums in its group. The closed and pedal hats stop the open hat, as on a real hi-hat.
- **SAMPLE: your own sounds.** Click LOAD SAMPLE, or drop a WAV, AIFF or FLAC on a drum's row or on its picture.
  - Mono or stereo, any sample rate, up to 30 seconds. It's saved inside the preset (and the project), so it plays on another computer.
  - DECAY, TONE and SNAP sit neutral at the middle on a sample. Below the middle, DECAY cuts it shorter, TONE darkens it and SNAP softens its attack; above, TONE thins it and SNAP adds a spike.
  - Loading into a drum-synth slot sets those to neutral, so the sample plays as recorded.
  - Samples from your Avenger folder work like any WAV. None ship with PR7-K: they're Vengeance's.
- **Routing, like the oscillators:** each drum goes straight into the mix (with its own TO DELAY and TO REVERB sends) or through insert bus A to D. So you could run the snare through a bus with a DRIVE and leave the kick clean.
- **The DRUMS page** (a new tab between ARP and FX):
  - **On the left, the KIT:** ON, ROOT, LEVEL, VELOCITY, then a row per drum: its key, its name, a picture of its hit, MUTE and SOLO. A row lights when its drum plays; click it to choose that drum and hear it.
  - **On the right, the chosen DRUM:** what it is, PLAY, LOAD SAMPLE and CLEAR, a large picture of its hit (drawn from a copy of it, as DRIVE's page does, with its length in seconds), its knobs, then CHOKE, INSERT and its sends.
- **A new factory preset: Kit And Bass** (A40). The kit on C2 to B2, a saw bass above. A small room on everything but the kick and the bass. Our own settings, not tuned by ear.
- **Levels and CPU, measured:** every drum peaks at about -3 dBFS at its default settings. The whole kit hit on every sixteenth note at 120 BPM costs about 4% of a core.
  - On the way I found and fixed a slowdown: fading envelopes were sinking into "denormal" numbers, which most processors handle very slowly. Fixed before release.
- I can't listen here. The drum synths' character is the part most worth your ears: tell me which drums sound wrong and how, and I'll reshape them.

## 0.20.2

**COSMONAUT runs at 4x instead of 8x: about 40% less CPU** (your choice, after 0.20.1).

- **What changed:** the drives work out their circuits at several times the sample rate, so the new high harmonics that distortion makes don't fold back down as harsh, out-of-tune tones (aliasing). COSMONAUT now works at 4 times the rate instead of 8. The other drives stay at 8x.
- **What I measured before changing it:** at 4x, COSMONAUT's aliasing was the same as at 8x (both under my measurement's floor, about 47 dB down), and its harmonics were within 0.2 dB. PR7-G's own COSMONAUT test (odd harmonics, the scooped mids) still passes.
- **Not sample-identical to 8x:** it's a different oversampling filter, so the output differs very slightly, mostly at the very top of the range. I can't listen here; if COSMONAUT sounds different to you, tell me and it goes back to 8x.
- **CPU on a bus (both sides), a played saw chord:** COSMONAUT 30% → about 17%. DRIVE's page shows the new figure.
- **Not changed:** RABID and ABYSS aliased audibly more at 4x, and CHIMERA moved one high harmonic by 6 dB, so they stay at 8x. HALO is as before (its fix is still offered).
- **PR7-G** still runs COSMONAUT at 8x; if you want this there too, it goes in PR7-G's own chat.

## 0.20.1

**Faster drives: RABID, CHIMERA, COSMONAUT and ABYSS use about a third less CPU, with exactly the same sound.**

- **What changed:** these four circuits model their diodes, and every step of the circuit solve needed two slow maths functions per diode (sinh and cosh). Both come from the same exponential, so now it's worked out once and both are made from it. Nothing else changed.
- **Same sound, checked:** I ran each changed drive against PR7-G's original code on saw chords, quiet and loud, at low and high DRIVE: the output is identical, sample for sample (not just close).
- **CPU on a bus (both sides, % of one core), a played saw chord, before and after:**

  | Model | 0.20.0 | 0.20.1 |
  |---|---|---|
  | RABID | 17% | 11% |
  | CHIMERA | 17% | 11% |
  | ABYSS | 22% | 18% |
  | COSMONAUT | 43% | 30% |
  | BANSHEE, SKYLINE | about 3.5% | unchanged |
  | HALO | about 33% | unchanged |

- **DRIVE's page now quotes these figures** (a played chord; 0.20.0's were measured on loud noise, the worst case). HALO's also says it can climb to about **120% on loud, bright sound**.
- **HALO: not sped up, and why.** HALO solves the whole fuzz circuit at once, and on loud, bright input (loud high notes, noise) its solver often doesn't settle within the steps it's given. That's what makes it heavy there, and it leaves a faint grainy residue in the sound, about 16 dB below the note on a loud high sine. Letting it take more steps fixes most of that but changes HALO's sound on those notes (and costs more), so it isn't a pure speed-up. I've left HALO exactly as PR7-G has it; fixing it is offered as a choice for both plugins, since it needs your ears.

## 0.20.0

Stage 7, part 4: **THE INSERTS**: four insert buses, A to D, each running PR7-G's **DRIVE** (all seven pedals), **GLITCH**, **MOD** and **LO-FI** on the oscillators you send to it. This is the insert-bus design you chose. It completes stage 7's effects.

- **How it works** (like a mixer's subgroups):
  - Each oscillator has an **INSERT**: NONE (straight into the mix, as before) or bus A, B, C or D. It's on the WAVE page (OUT, beside its sends) and on the new INSERTS view.
  - A bus runs DRIVE, then GLITCH, MOD and LO-FI on everything routed to it, after those oscillators' filter, AMP and pan. So a drive works on that oscillator's whole chord, as a pedal would.
  - Several oscillators can share a bus. Two oscillators on different buses never touch each other: a drive on A leaves B's oscillator clean (tested).
  - **One rule to remember:** a bus feeds DELAY and REVERB with its own two send knobs, after its effects, so the echoes carry the drive. An oscillator on a bus therefore uses the bus's sends, not its own. That's how a mixer's subgroup works, and it's the reason echoes of a distorted sound are distorted too.
  - This is also the way to put a MOD before the delay and reverb (the whole-sound MOD on the MOD / LO-FI view comes after them).
- **The FX page's new INSERTS tab** (first, as the sound goes through it):
  - **The bus strip:** BUS A to D, the oscillators on the bus shown (click 1 to 8 to route one here, again to take it off), and the bus's sends TO DELAY and TO REVERB.
  - **Four rack units for that bus:**
    - **DRIVE:** BANSHEE / RABID / COSMONAUT keys (the others on its page), DRIVE and LEVEL knobs, the bus's level on the ladder.
    - **GLITCH:** SEQUENCE / INTERFERE / KILLSWITCH keys, DENSITY and MIX knobs, LENGTH in big digits.
    - **MOD and LO-FI:** as on the MOD / LO-FI view.
  - **DRIVE's page:** its screen runs a separate copy of the chosen pedal, at your settings, on a 120 Hz sine. It shows one cycle in and out, the harmonics it adds, and what the model costs on a bus. The controls are the model's own: SKYLINE's ATTACK and GATE, ABYSS's EQ, and no TONE on HALO (the original has none).
  - **GLITCH's page:** a separate copy chops a plain riff of eighth notes at the song's tempo, so you see what it will do: the level in and out over two bars.
- **The drives and GLITCH are PR7-G 0.52.0's own code, unchanged.** I checked all seven drives against PR7-G's code, sample for sample: identical (one case differed by 0.000000007, rounding).
  - **Stereo:** they're mono pedals on PR7-G, so here each bus runs one per side. GLITCH's two copies make the same random choices, so both sides chop together. A sound on the left stays on the left through everything on a bus (tested).
- **One addition: a DC blocker after each bus's DRIVE** (5 Hz, only while the drive is on).
  - **Why:** RABID's circuit can leave its coupling capacitor charged after loud playing, a steady offset that never drains. On PR7-G the amp after the pedal blocks it; here nothing would. A real pedal's output capacitor does the same job.
  - It affects only what's below 5 Hz. Switched off, it fades out in about 50 ms (no click).
- **CPU, measured here: the drives are heavy, HALO very heavy.** PR7-G's drives solve their circuits part by part at 8 times the sample rate, and a stereo bus runs two. On a bus (both sides):

  | Model | CPU on a bus (% of one core) |
  |---|---|
  | SKYLINE | about 3% |
  | BANSHEE | about 4% |
  | CHIMERA | about 22% |
  | RABID | about 24% |
  | ABYSS | about 28% |
  | COSMONAUT | about 38% |
  | **HALO** | **about 124%: very heavy** |

  - **HALO** solves its whole germanium fuzz circuit at once, every sample. It already costs about 62% on PR7-G, which is mono; here, in stereo, it can be enough to cause dropouts.
  - DRIVE's page shows each model's cost, in red for HALO.
  - As you chose, they ship exactly as on PR7-G. A separate pass will speed up the drive circuits for PR7-G and PR7-K together. Faster solving can change the sound slightly on very loud, bright input (as I found with AEON in 0.19.0), so you'll want to check it by ear.
  - GLITCH, MOD and LO-FI are light.
  - **A bus nothing is routed to costs nothing**, whatever is switched on in it (tested).
- **Nothing changes until you use them:** with every oscillator on NONE, the sound is exactly as in 0.19.0, sample for sample. An oscillator on a bus with its effects off sounds as it did.
- **A new factory preset: Driven Lead** (A39).
  - A saw lead on bus A through RABID and a CARBONITE chorus, with ping-pong echoes taken after the drive.
  - A sine sub an octave down goes straight into the mix, so the low end stays clean.
  - Our own settings, not tuned by ear.
- **In the tests:**
  - PR7-G's own drive and GLITCH tests, run as they are (through a stand-in for PR7-G's plugin, using bus A's settings). Left out: GLITCH's expression pedal, its PRE / POST, and PR7-G's noise gate, none of which PR7-K has.
  - And, from the sound:
    - unused buses change nothing;
    - a drive works only on its oscillators (measured by what the drive adds, with a windowed measurement);
    - a bus's sends carry the driven sound into the echoes;
    - left stays left through everything on a bus;
    - buses are independent of each other;
    - no offset is left behind;
    - each model's CPU, the page, DRIVE's screen (BANSHEE's odd harmonics, HALO's strong 2nd), and the new preset.
- **Left in PR7-G,** as planned: the NAM amp and cab, the expression-pedal wah, the talk box and the XP-100 bends. Ask if you want any of them.
- **Next:** stage 6, the drums (you asked for them after stage 7). Then the drive speed-up pass, and in time the `.avxp2` import.
- I can't listen here. If a drive sounds different from PR7-G's, tell me which one and what you hear.

## 0.19.0

Stage 7, part 3: **MOD, MOD 2 and LO-FI**: PR7-G's modulation block (twice, as on PR7-G) and its lo-fi, ported from PR7-G 0.52.0 as you asked. The same code, with two kinds of change, both explained below: each side now kept on its own, and AEON made cheaper to run.

- **The FX page has a third view,** between SENDS and MASTER, in the order the sound goes through them:
  - **MOD / LO-FI:** three rack units, MOD, MOD 2 and LO-FI.
- **Where they sit:** after the sends come back, before the MASTER rack.
  - So the echoes and the reverb are chorused, panned or sent "through the phone" with everything else, as PR7-G's LO-FI does (it's last there, after the reverb).
  - The LIMITER still catches whatever comes out of them.
  - **One trade-off:** a tremolo or auto-pan chops the reverb's tail too. On PR7-G the modulation comes *before* the delay and reverb. Here the delay and reverb are sends taken from each oscillator inside the synth, so there's no single point "before" them to insert into. Effects on single oscillators, before their sends, are coming in 7d (inserts).
- **MOD and MOD 2: all twelve of PR7-G's types:**
  - Vortex, AEON, Hadron, Razor, Prism, Nebula, Rotary, Vibrato, Harmonic Tremolo, Auto-Pan, Doubler, Carbonite.
  - Each has PR7-G's own controls: SYNC to the song, AEON's MANUAL and RES, Hadron's SCRIPT / BLOCK, Razor's WAVE, the Vortex's EQ and STEREO.
  - MOD 2 starts on AEON, to pair with MOD's Vortex, as on PR7-G.
- **On each unit:**
  - **MOD and MOD 2:** < TYPE / TYPE > and SYNC keys, RATE and DEPTH knobs, the rate in big digits, the type's own settings beside it, and a ladder that rises and falls with the sweep.
  - **LO-FI:** TELEPHONE, CASSETTE and BITCRUSHER keys, AMOUNT and MIX knobs, the band it lets through (or the bitcrusher's bits and rate), and its output on the ladder.
- **On each page:**
  - **MOD:** the next two cycles of what the sweep moves, scrolling as it plays (the chip clock, the delay, the volume, the pan...), and only the controls the chosen type uses, as on PR7-G's panel.
  - **LO-FI:** its filters' response from 20 Hz to 20 kHz, or for the bitcrusher, a sine as it comes out, stepped and held.
- **Stereo, kept.** PR7-G's chain is mono up to its modulation, so a few types there summed or crossed the two sides. Here each side runs on its own:
  - **AEON:** one pedal per side, sweeping in step.
  - **Rotary:** one cabinet per side, the rotors turning together.
  - **Doubler:** each side's take comes from that side.
  - **LO-FI's TELEPHONE, AM RADIO and MEGAPHONE:** each side through its own speaker. They're mono on PR7-G, as real phones and radios are. NOISE is still one hiss, in the middle.
  - **The Vortex's STEREO** needed an adaptation of our own. On the pedal it puts the chorus alone on the left and the dry sound alone on the right. That needs a single input, and the synth's sound is already stereo. So here each side keeps its own sound, and its chorus is added on the left and taken away on the right. The chorus still sits between the speakers, and summed to mono it folds back to the dry sound.
  - **Checked against PR7-G's own code, sample for sample:** for a sound in the middle, every type comes out exactly as on PR7-G. A sound on the left stays exactly silent on the right.
- **AEON made cheaper (about 45% less CPU).**
  - PR7-G's AEON models the pedal's circuit part by part, at twice the sample rate. Two of them (one a side) cost about 17% of a core here.
  - I changed only how the maths is done, not what it computes: the op-amps' clipping curve with one power instead of two, the diode limiter's sinh and cosh from one shared exp, and three sums that never changed worked out once.
  - Now about 9%. The same sound: bit for bit identical to PR7-G's on saws, from quiet to full scale.
- **Something I found in PR7-G's AEON (and so here too):** very loud, very bright input becomes unpredictable inside its diode limiter.
  - For example, white noise above about a third of full scale, or a full-scale saw at 1760 Hz.
  - A one-in-ten-million change to the input then changes the output completely. It can come out much louder than it went in (white noise at 0.3 came out peaking near 1.9).
  - The cause: the limiter's solver takes four fixed steps per sample, and with that much treble they aren't enough to settle.
  - Ordinary playing levels are perfectly steady.
  - I haven't changed it, because it's PR7-G's sound and should change in both plugins together. If you hear crackle from AEON on hot, bright sounds, say so and I'll give the solver as many steps as it needs, in PR7-G and here.
- **CPU:** every type except AEON costs under 1% of a core, and LO-FI about 1%. A chord through AEON, the Rotary and the cassette together cost about 11%. Switched off, they cost nothing.
- **A new factory preset:** **Tape Keys** (A38): soft keys through the Vortex in STEREO, a slow auto-pan in half notes, and a lightly worn cassette, with a little room.
  - Its cassette has no hiss, because hiss would keep playing when no notes do. Turn NOISE up on the LO-FI page if you want it.
  - Our own settings, not tuned by ear.
- **Not yet:** the drives and glitch, and inserts on single oscillators (7d).
- **In the tests:**
  - PR7-G's own MOD, LO-FI and AEON tests, run as they are, through a stand-in for PR7-G's plugin. The Vortex's STEREO tests read the pedal's two outputs back from PR7-K's sides.
  - And, from the sound:
    - each side stays its own in every type;
    - switched off, they change nothing;
    - the reverb's tail goes through the phone;
    - the LIMITER holds LO-FI turned all the way up;
    - the sweep's marker moves at RATE;
    - the speakers' band edges are where the page draws them;
    - the bitcrusher holds for as long as drawn;
    - CPU, the page, and the new preset.
- I can't listen here. If a type sounds different from PR7-G's, tell me which and what you hear.

## 0.18.0

Stage 7, part 2: **THE SENDS**: PR7-G's **DELAY** and **REVERB**, fed by a send from each oscillator. They're PR7-G 0.52.0's own effects, the same code ported, as you asked.

- **The FX page now has two views,** with tabs along the top:
  - **SENDS:** DELAY and REVERB as two rack units, and a send matrix under them.
  - **MASTER:** 0.17.0's rack (ECHELON, DOMINION, CONSTRICTOR, LIMITER), unchanged.
- **How the sends work** (as on a mixing desk):
  - Each oscillator has a **DELAY** and a **REVERB** send: on its WAVE page (SENDS, beside NOTES), on the matrix, and on each effect's page.
  - The sound itself plays as before. A copy of each oscillator, at its send amount, goes to the effect, which gives back only its echoes or reverb, at its MIX.
  - So you can keep a sub dry and tight while the lead swims in reverb, or echo just one layer.
  - The sends are taken after the AMP and the note's pan, so each echo follows the note's envelope and position.
  - Sends are 100% by default, so switching DELAY or REVERB on just works on everything.
- **DELAY > REVERB** (on the matrix and the DELAY page): an amount of the echoes goes on into the reverb, so they get a space of their own.
- **The returns** come back in before the MASTER rack, so ECHELON, CONSTRICTOR and the LIMITER treat the echoes and reverb as part of the sound (the LIMITER holds them under its ceiling too).
- **DELAY**, PR7-G's, in all its types:
  - Digital, Analog, Tape, Mod, Reverse, Multi-Tap, Shimmer, Ducking;
  - TIME or SYNC to the song, FEEDBACK, TONE, MIX, PING-PONG, DUCK;
  - **on the unit:** PING-PONG, SYNC and DUCK keys, FEEDBACK and MIX knobs, the echo time in big digits, the return's level on the meter;
  - **its page** draws the echoes on a timeline: where each comes and how far down it is in dB, left above and right below with PING-PONG.
- **REVERB**, PR7-G's:
  - Classic, Room, Hall, Cathedral, Plate, Spring, Digital, Reverse;
  - SIZE, PRE-DELAY, TONE, MIX, DUCK;
  - **on the unit:** ROOM, HALL and PLATE keys to pick those quickly, SIZE and MIX, the tail's length in big digits;
  - **its page** draws the tail: silence for PRE-DELAY, then the fall to -60 dB.
- **Stereo:**
  - A sound on the left echoes on the left. It's exactly silent on the right; only TAPE's own hiss is on both sides.
  - PING-PONG takes what's sent as one sound and bounces it left, right, left, as on PR7-G. Otherwise a sound in the middle would echo on both sides at once and never bounce.
  - The reverb spreads what it's given across both sides, as a room does.
- **Nothing changes until you use them:**
  - With DELAY and REVERB off, the sound is exactly as in 0.17.0, sample for sample.
  - Switching one on while a note plays doesn't re-route the note (no click).
- **One trade-off:** oscillators with *different* send amounts each get their own copy of the filter and shapers. That's the same rule as routing them to different shapers. If the filter's DRIVE or a SHAPER is on, distorting them separately sounds a little different from distorting them together.
- **CPU:** DELAY and REVERB on a two-oscillator chord added about 2% of a core here; off, nothing.
- **A new factory preset:** **Echo Pluck** (A37). A pluck with ping-pong dotted-eighth echoes and a hall, over a sub that sends nothing. Our own settings, not tuned by ear.
- **Not yet:** inserts (effects on single oscillators: 7d), modulation and lo-fi (7c).
- **In the tests:**
  - PR7-G's own delay and reverb tests, run as they are (through a stand-in for PR7-G's plugin);
  - and, in the synth, from the sound:
    - an oscillator at 0% gets no echoes and changes nothing in them;
    - 50% gives echoes 6 dB down;
    - the reverb's tail comes only from what's sent, and DELAY > REVERB carries the echoes in;
    - left stays left (and PING-PONG bounces);
    - switching DELAY on mid-note, or different sends while it's off, changes nothing, sample for sample;
    - the LIMITER holds the echoes;
    - CPU;
    - the SENDS view, its pages and the echo timeline;
    - the new preset.
- I can't listen here. If an echo or a reverb sounds different from PR7-G's, tell me which type and what you hear.

## 0.17.0

Stage 7, part 1: **THE RACK**, a new **FX** page (in the header, after ARP) with PR7-E's four effects after the synth. They're PR7-E 0.18.1's, as you asked, with the same sound and controls: the same code, ported, not new versions.

- **The four rack units,** drawn as on PR7-E (after your rack photo), one to a row in the order the sound goes through them:
  - **ECHELON**, the EQ (inspired by FabFilter Pro-Q): up to 24 bands, dynamic bands, the analyser, and **EQ Match**.
  - **DOMINION**, the resonance smoother (inspired by soothe).
  - **CONSTRICTOR**, the compressor, with its up to six bands.
  - **The LIMITER** (inspired by Pro-L 2): its styles, look-ahead, true peak, loudness meters and dither, and 0.18.1's cleaner bass.
- **On each unit:** POWER, its meter, its screen, three keys, two knobs, and EDIT, which opens its big page in the rack's place (**BACK TO RACK** comes back).
- **Where they sit:** after the synth (its oscillators, filter, shapers and amp); then the preset's level and OUTPUT; then the LIMITER, last, so turning OUTPUT up drives into it as into a mastering limiter; then BYPASS.
- **In your presets:** the rack is saved with each preset (all four are off in the presets made before, so they sound as they did).
  - CONSTRICTOR and DOMINION also have **their own presets**, as on PR7-E. They're the same files (the "Comp Presets" and "Dominion Presets" folders in PR7-K's presets folder), so a preset saved in PR7-E can be copied over and loads the same.
  - Their parameters have PR7-E's names, so DAW automation reads the same too.
- **EQ Match on a synth:** "your sound" is the synth going into ECHELON.
  - **RECORD SYNTH** records the synth itself as the reference: play one preset as the reference, switch presets, and match the new one to it.
  - **LOAD FILE** matches to a song file.
- **Delay:** the LIMITER's look-ahead (1 ms by default, about 3.3 ms with TRUE PEAK) and CONSTRICTOR's LOOK-AHEAD (20 ms) make the sound a little late. Your DAW makes up for it on playback, but you'll feel it when playing live. Switched off, nothing is late. With the LIMITER's LOOKAHEAD at 0, TRUE PEAK and OVERSAMPLING off, it adds no delay either.
- **CPU, measured on a four-note saw chord** (one core of this machine; yours will differ):
  - the synth alone 0.7%;
  - ECHELON with six bands +0.4% (dynamic bands +0.9%);
  - CONSTRICTOR +0.3% (with three bands +1.3%);
  - the LIMITER with TRUE PEAK +3.9%;
  - **DOMINION +10.6%: by far the heaviest.** It listens with about 120 detectors and works out its cuts all the time; it costs the same on PR7-E. Switch it on where it's needed. Off, every unit costs nothing. If its weight bothers you, tell me: it could be made lighter, at some risk of changing its sound.
- **Changes from PR7-E:**
  - DOMINION's right-hand column is a little tighter, to fit PR7-K's page.
  - The text talks about the rack, presets and your sound instead of stomps, rigs and your track.
- **A new factory preset:** **Mastered Supersaw** (A36). Supersaw Lead with a low cut, a small dip in the boxy low mids, air on top, CONSTRICTOR's glue and the LIMITER at -1 dB. Our own settings, not tuned by ear.
- **Not yet:** inserts (effects on single oscillators) and sends, which come with PR7-G's delay and reverb (7b), modulation and lo-fi (7c), drives and glitch (7d).
- **In the tests:**
  - PR7-E's own tests for each effect (the limiter's came with it; the others were already here);
  - and, in the synth, from the sound:
    - off, the rack changes nothing, sample for sample;
    - ECHELON cuts exactly the band it's set to;
    - CONSTRICTOR turns a loud chord down;
    - the LIMITER holds its ceiling;
    - left and right stay apart (a note on one side leaves the other exactly silent through all four), and LINK turns both down together;
    - the delay reaches the DAW;
    - presets keep the rack;
    - the effects' own presets load;
    - the new preset;
    - CPU;
    - the FX page's layout, POWER and EDIT;
    - no parameter ID shared.
- I can't listen here. If something about the rack sounds or behaves differently from PR7-E, tell me.

## 0.16.0

Stage 5, part 2 (stage 5 complete): **chords, fixed note, the note randomizer, MIDI export, arp-note triggers and arps together**, modelled on Avenger's arpeggiator (the manual's pages 74-80; our own design and code).

**This release also brings 0.15.0's ARPEGGIATOR (stage 5a), which was never published:** the eight arps, patterns A to D, the ARP page and the preset Arp Over Pad (A34); see 0.15.0 below for all of it.
- **Why 0.15.0 never appeared:** the arp patterns made each preset's data 40 KB bigger. The plugin copies that data (for undo, loading presets, comparing) in places that use the thread's stack, which Windows keeps at 1 MB (Linux 8 MB). On Windows the tests crashed, so CI didn't publish it.
- **The fix:** the patterns now live on the heap (the copies are 7 KB again), and a new test keeps those sizes small. All the tests also pass here with Windows' stack size.

- **CHORDS mode** (MODE: CHORDS): each block plays a chord on the next key in turn. In this mode the pattern gets three more lanes:
  - **CHORD:** click for its type: Single, Major, Minor, Sus2, Sus4, Dim, Aug, 6, Min6, 7, Maj7, Min7, Min7b5, Dim7, Add9, 9, Maj9, Min9, Power, Octave.
  - **TONES:** a small switch per tone of the chord (root lit orange): click one to leave it out, e.g. a 7th without its fifth.
  - **SUB:** click for a note under the chord (an octave down); right-click for how far: 5, 7, 12, 17, 19 or 24 semitones.
  - The block's transposition moves the whole chord, so a pattern can be a progression.
- **NOTE SOURCE** (the new NOTES column, right of the pattern):
  - **KEYS:** as before.
  - **FIXED NOTE:** the arp takes the chord you hold and plays from its root alone (in your lowest key's octave), so the pattern's blocks make the tune. One key: that key.
  - **FIXED NOTE +:** the same, and the pattern follows the chord. A pattern is written as if over a minor chord; hold a major one and its minor third, sixth and seventh (+3, +8, +10, in any octave) become major (+4, +9, +11).
- **The RANDOMIZER:**
  - **ROLL** makes a new pattern from chances you set: which notes (by semitones above the key, kept to a **SCALE**: 14 to choose from, notes outside it dim), which octaves (-2 to +2), lengths (1 to 4 steps), and how likely a rest, a slide or a ratchet is, with the velocity's range.
  - **ROLLS** chooses what a roll changes (notes, octaves, lengths, velocities, rests): the rest of the pattern stays, so you can keep a rhythm and roll new notes, or the reverse.
  - Each roll has a **SEED** (shown, with - and +): the same seed and chances always make the same pattern. Undo brings the last pattern back.
  - **SYMMETRY:** the second half of a roll is the first half backwards.
  - **AUTODICE:** a new pattern rolled at the start of every time through, from the pattern written and the chances. It's the same every time you play the song (each pass has its own seed), so a bounce matches what you heard. Not with CHAIN.
- **EXPORT MIDI:** click to save the arp's notes as a MIDI file, or **drag the button onto a track** in your song.
  - It writes what the arp plays over the keys you hold on the on-screen keyboard (none held: C4), at the song's tempo.
  - Length: one time through (CHAIN: each pattern; AUTODICE: four passes).
  - It uses the same note code as the sound, so the file matches. A slide becomes notes that overlap, which most synths glide on.
- **ARP NOTES** trigger, for the LFOs and the mod envelopes (TRIGGER: ARP NOTES): restarted by every note any arp plays. One envelope shared by all notes; it lets go when no arp note sounds.
- **ARPS TOGETHER** (in the NOTES column): every arp counts its steps from the first key held, so an arp started later (a second key zone) comes in on the next step, in time with the others. Off: each arp runs from its own first key, as before.
- **Ours, where the manual says nothing:**
  - Avenger's chord types and scales aren't listed in the manual: these are the common ones.
  - How a held chord is recognised (each held note tried as the root against each chord type: most tones matched, fewest left over; the lowest key wins a tie).
  - The "+" mapping above.
  - The randomizer's chances, its defaults and how SYMMETRY mirrors.
  - That AUTODICE repeats the same passes each time you play.
- **Saved** with the preset and the project; undo covers chords, rolls and the randomizer. Presets from 0.15.0 load unchanged.
- **A new factory preset:** **Chord Stabs** (A35). Hold a chord or one key: a four-chord progression (i, VI, III, VII), stabbed in sixteenths with a sub note on each beat. Our own, not tuned by ear.
- In the tests, from the sound (each note's pitch and level) where it plays:
  - chord types, tones left out, the sub note, chords on each key in turn;
  - FIXED NOTE and "+" over major and minor chords, chord detection;
  - rolls kept to the scale and chances, the same seed giving the same pattern, ROLLS keeping what isn't rolled, SYMMETRY;
  - AUTODICE changing each pass and repeating;
  - an envelope restarted by each arp note;
  - ARPS TOGETHER's later arp waiting for the step;
  - the MIDI file's notes, times, lengths and velocities, and chords in it;
  - saving (and 0.15.0's patterns still reading);
  - the page's layout, lanes and ROLL with undo.

## 0.15.0

Stage 5, part 1: **the ARPEGGIATOR**, modelled on Avenger's (the manual's pages 74-80; our own design and code). A new **ARP** page (in the header, between MOD and PRESETS).

- **Eight arps.** Each oscillator chooses where its notes come from:
  - **OFF** (straight from the keys) or **ARP 1** to **8**. Set it on the oscillator's WAVE page (NOTES: ARP), or with the ARP page's **OSCILLATORS** buttons.
  - So one arp can run a pluck while another runs a bass, and an oscillator left off plays the chord itself.
- **Patterns A to D**, each up to 32 steps. Play the one chosen, or **CHAIN** them (those with blocks, one after another).
- **The blocks**, on a grid (steps across, transposition up and down; octaves orange, the rest amber):
  - click to put one, drag it up or down to transpose it (up to two octaves either way), drag its right edge to make it longer, right-click to delete it;
  - under each: its **VELOCITY** (drag), **SLIDE** (click: it glides into the next block with PLAY's GLIDE time, no new note) and **RATCHET** (click for up to 8 repeats in its length, right-click for fewer);
  - **-1 / +1** move every block a semitone; **START FROM** gives ready-made patterns (every step, eighths, octave bounce, up the scale, trance off-beats, slides, ratchets, long notes, clear).
- **The controls:**
  - **ON**, **MODE**, **SPEED** and **TIMING** (straight, triplet, dotted), **OCTAVES** (1 to 4), **GATE**, **LENGTH** (2 to 32 steps).
  - **MODE:** UP, DOWN, UP/DOWN, DOWN/UP, AS PLAYED, RANDOM, or POLY (every held note on each block).
  - **VELOCITY:** each note's own, the first key's, the last key's or the lowest key's, times each block's.
  - The key zone (**START NOTE** to **END NOTE**: keys outside it don't play the arp's oscillators).
  - **HUMANIZE** (each block late by up to a quarter step, at random), **STRUM** (POLY's notes one after another, over up to half a step).
  - **TRIGGER:** SONG keeps in time with your song while it plays (starting from the first key when it's stopped); KEY starts from the first key, always.
  - **SHUFFLE:** the synth's, shared with the step sequencer.
  - The sustain pedal keeps the arp going after you let go.
- **Timing:** the notes start and stop on their exact samples, wherever they fall in the DAW's audio blocks.
- **Ours, where the manual says nothing:**
  - The manual names UP, POLY and CHORDS: DOWN, UP/DOWN, DOWN/UP, AS PLAYED and RANDOM are the usual others.
  - How far HUMANIZE and STRUM go.
  - With the song playing, a first key waits for the next step (so the arp stays on the beat).
- **Not yet (stage 5b):**
  - CHORDS mode;
  - FIXED NOTE chord detection and the scale-aware "+" mode;
  - the note randomizer;
  - MIDI export;
  - envelopes and LFOs triggered by arp notes;
  - starting every arp together.
- Saved with the preset and the project; undo covers the patterns.
- **A new factory preset:** **Arp Over Pad** (A34). Hold a chord: a pluck arpeggiates through ARP 1 while a pad plays the chord. Our own, not tuned by ear.
- In the tests, from the sound (each note's pitch and level):
  - UP and GATE; every mode and OCTAVES;
  - rests, TRANSPOSE, long blocks, VELOCITY, RATCHET and SLIDE's glide;
  - the chosen pattern and CHAIN;
  - the velocity modes, SHUFFLE, HUMANIZE, STRUM;
  - routing (an unrouted oscillator playing the key under the arp), the arp switched off, the key zone, the sustain pedal;
  - SONG timing;
  - the plugin playing from a fresh start;
  - presets, projects and undo;
  - the page's layout and editing.

## 0.14.0

Stage 4, part 6 (stage 4 complete): **MIDI learn, controller to mod wheel, modulation on the knobs, drag and drop routing, and more for the matrix to move**, modelled on Avenger's (the manual's pages 106-108; our own design and code).

- **The strip above the keyboard:** every modulation source as a chip: LFO 1-4, ENV 1-8, MACRO 1-3 and the two buttons, the wheel, bend, velocity, aftertouch, keytrack, breath, expression, sustain, random, the pitch envelope, the step sequencer.
  - **Drag a chip onto a knob** to route that source to it (at +50%: change it in the MOD page's matrix).
- **The modulation ring:** a knob the matrix moves shows a white ring outside its own, from where the knob is to where the routes can take it.
  - **Right-click a knob:** its routes, each to remove, and MIDI learn.
- **MIDI learn:**
  - Right-click a knob, choose **MIDI learn**, and move a knob or fader on your controller: they're linked. Or drag the strip's **CC** chip (the last controller you moved) onto a knob.
  - A linked knob shows its controller's number beside it. The controller turns it straight away (in the same block of audio), and it's recorded as automation.
  - The links are kept with the project. **SETTINGS** lists them, each with a button to remove it, and has **SAVE AS DEFAULT** (every new PR7-K starts with them), **EXPORT**, **IMPORT** and **CLEAR ALL**.
  - One controller per knob and one knob per controller: linking either again moves the link.
- **Controller to mod wheel:** the **MW** switches on aftertouch, breath, expression and sustain make that controller drive the mod wheel too, so every sound made for the wheel follows your pedal or breath (whichever is higher, the wheel or the controller). They stay set when you change presets.
- **More the matrix can move:**
  - **SKEW** and **PINCH**. A note moved by the matrix works out its own curve every 16 samples, from curves worked out ahead at 65 settings of each. It's within a hair of the exact curve: the difference measured 66 to 72 dB down.
  - The shapers' **EQ** bands (gain, frequency, Q of both): a modulated note works out its own EQ.
  - The PITCH module's **RANGE** and **KEYTRACK**.
  - TRAVEL's **STRENGTH** and **PHASE**.
  - The step sequencer's **VOL MIX**.
  - Sounds that don't use them are exactly as before.
- **Not movable** (why, honestly):
  - **FFT SWEEP, FLIP and VSTRENGTH:** each new setting rebuilds the oscillator's tables in the background, which takes milliseconds. Too slow to follow an LFO, and per note it would need a table for every note.
  - **The LFOs', envelopes' and step sequencer's own speeds and shapes:** these are worked out before the routes, or once for every note. Moving them needs a deeper change, offered for later.
- **CPU:** SKEW moved by an LFO on every note of a 14-saw chord adds about half a point here.
- In the tests:
  - every new destination routed against the same value simply set (the same sound);
  - SKEW's curve accuracy;
  - the expression pedal driving the wheel's route;
  - MIDI learn (learning, turning, moving a link, the project, the default file, forgetting);
  - dropping a source and a controller on knobs;
  - the ring's range;
  - removing a route;
  - the strip's layout;
  - SETTINGS' list;
  - CPU.

## 0.13.0

Stage 4, part 5: **TRAVEL**, each oscillator's own movement through its wavetable, modelled on Avenger's wavetable envelope, pattern editor and strength editor (the manual's pages 48-51; our own design and code). It's on the oscillator panel's new **TRAVEL** page, separate for each of the 8 oscillators. It moves an oscillator playing a WAVETABLE.

- **The envelope (ENV):** a path through the table, drawn over one cycle: bottom is the first frame, top the last.
  - Double-click to add points, drag lines to curve them; its ends don't have to meet.
  - A new one is a straight ramp from the first frame to the last.
- **The pattern (SEQ):** up to 16 steps, each a SPEED period long, or two. Each plays the envelope its own way:
  - **x** (normal): the whole envelope, 1/2 to 16 times in the step;
  - **t** (trigger): its first part, restarted that many times;
  - **PP** (ping pong half): up its first half and back;
  - **R** (reverse) and **U** (flip, upside down);
  - **S** (silence): the oscillator goes quiet for the step (a millisecond's fade, so it doesn't click);
  - **H**, **M**, **L**: straight to the last, middle or first frame.
  - Drag a step up or down for more or fewer iterations, right-click it for its mode and length, double-click to make it one or two periods long.
- **The strengths (MOD):** under each step, a curve for how far that step moves (start, an optional middle point, end, bent).
  - Drag its left, middle or right; shift-drag bends it; double-click adds or removes the middle point.
- **The controls:**
  - **EDITORS** (Env, Env+Seq, Env+Seq+Mod).
  - **TRIGGER** (Voice, First Note, Last Note).
  - **MODE** (the envelope's: Loop, Ping Pong, One Shot) and **PATTERN** (the pattern's, the same three).
  - **STEPS**, **PHASE** (where it starts), **FLIP**.
  - **STRENGTH** (-100% to +100%), **VELOCITY** and its **VEL CURVE** (softer notes travel less far), **SMOOTH** (slows its moves, up to half a second).
  - **SPEED:** a note length at your song's tempo (1/4 to start) or **FREE** in cycles a second.
- **How it moves INDEX:** the frame played is INDEX + STRENGTH x the envelope (H, M and L set it outright). The INDEX knob, and the matrix routes on it, still work on top.
- **Our choices** (the manual doesn't give these):
  - an envelope cycle (or a step) is one SPEED period;
  - a step's iterations divide its own length;
  - PING PONG on the pattern runs it backwards, steps and all;
  - SMOOTH is up to 500 ms.
  - Avenger's fourth trigger, "global first", is the same as FIRST NOTE here: PR7-K's oscillators don't have key zones (yet).
- Saved with the preset and the project; unlike Avenger's editors, undo covers all three; its knobs can be automated.
- **A new factory preset:** **Talking Bass** (A33). The Vowels table moved by an eight-step pattern in sixteenths: sweeps, quick retriggers, a reverse, a jump to the last vowel, a silent step. Our own, not tuned by ear.
- **CPU:** TRAVEL is worked out every 16 samples per note: 16 travelling wavetable oscillators add about 0.3% of a core here.
- In the tests, measured on a two-frame table (a sine and its octave, so the octave's share is where in the table the note is):
  - the envelope's ramp and drawn shapes;
  - STRENGTH with INDEX, FLIP, PHASE, the three modes, FREE speed;
  - every step mode, iterations (2 and 1/2), SILENCE, a two-period step;
  - MOD's strength curve;
  - VELOCITY;
  - SMOOTH;
  - VOICE, FIRST NOTE and LAST NOTE with two notes;
  - the plugin moving a factory table from a fresh start;
  - presets, projects and undo;
  - the page's layout and its drags and clicks;
  - CPU.
- Not checked here: how it sounds (I can't listen).

## 0.12.0

Stage 4, part 4: **the STEP SEQUENCER** (the trance gate), modelled on Avenger's (our own design and code). It's on the MOD page, under the new **STEP** tab.

- **The steps:** up to 32 blocks.
  - Click a step to set its height; drag across to draw several.
  - Right-click a step to delete it (a rest).
  - Click the arrow under a step to link it to the next: the gate stays open from one to the other, moving to each one's height.
  - The line over the steps is the gate as it will play, so you can see GATE, CONTOUR, DECAY and SHUFFLE change it.
  - **START FROM:** an every-step trance gate, off-beats, on the beats, 3 + 3 + 2, accents, a rise, a fall, long and short (linked), a stereo ping-pong.
- **The controls:**
  - **ON:** the trance gate on the volume. **VOL MIX** sets how deep it cuts (100%: silence between steps).
  - **SPEED** (a step's length, 8 bars to 1/64) and **TIMING** (straight, triplet, dotted), at your song's tempo.
  - **TRIGGER:**
    - SONG (the default) stays in time with your song while it plays, and starts from your first note when it's stopped.
    - VOICE gives every note its own gate, from its start.
    - FIRST NOTE and LAST NOTE work as on the LFOs.
  - **MODE:** MONO, or STEREO with two rows (the top lane for the left channel, the bottom for the right), and **WIDTH** from fully apart to the same.
  - **LENGTH** (2 to 32 steps), **GATE** (how much of each step it's open), **CONTOUR** (soft edges, up to half a step), **DECAY** (each block fades out).
  - **DELAY** and **FADE IN** (from each note's start), and the key zone (**START NOTE** to **END NOTE**: other notes are left alone).
  - **SHUFFLE:** a swing. Every second step starts late, up to half a step (75% swing). It's the synth's shuffle: the arpeggiator and drums will share it when they come.
- **A matrix source, STEP SEQ** (0 to 1): the steps can move any knob, with or without the gate on the volume.
- **Our choices** (the manual doesn't give these numbers):
  - the edge is a raised-cosine fade;
  - CONTOUR 0 still has a 0.5 ms edge, so a hard gate doesn't click;
  - DECAY is the time to fall 60 dB;
  - SHUFFLE moves a step up to half a step;
  - in STEREO the matrix hears the two rows' average.
- Saved with the preset and the project; undo covers the steps; its knobs can be automated.
- **A new factory preset:** **Trance Gate** (A32). A wide supersaw cut in sixteenths: accents on the beats, off-beats bouncing left and right, a long linked step closing each bar, the filter opening on the loud steps. Our own, not tuned by ear.
- **CPU:** a step sequencer on each of 8 notes of a 14-saw chord adds well under 1% of a core here.
- **A test fix:** the shaper CPU test now keeps the fastest of three timings. A busy build machine had failed 0.11.0's first build on it; the limit is unchanged.
- In the tests:
  - the gate open for GATE of each step and shut for the rest;
  - heights, deleted and linked steps;
  - VOL MIX, LENGTH, CONTOUR's edge, DECAY's fall;
  - SHUFFLE's late steps, triplet and dotted steps;
  - STEREO's rows and WIDTH;
  - DELAY, FADE IN and the key zone;
  - SONG following the song's beats, and VOICE, FIRST NOTE and LAST NOTE with two notes;
  - STEP SEQ as a matrix source;
  - the plugin gating from a fresh start and keeping the steps through presets, projects and undo;
  - clicks in the editor;
  - the tab's layout;
  - CPU.
- Not checked here: how it sounds (I can't listen).

## 0.11.0

Stage 4, part 3: **the PITCH module**, modelled on Avenger's (our own design and code). It's on the MOD page, under the new **PITCH** tab.

- **The pitch envelope:** a multipoint envelope on the oscillators' pitch, drawn and edited like the modulation envelopes.
  - Its middle line is no change; **RANGE** (up to ±24 semitones) is how far its top and bottom go.
  - **ON**, **MODE** (one shot, sustain, sus jump, loop, ping pong and their +REL versions), **SPEED** (x0.1 to x10), **STEPS** (stepped, for arcade and FX sounds), **FLIP**.
  - **FOLLOWING IT:** which oscillators it moves (all to start with). In Avenger an oscillator is routed to a pitch module; here each has a switch.
  - It's also a source in the matrix, **PITCH ENV**, even with the envelope itself off.
  - **START FROM:** a drop, a kick-drum drop, a rise, a fall away, arpeggio steps, a wobble.
  - A new sound starts with a short drop (+6 semitones to none in 150 ms), switched off.
- **KEYTRACK:** how far the pitch follows the keys, from middle C. 100% is normal; 0% plays every key as middle C; -100% turns the keyboard upside down.
- **BEND LAG:** smooths the pitch wheel (and its automation), so jumps glide.
- **GLIDE CURVE:** the glide's path.
  - 0 is the straight glide as before (unchanged, sample for sample).
  - Below 0 it gets most of the way at once and settles (the manual's siren-like swoop); above 0 it starts slowly.
  - GLIDE's time and modes stay on the PLAY panel.
- **GLIDE SPLIT:** notes below this one never glide and aren't glided from, so a bass line can sit under a gliding lead (the manual's "split zone").
- Saved with the preset and the project; undo covers the pitch envelope's points; its knobs can be automated.
- **A new factory preset:** **Zap Bass** (A31): a fast two-octave pitch drop on each note. Our own, not tuned by ear.
- In the tests:
  - the pitch envelope moving a note by the right number of semitones over time;
  - RANGE, FLIP, STEPS;
  - an oscillator not following it staying put;
  - KEYTRACK at 0%, 50% and -100%;
  - BEND LAG easing a jump;
  - GLIDE CURVE's path, and CURVE 0 identical to before;
  - GLIDE SPLIT;
  - PITCH ENV as a matrix source;
  - the plugin playing the envelope from a fresh start and keeping it through presets, projects and undo;
  - the tab's layout.
- Not checked here: how it sounds (I can't listen).

## 0.10.0

Stage 4, part 2: **8 modulation envelopes** and **3 drawn LFO shapes**, modelled on Avenger's multipoint envelopes (our own design and code).

- **The MOD page has a new layout:** a wide **MODULATORS** panel on top with tabs **LFO 1-4**, **ENV 1-8** and **SHAPE 1-3**. The macros and the matrix are below it.
- **The multipoint editor** (for envelopes and shapes alike):
  - drag a point to move it;
  - drag a line up or down to curve it;
  - double-click empty space to add a point, or double-click a point to delete it;
  - right-click a point for its menu: delete it, put **SUSTAIN** (S) there, or put the **LOOP** points (L1 and L2) there.
  - Up to 64 points.
  - **START FROM** gives ready-made shapes to begin from:
    - envelopes: ADSR, ramps, pluck, swell, gate, wobble;
    - LFO shapes: triangle, ramps, square, steps, bounce, spike.
- **Modulation envelopes** (sources **ENV 1-8** in the matrix):
  - **MODE**, as the manual describes them:
    - **ONE SHOT:** once through, then it holds the last point.
    - **SUSTAIN:** holds at S while the key is down; let go earlier and it jumps to S.
    - **SUS JUMP:** the same, but it jumps to where the level matches, so nothing jumps.
    - **LOOP:** round L1 to L2 for ever.
    - **LOOP+REL:** loops while the key is down, then runs on to the end.
    - **PING PONG** and **PINGPONG+REL:** back and forth instead of round.
  - **TRIGGER:** **VOICE** (every note its own), **FIRST NOTE**, **LAST NOTE**. The arpeggiator and drum sequencer triggers come with those stages.
  - **SYNC:** times in beats of your song instead of seconds.
  - **SPEED** (x0.1 to x10), **STEPS** (stepped, for arcade and FX sounds), **FLIP**.
  - **OFFSET:** the level counted as zero; at 50% it pushes both ways, as the manual's "bipolar" offset does.
- **Drawn LFO shapes:** choose **CUSTOM 1, 2 or 3** as any LFO's WAVE to play the shape drawn on that SHAPE tab. Its two ends are kept level, so it loops without a click.
- Envelopes and shapes are **saved with the preset** (and the project), and undo covers them. Their knobs (MODE, SPEED...) can be automated.
- A modulator nothing uses costs nothing. The test chord with six routes per note still costs what it did.
- **Not yet:**
  - The manual says the drawn shapes can also drive the oscillators' vibrato and FM. Here they drive the LFOs only; vibrato and FM can follow in a later part.
  - Avenger's envelope "scale" control: a route's AMOUNT does that job here.
- **Two new factory presets:**
  - **Gated Pad** (A29): a looping, synced envelope chopping a pad in time.
  - **Swell Lead** (A30): a multipoint envelope on the filter, and a drawn bounce wobbling the pitch.
  - Our own, not tuned by ear.
- In the tests:
  - curved and straight lines;
  - every mode stepped through, with its release;
  - SPEED, SYNC, STEPS, FLIP, OFFSET;
  - an envelope moving a note's pitch exactly as drawn;
  - VOICE against LAST NOTE measured as level;
  - a drawn shape playing as an LFO;
  - presets, projects and undo;
  - the editor's mouse actions.
- Not checked here: how it sounds (I can't listen).

## 0.9.0

Stage 4, part 1: **modulation**: the MOD MATRIX, 4 LFOs, macros and your keyboard's controllers, modelled on Avenger's (our own design and code). Everything else in stage 4 (modulation envelopes, the pitch envelope, the step sequencer, the wavetable travel envelope, MIDI learn) plugs into this matrix.

- **The MOD page** (the new **MOD** tab beside SYNTH):
  - **LFO 1 to 4**, each with a picture of its wave:
    - **WAVE**: sine, triangle, saw up, saw down, square, S&H (a random level each cycle), smooth random.
    - **SYNC**: FREE (RATE in Hz) or locked to your song's tempo in note LENGTHS from 8 bars to 1/64, straight, **triplet** or **dotted**.
    - **TRIGGER**: **VOICE** (every note its own LFO, starting with the note), **GLOBAL** (one for all, always running, in step with the song's beats when it plays), **FIRST NOTE** (restarted when you play with no other key down), **LAST NOTE** (restarted by every note).
    - **PHASE**, **OFFSET** (moves the wave up or down and fits it in: at +100% it only ever pushes up), **DELAY**, **FADE IN**, **ONE SHOT** (once through, then it holds: an envelope).
  - **MACROS**: three knobs (double-click a name to rename it, saved with the preset) and two buttons. They're DAW parameters, so you can automate them or map hardware knobs to them.
  - An on-screen **MOD WHEEL**, for trying mod wheel routes without a keyboard.
  - **The MOD MATRIX**: up to 64 routes. Each: **ON**, a **SOURCE**, a **DESTINATION**, an **AMOUNT** (-100% to +100% of the knob's travel), **+/%**, **LAG** and **CURVE**.
    - Sources: the LFOs, macros and buttons, **mod wheel, pitch bend, velocity, aftertouch** (channel and per note), **keytrack, breath, expression, sustain pedal** and **random** (a new value for each note).
    - **+** adds the amount to the knob; **%** moves it by a share of the travel it has left, so it always reaches the end exactly at 100% (as the manual describes).
    - **LAG** slows the source down, smoothing jumps (a stepped LFO, a pressed button).
    - **CURVE** bends the source: below 0 it does most of its work early, above 0 late. (Avenger draws a curve per route; ours is a knob.)
    - Several routes to the same knob add up.
- **Every note is modulated on its own:** a velocity or a VOICE LFO moves each note's knobs separately, worked out every third of a millisecond.
- **Destinations:** every knob the synth follows live while a note plays (the oscillators' level, pan, pitch, shape, wavetable index, noise, sub, SYNC/FM/AM, unison, vibrato and the FFT editor's LP/HP; the filter; the shapers' DRIVE, MIX and OUTPUT; the AMP's volume and pan).
  - Not yet: SKEW and PINCH (their curve is built ahead of time), the FFT editor's SWEEP, FLIP and VSTRENGTH, and the shapers' EQ bands (both worked out ahead too), menus and switches. Those need changes inside the engine; I'll offer them later.
- **Saved with the preset** (and the project), and undo covers the matrix. Routes are stored by name, so presets keep working as later versions add sources and destinations.
- **CPU:** six routes on every note of the test chord cost about as much as none (4.7% against 5.1% of a core: within measuring noise). A sound with no routes runs exactly as before.
- **Three new factory presets:**
  - **Wobble Bass** (A26): a synced LFO on the filter.
  - **Mod Wheel Pad** (A27): the wheel opens the filter and adds vibrato.
  - **Velocity Keys** (A28): velocity on the filter and a shaper, and a TONE macro.
  - Our own, not tuned by ear.
- In the tests:
  - the LFOs measured as pitch: a 2 Hz square moving a note exactly 100 cents at the right times, synced and dotted rates, PHASE, OFFSET, DELAY, FADE IN, ONE SHOT;
  - the trigger modes measured as level;
  - every source;
  - + and %, CURVE, LAG, routes adding up;
  - velocity brightening the filter;
  - the plugin keeping the matrix through presets, projects and undo;
  - the page's layout and editing a route;
  - CPU.
- Not checked here: how it sounds (I can't listen).

## 0.8.0

Stage 3, part 5: **the SHAPER**, a distortion on each note, modelled on Avenger's shaper section (our own design and code). This finishes stage 3.

- **Where it is:** the FILTER panel now has tabs: **FILTER**, **SHP 1**, **SHP 2**, **SHP 3**, **SHP 4**. Each tab's light is on while that filter or shaper is.
- **What each shaper has**, as the manual describes it:
  - **MODEL** and **DRIVE** (up to +36 dB into the distortion; the level is kept about the same, so more drive means more grit, not just louder). **DRIVE VEL**: softer notes are driven less.
  - **Two EQ bands before the distortion** (GAIN, FREQ, Q each; at 0 dB a band is off): boost a band and that part distorts first.
  - **EQ SPLIT**: band 1 on the left channel only and band 2 on the right only, so the two sides distort differently and the sound gets much wider.
  - **PLACE**: before the filter (the filter then tames the buzz) or after it. Up to 4 shapers, run in number order on each side of the filter.
  - **OSCILLATORS 1 to 8**: which oscillators go through it. All of them are ticked to start with, so switching a shaper on is heard at once; untick the ones you want clean.
- **Ours, not Avenger's:**
  - **The 10 models** (the manual doesn't list Avenger's): SOFT, SMOOTH and CUBIC round off gently; HARD clips flat; TUBE and DIODE lean to one side (warmer, with even harmonics); SINE FOLD and TRI FOLD fold the wave back on itself (metallic); RECTIFY flips the bottom half up (an octave up); HALF RECT cuts it off.
  - **MIX** (below 100%: the clean and distorted sound together) and **OUTPUT**. The manual lists neither.
- **Each note is distorted on its own** (as on Avenger, "on an oscillator level"), so chords stay clean of the harsh tones you get distorting several notes at once. Oscillators on different routes each get their own filter and shapers, so a clean oscillator stays clean beside a distorted one (tested: playing both is exactly each alone, added).
- **Clean at the top:** distortion makes harmonics far above the note, and those past the top of what the sample rate can hold fold back down as harsh, out-of-tune tones. Each shaper runs at twice the sample rate, with the anti-aliasing the filter's DRIVE uses on top. Measured on a hard-driven 1.76 kHz tone: folded noise about -55 to -65 dB, against -26 to -32 dB for a plain distortion.
  - Two costs: half a millisecond of delay through a shaper (its MIX's clean sound is delayed to match), and a very gentle top roll-off from the anti-aliasing (-0.5 dB at 10 kHz, -1.6 dB at 18 kHz).
  - The FOLD models, pushed hard on high notes, still make more harmonics than this can hold. Use them lower down, or with less DRIVE, for the cleanest result.
- **CPU:** a shaper costs nothing while it's off. On the test chord (8 notes, 2 supersaws of 7 copies, through the filter): 4.5% of a core with none, 9.3% with one, 15% with two.
- **Your old sounds are untouched:** with no shaper on, the synth's output is identical, sample for sample, to 0.7.0 (checked on a test sound with three oscillators, a driven filter and chords).
- **Changing MODEL while a note plays** switches the curve at once, so it can click, like switching any waveform.
- **Two new factory presets:** Tube Bass (A24) and Wide Fold Lead (A25, EQ SPLIT). Our own, not tuned by ear.
- In the tests: every model's maths, harmonics (SOFT odd only, TUBE and DIODE adding even ones, RECTIFY an octave up), level kept as DRIVE rises, aliasing against a plain distortion for every model, the oversampling's flatness and delay, the EQ bands against their picture, SPLIT, routing, separate paths, PLACE, DRIVE VEL, MIX, OUTPUT, saving, the page's layout and CPU. Not checked here: how it sounds (I can't listen).

## 0.7.0

Stage 3, part 4b: **FREEFORM** (draw a wave by hand) and **the resampler** (choosing how a long sample is cut into wavetable frames), both modelled on Avenger's (our own design and code).

- **FREEFORM**, a new WAVE choice, and **the DRAW page** (each oscillator: WAVE, SHAPE, FM, FFT, **DRAW**, VOICES): drag to draw one cycle of the wave; hold **SHIFT** for a straight line. Drawing switches the oscillator to FREEFORM so you hear it straight away. The thin line is what you drew, the bright one what plays.
  - **START**: begin again from a flat line (Avenger's start), or a sine, saw, square or triangle to change.
  - **SMOOTH A** and **SMOOTH B** round off sharp corners (sharp corners are what sound bright and buzzy): A before **SPLIT**, B after it (SPLIT at 100%: A everywhere).
  - **EDGE** joins the end of the cycle back to its start, so there's no jump (a click on every cycle, heard as buzz). Its modes: **ADAPT** moves both ends to meet in the middle, **ZERO** fades them to silence, **SYMMETRY** blends the wave with its mirror image.
  - **NORMALIZE** makes it as loud as it can go without clipping (off: as loud as drawn).
  - **WAV** exports the cycle as it plays (2048 samples), to load anywhere as a wavetable.
  - It plays as cleanly as the basic waves (no aliasing, high up included), and everything else works on it: unison, SHAPE, SYNC / FM / AM, and the FFT editor.
  - Saved with the preset and the project; undo takes back each stroke.
  - **What's ours, not Avenger's:** the smoothing and edge maths are our own guesses at what the manual describes; the controls are as the manual lists them.
- **The resampler:** loading a WAV, AIFF or FLAC that's longer than one cycle (from TABLE's menu or by dropping it on the oscillator) now asks how to cut it:
  - **Frame length:** as the file says (if another wavetable synth marked it), or 256 to 4096 samples, or **the note it's played at** (each frame is one cycle of that note: load a sustained C2 and set C2, and every frame is exactly one cycle, so it plays in tune with no buzz).
  - **Which frames:** one after another, every 2nd / 3rd / 4th, or 16 to 256 spread evenly through the file.
  - **Start** and **End**: just a part of the file.
  - Files of one cycle or less load straight away, as before.
- **A new factory preset:** Drawn Growl (A23), a bass from a hand-drawn cycle. Our own, not tuned by ear.
- In the tests: a drawn sine plays one harmonic and a drawn saw a saw's; SMOOTH takes the high harmonics away, and SPLIT gives each half its own; each EDGE mode closes the jump; saving; no aliasing at C7; the plugin playing the drawing and keeping it through presets, projects and undo; the FFT editor on a drawing; the resampler cutting by note, every nth, spread and part; drawing on the page with the mouse. Not checked here: how it sounds (I can't listen), and the resampler's question window itself (it opens on its own, so the screenshots can't show it).

## 0.6.0

Stage 3, part 4a: **the FFT editor**, modelled on Avenger's (our own design and code). **This release also brings 0.5.0's SYNC, FM and AM** (the FM page: see 0.5.0's notes in the changelog), which didn't publish on its own: one older test, which hears random choices, failed on Windows. The tests now make the same random choices every time, on every computer.

- **The FFT page** (each oscillator: WAVE, SHAPE, FM, **FFT**, VOICES): draw how loud each of the wave's first 256 harmonics is, from +24 dB down to gone. Drag to draw; hold **SHIFT** for a straight line from where you started. The wave's own harmonics are drawn faintly behind, and what comes out over them.
  - Three views, as Avenger's: **FREE** (each octave the same width), **HARMONIC** (the first 24 harmonics as wide bars, each named by the interval it makes with the note: root, octave, fifth, third, b7..., and one bar for all the rest; our layout) and **BIN** (all 256 side by side).
  - **RANDOM** (every harmonic a random level, leaning towards cuts so it stays usable) and **RESET**.
  - **SWEEP** moves your drawing up or down the harmonics (up to 4 octaves): phasing and morphing sounds. **FLIP** turns it upside down (cuts become boosts).
  - **LP** and **HP** take away the harmonics above (or below) a given one: a filter that follows the note exactly, and **SLOPE** makes it a gentle shelf instead of a steep cut.
  - **VSPEED** and **VSTRENGTH** make the harmonics pulse, each swelling and fading at its own depth, some against the others.
  - Works on every wave, wavetables included (INDEX still moves through the edited frames). The wave picture shows the edited wave, marked (FFT).
- **How it stays light:**
  - Your drawing, SWEEP and FLIP are baked into a rebuilt copy of the wave, in the background (about a millisecond for a basic wave, a few dozen for a wavetable), so playing an edited wave costs what playing a wavetable does: the test chord, 3.3% of a core against 3.1% plain.
  - LP, HP and SLOPE are live instead, so they can move as fast as you like (and take an envelope once stage 4's modulation is in). The test chord costs about 15% with LP on.
  - VSPEED's pulsing swings between two rebuilt copies: about 24% with LP, HP and pulsing all on.
  - One thing to know: while you drag SWEEP, the rebuilt wave follows a few times a second rather than instantly (a wavetable's rebuild takes longest). LP and HP have no such lag.
- **Your curves are saved** with the preset and the project, and undo covers them.
- **Two new factory presets:** Hollow Reed (A21: every even harmonic drawn out) and Spectral Shimmer (A22: pulsing harmonics). Our own, not tuned by ear.
- In the tests: the curve, SWEEP and FLIP exact; a cut harmonic gone and the rest where they were; a flat curve the same as the plain wave (every harmonic within 0.05 dB); LP and HP cutting at the right harmonics, blending between, SLOPE's shelf, and no aliasing; the pulsing swinging as set; the plugin building in the background and keeping curves through presets, projects and undo; the page's layout; CPU. Not checked here: how it sounds (I can't listen).

## 0.5.0

(Not published on its own: its Windows build failed one older test. Everything here is in 0.6.0.)

Stage 3, part 3: **SYNC, FM and AM**, modelled on Avenger's (our own design and code). Each oscillator has a new **FM** page, between **SHAPE** and **VOICES**.

- **SYNC**: a copy of the wave restarted at the start of every cycle of the note, running faster, from the note's own pitch up to 5 octaves above: the classic tearing "sync" sound. **MODE**: **PURE** (the copy alone), **ADD** (the copy with the wave), and **PURE WIN** / **ADD WIN**, which fade the copy in and out each cycle so there's no hard edge (smoother, more vocal).
- **SYNC ENVELOPE**: SYNC's own little envelope, as on Avenger: **ENV** (how far it sweeps SYNC up, or down, from each note's start), **LENGTH** (50 ms to 3.5 s to fall back) and **CURVE**.
- **FM**: the wave read ahead and behind as a modulator swings (phase modulation, as most "FM" synths do it). **SOURCE**: a sine, triangle, saw or square at **RATE** times the note, or **another oscillator**, which then only modulates and isn't heard itself (as on Avenger, "real FM").
- **AM**: the wave's level swung by the modulator (**SHAPE**); at 100% it's ring modulation.
- **The modulator: RATE** and **GRID**, Avenger's eight: **FREE** (1/16 to 16 times the note), **HARMONIC** (octaves and fifths), **SEMI** (semitones), **OCTAVE**, and octaves with a minor third, major third, fourth or fifth. The grids step from 1 (the middle of the knob) up to 16 times the note. **LOCK** starts the modulator in step with the wave on every note, so every note sounds the same; off, each starts anywhere. The display under the controls shows the result and the ratio RATE has landed on.
- **Kept clean without oversampling**, measured on a saw at A5 (880 Hz), loudest between its harmonics: SYNC -51 dB (WIN -93), FM -119 dB, AM -116 dB, where doing it the plain way leaves -13 to -33 dB. Avenger's FM has four quality modes, the cleanest of which oversamples (its manual warns it's CPU-heavy); ours has one, which reads band-limited tables chosen with room for FM's side tones. Its table-based FM modes aren't built: tell me if you hear FM getting harsh up high and I'll add one.
- **CPU:** nothing at all until you turn SYNC, FM or AM up. The supersaw test chord (2 oscillators x 7 copies x 8 keys) took about 3% of one core plain, 9% with SYNC, 12% with AM, 14% with FM and 19% with all three. Your computer will differ.
- **Three new factory presets:** Sync Sweep (A18), FM Bell (A19, oscillator 2 modulating oscillator 1) and Ring Mod (A20). Our own, not tuned by ear.
- In the tests: SYNC at twice the note is exactly the octave, at 2.5 times only the note's harmonics; ADD half and half; the WIN modes never jump; the envelope sweeps from far up back to the note; FM matches the textbook formula sample for sample (within 1%), from a shape and from another oscillator (which isn't heard); AM at full gives just the sum and difference tones, at half the right mix; the grids' ratios; LOCK makes every note the same; how much less folds back than the plain way; CPU; the FM page's layout. Not checked here: how it sounds (I can't listen).

## 0.4.0

Stage 3, part 2: **SHAPE**, bending each oscillator's wave while it plays, modelled on Avenger's shape controls (our own design and code: the manual says what each does, not how). Each oscillator has a new **SHAPE** page, between **WAVE** and **VOICES**.

- **BEND: SKEW** squashes one side of the wave and stretches the other (right of centre the left side, left of centre the right side): the rounded, buzzy sound of a "phase distortion" synth. **PINCH** zooms into the middle of the wave (right, smoothing it there and sharpening its edges) or out of it (left).
- **FORMANT** plays the wave faster with a gap after it (left, up to 8x), which gives a fixed, vocal resonance that stays put as you play up and down; or slower (right, up to 4x), so only its start fits in each cycle. Its **MODE** says what happens where the wave is cut: **CUT** leaves a hard edge (a buzzy click, as the manual describes Avenger's), **FADE** fades its end to silence, **X-FADE** fades both ends, **X-FADE 2** fades its end into its start, so it never jumps. The default is FADE (our choice).
- **CRUSH** for lo-fi: **RATE** holds the wave in steps (down to 3 a cycle), **BIT** rounds it to fewer levels (down to a square), **BOTH** does both.
- Works on every wave, wavetables included, and on unison copies and chord notes. The oscillator's wave picture shows the bent wave; the SHAPE page also shows the plain and the bent wave side by side, large.
- **Our names:** SKEW and PINCH are what Avenger calls XCITE side and mid (that's its own name, so not used here). Tell me if you'd like other names. The four FORMANT modes are our reading of the manual's short description.
- **Kept clean without oversampling** (what makes Avenger heavy):
  - SKEW and PINCH bend the wave with curves that are smooth all the way round, and read a table level with room for how much faster they play parts of the wave. Measured at A5 (880 Hz) on a saw: nothing between its harmonics above -116 dB, where doing it the plain way leaves -22 to -32 dB.
  - FORMANT and CRUSH make hard edges on purpose. Each edge is smoothed over the two samples around it, the way a clean edge would be. At A5: FORMANT 43 to 61 dB down, CRUSH 43 to 46 dB down, 8 to 18 dB cleaner than the plain way. Near the top of the keyboard (E7) hard edges are harder to keep clean (a cut saw is about 35 dB down there): CUT's clicks are the sound it's meant to make, and the fades are cleaner.
- **CPU:** nothing at all until you turn a SHAPE control. A supersaw chord of 2 oscillators x 7 copies x 8 keys took about 3% of one core plain, 6 to 10% with one SHAPE control on, and about 19% with SKEW, FORMANT and CRUSH all on at once. Your computer will differ.
- **Three new factory presets:** Formant Vox (A15), Phase Bass (A16) and Crushed Keys (A17). Our own, not tuned by ear.
- In the tests: SKEW moves a sine's peak (earlier or later), PINCH makes its middle gentler or steeper, FORMANT plays it faster with silence after (or only its first part), each MODE's edges (only CUT jumps), RATE's 3 steps a cycle, BIT's square (its 3rd harmonic at a third, no 2nd), the curves smooth and never running backwards, how much less folds back than the plain way, wavetables and copies bent too, and the CPU figures. Not checked here: how it sounds (I can't listen).

## 0.3.0

Stage 3, part 1: **wavetables**, modelled on Avenger's wavetable oscillator (our own design and code). Stage 3 comes in parts, each its own release: next are the wave-bending controls, then sync, FM and AM, then the FFT and freeform editors, then the shaper.

- **WAVETABLE**, a new choice in each oscillator's **WAVE** menu: a row of up to 256 single-cycle waves ("frames") that **INDEX** moves through, morphing the sound as it goes.
  - **TABLE** (in place of WIDTH) chooses the table: one of ours, or **Load WAV file...** to load your own (WAV, AIFF or FLAC). You can also drop a file straight onto the oscillator panel.
  - **INDEX**: where in the table. **MODE**: **SMOOTH** blends between neighbouring frames, **STEPPY** jumps from one to the next. **FRAMES**: how many of the table's frames INDEX moves through, spread evenly from the first to the last (fewer makes broader, simpler steps).
  - The wave picture shows the frames stacked from front to back, with the one INDEX points at drawn bright, and the table's name and frame number above.
  - Unison, vibrato, the chorder, the sub and noise all work with wavetables as with the other waves.
- **Ten factory tables of our own** (generated, not copied from anyone's): Analog Morph (sine, triangle, saw, square), Pulse Width, Harmonic Rise (harmonics added one by one), Vowels (a sung "ah eh ee oh oo"), Hard Sync, Wavefold, FM Sweep, Bit Steps, Comb Sweep and Organ Stack.
- **Your own tables:** a file written by another wavetable synth is cut into frames the size it says (the "clm" marker most of them write). Otherwise a short file (4,096 samples or fewer) is taken as a single cycle, and anything longer is cut into frames of 2,048 samples, the size most wavetable packs use. Files with more than 256 frames keep 256, spread through the file. Avenger's own tables can't ship with PR7-K, but any WAV wavetable you have will load.
- **Saved with your sound:** a loaded table is kept inside the preset and the DAW project (16-bit, compressed: a 64-frame table adds about 330 KB), so it plays the same on another computer even without the file. Undo covers loading and changing tables too.
- **No aliasing, no extra CPU for it:** every frame is stored at the same 28 brightness levels as the saw, so a wavetable stays clean right up the keyboard with no oversampling (measured: a very bright frame at 2.6 kHz is 96 dB down between its harmonics). A wavetable supersaw chord (2 oscillators x 7 copies x 8 keys) took about 3.4% of one core in the tests, against 2.5% for the same chord with saws. Tables take memory instead: about 16 MB for a 64-frame table (64 MB for a full 256-frame one), built when you choose it.
- **Three new factory presets:** Vowel Pad (A12), Sync Lead (A13) and Bit Bass (A14). Our own, not tuned by ear. Nothing moves INDEX by itself yet: modulation is stage 4, so for now INDEX is a knob to set or automate from your DAW.
- Not in this part: Avenger's wavetable envelope and step pattern that move INDEX (they're modulators, so they come with stage 4), and its resampler options for loading long samples at a chosen root note (with the FFT editor, part 4).
- In the tests: each frame keeps its own harmonics, each level only the harmonics it's allowed, INDEX picks and blends frames at the right levels, SMOOTH, STEPPY and FRAMES, INDEX moving without clicks, every factory table, cutting files into frames, saving and loading, and the plugin keeping a loaded table through the project, presets and undo. Not checked here: how it sounds (I can't listen).

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
