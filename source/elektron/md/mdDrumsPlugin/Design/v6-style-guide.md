# MD Drums style guide, v6 (Graphite, on a grid)

v5's rules hold (one grid, no cards, no tints or pills, one accent, screens as the only dark panels); the tokens
change where the critique measured them short.

## Tokens

| Token | Hex | Use | Contrast on #222325 |
|---|---|---|---|
| bg | #222325 | The one surface | |
| rule | #3a3c40 | Region and cell rules | 1.42:1 (structure) |
| row | #2c2e31 | Rules between list rows, screen grid | |
| screen | #18191b | Screens, selected row and strip, hover | |
| ink | #f2f2f2 | Values, names | 14.05:1 |
| ink-dim | #a3a6ab | Labels, captions, waveforms | 6.44:1 |
| ink-faint | #8a8d93 | Hints, footers | 4.73:1 (5.29:1 on screen) |
| accent | #6fd1c4 | Selection bar, trigs, curves, active chip | 8.68:1 |
| on-accent | #10201e | Text on an accent fill | 9.29:1 on accent |
| mute | #ff7a5c | Mute on | 7.36:1 with #111 |
| track | #6a6d73 | Knob track, meter background | 3.03:1 |

## Type

Inter only (OFL; bundled with `tnum` frozen into the default digits).

| Role | Weight | Size |
|---|---|---|
| Readout (track number) | Light 300 | 28 px |
| Value | Regular | 14 px, tabular |
| Machine name in the head | Regular, tracking 2 px | 15 px |
| Page title | Semibold, tracking 3 px | 12 px |
| Label, caption | Regular, tracking 1-1.5 px, captions upper case | 11 px |
| Hint, footer | Regular | 11 px, ink-faint |

Nothing under 11 px.

## States

| State | Look |
|---|---|
| Selected (row, strip, tab, machine) | 3 px accent bar on its edge (left for rows and machines, top for strips, bottom for tabs) |
| Switch on (chip, mute, solo) | Solid fill: accent, mute colour or ink, with dark text |
| Hover | Text to ink; cells #18191b |
| Focus (keyboard) | 1 px accent ring inside the cell |
| Unused or not yet working | 35 % opacity, reason in a tooltip |
| Trig | Track number in accent for 140 ms (not under reduced motion) |
| LFO target | Label in ink, 2 px accent underline |
