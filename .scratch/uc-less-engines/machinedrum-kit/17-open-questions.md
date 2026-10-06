# Open questions and errata

## Open questions

| Question | Why it matters | How to settle it |
| --- | --- | --- |
| Why area 3 costs about 5 wait states when the BCR decodes to 4 ([measured](02-memory-maps.md#measured-wait-states-hardware-dsp2)) | Exact cost models | Check the DSP56303 user manual's bus timing; time reads and writes separately |
| The 32-ROM branch with the area-2 reservation (`R = 0x190000`) | It leaves only `0x010600` units of ROM budget there; only 48-ROM has run with this layout on hardware | Boot-test that branch and load a small sample bank |
| Instruction-cache behaviour of machine code (1K words; 8 sectors of 128 words with LRU replacement, per the family manual) | A miss costs about 6 cycles in area 3 ([measured](02-memory-maps.md#measured-wait-states-hardware-dsp2)). Tight loops are nearly free after the first pass; with 16 machines per block each render probably starts mostly cold | Hardware timing of test kernels with different code footprints |
| Descriptor "format" bytes `+50..+53` and the tail `+54..+85` | Custom display formats for knob values | Vary the bytes and observe the LCD |
| When exactly the OS reruns control handlers | Handlers with expensive work; tempo-synced parameters | Breakpoint the handler during sequencer playback, tempo changes, trigs |
| Maximum packet length | Machines with more than ~10 control words | Return larger counts and read back `Y:S…` |
| The extra free IDs (30–31, 40–47, 73–79, 86–94) on hardware, and under sequencer playback, copy/paste, sound save, randomize and song mode | They passed the static audit and an emulator differential test of assignment, kits, SysEx and the browser ([catalogue](07-machine-catalogue.md#free-ids)) | Boot-test a machine on a new ID; repeat the differential test with those operations |
| Why the interlock estimate overcounts after scheduling in some kits ([pipeline interlocks](13-dsp-programming.md#pipeline-interlocks)) | Absolute hardware costs without a load-meter run | Time single kernels with and without one interlock kind on hardware; check the AGU rule for post-update addressing and moves into AGU registers |
| Raising the ESSI0 voice-link rate | 16 tracks × ~3,100 ≈ 50k of the ~68k usable cycles go to the per-track floor | Find where the firmware sets the ESSI0 clock and test a faster rate on hardware with DSP1 |
| The [E12 layout](12-packing-firmware.md#removing-e12-instead-memory-without-a-reservation) on hardware | The kit examples' layout: stock sample memory and RAM with a full delay pool, and the shifted family table; verified only in the emulator | Flash a kit example; load a full ROM bank, record with RAM-R1 and run delay machines together; compare a load-meter threshold with the area-2 layout |
| 32-ROM/2-RAM configuration with the pool reservation | Other Machinedrum variants | Boot-test that branch |
| The code-region reservation (`0x1B0000`) with RAM machines kept, on hardware | The layout for builds that keep E12; verified only in the emulator (the pool reservation `0x190000`, with RAM machines removed, has run on hardware) | Flash a reserving build (the examples before the E12 layout); record with RAM-R1..R4 and load a large sample bank |
| First-run flash programming by MainOS (`0x200140`, magic at `0x3FFE`) | Whether MainOS can rewrite the boot sector | Static analysis of that routine; it has never triggered with these images |
| ColdFire task and interrupt structure (priorities, timer ISRs) | Only needed for deeper OS changes | Trace in the emulator |
| Kit record bytes `+0x1E0…+0x45F` | Kit tools | Diff kits after targeted edits |
| Bit-exact models of the stock DSP1 effects | Offline rendering, new DSP1 effects | Instruction-level derivation, as done for the GND conversions |

## Errata to older notes

Earlier community notes on this firmware contain claims that later work
disproved. If you read older material, correct it as follows:

| Older claim | Correct |
| --- | --- |
| The CPU is an MCF5307 | MCF5206E (board model and MAME driver) |
| Blob checksum = sum of decompressed data | Sum of **compressed** bytes |
| The DSP blobs are S-record-like with tag 3 = P memory | Little-endian 24-bit word stream: preamble `3,entry,4,config`; sections tagged 0/1/2 = P/X/Y; terminator `3,entry` |
| DSP2 entry point is `P:0x22` | `P:0x24` (preamble and terminator both say so) |
| DSP2 loads synthesis engines into "overlay slots" `P:0x020F00–0x0C7FBD` from external memory through `X:0xFFFF80` | All machines are resident; no program writes occur on assignment or trig; the "slots" are zero-filled gaps in a flattened image |
| A 16-entry machine dispatch table at `P:0x101AC0` | That is instruction code read at the wrong boundary; the real tables are at `0x145AF5/0x145BB6/0x145C77`, 193 entries |
| P:/Y: tables at `0x145AF5` hold different values | One physical table seen through two aliases |
| Three dispatch stages are INIT / STEP / UPDATE-per-frame, and `Y:0x800+0x40·v` holds the machine type | Init (on type change), update (on trig), render (every block); `Y:S` is a **pending** marker cleared before update, the active type is `Y:0x153+t` |
| `P:0x103D7B` is a 20-entry TRX-UW function table | A 21-entry E12 sample descriptor table {start, length, 0} |
| The ColdFire sends MIDI over a UART to the DSPs' SCI port to play notes | MIDI UART1 is external MIDI; UART2 is the panel; the DSPs get control through HI08 |
| The audio links are ESAI / memory-mapped FIFOs | ESSI0 (DSP2↔DSP1) and ESSI1 (codec) with DMA |
| `0x1000000` scratch is shared memory between the CPU and the DSPs | ColdFire internal SRAM; the DSPs communicate through HI08/ESSI only |
| OS_A/OS_B are auxiliary operating systems | 512 KiB factory machine/kit databases |
| Custom flash data can be referenced at its file offset | Only through `0x10000000 + offset` after the CS0 remap |
| MCF registers at `0x300064/0x300068` are CS1 | They are CSAR0/CSMR0 (chip select 0, the boot flash) |
