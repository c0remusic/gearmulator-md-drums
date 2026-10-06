# Machinedrum OS 1.63 file format

Findings from `Elektron_SPS1-1UW_OS1.63.syx` (SHA-1 `14a3396ab1625463f83aeac0e66528a5dec3966d`), checked
with `tools/mdfw`.

## Transport and container

The same as the Monomachine OS (see Monomodule's `src/core/firmware`), with two differences:

- The sysex device byte is `02` (the Monomachine's is `03`): `F0 00 20 3C 02 00 7E ...`. 14,683 data messages of
  111 bytes each, plus an end marker and a version message.
- The version tag between sections is 4 ASCII bytes (`"163 "`), not 8.

The decoded flash image starts at `$004000` and is 939,744 bytes. It is byte-for-byte identical to a full
8 MB MD flash image (`elektron_sps1-1uw_os1.63.bin`, SHA-1 `a872a2f3527063673d6ea6d3080c4c62ef0cadc1`, the image
gearmulator-md-mm uses) from `0x4000` on. The rest of that 8 MB image (6.9 MB of data at `$100000..$7C0000`) is
not in the `.syx`; what it holds has not been checked.

| Section | Flash | Packed | Unpacked | Contents |
|---|---|---|---|---|
| 0 | `$004000` | 164,328 | 404,766 | ColdFire main OS (not used by this port) |
| 1 | `$02C1F0` | 587,978 | 750,369 | DSP program A |
| 2 | `$0BBAC2` | 39,310 | 56,469 | DSP program B |
| 3 | `$0C545C` | 74,169 | 524,288 | 512 KB image, not DSP records |
| 4 | `$0D761D` | 73,915 | 524,288 | 512 KB image, 89% identical to section 3 |

All five section checksums match, and the container ends exactly at the last byte.

## DSP records

The Monomachine layout, with one extra marker: 24-bit little-endian words, `[space][addr][count]` followed by
`count` words (space 0/1/2 = P/X/Y); `[3][addr]` = start address; `[4][arg]` = Machinedrum-only marker, argument 0
in OS 1.63, meaning unknown. Each program starts with `[3][$24] [4][0]` and ends with `[3][$24]`.

| | Records | P int + ext | X int + ext | Y int + ext | Start |
|---|---|---|---|---|---|
| Program A (section 1) | 135 | 961 + 233,753 | 74 + 13,056 | 1,604 + 264 | `P:$24` |
| Program B (section 2) | 58 | 2,568 + 15,921 | 66 + 0 | 88 + 0 | `P:$24` |

"ext" = addresses at or above `$100000`, the bridged external memory, as on the Monomachine. Both programs load
a full vector table at `P:$0` (reset vector `jmp >$ff0000`, the bootstrap ROM; unused vectors jump to
themselves).

## DSP roles: confirmed by tracing

Program A (section 1) runs on DSP2 (voice producer); program B (section 2) runs on DSP1
(mixer/codec/master FX). Confirmed by capturing the real ColdFire→DSP transfer with
`tools/mdtrace` and matching it word-for-word against these decoded sections. See
`docs/PROTOCOL.md`.

## Open questions

- **What the 234K words of external P memory in program A are.** Code, tables or sample data;
  likely includes the E12/ROM/RAM sample data given the size, not yet directly confirmed.
- **Sections 3 and 4.** Probably RAM or factory data images, in two variants.
- **The `[4][arg]` marker.**
- **The runtime CPU↔DSP protocol** (post-boot parameter/control words): see `docs/PROTOCOL.md`.
