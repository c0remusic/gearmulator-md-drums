# MD Drums UI spec, v17

v16, with the user's request on seeing v6 again: "je préférais aussi avec les écrans à côté et les catégories en
ligne, tout en gardant le layout actuel". Mockup: `v17-test.html` (`v17-ui.css`, `v17-ui.js`, `v6-machines.js`).
Everything not listed here is as in `v16-ui-spec.md` and the specs before it (head band, group rules with an
unnamed rule over SYN, Fibonacci type, 1280 x 800, the browser's behaviour, ROM's state in the plug-in).

## What changed, and why

- **Three bands again, as in v6**: SYN, EFX, ROUTING one under the other, each its title, its eight knobs in a row in
  the Machinedrum's order (encoders A-H), and its screen right after them.
- **ROUTING in the Machinedrum's order again**: DIST VOL PAN DEL REV LFOS LFOD LFOM. In one row every group is
  contiguous (CANAL, ENVOIS, LFO), so v8's move of REV is no longer needed; the knobs a screen shows end right before
  it (FILTRE and SRR before the filter curve, the LFO's three before its wave).
- **Each screen is attached to its band**: it starts 32 px after the eighth knob's circle, the space between two
  knobs, shares the band's top and bottom, and all three share one left edge (896) with the head's facts.
- **The golden ratio moves between the knobs and the screens**: 640 px of knobs to 400 px of screen, 1.6.
- **The machine browser follows**: the families over the eight knob slots (GND 240-400, TRX 400-560, EFM 560-640,
  E12 640-800, P-I 800-880), ROM and the preview over the screens (880-1280), the preview's screen at ROUTING's
  screen place.

## Grid

| Region | x (px) | y (px) |
|---|---|---|
| Head band | 240-1280 | 40-120; facts from 896 |
| SYN band | 240-1280 | 160-320 |
| EFX band | 240-1280 | 360-520 |
| ROUTING band | 240-1280 | 560-720 |
| In a band | knobs 240-880 (eight slots of 80), screen 896-1264 | title 0-40, rule and group names 40-60, knobs 60-120, labels and values 120-160 |
| Footer | 240-1280 | 760-800 |

Checked in the browser: every cell on the 40 x 20 grid; in each band the title and the first knob's circle start at
256, the eighth knob's circle ends at 864, the screen spans 896-1264 over the band's height (368 x 160); the three
screens and the head's facts share x 896; every group rule ends at its last knob's edge; the knob orders are the
Machinedrum's; no text past its cell, none under 11 px; the browser on the grid, its title on SYN's title line, its
preview screen at (896, 560)-(1264, 720), a click on E12-SD loads it and plays it.
