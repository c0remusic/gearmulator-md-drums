# MD Drums UI spec, v4

Chosen on 2026-10-06 from `v1-var-options.html`: layout A (track list + pages, after Overbridge), look Graphite.
v2 added frames that draw the values (envelope, LFO and the like), as the user asked; v3 added the MIX and
SORTIES tabs; v4 numbers the outputs and puts the whole UI in one font, Inter (picked from `v4-var-font.html`).
Framework: RmlUi (`juceRmlUi`), as Gearmulator's skins. Mockup: `v4-test.html`, drawn by `v4-ui.css` and
`v4-ui.js` (serve the folder over HTTP, or open the file in a browser that loads local scripts).

## Outputs (v4)

Track `n` has output `n`: the UI shows only the number, `01` to `16`, and the plug-in names its buses after it,
**`Out 01`** to **`Out 16`** (mono); the stereo main bus is **`Main`**. Code change: `Processor::buses()`
names them so today "Output" and "Track 1".. "Track 16".

## Window

1280 x 760, resizable later in steps (the skin scales). Sections:

| Section | Position (x, y, w, h) | Content |
|---|---|---|
| Top bar | 0, 0, 1280, 48 | Name, status screen (kit, track, machine), tabs PISTE / MIX / SORTIES, rate and latency, output meter |
| Track list | 0, 48, 250, 712 | 16 rows of 38 px: number, machine, level bar, mute |
| Track head | 268, 60, 994, 54 | Number, machine selector, family, LEVEL knob, mute |
| Page SYN | 268, 123, 994, 198 | 8 knobs (4 x 2) + Hit screen (330 x 180) |
| Page EFX | 268, 330, 994, 198 | 8 knobs + Filter / EQ / AM screen |
| Page ROUTING | 268, 537, 994, 198 | 8 knobs + LFO screen with shape and mode chips |

The top bar's tabs switch the area below it: PISTE (above), MIX and SORTIES (below).

### MIX

| Section | Position (x, y, w, h) | Content |
|---|---|---|
| 16 strips | 12, 58, 16 x ~70 + gaps, 692 | Number, machine, PAN knob 34 px, DEL and REV knobs 28 px (dimmed until the master effects exist), meter + LEVEL fader filling the strip, value, mute, destination (`MAIN`, or `→ 01`..`→ 16` when the host has taken the track's output) |
| Main strip | right, 92 wide | Stereo meter of the main output, peak in dB, "sans effets maîtres" |

Clicking a strip selects its track (PISTE then opens on it). The fader is `t<n>_level`; PAN, DEL and REV are
`t<n>_PAN`, `t<n>_DEL`, `t<n>_REV`, the same parameters as on the ROUTING page.

### SORTIES

| Section | Position | Content |
|---|---|---|
| Table | left, 1fr | A `MAIN` row (stereo, "les pistes libres"), then 16 rows: track, machine, its output number `01`..`16`, state pill ("prise par l'hôte, hors du mix" / "libre, dans MAIN"), level meter |
| Help | right, 340 px | How separate outputs work, how to take one in Live, what the main output carries |

Read-only: the host enables a track's output (a JUCE plug-in cannot), and the table shows which it has.

## Controls

One row per parameter of `parameter-spec.md`, for the selected track `n`:

| Parameter | Control | Where | Range | Default |
|---|---|---|---|---|
| `t<n>_machine` | Selector (list of the OS's ~135 machines, by family) | Track head | 0-191 | per track |
| `t<n>_SYN1`..`SYN8` | Knob 46 px, label = machine's name for it | SYN page | 0-127 | 64 |
| `t<n>_AMD` `AMF` `EQF` `EQG` `FLTF` `FLTW` `FLTQ` `SRR` | Knob 46 px | EFX page | 0-127 | 0 0 64 64 0 127 0 0 |
| `t<n>_DIST` `VOL` `PAN` `DEL` `REV` `LFOS` `LFOD` `LFOM` | Knob 46 px | ROUTING page | 0-127 | 0 100 64 0 0 64 0 0 |
| `t<n>_level` | Knob 40 px + bar in the track list | Track head, list | 0-127 | 100 |
| `t<n>_mute` | Toggle 18 px | Track head, list | | off |
| `t<n>_lfoShape1` `lfoMode` | Chips | LFO screen | | TRI, FREE |
| `t<n>_lfoTrack` `lfoParam` | Click on the destination in the LFO screen's title | LFO screen | | own track, FLTF |
| `t<n>_lfoShape2` | Second chip row (v3) | LFO screen | | TRI |

The knob an LFO modulates shows its label in the selection colour.

## Screens

| Screen | Draws | From |
|---|---|---|
| Hit (SYN) | Waveform of the track's last note, its envelope in colour | The engine's track output, captured on each trigger (real, not computed) |
| Filter / EQ / AM (EFX) | Response over 20 Hz - 20 kHz: band FLTF..FLTF+FLTW, resonance FLTQ at both edges, EQ peak at EQF with gain EQG; SRR noted | Parameters (the curve is an illustration; the MD's own filter maths can replace it later) |
| LFO (ROUTING) | Two periods of the shape at LFOS, scaled by LFOD; destination and mode in the title | Parameters |

Clicking the Hit screen plays the track (as a note 36 + n - 1 at velocity 100).

## Palette (Graphite)

| Token | Hex | Use |
|---|---|---|
| bg | #2b2c2e | Window |
| head | #232426 | Top bar, track list |
| panel | #26272a | Pages |
| cell | #34363a | Rows, knob bodies, chips |
| cell-q | #3d4045 | Selected / hovered row |
| hair | #3a3c40 | Borders |
| ink | #f2f2f2 | Values, names |
| ink-dim | #a9abb0 | Labels |
| ink-faint | #74777d | Disabled, hints |
| sel | #6fd1c4 | Selection, curves, active chips |
| arc | #ffffff | Knob value arc |
| track | #4a4d52 | Knob track, bars |
| screen | #1a1b1d | Screens |
| grid | #2a2c2f | Screen grid |
| warn | #ff7a5c | Mute on, warnings |

## Style notes

- Overbridge's grey and white, one selection colour, no gradients: the screens carry the colour.
- The Machinedrum's three pages of 8 encoders kept as rows, so the hardware's muscle memory holds.
- One font family for everything (v4, at the user's request), its tabular figures frozen into the default
  digits of the bundled font file: RmlUi cannot ask for OpenType features, and proportional digits make a
  changing value shift its neighbours. Measured at 12 px (`v4-var-font.html`): default digit spread Barlow
  Semi Condensed 2.42 px, Inter 2.88 px, JetBrains Mono 0; with `tnum`, 0 for all three.
