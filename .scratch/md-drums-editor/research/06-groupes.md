# Trig groups and mute groups: what the Machinedrum does, and what mdEngine needs

Research for the decision that MD Drums' Device plays trig groups and mute groups from the first port, exactly as the
Machinedrum does. Repository paths are relative to the root; `file:line` points at the code on 2026-10-08 (`main` at
ef8a55a2). No source file was changed.

Sources and method:

- **Manual:** Elektron, *Machinedrum User's Manual* for OS 1.63,
  https://www.elektron.se/wp-content/uploads/2024/09/machinedrum_manual_OS1.63_1.pdf (linked from
  https://www.elektron.se/products/machinedrum, read 2026-10-08; the user's copy in `Downloads` is the same file name,
  126 pages). Page numbers below are the printed ones, with the PDF page in brackets.
- **OS code:** the user's flash image `elektron_sps1-1uw_os1.63.bin` (SHA-1 a872a2f3527063673d6ea6d3080c4c62ef0cadc1,
  checked), container sections depacked by the Python port of `md::fw::aplibDepack` / `parseContainer` that
  `research/04-kits.md` used (`source/elektron/md/mdEngine/tools/mdfw/Firmware.cpp:56-128`), disassembled with Capstone
  5.0.7 in M68K mode (ColdFire ISA-A decodes the same for this code). Static reading, except one run noted in "Not
  settled".
- **Address notation** as in `research/04-kits.md`: **OS $x** is the ColdFire OS (section 0, run at $200000); the
  working Kit sits at $70000a in patch memory, so its trig groups are **$70044a + track** and its mute groups
  **$70045a + track** (record +$440 and +$450). Internal SRAM ($1000000-) holds the OS's run-time state.
- Each fact is marked **[verified]** (read in the code or the document) or **[inferred]** (deduced, not seen directly).

## In short

- **Trig group** (manual: "TRIG POS", the trigger relation): when track A is triggered, the track A names is triggered
  in the same control tick, with the same velocity for a MIDI note (the source's accent for a sequencer or panel
  trig). One level only: no chains, as the manual says and the code confirms. **Mute group** ("MUTE POS"): when A is
  triggered, the track A names is silenced. A self-reference does nothing. [verified]
- **MIDI notes honour both.** MIDI note-on, sequencer trigs and the front panel's trig keys all go through one note
  handler (OS $20cc48), and the trig-group and mute-group steps sit downstream of it, in that handler and in the
  control tick (OS $20ad9a). [verified]
- **Mute is not a voice stop.** The OS sets a per-track flag (SRAM $1001510 + track). While it is set, the control
  tick writes 0 into that track's four mix words for DSP1 (VOL gain, PAN, REV send, DEL send). The voice keeps
  running on DSP2, its effects chain keeps running, and nothing is ramped: the cut lands on a 32-sample block
  boundary. The flag clears only when the muted track is itself triggered. [verified]
- **Timing:** both triggers of a trig group land in the same tick. A mute takes effect in that tick when the muted
  track has a higher number than the triggering one, otherwise one tick later. [verified]
- **Set in** KIT, EDIT, EDIT KIT menu, RELATE column, MUTE POS and TRIG POS fields. Any of the 16 tracks can be chosen,
  the current track included, or "-" for OFF. The values live in the working Kit and in Kit dumps (37 bytes at $4a7).
  SysEx $65 and $66 set one track's value; $7f means OFF, which the manual does not say. [verified]
- **mdEngine has the mute mechanism already:** `HostModel::setMute` zeroes the same four words the OS zeroes for a
  mute group (`HostModel.cpp:226-230`). What it lacks: the two 16-byte tables, a group-mute flag kept apart from the
  user's mute, a non-recursive second trigger, and setting and clearing the flag at trigger time. No OS routine and no
  DSP write is needed; the cost is a few comparisons per trigger. [verified]
- **Factory Kits:** no trig group anywhere in the 32. Mute groups appear in 12 of the 16 non-UW Kits and 9 of the 16
  UW Kits, almost all closed hat → open hat on track 9 → 10, sometimes both ways. Several Kits also hold a harmless
  self-reference (track 10 → 10). [verified]
- **Not settled:** whether a track freed from a mute group loses its first tick of sound when retriggered (the code
  order says yes; one firmware-emulator run was confounded), and how the hardware behaves with user-muted targets.
  See "Not settled". [inferred]

## 1. Behaviour on the hardware, per Elektron's manual

All from the manual (URL above), paraphrased.

- **What they are for** [verified, p. 19 (PDF 27), "SETTING MUTE AND TRIG RELATIONS OF A KIT"]: mute groups interleave
  drums such as open and closed hi-hats, which "are supposed to stop the sound of each other when played"; trig groups
  layer two drums on different tracks triggered by one trig.
- **Direction** [verified, same page]: the MUTE POS value of the current track is the track that is muted *when the
  current machine is triggered*; the TRIG POS value is the track played *in addition to the current one* when it is
  triggered. Both relations are one-way; a pair needs both tracks set.
- **Chains** [verified, same page]: trig relations do not chain. If the bass drum triggers the snare and the snare
  triggers the cymbal, the bass drum plays the bass drum and snare only. The manual says nothing on mute-group chains,
  cycles, self-reference, velocity or accent: see section 3 for what the code does.
- **Where** [verified, p. 18-19 (PDF 26-27)]: KIT window, EDIT icon, EDIT KIT menu (machine assignment per track).
  [LEFT]/[RIGHT] move between the SYNTH, MACHINE and RELATE columns and, inside RELATE, between MUTE POS and TRIG POS;
  [UP]/[DOWN] pick the track; [ENTER/YES] confirms. They are part of the Kit.
- **SysEx** [verified, Appendix C, p. C-3 (PDF 117)]: `$65` "set trig group" (trigger track 0-15, track to be
  triggered 0-15) and `$66` "set mute group" (mute-control track 0-15, track to be muted 0-15).
- **Not the same as the track mute** [verified, p. 44-45 (PDF 52-53), "THE TRACK MUTE WINDOW"]: a track muted from
  the MUTE window ([FUNCTION]+[A/E]) keeps sounding what it already plays, but further trigs are masked. A mute group,
  by contrast, stops the sound of the other track (p. 19).

## 2. Do MIDI notes honour the groups?

**Yes, by the OS code** [verified, details in section 3]: a MIDI note-on reaches the same note handler as the
sequencer's trigs, and both group steps are in that handler and in the control tick, with no test on where the trig
came from.

- MIDI input (OS $209de2): takes each message of the receive queue ($28d9b8), keeps a message below $c0 (notes, CCs)
  only when its channel is one of the base channel's four, and calls the handler of its status nibble from the table
  at OS $252d1e, with a source argument of **1**. The argument is **2** when the message sits in the buffer at $2a0200 (OS
  $209e20-$209e44), which the front panel's key routine fills (OS $212cb0-$212e6c) [inferred: panel keys]. Note-on
  ($9x) → OS $20cc48; note-off ($8x) → OS $20d1f6, which zeroes the velocity and calls OS $20cc48 too.
- Sequencer playback (OS $23980c, $23989e, $239cd0, $239faa, $23a03c, $23a44c) calls OS $206c8e, which dispatches its
  3-byte trig records through the same table with source argument **0** (OS $206c8e-$206cae).
- OS $20cc48 maps the note to a track through the global MIDI map ($7120dc, OS $20cc70-$20cc94), and leaves at once
  when that track is user-muted (`tst.b $1001520(track)`, OS $20ccf6-$20cd00): this is the masking the manual
  describes, and it also drops that trig's groups.

**Public statements** found: none from Elektron or Elektronauts on groups with MIDI notes. One Elektronauts user
reports that the Machinedrum's track mute silences externally sequenced notes too (ernestobv, "How to internally mute
an externally sequenced track", 2018-07-17,
https://www.elektronauts.com/t/how-to-internally-mute-an-externally-sequenced-track/59619, read 2026-10-08), which
agrees with the gate at OS $20ccf6. A 2022 thread on TRIG POS between MIDI-machine tracks concerns the unofficial OS
X.06 and MID machines, which MD Drums has no use for
(https://www.elektronauts.com/t/trig-pos-from-one-midi-track-onto-another-midi-track/174396).

## 3. How OS 1.63 implements them

### Who reads and writes the bytes

| OS address | Access | What |
|---|---|---|
| $20ce20 | read $70044a + t | note handler: trig group of the triggered track (velocity, LFO flag, MIDI out of the target) |
| $20adea | read $70044a + t | control tick: trig group → trigger the target |
| $20b3bc | read $70045a + t (`$452(a0)`, a0 = $700008 + t) | control tick: mute group → mute the target |
| $205a38 | write $70044a | SysEx $65 (command table entry $2524ac) |
| $205a7e | write $70045a | SysEx $66 (entry $2524b0) |
| $2353fa, $2353d0 | write | EDIT KIT, TRIG POS and MUTE POS confirmed with [ENTER/YES] |
| $23dd24-$23dd3c | write $ff | new or cleared Kit: all 32 OFF (`research/04-kits.md:197-198`) |
| $207b76-$207c5a | write (stored Kit) | Kit dump received (`research/04-kits.md:222`); LOAD KIT copies the whole record |

All [verified]. Only the working Kit is read at run time: a change to a group takes effect at the next trigger.

### Trigger path

1. **Note handler, OS $20cc48** (args: 3-byte message, source 0 sequencer / 1 MIDI / 2 panel) [verified]:
   - track t from the note map; user-muted → return (OS $20ccf6);
   - source 1: velocity byte $100154c[t] = note velocity, accent flag $1000a3c[t] = 0 (OS $20cd7c-$20cd90);
     source 0 or 2: velocity $80, accent flag = (byte > 111) (OS $20cdb0-$20cdce);
   - t's LFO trigger flag set (SRAM $1000f91 + $24·t, OS $20ce0a-$20ce1c);
   - **trig group** g = $70044a[t]; if not $ff (OS $20ce20-$20ce30): g's velocity and accent flag get the same values as
     t's (OS $20cf8c-$20cfe0), g's LFO trigger flag is set (OS $20d010-$20d022), g's parameter-lock state is reset to the
     Kit values (loops OS $20cec0-$20cf8a, arrays $2ad8a6/$2ad726/$2b034d [inferred: p-lock bookkeeping]), and a note-on
     for g's mapped note goes into the MIDI out ring when $252410 is set (OS $20ce34-$20ceb2) [inferred: the "send
     trigs to MIDI out" setting]. MID-machine targets are handled differently per source (OS $20cf98-$20d00e); MD Drums
     has no use for them;
   - **t's trigger flag** $1000a2c[t] = 1 (OS $20d026-$20d02e). g's is *not* set here.
2. **Control tick, OS $20ad9a** (called from the DSP2 handshake interrupt, `docs_PROTOCOL.md:246-254`; runs as fast
   as the CPU allows):
   - first loop, t = 15 down to 0 (OS $20add8-$20ae32) [verified]: triggered[t] ($262470 + 4t) |= $1000a2c[t]; accent
     latch |= $1000a3c[t]; **if $1000a2c[t] and trig group g ≠ $ff: triggered[g] = 1**; then $1000a2c[t] = 0. Only the
     handler's flags are read, never `triggered[]`, so **a trig-group target never fires its own trig group: no chain**.
     A self-reference sets a flag already set: no effect (MIDI out aside);
   - per-track loop, t = 0 to 15 (OS $20af52-$20b44c): a triggered track applies its pending machine, snaps its level to
     the Kit level (smoothed and target, no slew, OS $20b022-$20b04a), applies p-locks and restarts its LFO
     ($204c94, $10001e8); then **every** track's five DSP1 mix words are computed (OS $20b1e2-$20b302); then the
     machine function runs (OS $20b330-$20b39e);
   - **mute group**, still in t's iteration, only when t was triggered (OS $20b3a2-$20b3e2) [verified]: g =
     $70045a[t]; if not $ff, **group-muted[g] = 1** ($1001510 + g); then **group-muted[t] = 0**. With g = t the flag is
     set then cleared: a self-reference does nothing. A trig-group target is triggered, so its own mute group applies
     too.
3. **The mute itself** (OS $20b20c-$20b24e) [verified]: when group-muted[t] = 1, the tick writes the route word and then
   0, 0, 0, 0 for VOL gain, PAN, REV and DEL (`Y:$100+5k` words 1-4, `docs_PROTOCOL.md:337-343`). The same zeroing
   happens for a user-muted track only if its machine is an INP machine ($50-$55, OS $20b21c-$20b244). The 80 mix words
   go to DSP1 at every group-3 handshake interrupt (SRAM $10004b2 → OS $20ab1c), that is, every 32-sample block.
   DSP1 applies a track's gain as a constant over the block (`Mixer.cpp:39-61`, a translation that Machinemodule
   reports bit-exact against DSP1's code, `docs_PROTOCOL.md:403-405`), so the cut has no ramp. Tracks on individual
   outputs are cut too (VOL = 0, `Mixer.cpp:42-50`). The REV and DEL sends stop; tails already in the master effects
   carry on. [verified]
4. **Release:** nothing else writes $1001510 (searched: absolute $10014f0-$100152f and every `$ae4(` displacement from
   the $1000a2c base). The flag is zero at boot (outside the SRAM image, `docs_PROTOCOL.md:267-268`), stays set across
   Kit loads and group edits, and clears only when the muted track is triggered. [inferred from the search]

### Same tick?

- **Trig group:** source and target get their trigger in the same tick (first loop), so they start in the same block.
  [verified]
- **Mute group:** the flag is set during the source's iteration, and the target's mix words are computed during the
  target's. Target number above the source's (CH on 9 mutes OH on 10): zeroed in the same tick. Below: one tick
  later. [verified]
- **Retriggering a muted track:** its mix words for that tick are computed *before* its own flag is cleared
  (OS $20b210 before $20b3e2), so they are still zero, and they get a gain at the next tick. The voice starts in the
  meantime. How much of the attack that costs is in "Not settled". [verified order, inferred effect]
- The note handler runs as MIDI arrives, the tick later: on the MD every trigger waits for the next tick.
  `HostModel::renderBlock` instead updates a triggered voice at the next block (`HostModel.cpp:271-279`).

### Other findings on the way

- `docs_PROTOCOL.md:272-273` and the comment at `HostModel.cpp:155` call OS $20cdf0 "the OS's track trigger". It is a
  point inside the note handler, whose entry is OS $20cc48. [verified]
- At boot the OS overwrites the handler's first word with `rts` (OS $200f4a-$200f5e, the original kept at $28d6c0) and
  restores it at OS $225374. Every note is dropped until then, which may explain the ~14 s of silent notes after
  `isFirmwareMidiReady()` that `CLAUDE.md` describes. [verified patch; inferred link; the caller of $225374 not
  traced]
- Every trigger snaps the track's level to the Kit level, smoothed value and target alike (OS $20b022-$20b04a).
  `HostModel::trigger` does not do this (`HostModel.cpp:102-171`). [verified]
- `HostModel.h:9` says trigger groups are "sequencer features". They are Kit features and act on MIDI notes too.

## 4. What mdEngine would need

What is there [verified]:

- `HostModel::trigger` (`HostModel.cpp:102-171`): velocity, accent, the LFO trigger flag (`:155-156`), pending
  machine, voice budget. `updateVoice` (`:183-213`): LFO restart, machine function and slot, then `updateMixer`
  (`:211`), then the trigger cleared (`:212`). Triggers between ticks are updated at once (`:274-279`).
- **The mute mechanism:** `setMute` (`HostModel.h:64`) → `m_mute` (`:107`) → `updateMixer` writes 0 into mix words 1-4
  (`HostModel.cpp:226-230`): exactly what the OS does for group-muted[t]. MD Drums uses it for its own mute parameter
  (`mdDrumsEngine.cpp:126-129`), and its per-track outputs scale by the same VOL word (`mdDrumsEngine.cpp:179-182`),
  so a mute through these words cuts them too.
- **Voice stops** exist, but the OS does not stop a voice for a mute group: `silenceVoice` (`HostModel.cpp:89-99`)
  switches the voice to GND-- (budget cuts, `m_silenceNext`, `HostModel.h:116`, `HostModel.cpp:195-199`), and the
  silence release (`setSilenceRelease`, `HostModel.h:58`; `m_quiet`, `:120`; `HostModel.cpp:295-312`) frees a voice
  whose DSP2 output (taken before the mix) has died away. `VoiceEngine` itself only takes slot words
  (`VoiceEngine.h:37`).

What reproducing the MD takes, in mdEngine terms [inferred: design sketch, not built]:

1. **Tables:** `trigGroup[16]`, `muteGroup[16]` ($ff = OFF), set from a Kit dump's 37 bytes at $4a7 (which
   `parseKitDump` does not read yet, `research/04-kits.md:245`) or from the Device's own state.
2. **Trig group:** in `trigger(A, vel, accent)`, run the per-track part once more for B = trigGroup[A], with the same
   velocity (MIDI) or accent (sequencer), never recursing (no chain), with A's LFO flag and B's. Both land in the same
   block, as on the MD. Cost: the target's voice, which the MD pays as well.
3. **Mute group:** a `m_groupMute[16]` kept apart from `m_mute` (the OS keeps $1001510 apart from the user mute
   $1001520). In `updateVoice(t)`, when t is triggered: `updateMixer(t)` with the flag as it was (OS order), then set
   `m_groupMute[muteGroup[t]]` and clear `m_groupMute[t]`. `updateMixer` zeroes words 1-4 when either flag is set.
   No MachineRunner call and no DSP write: the OS work is bookkeeping, and HostModel already produces the words.
   Calling the OS routines is not an option: OS $20cc48 carries MIDI out, p-lock and UI side effects, and HostModel
   replaces the tick ($20ad9a) rather than calling it.
4. **When the target's words change:** at the next tick (`setBlocksPerTick`, 11 blocks by default), as on the MD for a
   lower-numbered target. Recomputing the target's words at once is cheap if a tighter cut is wanted. That differs from
   the MD by up to one tick.
5. **CPU:** a group-muted voice keeps running on DSP2, as on the MD, until it decays and the silence release frees
   it (when enabled). Calling `silenceVoice` on the target instead would save DSP time, at the cost of the target's
   noise-generator state and its effects chain's state at its next trigger. That is not bit-exact, though probably
   inaudible.
6. **User mute semantics:** on the MD a user-muted track's trigs are dropped, groups included (OS $20ccf6), while what
   it already plays rings on. `HostModel::setMute` gates the output instead, with triggers still processed, so a muted
   source would still fire its groups. "Exactly as the Machinedrum" calls for a decision on MD Drums' mute parameter.

## 5. Factory Kits

Read from container sections 3 (non-UW) and 4 (UW), Kit n at +$8ca + $460·n (`research/04-kits.md:132-149`), groups
at record +$440/+$450. Machines by id from `source/elektron/md/mdLib/mdmachines.cpp:48-89`. Tracks 1-16. [verified]

**Trig groups: OFF in all 32 Kits** (and in both images' working Kit). This agrees with `research/04-kits.md:161-162`.

Mute groups (source track (machine) → muted track (machine); unlisted Kits have none):

| Image | Slot | Kit | Mute groups |
|---|---|---|---|
| UW (S4) | 1 | TRX UW | 9 TRX-CH → 10 TRX-OH |
| UW | 3 | E12 UW | 9 E12-CH → 10 E12-OH |
| UW | 4 | P-I UW | 9 P-I-HH → 10 P-I-HH |
| UW | 5 | USERWAVES | 9 ROM-11 → 10 ROM-12; 10 → 10 (self) |
| UW | 6 | HITHOT | 9 ROM-11 → 10 EFM-HH; 10 EFM-HH → 9 ROM-11 (both ways) |
| UW | 8 | GOLDCHAINS | 9 E12-CH → 10 E12-OH; 10 E12-OH → 9 E12-CH (both ways) |
| UW | 9 | BEATDOWN | 9 E12-OH → 10 E12-OH |
| UW | 13 | ROMANCE | 9 EFM-CY → 10 EFM-CY |
| UW | 14 | FOUROHFOUR | 14 RAM-R1 → 16 RAM-P1 |
| non-UW (S3) | 1 | TRX | 9 TRX-CH → 10 TRX-OH |
| non-UW | 3 | E12 | 9 E12-CH → 10 E12-OH |
| non-UW | 4 | P-I | 9 P-I-HH → 10 P-I-HH |
| non-UW | 5 | FRANSISCO | 6 EFM-CP → 7 P-I-RS; 9 E12-OH → 10 E12-OH; 10 → 10 (self) |
| non-UW | 6 | BIPBOP | 9 E12-CH → 10 E12-OH; 10 → 10 (self) |
| non-UW | 7 | WIGGLY | 6 EFM-SD → 7 E12-RS; 9 E12-OH → 10 E12-OH; 10 → 10 (self); 11 GND-NS → 5 EFM-XT |
| non-UW | 10 | BOOGIE | 6 EFM-CP → 7 E12-BR; 9 E12-OH → 10 E12-OH; 10 → 10 (self) |
| non-UW | 11 | SWARM | 6 E12-BR → 7 EFM-RS; 9 EFM-HH → 10 E12-OH; 10 → 10 (self) |
| non-UW | 12 | MAYHEM | 6 EFM-CP → 7 E12-RS |
| non-UW | 13 | HAUNTED | 9 GND-NS → 10 E12-OH; 10 → 10 (self) |
| non-UW | 14 | ALIENS | 9 E12-SD → 10 E12-OH; 10 E12-OH → 9 E12-SD (both ways) |
| non-UW | 16 | FUGLESANG | 6 EFM-SD → 7 EFM-CP; 9 EFM-HH → 10 E12-OH; 10 → 10 (self) |

No groups: EFM UW, CLUBBING, DARKER, DONTGOA, KONTAKTE, RESAMPLING, SEACLONES (UW); EFM, BENTY, TOAST, BRONCO
(non-UW). The pattern is track 9 → 10 (the default "09-CH" → "10-OH" labels of the RELATE list, OS table $260ce4),
plus a few 6 → 7 pairs (clap or snare cutting a rim or second clap). The self-references (10 → 10) do nothing, per
OS $20b3c8-$20b3e2.

## Not settled

- **Attack of a retriggered group-muted track.** By the code order (OS $20b210 before $20b3e2) its first tick of mix
  words is zero, while its voice starts. If so, an open hat struck after the closed hat that cut it loses up to one
  tick of its attack. One run settled nothing: `mdTrigLatencyFirmwareTest` (the existing build, under a 150 s guard,
  killed after its first section) played notes 34-63 once each on factory Kit 1 after boot. Note 52 (track 10,
  TRX-OH, muted by note 50 just before) sounded 376 samples after the note, against 158-285 for the other 14 tracks
  and 164-310 for 48 hits of track 1 (jitter 146 samples, the tick at idle). But that OH has GAP = 57 and peaks at
  0.026, so its late onset may be its own. To settle it, record track 10 alone and right after track 9, same
  parameters and phases, on the firmware, then repeat with the mute group set OFF (SysEx `$66 08 7f`).
- **User-muted trig-group targets.** Nothing in the tick checks the user mute ($1001520) of a trig-group target
  (OS $20ae16-$20ae1e, $20af52-$20b3e6), so it should sound when its source fires. Static only; not tried.
- **Hardware confirmation** that MIDI notes fire the groups: code only, no Elektron or forum statement found.
- **Source 2 = front-panel keys** and **$252410 = "trigs to MIDI out"**: inferred from the code around them, not traced
  to a menu.
- **Nothing else clears the group-mute flag:** shown by a search of absolute addresses and of the $1000a2c-based
  displacement only, so a write through another base register would have been missed.
