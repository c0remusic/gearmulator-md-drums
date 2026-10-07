# MD Drums UI spec, v9

v8 (one column per page of the track: SYN, EFX, ROUTING; knobs 2 x 4 in the Machinedrum's order; groups named over
their knobs; screens under them; shared edges), with the user's review of v8: "il faudrait une démarcation entre le
titre (trx bd, toute la partie synthèse) et les éléments d'édition". Mockup: `v9-test.html` (`v9-ui.css`,
`v9-ui.js`, `v6-machines.js`). Everything not listed here is as in `v8-ui-spec.md`.

## What changed, and why

- **The track head is a band of its own**, in the chrome of the top bar and the track list. Everything that names or
  picks (the plug-in, the views, the tracks, the selected track's machine) is now dark; only what edits the sound is
  light. The edge between the two, at y 120, runs the width of the editing surface and meets the line between track
  rows 01 and 02.
- **The band follows the pages' columns.** Number, machine and ‹ › over SYN; the machine's family and where the track
  goes (output, last velocity) over EFX, on two lines starting at EFX's left edge; JOUER and the level over ROUTING,
  each control centred on a slot of the page below (slots 1 and 3), its label in the next slot.
- **A line under the top bar** (`line`, across the window): the top bar is the plug-in's, the band the track's, and
  both are chrome.
- **Head 80 px instead of 100**, the screens 20 px taller (220 px).

## Grid

| Region | x (px) | y (px) | Content |
|---|---|---|---|
| Head band (chrome) | 240-1280 | 40-120 | Number and machine in 32 px, ‹ › (480-560); facts on two lines from 616; JOUER ring centred on 1000, label from 1040; level knob centred on 1160, label and value from 1200 |
| Pages | 240-1280 | 160-700 | Titles 160-200; group names 200-220 and 340-360; knob rows 220-280 and 360-420; labels and values 280-320 and 420-460; screens 480-700 |
| Machine browser | 240-1280 | 120-720 | Title 160-200, families 200-220, machines from 220 |

Checked in the browser: every cell on the 40 x 20 grid; the band from (240, 40) to (1280, 120) in chrome; JOUER and
the level centred over ROUTING's slots 1 and 3; the facts start at EFX's left edge, for all 98 machines without
passing the column; the title, the facts block, the ring and the knob share one centre line (y 80); in each page one
left edge and one right edge; titles, knob rows, labels and screens level across pages; no text past its cell, none
under 11 px; all 33 ids of track 1, none repeated; the browser's title on the pages' title line.

## Controls

As in v8, except where they sit in the head:

| ID | Control | Position |
|---|---|---|
| `t<n>_machine` | Machine name in 32 px (opens the browser), ‹ › step | Head band, 320-480 |
| `t<n>_level` | Knob 48 px, label NIVEAU and value to its right | Head band, slot 3 of ROUTING (1120-1200) |
| JOUER | Ring 44 px, label to its right | Head band, slot 1 of ROUTING (960-1040) |
