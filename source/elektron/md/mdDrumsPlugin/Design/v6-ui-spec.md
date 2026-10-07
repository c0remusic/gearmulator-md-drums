# MD Drums UI spec, v6

v5's grid, flat surface, Graphite palette, Inter and numbered outputs, with the fixes of `v5-critique.md` and one
signature. Mockup: `v6-test.html` (`v6-ui.css`, `v6-ui.js`, machine data `v6-machines.js` generated from
`mdLib/mdmachines.cpp`). Serve the folder over HTTP to open it.

## Signature: the track list plays

Each row of the track list carries a 2-module scope of its track's output: the last 1.3 s of its level, newest on
the right, the newest sample in the accent colour, and the track number flashes on a trig. It replaces v5's level
bars, which looked like sliders and did nothing (critique), and it answers the question a drum module raises all
the time: what is playing, and how long does it ring. In the plug-in it reads each track's output, which the engine
renders anyway (`mdDrums::Engine` track outputs); RmlUi draws it with `juceRmlUi::ElemCanvas`.

## Grid

40 px modules, 32 x 19 (1280 x 760), every region on the grid (measured in the mockup: no region of PISTE, MIX,
SORTIES or the machine browser off by more than its 1 px rule).

| Region | Columns | Rows | Content |
|---|---|---|---|
| Top bar | 1-32 | 1 | Name (1-7), status (8-15), tabs (16-21), window size 100/125/150 % (24-25), rate and latency (26-29), main meter (30-32) |
| Track list | 1-7 | 2-19 | Title; 16 rows: number (plays the track), machine (selects it), scope (2), mute, solo; footer |
| Track head | 8-32 | 2-3 | Number, JOUER (2 x 2), ‹ machine › and its family, output, last velocity, kit level knob and value, mute, solo |
| Pages | 8-32 | 4-18 | SYN, EFX, ROUTING: title row, 8 knobs (2 x 2 each), labels, values; screen 9 x 5 on the right |
| Machine browser | 8-32 | 4-18 | Replaces the pages: one column per family (GND 3, TRX 4, EFM 4, E12 4 in two sub-columns, P-I 4, ROM 6 as a 6 x 8 grid of numbers); hover shows the machine's family and SYN names |
| Footer | 8-32 | 19 | Gestures; last note received and its velocity |
| MIX | 1-32 | 2-19 | 16 strips of 2 modules: number (plays), machine, PAN (2 x 2), meter and level fader (11 rows), value, mute and solo, output |
| SORTIES | 1-32 | 2-19 | Table (1-20) and help (21-32), as v5 |

## Controls

One row per parameter of `parameter-spec.md` (track `n`, 1-16); every control carries the parameter's ID as its
element `id` in the mockup (checked: all 33 per track present for tracks 1, 9 and 16 in PISTE, and all 64 of MIX,
no duplicate). Positions are module columns / rows of the 32 x 19 grid, for the selected track in PISTE.

| ID | Control | View: position | Range | Default |
|---|---|---|---|---|
| `t<n>_machine` | Machine name (opens the browser), ‹ › step | PISTE: cols 13-16, row 2 (‹ 12, › 17); browser cols 8-32, rows 4-18 | 0-191 by id | TRX-BD..B2, EFM-BD, -SD, -HH |
| `t<n>_SYN1`..`SYN8` | Knob 48 px in 2 x 2, label, value; named per machine, dimmed when unused | PISTE SYN page: knob k at cols 2k+6..2k+7, rows 5-6; label row 7; value row 8 | 0-127 | 64 |
| `t<n>_AMD` `AMF` `EQF` `EQG` `FLTF` `FLTW` `FLTQ` `SRR` | Same | PISTE EFX page: knobs rows 10-11, labels 12, values 13 | 0-127 | 0 0 64 64 0 127 0 0 |
| `t<n>_DIST` `VOL` `PAN` `DEL` `REV` `LFOS` `LFOD` `LFOM` | Same; DEL and REV dimmed until the master effects exist | PISTE ROUTING page: knobs rows 15-16, labels 17, values 18 | 0-127 | 0 100 64 0 0 64 0 0 |
| `t<n>_PAN` | Knob 48 px, also | MIX: strip n, cols 2n-1..2n, rows 4-5 | 0-127 | 64 |
| `t<n>_level` | Knob 48 px + value (PISTE); fader with meter (MIX) | PISTE: cols 26-27, rows 2-3, value cols 28-30; MIX: strip n, rows 6-16 | 0-127 | 100 |
| `t<n>_mute` | Switch M (20 px in a 1 x 1 cell) | PISTE list: col 6, row n+2 (also in the head, col 31); MIX: strip n, col 2n-1, row 18 | off/on | off |
| `t<n>_solo` | Switch S | PISTE list: col 7, row n+2 (head col 32); MIX: col 2n, row 18 | off/on | off |
| `t<n>_lfoTrack` | Cell in the LFO screen's title, cycles 01-16 | PISTE: cols 26-27, row 14 | 1-16 | n |
| `t<n>_lfoParam` | Cell beside it, cycles the 24 parameters | PISTE: cols 28-29, row 14 | 0-23 | FLTF |
| `t<n>_lfoShape1` | 6 chips TRI SAW SQR RMP EXP RND, after a "1" | PISTE: cols 25-30, row 17 | 6 shapes | TRI |
| `t<n>_lfoShape2` | 6 chips after a "2"; LFOM mixes 1 into 2, its value shown beside | PISTE: cols 25-30, row 18 (LFOM readout cols 31-32) | 6 shapes | TRI |
| `t<n>_lfoMode` | Cell cycling FREE TRIG HOLD | PISTE: cols 31-32, row 17 | 3 modes | FREE |

Not parameters, on the same grid: JOUER (cols 10-11, rows 2-3) and the track numbers play a track; the track list's
scopes (cols 4-5) and the MIX meters show output; SORTIES shows which outputs the host has taken.

## Changes from v5, by critique item

| Critique | v6 |
|---|---|
| Choosing a machine hid in a drop-down | Browser by family over the pages (click the name), ‹ › to step through the table, Échap to close |
| Playing a track was hidden in the Hit screen | JOUER in the head; every track number plays its track (list and MIX) |
| LEVEL and VOL undistinguished | "Niveau kit" in the head and the MIX; VOL stays a machine parameter on ROUTING |
| LFO destination not settable | Two cells in the LFO screen's title: track, then parameter; a second shape row (lfoShape2, mixed by LFOM) and the mode as one cycling cell |
| DEL and REV did nothing | Dimmed and locked on ROUTING ("Effets maîtres à venir"); gone from the MIX |
| Inert level bars | Replaced by the scopes |
| No solo; no fine adjustment | S beside M everywhere; Shift drags four times finer, the wheel steps by 2 (Shift: 1), arrows by 1 (Shift: 10) |
| Hit waveform drew the eye | Waveform in ink-dim at 1 px, envelope in accent at 2 px |
| Weak page titles | 12 px semibold in ink, with what the page holds |
| Four selection treatments | A 3 px accent bar on the selected thing's edge; solid fills only for switches (chip, mute, solo) |
| Accent overloaded | Accent for selection and what is alive (trigs, curves, scopes' newest sample, active chip); taken outputs and the LFO target in ink, the target underlined |
| Values in three styles | One value style, 14 px |
| Four knob sizes | One, 48 px |
| ink-faint at 3.12:1; knob track at 1.85:1 | ink-faint #8a8d93 (4.73:1), knob track #6a6d73 (3.03:1) |
| 10 px text | Nothing under 11 px (measured: no text node below 11 px in any view) |
| No window size | 100 / 125 / 150 % in the top bar |

Keyboard: every control takes focus (Tab), shows a 1 px accent ring, and acts on Enter or Space; knobs and faders
move with the arrows. Trig flashes are skipped under `prefers-reduced-motion`; the scopes still draw, as data.

Unused SYN slots (a machine with fewer than 8 parameters) are dimmed, with the reason in a tooltip.
