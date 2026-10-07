# MD Drums UI spec, v5

Layout A (track list + pages, after Overbridge), look Graphite, outputs numbered and one font (Inter), as in v4.
v5 answers the user's "design sur une grille, pas de cartes semi colorées IA": the whole UI sits on one module grid,
regions are divided by rules on its lines, and the rounded, tinted cards and pills are gone.
Framework: RmlUi (`juceRmlUi`). Mockup: `v5-test.html`, drawn by `v5-ui.css` and `v5-ui.js` (its "Afficher la
grille" button overlays the grid).

## Grid

One module of **40 px**; the window is **32 x 19 modules** (1280 x 760). Every region, row, cell and control box
starts and ends on a grid line; a 1 px rule takes the last pixel of the module it closes. Checked in the mockup:
no region of PISTE, MIX or SORTIES is off the grid by more than that pixel.

| Region | Columns | Rows | Content |
|---|---|---|---|
| Top bar | 1-32 | 1 | Name (1-6), status "KIT · track · machine" (7-14), tabs PISTE / MIX / SORTIES (2 modules each, 15-20), rate and latency (25-28), main output meter (29-32) |

### PISTE

| Region | Columns | Rows | Content |
|---|---|---|---|
| Track list | 1-6 | 2-19 | Title row; 16 track rows (number 1, machine 2-4, level 5, mute 6); footer (how many outputs the host has taken) |
| Track head | 7-32 | 2-3 | Number (2 x 2), machine selector and family (6 x 1 each), output (6 x 2), LEVEL knob (2 x 2), mute (2 x 2) |
| Page SYN | 7-32 | 4-8 | Title row; 8 knobs of 2 x 2 modules in one row (as the machine's 8 encoders); labels; values. Hit screen 10 x 5 on the right |
| Page EFX | 7-32 | 9-13 | Same; Filter / EQ / AM screen |
| Page ROUTING | 7-32 | 14-18 | Same; LFO screen, its bottom row 10 cells: 6 shapes, 3 modes |
| Footer | 7-32 | 19 | Note map (36-51 → tracks 01-16), last note received and its velocity |

### MIX

16 strips of **2 modules** (80 px: 16 x 80 = 1280), rows 2-19: number, machine, PAN (2 rows), DEL, REV (dimmed
until the master effects exist), fader and meter (9 rows), value, mute, destination (`MAIN` or `→ 01`..`→ 16`).
The main output's meter lives in the top bar.

### SORTIES

| Region | Columns | Rows | Content |
|---|---|---|---|
| Table | 1-20 | 2-19 | Head, `MAIN` row, 16 rows: track (2), machine (4), output `01`..`16` (3), state (7), level (4) |
| Help | 21-32 | 2-19 | Separate outputs, taking one in Live, what MAIN carries; titles on grid rows 1, 6 and 11 |

## Outputs

Track `n` has output `n`, shown as `01`..`16`; the buses are named `Out 01`..`Out 16` (mono) and `Main` (stereo).
Code change: `Processor::buses()` names them so (today "Output" and "Track 1".."Track 16").

## Controls

Unchanged from v4 (`parameter-spec.md`): per track the machine, 24 parameters, level, mute, and the proposed LFO
destination, shapes and mode. The knob an LFO modulates shows its label in the accent colour.

## Palette (Graphite, flat)

| Token | Hex | Use |
|---|---|---|
| bg | #222325 | The one surface |
| rule | #3a3c40 | Region and cell rules |
| grid | #2c2e31 | Rules between rows of a list; screen grid |
| screen | #18191b | Screens; selected row and strip |
| ink | #f2f2f2 | Values, names |
| ink-dim | #a3a6ab | Labels |
| ink-faint | #6c6f75 | Hints, inactive |
| sel | #6fd1c4 | Selection bar, curves, active chip (solid), taken outputs |
| track | #4a4d52 | Knob track, meter background |
| warn | #ff7a5c | Mute on |
