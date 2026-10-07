# MD Drums: parameter spec

Read from `mdDrumsProcessor.cpp` (`createLayout`, 2026-10-06). IDs are part of saved Live sets: keep them.

Per track `n` = 1..16 (27 parameters, 432 in all):

| ID | Name | Type | Range | Default | Notes |
|---|---|---|---|---|---|
| `t<n>_machine` | Machine | int | 0-191 | TRX-BD..TRX-S2, EFM-BD, EFM-SD | Machine id in the OS's table (~135 used); shown by name |
| `t<n>_SYN1`..`t<n>_SYN8` | SYN1-8 | int | 0-127 | 64 | Named per machine ("PTCH", "DEC"...) |
| `t<n>_AMD` | AMD | int | 0-127 | 0 | Amplitude modulation depth |
| `t<n>_AMF` | AMF | int | 0-127 | 0 | Amplitude modulation frequency |
| `t<n>_EQF` | EQF | int | 0-127 | 64 | EQ frequency |
| `t<n>_EQG` | EQG | int | 0-127 | 64 | EQ gain |
| `t<n>_FLTF` | FLTF | int | 0-127 | 0 | Filter base |
| `t<n>_FLTW` | FLTW | int | 0-127 | 127 | Filter width |
| `t<n>_FLTQ` | FLTQ | int | 0-127 | 0 | Filter resonance |
| `t<n>_SRR` | SRR | int | 0-127 | 0 | Sample rate reduction |
| `t<n>_DIST` | DIST | int | 0-127 | 0 | Distortion |
| `t<n>_VOL` | VOL | int | 0-127 | 100 | Volume |
| `t<n>_PAN` | PAN | int | 0-127 | 64 | Pan |
| `t<n>_DEL` | DEL | int | 0-127 | 0 | Delay send (no master effects yet) |
| `t<n>_REV` | REV | int | 0-127 | 0 | Reverb send (no master effects yet) |
| `t<n>_LFOS` | LFOS | int | 0-127 | 64 | LFO speed |
| `t<n>_LFOD` | LFOD | int | 0-127 | 0 | LFO depth |
| `t<n>_LFOM` | LFOM | int | 0-127 | 0 | LFO mix |
| `t<n>_level` | Level | int | 0-127 | 100 | Kit level |
| `t<n>_mute` | Mute | bool | | off | |

Proposed in v2 (not in the code yet; `HostModel::setLfo` takes them, and new IDs append to the list):

| ID | Name | Type | Range | Default | Notes |
|---|---|---|---|---|---|
| `t<n>_lfoTrack` | LFO dest track | int | 1-16 | n | The MD's LFO can modulate another track |
| `t<n>_lfoParam` | LFO dest param | int | 0-23 | 12 (FLTF) | One of the 24 track parameters |
| `t<n>_lfoShape1` | LFO shape 1 | choice | TRI SAW SQR RMP EXP RND | TRI | |
| `t<n>_lfoShape2` | LFO shape 2 | choice | same | TRI | LFOM mixes shape 1 into shape 2 |
| `t<n>_lfoMode` | LFO mode | choice | FREE TRIG HOLD | FREE | TRIG restarts on the track's note; HOLD samples at it |
| `t<n>_solo` | Solo | bool | | off | v6: any soloed track mutes the tracks that are not (the engine's mutes) |

The Machinedrum's own pages: SYN = SYN1-8, EFX = AMD..SRR, ROUTING = DIST..LFOM (8 encoders each).

Outputs: main stereo, plus "Track 1".."Track 16" mono, enabled by the host (not parameters).
Notes 36-51 trigger tracks 1-16 (not parameters; the UI may flash a track on trigger).
