# DSP1 audio path

DSP1 turns the 16 raw voice blocks into the six codec outputs. Custom machines
do not modify DSP1, but a machine designer needs to know:

- what happens to its output afterwards;
- how DSP1's own deadline depends on DSP2's timing.

All addresses are DSP1 word addresses.

## Block scheduler

```text
wait until ADC DMA0 destination (DDR0) reads exactly 0x13F or 0x17F   # P:0x3C-0x43
    0x13F -> input half X:0x100, output half X:0x400
    0x17F -> input half X:0x140, output half X:0x4C0
block start (P:0x4F)
for each of the 16 voices, in arrival order:
    wait until DMA4 (ESSI0 RX) has received 32 more words             # P:0x73-0x79
    before the last voice: copy the previous master block             # P:0x7D -> P:0x270
        X:0x180 -> X:0x688 and start its DMA back to DSP2
    run the track chain on that voice (P:0x7E-0x25D, padded to fixed length)
clear the next DAC half; route/mix; master effects; dynamics          # P:0x294-0x980
meters                                                                 # P:0x981-0x9A5
bra 0x3C                                                               # P:0x9A8
```

Three properties matter for custom work.

**The block start is an exact-match poll.** If DSP1 returns to `P:0x3C` after
DDR0 has moved past `0x13F`/`0x17F`, it waits for the **next** boundary. A whole
block is skipped: the DAC replays a stale half (192 words) and the mixer period
doubles. This is how a deadline miss shows up.

**The voice link paces DSP2.** DSP2 sends each voice block (32 words) with
DMA0 over ESSI0 and waits for the previous block's transfer before starting the
next. One transfer takes about 2,900–3,000 DSP2 cycles (emulator), so DSP2 can
deliver at most one voice per ~3,000 cycles, whatever the renders cost (see the
[voice-link slot floor](08-dsp2-voice-abi.md#the-voice-link-slot-floor)).

**DSP1's work includes waiting for DSP2.** DSP1's own processing is about 59k
cycles per block. It cannot finish until the last voice has arrived, so a late
DSP2 pass eats DSP1's slack: measured about 14.7k cycles typical, 11k at worst
with 15 heavy custom voices. DSP2 overruns therefore surface as DSP1 missing
its block start.

**The host-command ISR busy-waits.** DSP1's host ISR (`P:0x9AA`) reads a
destination, programs DMA5, then **spins inside the interrupt** until the
ColdFire sends the count word (`P:0x9AF`). It runs about 7 times per block; on
hardware the wait is a few cycles. It matters in emulation
(see [emulation and verification](16-emulation-and-verification.md#interleave-quantum)).

## Track chain

Per track `t`, state `T = Y:0x200 + 0x40·t`. The order in the code is:

| Stage | Code | Controls (DSP1 Y) | Notes |
| --- | --- | --- | --- |
| Filter parameter prep | `P:0xA4–0xBC` | base `T+4`, width `T+5`, Q `T+6` | Derives two edges and Q |
| Amplitude modulation | `P:0xBD–0xD1` | depth `T+0`, frequency `T+1` | Table oscillator over the 32,768-word sine table |
| Track EQ | `P:0xD2–0x134` | frequency `T+2`, gain `T+3` | Recursive single band, coefficients built on the DSP |
| Track filter | `P:0x135–0x21E` | from prep | Two-edge recursive filter with coefficient interpolation across the block |
| Sample-rate reduction | `P:0x21F–0x233` | `T+7` | Accumulator-driven sample and hold |
| Distortion | `P:0x234–0x25D` | `T+8` | Table-driven, stateful |
| Padding | `P:0x25E–0x26E` | | Fixed timing per track |

These controls arrive as the 7-bit knob value with about 7 extra fractional
bits (`value << 7`). The ColdFire smooths them across blocks.

## Mixing and routing

`Y:0x100 + 5·t` holds route, volume, pan, reverb send and delay send.

- **MAIN-routed tracks** are packed into processed-voice buffers growing up from
  `X:0x200`. A **generated** mixer at `P:0x9E2` sums them into three stereo
  buses: dry `X:0x180`, reverb send `X:0x1C0`, delay send `X:0x600`. The
  generator (`P:0x294–0x31B`) emits two multiply-accumulate instructions per MAIN
  track, then the tail and loop end. It rewrites the code every block. This is
  the only self-modifying code found in the firmware.
- **Mono-routed tracks** (outputs A–F) are packed downward from `X:0x3E0` and
  accumulated directly into their DAC slot. They skip pan, sends and all master
  processing.

| Route | Output | Offset in the six-word DAC frame |
| --- | --- | --- |
| 0 | A | 2 |
| 1 | B | 5 |
| 2 | C | 1 |
| 3 | D | 4 |
| 4 | E | 0 |
| 5 | F | 3 |
| 6 | MAIN | 2 and 5 |

## Master effects

| Effect | Code | Parameters | Structure |
| --- | --- | --- | --- |
| Rhythm Echo delay | `P:0x342–0x43B` | `Y:0x150–0x158`, state to `0x165` | Two external delay lines, 97,020 words apart. Modulated read, cross-feedback, two feedback filters inside the loop. |
| Gate Box reverb | `P:0x43C–0x66B` | `Y:0x185–0x18C` | Predelay ring, 5-stage allpass diffusion, 6-line feedback network, gate, HP/LP return filters |
| Return sum | `P:0x66C–0x67B` | | dry + delay return + reverb |
| Master EQ | `P:0x67C–0x87E` | `Y:0x170–0x179` | Low shelf, high shelf, parametric, output gain |
| Dynamics | `P:0x87F–0x980` | `Y:0x17A–0x184` | Stereo-linked energy detector, 16-frame look-ahead ring, parallel mix |

The completed master pair is added into DAC offsets 2 and 5 on top of any
mono A/B tracks. During the next block (before its last voice) the master
block is copied to `X:0x688` and sent back to DSP2; that feeds RAM recording.

## Links and DMA (both DSPs)

| Channel | Direction | Role |
| --- | --- | --- |
| DSP2 DMA0 | `Y:0x100/0x120` → ESSI0 TX | One voice block |
| DSP1 DMA4 | ESSI0 RX → `Y:0x600–0x7FF` | 512 voice words per pass |
| DSP1 DMA2 | `X:0x688–0x6C7` → ESSI0 TX | Master block back to DSP2 |
| DSP2 DMA2 | ESSI0 RX → `X:0x700–0x7FF` | Master return ring, pointer `X:0x243` |
| DSP1 DMA0 | ESSI1 RX → `X:0x100–0x17F` | ADC input; its position paces DSP1's blocks |
| DSP2 DMA1 | ESSI1 RX → `X:0x100–0x1FF` | ADC ring for INP and RAM machines |
| DSP1 DMA1 | `X:0x400–0x57F` → ESSI1 TX | Six-channel DAC stream, double buffered |
| DMA5 (both) | HI08 RX → host-selected Y address | ColdFire control packets |
