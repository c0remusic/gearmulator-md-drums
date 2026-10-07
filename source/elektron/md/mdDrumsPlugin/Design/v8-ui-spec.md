# MD Drums UI spec, v8

v7 (Graphite, Inter, numbered outputs, three surface levels, no drawn grid), reworked after the user's review of v7:
"on dirait encore que les éléments sont posés au hasard", and the changes they picked: sub-groups, screens next to
their knobs, more air. Mockup: `v8-test.html` (`v8-ui.css`, `v8-ui.js`, `v6-machines.js`).

## What changed, and why

- **Nothing lined up with anything in v7.** The screens formed a column of their own, two modules away from the
  knobs; the screens' edges fell on the module grid while the knobs' circles sat 16 px inside it; ‹ ›, JOUER, the level
  and M S stood at columns of their own in the head. The grid was there, but no edge was shared, so the eye saw none.
- **One column per page of the track.** SYN, EFX and ROUTING side by side, each built the same way: the page's title,
  its eight knobs as the Machinedrum lays them out (two rows of four, its encoders A-D over E-H), then the screen that
  shows them. Within a column, the title, the group names, the knobs' circles and the screen share one left edge and
  one right edge; across the columns, the titles, both knob rows, both label rows and the screens share their tops.
- **Sub-groups.** EFX: AM and EQ over the first row, FILTRE over the second, SRR alone. ROUTING: CANAL (DIST VOL PAN)
  and ENVOIS (DEL) over the first row, LFO (LFOS LFOD LFOM) over the second, REV under DEL. A group's name sits over
  its first knob, and a rule runs to the edge of its last one: the line says how far the group goes.
- **Screens under their knobs.** FILTRE sits right over the filter curve, LFO right over the LFO's wave; the hit
  screen under all of SYN. In ROUTING, REV moves from E to H so that the sends stay together and the LFO's three knobs
  reach the screen.
- **The head on the same slots.** JOUER and the track's level are built like knobs (control, then label and value) and
  sit over ROUTING's slots 3 and 4. Mute and solo stay in the track list, on the selected track's row.
- **More air.** 40 px between the head and the pages, 72 px between the knobs of two pages (32 px within a page),
  section titles in 20 px.
- **The machine browser on the same columns**: its title where the pages' titles are, two families per column, each
  family named over its machines like a group over its knobs.

## Grid

40 px columns (32), 20 px rows in the editing surface (36). Pages: four 80 px slots each; content 16 px in from a
page's edges, so a knob's circle (48 px, centred in its slot) starts where the page's text starts.

| Region | x (px) | y (px) | Content |
|---|---|---|---|
| Top bar (chrome) | 0-1280 | 0-40 | Name (0-240), selection (240-560), tabs PISTE MIX SORTIES on 80 px slots (600-840), window size (960-1040), rate and latency (1040-1200), Main meter (1200-1280) |
| Track list (chrome) | 0-240 | 40-760 | Title; 16 rows of 40 px: number (plays), machine (selects), scope, M, S; footer |
| Head | 240-1280 | 40-140 | Number and machine in 32 px, ‹ › (480-560); facts at 100-120; JOUER over 1120-1200, level over 1200-1280 |
| SYN page | 240-560 | 180-700 | Title 180-220; knob rows 240-300 and 380-440; labels and values 300-340 and 440-480; hit screen 500-700 |
| EFX page | 600-920 | 180-700 | Same; group names at 220-240 and 360-380; filter and EQ screen |
| ROUTING page | 960-1280 | 180-700 | Same; LFO screen, its two shape rows at its foot |
| Footer | 240-1280 | 720-760 | Gestures; last note |
| Machine browser | 240-1280 | 140-720 | Over the pages: GND and TRX, EFM and E12, P-I and ROM |
| MIX | 0-1280 | 40-760 | 16 channels of 80 px (unchanged) |
| SORTIES | 0-1280 | 40-760 | Table and help (unchanged) |

Checked in the browser: every cell on the 40 x 20 grid; in each page one left edge (title, first group name, both
first knobs, screen) and one right edge, to within 1 px; titles, knob rows, label rows and screens level across the
pages; JOUER and the level centred over ROUTING's slots 3 and 4, on the title's centre line; no text past its cell;
nothing under 11 px; all 98 machine names fit the head and the list.

## Controls

Every control carries its parameter's ID as element `id` (track 1: all 33 present, none repeated).

| ID | Control | Position (PISTE) | Range | Default |
|---|---|---|---|---|
| `t<n>_machine` | Machine name in 32 px (opens the browser), ‹ › step | Head, 320-480 | 0-191 by id | TRX-BD..B2, EFM-BD, -SD, -HH |
| `t<n>_SYN1`..`SYN8` | Knob 48 px, label and value under it; named per machine, dimmed when unused | SYN, slots 1-4, rows 1 and 2 | 0-127 | 64 |
| `t<n>_AMD`..`SRR` | Same | EFX, encoder order | 0-127 | 0 0 64 64 0 127 0 0 |
| `t<n>_DIST`..`LFOM` | Same; DEL and REV dimmed until the master effects exist | ROUTING: DIST VOL PAN DEL over LFOS LFOD LFOM REV | 0-127 | 0 100 64 0 0 64 0 0 |
| `t<n>_level` | Knob 48 px (PISTE), fader with meter (MIX) | Head, 1200-1280 | 0-127 | 100 |
| `t<n>_mute`, `t<n>_solo` | Switches M and S | Track list (and MIX) | off/on | off |
| `t<n>_PAN` | Also a knob in MIX | MIX channel n | 0-127 | 64 |
| `t<n>_lfoTrack`, `t<n>_lfoParam`, `t<n>_lfoMode` | Words in the LFO screen's title (click to change) | ROUTING screen, top | 1-16, 0-23, FREE TRIG HOLD | n, FLTF, FREE |
| `t<n>_lfoShape1`, `t<n>_lfoShape2` | 6 shapes each | ROUTING screen, foot | TRI SAW SQR RMP EXP RND | TRI |

## Palette

v7's, plus `rule`.

| Token | Hex | Contrast on surface | Use |
|---|---|---|---|
| chrome | #1b1c1e | | Top bar, track list |
| surface | #26272a | | Editing |
| display | #151618 | | Screens (ink-faint 5.73:1, ink-dim 7.77:1 on it) |
| ink, ink-dim, ink-faint | #f2f2f2, #a7aaaf, #8e9197 | 13.34, 6.41, 4.73 | Text |
| accent, on-accent | #6fd1c4, #10201e | 8.25 | Selection, what is alive |
| mute | #ff7a5c | | Mute |
| track | #6e7177 | 3.05 | Knob tracks, rails |
| rule | #55585e | 2.09 | A group's extent (its name carries the meaning) |
| line | #3a3c40 | 1.35 | MIX channels, table head |
| tick | #3c3e43 | | Screen graduations |
