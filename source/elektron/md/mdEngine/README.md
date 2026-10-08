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
- `tools/mdfw/Firmware.*` and `engine/MdEngine.h`: `loadRomBankFromFlash` redoes what OS 1.63's boot does with the
  UW sample bank in the flash (`$20d20e`), and `EngineT::loadRomBank` writes the result into the voice DSP, which
  upstream leaves without it: the ROM machines play their samples.
- `tools/mdfw/Firmware.*`: `loadPatchImageFromFlash` returns the factory patch-memory image (container section 4
  on a UW, 3 otherwise) that the bootloader's factory reset depacks, with the 16 factory Kits.
- `engine/HostModel.*`: the Kit's trig and mute groups (Links and Chokes, `setLink`, `setChoke`), played as OS
  1.63's note handler ($20cc48) and tick ($20ad9a) play them; a muted track drops its Hits, as the note handler does
  ($20ccf6); every Hit sets the track's smoothed level to its Kit level, as the tick's trigger path does ($20b022).
- `engine/Mixer.*`, `engine/HostModel.h`, `engine/MdEngine.h`: for MD Drums' sample-accurate Hits, which mix the
  tracks itself (`mdDrums/mdDrumsEngine.cpp`): `Mixer::gains` and `Mixer::mainSample` are `process`'s own gain and
  output steps, split out (bit-exact, `process` uses them); `HostModel::muted` and `choked` read a track's mute and
  Choke flags; `EngineT::mixOn = false` skips the mixer.

Machinemodule's own `dsp56300` fork is not used: its x64 JIT stops the voice DSP's init on Windows
(`instruction budget exceeded at PC=$10008b`), and ours runs it.

## Verified

`mdEngineFirmwareTest` (with `mdEngineFirmwareHits`) plays one hit of TRX-BD, TRX-SD, EFM-CB and P-I-MT on a fresh
track of both this engine and the full firmware (`mdLib`), with the same parameters and level, routed to output A:
correlation 1.000000, gain 1.0000, residuals 99 to 110 dB down (2026-10-06).

The UW bank (2026-10-07, with `mdEngineFirmwareRomBank`): `loadRomBankFromFlash` gives, word for word, the voice
DSP's directory and the 261,138 words of samples that the firmware's boot leaves there (32 samples, from the flash
image alone). ROM-01, -02 and -22 (44.1 kHz with an odd length, 32 kHz, 44.1 kHz looped), played whole, correlate at
1.000000 with gain 1.0000; residuals -77.5, -104.0 and -79.5 dB.

Known difference: a voice keeps its noise generator's state between hits, and the engine stops rendering a voice
that has died away while the firmware keeps it running, so a noise-based machine's second hit on a track does not
repeat the firmware's sample for sample.

Links and Chokes (2026-10-08, with `mdEngineFirmwareGroups`): factory Kit 1's Choke (TRX-CH cutting TRX-OH), a
choked track struck again, a Link at velocity 80, a muted track struck and a Choke onto a lower track give the
firmware's samples wherever both sound or both are silent (identical, or within -67.7 to -79.1 dB where another track
sounds alongside). Two moments differ, both set by the firmware's UC and not by the sound: DSP1 takes the UC's words a
block ahead of DSP2's audio of the same tick, so a Choke in the Hit's tick cuts its target a block before the Hit
sounds on the firmware and in the Hit's block here (32 samples longer); and what the OS changes at its next tick (a
Choke onto a lower track, a choked track's sound coming back) comes at this engine's next tick (11 blocks) rather
than the firmware's, whose period follows its load (3 to 11 blocks measured). Concurrent voices also nudge each
other on the firmware: its P-I-MT after a TRX-BD differs by -79 dB from the same hit after an EFM-CB, where the
engine's tracks are independent.

Known difference: the firmware's ROM hits vary with what it played before them, by up to about -75 dB of residual.
The same ROM-22 hit recorded alone and after ROM-01 and -02 differs from itself by -79.5 dB; this engine's ROM hits
do not depend on what came before, and match the firmware's within -100 to -112 dB in some sequences and -75 to -80
dB in others (all 32 slots measured one by one, 2026-10-07). The voice DSP's P, X and Y memory and the voice's
coefficient words are the same in both; the cause is not known. With 8 or more ROM machines sounding, the firmware's
voice DSP misses its deadline (ROM-09 played as the 8th of 15: residual +3.6 dB), and this engine does not.

Known difference: a voice keeps its noise generator's state between hits, and the engine stops rendering a voice
that has died away while the firmware keeps it running, so a noise-based machine's second hit on a track does not
repeat the firmware's sample for sample.
