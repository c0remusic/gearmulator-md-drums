# MD Drums: parameter spec

Since the port to the TUS stack (ticket 07, 2026-10-08) the parameters are `parameterDescriptions_mddrums.json`, edited
by hand since ticket 18 (its generator went with the minimal skin). Their JUCE IDs are pluginLib's `page_part_index`
(`Parameter::genId`), frozen from the port's first push: append parameters, never renumber. The earlier `t<n>_<name>`
IDs are gone (no Live set needed keeping them, ADR 0001).

608 host parameters, in this order: per Track (part 0-15, named "Track n"), 34; then the 32 master effects (part 0,
`NonPartSensitive`, named without a prefix since ticket 27: "Echo TIME", "GBox DVOL", "EQ LG", "Dyn ATCK"); then the 32
relays for Push 2 (ticket 27, `docs/md-drums-push2.md`).

| Page | Index | Name (`param=` in a skin) | Range | Message to the Device (ADR 0002) |
|---|---|---|---|---|
| 5 | 0 | Machine | 0-191, by name | ASSIGN MACHINE `$5B` (an id the Machinedrum lacks sends nothing) |
| 0 | 0-7 | SYN1 … SYN8 | 0-127, named per machine ("PTCH 64") | CC, channels 1-4 |
| 1 | 0-7 | AMD AMF EQF EQG FLTF FLTW FLTQ SRR | 0-127 | CC |
| 2 | 0-7 | DIST VOL PAN DEL REV LFOS LFOD LFOM | 0-127 | CC |
| 6 | 0-4 | LfoTrack, LfoParam, LfoShape1, LfoShape2, LfoMode | 1-16, 24 names, TRI SAW SQR RMP EXP RND, FREE TRIG HOLD | SET LFO PARAM `$62` |
| 3 | 0 | Level | 0-127 | CC 8-11 |
| 4 | 0 | Mute | Off, On | CC 12-15 |
| 7 | 0 | Solo | Off, On | MD Drums' own `F0 7D 4D 44 44 01` |
| 7 | 1 | Out | Main, Out | MD Drums' own `F0 7D 4D 44 44 02`: the Track leaves Main for "Out nn", whatever the host does with the bus |
| 8 | 0-7 | EchoTIME MOD MFRQ FB FLTF FLTW MONO LEV | 0-127 | `$5D` |
| 8 | 8-15 | ReverbDVOL PRED DEC DAMP HP LP GATE LEV | 0-127 | `$5E` |
| 8 | 16-23 | EqLF LG HF HG PF PG PQ GAIN | 0-127 | `$5F` |
| 8 | 24-31 | DynamixATCK REL TRHD RTIO KNEE HP OUTG MIX | 0-127 | `$60` |
| 9 | 0 | FocusTrack | 1-16 | none: shows that Track in the editor, which the other relays aim at |
| 9 | 1-7 | FocusMachine Level Mute Solo Out LfoShape1 LfoShape2 | as their Track parameters | their Track parameter's, for the Track shown |
| 9 | 8-31 | FocusSYN1-8, FocusAMD … SRR, FocusDIST … LFOM | as their Track parameters | the same |

The master effects are kept in the Kit and play since ticket 24. Links and Chokes are Kit values sent as
`$65` and `$66`, not host parameters.

Outputs: "Main" stereo, then "Out 01" to "Out 16" mono, off until the host enables them. Notes 36-51 on any channel
trigger Tracks 1-16 with velocity; the host's other MIDI does nothing.
