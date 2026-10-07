# MD Drums UI spec, v15

v14, with two requests from the user: lay the design out with the rules of Figma's guide to the golden ratio
(figma.com/resource-library/golden-ratio), and, on the samples, "il n'y a que 16 samples ? ça m'étonnerait" (they were
right). Mockup: `v15-test.html` (`v15-ui.css`, `v15-ui.js`, `v6-machines.js`). Everything not listed here is as in
`v14-ui-spec.md` and the specs before it.

## The golden ratio, where it applies

The guide's rules: φ = 1.618 = a/b = (a + b)/a; golden rectangles; columns and rows divided by φ; font sizes, margins
and spacing in steps of φ. Its own limits: keep the main layout symmetric and use φ for the details, and do not
follow it where it hurts usability or readability. v15 keeps the three equal page columns (the Machinedrum's three
pages, one anatomy) and the 40 x 20 grid that gives the shared edges, and puts φ in the details, each size the
nearest one on the grid:

| Proportion | v14 | v15 | Off φ |
|---|---|---|---|
| Values, page titles, machine name | 14, 20, 32 px | 13, 21, 34 px (Fibonacci) | 21/13 = 1.615, 34/21 = 1.619 |
| Screens | 288 x 220 (1.309) | 288 x 180 (1.600) | -1.1 % |
| Pages, title to screen bottom | 320 x 540 (1.688) | 320 x 520 (1.625) | +0.4 % |
| Window | 1280 x 760 (1.684) | unchanged | +4.1 % |

Measured in the browser: the three pages 320 x 520, the three screens and the browser's preview 288 x 180 at y 500,
type at 13, 21 and 34 px. "Afficher la grille" outlines the golden rectangles (pages and screens) in dashed gold.

Not applied, and why: dividing the editing surface 618/382 into unequal columns would break the three pages' shared
anatomy, which the user chose in v11 (rules over every row of every page); and a 1280 x 791 window (exactly golden)
leaves the 20 px grid; 1280 x 800 (1.600) stays on it, if the window may grow by 40 px.

## The samples, corrected

v12 to v14 said the ROM slots were empty. That was wrong. The flash image holds the UW bank at 0x100000: the magic
0xABCD, then the count 0x0020 = 32, then about 970 KiB of 16-bit big-endian audio, the samples packed one after the
other (the first, 0x18CD = 6349 samples long, 144 ms, is followed by an attack 140 ms in). ROM-01 to -32 play them;
ROM-33 to -48 have nothing. v12's "flat buzz" came from the bench: all eight SYN knobs at 64 put a ROM machine's STRT
and END both in the middle of its sample and RTRG on, so it looped a sliver.

| Where | What | How many |
|---|---|---|
| The OS | E12, 12-bit samples (DSP2's table at `P:$103D7B` lists 21 descriptors) | 16 machines |
| The UW bank in the flash image | The owner's samples (factory set or a previous owner's: unknown) | 32 of 48 slots, about 970 KiB |
| RAM machines (RAM-R1..R4 record, RAM-P1..P4 play, on the hardware) | Live recording | Not in this engine (Machinemodule dropped them) |

No sample name was found in plain text in the flash (scans of every stride from 4 to 256 bytes); they are shown by
their numbers, 01 to 32.

## What changed in the browser

- **ROM is back, with its 32 samples**, in the third column (960-1280, y 200-400): ROM 32, "samples chargés", 01-32 in
  eight sub-columns of 40 px; ROM-33 to -48 are not offered. 82 machines in all.
- **The preview moves under ROM**: APERÇU (420-440), the machine and what it is (440-480), its screen at the pages'
  screen place (976-1264, 500-680), the switch "Écouter en choisissant" under it (680-720). The list of its eight
  parameters is gone: it showed again what SYN shows once the machine is kept.

Checked in the browser: every cell on the 40 x 20 grid; no text past its cell, none under 11 px; all 82 machine names
end before ‹ at 34 px; a click on ROM-05 loads it, plays it and keeps the browser open; ↓ gives ROM-06; past ROM-32 the
list starts again at GND; Échap returns to TRX-BD.
