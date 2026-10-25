# Changelog

Each version's notes appear on its GitHub Release page.

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
