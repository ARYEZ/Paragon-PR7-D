# Changelog

Each version's notes appear on its GitHub Release page.

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
