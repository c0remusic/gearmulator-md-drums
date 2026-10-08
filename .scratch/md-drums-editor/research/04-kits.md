# Factory Kits and the bytes a Kit dump writer must fill

Research for ticket 04 (`issues/04-kits-usine-dump.md`). Repository paths are relative to the root; `file:line` points
at the code on 2026-10-08 (`main` at 21fefc70). Nothing was built and no firmware executable ran. Firmware facts come
from the user's flash image `elektron_sps1-1uw_os1.63.bin` (8 MiB, SHA-1 a872a2f3527063673d6ea6d3080c4c62ef0cadc1, the hash
`.scratch/uc-less-engines/machinemodule/docs_FIRMWARE.md:15` gives), read with Python scripts that port
`md::fw::aplibDepack` and `md::fw::parseContainer` (`source/elektron/md/mdEngine/tools/mdfw/Firmware.cpp:56-128`) and
disassemble with Capstone (M68K mode; ColdFire ISA-A decodes the same for this code).

Address notation:

- **flash $x**: offset in the 8 MiB image. The bootloader runs from flash $0-$3fff with patch memory at $100000
  (MAME's notes, `.scratch/uc-less-engines/mame/elektronmono.cpp:50-52`).
- **OS $x**: the ColdFire OS, container section 0 depacked and run at $200000
  (`.scratch/uc-less-engines/machinedrum-kit/06-coldfire-os.md:3-5`). The OS sees patch memory at $700000
  (`mame/elektronmono.cpp:54-56`); the emulator maps it at both ($100000 and $700000,
  `source/elektron/md/mdLib/mdmemorymap.h:22-36`).
- **S3 +$x / S4 +$x**: offset in container section 3 or 4 once depacked (512 KiB each).

## In short

- **Kit 1's boot values come from the OS file itself**, not from a separate flash area: container section 4 (flash
  $0d761d, 73,915 packed bytes, 524,288 unpacked) is a complete factory image of the first 512 KiB of patch memory.
  When patch memory lacks the marker `DAVE` at +$7fff8, or when FACTORY RESET is chosen, the bootloader depacks
  section 4 (UW) or section 3 (non-UW) into patch memory and writes `AnDY`; the OS then turns `AnDY` into `DAVE`.
  The working Kit (patch +$0a) in that image is TRX UW, byte for byte the stored Kit 1 (patch +$8ca). The master
  effects the ticket quotes (echo 16 0 32 27 0 44 0 71, reverb 0 0 68 50 1 82 71 109, EQ 64×7 127, dynamix 127×6 0 0)
  are S4 +$cea..+$d09 and +$42a..+$449.
- **Factory Kits: 16 per image, 2 images.** S4 (UW): TRX UW, EFM UW, E12 UW, P-I UW, USERWAVES, HITHOT, CLUBBING,
  GOLDCHAINS, BEATDOWN, DARKER, DONTGOA, KONTAKTE, ROMANCE, FOUROHFOUR, RESAMPLING, SEACLONES. Slots 17-64 are empty
  (first name byte $ff). S3 (non-UW) holds 16 others (TRX, EFM, E12, P-I, FRANSISCO, ...). A Kit is a $460-byte record
  at +$8ca + $460·n, the same fields as the dump without its 7-bit packing.
- **OS 1.63 sends** `F0 00 20 3C 02 00 52 04 01 <slot>` and **accepts** a Kit dump whose version byte is 1 to 4, whose
  revision byte is anything, whose length field is the message size minus 10 and whose checksum is the low 14 bits of
  the sum from the slot byte on. A writer must send version **04**: version 01 (what the test helper `makeDump` writes,
  `source/elektron/md/mdJucePlugin/mdAutomationTestSupport.h:298`) makes the OS read the older layout, and version 64
  (what MCL writes today) is refused.
- **Bytes `parseKitDump` ignores, safe values:** LFO block bytes 5-35 all zero except the big-endian word $0000029a at
  bytes 28-31 (what the OS writes into every LFO it initialises and what all 32 factory Kits hold); the 37 bytes at
  $4a7 are 16 trig groups then 16 mute groups, 7-bit packed, $ff (OFF) or a track 0-15; the machine words' upper three
  bytes 0; the name NUL-terminated and zero-padded, its first byte never $7f (that marks the slot empty).
- **Bank file:** 64 Kit dumps concatenated, slot bytes 0 to 63, each 1233 bytes (78,912 bytes). A Machinedrum given
  such a file stores each dump into the slot it names (or into consecutive slots from a chosen one on its SysEx receive
  page), without touching the Kit being played; Machinemodule reads a multi-Kit `.syx` as one bank.

## Where Kit 1's boot values come from

### The bootloader's factory reset

The OS container at flash $4000 holds five aPLib sections; the depacker gives (offsets match
`machinemodule/docs_FIRMWARE.md:19-25`):

| Section | Flash | Packed | Unpacked | Contents |
|---|---|---|---|---|
| 0 | $004000 | 164,328 | 404,766 | ColdFire OS |
| 1 | $02c1f0 | 587,978 | 750,369 | DSP program A (DSP2) |
| 2 | $0bbac2 | 39,310 | 56,469 | DSP program B (DSP1) |
| 3 | $0c545c | 74,169 | 524,288 | factory patch memory, non-UW |
| 4 | $0d761d | 73,915 | 524,288 | factory patch memory, UW |

The version tag `163 ` sits between sections 2 and 3. machinedrum-kit already calls the two 512 KiB images "factory
machine/kit databases" (`machinedrum-kit/17-open-questions.md:43`); Machinemodule left them unidentified
(`docs_FIRMWARE.md:55`) and extracts its 16 factory Kits by booting the emulated MD and requesting them instead
(`machinemodule/HANDOFF.md:1202-1204`).

The bootloader's reset path (flash $1fc0-$2034, disassembled):

1. If the boot menu choice is not 4 ("3..FACTORY RESET" in the menu strings at flash $2636-$26a0) and the long at
   patch $17fff8 (patch +$7fff8) is `DAVE` ($44415645), nothing is reset (flash $1fc8-$1fd4).
2. Otherwise it walks the container from the size word at flash $4000 to the header of section 3 and, when the word
   DSP2 returned after its boot test is zero, one section further to section 4 (flash $1fda-$1ff6; the word is read
   from DSP2's HI08 receive register, `movea.l $600004, a3` at flash $1e6c). It depacks that section to patch $100000
   (flash $1ff8-$2000, depacker at flash $1c4) and writes `AnDY` at patch +$7fff8 (flash $2006-$200c).
3. EMPTY RESET (choice 2) writes `ANDY` instead (flash $2022-$2032).

Which DSP2 answer means UW is inferred, not traced: section 4 is the image whose Kits use ROM machines (Kit 1 tracks
13-16 are ROM-14, ROM-14, ROM-06, ROM-29) and contain USERWAVES, and the emulated UW reports factory Kit 1 as TRX UW
with section 4's master effects (`.scratch/md-editor/README.md:75`, `:115`).

Both images end with `DAVE` and the long 8 at +$7fff8 and +$7fffc. The OS, at boot (OS $200478-$200586), turns
`AnDY` into `DAVE` and, when the flag at $262401 is set (probably the 1 MiB patch memory), clears 128 blocks of $90c
bytes at patch +$80000 (OS $2004b4-$200514), the half the image does not cover. Any other marker (empty or `ANDY`)
makes the OS initialise every Kit, pattern and song as empty (OS $200524-$200582, Kits by OS $23dc0a) and write `DAVE`
and the long 6 at +$7fffc.

In the emulator patch memory starts zeroed (`source/elektron/md/mdLib/mdmc.cpp:87`), so its first boot takes the
factory path; a saved state carries patch memory instead (`mddevice.cpp:273`, `mdmc.cpp:155`).

### The working Kit after a factory reset

Patch memory, as the OS addresses it at $700000 (`machinedrum-kit/06-coldfire-os.md:67-71`, confirmed by the OS code
below):

| Patch offset | Contents |
|---|---|
| +$08 | number of the current Kit (0-63) |
| +$09 | number of the undo Kit |
| +$0a | working Kit, $460 bytes |
| +$46a | undo Kit, $460 bytes |
| +$8ca + $460·n | stored Kit n, n = 0..63 |

LOAD KIT (OS $209e52) copies the working Kit to the undo Kit and +$08 to +$09, then stored Kit n to the working Kit
and n to +$08; with -1 it copies undo back (UNDO); a loaded Kit whose first name byte is $ff gets the name "NEW KIT"
(OS $209ef8-$209f12). It then copies each 36-byte LFO block of the working Kit into the running LFO at internal SRAM
$1000f8c + $24·k (OS $20a042-$20a05c).

In S4: +$08 = 0 (Kit 1 current), +$09 = 15 (undo = SEACLONES, which is what +$46a holds), and +$0a..+$469 equals
+$8ca..+$d29 on all $460 bytes. A factory-fresh UW therefore plays Kit 1, TRX UW:

| Track | Machine | Level | | Track | Machine | Level |
|---|---|---|---|---|---|---|
| 1 | TRX-B2 (28) | 112 | | 9 | TRX-CH (22) | 124 |
| 2 | TRX-SD (17) | 110 | | 10 | TRX-OH (23) | 115 |
| 3 | TRX-XC (27) | 113 | | 11 | TRX-CY (24) | 117 |
| 4 | TRX-XT (18) | 102 | | 12 | TRX-CY (24) | 122 |
| 5 | TRX-XT (18) | 113 | | 13 | ROM-14 (141) | 120 |
| 6 | TRX-CP (19) | 115 | | 14 | ROM-14 (141) | 120 |
| 7 | TRX-RS (20) | 127 | | 15 | ROM-06 (133) | 127 |
| 8 | TRX-CB (21) | 126 | | 16 | ROM-29 (156) | 120 |

Machine names from `source/elektron/md/mdLib/mdmachines.cpp:52-85`. Master effects (S4 +$cea): reverb 0 0 68 50 1 82
71 109, echo 16 0 32 27 0 44 0 71, EQ 64 64 64 64 64 64 64 127, dynamix 127 127 127 127 127 127 0 0. Trig groups all
OFF; mute group of track 9 (TRX-CH) = track 10 (TRX-OH), the others OFF. The 24 parameters per track are at S4
+$8da + 24·t (read them from the image; they are not reproduced here).

MD Drums today starts from values of its own (TRX machines by track order, `Engine::defaultMachine`, and
`paramDefaults`, `source/elektron/md/mdDrums/mdDrumsEngine.cpp:53-66`, `:95-101`), not from Kit 1.
`md::fw::loadFirmwareFromFlash` already depacks sections 3 and 4 with the rest and keeps only 0-2
(`Firmware.cpp:169-187`); its comment "the sample and Kit data from $100000 on is not part of it" (`Firmware.cpp:171`)
is half wrong: the samples are there, the Kits are in the container.

## Factory Kits in the image

S4, slots 1-16 (name, then the record offset in S4):

| Slot | Name | S4 offset | | Slot | Name | S4 offset |
|---|---|---|---|---|---|---|
| 1 | TRX UW | +$08ca | | 9 | BEATDOWN | +$2bca |
| 2 | EFM UW | +$0d2a | | 10 | DARKER | +$302a |
| 3 | E12 UW | +$118a | | 11 | DONTGOA | +$348a |
| 4 | P-I UW | +$15ea | | 12 | KONTAKTE | +$38ea |
| 5 | USERWAVES | +$1a4a | | 13 | ROMANCE | +$3d4a |
| 6 | HITHOT | +$1eaa | | 14 | FOUROHFOUR | +$41aa |
| 7 | CLUBBING | +$230a | | 15 | RESAMPLING | +$460a |
| 8 | GOLDCHAINS | +$276a | | 16 | SEACLONES | +$4a6a |

S3, slots 1-16: TRX, EFM, E12, P-I, FRANSISCO, BIPBOP, WIGGLY, BENTY, TOAST, BOOGIE, SWARM, MAYHEM, HAUNTED, ALIENS,
BRONCO, FUGLESANG (same offsets). Machinemodule's 16 extracted Kits run "TRX UW .. SEACLONES"
(`machinemodule/HANDOFF.md:1204`), the same set.

Slots 17-64 of both images are empty: first name byte $ff, the rest stale (old names such as "MX KIT 1", "PR KIT 4",
parameters left over). The OS sends an empty slot with name byte 0 = $7f (it masks every name byte with $7f,
OS $2089f6-$208a0a), which is Machinemodule's "$7F first = empty slot" (`machinemodule/HANDOFF.md:1199`).

Names: the field is 16 bytes, but every factory name is at most 10 characters followed by a NUL, and the bytes after
the NUL are often stale ("TRX UW\0BET\0" then $7f $17 $18 $7f $19). machinedrum-kit gives the name 10 bytes
(`06-coldfire-os.md:77`). `parseKitDump` stops at the NUL (`mdsysexautomation.cpp:1205-1213`); the comment "up to 16
characters on the Machinedrum" (`mdsysexautomation.h:82-83`) is not supported by anything found here. The naming page's
limit was not traced.

Across all 32 factory Kits: every machine word's upper three bytes are 0, every LFO block's bytes 5-35 are 0 except
bytes 30-31 = $02 $9a, and every trig group is OFF.

## Kit record and Kit dump

The record is the dump's payload unpacked; the OS's sender (OS $20898e, run for a Kit request by OS $208ce4) builds
the dump from stored Kit n like this:

| Record | Bytes | Dump offset | Encoding | Contents |
|---|---|---|---|---|
| - | 10 | $000 | | `F0 00 20 3C 02 00 52`, version $04, revision $01, slot (OS $2089a4-$2089d0; template at OS $2462b2) |
| +$000 | 16 | $00a | raw, each & $7f | name |
| +$010 | 384 | $01a | raw, & $7f | 16 tracks × 24 parameters (synthesis, effects, routing) |
| +$190 | 16 | $19a | raw, & $7f | levels |
| +$1a0 | 64 | $1aa (74) | 7-bit | machine per track, big-endian u32, id in the low byte |
| +$1e0 | 576 | $1f4 (659) | 7-bit | 16 LFO blocks of 36 bytes |
| +$420 | 32 | $487 | raw, & $7f | master effects: reverb, echo, EQ, dynamix, 8 each |
| +$440 | 32 | $4a7 (37) | 7-bit | trig group per track (16), then mute group per track (16) |
| - | 5 | $4cc | | checksum (2), length (2), F7 |

The 7-bit encoder (OS $206cb0) puts each group of up to 7 bytes after a byte of their top bits, the first byte's at
bit 6: the same as `append7Bit` (`mdsysexautomation.cpp:146-163`). The checksum starts at the slot byte (`move.l d3,d4`
at OS $2089ee) and keeps 14 bits; the length is the size minus 10. Rebuilding S4's Kit 1 this way gives 1233 bytes
($4d1, `g_mdKitSize`, `mdsysexautomation.cpp:242`) with the master effects at $487 and the groups ending at $4cc, as
`parseKitDump` expects (`mdsysexautomation.cpp:1224-1308`). A Kit request for a slot above 63 sends nothing
(OS $20899a-$20899e): OS 1.63 has no request for the working Kit.

The LFO block: bytes 0-4 are destination track, destination parameter, shape 1, shape 2, update (MCL's `MDLFO`,
`MDMessages.h:205-225` in jmamma/MCL at cdf6e081, 2026-07-18; Machinemodule, `docs_PROTOCOL.md:270-276`). Bytes 5-35
are the running LFO's state: LOAD KIT copies the whole block into the running LFO (above), byte 5 is its trigger flag
and +$20 its phase (`docs_PROTOCOL.md:271-272`). Every block the OS initialises gets 0 there except the long
$0000029a at +$1c (OS $207a62-$207a68 for a version-1 dump, OS $23dcc2-$23dcc8 for a new Kit). MCL calls those bytes
"the internal state of the LFO, must not all be 0" and sets an "LFSR magic" when they are (`MDMessages.h:224-240`);
it writes $9a $02 at bytes 30-31, byte-swapped against the OS's $02 $9a.

The groups: MCL's `MDKit` holds `trigGroups[16]` then `muteGroups[16]`, "255: OFF" (`MDMessages.h:400-403`), and
packs them as one 32-byte 7-bit run (`MDMessages.cpp:601-606` at d5714732, 2026-07-19). The OS writes $ff into all 32
for a new Kit (OS $23dd24-$23dd3c).

## What OS 1.63 accepts

Kit dumps ($52) dispatch through the command table at OS $252318 + 4·command (entry OS $252460 → OS $207ce4). The
handler ignores the dump when its receive counter (OS data $252d72, $ffffffff at boot, so unlimited) has run out,
only verifies it in the receive page's verify mode (OS $20707c, flag at $26245c), and otherwise stores it with OS
$207748:

1. Copies the message from the version byte to F7 into a buffer at $27a872 (OS $207750-$20776c).
2. **Length:** the two bytes before F7 must equal the size minus 10, else status 1 (OS $207776-$2077d2).
3. **Checksum:** the sum of the bytes from the slot byte to the one before the checksum, & $3fff, must equal the
   checksum, else status 2 (OS $2077d6-$207806).
4. **Version** (byte 7): 1 to 4, else status 4 (OS $20783e-$20785c). The revision byte (8) is never read: no
   reference to $27a873 anywhere in the OS.
5. **Slot** (byte 9): above 63, status 3 (OS $2078c8-$2078cc). On the receive page with a chosen slot (OS data
   $252d62 other than -1, set by OS $22d3c4-$22d3d0) the dump goes there instead and the chosen slot counts up per Kit
   (OS $207868-$2078c2), recording the old-to-new numbers for the patterns (tables $29f3c0 and $29f30a).
6. **Name:** 16 bytes copied into stored Kit n; if the first is $7f it becomes $ff, the empty mark
   (OS $2078d0-$207920).
7. Parameters and levels copied as they are; machines unpacked into the 16 words (OS $207954-$2079d0).
8. **LFOs:** version 1 reads $40 + $300 bytes and resets every LFO to the defaults above; version 2 reads $180 bytes
   (copies 10 blocks, resets blocks 11-15); versions 3 and 4 read $240 bytes and store the 16 blocks verbatim, state
   bytes included (OS $2079da-$207b70).
9. Master effects copied raw; groups unpacked and stored (OS $207b76-$207c5a).
10. Posts a status record (`F0 00 20 3C 02 00 46 …` template at OS $252d86: code 0 for stored, the slot) to the queue
    at $2ab2ec (OS $207c64-$207c96). Whether that reaches MIDI OUT or only the receive page's counters was not traced.

The dump lands in a stored slot, never in the working Kit, even when the slot is the current Kit: a real Machinedrum
plays it after LOAD KIT ($58) or SET STATUS Kit (`.scratch/md-editor/README.md:180`).

Against MCL: MCL accepts versions 1-4 and 64-66 and writes 64 (`MDX_KIT_VERSION`, `MDMessages.h:10`;
`MDMessages.cpp:372-391`, `:543-553`), with the comment "Stock MD firmware accepts version 64". OS 1.63's step 4
refuses 64; MCL's 64 and its TONAL flag (`models[t] >= 0x20000`, `MDMessages.cpp:221-225`, `:324-328`) belong to the
unofficial firmwares MCL detects (`FW_CAP_TONAL`, `MD.cpp:496`). Version 1's layout (MCL: LFO size $340 for version
1, `MDMessages.h:35-41`) agrees with step 8. MCL writes revision $01 and starts the checksum at the slot byte
(`Elektron.cpp:110-122` at d5714732), as the OS does.

## Bytes `parseKitDump` does not read, and what to write there

| Dump bytes | `parseKitDump` | Write |
|---|---|---|
| 7-8, version and revision | not checked (`validDump`, `mdsysexautomation.cpp:51-72`) | $04 $01, as the OS sends |
| 9, slot | read | 0-63 |
| $0a-$19 after the name's NUL | not read | 0 (name of 10 characters at most, first byte never $7f unless the slot is empty) |
| machine words, upper 3 bytes | dropped (only `raw[t*4+3]`, `mdsysexautomation.cpp:1283-1287`) | 0 (all factory Kits; TONAL is MCL's custom firmware) |
| LFO block bytes 5-35 | dropped (`mdsysexautomation.cpp:1288-1300`) | 0, except bytes 28-31 = 00 00 02 9a; keep a received dump's bytes when round-tripping |
| $4a7-$4cb, groups | not read | 16 trig groups then 16 mute groups, $ff or 0-15, 7-bit packed (37 bytes) |
| checksum, length | checked | `finishDump` (`mdsysexautomation.cpp:217-229`) |

LFO bytes 0-4 must stay in range: track 0-15, parameter 0-23, shapes 0-5, update 0-2 (the bounds `lfoChange` uses
and the firmware clamps to, `mdsysexautomation.h:264-268`, `.scratch/md-editor/README.md:238`).

Helpers already there: `append7Bit`, `read7Bit`, `finishDump` (anonymous, `mdsysexautomation.cpp:146-229`), and the
test builder `makeKitDump` (`mdAutomationTestSupport.h:405-462`), which leaves the LFOs and groups at zero and goes
through `makeDump`'s version $01 header (`mdAutomationTestSupport.h:291-307`): fine for parser tests, wrong for a
Machinedrum. `automationMidiTest` builds its Kit header as 04 01 (`source/elektron/md/mdLibTest/automationMidiTest.cpp:396-398`).

### An empty Kit as the OS makes it

What OS 1.63 writes for a new or cleared Kit (OS $23dc0a-$23dd5c, tables in its data), a model for an empty Slot or
"init Kit": name "NEW KIT" (OS $246100); per track synthesis 64 64 0 0 0 0 0 0 (OS $251cde), effects page
AMD 0 AMF 0 EQF 64 EQG 64 FLTF 0 FLTW 127 FLTQ 0 SRR 0 (OS $246148), routing page DIST 0 VOL 127 PAN 64 DEL 0 REV 0
LFOS 64 LFOD 0 LFOM 0 (OS $246150); level 100; machine 0 (GND---); LFO block: own track, parameter 0, shapes 0 and 4,
update 0, state as above; reverb 0 0 64 0 0 127 127 96 (OS $251d76), echo 32 0 32 0 0 127 0 96 (OS $251d5e), EQ 64×7
127 (OS $251d66), dynamix 127×6 0 0 (OS $251d6e); all groups OFF.

## A Bank file

- **Format:** the 64 Kit dumps one after another, each the 1233-byte version-4 dump above, slot bytes 0 to 63 in
  order: 78,912 bytes, no header. An empty Slot can be written as a dump whose name starts with $7f (what the OS sends
  for one, and what empties that slot on receipt) or left out (a receiving Machinedrum then keeps its own Kit there).
- **The Machinedrum reads it** without any menu: each dump goes to the slot it names (steps 1-9 above); on the receive
  page with a chosen slot the Kits fill consecutive slots from it, which is the ticket 10 "import into the Slots from
  the chosen one" behaviour on the hardware. Nothing reaches the working Kit. Pacing between messages was not
  measured; one dump takes 0.39 s on a 31,250-baud line (1233 × 10 bits).
- **Tools:** Machinemodule treats each `.syx` as a bank and lists every Kit dump in it (`machinemodule/README.md:276-280`,
  copied 2026-10-06; `HANDOFF.md:1205-1206`, "BANK = file"). A librarian that sends a `.syx` sends its messages in
  order; one Machinedrum editor sends with 60 ms between messages, configurable since its v1.3 (Elektronauts, "SM -
  Machinedrum sample bank and sysex editor", 2026-01-03, read 2026-10-08).
- **Import** of foreign files: keep the $52 messages of a file (a Machinedrum project dump also carries Globals $50,
  patterns $67 and songs $69: templates at OS $2462a4-$2462bf), accept versions 3 and 4 with the 1233-byte layout and
  MCL's 64 (same body, `MDMessages.cpp:374-379`), refuse MCL's 65 and 66 (SPS-X layouts of 1064 and 1144 bytes,
  `MDMessages.h:68-75`) and versions 1-2 (other LFO sizes) unless they are converted as the OS does.

## Not settled

- Which DSP2 answer selects section 3 or 4 (flash $1e6c, $1fec): inferred from the images' contents and the
  emulator's factory Kit, not traced in DSP2's boot code.
- The Machinedrum's own limit on name length (10 seen everywhere, field of 16).
- What LFO bytes 6-27 and 32-35 hold at run time, and whether SAVE KIT writes the running LFO state back (the factory
  Kits hold only the seed).
- Where the status record of step 10 goes ($2ab2ec), and whether a real Machinedrum needs a pause between Kit dumps.
- Elektron's own Kit dump specification (`md1_53-sysex-0_8`, given to developers on request according to forum posts
  found 2026-10-08) was not found; everything above comes from the OS code and the images.
