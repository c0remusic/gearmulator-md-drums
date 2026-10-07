# MD Drums style guide, v5 (Graphite, on a grid)

## Rules of the look

- **One grid.** 40 px modules, 32 x 19. Sizes are whole modules (a knob box 2 x 2, a strip 2 wide, a row 1 high);
  nothing is placed by eye.
- **No cards.** One flat surface. Regions are told apart by 1 px rules on grid lines, not by boxes with their own
  fill, shadow or corner radius. Corner radius is 0 everywhere.
- **No tints, no pills.** No semi-transparent coloured fills, no rounded badges. A state is said by text colour or
  a solid inversion (an active chip, a muted M), never by a pale tinted background.
- **One accent.** #6fd1c4 marks what is selected or alive (selection bar, curves, taken outputs, active chip).
  #ff7a5c marks mute only.
- **Screens are the only dark panels**: they are displays (#18191b), with their own grid drawn on the module grid.

## Type

Inter for everything, bundled as TTF (SIL Open Font License) with `tnum` frozen into the default digits
(`pyftfeatfreeze -f tnum`; its `-S` option renames the family, which the OFL asks for when the font declares a
Reserved Font Name: check Inter's licence).

| Use | Weight | Size |
|---|---|---|
| Captions, column heads, screen titles | Regular, upper case, letter-spacing 2 px | 10 px, ink-dim |
| Page titles (SYN, EFX, ROUTING) | Semibold, letter-spacing 3 px | 11 px |
| Knob labels | Regular, letter-spacing 1 px | 11 px, ink-dim |
| Values, machine names | Regular | 13-14 px |
| Output numbers | Regular | 15 px |
| Track number in the head | Light | 28 px |
| Plug-in name | Bold, letter-spacing 3 px | 14 px |

Text sits 12 px from the left rule of its cell, or centred in it.

## Controls

| Control | Look | States |
|---|---|---|
| Knob (48 px in a 2 x 2 box; 36 PAN, 24 sends) | 270 degree track 2 px #4a4d52, value arc 2.5 px ink, pointer 2 px ink; no body | Drag vertically, 1.5 px per step; double click = default; LFO target: label in accent |
| Track row | Text ink-dim on the surface | Hover: ink. Selected: #18191b fill, 3 px accent bar on the left, number in accent. Trigger: number flashes accent 160 ms |
| Mute | 20 px square, 1 px ink-faint outline, "M" | On: solid #ff7a5c, dark M |
| Tab | Text ink-dim between rules | On: ink, 2 px accent line at the bottom |
| Chip (LFO) | Text in a 1 x 1 cell between rules | On: solid accent, dark text |
| Fader | 2 px rail #4a4d52, cap 24 x 10 px ink; 4 px meter beside it | Drag over the rail's height |
| Meter | 3-4 px #4a4d52 track, accent fill | Empty when muted |
| Screen | #18191b, title row with a rule, grid #2c2e31 on the module grid, curve 2 px accent, waveform 1.2 px #e8e8e8 | Curve ink-faint when the depth is 0 |
