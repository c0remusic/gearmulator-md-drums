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
- `engine/MasterEngine.*` (new), `engine/HostModel.*`, `engine/MdEngine.h`: the master effects. `MasterEngine` runs
  the mixer DSP's master section (P:$342-$970 of OS 1.63's section 2: Rhythm Echo, Gate Box, their returns, EQ,
  Dynamix) on its own in a second dsp56300, after the parts of the program's boot it needs, on VoiceEngine's
  host-port handshake. `HostModel` keeps the Kit's 32 master bytes in the OS's slew targets (`setMaster`) and, each
  tick after the level slew, calls one of the tick routine's four inline bodies that compute an effect's DSP words
  ($20b4ae, $20b604, $20b74e, $20b940; their exits patched to `rts`), as the OS does in turn. `EngineT::enableMaster`
  creates it; `render` then runs it on the mixer's buses (`Output::master`), or the caller does (`processMaster`).
  Main comes out 16 samples after the buses, Dynamix's look-ahead.
- `engine/MdEngine.h`, `engine/TrackFx.*`: `EngineT` keeps `TrackFx::Tables` behind a `shared_ptr` and hands it out
  (`tables()`), and `TrackFx::eqWords` keeps the EQ's coefficient words of the last block, so that MD Drums' editor
  draws the filter's and the EQ's response from the words the DSP computes (`mdDrums/mdDrumsFilterResponse.*`).
- `engine/HostModel.*`: parameter locks from outside (`lock`, md-drums' ADR 0003), which upstream leaves out with the
  sequencer. A Hit's locks go where the tick's trigger path applies the OS's own, with a pending machine: straight
  into the smoothing target, the smoothed array and the voice array, held as the target until the track's next Hit,
  which puts the Kit's values back the same way. A lock with no Hit of its track in its block is dropped. Not
  compared with the firmware's sequencer.
- `engine/MdEngine.h`, `engine/VoiceEngine.*`, `engine/MachineRunner.h`: `EngineT::warmUpVoices` compiles every audio
  machine's voice code ahead. The voice DSP's JIT compiles a machine's code the first time a voice runs it, 1.5 to
  8 ms in one 32-sample block, which an audio thread at 128 samples cannot afford. Each machine but RAM-R and RAM-P
  plays a Hit at its default SYN1-8 for 4 blocks on the voice DSP alone, its words computed by a copy of the OS
  (`MachineRunner::clone`); then `VoiceEngine::restoreState` puts back the DSP's P, X and Y memory and registers as
  `saveState` took them, P's code unchanged so that what the JIT compiled stays valid. 177 ms on this PC; the engine
  then gives the same samples as one that was not warmed, and a Track's first Hit costs no more than its next ones
  (`mdDrums/mdDrumsFirstHitTest.cpp`).
- `engine/MasterEngine.*`: `MasterEngine::warmUp` does the same for the mixer DSP, whose JIT compiled the master's
  code in the engine's first block: 26 ms where an audio thread at 128 samples has 2.9. It runs 64 blocks, loud noise
  on the three buses then silence, and puts back the DSP's memory and registers. 33 ms on this PC; Main is then the
  same, sample for sample, the first block costs 0.25 ms, and no master parameter through its range costs a block more
  than 0.12 ms of master (`mdDrums/mdDrumsFirstHitTest.cpp`).

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

Master effects (2026-10-09, with `mdEngineFirmwareMaster`): seven scenarios (factory Kit 1's settings, the gate open,
each send alone, EQ and Dynamix compressing, the Echo filtered, mono and fed back, the Gate Box with the Echo into it)
on a booted firmware. The words the OS's own code computes in MachineRunner equal the firmware's mixer DSP's, word for
word. `MasterEngine`, started from that DSP's memory as it stood before the Hit and fed this engine's buses, gives the
firmware's Main sample for sample in all seven, and the same with random registers and scratch memory before every
block: the master section depends on nothing the skipped parts of the program leave. From this engine's own history,
Main differs: the Echo's and Gate Box's rings and the Gate Box's sine stand where the blocks run since the start put
them, and a ring that wraps jumps back to its start, so a read crossing the end lands a sample off. With EFM-BD and
TRX-SD, the firmware's Main differed in some runs from the Hit's first sample on by the same -75 dB whatever the
master's settings, so in what reached its master; TRX-BD, EFM-CB and P-I-MT never did (not traced).

Known difference: the firmware's ROM hits vary with what it played before them, by up to about -75 dB of residual.
The same ROM-22 hit recorded alone and after ROM-01 and -02 differs from itself by -79.5 dB; this engine's ROM hits
do not depend on what came before, and match the firmware's within -100 to -112 dB in some sequences and -75 to -80
dB in others (all 32 slots measured one by one, 2026-10-07). The voice DSP's P, X and Y memory and the voice's
coefficient words are the same in both; the cause is not known. With 8 or more ROM machines sounding, the firmware's
voice DSP misses its deadline (ROM-09 played as the 8th of 15: residual +3.6 dB), and this engine does not.

Known difference: a voice keeps its noise generator's state between hits, and the engine stops rendering a voice
that has died away while the firmware keeps it running, so a noise-based machine's second hit on a track does not
repeat the firmware's sample for sample.
