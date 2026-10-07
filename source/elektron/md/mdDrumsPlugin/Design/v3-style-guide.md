# MD Drums style guide, v3 (Graphite)

## Colours

See `v2-ui-spec.md`, Palette. One accent only (#6fd1c4); #ff7a5c is kept for mute and warnings.

## Type

| Use | Font | Size |
|---|---|---|
| Machine names, values, labels, status screen | Monospace (Consolas / Cascadia Mono; a bundled mono font in the skin) | 11-13 px, letter-spacing 1 px |
| Track number in the head | Monospace | 22 px, letter-spacing 2 px |
| Tabs, captions, page titles | Sans (Segoe UI / system) | 11-12 px, page titles letter-spacing 2 px, weight 600 |
| Plug-in name | Sans bold | 15 px, letter-spacing 3 px |

## Spacing

- Outer padding 16-18 px; between pages 9 px; inside a page 8 x 12 px.
- Track rows 38 px high, 3 px apart; corner radius 3 px (pages 4 px).
- Knob grid 4 x 2 per page; screen column 330 px, 12 px from the knobs.

## Controls

| Control | Idle | Hover | Dragging | Disabled | Modulated |
|---|---|---|---|---|---|
| Knob (46 px) | Track #4a4d52 3.5 px, arc white from 7 o'clock to the value, body #34363a, white pointer | Label in ink | Value shown, vertical drag 1.5 px per step, double click = default | Arc and label in ink-faint | Label in #6fd1c4 (LFO destination) |
| Track row | #34363a | #3d4045 | | | Number flashes #6fd1c4 for 160 ms on a trigger |
| Selected row | #3d4045, 3 px #6fd1c4 bar on the left | | | | |
| Mute | 1 px outline ink-faint | | | | On: filled #ff7a5c, black M |
| Chip | #34363a, ink-dim | ink | | | On: #6fd1c4, dark text |
| Tab | ink-dim | ink | | | On: #3d4045, 2 px #6fd1c4 underline |
| Screen | #1a1b1d, 1 px hair, grid #2a2c2f, curve 2 px #6fd1c4, secondary curve #e8e8e8 | | | Curve ink-faint when the depth is 0 | |
| Fader (MIX) | Rail 4 px #4a4d52, cap 26 x 14 px ink with a dark centre line | | Vertical drag over the rail's height | | |
| Meter | 6 px, #1a1b1d, fill #6fd1c4 to 70 %, pale yellow to 90 %, #ff7a5c at the top | | | Empty when muted | |
| Strip (MIX) | #26272a, 1 px hair | | | Sends at 40 % opacity until the master effects exist | Selected: #34363a, 2 px #6fd1c4 top line |
| State pill (SORTIES) | #3d4045, ink-dim, radius 10 px | | | | Separated: #6fd1c4, dark text |
