# Emulator harness

`tools/emulator` is a headless Machinedrum emulator with a three-processor
debugger, built on Gearmulator's Machinedrum board model. It boots the
**unmodified firmware from reset**: the ColdFire bootloader decompresses the
blobs and uploads both DSP programs itself, and nothing is preloaded. Every
emulator result in this kit was produced with it.

| Program | Purpose |
| --- | --- |
| `md-harness` (via `harness.py verify`) | Smoke test: boot, MIDI SysEx reply, panel response, MIDI-triggered audio |
| `md-monitor` (via `monitor.py`, `harness.py debug`) | Debugger with a JSON protocol: breakpoints, watchpoints, traces, memory, disassembly, MIDI/panel input, LCD, audio capture, deadline and cycle instrumentation |
| `md-kernel` | Runs one machine's DSP code outside the firmware for bit-exact and cycle tests |
| `md-debug-hooks-test` | Regression test of the DSP debug hooks (no firmware needed) |

Linux only; the monitor uses POSIX pipes and signals.

## Components and pins

Third-party code is included **only as a git submodule**, at pinned commits.
The kit adds local changes as patch files applied on top.

| Component | Repository | Commit | Path |
| --- | --- | --- | --- |
| Gearmulator (Machinedrum/Monomachine fork) | `joelanders/gearmulator-md-mm` | `dfe1e61b` | `tools/emulator/third_party/gearmulator` (submodule) |
| DSP56300 emulator | `joelanders/dsp56300-md-mm` | `a8ff3425` | `…/source/dsp56300` (nested submodule) |
| ColdFire core (Musashi) | `joelanders/mc68k-md-mm` | `ace95b3d` | `…/source/mc68k` (nested submodule) |
| AsmJit (DSP JIT backend) | `dsp56300/asmjit` | `3577608c` | `…/source/dsp56300/source/asmjit` (nested submodule) |

The nested pins are the ones Gearmulator itself records at `dfe1e61b`, so a
plain nested init reproduces them. Gearmulator has further submodules (JUCE,
RmlUi, freetype, …) that the headless build does not need; `setup.py`
initializes only the three above. **Do not clone with `--recursive`**: it
would also fetch all the GUI dependencies.

Gearmulator and the DSP56300 emulator are GPL-3.0; Musashi and AsmJit carry
their own licences in their trees. The kit's emulator sources are GPL-3.0
(`tools/emulator/LICENSE`).

## Setup and build

Requirements: git, Python ≥ 3.10, CMake ≥ 3.16, Ninja, a C++17 compiler, GNU
m68k binutils (`binutils-m68k-linux-gnu` on Debian/Ubuntu; the monitor's
ColdFire disassembly uses it).

```sh
git submodule update --init tools/emulator/third_party/gearmulator
cd tools/emulator
python3 harness.py build          # = setup.py, cmake configure, cmake --build
cmake --build build --target check  # hook + ColdFire regression tests, no firmware needed
```

`harness.py build` runs `setup.py`, which:

1. initializes the Gearmulator submodule if needed, and the three nested ones;
2. checks that all four repositories are at their pinned commits;
3. applies the patches below with `git apply`, each inside its own repository.
   It is idempotent: an already-applied patch is detected with
   `git apply --reverse --check` and skipped.

`python3 setup.py --check` only verifies, and CMake runs it at configure time,
so unpinned or unpatched sources refuse to build. `python3 setup.py --reset`
removes the patches. The superproject marks the submodule `ignore = dirty`, so
the patched working tree does not show up in `git status`.

The build writes `synthLib/buildconfig.h` into the Gearmulator tree (upstream
`configure_file`); that is expected.

### The patches

| Patch | Repository | What it changes |
| --- | --- | --- |
| `gearmulator-0001-monitor-hooks.patch` | Gearmulator (`mdLib`) | **ColdFire access hooks**: `debugAccess` callbacks on every 8/16-bit read and write; `debugEvent` for injected IRQs, IRQ4 and the task-list workaround; a switch to disable that workaround; `debugPeek`, a side-effect-free read of mapped storage (never MMIO). **Experimental images**: `RomLoader::allowExperimentalImage` accepts exactly one caller-supplied image by FNV-1a fingerprint, and `Hardware` asks the loader instead of comparing with the stock fingerprint. Without this, a non-stock image makes upstream silently search for, and boot, a stock ROM. **Harness RAM**: a 4 KiB window at CPU `0x0F000000` (outside the board map), used by older live-registration experiments; unused by the kit. |
| `gearmulator-0002-md-scheduler-30us-quantum.patch` | Gearmulator (`mdhardware.cpp`) | Machinedrum CPU/DSP interleave quantum 125 µs → **30 µs**, and the `MD_SCHED_QUANTUM_US` override (see [below](#scheduler-quantum)). |
| `mc68k-0001-coldfire-instruction-hook.patch` | mc68k (Musashi) | Enables Musashi's instruction hook and passes the CPU instance to it, so the monitor sees every ColdFire instruction boundary. |
| `dsp56300-0001-debug-hooks.patch` | dsp56300 | Per-instruction callback in the interpreter and the JIT, including REP bodies and fast-interrupt vectors. Memory-access, DMA-event and bulk-transfer callbacks. Register snapshots published from the JIT's cached registers without committing them. Everything is inert when no callback is installed. |

The patched sources behave like upstream unless a callback is installed.
`md-harness` installs none, `md-monitor` installs them all, and `md-kernel`
uses the DSP core directly.

## Smoke test: `harness.py verify`

```sh
python3 harness.py verify --firmware elektron_sps1-1uw_os1.63.bin --output out/verify
python3 harness.py verify --firmware custom.bin --experimental-firmware --output out/verify-custom
```

The output directory must be empty, so an old result can never pass for a new
one. Steps:

1. `mdkit` extracts and checks the image (compressed-byte checksums, DSP
   streams). A non-stock image needs `--experimental-firmware`.
2. `md-harness` constructs the board and checks the reset PC (`0xC`). It runs
   until both DSPs are ready, firmware MIDI is ready and the LCD shows pixels
   (`--boot-seconds`, emulated).
3. Compares the first 256 bytes of guest RAM `0x200000` with `mdkit`'s
   independent MainOS decompression.
4. Runs 3 s more, sends the current-kit SysEx request
   `F0 00 20 3C 02 00 70 02 F7` and requires a correctly framed reply.
5. Presses and releases Tempo, then Exit, on the panel UART and requires LCD
   changes both times.
6. Records an idle baseline, sends four note-on/off pairs on note 36 and
   requires audio above the baseline, finite output on all six channels, a
   drained MIDI queue and no host audio-queue under/overflow.

Outputs:

| File | Contents |
| --- | --- |
| `report.json` | Verdict, firmware extraction, executable hash, runtime results, limitations |
| `runtime.json` | Individual checks: boot frames, prefix match, SysEx reply, panel, audio peak/RMS |
| `trace.jsonl` | Sampled PCs and cycle counts of all three processors during boot |
| `drums.wav` | 4 s stereo PCM16 of the drum hits |
| `boot.pbm`, `tempo.pbm`, `final.pbm` | LCD snapshots (128×64, ASCII PBM) |
| `midi-replies.hex` | Raw firmware SysEx replies |
| `emulator.log` | Native diagnostics |

`--timeout` limits wall-clock seconds for the whole process. Environment
variables starting with `GEARMULATOR_` are stripped, so scheduler experiments
in a shell cannot leak into a verification.

## The monitor

```sh
python3 monitor.py --firmware IMAGE.bin                       # interactive, prompt md>
python3 monitor.py --firmware IMAGE.bin --experimental-firmware
python3 monitor.py --firmware IMAGE.bin --script commands.txt # one JSON result per line; exit 1 on error
python3 monitor.py --firmware IMAGE.bin --json < requests.jsonl
python3 harness.py debug --firmware IMAGE.bin …               # same, via the harness
```

The firmware may also come from `MD_FIRMWARE`. Diagnostics go to `--log`
(default `monitor.log`), never to the result channel. The machine starts
**paused at the ColdFire reset PC `0xC`**. A stop on any processor pauses the
whole machine. `help` lists the commands; Ctrl-C requests a pause.

`--experimental-firmware` first validates the image with `mdkit` (the blob
chain, compressed-byte checksums and DSP streams). It then authorizes exactly
that image's fingerprint in the backend, which verifies that the whole
loaded flash equals the file before it runs. Without the option only the
stock image is accepted.

### Conventions

- `SPACE` is `B` for ColdFire bytes, or `P`, `X`, `Y` for DSP 24-bit words.
  DSP peripherals use full 24-bit addresses (`X 0xfffffd`).
- Numbers are decimal or `0x` hex. Arguments with spaces can be double-quoted.
- `cpu coldfire|dsp1|dsp2` selects the processor that `break`, `step`,
  `memory` and `disasm` refer to. The DSPs have separate memories and
  breakpoint namespaces.
- Symbols (`symbols load FILE`, `symbol SPACE ADDRESS NAME`) can replace
  addresses in `break`, `disasm` and `memory`. `symbols/os163.tsv` is loaded by
  default: the addresses this kit documents, each marked `verified` or `static`.

### Command reference

**Execution**

| Command | Meaning |
| --- | --- |
| `status` | Stop reason, frame count, readiness, audio capture state, and per-processor PC/boundary information |
| `boot [FRAMES]` | Run until both DSPs are ready, firmware MIDI is ready and the LCD is lit. Readiness comes before the UI has settled: follow with `continue 330750` (7.5 s) before driving menus. |
| `continue [FRAMES]` | Run until a stop or the frame budget (reason `frame_budget`). Returns `paused: false` after 60 s wall-clock; then use `wait`. |
| `run [FRAMES]` | Start asynchronously |
| `pause [MS]`, `wait [MS]` | Request a stop / wait for one |
| `step [N]` | N instruction boundaries of the selected processor. The others run as the scheduler requires; REP bodies count per iteration. |

**Stops**

| Command | Meaning |
| --- | --- |
| `break ADDRESS\|SYMBOL` | Stop before the selected processor executes the address. A breakpoint at the current PC fires on the next visit. |
| `watch r\|w\|rw SPACE ADDRESS [COUNT]` | Watch a range. The stop happens at the processor's next instruction boundary, so the accessing instruction completes. The hit carries the access's PC, address and value (`null` for reads: the observer never performs an extra read). |
| `points`, `delete ID` | List and remove breakpoints and watchpoints |

Stop reasons: `reset`, `breakpoint`, `watchpoint`, `step`, `pause`,
`frame_budget`, `boot_ready`, `audio_complete`.

**Inspection** (never executes an MMIO read, advances time or drains a queue)

| Command | Meaning |
| --- | --- |
| `regs [CPU]` | Registers. DSP: `r0..r7`, `n0..n7`, `m0..m7`, `x0 x1 y0 y1`, `a`/`b` (as 56-bit hex text), `sr`, `omr`, `sp`, `sc`, `la`, `lc`, `vba`, `system_stack`. ColdFire: `d0..d7`, `a0..a7`, `sr`, `vbr`, `cacr`, `acr0/1`, `rambar`, `mbar`. All: `pc`, `boundary_pc`, `backend_pc` (DSP), `cycles`, `observed_instructions`, `at_instruction_boundary`. |
| `memory SPACE ADDRESS COUNT` | Values from mapped storage (ROM, RAM, flash, DSP memories). MMIO and unmapped addresses fail. |
| `dump SPACE ADDRESS COUNT FILE` | The same to a file: ColdFire bytes; DSP words as 3-byte little-endian |
| `disasm [ADDRESS] [COUNT]` | Live disassembly. ColdFire through GNU `m68k:isa-a:mac`; DSP through the emulator's decoder. DSP rows include raw `words`, `size`, `branch`/`call`/`return` flags, a direct `target` or `dynamic_target`, and `loop_end`. |
| `peripherals` | Snapshots of the MIDI/panel queues, SIM parallel port, both HI08s and all DMA channels |

**Traces** (a ring of the last 8,192 events; the response reports drops)

| Command | Records |
| --- | --- |
| `trace io` | SIM/UART/HI08 traffic, IRQs, DMA, workaround calls |
| `trace all` | Also every instruction boundary and memory access, on all processors |
| `trace cpu` | Instructions, accesses and events of the processor selected at that moment |
| `trace program` | Value-changing P-space stores of the selected DSP (the JIT suppresses stores of an unchanged value; aliased X/Y writes and bulk DMA are not covered) |
| `trace off`, `trace clear`, `events [N]`, `trace save FILE` | Stop, clear, read, save as JSONL |

Sequence numbers order events across processors. Cycle counters stay in each
processor's own clock domain. A DMA write can be followed by a generic write
event carrying the PC that happened to be executing: do not attribute it to
that instruction.

**Input and output**

| Command | Meaning |
| --- | --- |
| `midi BYTE…` | Queue a raw MIDI message on MIDI in (`midi 0x90 36 120`) |
| `midi-out` | Drain the decoded outbound MIDI (this one *does* consume a queue) |
| `panel ROW MASK` | Queue a panel event; release with the same row and mask 0 |
| `lcd FILE` | Save the current LCD as ASCII PBM (`P1`, 128×64) |
| `record FILE FRAMES [2\|6]` | Run and capture PCM16 WAV at 44.1 kHz, 2 channels (default) or all 6, at most 30 s. Stops with `audio_complete`; reports peak and RMS; rejects non-finite output. |
| `input tone HZ LEVEL A\|B\|both`, `input off` | Feed a sine into the codec ADC inputs during `record` |
| `workaround on\|off` | Enable/disable the board model's firmware task-list workaround (on by default; off can stall boot and panel) |

**Instrumentation**

| Command | Meaning |
| --- | --- |
| `audio-budget reset\|status\|off` | Deadline and cycle measurement (next section) |
| `pclog add CPU PC…` | Record a timestamp whenever the processor reaches one of these PCs |
| `pclog sample CPU N` | Also record every Nth instruction of that processor |
| `pclog reset\|clear\|status\|dump FILE` | Clear records (`reset`), clear records and watch list (`clear`), show counts, write records |

### Panel events

| Button | Row | Mask |
| --- | --- | --- |
| Tempo | `0x22` | `0x01` |
| KIT | `0x22` | `0x40` |
| EXIT | `0x23` | `0x10` |
| DOWN | `0x23` | `0x40` |
| RIGHT | `0x23` | `0x80` |
| UP | `0x24` | `0x10` |
| ENTER | `0x24` | `0x08` |

Hold each press for a few thousand frames and wait after the release:
`panel R M; continue 2205; panel R 0; continue 4410`.

Opening the kit-edit browser from the main screen, as the example verifiers do:

```text
press 0x22/0x40 (KIT), 0x23/0x20, 0x24/0x10 (UP), 0x23/0x40 (DOWN), 0x24/0x08 (ENTER)
```

The browser state is at ColdFire `0x28B72C` (selected family index) and
`0x28C2D8` (item count of that family). From there DOWN and UP move between
families, RIGHT enters the machine column, and ENTER assigns the machine. The
assigned ID appears at `0x7001AA + 4·t`. `0x281A46 == 23` means kit edit is
still open (leave with EXIT).

### Deadline and cycle measurement: `audio-budget`

`audio-budget reset` clears and enables the counters; `status` reads them
(paused); `off` freezes them.

| Field | Meaning |
| --- | --- |
| `voices[t].total_cycles`, `calls`, `max_cycles` | DSP2 cycles between the render call (`P:0xB4`) and its return (`P:0xB5`), per track |
| `mixer_blocks`, `mixer_period_max` | DSP1 block starts (`P:0x4F`) and the longest interval between them; a missed block shows as ≈ 147,456 |
| `dac_buffer_writes`, `writes_to_active_dac_half` | CPU writes to DSP1 `X:0x400–0x57F`; writes into the half DMA1 is currently reading mean the mixer overlapped playback |
| `tracked_dac_reads`, `stale_dac_reads` | DMA reads of DAC words written since the reset. A stale read is a word read twice without being rewritten: a replayed half, the audible signature of a missed block. |
| `untracked_dac_reads` | Reads of words not yet written since the reset (unused slots) |
| `collision_examples` | Up to 16 active-half writes with PC, address, DMA source, cycle |

Pass criteria used throughout the kit: no active-half writes, no stale reads,
a read count close to 6 × 44,100 per second, and a mixer period close to
73,728. The per-voice cycle averages are for budgeting. They are not the
pass/fail test.

### Timelines: `pclog`

Each record (text, one per line) is:

```text
cpu pc dsp1_cycles dsp2_cycles dsp1_ddr0 dsp1_ddr4 r6 coldfire_cycles
```

- `cpu` is 0 = ColdFire, 1 = DSP1, 2 = DSP2.
- All three counters are sampled at the same moment, so events on different
  processors can be put on one timeline.
- `dsp1_ddr0` / `dsp1_ddr4` are DSP1's ADC and voice-receive DMA positions.
- `r6` is the recording DSP's R6 (the track's state block during renders).

Typical uses:

- **Who waits for whom:** log DSP1 `0x4F 0x7E 0x9A8` and DSP2 `0xB4 0xB5`, then
  compare voice arrival with the block start.
- **Checking a suspected false deadline miss:** log DSP1 `0x9AA 0x9B1` (the
  host-command ISR's wait for the ColdFire's second word) and look for waits
  near one scheduler quantum.
- **Finding which routine is running:** `pclog sample dsp1 16`, then a
  histogram of PCs.

### Python API

```python
import sys; sys.path.insert(0, 'tools/emulator')
from monitor import MonitorClient

with MonitorClient('custom.bin', experimental_firmware=True, log='session.log') as m:
    m.execute('boot'); m.execute('continue 330750')
    m.execute('midi 0xf0 0 0x20 0x3c 2 0 0x5b 1 15 0 2 0xf7')   # track 2 <- ID 15
    m.execute('continue 22050')
    m.execute('cpu dsp2'); bp = m.execute('break call_render')['id']
    stop = m.execute('continue 44100')
    print(m.execute('regs dsp2')['r6'], m.execute('memory Y 0x850 8')['values'])
```

- `execute(command, timeout=None)` sends one command and returns the parsed
  result, or raises `MonitorError` with the monitor's message.
- A command that runs longer than 60 s returns `{'paused': False, …}`; poll with
  `execute('wait 60000')`.
- If a response does not arrive within `timeout` (default 90 s), the session is
  killed rather than risk pairing a late reply with the next request.
- `interrupt()` requests a pause without consuming the pending response.
- Constructor arguments: `load_symbols=False` skips the default symbol file,
  and `executable=` selects another `md-monitor` build.

### JSONL protocol

`--json` reads one request per line, `{"id": 1, "command": "regs dsp2"}`, and
answers `{"id": 1, "ok": true, "result": …}` or `{"id": 1, "ok": false,
"error": "…"}`.

The native `build/md-monitor IMAGE [--experimental-firmware]` speaks a
lower-level form: one textual command per stdin line, one JSON object per
stdout line. The dependencies' own diagnostics go to stderr.

### Stop semantics

The monitor runs the board's scheduler on one worker thread. A breakpoint
blocks inside the instruction callback; nothing unwinds through emulation
code. This keeps nested ColdFire→DSP HI08 catch-up calls intact.

- Only the processor that stopped is guaranteed to be at an instruction
  boundary. `at_instruction_boundary` shows the others; for example, the
  ColdFire can be suspended inside a host-port access.
- `backend_pc` is the DSP JIT's own PC. It can differ from the hook PC during
  REP or interrupts.
- Inspection commands are refused while the machine runs.
- The monitor runs DSP JIT blocks of one instruction for observability.
  Longer blocks were found to change execution in firmware tests, so they are
  not offered.
- Interrupts can run between a `jsr` and its target: break on a routine's
  entry, not on the call site.

## Kernel runner: `md-kernel`

Runs a machine's DSP code with the dispatcher's register contract, with no
firmware or board model involved. It is fast (thousands of blocks per second)
and supports both DSP engines.

```sh
md-kernel --program code.bin --bank 0x127000 \
          --init 0x127000 --update 0x127001 --render 0x12700A \
          --input in.bin --output out.bin --packet 4 \
          [--track 1] [--engine jit|interpreter] [--pool BASE:WORDS] \
          [--load Y:0x148000:table.bin] [--max-steps N]
```

| Item | Format |
| --- | --- |
| `--program`, `--load` files | 3-byte little-endian DSP words |
| Input record (u32 LE) | `flags` (bit 0 call init, bit 1 call update), `packet[P]` (written to `Y:S+1…`), `neighbour[32]` (written to the previous bank `Y:0x100`) |
| Output record (u32 LE) | `output[32]` (bank `Y:0x120`), `state[64]` (`Y:S+0…+0x3F`), `xscratch[32]` (`X:0…0x1F`) |

Per block:

1. Call init if the flag says so.
2. Write the packet and the neighbour input.
3. Clear the pending word and call update if the flag says so.
4. Poison the output bank and call render.

Every call starts with the dispatcher's contract:

| Register or word | Value |
| --- | --- |
| `R6` | `S` |
| `R7` (render) | `0x120` |
| `M7` (render) | `0x1F` |
| Other `Mn` | Linear |
| `Y:0x140` / `0x141` / `0x142` | Bank / `S` / track |
| **All other registers** | **Random** |

Before the run, X memory, low Y memory, the other 15 state blocks and the pool
range are filled with known patterns. Afterwards `md-kernel` checks:

- every X word above `X:0x1F`;
- low Y memory;
- the other tracks' state blocks;
- the scheduler words;
- the program;
- any `--load`ed tables.

Any change fails the run with `guard: …`. On success it prints
`blocks=… worst_block_cycles=… mean_block_cycles=… guards=pass`. Only the JIT
engine counts cycles; the interpreter reports 0. This is the harness for the
"bit-exact kernel test" in [doc 11](11-writing-a-custom-machine.md#7-verify-before-hardware).

## Example verifiers

```sh
python3 tools/examples/gnd-sw/build.py --firmware STOCK.bin --asm dsp56300-asm --output out/sw
python3 tools/examples/gnd-sw/verify.py --build out/sw --firmware STOCK.bin
python3 tools/examples/nfx-gn/build.py --firmware STOCK.bin --asm dsp56300-asm --output out/gn
python3 tools/examples/nfx-gn/verify.py --build out/gn
```

Each `verify.py` runs:

1. **`md-kernel` cases** against the example's `model.py`, in both engines:
   - GND-SW: all MIDI notes, ramp/decay/retrigger, Nyquist clamp;
   - NFX-GN: 512 random blocks on tracks 1, 15 and 0.
2. **A booted run** through the monitor: registration read back,
   front-panel selection, live audio and state equal to the model. For GND-SW
   it also checks stock sample budgets, the E12 removal, a RAM-R1 recording and
   front-panel selection from the RAM family.

`--kernel-only` skips the boot. A full run takes a few minutes.

## Tests

```sh
cmake --build build --target check                                     # no firmware
MD_FIRMWARE=elektron_sps1-1uw_os1.63.bin python3 -m unittest discover -s tests -v
```

The integration tests cover:

- rejection of non-stock images without the opt-in, and checksum validation
  with it;
- ColdFire watchpoints, trace overflow and export;
- the JSON protocol;
- DSP bootstrap breakpoints and DSP MMIO watchpoints;
- side-effect-free inspection;
- boot and guest decompression;
- the SysEx reply, the Tempo panel response, and quiet versus drum audio;
- the workaround switch and asynchronous pause;
- symbols, render breakpoints, `audio-budget` and `pclog`.

## Scheduler quantum

The board model advances the ColdFire and the two DSPs in turn, each for a
quantum of emulated time. A DSP catches up with the ColdFire when the ColdFire
touches its host port, but not the other way round.

DSP1's host-command ISR busy-waits inside the interrupt for the ColdFire's
second word. If the ColdFire's slice ends between its two writes, DSP1 spins
until the ColdFire runs again. With upstream's 125 µs Machinedrum quantum,
that could be about 12.7k DSP cycles: enough to make DSP1 miss its
exact-match block start. The result looked exactly like a real overrun (192
stale DAC reads, a doubled mixer period) at modest loads.

Patch 0002 sets 30 µs, the value upstream already uses for the Monomachine.
That caps the spin at about 3k cycles, removes the false misses, and does not
change emulator speed measurably. `MD_SCHED_QUANTUM_US=N` overrides it for
experiments; it is read once per process.

## Limits

This is a behavioural emulator. What it does not model is listed in
[emulation and verification](16-emulation-and-verification.md#where-the-emulator-is-not-the-hardware),
most importantly:

- no DSP external-memory wait states and no instruction-cache timing, so cycle
  counts are optimistic;
- flash readable at both `0x0` and `0x10000000`, so a CS0 alias bug boots here
  and hangs on hardware. `mdkit check` catches it statically. Dynamically,
  break at `0x2002F2` (after the remap), then `watch r B 0 0x100000` and
  exercise the machine.

The monitor has no reverse execution, checkpoints, conditional breakpoints,
register or memory writes, or GDB remote protocol.

## Updating the pins

1. Check out the new Gearmulator commit in the submodule, and let its nested
   submodules follow (`git submodule update --init source/dsp56300
   source/mc68k`, then `source/asmjit` in dsp56300).
2. Update `PINS` in `setup.py`.
3. Re-create the patches: apply the old ones where they fit, fix the rest,
   and regenerate each with `git diff` in its repository.
4. Run `check`, the monitor tests, both example verifiers and
   `harness.py verify`.
5. Commit the new gitlink together with the patches.
