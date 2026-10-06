# Engines without the ColdFire: public documentation

Copied on 2026-10-06 from public repositories, as reference for the question of running the Machinedrum's sound
engines without emulating its whole OS. Each folder keeps its project's licence; nothing here is compiled into the
plug-in. Only documentation was copied: their source code stays in their repositories.

| Folder | Project | Licence | What it holds |
|---|---|---|---|
| `machinemodule/` | [sd88me/mpc-vst-machinedrum](https://github.com/sd88me/mpc-vst-machinedrum) ("Machinemodule") | AGPL-3.0-only | Machinedrum UW OS 1.63 without the OS running. `docs_PROTOCOL.md`: DSP boot, host vectors (`P:$12` block write to Y, `P:$10` peek, `P:$14`), DSP2 voice slots `Y:$800+$40k`, DSP1 per-track values, machine descriptor table at `$24ef54`. `docs_FIRMWARE.md`: the OS file. `HANDOFF.md`: project state. |
| `machinedrum-kit/` | [janne808/machinedrum-kit](https://github.com/janne808/machinedrum-kit) (Janne Heikkarainen) | GPL-3.0 | OS 1.63 internals: system overview, ColdFire OS, DSP2 voice ABI, DSP1 audio path, emulation and verification, open questions. |
| `monomodule/` | [shnolk/monomodule](https://github.com/shnolk/monomodule) (Shnolk) | AGPL-3.0 | The same approach for the Monomachine (DSP only, no ColdFire); README and third-party notices. |
| `mame/` | [mamedev/mame](https://github.com/mamedev/mame/blob/master/src/mame/elektron/elektronmono.cpp) | BSD-3-Clause | MAME's Elektron skeleton driver: memory map, DSP roles. |

Licence note: reusing AGPL code would make this plug-in AGPL; the facts these documents describe carry no such
obligation.

Why it matters: `.scratch/parallel-transport/issues/07-go-no-go.md` rejected engines without the UC partly because
the HI08 protocol would need reverse engineering machine by machine. Machinemodule avoids that by calling the OS's
own machine, LFO and smoothing routines (pure functions in the OS image) in a 68k emulator, with DSP2 emulated and
DSP1's track chain and mix translated to C++.
