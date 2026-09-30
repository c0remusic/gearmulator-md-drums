# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Gearmulator is a low-level IC emulator that recreates classic virtual analog synthesizers (Access Virus, Waldorf microQ/XT, Clavia Nord Lead 2x, Roland JP-8000, Elektron Machinedrum/Monomachine) by emulating original DSP56300 and MC68K processors and running authentic firmware ROMs as audio plugins (FST, VST3, AU, CLAP, LV2).

## Build Commands

**Current dev setup uses `temp/cmake_vs22` with Visual Studio 2022.**

```bash
# Configure (Windows)
cmake . -B temp/cmake_vs22 -G "Visual Studio 17 2022"

# Build (use Debug for quick compile checks, Release for full optimization)
cmake --build temp/cmake_vs22 --config Debug -j 4
cmake --build temp/cmake_vs22 --config Release -j 4

# Package
cd temp/cmake_vs22 && cpack -G ZIP

# Run tests
ctest -C Release
```

Gate before commit/push (MD/MM): the targeted build and ctest list in `.scratch/parallel-transport/implementation-log.md` § "Commandes"; ctest only if build_exit=0.

Firmware tests exit 77 (ctest: Skipped, not failed) without `GEARMULATOR_MD_FIRMWARE_BIN` / `GEARMULATOR_MM_FIRMWARE_BIN`: set both (paths in the log § "Commandes") or the gate proves nothing.

Run firmware executables by hand only under a wall-clock guard (`Start-Process -PassThru` + `WaitForExit(ms)`, kill on timeout).

`jucePluginLib/pluginVersion.cpp` recompiles on every build (version timestamp, MSB8065 by design): a build with no changes still compiles one file.

Per-synth CMake flags: `-Dgearmulator_SYNTH_OSIRUS=ON`, `_OSTIRUS`, `_VAVRA`, `_XENIA`, `_NODALRED2X`, `_JE8086`, `_ELEKTRON` (all default ON). Plugin format flags: `gearmulator_BUILD_JUCEPLUGIN`, `_CLAP`, `_LV2`, `gearmulator_BUILD_FX_PLUGIN`.

Convenience scripts: `build_win64.bat`, `build_linux.sh`, `build_mac.sh`.

Worktrees (`.claude/worktrees/*`) have no submodules or build dir: `git -c protocol.file.allow=always -c submodule.<name>.url=<main>/.git/modules/<path> submodule update --init --recursive`, then configure a fresh `temp/cmake_vs22`.

`#ifdef DSP56K_TSC_PROBES` code: configure with `-DDSP56K_TSC_PROBES=ON` and compile one file via `MSBuild <proj>.vcxproj -t:ClCompile -p:Configuration=Release -p:Platform=x64 -p:SelectedFiles=<abs path>` (one file per call).

## Architecture

**Emulation stack:** DSP56300 emulator (`source/dsp56300/`, JIT via asmjit) + MC68K emulator (`source/mc68k/`, Musashi) form the core. Each synth has a device library that loads firmware ROMs, initializes processor memory, and handles MIDI/audio via HDI08.

**Per-synth pattern:**
| Emulator | Hardware | Device Lib | Plugin Dir |
|---|---|---|---|
| Osirus | Virus A/B/C | `virusLib/` | `osirusJucePlugin/` |
| OsTIrus | Virus TI/TI2/Snow | `virusLib/` | `osTIrusJucePlugin/` |
| Vavra | Waldorf microQ | `mqLib/` | `mqJucePlugin/` |
| Xenia | Waldorf MW II/XT | `xtLib/` | `xtJucePlugin/` |
| Nodal Red 2x | Nord Lead/Rack 2x | `nord/n2x/` | `nord/n2x/n2xJucePlugin/` |
| JE-8086 | Roland JP-8000 | `ronaldo/je8086/` | `ronaldo/je8086/jeJucePlugin/` |
| Gearmulator MD / MM | Elektron Machinedrum / Monomachine | `elektron/md/mdLib/` | `elektron/md/mdJucePlugin/` |

**Shared libraries:**
- `synthLib/` — Device base class, DAC, resampling, MIDI routing
- `jucePluginLib/` — Parameter system, MIDI Learn, Patch Manager, program change routing
- `jucePluginEditorLib/` — Plugin editor UI, parameter overlays, settings pages
- `juceRmlUi/` — RmlUi integration (HTML/CSS-like UI framework)
- `baseLib/` — Filesystem, logging, events, binary streams
- `hardwareLib/` — LCD, buttons, encoders abstractions

**Plugin build flow:** `createJucePluginWithFX()` macro in `source/juce.cmake` → links device lib → Processor inherits `synthLib::Plugin` wrapping `synthLib::Device` → skins via RML/RCSS compiled into binary data.

## Code Conventions

- **Tabs for indentation** (tab size 4, UseTab: Always), 120 char column limit
- **Braces on new lines** for all constructs; namespace content indented
- **Naming:** PascalCase classes, camelCase functions/vars, `m_` member prefix, `_` parameter prefix (`void func(int _param)`)
- **Namespaces:** camelCase (`virusLib`, `synthLib`, `dsp56k`)
- **Early returns** preferred over deep nesting
- `.clang-format` in `source/` directory
- C++17 required

## Git Conventions

- Do NOT include `Co-authored-by` trailers in commit messages
- Do NOT commit without explicit user approval
- Git remote: `origin` (github.com/c0remusic/gearmulator-md-mm)
- Parallel Claude sessions (`.claude/worktrees/*`, sometimes the main checkout) merge and push `release/md-mm-alpha` too: `git fetch` + `git worktree list` before a follow-up or push; stage with explicit pathspecs only
- DSP submodule (`source/dsp56300/`) is also owned by user — changes there are fine

## Key Build Files

- `base.cmake` — Compiler flags, platform-specific optimization settings
- `source/juce.cmake` — JUCE plugin configuration and multi-format support
- `source/skins.cmake` — Skin asset compilation
- `scripts/Jenkinsfile` / `JenkinsfileMulti` — Private CI (Jenkins)
- `.github/workflows/cmake.yml` — Public CI (GitHub Actions)

## Where to Make Changes

1. **Device-level changes** → `<synth>Lib/` (e.g., `virusLib/device.cpp`)
2. **Plugin UI** → `<synth>JucePlugin/` (RML/RCSS for layout, processor for logic)
3. **Shared plugin infra** → `jucePluginLib/` or `synthLib/`
4. **DSP emulator** → `dsp56300/source/dsp56kEmu/`
5. **New parameter** → update `parameterDescriptions_*.json`, map MIDI in processor, update skin RML

## Critical Implementation Details

- **State save/restore:** Device `getState()` MUST append to `_state` vector with `insert()`, never `assign()` — the Plugin layer prepends version headers
- **RmlUi threading:** DOM modifications MUST happen on JUCE message thread. Use `juce::MessageManager::callAsync` from audio/MIDI callbacks
- **callAsync safety:** Use static instance-set pattern to guard lambdas against use-after-free
- **Voice expansion (Xenia/Vavra):** Multiple DSP56300 instances connected via ESSI1 ring bus; main DSP is last (`g_mainDspIdx = g_dspCount - 1`)
- **Environment overrides (`MD_*`, `MDMM_*`):** read through `mdLib/mdenv.h` (`envOverride`, `envNumber`, `envCount<T>`); unset, empty and unparsable count as unset. Never raw `getenv` + `atoi`/`strtoul`: PowerShell's `SetEnvironmentVariable($n, $null)` leaves an empty variable children see.

## Detailed Reference

See `.github/copilot-instructions.md` for comprehensive documentation on MIDI Learn, Patch Manager, program change routing, Jenkins CI details, YouTrack workflow, release process, and voice expansion internals.
