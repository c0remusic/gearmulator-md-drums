# MD Drums UI spec, v14

v13, after the user asked twice where the samples are: nothing in the interface said it. The samples are the 16 E12
machines (12-bit samples that come with the OS; see `v12-ui-spec.md` for the measurements), and the browser named
them only "E12". Mockup: `v14-test.html` (`v14-ui.css`, `v14-ui.js`, `v6-machines.js`). Everything not listed here is
as in `v13-ui-spec.md` and the specs before it.

## What changed, and why

- **Each family says what it is, under its code**, in 12 px ink-dim: GND "générateurs", TRX "analogique", EFM "FM",
  E12 "samples d'usine", P-I "physique"; the full description in the tooltip. The Machinedrum's codes stay first:
  they are what its owners know and what the machines' names carry.
- **GND takes two slots** (240-400) so that "générateurs" fits; TRX goes to two sub-columns (400-560, seven rows).

## Browser grid

| Region | x (px) | y (px) | Content |
|---|---|---|---|
| Family code, count, rule | per family | 200-220 | As in v12 |
| What the family is | per family | 220-240 | GND 240-400, TRX 400-560, EFM 600-680, E12 680-840, P-I 840-920 |
| Machines | per family | from 240 | 40 px rows; P-I's ninth ends at 600 |

Checked in the browser: every browser cell on the 40 x 20 grid (measured at the page's 0.68 scale, corrected); no
text past its cell.
