# Screens: what the engine can draw for Filter and EQ, and for the LFO

Research for ticket `issues/05-reponses-ecrans.md`. The question: what can the engine give the EFX band's Filter and
EQ screen and the ROUTING band's LFO screen from its own calculation, instead of the mockup's models
(`source/elektron/md/mdDrumsPlugin/Design/HANDOFF.md:93-101`, `:281-282`)?

Sources: mdEngine's code in this repository, its upstream notes (`.scratch/uc-less-engines/machinemodule/docs_PROTOCOL.md`),
and the OS 1.63 image itself. I decoded the image from the user's flash file with a Python port of
`source/elektron/md/mdEngine/tools/mdfw/Firmware.cpp:169-186` and disassembled the 68k routines with capstone, in the
session scratchpad only. OS addresses below are cited as "OS $xxxxxx". The disassembly is firmware-derived and is not
reproduced here (the rule of `C:\dev\mpc-vst-machinedrum\tools\mddis\mddis.cpp:3`). Nothing was built or run against the
engine. The numbers in the frequency table and the LFO period table come from my Python evaluation of the OS's own
table words and formulas; a C++ test still has to confirm them against `TrackFx`.

## Summary

- **Filter and EQ: the engine can give the true response.** The filter and the EQ are linear, recursive and
  fixed-point, and every coefficient comes from OS tables that `TrackFx::Tables` already holds. `TrackFx` already has
  the hooks to run part of the chain on a test signal (`stopAfter`, `scratch()`). An impulse through a private `TrackFx`
  on the editor's thread, followed by an FFT, gives the bit-exact magnitude response. At ~1.3 µs per 32-sample block,
  that is well under a millisecond per redraw.
- **The code comments swap the two filter sections.** The section at FLTF + FLTW (`filter1`) is a 4-pole low-pass, and
  the section at FLTF (`filter2`) is a high-pass. The comments in `TrackFx.cpp` and `docs_PROTOCOL.md` say the reverse.
  This changes nothing in the sound; it only matters for labelling the screen.
- **LFO: the OS has no shape tables.** It has six small routines, reached through a pointer table, plus a decay step
  and a mix formula. All of them are decoded below. A C++ port of a few dozen lines, checked bit-exact against the OS
  routines in MachineRunner, draws the MD's real wave without touching the engine. Several facts differ from both
  models: the saw falls twice per period, RMP and EXP are one-shots, RND swings half as far, and LFOM fades into the
  *inverse* of shape 2.
- **RND is silent in mdEngine.** Its generator state is never seeded, so RND outputs a constant offset. This is an
  engine gap, separate from the screen.
- **What stays a model:** AMD, SRR and DIST (not linear, not drawn), one realisation of RND, and the static view of TRIG
  and HOLD, which depend on when the track is hit.

## 1. Filter and EQ

### The chain, as the code computes it

`TrackFx::process` runs prep, AMD, EQ, filter 1, filter 2, SRR and DIST, in that order (`source/elektron/md/mdEngine/engine/TrackFx.cpp:531-550`).
The inputs are the 9 effect words AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST, each `value << 7`
(`source/elektron/md/mdEngine/engine/TrackFx.h:19-24`).

- **Prep** (`TrackFx.cpp:72-89`):
  - `y[$22]` = FLTF·16, with its resonance `y[$23]` = FLTQ forced to 0 when FLTF = 0 (`:79`).
  - `y[$24]` = min(FLTF·16 + FLTW·16, $7ff), with its resonance `y[$26]` = FLTQ forced to 0 when FLTW·16 ≥ $7ef,
    that is FLTW = 127 (`:84-88`).
- **Filter 1, on `y[$24]`** (`TrackFx.cpp:234-332`):
  - A cosine-like word `k = T[$1402aa + y24 + T[$141f00 + q]]` and a radius word
    `r = T[$141700 + y24 + T[$141f80 + q]]` (`:240-250`).
  - The targets kept in the track's state: `y[$2a]` = 2rk − 1, `y[$2b]` = −r², and `y[$2c]` = 1 − 2rk + r²
    (`:252-277`).
  - The recursion (`:299-321`) runs two accumulators. Neither is ever cleared: each carries its previous output with
    weight 1, and the 2rk − 1 word supplies the remainder. Each sample is therefore
    y[n] = 2rk·y[n−1] − r²·y[n−2] + (1 − 2rk + r²)·x[n], and the second accumulator takes the first one's output as
    its input.
  - The result is two identical all-pole sections in cascade: a **4-pole low-pass** with unity gain at DC. It has one
    input tap, so no zeros, and its gain is normalised at DC.
  - Resonance is removed when FLTW = 127, which is the open default FLTF 0 / FLTW 127
    (`source/elektron/md/mdDrums/mdDrumsEngine.cpp:57`).
- **Filter 2, on `y[$22]`** (`TrackFx.cpp:337-453`):
  - The same k and r lookups (`:343-358`). The gain word is (1 + 2rk + r²)/8 (`:359-364`), which normalises at
    Nyquist.
  - The numerator taps g, −2g, g, ramped into Y:$60 (`:389-405`) and used per sample in `:434-445`, form (1 − z⁻¹)²:
    zeros at DC. So this is a **high-pass** at FLTF, and its resonance is removed when FLTF = 0.
- **Labels:** the comments at `TrackFx.cpp:231` and `:334` call filter 1 "high-pass" and filter 2 "low-pass", and so
  does `docs_PROTOCOL.md:367-368`. The arithmetic says the opposite, and so do the MD's controls: FLTF is the band's
  base and FLTW its width (`source/elektron/md/mdJucePlugin/mdCurveView.cpp:188-191` draws it that way too). The
  translation stays bit-exact either way (`docs_PROTOCOL.md:379-382`).
- **EQ** (`TrackFx.cpp:117-219`):
  - Two (k, r) pairs: `T[$14221d + EQF·16 + T[$143476 or $1434f6 + EQG]]` and
    `T[$142c76 + EQF·16 + T[$143576 or $1435f6 + EQG]]` (`:129-165`).
  - The first pair builds the denominator and the second the numerator. The numerator is then scaled by D(1)/N(1)
    through the 24-step division (`:170-193`), which gives unity gain at DC and a bell at the pairs' angle.
- **Coefficient ramp:** the filter coefficients ramp across one block from their previous values, using the X:$648 or
  X:$668 shapes (`TrackFx.cpp:279-296`, `:378-405`). The increments of each shape add up to about 1 across the
  32 samples. A coefficient therefore reaches its target by the end of the block in which its parameter changed. The
  EQ is recomputed every block with no ramp (`:124-127`).

### What is reachable today

- **Tables.** The tables are read from the OS file once, and the 32,768-word sine is rebuilt by the boot's own
  recursion (`TrackFx.cpp:22-56`). They never change after construction.
  - They are a private member of `EngineT` with no accessor (`source/elektron/md/mdEngine/engine/MdEngine.h:159`).
  - `mdDrums::Engine` does not keep the firmware after constructing the engine
    (`source/elektron/md/mdDrums/mdDrumsEngine.cpp:35-42`).
  - So the editor needs either an accessor to the immutable `Tables` (safe to read from any thread) or its own copy.
    A copy is 64 K + 2 K words, about 264 KB (`TrackFx.h:28-38`, `TrackFx.cpp:22`).
- **Per-track state.** The state is private too (`MdEngine.h:163`).
  - It holds the low-pass and high-pass targets (`y[$2a-$2c]`, `y[$2e-$30]`).
  - It does not hold the EQ coefficients, which live in a local array (`TrackFx.cpp:124`).
- **Measurement hooks.**
  - `TrackFx::stopAfter` and `scratch()` exist "for stage-by-stage comparison with the DSP" (`TrackFx.h:48-54`).
  - After filter 2, the block sits in X:$2-$21, which is SRR's input (`TrackFx.cpp:455-456`).
  - Each `TrackFx` instance has its own scratch, so one per thread is safe (`MdEngine.h:85-88`).
- **Live inputs.** The words the engine actually used, after smoothing and LFO, are `HostModel::mixerInput(t).fx`
  (`source/elektron/md/mdEngine/engine/HostModel.h:67-73`). They are written by the audio thread's tick, so another
  thread can only see them through a published snapshot.

### Computing a response away from the audio thread

- **A. Measure the engine's own code (recommended first).**
  1. Create a private `TrackFx` over the engine's `Tables`.
  2. Initialise a fresh `State` with `TrackFx::init` (`TrackFx.cpp:58-64`), then call `setParams` with the shown
     track's knob values `<< 7` and AMD forced to 0.
  3. Set `stopAfter = Stop::AfterFilter2`, so SRR and DIST do not run.
  4. Run two silent blocks so the coefficient ramp lands, then an impulse. Use an amplitude near 2^19 to leave
     headroom for +18 dB of resonance and +15 dB of EQ.
  5. Read `scratch()[2..33]` after each block, then FFT. JUCE's FFT is already linked in this tree
     (`source/xtJucePlugin/CMakeLists.txt:58` links `juce::juce_dsp`).
  6. Divide by the response at the neutral settings (FLTF 0, FLTW 127, FLTQ 0, EQG 64), so that "0 dB" means "what an
     untouched track does". This avoids depending on the sections' internal scaling, for example the /8 in
     `TrackFx.cpp:363`.

  **Cost.** The whole chain costs about 2.9 % of a core for 16 tracks (`docs_PROTOCOL.md:384`), which is about 1.3 µs
  per track-block. 8,192 samples take ~0.35 ms and 16,384 take ~0.7 ms, plus the FFT. The low-frequency poles set the
  length: at FLTF 0 the high-pass radius is 0.99929, so its impulse response rings for tens of thousands of samples.
  Below 20 Hz that is off-screen, but truncation ripple can show at the low edge, and the build should measure it.

  **What it costs the engine:** no lock and no snapshot. Only the knob values and the `Tables` are needed.

- **B. Evaluate the same table words in floating point.** Compute the low-pass as (g/D(z))², the high-pass as
  (g'(1 − z⁻¹)²/4D(z))², and the EQ as (D(1)/N(1))·N(z)/D(z), from the k and r words above. This takes microseconds,
  but it rests on my reading of the recursion. It is worth having once a test proves it against A over random settings,
  for example if A's cost shows up while dragging a knob.
- **C. Live response (optional).** Run A or B on the fx words the audio thread publishes once per tick
  (`mixerInput(t).fx`). The curve then moves with an LFO on FLTF and with the OS's smoothing. The HANDOFF does not
  ask for it.

### What the tables say (float evaluation, to be confirmed by A)

| Value | High-pass −3 dB at FLTF = v (FLTQ 0) | Low-pass −3 dB at FLTF + FLTW = v (FLTQ 0) | EQ centre at EQF = v |
|---|---|---|---|
| 0 | 10 Hz | below 5 Hz | 20 Hz |
| 32 | 91 Hz | 18 Hz | 112 Hz |
| 64 | 727 Hz | 138 Hz | 631 Hz |
| 96 | 5.4 kHz | 1.1 kHz | 3.55 kHz |
| 112 | 11.5 kHz | 3.3 kHz | 8.4 kHz |
| 127 | 15.6 kHz | 12.4 kHz | about 19-21 kHz (the bell warps near Nyquist) |

- **The two edges sit far apart.** Both sections read the same pole tables, but four real poles reach −3 dB well
  away from the pole frequency. At equal values, the low-pass edge is about 2.3 octaves below the high-pass edge, so a
  narrow FLTW gives a thin, quiet band.
- **Resonance.** FLTQ 127 raises a peak of about +18 dB; FLTQ 64 gives about +5 dB.
- **The open filter is not flat.** With FLTF 0 and FLTW 127, the response is about −0.6 dB at 20 Hz, −2.2 dB at 10 kHz
  and −4.7 dB at 20 kHz.
- **EQ gain and the EQ point.**
  - EQG spans about ±15 dB, linear in dB at ~0.24 dB per step. EQG 64 gives about +0.3 dB, so it is not exactly flat.
  - The centre follows EQF at about 12.8 steps per octave. The angle of the first pair,
    `acos(T[$14221d + EQF·16 + T[$143476 + EQG]])`, matches the bell's peak within about 2 %.
  - So the screen can place the "EQ point" at x = that frequency, which is the engine's own data, and at y = the
    measured gain there.

### The mockup's and Gearmulator MD's models, for comparison

- **v22 mockup.**
  - It maps every edge as 20·1000^(v/127) and draws 4th-order Butterworth magnitudes, with resonance as Gaussian bumps
    of up to ×4 at each edge (`source/elektron/md/mdDrumsPlugin/Design/v22-ui.js:641-647`).
  - Its EQ is a ±12 dB Gaussian bell (`:642`, `:648`), and the point is drawn at (EQF, EQG) on that model (`:653`).
  - Its EQF mapping is close to the table's: 653 Hz against 631 Hz at 64. Its EQ gain is 3 dB short, and its low-pass
    edges are far too high: 653 Hz against 138 Hz at 64.
- **Gearmulator MD.** `mdCurveView` uses textbook analog 2-pole low-pass and high-pass curves, Q from 0.5 to 8, and a
  Gaussian ±18 dB bell (`source/elektron/md/mdJucePlugin/mdCurveView.cpp:26-54`, `:186-200`). It says so itself: "not
  measured frequencies or times: the plug-in does not know the firmware's scales"
  (`source/elektron/md/mdJucePlugin/mdCurveView.h:29-32`).

## 2. LFO

### Where it runs

- **Settings.** `HostModel::setLfo` writes bytes 0-4 of the OS's LFO struct `$1000f8c + $24·k`: destination track,
  destination parameter, shape 1, shape 2 and type (`source/elektron/md/mdEngine/engine/HostModel.cpp:71-80`,
  constants at `:16-28`).
- **Each tick.** The tick calls the OS's smoothing, LFO oscillator, LFO apply and level smoothing (`HostModel.cpp:254-258`).
- **At a trigger.** A trigger sets the LFO's flag at struct + 5 (`HostModel.cpp:155-156`). The voice update then calls
  the waveform routine and the one-LFO apply before the machine function (`HostModel.cpp:187-193`).
- **Tick rate.** mdEngine ticks every 11 blocks of 32 samples, which is 125.3 Hz (`HostModel.h:80-82`,
  `HostModel.cpp:274`). The MD's own tick is CPU-bound at about 120 Hz and varies with load (`docs_PROTOCOL.md:246-254`).

### The LFO struct (decoded; 36 bytes each)

| Offset | Content | Source |
|---|---|---|
| +0 … +4 | destination track, destination parameter, shape 1, shape 2, type (bit 0 TRIG, bit 1 HOLD) | `HostModel.cpp:74-79`; the OS's SET LFO PARAM accepts track ≤ 15, parameter ≤ 23, shapes ≤ 5, type ≤ 2 (OS $20584e-$20591a) |
| +5 | trigger flag | `HostModel.cpp:156`; cleared by OS $204d52-$204d54 |
| +6, +7 | RND's current eighth of the period, per shape | OS $204f74-$204f7a, $204fce-$204fe0 |
| +8, +$c | shape 1 and shape 2 outputs, current | OS $204d78-$204f2e (written as `$1000a2c + 4·(9k + n) + $568`) |
| +$10, +$14 | shape 1 and shape 2 outputs, held (what the apply reads) | OS $204d46-$204d4c; `docs_PROTOCOL.md:164-166` |
| +$18, +$1c | RND generator state | OS $204f7e-$204fb0 |
| +$20 | phase, 0-$7fff | OS $100011a-$100012a |

Kit dumps carry all 36 bytes per track, settings and state together
(`source/elektron/md/mdLib/mdsysexautomation.cpp:1288-1298`, `.scratch/md-editor/README.md:126`). The OS copies all 36
bytes into the struct when a kit loads (OS $20a042-$20a074).

### Shapes: six routines, no table

The routines are reached through a pointer table at OS $2523ee. Entries 6 and 7 point to the triangle; the OS refuses
shapes above 5 (OS $2058aa-$2058e6). Below, ±$4000 = ±1 and P is the phase.

- **TRI** (OS $204d78): rises from 0 to +1 at P = $2000, falls to −1 at $6000, and returns to 0. One cycle per period.
- **SAW** (OS $204e0a): $4000 − 2P for P < $4000, then $c000 − 2P. It falls from +1 to −1 **twice** per period.
- **SQR** (OS $204e7a): +1 for the first half of the period, −1 for the second.
- **RMP** (OS $204eda): set to +1 when its track is triggered. The oscillator then lowers it by inc/8 each tick and
  stops at 0 (OS $1000144-$100017e). It is a one-shot linear fall, unipolar, lasting 4 phase periods. Until the first
  trigger it stays at 0.
- **EXP** (OS $204f06): set to +1 at a trigger. The oscillator multiplies it by (1 − inc/65536) each tick
  (OS $100015c-$100017e). It is a one-shot exponential decay with a time constant of 2 phase periods, unipolar.
- **RND** (OS $204f32):
  - It draws a new value whenever P >> 12 changes, which is 8 steps per period. In TRIG mode it also draws one at
    each trigger.
  - The value is (s & $3fff) − $2000, where s comes from the additive generator s ← s + s_previous on the two state
    words. That is ±0.5, half the other shapes' swing. Both shape slots share the generator.
- **Waveform routine** (OS $204c94):
  - TRIG together with a trigger resets the phase (OS $204cce-$204ce0). The routine then runs shape 1 and shape 2,
    passing each the trigger flag.
  - On a trigger it copies current to held and clears the flag. Otherwise it copies only if the type is not HOLD
    (OS $204d1c-$204d6e).
  - So HOLD samples the running LFO at each hit of the LFO's own track, and FREE runs on. RMP and EXP restart at every
    trigger in every mode, because their routines test only the flag.

### Speed

The oscillator is OS $1000088. For each LFO k it computes a phase increment from track k's LFOS as the voice array
holds it, after smoothing and any LFO (OS $10000a6). T is the tempo factor BPM × 24 at $100150c (`HostModel.cpp:20`,
`:66-69`).

- Up to $1fff: inc = ((v·16 + 16)·T + $3c8fe) / $791fd.
- Above: inc = (v⁴ >> 40)·T / $4099, a quartic. `docs_PROTOCOL.md:170` says "cubic", but the code multiplies by v
  four times.
- Each tick, phase = (phase + inc) & $7fff (OS $100011a-$100012a).

Periods at 120 BPM with mdEngine's 7.98 ms tick (smoothed LFOS settles 3 below `v << 7`):

| LFOS | 1 | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---|---|---|---|---|---|---|---|---|
| ticks per period | 2731 | 172 | 86 | 57 | 43 | 19 | 9.1 | 4.9 | 3.0 |
| seconds | 21.8 | 1.38 | 0.69 | 0.46 | 0.34 | 0.15 | 0.073 | 0.039 | 0.024 |

The values change once per tick. Above about LFOS 96 a period is under 10 ticks, so the plot should show the per-tick
steps, which is all the engine produces, rather than a smooth wave.

### Mix and depth

The apply routine is OS $1000332 for all 16 LFOs, and OS $10001e8 for one LFO at a trigger.

- **Mix.** lfo = ((\$3f80 − M)·S1 − M·S2) >> 14, where M is the LFO track's smoothed LFOM and S1 and S2 are the held
  outputs (OS $100035a-$1000376).
- **Depth.** dest = clamp(cur + (lfo·D) >> 14, 0, $3fff), where D is the smoothed LFOD and cur is the destination's
  smoothed value (OS $1000378-$10003ba). `docs_PROTOCOL.md:164-166` gives the same formula.
- **LFOM fades from shape 1 into −shape 2.** At LFOM 127 the wave is shape 2 inverted. Two equal shapes cancel near
  LFOM 64: TRI with TRI goes flat.
- **LFOD 127 swings about ±126 knob steps** for a full-scale shape, and about ±63 for RND.
- **Several LFOs can reach one destination.** The LFOs apply in order 0-15 onto the same working array, so their
  modulations add up. The un-modulated values are then restored (OS $10003d2-$100042e).

### What a screen can do

- **Static wave (the HANDOFF's plot).**
  - Port the arithmetic above to C++ on the editor's side: the six shapes, the two decays, mix, depth, clamp and speed.
    It needs only the knob values, the LFO settings and the tempo, so no engine state and no lock.
  - Prove it bit-exact against the OS routines, called through `MachineRunner::call` and `peek`
    (`source/elektron/md/mdEngine/engine/MachineRunner.h:44-55`), in a test. That is the same discipline as `TrackFx`
    against the DSP.
  - The plot's "highest and lowest values reached" are then the min and max of `dest` over the drawn window.
  - For RMP and EXP, the window has to span the one-shot: 4 periods for RMP, several time constants for EXP.
- **Calling the OS routines from the editor.** This would need a second `MachineRunner` with its own copy of the OS
  image (`source/elektron/md/mdEngine/engine/MachineRunner.cpp:67-87`). Its oscillator steps all 16 LFOs at once
  (OS $1000088-$10001da), which makes it heavier than the port for the same result.
- **Live trace (optional).** The phase, the held outputs and the modulated destination are in the 68k memory. The
  audio thread can read them through `EngineT::os()` and `peek*` (`MdEngine.h:59`, `MachineRunner.h:50-55`) and
  publish them once per tick. `mdDrums::Engine` exposes none of this, nor `setLfo` (`mdDrumsEngine.h:41-66`).
  Exposing the LFO settings is new work in any case (HANDOFF's `t<n>_lfoTrack` … `t<n>_lfoMode`, `HANDOFF.md:254-256`).

### Engine gap found on the way: RND never moves in mdEngine

`setLfo` writes bytes 0-4 only (`HostModel.cpp:74-79`). `MachineRunner` copies the OS image's SRAM part, which ends at
$1000a29 (`MachineRunner.cpp:73-77`), and sets just 16 boot bytes at $1001500 (`:78-84`). So the RND state words at
struct + $18 and + $1c start at zero, and an additive generator started at (0, 0) stays at 0. By the decoded
arithmetic, RND therefore outputs a constant −$2000, a fixed negative offset of half the depth. This is not verified by
running.

On the MD these words come from the kit's LFO block (OS $20a042-$20a074). A kit load that copies all 36 bytes seeds the
generator (tickets 04 and 10). An explicit nonzero seed would do as a stopgap.

### The mockup's and Gearmulator MD's models, for comparison

- **v22 mockup** (`v22-ui.js:666-677`):
  - Depth is LFOD/127 × 64 each way; the engine gives ±126.
  - It shows 1 to 6 periods by LFOS.
  - It mixes as (1 − m)·S1 + m·S2; the engine uses −S2.
  - Its TRI starts at −1; the engine's starts at 0, rising.
  - Its SAW falls once per period; the engine's falls twice.
  - Its RMP rises; the engine's is a one-shot fall.
  - Its EXP is periodic and bipolar; the engine's is a one-shot decay.
  - Its RND is 2 steps per period at full swing; the engine's is 8 steps at half swing.
- **Gearmulator MD.** `mdLfoView` draws shapes traced by a probe (`source/elektron/md/mdJucePlugin/mdLfoView.cpp:110-134`)
  and the same linear crossfade (`:149`, `:157`). It shares the saw, one-shot length, RND and LFOM-sign differences.
  It is md-mm's plug-in and out of this map's scope (`.scratch/md-drums-editor/map.md:59`).

## 3. What stays a model

- **AMD:** amplitude modulation by the sine table at AMF (`TrackFx.cpp:91-115`). It shifts the spectrum and has no
  magnitude response; the HANDOFF does not draw it.
- **SRR:** a sample-and-hold on a phase accumulator (`TrackFx.cpp:455-484`). It aliases and is not linear. The
  HANDOFF's note "SRR n" is the right treatment (`HANDOFF.md:93-94`).
- **DIST:** a level-dependent saturation through the limiter, followed by a first-order filter whose coefficients
  depend on DIST (`TrackFx.cpp:486-529`). It is left out of the Filter and EQ screen.
- **RND:** a drawn sequence is one realisation. It is the engine's own sequence only if seeded from the live state
  words, which are zero today.
- **TRIG and HOLD:** they depend on when the LFO's track is hit. A static plot shows a free-running window; only a live
  trace shows the real restarts and holds.
- **Time axis:** exact for mdEngine's fixed 125.3 Hz tick, approximate for a hardware MD (about 120 Hz, varying).
- **The frequency and gain figures above:** they come from my reading of the code and need confirming. The measured
  response (A) needs no reading and is the reference.
