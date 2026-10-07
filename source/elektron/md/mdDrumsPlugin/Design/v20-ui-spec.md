# MD Drums v20 — UI spec

v19's arrangement placed on a column grid the way Figma lays one out, in English, without the bar of shortcuts.

## Window and grid

- 1280 × 800 px.
- **Columns:** 16 of 64 px, gutters 16 px, margins 8 px (column *c* starts at x = 8 + 80 (c − 1)). The 80 px pitch
  is MIX's 16 strips.
- **4 px, both ways** (the user: multiples of 4, always, horizontally too): every box's sides, every padding, margin
  and gap, every left-aligned text's start and every text baseline is a multiple of 4. 1-2 px strokes (rules,
  borders, underlines, the fader's rail) are centred on such positions. Baselines are measured with a 0 × 0 inline-block on the baseline; where Inter
  puts one 1-1.5 px off in its box, the text moves inside the box (padding), the box stays on the grid.
- **Rule:** every element is a box whose left and right sides lie on column edges (a box narrower than a column puts
  one side on an edge); its text starts on the box's left side or is centred in it. Backgrounds run to the middle of a
  gutter (x 240 between the track list and the editor) or to the window's edge. Inside a component (the name's field,
  a screen) the content may be inset: screens and the text above them by 16 px (one gutter).
- Checked in the browser: every `.cell` and rule of every view (both play and arrows choices, browser, MIX, LFO menu)
  is on the grid except the interiors of the name's field and the caption arrows.

## Regions

| Region | Columns | y |
|---|---|---|
| Top bar | 1–16 | 0–40 |
| Track list | 1–3 (background to x 240) | 40–800; header baseline 108, 16 rows of 40 from 128, kit 768–792. A row: number x 8–32, machine from x 40, scope x 132–164, M x 172–196 and S x 200–224 (a pair 4 px apart, 16 px from the panel's edge); the shown row lit to x 240, a 4 px accent bar at x 0 |
| Head band | 4–16 | 40–128 |
| Bands SYN, EFX, ROUTING | 4–16 | 128–352, 352–576, 576–800 (224 each, to the window's bottom) |
| Knobs (one per column) | 4–11 | band + 88 to + 152 (64 × 64) |
| Screens | from column 12 to the window's right edge (x 888–1280); content inset 16 px from column 12's start and column 16's end | each band's full height |

Inside a band (224 px, its 160 px of content centred): title + 32, groups + 64, knobs + 88, labels + 160, values + 176.

## Rules

Everywhere or nowhere (the user): a rule under the top bar (full width) and over each band, across the editing frame
(x 240, where its surface starts, to the window's edge); the screens fill their bands, so their tops are the rules.
Without them the sections lost their hierarchy.

## Head band (y 40–128)

Baselines: the caption `TRACK 01`, its arrows and the info's first line at 68, the machine's name (34 px) and the
info's second line at 108: 20 px over the caption's capitals, 20 under the name. The name on columns 4–7; on columns
12–16, inset 16 px like the screens' titles: the family and what it is, the output. Two choices:

| Arrows (`data-arrows`) | |
|---|---|
| `ends` | ‹ and › at the two ends of an outlined field (columns 4–7), the name centred (26 px) between them |
| `stepper` | ▲ ▼ stacked at the right end of the name's underlined field |
| `caption` | **chosen:** small ‹ › right after `TRACK 01` (column 5): previous and next **track**; the machine changes from its name |
| `wheel` | none: scroll or ↑ ↓ on the name; click opens the browser |

| Play (`data-play`) | |
|---|---|
| `key` | **chosen:** a ▶ key 64 × 64 on column 16; on column 15 the velocity it plays with (drag) |
| `screen` | the SYN screen plays on click; ▶ in its title |
| `list` | no control in the band: the list's numbers (▶ on hover) and Space |

Scroll and ↑ ↓ on the name step through the machines in every version; a click opens the browser. The top bar no
longer shows the sample rate and latency.

## LFO target (both ways, the user's choice on 2026-10-07)

The LFO screen's title: `LFO →`, the target (`T01 · TRX-BD · FLTF ▾`), then `ASSIGN` and the mode (FREE/TRIG/HOLD).

- **Menu:** clicking the target opens a menu over the LFO's whole screen: its title (`LFO 01 MODULATES`, the track
  and its machine, `Done`), the 16 tracks, then the chosen track's 24 parameters by their names on its machine, a row
  per band. Choosing a parameter, `Done` or Esc closes it.
- **ASSIGN:** lit while waiting; every usable knob is ringed as a target; a click on one, on this track or on another
  chosen in the list, makes it the target. Esc or ASSIGN again cancels.

A knob modulated by any LFO (with depth) has its name underlined; its tooltip names the LFOs.

## Screens

- SYN: the machine's real hit (`v20-hits.js`, rendered by `mdDrums::Engine` with the machine's SYN defaults: 0.8 s,
  min/max over 384 columns), scaled by the last velocity. Dim until the track plays; on a hit, an accent playhead
  crosses the screen in 0.8 s and what it has passed turns to ink (only this canvas redraws meanwhile). Over it, the
  amplitude envelope in accent (v19's, the user's "ADSR control"): the real hit's peaks, so it hugs the waveform at
  the defaults; DEC stretches it and HOLD holds its start. In the mockup that is a model; the plug-in re-renders the hit
  with a second engine (measured: 22-27 ms for 0.8 s, +88 MB) and draws the envelope of that. When the SYN knobs
  differ from the defaults, the title says so.

## Top bar

The logo, the tabs (TRACK on column 4, MIX on 5, their text at the column's start), the window's size (column 13),
the main output's meter (column 16). The open tab's mark: 2 px of accent under its text only, on the bar's bottom
edge, drawn over the bar's rule.

## Play key

A hit lights the ▶ key's border and glyph in accent for 140 ms; nothing fills (a full fill on every hit was too
intense, the user).
- EFX: filter and EQ curve. ROUTING: LFO shape, its two shape rows.
- Plots are inset 16 px on both sides: nothing touches a screen's edge.

## Controls

Unchanged from v19 (`t<n>_<name>` ids): machine, SYN1–8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST VOL PAN DEL REV LFOS
LFOD LFOM, level, mute, solo, lfoTrack, lfoParam, lfoShape1, lfoShape2, lfoMode. DEL and REV wait for the master
effects. Shortcuts are in tooltips: knobs (drag, Shift fine, double-click default, scroll), the browser's title (↑ ↓,
← →, Enter, Esc).

## Palette

Unchanged (v19): chrome `#1b1c1e`, surface `#26272a`, display `#151618`, line `#3a3c40`, rule `#55585e`, ink `#f2f2f2`,
ink-dim `#a7aaaf`, ink-faint `#8e9197`, accent `#6fd1c4`, mute `#ff7a5c`.
