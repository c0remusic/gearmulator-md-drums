# mdEngine

The Machinedrum SPS-1 UW's engines without its ColdFire OS: the voice DSP (DSP2) emulated on this repository's
`dsp56300`, the OS's own machine, smoothing and LFO routines called in a 68k emulator (`mc68k`), and the mixer
DSP's (DSP1) per-track effects and mix as C++. No boot: the engine plays as soon as it is built (about 45 ms).

## Origin

`engine/` and `tools/mdfw/Firmware.*` come from Machinemodule, https://github.com/sd88me/mpc-vst-machinedrum,
commit `796ded6`, © sd88me, licensed under the GNU AGPL v3 (`LICENSE` here). `tools/mdfw/Firmware.*` is itself
adapted from Monomodule, https://github.com/shnolk/monomodule (© Shnolk, AGPL v3). The layout is theirs, so that
later versions can be compared file by file. Their documentation is in `.scratch/uc-less-engines/`.

## Changes from upstream

- `engine/MachineRunner.cpp`: `memmem` (POSIX) replaced by `std::search`, for MSVC.
- `engine/VoiceEngine.cpp`: the call to `DSP::setInterpreterEnabled`, which only Machinemodule's `dsp56300` fork
  has, is left out: this repository's `dsp56300` runs the JIT on every target it builds.
- `tools/mdfw/Firmware.*`: `loadFirmwareFromFlash` reads the OS from the 8 MB flash image the firmware plug-in
  already uses (it holds the OS file's decoded image from `$4000` on), so no `.syx` is needed.

Machinemodule's own `dsp56300` fork is not used: its x64 JIT stops the voice DSP's init on Windows
(`instruction budget exceeded at PC=$10008b`), and ours runs it.

## Verified

`mdEngineFirmwareTest` (with `mdEngineFirmwareHits`) plays one hit of TRX-BD, TRX-SD, EFM-CB and P-I-MT on a fresh
track of both this engine and the full firmware (`mdLib`), with the same parameters and level, routed to output A:
correlation 1.000000, gain 1.0000, residuals 99 to 110 dB down (2026-10-06).

Known difference: a voice keeps its noise generator's state between hits, and the engine stops rendering a voice
that has died away while the firmware keeps it running, so a noise-based machine's second hit on a track does not
repeat the firmware's sample for sample.
