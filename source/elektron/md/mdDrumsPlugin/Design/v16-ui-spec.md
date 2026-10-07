# MD Drums UI spec, v16

v15 in a 1280 x 800 window, the user's choice among v15's options (1.600: a golden rectangle that stays on the 40 x 20
grid). Mockup: `v16-test.html` (`v16-ui.css`, `v16-ui.js`, `v6-machines.js`). Everything not listed here is as in
`v15-ui-spec.md` and the specs before it.

## What changed

- **The window is 1280 x 800**: 32 columns of 40 px, 20 rows.
- **The 40 px it gains go around the pages**: 60 px between the head band and the pages (v15: 40), 60 px between the
  pages and the footer (v15: 40). The pages keep their golden size, 320 x 520, now at y 180-700; the screens 288 x 180
  at y 520-700.
- **The track list** keeps its 16 rows of 40 px (80-720); its footer moves to 760-800, level with the editing
  surface's footer.
- **MIX**: each channel's fader gains a row (y 200-680); value, M S and output follow (680-800).
- **The browser** follows the pages: title on their title line (180-220), families from 220, ROM's 32 samples at
  260-420, the preview screen at the pages' screen place (976-1264, 520-700), "Écouter en choisissant" at 700-740.
- **ROM's tooltip** says what the Machinedrum's manual (OS 1.63) says of the slots: 48 per bank on a MKII, 2.5 MB of
  sample memory, ROM-25 to -48 optimised for loops; and what the flash image holds: 32 samples, about 970 KiB.

## The ROM machines in the plug-in, measured

The browser offers ROM-01 to -32, the samples of the factory bank; in MD Drums' engine they are silent today. Measured
with a probe on `mdDrums::Engine` (one hit at velocity 100, STRT 0, END 127, no retrig): TRX-BD peaks at -20.1 dBFS,
E12-BD at -16.0 dBFS, ROM-01, -02, -13, -32 and -33 at 0. The Machinedrum's OS copies the bank from its flash into the
voice DSP at boot (the sample data at P:$150000-$18FC12 and a directory at P:$147E00, 4 words per slot: start, length,
loop, flags; Machinemodule's HANDOFF, 2026-09-28); the engine runs without the OS, so nothing makes that copy. Until it
does (Machinemodule's way: boot the full emulated MD once, keep DSP2's P memory from $140000 up, write it into the
engine with `VoiceEngine::writeP`), the browser should not offer ROM.

## Grid

| Region | x (px) | y (px) |
|---|---|---|
| Top bar | 0-1280 | 0-40 |
| Track list | 0-240 | 40-800 (rows 80-720, footer 760-800) |
| Head band | 240-1280 | 40-120 |
| Pages | 240-560, 600-920, 960-1280 | 180-700 |
| Screens | 16 px inside each page | 520-700 |
| Footer | 240-1280 | 760-800 |

Checked in the browser, in PISTE, the machine browser and MIX: the frame 1280 x 800; every cell on the 40 x 20 grid
and none below 800; no text past its cell, none under 11 px; the pages 320 x 520 at 180-700, the screens 288 x 180 at
520; the browser's title bottom at 220, its preview screen at (976, 520)-(1264, 700); MIX's output line at 760-800.
