# MD Drums UI spec, v7

v6 (layout A, Graphite, Inter, numbered outputs, the playing track list, the critique's fixes), reworked after the
user's review of v6: "ça manque de hiérarchie entre les sections, c'est moyen lisible ; il y a des grilles partout
mais on ne sait pas trop pourquoi". Mockup: `v7-test.html` (`v7-ui.css`, `v7-ui.js`, `v6-machines.js`).

## What changed, and why

- **The grid places, it is not drawn.** v5-v6 drew a rule on every cell edge and a 40 px lattice in every screen:
  structure shown for its own sake. In v7 no rule separates cells; the 40 px module still positions everything
  (checked: no region of PISTE, MIX, SORTIES or the browser off the grid by more than 1 px). The only lines left say
  something: a screen's labelled graduations, the MIX's channel separators, the SORTIES table's head.
- **Three surface levels instead of rules.** Chrome (#1b1c1e: top bar, track list, SORTIES help) for navigation;
  surface (#26272a) for editing; display (#151618) for screens. Solid fills, no cards.
- **A stepped type scale.** The track's title in 32 px (number light, machine regular), a line of facts under it
  (family, output, velocity); each section headed by its name in 15 px semibold and what it holds in 12 px; then
  11 px labels over 14 px values; hints in 11 px.
- **Space between sections.** Each section's header line sits at the bottom of its own grid row, so the row above it
  reads as space.
- **Screens with meaning.** Hit: time in seconds (0 to 0.6 s) and the zero line. Filter: 100 Hz, 1 kHz, 10 kHz and
  0 dB. LFO: its zero line. Every graduation is labelled.
- **The LFO grouped.** Destination and mode in its screen's title; its two shape rows under the screen.

## Grid

40 px modules, 32 x 19.

| Region | Columns | Rows | Content |
|---|---|---|---|
| Top bar (chrome) | 1-32 | 1 | Name, status, tabs PISTE MIX SORTIES (accent underline on the open one), window size, rate and latency, main meter |
| Track list (chrome) | 1-7 | 2-19 | Title; 16 rows (number plays, machine selects, scope, M, S); footer |
| Track head | 8-32 | 2-4 | Number and machine in 32 px (cols 8-16), ‹ › (17, 18); facts line (row 4); JOUER (24-25), kit level knob (26-27) with value (row 4), M (29), S (30) |
| SYN | 8-32 | 5-8 | Header line (row 5); knobs rows 6-7, label and value row 8; Hit screen cols 26-32, rows 6-8 |
| EFX | 8-32 | 9-12 | Same; Filter / EQ / AM screen |
| ROUTING | 8-32 | 13-18 | Same (knobs 14-15, labels 16); LFO screen cols 26-32, rows 14-16; shapes 1 and 2 cols 27-32, rows 17-18 (row numbers in col 26); note on DEL and REV row 17 |
| Footer | 8-32 | 19 | Gestures; last note |
| Machine browser | 8-32 | 5-19 | Over the sections: one column per family |
| MIX | 1-32 | 2-19 | 16 channels of 2 modules: number, machine, PAN, meter and fader, value, M S, output |
| SORTIES | 1-32 | 2-19 | Table (1-20), help (21-32, chrome) |

## Controls

Every control carries its parameter's ID as element `id` (checked for track 1: all 33 present, none repeated).

| ID | Control | View: position | Range | Default |
|---|---|---|---|---|
| `t<n>_machine` | Machine name in 32 px (opens the browser), ‹ › step | PISTE: cols 10-16, rows 2-3 | 0-191 by id | TRX-BD..B2, EFM-BD, -SD, -HH |
| `t<n>_SYN1`..`SYN8` | Knob 48 px, label over value; named per machine, dimmed when unused | SYN: knob k cols 2k+6..2k+7, rows 6-7; label row 8 | 0-127 | 64 |
| `t<n>_AMD`..`SRR` | Same | EFX: rows 10-11, label 12 | 0-127 | 0 0 64 64 0 127 0 0 |
| `t<n>_DIST`..`LFOM` | Same; DEL and REV dimmed until the master effects exist | ROUTING: rows 14-15, label 16 | 0-127 | 0 100 64 0 0 64 0 0 |
| `t<n>_PAN` | Knob 48 px, also | MIX: channel n, rows 4-5 | 0-127 | 64 |
| `t<n>_level` | Knob 48 px (PISTE), fader with meter (MIX) | PISTE: cols 26-27, rows 2-3; MIX: rows 6-16 | 0-127 | 100 |
| `t<n>_mute`, `t<n>_solo` | Switches M and S | Track list cols 6 and 7, row n+2 (head 29, 30); MIX row 18 | off/on | off |
| `t<n>_lfoTrack`, `t<n>_lfoParam`, `t<n>_lfoMode` | Words in the LFO screen's title (click to change) | PISTE: row 14, cols 26-32 | 1-16, 0-23, FREE TRIG HOLD | n, FLTF, FREE |
| `t<n>_lfoShape1`, `t<n>_lfoShape2` | 6 shapes each | PISTE: rows 17 and 18, cols 27-32 | TRI SAW SQR RMP EXP RND | TRI |

## Palette

| Token | Hex | Contrast of text on it (ink / ink-dim / ink-faint) |
|---|---|---|
| chrome | #1b1c1e | 15.23 / 7.32 / 5.40 |
| surface | #26272a | 13.34 / 6.41 / 4.73 |
| display | #151618 | 16.17 / 7.77 / 5.73 |
| ink, ink-dim, ink-faint | #f2f2f2, #a7aaaf, #8e9197 | |
| accent, on-accent | #6fd1c4, #10201e | accent 8.25:1 on surface |
| mute | #ff7a5c | |
| track (knob track, rails) | #6e7177 | 3.05:1 on surface |
| line (MIX channels, table head) | #3a3c40 | |
| tick (screen graduations) | #3c3e43 | |
