# Ticket 03: DSP1's master section, run alone

Research for `issues/03-section-master-dsp1.md`. Question: what it takes to run the master section of OS 1.63's mixer
DSP program (`fw::Firmware::dspB`, P:$342-$970) on an emulated DSP56300, fed by mdEngine's `Mixer` outputs, without
the rest of the program; how the Kit's 32 master bytes reach Y:$150-$18C; cost, memory, latency; effect order.

## Method and sources

Primary sources only: the firmware image `%LOCALAPPDATA%\Programs\Gearmulator-Elektron\elektron_sps1-1uw_os1.63.bin`,
read without modification, and this repository's code.

- DSP1 program: section 2 of the OS container at flash $4000, decoded with a Python port of
  `source/elektron/md/mdEngine/tools/mdfw/Firmware.cpp:169-186` (`loadFirmwareFromFlash`); all five section checksums
  match. Section 2 holds 58 records, start address P:$24.
- Disassembly: a Python port of `source/dsp56300/source/dsp56kEmu/disasm.cpp`, driven at run time by the opcode table
  in `source/dsp56300/source/dsp56kEmu/opcodeinfo.h:72-391`, so the encodings are the emulator's own. Peripheral
  addresses from `source/dsp56300/source/dsp56kEmu/peripherals.h:26-87` (DMA $FFFFD8-$FFFFF4) and `essi.h:121-156`.
- ColdFire OS: section 0 of the same container (load base $200000; internal SRAM $1000000 is the OS's copy of image
  $2622F4, `mdEngine/engine/MachineRunner.cpp:16-18`), disassembled with Capstone (M68K, 68040 mode).
- The scripts are throwaway, outside the repository. Nothing was built or executed on the DSP or 68k emulators: every
  cost below is a static count or a figure quoted from the public documents.

"Verified" below means read in the disassembly at the address given. The public documents' claims are checked one by
one in the last section but one.

## Short answer

Yes, the master section runs alone. It is straight-line code with DO loops from P:$342 to P:$970. It has no `jsr`,
no peripheral access and no interrupt dependency. It reads three stereo buses in internal X, which are exactly what
`md::engine::Mixer` produces, and parameters in internal Y. It writes the finished Main pair to Y:$000-$03F, and the
same samples to X:$180-$1BF. It needs the whole section 2 image loaded (external tables), external memory shared by P,
X and Y, and parts of the program's own boot run once: the sine table, two clears and three effect inits. The 32
Kit bytes go through the OS's level slew, which mdEngine already calls, then a 4-phase coefficient computation inlined
in the OS tick routine, then four host-port block writes to Y. mdEngine can call those four code bodies in
MachineRunner if their exits are turned into returns. Cost: about 9,740 DSP instructions per 32-sample block, constant.

## 1. Where the master section sits in DSP1's block (verified)

| P address | What it does |
|---|---|
| $3C-$43 | Waits for DMA0's destination (DDR0, X:$FFFFEE) to read $13F or $17F (ADC half boundary) |
| $44-$4F | Chooses the halves: X:$641 = ADC input half ($100/$140), X:$640 = DAC output half ($400/$4C0) |
| $51-$59 | Sets m0-m7 = $FFFFFF (linear addressing) |
| $5A-$26F | Per-block setup, then the 16 track chains, one per arriving voice block; before the last voice, `jsr $270` (P:$7D) |
| $270-$293 | Copies X:$180-$1BF (last block's master) to X:$688-$6C7 and starts DMA2 to ESSI0 TX (to DSP2); re-arms DMA4 (voice receive, 512 words to $600) |
| $294-$331 | Clears the DAC half, routes non-MAIN tracks into it, generates the MAC pairs for MAIN tracks |
| $32C-$33D | Runs the generated mixer `$9DE` three times: r1 = $180 (dry), $1C0 (REV send), $600 (DEL send) |
| $33E-$341 | m4, m0 back to linear |
| **$342-$970** | **Master section** (r7 = $150 at P:$342) |
| $971-$980 | Adds Y:$000-$03F into the DAC frame at channels 2 and 5 (X:$640 + 2, stride 3) |
| $981-$9A6 | Input meters into Y:$1BE/$1BF (the ColdFire polls them) |
| $9A8 | `bra $3C` |

Host-command and DMA interrupt handlers follow at P:$9AA-$9DD, and the generated mixer at P:$9DE-$A07. The vectors are
P:$10 (`jsr $9B6`, peek), P:$12 (`jsr $9AA`, block write), P:$18 (DMA0, the ADC), P:$1A (DMA1, the DAC stream) and
P:$22 (DMA5).

## 2. Entry, exit, inputs, outputs (verified)

**Entry** P:$342 (`move #$150,r7`); the Echo addresses all its words as `y:(r7+n)`. **Exit**: the last master
instruction is P:$96E. P:$96F-$970 sets m0 back to linear, and P:$971 starts the output add. All m registers are
linear at entry. The main loop sets them at P:$51-$59, the track chain restores m1 and m5 (P:$D0, P:$134), the mixer
sets m0, m4, m5 and m7 (P:$2A9-$2B9, $33E-$340). The master itself only sets m0-m6 locally, and leaves them linear by
P:$970.

**Inputs, per 32-frame block, interleaved L then R (word 2i = L, 2i+1 = R):**

| Buffer | Content | Written by (firmware) | mdEngine equivalent |
|---|---|---|---|
| X:$180-$1BF | dry Main mix | generated mixer, P:$32D-$331 | `Mixer::Output::main` (`engine/Mixer.h:22`) |
| X:$1C0-$1FF | REV send | P:$333-$337 | `Mixer::Output::rev` (`Mixer.h:23`) |
| X:$600-$63F | DEL send | P:$339-$33D | `Mixer::Output::del` (`Mixer.h:24`) |

The generated mixer's tail writes L (accumulator a) then R (b) per frame (P:$A04-$A05). `Mixer::process` packs
`{L, R}` the same way, from the same formula (`Mixer.cpp:65-77`). The buses can therefore be copied word for word,
masked to 24 bits, as `VoiceEngine::setSlot` does with its words (`engine/VoiceEngine.cpp:235-240`).

**Outputs:** Y:$000-$03F, interleaved L/R: the Dynamix stage writes `a,y:(r4)+ ... b,y:(r4)+` from r4 = 0
(P:$958, $96C-$96D). The same values go to X:$180-$1BF (`a,x:(r1)+`, `b,x:(r1)+` from r1 = $180, P:$956, $96D-$96E).
This is the buffer P:$270 sends to DSP2 in the next block. In the firmware, P:$979-$980 adds Y:$000-$03F to whatever
routes 0 and 1 already put on DAC channels 2 and 5, limited by the accumulator-to-memory move. MD Drums keeps every
Track on route 6 (section 9), so its Main would be the Y words directly.

**Signal path inside the section (verified):**

1. **Rhythm Echo**, P:$342-$43B. Input: DEL send, X:$600 (`r1 = $600`, P:$3C9). Feedback: Y:$154 times the delayed
   sample, plus the input (P:$3CD-$3D7). Two filters (P:$3E5-$421) and the MONO cross-feed (Y:$157, P:$3EB). Writes
   into the delay lines (P:$423-$436). Output: X:$600-$63F is overwritten with LEV (Y:$158) times the delayed signal
   (P:$3DB-$3E4; X:$5FE-$5FF are rewritten with their own values). The two delay lines are X:$10011A-$117C15 and the
   same + $17AFC (97,020 words; `n2 = n3 = $17AFC`, P:$3AE-$3B0; wrap constants $10011A and $117C15 at P:$354-$356).
   Delay time is slewed on the DSP: target Y:$150, current Y:$15A (P:$361-$370). The modulation is a triangle from the
   phase Y:$165 (P:$371-$37C); the Echo uses no table.
2. **Gate Box**, P:$43C-$66B. Input: REV send X:$1C0, plus DVOL (Y:$18B) times the Echo output X:$600 (P:$44E-$464).
   That sum goes into a 16K-word predelay ring in X at the pointer Y:$143979 (initially $134000, m2 = $3FFF). It is
   read back PRED (Y:$18C) words later into X:$1C0-$1FF (P:$469-$473), then summed to mono in Y:$000-$01F
   (P:$476-$480). Five allpass stages follow (P:$481-$49F), configured by the table at $14398D: pointers $1319C0,
   $131A00, $131A80, $131B00, $131C00, lengths $2E-$DE+1. Then six delay lines with modulo $A86, $9AE, $732, $674,
   $7FF and $3FF, read at pointers that start at $130000, $131000, $132000, $132800, $133000 and $130C00
   (Y:$1439A7-$1439AC, P:$4A1-$53E). The same six lines are written back, each with 1/6 ($155556) of the network's
   mix, at the write pointers $1439A1-$1439A6 with the modulos $1439AD-$1439B2, both read here as X (P:$599-$5B4). One tap is modulated from the boot sine table (Y:$1439B3 = $148000, m4 = $7FFF, P:$4F6-$500). Then
   the gate (Y:$18A at P:$5CE and $5DA, state at Y:$143987-$14398A), the HP and LP return filters (Y:$185 at P:$622,
   Y:$186 at P:$646), and LEV (Y:$189, `<< 4`) into Y:$002-$041 (P:$661-$66B).
3. **Return sum**, P:$66C-$67B: X:$180 := dry X:$180 + Echo X:$600 + Gate Box Y:$002+ (r4 = 2), per word.
4. **EQ**, P:$67C-$87E. Coefficients are computed on the DSP from Y:$170-$178 into Y:$18D-$191 (P:$67C-$6F7, a
   24-step `div` at P:$68C). The filters read X:$180 and X:$181 with stride 2 (P:$6F8-$72C) and work in double
   precision in L memory (X and Y pairs) below $100. Filter state is in Y:$192-$1AF. GAIN (Y:$179) is applied at P:$870-$87E on L:$04E-$06D
   (left) and L:$07E-$09D (right). Scaling mode bit 11 of SR is set at P:$7DD and cleared at P:$857.
5. **Dynamix**, P:$87F-$970. The EQ output goes into a look-ahead ring in X, 192 words (m2 = $BF, P:$881-$894),
   pointer Y:$1B0 (initially $143D00). Each frame takes four words (two per channel, double precision). The block
   writes 128 words and reads 128 words from the new pointer (P:$953-$96E), so Main comes out 16 frames late. Then the
   detector, the HP (Y:$17D, P:$8A4), attack and release (Y:$17A/$17B, P:$8D7-$8DA), the curve (Y:$17F-$184,
   P:$90E-$91C), OUTG (Y:$17C, P:$945) and MIX (Y:$17E, P:$940, $959).

So the order is: Echo on DEL; Gate Box on REV plus DVOL times the Echo; Echo and Gate Box returns added to dry Main;
EQ; Dynamix.

## 3. What it does not depend on (verified)

- **No peripherals**: no `movep` and no X:$FFFFxx access between P:$342 and P:$970. All peripheral accesses in that
  part of the program are at P:$9AA and above.
- **No subroutine calls, no interrupts**: no `jsr`, `bsr`, `rts` or `rti` in the range. The only branches are local
  (P:$365-$39C in the Echo, P:$5D1-$5F7 in the Gate Box, P:$67F/$6BB in the EQ, P:$8EC-$928 in the Dynamix). DO loops
  nest at most two deep (P:$48B/$496 and P:$5A3/$5A9), so the hardware stack only holds loop state.
- **ESSI, DMA, HI08**: none. In the firmware, parameter blocks arrive by host command P:$12. Its handler (P:$9AA-$9B5)
  moves the destination from HRX into DDR5 and configures DCR5 ($0E9AC4). It then spins on HSR bit 0 for the count
  word (into DCO5) and enables DMA5 (DCR5 = $8E9AC4). This can interrupt the master mid-block. A harness writes Y directly between blocks
  instead.
- **SR at entry**: the boot leaves SR = $000 (P:$27 `movec #$300,sr`, then P:$2D `andi #$FC,mr`). The master's only
  mode change is the scaling bit, which it restores itself (P:$7DD/$857).

## 4. Memory (verified)

| Region | Use |
|---|---|
| Y:$150-$158 | Echo parameters (host) |
| Y:$159-$165 | Echo state (DSP); Y:$166-$16F not referenced by the master |
| Y:$170-$179 | EQ parameters (host) |
| Y:$17A-$184 | Dynamix parameters (host) |
| Y:$185-$18C | Gate Box parameters (host) |
| Y:$18D-$1BD | EQ and Dynamix derived coefficients and state (DSP) |
| X:$000-$0FF, Y:$000-$0FF | Scratch (also as L), shared with the track chain in the firmware |
| X:$180-$1FF, X:$5FE-$63F | Buses, see section 2 |
| X:$1000DA-$137FFF | Delay pool, 229,158 words, cleared at boot (`do x0` with x0 = $37F26 from r0 = $1000DA, P:$143E5F-$143E68). Echo lines X:$10011A-$12F711; Gate Box lines, allpasses and predelay X:$130000-$137FFF |
| $14398D-$1439BF | Gate Box configuration and pointers (section 2 record, 51 words), read as X and Y |
| $1439C0-$143CBF | Gate Box DEC and DAMP tables (records of 384 words each; 3 words per entry, `maci #3`, P:$43E-$448) |
| $143D00-$143DBF | Dynamix look-ahead ring (cleared by `rep #$C0` at P:$143DE9) |
| Y:$148000-$14FFFF | Sine table built by the boot code (P:$100000-$100022); the Gate Box reads it |

The program stores tables as P records ($140000 and up) but reads them as X and Y (for example
`move x:(r4+5),r1` with r4 = $14398D at P:$48D, `move y:>$143979,r2` at P:$454). The emulator must share external
memory between P, X and Y. mdLib does so for both DSPs: "XY is bridged into P above g_bridgedAddr", $020000
(`mdLib/mddsp.cpp:20-26`), and VoiceEngine does the same (`engine/VoiceEngine.cpp:22-25`).

The highest address used is $14FFFF, so P and XY of $200000 words are enough. With a bridge, the emulator allocates
max(P, XY) words for P, plus two bridge-sized X and Y areas (`dsp56kEmu/memory.h:139-151`). That is 2,359,296 words,
about 9 MB, plus whatever the JIT keeps per P address (not measured). Note: VoiceEngine passes XY = $800000
(`VoiceEngine.cpp:25`), so its P allocation is 8M words despite `kSizeP = 0x200000`.

## 5. Initialisation needed (verified)

From the program's own boot (`jsr $100000` at P:$2B). Only these parts concern the master:

1. P:$100000-$100022: the sine table at Y:$148000 (a `do #$7FFE` recursion). mdEngine already recomputes the same
   table for `TrackFx` (`engine/TrackFx.h:26-29`). Running the DSP code is simpler for a DSP-side harness.
2. P:$100023-$100031: all m registers linear, X:$000-$63F cleared.
3. P:$100058-$10005C: Y:$150-$1BD cleared (`do #$6E`).
4. P:$10005D-$100064: r7 = $150, then three external routines:
   - `$143E4A`, Echo: Y:$150 = Y:$15A = $9C40, Y:$151 = Y:$15B = $4001, the other Echo words 0; then the delay pool
     clear above (about 460k instructions, once).
   - `$143DE4`, Dynamix: ring clear; Y:$1B0 = $143D00; Y:$17A = Y:$17B = $20C5; Y:$17D, $17E, $17F, $180 = $7FFFFF;
     Y:$181 = Y:$183 = 0; Y:$182 = Y:$184 = $7FFFF8; Y:$1B7-$1BD = $266666; Y:$170-$175 and Y:$18D-$191 = 0.
   - `$143DC0`, Gate Box: Y:$185 = $347, Y:$186 = $7FDF3B, Y:$187 = $32, Y:$188 = 0, Y:$189 = 0, Y:$18A = $F4240,
     Y:$18C = 0, Y:$143979 = $134000, Y:$14397A = $347, Y:$143989 = 0.

The rest of the boot (P:$100031-$100057 track state, P:$100065-$1000D9 HI08, ESSI, DMA, interrupt priorities, codec
probe) is not needed. A harness can patch P:$100031 to jump to $100058, and P:$100065 to jump to its own wait stub.

## 6. How the 32 Kit bytes become Y:$150-$18C (verified on the OS image)

The chain, in the OS's own code:

1. **Kit to targets.** $209FE2-$20A03C copies the current Kit's bytes +$420-$43F (at $70042A-$700449; the current
   Kit record is at $70000A) into the slew targets $1000F6C-$1000F8B, eight longs. The code that follows copies the
   LFO settings ($7001EA to $1000F8C). Only the targets are written, so a Kit change glides.
2. **Slew.** `$100029E` slews 48 halfwords at $1000D7C toward the byte targets at $1000F5C:
   `new = (3·old + target << 7) >> 2`, masked to $3FFF, two values per long, 24 iterations ($10002A6-$10002D4 of
   the SRAM copy). Values 0-15 are the track levels, 16-47 the 32 master bytes. mdEngine already calls this routine
   every tick (`engine/HostModel.cpp:26-28` for the addresses, `:258` for the call), but nothing pokes the master
   targets yet, so they stay 0.
3. **Coefficients: one effect per tick, inline in the tick routine `$20AD9A`.** The tick increments a counter at
   $265404 ($20ADB0). After the level slew (call at $20B49C), `counter & 3` picks one effect ($20B4A2-$20B4AA,
   $20B5FC, $20B746, $20B938). That effect's DSP words go into a staging area in SRAM, and a pending flag is set:

   | Phase | Body | Reads | Staging | Flag |
   |---|---|---|---|---|
   | 0 Echo | $20B4AE-$20B5F2 | $1000DAC-$1000DBA | $1001AF4, 9 longs | $29F448 |
   | 1 Dynamix | $20B604-$20B73C | $1000DCC-$1000DDA | $1001B18, 11 longs | $29EE50 |
   | 2 EQ | $20B74E-$20B92E | $1000DBC-$1000DCA | $1001B44, 10 longs | $29F39A |
   | 3 Gate Box | $20B940-$20BA46 | $1000D9C-$1000DAA | $1001B74, 8 longs | $29EA40 |

   Phases 0-2 end with `bra.w $20BA4C`, the tick's epilogue ($20B5F8, $20B742, $20B934). Phase 3 falls through to
   it. The bodies use only d0-d6, a0 and a1 with absolute addresses: no stack frame, no a6 or a7 use, no call. They
   read the tempo at $100150C (Echo) and OS tables at $24BB90, $24BD94, $24DA14, $24DE14, $24E014, $24E214, $24E414,
   $24E614, $24E814, $24EA14, $24EC14 and $24EE14.
4. **Send.** `$20AA66`, called from the DSP2-group interrupt handler when DSP2 reports voice group 3
   ($10004A6-$10004AC), sends each pending block with `$100074C(dest, count, src)` and clears its flag:
   (Y:$150, 9, $1001AF4), (Y:$17A, 11, $1001B18), (Y:$170, 10, $1001B44), (Y:$185, 8, $1001B74) ($20AA70-$20AB0C).
   `$100074C` writes the destination to the DSP1 HI08 data register ($500004), then host command $89 (vector P:$12),
   then count - 1, then the longs ($100074C-$1000780). That is DSP1's block-write handler at P:$9AA.

The 32 bytes are in the same order as the Kit dump, Gate Box first (`mdLib/mdsysexautomation.cpp:240-249`). Byte k is
slewed into halfword $1000D9C + 2k. Names from `mdLib/mdmachines.cpp:188-191`; v is the slewed value, 0 to $3FFF.

| Y | From | Formula (OS) |
|---|---|---|
| $150, $151 | Echo TIME (k = 8), tempo | n = ((v·44100 [+$562200 above v = $3F3D]) >> 10)·360 / tempo. If n ≤ $17ADB: $150 = $17ADC - n, $151 = $4001; else $150 = $20, $151 = $5EBF0000 / n ($20B4B4-$20B530) |
| $152 | MOD (9) | (v·2) & $1FFFE |
| $153 | MFRQ (10) | (v << 4) + $200 |
| $154 | FB (11) | (v << 9) & $1FFFE00 |
| $155 | FLTF (12) | $7FFFFF - T$24DA14[v >> 6] |
| $156 | FLTF + FLTW (12, 13) | $7FFFFF - T$24DA14[min(255, FLTF >> 6 + FLTW >> 6)] |
| $157 | MONO (14) | ($3FFF - v) << 9 |
| $158 | LEV (15) | (v << 8) & $FFFF00 |
| $170-$172 | EQ LF, LG (16, 17) | low shelf from T$24E614[LF >> 7], T$24E814[LG >> 7] ($20B754-$20B7EE) |
| $173-$175 | HF, HG (18, 19) | high shelf from T$24E414, T$24E814 ($20B7F4-$20B8AC) |
| $176 | PQ (22) | T$24EC14[v >> 7] |
| $177 | PF (20) | T$24EA14[v >> 7] |
| $178 | PG (21) | ±T$24EE14, bipolar around $2000 ($20B8E6-$20B916) |
| $179 | GAIN (23) | v² >> 5 |
| $17A | Dynamix ATCK (24) | T$24E014[v >> 7] |
| $17B | REL (25) | T$24E214[v >> 7] |
| $17C | OUTG (30) | T$24DE14[v >> 7] |
| $17D | HP (29) | T$24DA14[255 - (v >> 6)] |
| $17E | MIX (31) | (v << 9) & $1FFFE00 |
| $17F-$184 | TRHD, RTIO, KNEE (26-28) | six curve words from T$24DE14[TRHD high byte], (RTIO² >> 21) + 1, (KNEE >> 7) + 42 ($20B68A-$20B734) |
| $185 | Gate Box HP (4) | $7FFFFF - T$24DA14[v >> 6] |
| $186 | LP (5) | $7FFFFF - T$24DA14[v >> 6] |
| $187 | DEC (2) | high byte of the halfword (0-63), index into the DSP table at $1439C0 |
| $188 | DAMP (3) | high byte, index into $143B40 |
| $189 | LEV (7) | T$24BB90[v >> 7] << 7 |
| $18A | GATE (6) | $7FFFFF above $3F70, else from T$24BD94 ($20B9B8-$20BA16) |
| $18B | DVOL (0) | (v << 9) & $1FFFE00 |
| $18C | PRED (1) | v & $FFFE (words in the predelay ring) |

Cross-check on the DSP side: Y:$18B multiplies the Echo output into the Gate Box input (P:$44E), and Y:$18C is the
predelay offset (P:$469). Y:$187 and Y:$188 index $1439C0 and $143B40 by 3 (P:$43E-$448).

**Can mdEngine call it the way it calls the machine, LFO and slew routines?** The slew, yes, as it already does. The
coefficient step is not a separate routine: it is four bodies inside `$20AD9A`, which mdEngine does not call (HostModel
reimplements the tick around the OS's routines, `engine/HostModel.h:1-4`). `MachineRunner::call` pushes a return
sentinel and runs until the PC reaches it (`MachineRunner.cpp:140-161`). The OS image lives in MachineRunner's own
writable RAM (`MachineRunner.cpp:16, 67-77`). So two options work without translating the 68k code:

- Patch MachineRunner's copy: `rts` ($4E75) at $20B5F8, $20B742, $20B934 and $20BA4C (`poke16`). Then call
  $20B4AE, $20B604, $20B74E or $20B940, one per tick by `tick & 3` as the OS does, after `kLevelSmooth`. None of the
  routines mdEngine calls passes through those addresses.
- Or give MachineRunner a call that stops at a chosen PC ($20BA4C).

Then read the staged longs with `peek32`, mask them to 24 bits, and write them into the master DSP's Y at $150, $17A,
$170 or $185. That replaces `$20AA66` and `$100074C`.

Also needed: poke the 32 targets at $1000F6C + k, like `setLevel` does for $1000F5C + t (`HostModel.cpp:83-86`). To
start without a glide, poke the slewed halfwords too, as the constructor does for levels (`HostModel.cpp:41-42`). The
Echo's TIME already follows `setTempo` (`HostModel.cpp:66-69`). OS 1.63's boot Kit 1 values are listed in
`mdDrumsPlugin/Design/HANDOFF.md:151-156`.

The alternative, a C++ translation of the four bodies (about 250 68k instructions and the 12 tables), is also small.
It departs from the repository's pattern of running the OS's own code.

## 7. Cost

- **Static count (this research):** P:$342-$970 executes 9,730 to 9,919 DSP instructions per 32-frame block. The
  count expands every DO and REP loop and takes the shortest and longest paths through the data-dependent branches:

  | Effect | Instructions per block |
  |---|---|
  | Echo | 1,520-1,526 |
  | Gate Box | 3,647-3,670 |
  | Return sum | 199 |
  | EQ | 2,615 |
  | Dynamix | 1,749-1,909 |

  At 44.1 kHz that is 13.4 to 13.7 M instructions/s. It is almost independent of the input, and it does not skip
  silence: delay and reverb state spans about 230k words.
- **Measured by Machinemodule** (not re-measured here): "~9,740 DSP instructions per 32-sample block = ~13.4 M
  instr/s", "input-independent so far" (`.scratch/uc-less-engines/machinemodule/HANDOFF.md:1429-1431`). This agrees
  with the static count.
- **For scale, same emulator:** one sounding voice on DSP2 costs about 1,830-3,600 DSP instructions per block
  (`HANDOFF.md:1244`, `:1288`). The master costs about as much as 3 to 5 voices sounding at all times. md-mm's whole
  DSP1 is "~79 M instructions/s, always" (`machinemodule/docs_PROTOCOL.md:123`), so the master is about 17 % of it.
  Host CPU time per DSP instruction on the reference PC has not been measured; ticket 09 should measure it with the
  harness.
- The ColdFire side adds one body of about 60-120 68k instructions per tick, which is negligible.

## 8. Latency

- **Scheduling:** none, if the master DSP runs in `EngineT::render` right after `m_mixer.process`
  (`engine/MdEngine.h:105-129`). It consumes the block's buses and produces the same block's Main.
  `mdDrums::Engine::render` already works one 32-sample block at a time (`mdDrums/mdDrumsEngine.cpp:154-187`).
  Running it one block behind on a worker would add 32 samples (0.73 ms).
- **Built into the effect:** Dynamix delays all of Main by 16 frames, 0.36 ms (section 2, step 5), whatever its
  settings. In the firmware, outputs A-F and routes 0-1 skip it. In MD Drums, the Outs (`Mixer::solo`,
  `mdDrumsEngine.cpp:182`) would lead Main by 16 samples, as on the hardware.
- **Parameter updates:** each effect's words change every 4th tick, about 31 Hz at mdEngine's default of 11 blocks
  per tick (`HostModel.h:80-82`). On the hardware they land mid-block, through interrupts.

## 9. Tracks on their Out

In the firmware, a Track whose route is not 6 takes the individual-output path (P:$2C2-$2DD: sample × VOL << 4 added
into the DAC frame). It skips the gain table and the MAC-pair generation (P:$2DE-$300), so it feeds none of the three
buses: no Main, no REV, no DEL. A Track on an individual output does not feed its Sends on the Machinedrum.

MD Drums does otherwise, on purpose: `Engine::setSeparateOutputs` sets `dryMute` (`mdDrumsEngine.cpp:141-144`). The
Track stays on route 6 and keeps its REV and DEL gains, and only its dry gains are zeroed (`engine/Mixer.cpp:60-61`).
Its Out is computed separately with the individual-output formula (`Mixer.cpp:12-15`, `mdDrumsEngine.cpp:182`). This
matches `CONTEXT.md` ("a Track on its Out leaves Main but still feeds its Sends"). The master section only reads the
three buses, so this choice needs nothing from it.

## 10. What a harness needs (for ticket 09)

On the VoiceEngine pattern (`engine/VoiceEngine.cpp:50-111, 192-198`):

1. A dsp56300 instance with Peripherals56303 for X, external memory bridged at $020000, P = XY = $200000.
2. Load every record of `fw::Firmware::dspB` (`tools/mdfw/Firmware.h:60-63`), writing P and external words through
   `memWriteP`.
3. Run the boot pieces of section 5 once.
4. Patch P:$971 to jump to a stub that signals the host and waits for a go word, like `kEndOfBlock`
   (`VoiceEngine.cpp:192-198`). The stub then jumps to P:$342.
5. Per block: write X:$180-$1BF, X:$1C0-$1FF and X:$600-$63F from `Mixer::Output`, plus any pending parameter block;
   send go; read Y:$000-$03F.

No ESSI callbacks, DMA or HI08 data are needed beyond the go and done words. Machinemodule's `MixerRef::runMaster`
(`tools/mdmix`, `HANDOFF.md:1357, 1435-1438`) is not in this repository; only `tools/mdfw` was imported
(`mdEngine/README.md:9-12`).

## 11. Public documentation, checked against the firmware

| Claim | Source | Status |
|---|---|---|
| Echo P:$342-$43B, Y:$150-$158, state to $165 | `machinedrum-kit/09-dsp1-audio-path.md:98` | Verified (r7 = $150 at P:$342; highest offset r7+$15 = $165) |
| Two external delay lines, 97,020 words apart | same, :98 | Verified: `n2 = $17AFC` (P:$3AE), lines in X:$10011A-$12F711 |
| Gate Box P:$43C-$66B, Y:$185-$18C; return sum P:$66C-$67B | same, :99-100 | Verified; also uses Y:$143979-$1439BF, X:$130000-$137FFF and the sine table |
| EQ P:$67C-$87E, Y:$170-$179 | same, :101 | Verified; derived coefficients in Y:$18D-$191, state Y:$192-$1AF |
| Dynamics P:$87F-$980, Y:$17A-$184, 16-frame look-ahead | same, :102 | Verified, but the effect ends at P:$970; P:$971-$980 is the add into the DAC frame |
| Master added at DAC offsets 2 and 5; copied to X:$688 next block for DSP2 | same, :104-106 | Verified (P:$971-$980, P:$270-$281) |
| Non-MAIN tracks skip pan, sends and master | same, :80-82 | Verified (P:$2C2-$2DD) |
| Master section P:$344-$970 | `machinemodule/docs_PROTOCOL.md:412` | Entry is P:$342, which sets r7 |
| Y:$150-$158 and $170-$18C change every tick, not decoded | same, :110, :328 | Decoded (section 6): one effect per tick, in turn |
| "delay lines in external memory at $1439xx" | `machinemodule/HANDOFF.md:1430` | Inexact: $1439xx holds the Gate Box state, pointers and tables; the lines are in X:$10011A-$137FFF |
| ~9,740 DSP instructions per block | same, :1430-1431 | Consistent with the static count, 9,730-9,919 |
| Params from the 32 Kit bytes at $1000D7C + 16 | same, :1436 | Verified: halfwords $1000D9C-$1000DDB (16 halfwords after $1000D7C) |
| Master-delay TIME law `((U·44100) >> 10)·360 / tempo` | `machinedrum-kit/06-coldfire-os.md:140-144` | Verified at $20B4B4-$20B4F6, plus the $562200 term above $3F3D |
| Mixer block period about 73,728 DSP cycles | `machinedrum-kit/16-emulation-and-verification.md:121` | Consistent with 2,304 cycles per sample (`mdLib/mddsp.cpp:50-54`) |

## 12. Not verified, risks

- Nothing was executed, so bit-exactness of a master-only run is not proven. In the firmware, the track chain also
  uses X/Y:$000-$0FF between two master sections. The reads checked (Y:$01F and Y:$03F at P:$3CF, $3F1, $400) are
  written earlier in the same block. A full read-before-write proof of the scratch was not done. A comparison with the
  full firmware, as `mdEngineFirmwareTest` does for the Tracks, should settle it.
- Parameter timing differs: the firmware's host-command writes can land mid-block, the harness's between blocks. A
  fidelity test should hold parameters fixed.
- The meaning of some derived words (Dynamix curve Y:$17F-$184, EQ shelf words) was traced to their formulas, not
  modelled.
- Host CPU per block and memory under the JIT: not measured.
