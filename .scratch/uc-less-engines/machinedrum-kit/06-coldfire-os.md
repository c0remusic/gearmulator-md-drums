# ColdFire OS (MainOS)

MainOS is decompressed to `0x200000` and entered there. It is 404,766 bytes
(`0x200000–0x262D1D`): code first, then initialized data (tables, descriptors,
menus, strings). Globals, BSS and task stacks follow directly after it.

**MainOS cannot grow.** Appending to it would push into runtime globals and
would require moving every absolute reference. All custom additions go into
**flash banks** in the erased tail of the first flash megabyte. They are
referenced through the CS0 alias `0x10000000 + offset` (see
[memory maps](02-memory-maps.md#the-cs0-remap)). MainOS edits are
**length-preserving word replacements** only.

Disassemble MainOS as ColdFire ISA-A with VMA `0x200000`
(see [disassembly guide](15-disassembly-guide.md#2-coldfire)).

## Structure overview

| Region | Address | Notes |
| --- | --- | --- |
| Entry | `0x200000` | Copies code to internal SRAM, clears BSS, starts the scheduler (**Static**) |
| Chip-select setup | `0x2002B8…0x2002F2` | Includes the CS0 remap at `0x2002C8` |
| Sample-bank budgets | `0x200376…0x2003B6` | Two model branches (48-ROM/4-RAM and 32-ROM/2-RAM) |
| Machine control handlers | `0x201130…` | GND-SIN `0x201130`, GND-NS `0x201194`, GND-IM `0x2011B4`, others per descriptor |
| Sample loading paths | `0x20D2A8`, `0x20D7E2` (loader entries edited by custom builds) | ROM sample transfer, RAM slot partition |
| Tempo-to-samples helper | `0x20B4B4` | Master-delay TIME law (see [below](#tempo)) |
| Menu construction | `0x22C1A0…0x22C20E` | Walks the family table, builds the inverse ID→menu map, counts lists |
| Machine name/menu display | `0x230658…0x235366` | Reads family names and list pointers |
| Pitch table | `0x24BD94` | 128 × u32, GND pitch conversion |
| Decay table | `0x24BF94` | u32 coefficients, GND decay conversions |
| **Descriptor table** | `0x24EF54` | 135 × 86-byte machine descriptors |
| GND menu list | `0x251E3E` | Four descriptor pointers + 0 |
| **ID table** | `0x252092` | 192 × u32 descriptor pointers |
| **Family table** | `0x252396` | 10 × 8-byte records + zero terminator at `0x2523E6` |
| Menu state (RAM) | `0x28C2D8` | Cached item count of the selected family |
| Inverse map (RAM) | `0x28D838` | 2 bytes per ID: family index, row |
| Handler slots (RAM) | `0x29F27C + 4·t` | Per-track pointer to the assigned machine's control handler |
| Sample-bank globals (RAM) | `0x29E9F0`, `0x29F38E`, `0x29F6CA`, `0x29F6DE` | ROM budget, DSP sample base, slot count, total budget |

The machine-related tables are described in [machine catalogue
structures](07-machine-catalogue.md).

## Tasks and interrupts (Static)

The OS runs a small priority scheduler. Tasks yield with `trap #0`. Per the
older static analysis:

- a MIDI receive task (highest priority);
- three UI worker tasks;
- the main UI/event task;
- an init/idle task.

Timer 1 drives the sequencer clock and timer 2 a secondary rate. The vector
base is internal SRAM `0x1000000`. These details are not needed to add machines.
The exact priorities and addresses are in the older notes and should be treated
as hypotheses (see [open questions](17-open-questions.md)).

The OS writes control data into DSP memory through the HI08 host command
protocol. Machine packets use the DSP2 receive handler (destination address +
count, then DMA). There is no separate MIDI or SCI path from the ColdFire to the
DSPs for voices.

## Kits

A kit is a `0x460`-byte record.

| Address | Contents |
| --- | --- |
| `0x70000A` | **Current kit** (working copy) |
| `0x70046A` | Undo kit |
| `0x7008CA + 0x460·n` | Stored kits, `n = 0..63` |

Kit record layout (offsets within the record):

| Offset | Size | Contents |
| --- | --- | --- |
| `+0x000` | 10 | Kit name, NUL-terminated |
| `+0x010` | 16 × 24 | Track parameters: 8 synthesis, 8 track-effect, 8 routing bytes per track (`+0x10 + 24·t`) |
| `+0x190` | 16 | Track levels |
| `+0x1A0` | 16 × 4 | **Firmware machine ID per track**, big-endian u32 (`+0x1A0 + 4·t`) |
| `+0x1E0…+0x45F` | | Further per-track records and MIDI/trigger mapping. Not interpreted here; preserve verbatim. |

So the current kit's machine IDs are at `0x7001AA + 4·t` and its synthesis knobs
at `0x70001A + 24·t`.

Parameter bytes are 0..127. A fresh assignment loads the descriptor's
defaults. A kit saved with an **earlier version** of a custom machine keeps its
stored bytes, including zeros in knob slots that the earlier version did not
use. When a new version gives an unused knob a meaning, choose it so that the
stored value (often 0) is harmless, or document which value reproduces the old
sound, for example "set GAIN to 64 for kits saved with version 1".

**Kits referencing custom IDs are not portable to stock firmware**: stock OS 1.63
maps all the free IDs (4–15, 30–31, 40–47, 73–79, 86–94) to the empty machine.

**DSP type latching.** Reassigning a track in the kit does not immediately change
the DSP voice. The old DSP type keeps running until the next trig sets the
pending word (see [DSP2 voice ABI](08-dsp2-voice-abi.md#dispatch-and-lifetime)).

## MIDI

Channel numbering below is the Machinedrum's base channel (channel 1 = `0`).

| Message | Format |
| --- | --- |
| Trig track `t` | Note-on, notes `36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62` for tracks 0–15 |
| Synthesis knob `k` (0–7) of track `t` | CC on channel `base + t//4`, CC number `[16, 40, 72, 96][t%4] + k` |
| Track-effect and routing knobs | Following CCs in the same per-track blocks (see the Elektron manual, MIDI appendix) |
| Assign machine | `F0 00 20 3C 02 00 5B TRACK MODEL UW F7` (an optional init-scope byte may precede `F7`) |
| Request current kit number | `F0 00 20 3C 02 00 70 02 F7`, reply `F0 00 20 3C 02 00 72 02 kit F7` |
| Device inquiry | Standard universal inquiry; the reply carries the version record |

In the assignment SysEx, MODEL is the firmware machine ID for the standard
machines (`UW = 0`). UW machines (ROM and RAM, IDs 128–191) use `MODEL = ID − 128`
with `UW = 1` (for example ROM-01 = `00/01`, RAM-R1 = `20/01`, ROM-33 = `30/01`).
Custom machines (free IDs, all below 95) are assigned with `UW = 0`, `MODEL = ID`. The existing
verifiers send `F0 00 20 3C 02 00 5B t ID 00 02 F7`.

## Raw control words and smoothing

A machine's control handler does not see the 0–127 knob byte. It receives one
**unsigned 16-bit word per parameter**, approximately `value << 7` (0…16256).
The OS **slews** these words when a knob or CC changes: the handler is called
repeatedly with intermediate values over several blocks.

Consequences for handler code:

- Reduce to a knob position with rounding: `k = min(127, (raw + 64) >> 7)`.
- Endpoint detection should use a threshold. The existing machines treat
  `raw ≥ 16192` (126.5) as "maximum" and `raw < 64` as "zero", so the slew
  cannot leave a residual.
- For stepped parameters (for example a MIDI note), round to the nearest step so
  that slews move through steps instead of detuning.
- A handler is a pure function of the raw words and globals. It must not keep
  state between calls.

## Tempo

The current tempo is a u32 at ColdFire `0x100150C`, equal to **BPM × 24**. The
master delay converts note lengths with (helper at `0x20B4B4`):

```text
samples = ((U * 44100) >> 10) * 360 / tempo       # U = length in 1/128 notes, << 7
```

Custom machines that need tempo-synced times (a gate or hold length in note
values) can read `0x100150C` in their handler and use the same law. A sequencer tempo change
reaches the packet without any knob being touched: in tests the DSP packet
already carried the new length before the next trig. So the OS reruns handlers
on its own; exactly when is not mapped.

## Sample bank memory

On DSP2, ColdFire partitions sample memory into ROM slots and then RAM slots when
it builds the sample bank. There is no heap. Relevant globals and code:

| Item | Address |
| --- | --- |
| ROM slot count | `0x29EE58` |
| ROM + RAM slot count | `0x29F6CA` |
| DSP sample base | `0x29F38E` |
| Sample budget | `0x29F6DE` (halved to get word offsets) |
| Model branch constants | `0x200376…0x2003B6` (see [packing](12-packing-firmware.md#reserving-sample-memory)) |
| Slot partition code | `0x20D5A0–0x20D61C` and `0x20DAA4–0x20DB1E` |
| Metadata mirror uploaded to DSP2 `0x147E00` | ColdFire `0x29EE60` (`0x102` words, code at `0x20A9D0`) |

The partition logic, in the 48-ROM configuration (base `0x150000`, budget
`0x15F400`):

```text
slot_base      = sample_base + running_word_offset
word_stride    = floor((budget/2 - end_of_ROM_words) / RAM_slot_count)
running_offset += word_stride
record_limit   = floor((budget - 2*end_of_ROM_words) / RAM_slot_count) - 32
```

The RAM recorder does not allocate on a trig. It writes into a fixed staging
area (`0x135206 + 0x80·slot`) and then switches to the slot base published in
`0x147E00 + 4·slot` once enough has been recorded.
