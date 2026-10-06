# CPU-to-DSP protocol: findings from tracing gearmulator-md-mm

Captured with `tools/mdtrace` against `elektron_sps1-1uw_os1.63.bin` (the same image
`docs/FIRMWARE.md` decodes from the `.syx`). 5 emulated seconds, `mdTraceTool ROM.bin 0 220500`.

## DSP roles: confirmed

`FIRMWARE.md` guessed the roles from section size. Tracing settles it: the actual ColdFire→DSP
program transfer was captured and matched **word for word** against the two sections decoded by
`tools/mdfw` from the `.syx`.

| DSP (gearmulator-md-mm) | HI08 window | OS file section | Role |
|---|---|---|---|
| DSP1, index 0 (`m_dspMixer`) | `$500000` | **section 2** (small, 18,823 words) | mixer / codec / master FX |
| DSP2, index 1 (`m_dspProducer`) | `$600000` | **section 1** (large, 250,123 words) | voice producer |

This confirms the split the user wants: **Machinedrum One** (the voice machines) is section 1 on
DSP2; **Machinedrum FX** (the master effects, as a separate plugin) is section 2 on DSP1. It also
confirms `tools/mdfw`'s decoding is structurally and byte-exactly correct: the ColdFire firmware's
own program transfer carries precisely the record stream the `.syx` decodes to.

## How the transfer actually works: two stages

The wire protocol is not "one record stream over the standard dsp56300 boot protocol" as first
guessed. It's two stages:

1. **A tiny first-stage loader**, uploaded via dsp56300's ordinary `DspBoot` Length/Address/Data
   host-port protocol (`BOOT dspN word=...` in the trace): 153 words to DSP1 at P:$100, ~184 words
   to DSP2. This is what the `mddsp.cpp` comment calls "the Machinedrum second-stage loader" — the
   thing that "uploads its main program into external SRAM and jumps there". `onDspBootFinished()`
   fires as soon as *this* tiny upload completes; gearmulator-md-mm's own scheduler considers the
   DSP "booted" at that point, well before the real program has arrived.
2. **The real program**, streamed in afterward as ordinary runtime host-port words (`RUN dspN
   UC->DSP` in the trace) — read by the first-stage loader now running *on* the DSP, not by any
   C++-side boot state machine. This is the record stream `tools/mdfw` decodes: `[3][$24]`
   (unconsumed by the wire — it configures the loader before transfer, not sent) then, exactly as
   the file has it, back-to-back `[space][addr][count]` headers each followed by `count` data
   words. The trace matched the section byte-for-byte from the first data word (`$24`, the shared
   vector-table load address both programs start at) for as far as compared (see below).

Once a DSP's record stream is exhausted, the traffic changes character: both DSPs finish loading and
reach runtime operation, and the rest of the traffic is the runtime protocol below.

## Runtime protocol (decoded)

Captured with `tools/mdtrace` (`mdProbe`) on a properly booted machine: first-run flash
initialisation, then 20 emulated seconds of `advance()`, then scripted triggers, MIDI CCs and sysex.
The DMA log records every word the host writes into DSP memory, so the tables below are exact, not
inferred from the host-side word order.

### Host commands: the ColdFire just writes DSP memory

Both DSPs expose the same small command set on their HI08 port (`vba` = vector address):

| Vector | DSP2 handler | DSP1 handler | Does |
|---|---|---|---|
| `P:$12` | `$e8` | `$9aa` | **Block write.** Handler reads a destination address and a count from HRX, then programs DMA channel 5 (source HORX, request = host receive, destination space **Y**) to copy `count+1` words from the host port into `Y:dest`. |
| `P:$10` | `$f4` | `$9b6` | **Peek.** Reads an address, replies with one word (DSP2 `X:(addr)`, DSP1 `Y:(addr)`). The ColdFire polls a few DSP1 words (`Y:$1be/$1bf`, meters) this way. |
| `P:$14` | `$fd` | — | Batched table lookup (DSP2 only; not seen at runtime). |

So there is no "parameter block protocol" as such: the ColdFire keeps a set of fixed-layout structures
in each DSP's Y memory up to date, and the DSP code reads them. Our host model can do the same by
writing DSP memory directly — no HI08 emulation needed.

### Update rate

Everything runs on a fixed control tick of **96 samples** (2.18 ms): DSP1's structures are rewritten
every tick, DSP2's voice slots every other tick (192 samples).

### DSP2 (voice producer): 16 voice slots

`Y:$800 + $40·k` for voice k = 0..15 (track 1..16), 64 words each; the host writes the first 13.

| Word | Content |
|---|---|
| 0 | **Trigger/machine code.** Non-zero only on the tick a voice is triggered: machine index + 1, with bit 7 set for the UW (ROM/RAM) machine table (UW machine 16 → `$91`, 32 → `$a1`; classic-table default kit tracks gave `$1d`, `$12`). 0 on every other tick. |
| 1..12 | **Machine coefficients**, precomputed by the ColdFire from the track's SYN1-8 parameters (plus LFO modulation). Re-sent every tick while the voice is active; idle voices only get words 0-1 = 0. |

Parameter sweep on track 1 (TRX default kit machine), CC 16-23 = SYN1-8:

| Param | Slot words changed |
|---|---|
| SYN1 | 1 |
| SYN2 | 3 |
| SYN3 | 2 |
| SYN4 | 4 |
| SYN5 | 12 |
| SYN6 | 11 |
| SYN7 | 9, 10 |
| SYN8 | 5, 6, 7, 8 |
| LFOD, LFOM (LFO depth/mode) | 1 (the LFO modulates word 1 over time) |

The words are machine-specific coefficients, not the raw 0-127 values (e.g. SYN6 = 20 → `$140000`,
100 → `$640000` is linear, but SYN8 changes four filter-style coefficients at once). **Computing
these 12 words per machine is the core of the host model** — see "Open: coefficient generation".

### DSP2 → DSP1: 16 separate voice streams

The ESSI0 link carries 16 words per sample period, as 512-word superframes every 32 samples: **one
32-sample mono block per voice**, voice k at block k (inactive voices send zeros). Triggering tracks
1, 2 and 5 puts the audio in blocks exactly 1 and 4 apart. DSP2 does **not** mix: per-voice audio is
fully separable at this link.

### DSP1 (mixer): per-track effect chain, mix and master FX

| Y address | Per | Content |
|---|---|---|
| `$200 + $40·k`, words 0-8 | track k | **AMD, AMF, EQF, EQG, FLTF, FLTW, FLTQ, SRR, DIST**, each the raw 0-127 value `<< 7` |
| `$100 + 5·k`, words 0-4 | track k | word 1 VOL (a computed gain), 2 PAN (`value << 16`), 3 REV send, 4 DEL send |
| `$150-$158`, `$170-$18c` | global | change every tick (master FX / modulation state); not decoded |

So the whole **per-track effects page (amp modulation, EQ, filter, sample-rate reduction,
distortion) runs on DSP1**, together with the volume/pan mix and the master effects. DSP2 only
produces the raw machine voices.

### DSP load

Measured with `mdProbe prof` (PC histogram + instruction counters):

| DSP | Idle | 16 voices / pattern playing |
|---|---|---|
| DSP2 (voices) | spins in a delay loop at `P:$100090` (~96% of samples): inactive voices cost ~nothing | ~45% in the link-DMA wait at `P:$cf` → roughly **35-60 M instructions/s** of work |
| DSP1 (per-track FX, mix, master FX) | no wait loop in its hot spots: filter/MAC loops over 32-sample blocks | same: **~79 M instructions/s, always** |

Monomodule's one Monomachine track is ~21 M instructions/s and needed the static recompiler to fit
58% of one Force core. Emulating both MD DSPs is roughly 5-7× that.

## Host model: the ColdFire side (decoded)

Found by watching the ColdFire (68k) code that writes DSP2's host port (`tools/mdtrace`: `cf:on`,
`watch:`), then reading the OS image (section 0 of the `.syx`, load base `$200000`; the running RAM
matches the file byte for byte) with Capstone (`tools/mdtrace/analysis/dis68.py`).

- **Pump.** An interrupt handler in the ColdFire's internal SRAM (`$1000760-$100077c`) streams a
  staging buffer of 32-bit words (`$010015b4 + …` per voice) to DSP2's HI08. It only transports.
- **Machine descriptor table** at `$24ef54`: 135 records of 86 bytes = `[coefficient function
  pointer][machine id][5-char name][8 × 4-char parameter names][8 defaults][flags…]`. It covers
  every MD UW machine: GND, TRX, EFM, E12, P-I, INP, MID, CTR, ROM, RAM
  (`tools/mdtrace/analysis/machines.py` lists it from your own OS file). MID/CTR point at a stub;
  ROM/RAM machines share two functions.
- **Machine coefficient functions are pure.** Each is compiled C, called as
  `count = fn(uint32_t *out, const uint16_t *params)`: it reads the 8 SYN parameters as 16-bit
  values, computes `out[1..12]` with arithmetic and lookup tables that live in the OS image, and
  returns the word count. No other OS state. Example: TRX-B2 (`$201fd2`) is ~70 straight-line
  instructions. **So Machinedrum One can call the original functions in a 68k emulator (Musashi,
  already in md-mm) for exact results at negligible cost** (worst case 16 voices × every 192-sample
  tick ≈ 3,700 calls/s ≈ 0.3 M 68k instructions/s).
- **Caller** (`$20b394`, inside the per-voice tick routine): `jsr` through a per-voice function
  pointer array in RAM (`$29f27c`), passing the voice's staging slot and `a6`. **`a6` is the track's
  current parameter array: 24 16-bit values** (`$0-$e` SYN1-8, `$10-$20` AMD..DIST, `$22-$28`
  VOL/PAN/sends, …), i.e. the kit values after the OS's own modulation. The same routine turns the
  non-SYN part into DSP1's per-track values (with velocity/accent handling).

### The per-track parameter pipeline (decoded)

`a6` for voice k is `$010011cc + $30·k` in internal SRAM (24 16-bit values). It is produced each
**sequencer tick** by three routines that the OS copies into internal SRAM (`$1000000` = OS image
`$2622f4`), called from the tick routine (`$20ad9a`) at `$20b488..$20b49c` after the per-voice loop:

1. **Smoothing** `$10002e0`: for all 16 tracks × 24 parameters (two per 32-bit word),
   `cur = (3·cur + (raw << 7)) >> 2`, masked to `$3fff`. `raw` = kit bytes at `$1000ddc`
   (16 × 24), `cur` = working array at `$1000a4c` (16 × 48 bytes). Same slew as the Monomachine.
   A second smoother (`$100029e`, 48 values at `$1000d7c` from `$1000f5c`) handles global values.
2. **LFO apply** `$1000332`: for each track's LFO (structs at `$1000f8c + $24·k`: destination
   track and parameter, two shape outputs S1/S2 at `+$10/+$14`), `lfo = ((0x3f80 - LFOM)·S1 -
   LFOM·S2) >> 14`, `dest = clamp(cur + (LFOD · lfo) >> 14, 0, $3fff)`, then copies all 16 working
   arrays to the `a6` arrays and restores the un-modulated values. (`$10001e8` = the same for one
   track, used at trigger.)
3. **LFO oscillator** `$1000088` (called when flagged): per LFO, a phase increment from LFOS (param
   21) and a tempo factor at `$100150c` (linear below `$1fff`, cubic above), `phase = (phase + inc)
   & $7fff`, then the OS waveform routine `$204c94(k)`, which calls two of 8 **shape functions**
   through a table at `$2523ee` (args: track, shape 0/1, trigger flag), then the decaying shape
   types 3 (linear fall) and 4 (exponential decay).

**Tick rate:** at 120 BPM the tick routine ran 128.6 times/s = 64 per beat, i.e. the parameter
pipeline is tempo-synced; only the HI08 transport to the DSPs runs at a fixed rate. (To confirm at
other tempos.)

**Cost if we call these routines in a 68k emulator** (measured, `mdProbe count`): smoothing + LFO
0.5-0.65 M 68k instructions/s, LFO shapes ~0.15 M/s, machine functions up to ~0.2 M/s: **~1 M/s in
total**, a few percent of a Force core. For comparison the entire ColdFire OS runs ~4.1-4.8 M/s.

### Host model plan: hybrid

Our C++ owns the tick schedule and the inputs, the MD's routines do the maths:

- C++ writes the kit bytes (`$1000ddc`), LFO settings (struct fields), tempo factor (`$100150c`),
  and trigger flags into a memory image built from section 0 of the user's `.syx` (+ the internal
  SRAM code copy), then calls, in Musashi: smoothing, LFO oscillator, LFO apply, and for each voice
  its machine function with its `a6` array. It writes the 13-word result and trigger code straight
  into DSP2's voice slot.
- Still to translate from the tick routine (`$20ad9a`, the per-voice loop around `$20af52-$20b44c`):
  trigger handling (machine code in word 0, LFO restart per LFO mode), velocity/accent, and the
  DSP1 per-track values it derives from `a6` (volume, pan, sends, effect parameters). This is
  ordinary compiled C, readable with `analysis/dis68.py`.

Alternative, kept as a fallback: run the **whole** ColdFire OS in Musashi (as md-mm does) and drive
it with MIDI. Exact with no translation at all and only ~4-5 M 68k instructions/s, but it brings the
sequencer, the 20 s boot / first-run flash state, the three-processor scheduling md-mm needed, and
MIDI-UART latency and bandwidth limits on every parameter change from the plugin.

## Voice engine prototype: verified bit-exact

`engine/VoiceEngine` runs DSP2's program from the user's `.syx` alone in dsp56300 (Monomodule-style
harness), and `engine/MachineRunner` runs the OS's machine coefficient functions in Musashi.

**DSP2 main loop** (`P:$64-$e7`, once per 32-sample block): for voice k = 0..15, if slot word 0 is
non-zero it calls the machine's init routine on a machine change (`Y:$145af5 + code`) and its
trigger routine (`Y:$145bb6 + code`) and clears word 0; then it calls the machine's render routine
(`Y:$145c77 + code`) into a ping-pong buffer (`Y:$100`/`$120`, pointer in `Y:$140`) and DMAs the 32
samples to the ESSI0 link (voice 0 first waits for the link frame sync on Port C). Init (`$24`):
`$100000`/`$100012` fill unused machine-table entries depending on the memory map (AAR2), `$100024`
sets up ESSI, DMA and interrupts, clears the slots and builds a table at `$148000`.

**Harness:** load section 1's records; set AAR0-3 = `$100539 $140639 $180539 $1c0639` and OMR =
`$00498d` (DSP2's state in md-mm); run from `$24`; ESSI ports get non-blocking silent input and
discarded output; patches: `$73` (per-group handshake word to the ColdFire) → nop; `$b5` → a stub
that sends the voice's 32 samples out over HI08 and continues at `$d5`; `$e2` → wait for a host
"go" word, then `jmp $64`; the silent-voice render's timing-padding loop (`$100093-4`) → nops
(output unchanged).

**Results:**
- Voice 1 (TRX-B2) triggered with md-mm's captured slot words: **6,400/6,400 samples identical** to
  md-mm's link output over 200 blocks.
- `MachineRunner` with md-mm's captured parameter arrays reproduces the slot words **exactly** for
  TRX-B2 (103 68k instructions) and TRX-SD (82 instructions).
- DSP2 cost in the harness: ~4,500 instructions per block for the loop and 16 silent voices
  (6.2 M/s), ~1,300 more per playing TRX-B2 voice (~1.8 M/s per voice).

**Silent voices skipped (2026-09-27):** a voice's persisted "current machine" code (the `Y:$142+
$153`-indexed table, read at `P:$a3`) is 0 before any trigger and `id+1` after — the empty machine
GND-- (id 0) is applied to all tracks at boot, so idle tracks actually read back **1**, not 0 (see
"per voice, every tick" below). Harness patch: redirect `P:$a8`'s render-function lookup (normally
`r1 = y:(r0+$145c77)`) through a check — code 0 or 1 skips the `jsr` and instead clears the
32-sample buffer directly (`kSkipStub`/`kSkipNormal`/`kSkipSilent`/`kClearVoice` in
`VoiceEngine::installHarness`). GND--'s real render and this clear both write 32 zeros, so output
is unchanged: confirmed byte-identical to the pre-patch engine over 200 blocks, both for an idle
voice (all-zero output) and for a voice playing TRX-B2 (2,692/6,400 nonzero samples, unchanged
sample values). Cost: **16 silent voices 6.2 → 2.7 M/s; one playing voice + 15 idle 8.0 → 4.8
M/s.** (Gotcha: this assembler's `Bcc_xxxx` — `beq`/`bra`/etc. — takes a raw PC-relative
displacement as its operand, not an address, unlike `jmp`/`jclr`/`jset`; giving it an absolute hex
address sent the DSP to an invalid PC. `jeq`/`jmp`, the absolute forms, are correct here.)

## Host model: tick scheduling and the per-voice rule (decoded)

- **What drives the tick.** DSP2's main loop sends the ColdFire a word after every 4 voices (group
  0-3, `P:$73`), which raises a ColdFire interrupt (handler `$100043a` in internal SRAM). Group 3
  runs the slot pump (`$10004d0`) and other jobs; group 1 calls the tick routine `$20ad9a`; every
  4th group-2 interrupt flags the LFO oscillator. The tick routine holds a lock and runs with
  interrupts enabled, so the next tick starts only after it finishes: the tick rate is **CPU-bound,
  ~120 Hz** in gearmulator-md-mm (intervals of 12-14 DSP blocks, varying with load) and **not
  tempo-synced**. Smoothing, LFO oscillator and LFO apply run once per tick. On the real machine,
  LFO and smoothing speed therefore vary slightly with load; the host model uses a fixed schedule
  (default one tick per 11 blocks, 125.3 Hz).
- **Tempo** is a factor at `$100150c` = BPM × 24 (default 3000 = 125 BPM), used by the LFO speed and
  by the E12/ROM machines' retrigger times.
- **Per voice, every tick:** the OS calls the voice's machine function on its current `a6` and the
  pump sends the returned words. Word 0 is staged as the trigger flag (1/0) and the pump replaces a
  non-zero value with **current machine id + 1** (UW ids ≥ 128 give bit 7). Idle tracks have the
  empty machine GND-- (`$201128`), whose function returns 2: that is the 2-word `[0, 0]` update.
  MID/CTR machines (ids `$60-$7b`) are not sent. A pending **machine change is applied at the
  trigger**: the track's 24 kit values go straight into the targets, the smoothed array and `a6`
  (no glide), and the voice's function pointer switches.
- **DSP1 per-track effects slot** (`Y:$200+$40·k`, 9 words) is sent by `$1000702` as `a6` words 8-16
  unchanged (AMD..DIST after smoothing and LFO). The 5-word block at `Y:$100+5·k` is computed in the
  tick routine (`$20b1f6-$20b302`), decoded below ("Mixer DSP inputs").
- **Internal SRAM** `$1000000-$1000a2a` is the OS's copy of image `$2622f4`, apart from a few bytes
  of variables near the start; everything after is zero-initialised state.

- **LFO settings and trigger.** LFO k belongs to track k; its struct (`$1000f8c + $24·k`) starts with
  the kit's LFO settings: byte 0 destination track, 1 destination parameter, 2 shape 1, 3 shape 2,
  4 type; byte 5 is the trigger flag, `+$20` the phase. The OS's track trigger (`$20cdf0`) sets the
  track's own LFO flag (and the trigger-group track's). The tick's trigger path then calls the
  waveform routine `$204c94(k)` (type bit 0 TRIG: phase reset; bit 1 HOLD: output only taken at a
  trigger; FREE = 0) and applies that LFO immediately (`$10001e8(k)`), before the machine function.
  Speed, depth and mix are track parameters 21-23. Sysex `0x62 [track<<3|setting] [value]` sets
  settings 0-4 on the hardware.
- **Slot transport can drop a state.** md-mm computes a slot every tick but its pump runs on other
  interrupts, so a computed slot is occasionally overwritten before it is sent (seen at an LFO
  peak). The host model writes every tick's slot.

`engine/HostModel` implements this with the OS's routines in `MachineRunner` (which now holds the
OS's main RAM and internal SRAM). **End to end** (assign machine, set 24 parameters, trigger →
MachineRunner → VoiceEngine): TRX-B2 on track 1 and TRX-SD on track 2 are each **6,400/6,400
samples identical** to gearmulator-md-mm. **With an LFO** (TRX-B2, LFO → PTCH, triangle, TRIG,
speed 80, depth 100): the per-tick voice arrays md-mm passed to the machine function give exactly
the slot sequence the host model produces, 20/20 ticks.

### DSP2 cost per machine (measured)

One voice of each machine triggered with its defaults, 150 blocks, `mdhost` (DSP2 M instructions/s
at 44.1 kHz, including the harness's 16-voice loop; baseline with all voices silent = 6.2 **at the
time of this table** — the other 15 idle voices' cost, not the playing one's, dropped afterward
when silent voices were skipped: baseline is now 2.7, so subtract ~3.5 from every figure below for
the current harness).

GND-- 6.2 | GNDSN 6.7 | GNDNS 6.5 | GNDIM 6.2 | TRXBD 8.6 | TRXSD 8.3 | TRXXT 6.8 | TRXCP 6.2 | TRXRS 8.0 | TRXCB 8.8 | TRXCH 8.8 | TRXOH 8.8 | TRXCY 9.5 | TRXMA 6.2 | TRXCL 6.2 | TRXXC 6.8 | TRXB2 8.0 | TRXS2 9.6 | EFMBD 8.0 | EFMSD 8.8 | EFMXT 8.2 | EFMCP 8.3 | EFMRS 9.2 | EFMCB 9.7 | EFMHH 8.9 | EFMCY 9.3 | E12BD 7.6 | E12SD 8.5 | E12HT 7.7 | E12RS 8.5 | E12OH 7.7 | E12RC 8.5 | E12CC 7.7 | E12SH 8.3 | P-IBD 9.5 | P-ISD 9.8 | P-IMT 9.8 | P-IML 8.8 | P-IMA 7.8 | P-IRS 9.8 | P-IRC 9.2 | P-ICC 9.2 | P-IHH 9.2 | INPGA 6.6 | INPFA 7.2 | INPEA 7.2 | ROM01 6.2 | ROM25 6.2 | RAMR1 8.1 | RAMP1 6.2

So one playing voice adds ~1.5-2.5 M/s (TRX, E12) up to ~3.6 M/s (EFM-CB, P-I). TRX-CP/MA/CL and
ROM (no sample data) stayed at baseline with defaults. A busy kit (6-8 voices ringing) is ~20-30 M/s
plus overhead; the worst case, 16 P-I voices at once, ~60 M/s. Silent voices can be skipped (most of
the 6.2 M/s baseline). For scale: Monomodule's one Monomachine track, ~21 M/s, needed 58% of a Force
core with the static recompiler.

## Design consequences

- **All voices from one instance: yes.** One DSP2 renders all 16 voices; the host drives 16 slots.
  Note-number → voice mapping is our own host-model choice.
- **Individual voice outputs: yes**, at the DSP2→DSP1 link — but those are *dry* voices. The MD's
  per-track sound includes its effects page (filter, EQ, SRR, distortion, AMD), which runs on DSP1.
  Per-track outputs *with* those effects need either DSP1's per-track buffers tapped before its
  pan/mix, or the chain reimplemented natively.
- **Machinedrum One needs DSP1's per-track section, not just DSP2.** "Voices only, no master FX" is
  not "DSP2 only": the filter/EQ/distortion that define an MD track are on DSP1.
- **CPU is the main risk on the Force.** Full emulation of both DSPs is ~3-4 Force cores at the
  Monomodule port's efficiency. Options, cheapest first:
  1. DSP2 emulated (machines are complex DSP code), DSP1's per-track chain + mixer **reimplemented
     natively in C++** from its disassembly (standard filters/EQ/SRR/distortion; ~10× cheaper than
     emulation, but "very close" rather than bit-exact), master FX likewise for the FX plugin.
  2. Both emulated, with the static recompiler, plus polyphony limits.
  3. Desktop first (full emulation is fine on a desktop CPU), Force later.

## Open

- The tick routine's per-voice orchestration and DSP1 values (see "Host model plan: hybrid").
- Tick rate vs. tempo (64 per beat at 120 BPM measured).
- The rest of DSP2's 64-word slot (words 13-63: DSP-side voice state?) and where pitch/note enters.
- DSP1's global blocks (`$150-$18c`): master FX parameters and per-tick modulation.
- Whether DSP1 keeps per-track processed blocks in memory before the pan/mix (for per-track outs
  with effects).
- Sample data for E12/ROM machines (DSP2's ~233K external P words).

## Mixer DSP inputs (decoded, verified)

Per track, each tick, from the tick routine `$20b1e2-$20b302` (MID/CTR machines send nothing):

| Y:$100+5k word | Value |
|---|---|
| 0 | output routing byte (`$10014fc+k`); **6 = main outs** (the default); DSP1 sends other values to the individual-output buffers |
| 1 | **VOL gain** = `((LEV² >> 8) · VEL >> 17) · (VOL² >> 17)`; 0 when the track is muted |
| 2 | **PAN** = `(a6 PAN << 9) & $1fffe00` (= pan value `<< 16`) |
| 3 | **REV** send = `a6 REV² >> 5` |
| 4 | **DEL** send = `a6 DEL² >> 5` |

`a6` values are the smoothed + LFO'd parameters (value `<< 7`). **LEV** is the track level, smoothed
separately by `$100029e` at the end of each tick: 48 halfwords at `$1000d7c` (16 track levels, then 32
master-FX parameters) slew towards byte targets at `$1000f5c` as `new = (3·old + target<<7) >> 2`.
**VEL** is the last trigger's velocity byte (`$100154c+k`; 0 until the track is first triggered), or
`128 + 2·accent amount` on an accented sequencer step. The tick order is: LFO oscillator → parameter
smoothing → LFO apply → level smoothing.

Checked against md-mm's DSP1 writes (level 90, velocity 77, VOL 100, PAN 30, DEL 50, REV 70):
`000006 05cc60 1e0000 264800 138800` from both. `HostModel` now computes these
(`mixerInput(track)`), with `trigger(track, velocity, accent)`, `setLevel`, `setMute`, `setRouting`.

## Mixer DSP per-track chain: translated to C++, bit-exact

DSP1's main loop (`$6f-$a3`) calls one function per track (`P:$a4-$25d`, `r6 = Y:$200+$40k`, input
`Y:$600+$20k` from the voice link, output `X:$200+$20k` for tracks routed to the main outs, or
downwards from `X:$3e0` for the others). It runs the whole effects page in this order:

| Stage | P | What it does |
|---|---|---|
| prep | `$a4-$ba` | cutoffs/resonances: `y[$22]` = FLTF·16, `y[$23]` = FLTQ (0 if FLTF = 0), `y[$24]` = min(FLTF·16 + FLTW·16, $7ff), `y[$26]` = FLTQ (0 if FLTW = 127) |
| AMD | `$bb-$d1` | `out = (1−d)·in + d·(in·sin)`, d = AMD/128, sine phase step AMF·16 through a 32768-word sine (`$148000`, built at boot by a 48-bit recursion), phase in `y[$25]` |
| EQ | `$d2-$133` | peaking biquad: coefficients from OS tables indexed by EQG and EQF, gain normalised with `clb`/`normf` and a 24-step `div`; history in `y[$12-$16]` |
| filter 1 | `$134-$19f` | 2-pole section at `y[$24]`/`y[$26]` (high-pass side), coefficients from OS tables, **ramped linearly across the block** (ramp shapes `X:$648`/`$668`), 48-bit state |
| filter 2 | `$1a0-$21e` | 2-pole section at `y[$22]`/`y[$23]` (low-pass side), same scheme |
| SRR | `$21f-$233` | sample-and-hold on a phase accumulator, period from `SRR²` (`y[$1c]`, held `y[$1d]`) |
| DIST | `$234-$25d` | `in · gain[DIST] << 9` **saturated by the limiter** (the distortion), then a first-order filter (`y[$1e-$21]`) |

`engine/TrackFx` is a translation of this function, instruction by instruction where the DSP code
is software-pipelined, with the DSP56300's arithmetic (`engine/Dsp56.h`: 56-bit accumulators, the
move limiter, fractional multiply, `dmac`, `div`, `clb`, `normf`, modulo addressing). The OS tables
(`$1402aa $141700 $141f00/80 $14221d $142c76 $143476-$1435f6 $143777 $143878`, and `X:$648-$687`) are
read from the user's OS file at load time; the sine table is recomputed with the boot code's own
arithmetic (all 32,768 words identical). Per-track state is the same 64-word block as the DSP's.

**Verification** (`tools/mdmix`: the DSP's own function in the emulator, `mdfxtest`): every stage
compared separately, then the whole chain: **3,200,000 samples and every state word identical**,
1,000 trials × 100 blocks on random tracks, parameters (with 0/127 extremes) and signals (saw, noise,
sine, silence, full-scale square), both with parameters fixed per trial and moving every block.

**Cost:** 16 tracks ≈ 2.9% of one x86 core, against ~1,830 DSP instructions per track per block
(~40 M instr/s for 16 tracks) if emulated.

## Mixer DSP mix: translated to C++, bit-exact

After the 16 tracks, the main loop runs the mix (`P:$294-$341`):

- **Tracks routed to the main outs** (route word 6): pan picks a gain pair from the boot-time sine
  table: `idx = min(PAN, $7fffff if PAN > $7ecccd) >> 10`, **L = VOL·sin[$14a000+idx]** (cosine),
  **R = VOL·sin[$148000+idx]**; then REV·L, REV·R, DEL·L, DEL·R, six gains per track at `Y:$0`. The
  code emits a two-instruction MAC pair per main track into a routine at `P:$9e2` (and patches its
  loop end and last pair), which `$9de` runs three times: sums over the main tracks per sample, `<< 3`,
  limited: **dry main L/R at `X:$180`**, **reverb send at `X:$1c0`**, **delay send at `X:$600`**
  (interleaved pairs). These feed the master FX.
- **Other routes** (0-5): `sample · VOL << 4`, added (limited) into the 6-channel output frame buffer
  (`X:$400` or `$4c0`) at channel 2, 5, 1, 4, 0, 3 for route 0-5. At the end of the block (`$971`) the
  master output is added to channels 2 and 5 (the main pair), so routes 0 and 1 go to the main outs
  without panning or effects, and routes 2-5 to the individual outputs.

`engine/Mixer` is the translation. **Verification** (`tools/mdmix`: `MixerRef::runMix` runs the DSP's
mix code on the same inputs; `mdmixtest`): **384,000 output words identical** over 1,000 random
blocks (random routing mixes, levels, pans, sends, full-scale and saturating inputs).

`engine/Engine` ties it together: `HostModel` → `VoiceEngine` (DSP2, emulated) → 16 × `TrackFx` →
`Mixer`, per 32-sample block, giving the dry main mix, the sends, the individual outputs and each
track's own post-effects signal. `tools/mdrender` plays a demo pattern through it into a WAV
(x86: 8 s of audio in ~1.3 s, almost all of it the voice DSP emulation).

Not yet translated: the master FX (`P:$344-$970`: rhythm echo, gate box, EQ, dynamix, i.e.
Machinedrum FX), and so no end-to-end comparison with md-mm's final audio yet (its output always
includes the master section).

## Force port: VoiceEngine cross-compiles and runs correctly on 32-bit ARM

Cross-compiled `VoiceEngine`, `HostModel`, `MachineRunner`, `TrackFx` and `Mixer` for armhf against
`libs/dsp56300` (the arm32 fork; `tools/arm32jit_prototype/toolchain-diff/armhf.cmake`,
`-mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard`, matching the Force). No source changes were
needed beyond one: **on a target with no JIT (`dsp56k::g_useJIT` false at compile time, i.e. not
x86-64/arm64), the DSP must be told to enable its interpreter opcode cache explicitly**
(`m_dsp->setInterpreterEnabled(true)`) — this fork only builds that cache on request, since normal
(JIT) hosts never read it. Without it, `exec()` still correctly falls back to `execInterpreter()` on
armhf, but every entry is an unresolved null instruction pointer, and the very first instruction
segfaults. `VoiceEngine::VoiceEngine()` and `MixerRef::MixerRef()` (test-only) now do this when
`!dsp56k::g_useJIT`.

**Verified, no physical device available in this session** (`qemu-arm`, cross-built, statically
linked): `mdvoice` cross-built for armhf reproduces the x86 build's output exactly for both a
playing voice (TRX-B2) and an idle voice, 200 blocks. The decisive portability check: this fork's
interpreter forced on x86 too (`-DDSP56K_NO_JIT_RUNTIME`) against the same armhf cross-build,
running the full engine (`mdrender`'s demo pattern, 8 s, 6 tracks) — **byte-identical WAV output**.
That proves the port itself (source + cross-compilation) is sound and deterministic across
architectures; it does not by itself prove the arm32 fork's interpreter matches real Machinedrum
hardware for every instruction (see below).

**Found and fixed along the way:** `op_Merge` was an unimplemented stub in this fork
(`errNotImplemented("MERGE")`); gearmulator-md-mm's separate dsp56300 fork has a real
implementation. Ported it (`libs/dsp56300`, branch `armhf-interp-merge-fix`), adapted to this
fork's left-aligned accumulator (`a1()`/`b1()` instead of that fork's `aluField24(reg, pos)`).
Not exercised by the demo kit (neither DSP1 nor DSP2's program in OS 1.63 uses `MERGE`), but a real
gap worth having fixed regardless.

**Open: one fork-level discrepancy found, not yet root-caused.** Bisecting the demo kit's 6
machines one at a time (`mdvoice`, single voice, `mdmachine`-computed coefficients) against both
dsp56300 forks: TRX-BD, TRX-CH, TRX-OH, EFM-CB and P-I-MT agree between the two forks;
**TRX-SD (id 17) does not** — output matches for the first 64 samples (2 blocks) then diverges.
Candidates checked and ruled out by inspection (implementations read equivalent, modulo each
fork's accumulator convention): `Tcc` (`op_Tcc_S1D1`/`S1D1S2D2`), `LRA` (`op_Lra_Rn` is an
identical stub in both forks, so not a source of disagreement between them; `op_Lra_xxxx` is
identical and implemented in both). Not yet found: whichever instruction TRX-SD's render routine
(`P:$100776`, section 1) uses that the two forks disagree on. Since this session only ever verified
TRX-B2's audio (not TRX-SD's) sample-for-sample against real captured hardware output (only
TRX-SD's *coefficient words* were checked, in "Voice engine prototype"), it is not yet known which
fork is correct here — worth noting, this arm32/Monomodule fork independently added its own fixes
(SR.SM saturation, MPYRI) that gearmulator-md-mm's fork lacks, so "disagrees with md-mm" does not
imply "wrong". Needs a real hardware capture of TRX-SD audio (or a traced instruction-level diff
between the two forks' interpreters on the same input) to resolve. Tracked in `HANDOFF.md`.

**Not done in this session (no physical Force access):** on-device timing. The static recompiler
(`libs/dsp56300`'s `tools/arm32jit_prototype/recomp/`, see `docs/ARM32_JIT.md`) is generic
infrastructure, not Monomodule-specific, and should apply directly to `VoiceEngine`'s DSP2 program
once discovery is re-run against it; measured ~3.8-3.9x over the plain interpreter for Monomachine
machines. DSP2's interpreted cost is already measured (this doc, "DSP2 cost per machine"); the
Force number needs the actual device.
