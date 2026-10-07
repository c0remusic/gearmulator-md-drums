# MD Drums v22 — UI spec

**Approved by the user on 2026-10-08.** `HANDOFF.md` gives the final placement in window pixels; the y values in this
spec are those of the versions it went through.

v21 (see `v21-ui-spec.md`, and `v20-ui-spec.md` for the grid, regions, head band, rules, screens and LFO target) with
the user's review of v21 on 2026-10-07: the hit's curve looked odd and its waveform no longer changed, the screens had
lost their colour, ASSIGN sat in a frame, the LFO's "0" said nothing, the play key still flashed on every hit, and MIX
gave no way to choose an output. Then the kits (the user's choice: a Kit menu in the plug-in, with .syx import and
export), and the user's notes on the first v22: the LFO's 127 and 0 lines read as rules under its title and ASSIGN,
the rule under the head band stopped at the track list, Size did not line up with "TRX modelled analog", the
window's edges had 8 px where the gutters have 16, and the kit belonged over "Tracks".

## Changes from v21

| Area | v22 |
|---|---|
| Screens | Colour back: the curves the controls set (the hit's envelope, the filter and EQ curve, the LFO's wave) and the options chosen on a screen (the LFO's shape and mode chips, the LFO menu's track and parameter) in the accent, as in v20. The hit's waveform stays dim until heard, ink after. |
| Hit | The waveform follows the SYN parameters the mockup can model (below), so it changes as a knob turns; its window is as long as the hit (0.1, 0.2, 0.3, 0.4, 0.6 or 0.8 s, ticks in ms), so a short hit fills the screen; the playhead crosses it in real time. The note "Waveform at the machine's default SYN" is gone. |
| ASSIGN | A word, like the target beside it ("FLTF on track 01 ▾"): 14 px ink, the screens' hover. Armed, it reads Cancel, in accent, while the title reads "Click a knob to modulate" and every knob is ringed. |
| LFO plot | Zoomed on what the wave covers (the user's choice over the full 0-127): the target knob's value swung by LFOD (127: 64 either way), clipped to 0-127, from the plot's foot to its top, so the wave fills it whatever the knob and the depth. No line (the user: the line bothered); the lowest and highest values reached named at the foot and the top in the rows' label column ("37", "0" for FLTF 18 and LFOD 38), the title naming the parameter. LFOD 0: "LFOD 0: no modulation". The plot spans the chips' columns (x 52 on). The layout stays v22's (the user, over the three of `v22-var-lfo.html`: side by side, the Machinedrum's fields, the mix). |
| Head band | Its rule (y 127) across the whole window, as the top bar's; the bands' rules over the editing frame only (x 240-1280). |
| Window | 1296 × 824, 16 px at its four edges, as wide as a gutter. Across: 16 columns of 64 px and gutters of 16 as before, every box 8 px to the right of v21's. Down: the top bar 56 px tall (was 40), its controls on y 16-40, its rule at y 55; everything under it 16 px lower than the y values in this spec, which stay those of a 40 px bar; the head band 120 px (y 40-160, was 88), its content 28 px from the rules over and under it as the bands' is (the user: the room by the rules the same everywhere); the window ends 16 px under the track list's last row (792), so the three bands are 216 px tall (were 224: 160, 376, 592; their rows 4 px higher: title +28, groups +60, knobs +84, labels +156, values +172). Backgrounds, rules and screens still run to the edges. |
| Screens | 16 px inside on every side: the title on y 16-40, the plot from y 48 to 36 px over the foot, the axis' words on a baseline 16 px over it, the content from 16 px past the screen's left edge to 16 px short of its right one (x 1280: column 16's end, as the play key and the meter). The LFO menu 16 px inside too. The LFO's plot is 56 px tall (48-104), over its three rows of chips. |
| MIX | 28 px under the top bar's rule (the numbers at y 68), down to 16 px from the foot: fader and meter 492 px (y 212-704), the level at 712, M and S at 736, Out at 768-792. |
| Kit | Over "Tracks", on the caption row (y 52-76, as "Track 01"): "Kit 01 Dub techno ▾" opens the kits, Save at the list's right end (over S) keeps the changes in the kit's slot, lit when there are some (Ctrl+S). The track list's old "Kit 01 ▾" at its foot is gone. |
| Top bar | Tabs Track, Mix, Master (columns 4-6). Size on columns 12-14, its text on the head band's inset (x 912), over "TRX modelled analog". |
| Play key | Lights while pressed only (panel fill). A hit no longer flashes it; the hit shows in the hit screen, and the track's number in the list and in MIX. |
| MIX | Each strip ends with an output toggle under M and S, "Out 01" (x +4..60, y 736-760): on, the track plays on its own output and leaves Main; off, it plays on Main. The head band's "Plays on Out 01" says the same, its tooltip pointing to Mix. |

## The hit, as the mockup models it

The recording is the engine's own hit at the machine's SYN defaults (`v22-hits.js`, 384 columns of min/max over
0.8 s). The mockup splits it once into its envelope (an instant attack, a 30 ms release) and its carrier (the waveform
over that envelope), then rebuilds it:

- DEC stretches the envelope's decay: a doubling per 16 steps from the default.
- HOLD holds the envelope's peak: up to 0.4 s at 127 over the default.
- PTCH changes the rate, an octave per 24 steps: a sample's playback (shorter and denser), a synthesis machine's
  oscillation under the same envelope.
- STRT and END cut a sample (E12, ROM), as fractions of its recording.
- The velocity and VOL scale it.
- Where a longer envelope outlasts the recording, the carrier's last period repeats.

Every other SYN parameter (NOIS, HARM, MOD, the filters...) leaves the waveform as recorded. The plug-in will not
model: it re-renders the hit with a second engine on each change (22-27 ms a hit, measured), and shows the live
capture of the track after each hit it plays.

## Master

A third tab, Master (top bar, column 6), for the Machinedrum's four master effects (the user: "still missing the
master effects"). They have their engine to come: mdEngine has none yet; this is the editor they will have.

- **Layout:** two columns of two (the user): RHYTHM ECHO and GATE BOX on the top row (y 40-312), EQ and DYNAMIX
  under them (312-584); each effect on 8 columns (1-8, 9-16), a knob a column, in a cell of 272 px whose content sits
  28 px from the rules over and under it, as in the Track tab's bands (the user: less empty room above and below): its
  name (y + 28, 22 px), what it is and what feeds it ("Delay, tracks send with DEL", + 60), its groups (+ 116), 8 knobs
  (+ 140), their names (+ 212) and values (+ 228). Rules between the rows only, 28 px of room on both sides of each;
  the two effects of a row apart by a gutter (a rule between the columns had 8 px on each side: the user saw uneven
  room by the rules). No screens (the user). No kit row (as in Mix): Ctrl+S saves.
- **Sends** (the user's choice for the room left, y 584-808), 28 px from the rule over them and from the window's
  foot: the title "Sends" and what they feed (y + 28); then a column per track, its number and machine as in MIX, its
  DEL and REV as two 28 px knobs side by side (x +0 and +36 in the column), their names and values: every track's
  sends set from one place, as the ROUTING band sets one track's.
- **The four**, in the order of their SysEx ids ($5D to $60), their parameters named as MCL names them (MDParams.h,
  as md-mm's editor has them):
  - RHYTHM ECHO, delay, fed by the tracks' DEL: TIME MOD MFRQ FB (Delay), FLTF FLTW (Filter), MONO LEV (Output).
  - GATE BOX, reverb, fed by the tracks' REV: DVOL PRED DEC DAMP (Reverb), HP LP (Filter), GATE, LEV.
  - EQ, on Main: LF LG (Low), HF HG (High), PF PG PQ (Peak), GAIN; LG, HG and PG are bipolar knobs (64 in the middle).
  - DYNAMIX, compressor, on Main after the EQ: ATCK REL (Envelope), TRHD RTIO KNEE (Compression), HP (Filter), OUTG
    MIX (Output).
- **Values:** the Machinedrum's own, Kit 1 as OS 1.63 boots (`mdEditorFirmwareTest`, "MD master effects of Kit 1",
  2026-10-07): echo 16 0 32 27 0 44 0 71; reverb 0 0 68 50 1 82 71 109; EQ 64 64 64 64 64 64 64 127; dynamix 127 127
  127 127 127 127 0 0. They are also the knobs' defaults: a knob recedes at them, double-click returns to them.
- **In the kit:** the four effects' 32 values belong to the kit, as on the Machinedrum (a kit dump holds them,
  `MasterEffects` in `mdLib/mdsysexautomation.h`); a kit saved or loaded carries them. Host parameters to add, by
  effect and name ("m_echo_time", ...), appended to the existing pages.
- **Sends:** the ROUTING band's DEL and REV are live. A track on its own output (Out 01) still sends to the echo and the
  reverb (the user), whose returns play on Main; EQ and DYNAMIX act on Main only.

## Kits

The kit over "Tracks" opens the kits over the window under the top bar; the kit's row stays, the kits' title takes
the "Tracks" row (y 92), and a tab closes them. MIX has no track list, so no kit row: Ctrl+S saves there.

- **Slots:** 64, as a Machinedrum project holds them, in four columns of 16 (01-16 on columns 1-3, 17-32 on 4-6, 33-48
  on 7-9, 49-64 on 10-12), rows of 40 px as the track list's. The kit played has the track list's accent bar; the
  chosen slot is lit. Click: choose; double-click or Enter: load; arrows: ↑ ↓ the next slot, ← → the next column.
- **The chosen slot** (columns 13-16, past a hairline in the gutter, x 968): "Slot 06", its name (22 px), its 16 tracks' machines
  in two columns, then Load, Save here, Rename, Delete. A click that would lose something asks for a second: Load
  with changes not saved ("Changes to Dub techno will be lost": Load anyway, Cancel), Save here on another kit's slot
  (Replace it), Delete (Delete it), in the mute colour. A line under the actions says what the last one did.
- **Rename:** the name becomes editable in place, up to 16 characters (the Machinedrum's kit name: 16 bytes at $0A of
  a kit dump); Enter keeps it, Esc leaves it.
- **Import .syx:** a file of Machinedrum kit dumps, one kit or a project's 64; its kits go into the empty slots from
  the chosen one on and keep the names the machine gave them. The mockup imports a made-up "TECHNO 1".
- **Export .syx:** the chosen slot as one Machinedrum kit dump, for the machine or another set.
- **Where kits live:** the bank in the plug-in's data folder, shared by every set; the Live set keeps the kit played,
  its slot and whether it changed. A kit holds what the Machinedrum's does: the 16 machines, their parameters, levels
  and LFOs, and the master effects, kept in the file for when MD Drums plays them. Not in a kit: mutes, solos, the
  tracks' outputs.
- **Code:** reading a dump exists (`md::automation::sysex::parseKitDump`, `mdLib/mdsysexautomation.h:279`: machines,
  parameters, name, master effects, LFOs); writing one is to do.

## Outputs

A track's output is a choice in the plug-in, not in the host: a new host parameter per track, `t<n>_out` (Main or its
own output), saved with the set. On, the track leaves the main mix for its bus; in Live the user adds an audio track
with Audio From "MD Drums", then "Out 01". Today the processor takes a track out of the main mix when the host enables
its bus, so the choice depends on how a host activates VST3 buses; the parameter removes that dependence. The buses
are named "Track 1" to "Track 16" today: to be renamed "Out 01" to "Out 16" so that Live lists the names the editor
shows.

## Checked (2026-10-07, every arrows × play combination, the browser, the LFO menu, ASSIGN armed, after a hit, MIX, the kits and each of their second clicks, a kit edited)

- Boxes: 0 off the 4 px grid; no text overflows its box.
- Text baselines: 0 off a multiple of 4 (the probe and the text in one inline wrapper, so that a flex parent keeps
  them on one line box; v21's probe, inserted as a flex item, measured the item's centre, and missed the family
  line's 67.5, now 68).
- Font sizes: 12, 14, 22, 34 in every view; the "ends" arrows variant adds 20 (‹ ›) and 26 (the name in its picker).
- The hit renders for all 82 machines offered, without an error; a redraw takes about 10 ms in the browser.
- The window's edges, measured on the glyphs and the controls in every view: 16 px on the left (the logo, the kit,
  the track numbers) and on the right (the Main meter, the play key, the kits' Done); the screens' words end 32 px
  from the right edge, 16 inside column 16 as they start 16 inside column 12; at the top, 16 px (the top bar's
  words, centred on its row of controls at y 16-40); at the foot, 16 px under the LFO's chips, the track list and
  MIX's output toggles. Inside the screens, 16 px on every side. (Before: 8 px left, right and top, 32 to 72 at the
  foot.)
