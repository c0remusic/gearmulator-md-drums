# MD Drums editor: handoff

**Approved: v22**, by the user on 2026-10-08 ("valide la v22"), after 22 versions; not an early approval. Open
`v22-test.html` (or `index.html`, the same page) in a browser to see it live; `v22-ui-spec.md` and `v22-style-guide.md`
hold the reasons behind each choice, and `v22-var-head.html` and `v22-var-lfo.html` the variants it was chosen from.
Everything below is framework-neutral; the last section is for the RmlUi editor (`juceRmlUi`) this plug-in will use.

Fidelity: flat. There are no rendered assets: every control is drawn from vector geometry and live values.

## Window and grid

- **1296 x 824 px** at 100 %. A Size control scales the whole window to 125 % and 150 %; nothing reflows.
- **Columns:** 16 columns of 64 px, gutters of 16 px, margins of 16 px. Column `c` (1-16) starts at
  `x = 16 + 80 * (c - 1)`; `n` columns span `64 * n + 16 * (n - 1)` px.
- **Multiples of 4:** every box, padding, gap and text baseline sits on a multiple of 4 px (baselines measured with a
  0 x 0 inline-block probe in the text's own line box). Some text carries a 1-3 px bottom padding to bring Inter's
  baseline onto the step; the boxes stay on the grid.
- **Room:** 16 px at the window's four edges and inside every screen; 28 px on both sides of every rule. Backgrounds,
  rules and screens run to the window's edges.
- **Changed after approval (2026-10-08, the user):** the Track tab's editing area (its surface, the bands' rules and the
  shown row's background) starts at x 240, column 3's end, not at the gutter's middle (248) as the mockup draws it:
  16 px between it and knob 1, as between knob 8 and the screens. `v22-boxes.txt` carries the change.
- **Vertical structure (Track tab):** top bar y 0-56 (rule at y 55); head band y 56-176 (rule at y 175, across the
  window); three bands of 216 px at y 176, 392 and 608 (rules at y 391 and 607, from x 240 to the right edge); the
  last band ends at the window's foot (824).

All coordinates below are window pixels at 100 %, `x, y, width, height`.

## Top bar (every tab)

| Element | Box | Notes |
|---|---|---|
| Logo "MD Drums" | 16, 0, 224, 56 | "MD" 14 px bold, tracked 2 px; "Drums" ink-dim regular |
| Tab Track / Mix / Master | 256 / 336 / 416, 0, 64, 56 | 14 px medium; the open tab's mark: 2 px accent under its text only, over the bar's rule |
| Size | 896, 16, 224, 24 | text from x 912 (16 px inset), "Size" label + "100%"; click steps 100, 125, 150 % |
| Main (label) | 1216, 16, 64, 16 | |
| Main meter | 1216, 32, 64, 8 | two 2 px bars, 4 px apart, accent fill |
| Rule | 0, 55, 1296, 1 | |

## Track tab

### Head band (y 56-176)

| Element | Box | Notes |
|---|---|---|
| Kit | 16, 84, 168, 24 | "Kit" label, slot "01", name, "▾"; opens the kits |
| Save | 192, 84, 40, 24 | right-aligned; ink semibold when the kit has changed, ink-faint otherwise; Ctrl+S |
| "Tracks" label | 16, 124, 224, 24 | |
| Caption "Track 01" | 256, 84, 64, 24 | 12 px label |
| Previous / next track | 336 / 360, 84, 24, 24 | ‹ › step through the tracks (not the machines) |
| Machine name | 256, 108, 304, 40 | 34 px; chevron after it; click opens the machine browser, wheel or ↑ ↓ steps machines |
| Link line | 576, 84, 304, 24 | ticket 29: "Also strikes track 05 ▾" or "Strikes no other track ▾", 14 px ink-dim, the track in ink; click opens the Links and Chokes menu |
| Choke line | 576, 124, 304, 24 | "Silences track 10 ▾" or "Silences no track ▾"; click opens the same menu |
| Family line | 896, 84, 224, 24 | text from x 912: "TRX" (12 px, tracked) + "modelled analog" (14 px ink-dim) |
| Output line | 896, 124, 224, 24 | "Plays on Out 01" or "Plays on Main" |
| Velocity label | 1136, 84, 64, 24 | |
| Velocity value | 1136, 112, 64, 32 | 22 px; drag up or down, 1-127 |
| Velocity bar | 1136, 144, 64, 4 | ink-dim fill proportional to the velocity |
| Play key | 1216, 84, 64, 64 | `--track` border, ▶ glyph 18 x 20 ink; lights (panel fill) while pressed only, never on a hit |

### Track list (columns 1-3, y 176-808)

Rows of 40 px from y 176 (row `i` at `176 + 40 * i`); in each row, 24 px tall cells 8 px down:

| Element | Box (row 1) | Notes |
|---|---|---|
| Shown row's background | 0, 176, 240, 40 | panel colour, to the editing area |
| Shown row's mark | 0, 184, 4, 24 | accent |
| Number | 16, 184, 24, 24 | 12 px; click plays the track and shows it; accent for 140 ms on a hit |
| Machine | 48, 184, 84, 24 | 14 px tracked 1 px; click shows the track |
| Scope | 136, 184, 32, 24 | the track's output level over time |
| M / S | 176 / 208, 184, 24, 24 | toggles |

### Bands (SYN, EFX, ROUTING)

Each band is 216 px, its content 28 px from the rules: title (y + 28), groups (+ 60), knobs (+ 84), names (+ 156),
values (+ 172). Knob `k` (1-8) on column `3 + k`.

| Element | Box (SYN) | Notes |
|---|---|---|
| Title | 256, 204, 224, 24 | 22 px semibold tracked 1.5 px |
| Group row | 256, 236, 624, 16 | 12 px semibold name, then a 1 px rule to the group's end; EFX: AM, EQ, Filter; ROUTING: Channel, Sends, LFO |
| Knob 1 | 256, 260, 64, 64 | then every 80 px |
| Name | 256, 332, 64, 16 | 12 px tracked 1 px; underlined in accent when an LFO modulates it |
| Value | 256, 348, 64, 16 | 14 px tabular; ink-faint at the default, ink when edited |
| EFX knob 1 / ROUTING knob 1 | 256, 476 / 692, 64, 64 | |

A SYN slot the machine does not use shows "—" at 35 % opacity, without a value.

### Screens (columns 12-16, x 896 to the right edge, 400 x 216 each)

Hit at y 176, Filter and EQ at y 392, LFO at y 608. Inside each, 16 px on every side: the title row 912, top + 16,
368, 24; the plot from top + 48 to 36 px over the foot; axis words on a baseline 16 px over the foot.

- **Hit:** the last hit's waveform (min/max columns), `--track` until heard, ink behind a 1 px accent playhead that
  crosses the window in real time; over it, the amplitude envelope (2 px accent). The window fits the hit: 0.1, 0.2,
  0.3, 0.4, 0.6 or 0.8 s, ticks in ms. Title "Hit", note "Last hit, velocity 110", "Preview, velocity 110" (the
  sound changed since: its hit rendered, ticket 28) or "Not played yet".
- **Filter and EQ:** response from 20 Hz to 20 kHz (ticks 100, 1k, 10k; "0 dB" line), 2 px accent; the EQ point as
  a 6 x 6 ink square. Note "SRR n" when SRR is above 0.
- **LFO:** title row "LFO", the target "FLTF on track 01 ▾" (opens the target menu), "Assign" at the right (a word, no
  frame; armed it reads "Cancel" in accent, the title reads "Click a knob to modulate" and every knob gets a 1 px
  accent ring). The plot (x 964 to 1280, y 656-712) is zoomed on what the wave covers (the knob's value swung by
  LFOD, clipped to 0-127), no line; the highest and lowest values reached in 12 px ink-faint at the plot's top and foot
  in the label column (x 912). LFOD 0: "LFOD 0: no modulation". Under it three rows of chips, 24 px tall, 8 px
  apart (rows at y 720, 752, 784): "Shape 1" / "Shape 2" with TRI SAW SQR RMP EXP RND, "Mode" with FREE TRIG HOLD;
  label column 48 px, chips 44 px wide, 4 px apart; the chosen chip accent-filled.
- **LFO target menu** (over the whole LFO screen, display colour, 16 px inside): head "LFO 01 modulates", the
  track's machine, "Done"; then a grid of 80 px label column + 8 cells of 36 px: "Track" over two rows of 8 track
  numbers (y 656, 680), then SYN, EFX, ROUTING rows of parameter names (from y 712). Chosen cells accent-filled.
- **Links and Chokes menu** (ticket 29; over the whole Hit screen, the LFO target menu's grid): head "Track 01"
  "strikes and silences", "Done"; "Strikes" and "Silences" each a label at rows y 224 and 280 with "Off" under it
  (x 912, 36 px) and 16 track numbers in two rows of 8 (cells of 36 from x 992). The target accent-filled, the Track
  itself dimmed and inert. A choice keeps the menu open; Done and Esc close it. Values of the Kit, not host parameters.

### Machine browser (over the bands, Track tab)

| Element | Box | Notes |
|---|---|---|
| Title | 256, 208, 624, 24 | "Choose a machine" 22 px + "for track 01" + a hint of the hovered machine |
| Cancel / Keep | 896, 208, 384, 24 | right-aligned; Keep semibold |
| Family header | 256, 248, 64 * n, 16 | GND col 4, TRX 5-6, EFM 7, E12 8-9, P-I 10, ROM 12-16 |
| Family kind | 256, 264, 64 * n, 16 | "generators", "analog"... |
| Machine cells | 256, 288, 64, 24 | then every 32 px down, column by column (ROM row by row) |
| Preview group / name / listen | 896, 528 / 552 / 584, 384, 16 / 24 / 16 | "Listen while choosing" checkbox |
| Preview screen | 896, 608, 400, 216 | the hit of the machine being tried |

## Mix tab

One strip per column (strip `t` on column `t`), every 80 px; a 1 px `--line` hairline in each gutter's middle from
y 56 to the foot; the shown track's strip in the chassis colour (to the gutters' middles, to the window's edge for
strips 1 and 16), with a 4 px accent bar at y 56.

| Element | Box (strip 1) | Notes |
|---|---|---|
| Number | 16, 84, 64, 16 | click plays and shows the track |
| Machine | 16, 100, 64, 24 | |
| PAN | 16, 132, 64, 64 | bipolar knob |
| "Pan" | 16, 196, 64, 16 | |
| Meter | 16, 228, 4, 492 | accent fill from the bottom |
| Level fader | 16, 228, 64, 492 | rail 2 px `--track` at x + 39; cap 48 x 8 ink at x + 16 |
| Level value | 16, 728, 64, 16 | |
| M / S | 20 / 52, 752, 24, 24 | |
| Out 01 | 20, 784, 56, 24 | toggle: on, the track plays on its own output and leaves Main |

## Master tab

Two rows of two effects, each on 8 columns (1-8 and 9-16), a knob a column; rules at y 327 and 599 across the
window, 28 px of room on both sides; no rule between the columns.

| Element | Box (RHYTHM ECHO) | Notes |
|---|---|---|
| Name | 16, 84, 624, 24 | 22 px semibold tracked |
| What it is / what feeds it | 16, 116, 624, 24 | "Delay, tracks send with DEL" 14 px ink-dim |
| Group row | 16, 172, 304, 16 | |
| Knob 1 | 16, 196, 64, 64 | then every 80 px |
| Name / value | 16, 268 / 284, 64, 16 | |

GATE BOX starts at x 656 on the same row; EQ and DYNAMIX on the second row, 272 px lower (knobs at y 468).

| Effect | Parameters (groups) | Values and defaults (Kit 1 as OS 1.63 boots) |
|---|---|---|
| RHYTHM ECHO, fed by DEL | TIME MOD MFRQ FB (Delay), FLTF FLTW (Filter), MONO LEV (Output) | 16 0 32 27 0 44 0 71 |
| GATE BOX, fed by REV | DVOL PRED DEC DAMP (Reverb), HP LP (Filter), GATE, LEV | 0 0 68 50 1 82 71 109 |
| EQ, on Main | LF LG (Low), HF HG (High), PF PG PQ (Peak), GAIN; LG HG PG bipolar | 64 64 64 64 64 64 64 127 |
| DYNAMIX, on Main after the EQ | ATCK REL (Envelope), TRHD RTIO KNEE (Compression), HP (Filter), OUTG MIX (Output) | 127 127 127 127 127 127 0 0 |

**Sends** (y 600-824): title "Sends" 16, 628, 224, 24 and "DEL feeds RHYTHM ECHO, REV feeds GATE BOX; a track on its
own output sends too" 256, 628, 1024, 24; per track (column `t`): number 16, 676, 64, 16; machine 16, 692, 64, 24;
DEL and REV knobs of 28 px at x + 0 and x + 36, y 728; their names and values at y 764 and 780, 28 px wide.

## Kits (over every view under the top bar; a tab closes them)

| Element | Box | Notes |
|---|---|---|
| Kit row | as in the head band | stays visible; clicking it again closes the kits |
| Title | 16, 124, 624, 24 | "Kits" 22 px + "64 slots, kept by the plug-in" |
| Import .syx / Export .syx / Done | 896, 124, 384, 24 | right-aligned, 24 px apart; Done semibold |
| Rule | 0, 175, 1296, 1 | |
| Slot 01 | 16, 184, 224, 24 | number (24 px, 12 px) + name (14 px) or "Empty"; slots 01-16 down column 1-3, 17-32 on 4-6 (x 256), 33-48 on 7-9, 49-64 on 10-12; rows of 40 px |
| Chosen slot's background / played slot's mark | as the track list's row and mark | |
| Hairline | 968, 176, 1, 648 | `--line` |
| "Slot 06, playing" | 976, 184, 304, 24 | |
| Name | 976, 208, 304, 32 | 22 px; editable in place on Rename, 16 characters at most |
| "Tracks" group | 976, 256, 304, 16 | |
| Machines | 976, 280, 144, 24 | 01-08 then 09-16 at x 1136, rows of 24 px |
| Actions | 976, 496, 304, 24 | Load (semibold), Save here, Rename, Delete, 24 px apart; a destructive one asks for a second click: "Load anyway", "Replace it", "Delete it" in the mute colour, with Cancel |
| Note | 976, 528, 304, 16 | what the last action did, or what the second click will do |

## Colour

| Token | Hex | Role |
|---|---|---|
| `--chrome` | #1b1c1e | window, track list, kits |
| `--surface` | #26272a | editing area, the shown row, hover fill |
| `--display` | #151618 | screens, the LFO menu |
| `--line` | #3a3c40 | hairlines between strips, velocity bar's track |
| `--rule` | #55585e | rules, group rules, the screens' left edge |
| `--tick` | #3c3e43 | graduations in the screens |
| `--ink` | #f2f2f2 | content, chosen options on the panel, edited values |
| `--ink-dim` | #a7aaaf | secondary text, labels |
| `--ink-faint` | #8e9197 | values at their default, hints, axis words |
| `--track` | #6e7177 | every control's border (3.5:1 on the chassis), knob tracks, a waveform not yet heard |
| `--accent` | #6fd1c4 | live state (the shown track, the open tab, the playhead, a modulation, ASSIGN armed, meters) and, on the screens, the curves the controls set and the options chosen there |
| `--on-accent` | #10201e | text on accent |
| `--mute` | #ff7a5c | mute on, destructive confirmations |

Contrast: text 4.7:1 or more everywhere; borders 3.5:1 on the chassis, 3.05:1 on the panel.

## Type

Inter (OFL), regular 400, medium 500, semibold 600, bold 700; bundle the font files with the plug-in.

| Size | Line | Use |
|---|---|---|
| 12 | 16 | labels, captions, chips, track numbers, axis words, hints |
| 14 | 16-24 | values, machine names in lists, body text, the screens' title controls |
| 22 | 24-32 | band titles, the browser's and the kits' titles, the play velocity, the chosen kit's name |
| 34 | 40 | the shown machine's name |

The Machinedrum's own names (SYN, EFX, ROUTING, PTCH, TRX, FREE, RHYTHM ECHO...) in capitals with 1 px tracking
(band titles 1.5 px); every other word in sentence case. No middle-dot strings, no arrows in labels. Values use
tabular figures.

## Controls

- **Knob, 64 px:** a 270° track, 2 px `--track`, radius 29; at its default no value arc and a 2 px ink-dim pointer
  (from 35 % to the radius minus 5 px); edited, a 3 px ink arc from the minimum to the value (bipolar parameters: from
  the top, 64) and an ink pointer. Bipolar: EQG, PAN, a sample machine's PTCH (E12, ROM), the Master EQ's LG, HG,
  PG.
- **Knob, 28 px:** the same drawing at 28 px (Master's sends).
- **Toggle (M, S, Out):** 1 px `--track` border, 12 px semibold; hover border ink-dim; Mute on: mute fill, text #111;
  Solo and Out on: ink fill, text #111.
- **Chip:** 24 px tall, 12 px medium tracked; hover panel fill; chosen: accent fill, `--on-accent` text.
- **Word controls on a screen's title:** 14 px ink, 4 px side padding, panel fill on hover; no frame.
- **Focus:** a 1 px accent inset ring on the focused control (keyboard only).

## Interaction

| Gesture | Effect |
|---|---|
| Drag a knob or fader up or down | 1.5 px per step (a fader: its height over 127); Shift: 4 times finer |
| Wheel on a knob | 2 steps, 1 with Shift |
| ↑ ↓ on a focused knob | 1 step, 10 with Shift |
| Double-click a knob | back to its default (the machine's for SYN) |
| Wheel or ↑ ↓ on the machine's name | the next machine |
| Space, nothing focused | plays the shown track |
| Esc | closes the open overlay (the LFO menu, ASSIGN, the browser: Cancel, the kits, a second-click question) |
| Ctrl+S | saves the kit in its slot when it has changed |
| Kits: click / double-click or Enter / arrows | choose / load / ↑ ↓ next slot, ← → next column |

Any change to a machine, a parameter, a level or an LFO setting marks the kit changed (Save lit). Mutes, solos and
outputs do not.

## Parameters

The existing host parameters stay as `parameter-spec.md` lists them (their IDs are in saved Live sets). The editor
adds, appended after them:

| ID | Type | Where |
|---|---|---|
| `t<n>_out` | bool | Mix, Out 01-16: on, the track leaves Main for its own output |
| `t<n>_solo` | bool | list and Mix, S |
| `t<n>_lfoTrack`, `t<n>_lfoParam` | int, track 1-16; parameter 0-23 | LFO target menu and ASSIGN |
| `t<n>_lfoShape1`, `t<n>_lfoShape2` | choice TRI SAW SQR RMP EXP RND | LFO chips |
| `t<n>_lfoMode` | choice FREE TRIG HOLD | LFO chips |
| `m_echo_TIME` ... `m_dyn_MIX` | int 0-127, 32 in all | Master knobs (`m_<echo/reverb/eq/dyn>_<NAME>`) |

The mockup gives each control the ID of its parameter (`t1_SYN1`, `t1_out`, `m_echo_TIME`...).

## Asset manifest

None rendered. Vector only: the knob geometry above, the play glyph (path `M2 1 L13 8 L2 15 Z` in a 14 x 16 box,
drawn 18 x 20), the chevron (an 8 x 8 L of 2 px rotated 45°). Font: Inter, four weights. `v6-machines.js` and
`vN-hits.js` are the mockup's data only: the plug-in takes machines from `mdLib/mdmachines.*` and draws hits from
the engine.

## Implementation notes for the RmlUi editor

- **Fluidity is a requirement** (every click and drag instant): read the machine through lock-free snapshots, never
  `Plugin::withDeviceLocked` per frame or per click; measure input to pixel.
- **Threads:** RmlUi DOM changes on the JUCE message thread; from audio or MIDI callbacks post with
  `juce::MessageManager::callAsync`, guarded by the static instance-set pattern.
- **Drawing:** the software renderer ignores transforms, so whatever turns or moves (knob arcs and pointers, meters,
  scopes, the screens' plots and playhead) is drawn on `juceRmlUi::ElemCanvas`, as `mdOutputMetersView.cpp` and
  `mdCurveView.cpp` do. Hide elements with `display`, never a zero scale.
- **Hit screen:** two sources. After each hit the engine plays, a lock-free capture of the track's own output (after
  its effects and VOL) gives the real waveform (ticket 21); when the track's sound changes, a second engine without the
  master effects renders its hit on a thread of its own (`mdDrums::HitPreview`, ticket 28): a fresh engine each time,
  so that the same settings draw the same hit, the one a Device just loaded with that Kit plays first; the latest
  request only; about 84 MB while an editor uses it, freed after 30 s without a request. The note says "Preview,
  velocity 110" until the track sounds again. The browser's preview screen shows the same, for the machine chosen (not
  the one hovered). The mockup's model (DEC, HOLD, PTCH, STRT, END reshaping a recorded hit) is not for the plug-in.
- **Filter and EQ, LFO screens:** the mockup's curves are models; draw the engine's responses where it can give them,
  the MD's LFO shapes from its own tables. Done in ticket 22 of the editor map: the response comes from the mixer DSP's
  own coefficient words (`mdDrums/mdDrumsFilterResponse.*`), 0 dB being an untouched Track at 1 kHz, so the open
  filter shows its own fall (-2.2 dB at 10 kHz, -4.7 dB at 20 kHz); the EQ's point sits on the curve at the angle of
  its poles (a boost) or its zeros (a cut). The LFO is the OS's own routines ported (`mdDrums/mdDrumsLfo.*`), which
  correct the mockup: LFOD 127 swings about 126 steps each way, the saw falls twice a period, RMP and EXP fall once
  from each Hit, RND takes 8 steps a period at half the swing, LFOM fades shape 1 into shape 2 inverted, and the value
  moves once a tick (125 Hz). The plot starts on a Hit of the Track and runs free after it, HOLD included.
- **Outputs:** add `t<n>_out`; the track leaves the main mix when it is on, whatever the host does with the bus; name
  the buses "Out 01" to "Out 16" (they are "Track 1" to "Track 16" today) so that Live lists what the editor shows.
- **Master effects:** mdEngine has none yet. Echo and reverb take every track's DEL and REV sends, tracks on their own
  output included, and return on Main; EQ and DYNAMIX act on Main only. Defaults: the values in the table above.
- **Kits:** a bank of 64 slots in the plug-in's data folder, shared by every set; the plug-in state keeps the kit
  played, its slot, its name and whether it changed. Import reads Machinedrum kit dumps with
  `md::automation::sysex::parseMdKit` (one kit or a project's 64, into the empty slots from the chosen one on);
  Export writes the chosen slot with `mdKitDump`. A kit holds machines, parameters, levels, LFOs and master
  effects; not mutes, solos or outputs. As built (ticket 26 of the editor map): `mdDrums/mdDrumsBank.*` (the file,
  `Bank.syx`, 64 dumps of 1233 bytes, a Slot written in place), `mdDrumsKitsView.*`; Save is lit while the kit played
  differs from its slot, whatever changed it (ticket 10).
- **Not retained:** the head band's other arrow and play layouts (`v22-var-head.html`) and the three other LFO screens
  (`v22-var-lfo.html`) stay in the mockup for reference only.
