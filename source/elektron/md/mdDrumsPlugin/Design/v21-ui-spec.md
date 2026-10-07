# MD Drums v21 — UI spec

v20 (see `v20-ui-spec.md`: grid, regions, head band, rules, screens, LFO target) with the v20 critique applied in full,
the user's go on 2026-10-07 ("oui", "fais tout le reste aussi"), and type in even sizes (the user: multiples of 2).

## Design plan (frontend-design pass)

- **Subject:** the Machinedrum's 16-track engine inside a Live set. **Audience:** the user, who knows the MD.
  **Primary job:** pick a track, shape its machine, hear and see the hit.
- **Colour:** chassis `#1b1c1e`, panel `#26272a`, screen `#151618`, silkscreen `#f2f2f2` / `#a7aaaf` / `#8e9197`,
  controls' border `#6e7177`, accent teal `#6fd1c4`, mute `#ff7a5c`.
  Review against the generic defaults: a near-black ground with one bright accent is a stock look, but the user's
  Graphite brief pins it, so the change is in the accent's use: from nine roles to one, "live now".
- **Type:** Inter; 12 labels, 14 body and values, 22 titles, 34 the machine's name (ratios 1.17, 1.57, 1.55). The
  Machinedrum's own names (SYN, EFX, ROUTING, PTCH, DEC, TRX, FREE...) in capitals with 1 px tracking, as the machine
  prints them; every other word in sentence case. No middle-dot strings, no arrows in labels.
- **Layout:** v20's, unchanged (16 × 64 px columns, 16 px gutters, 8 px margins; multiples of 4 everywhere).
- **Principle, the one bold thing:** the knobs show the sound design. At its default a knob recedes (a dim pointer,
  no arc, its value in faint grey); edited, its arc, pointer and value turn to ink. Where 64 is neutral (EQG, PAN, a
  sample's PTCH on E12 and ROM machines) the arc starts at the top.

## Changes from v20

| Area | v21 |
|---|---|
| Knobs | default-aware (above); bipolar arcs; MIX's 16 PAN knobs read as centred |
| Accent | only the shown track's bar, the open tab's mark, a hit on the play key, the playhead, a modulated knob's underline, ASSIGN armed, live meters |
| Chosen options | ink on chassis: LFO shapes and mode, the LFO menu's track and parameter, the browser's current machine, "Listen while choosing" |
| Screens | data in silkscreen grey: the hit's envelope, the filter and EQ curve, the LFO's wave; the hit's waveform grey until heard, ink after |
| Head band | the machine's browser: an open chevron after the name, the name underlined on hover; "TRX modelled analog"; "Plays on Out 01"; "Velocity" with a 4 px bar under its value (drag) |
| Top bar | "MD Drums"; tabs "Track", "Mix"; "Size 100%" (click: 100, 125, 150%) on columns 13-14 |
| LFO | its title "LFO", the target "FLTF on track 01 ▾", ASSIGN (24 px tall); at the foot three rows of chips: Shape 1, Shape 2, Mode (FREE, TRIG, HOLD) — the mode a visible choice, no longer a word that cycles |
| Track list | M x 168-192 and S x 200-224 (8 px apart, as in MIX), scope x 128-160 |
| MIX | M and S a pair centred in the strip's column (x +4..28 and +36..60) |
| Borders | every control's border `--track` (3.5:1 on the chassis, WCAG 1.4.11): M/S, the play key, ASSIGN, pickers, fields, checkboxes |
| Words | "Hit", "Filter and EQ", "Last hit, velocity 110", "Not played yet", "Waveform at the machine's default SYN", "Choose a machine for track 01", tooltips with semicolons |

## Checked (2026-10-07, every view and every arrows × play combination, the browser, the LFO menu, ASSIGN, MIX)

- Boxes: 0 off the 4 px grid; no text overflows its box.
- Text baselines: 0 off a multiple of 4 (probe on the baseline; 1-8 px paddings move text inside its box where Inter
  puts it off the step; the user chose aligned baselines over 4 px paddings there).
- Font sizes in use: 12, 14, 22, 34 only.
- Contrast (WCAG): text 4.7:1 or more everywhere; controls' borders 3.5:1 on the chassis, 3.05:1 on the panel.
