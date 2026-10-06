# DSP2 voice ABI

This is the contract between DSP2's resident dispatcher and a machine's DSP code.
It was recovered from the dispatcher (`P:0x64–0xE1`) and checked with
breakpoints at every stage entry and return. The kit's example machines rely
on all of it.

## Dispatch and lifetime

DSP2's main loop (`P:0x64`) runs one **producer pass** per audio block. With
`S = 0x800 + 0x40·t`:

```text
for t in 0..15:
    S = 0x800 + 0x40*t;  Y[0x141] = S;  Y[0x142] = t
    pending = Y[S]
    if pending != 0:
        if pending != active[t]:            # active[t] = Y[0x153 + t]
            active[t] = pending             # stored BEFORE init   (P:0x8B)
            call init_table[pending]        # call site P:0x8D
        Y[S] = 0                            # cleared BEFORE update (P:0x97)
        call update_table[active[t]]        # call site P:0x98
    Y[0x140] ^= 0x20                        # alternate between Y:0x100 and Y:0x120
    call render_table[active[t]](R7 = Y[0x140])   # call site P:0xB4
    wait for the previous voice's DMA0 to finish   # P:0xCF
    start DMA0 from Y[0x140] to ESSI0             # P:0xD1-0xD3
```

| Table | Address | Called |
| --- | --- | --- |
| Init | `Y:0x145AF5 + type` | When a trig brings a type different from the active one |
| Update | `Y:0x145BB6 + type` | On every trig (pending word nonzero), after init if init ran |
| Render | `Y:0x145C77 + type` | **Every block**, for every track, whether or not anything changed |

Each table has 193 entries (types 0–192). Unused types point at the fallback
entries (type 0's values): init/update `P:0x10008E` (RTS) and render
`P:0x10008F` (writes 32 zeros after a deliberate padding loop).

Lifetime facts:

- **Update means "trig".** The OS writes the type into `Y:S` only for a trig.
  Knob changes rewrite the packet words `Y:S+1…` without setting it.
- **Init runs only on a type change.** Retriggering the same machine skips init.
  Init must cope with whatever the previous machine left in the 64-word block:
  the dispatcher never clears it.
- **Render runs even without a trig.** A newly assigned machine renders the old
  type's state until its first trig. There is no note-off, no destructor and no
  "voice finished" callback.
- **Assignment latches.** A kit reassignment does not change `active[t]`; the
  old DSP code keeps rendering until the next trig. Removing custom code
  therefore requires clearing matching `active[t]`/pending cells, or retriggering.
- `Y:S` itself belongs to the dispatcher and the host protocol. Never use it as
  state.

## Entry registers

Calls are `JSR` through the tables; return with `RTS`.

| Stage | On entry | Modifiers | Returns to |
| --- | --- | --- | --- |
| Init | `R6 = S`, `R1 = t`, `R0 = new type`, `R2 = target` | `M0–M7 = 0xFFFFFF` (linear) | `P:0x8E` |
| Update | `R6 = S`, `R0 = active type`, `R1 = target`, `X0 = 0` | Linear if init ran; otherwise as set at `P:0x74` (linear) | `P:0x99` |
| Render | `R6 = S`, `R0 = type`, `R1 = target`, **`R7 = output bank`** | `M0–M5 = 0xFFFFFF`, **`M7 = 0x1F`**, **M6 inherited** | `P:0xB5` |

- `N0–N7` are **not** initialized. Never rely on an `Nn` you did not load.
- Render inherits `M6` from earlier code. Set it if you use R6 with an addressing
  mode that `M6` affects. A plain `(r6+n)` displacement is safe with `M6` linear,
  but do not assume it is linear.
- The dispatcher reloads everything it needs from memory between stages. A, B,
  X, Y, R0–R7 and N0–N7 are scratch.
- **Restore** anything global you change:
  - modifier registers left non-linear (`Mn`), because the next machine may
    assume linear;
  - SR mode bits (scaling, rounding, saturation);
  - OMR;
  - interrupt and peripheral configuration.

  `RTS` does not restore SR.
- Balance the hardware stack and finish every `DO`/`REP` loop before returning.
- There is no return value.

Interrupts (host transfers, DMA, ESSI) can run inside your code. Do not assume
exclusive access to peripherals, and keep interrupt latency in mind in very
long uninterruptible sections (`REP`, long `DO` bodies are fine).

## The state block

`Y:S … Y:S+0x3F`, 64 words per track:

| Offset | Owner |
| --- | --- |
| `+0` | Dispatcher/host: pending type marker |
| `+1 … +(n−1)` | **Packet**: control words written by the ColdFire handler (`n` = handler's return count) |
| `+n … +0x3F` | Machine-private state: phases, envelopes, filter memories, ring indices |

The block persists across blocks and trigs, and across machine changes. It is
only reset by your own init.

Conventions used by all custom machines:

- The packet is at `+1…` (one word per converted knob, more if the handler
  needs them; up to `+10` has been used), private state from the next word up.
- Init clears exactly the private words the machine relies on.
- Update resets what a trig should reset (envelopes, phases) and leaves the rest.
- Private state words can be read and written by host tests, which is useful for
  bit-exact verification (see [emulation and verification](16-emulation-and-verification.md)).

## Output

Render must write **exactly 32 signed 24-bit samples** into the bank given in
`R7` (`Y:0x100–0x11F` or `Y:0x120–0x13F`), normally with 32 × `move …,y:(r7)+`
(`M7 = 0x1F` wraps the pointer back). Requirements:

- Write all 32 samples every call, zeros included. A silent machine must still
  write silence.
- Do not write outside the bank, and do not change `Y:0x140`. The dispatcher
  starts the DMA from `Y:0x140`, not from R7.
- All samples must be complete on return. DMA0 reads the bank while the next
  voice renders.
- Output is mono. DSP1 applies track effects, level, pan and routing.

**Level.** Stock generators leave headroom: GND-SIN's oscillator is shifted
right by two (about quarter scale) before its envelope. The custom generators
follow that convention (`>> 2` for a single oscillator), and the effects keep
unity gain at their neutral settings. How much of DSP1's chain (track EQ,
filter, distortion, mixer) tolerates full-scale voices has not been measured;
matching the stock level is the safe choice.

## The neighbour tap

Because the output banks alternate, at render entry the **other** bank holds the
previous track's output **from the same audio block**:

```text
current  = Y[0x140]            # == R7 at entry
previous = current ^ 0x20      # Y:0x100 <-> Y:0x120
input[i] = Y[previous + i]     # track t-1's raw voice, i = 0..31
```

This is the input of every neighbour (NFX) machine. Properties:

- It is the **raw** DSP2 voice, before the source track's DSP1 effects, level,
  pan and sends.
- Reading is safe; DMA0 is reading the same bank. **Never write it.**
- Chains work without extra latency: a neighbour machine on track 3 reads one on
  track 2, which read track 1, all in the same block.
- **Track 0** has no previous track: its "previous" bank holds track 15 of the
  previous block. A neighbour machine should output silence on track 0 (check
  `Y:0x142 == 0`) rather than feeding back.
- Only the immediately preceding track is available. A second earlier track
  would need its own capture.

## Memory a machine may use

| Region | Rule |
| --- | --- |
| Registers | All scratch, subject to the restore list above |
| `Y:S+n…+0x3F` | Your private state |
| Your P bank (e.g. `0x127000–0x127FFF`) | Code, and constant tables read through the Y alias |
| `X:0x000–0x01F` | Scratch **within one call** (stock renderers use it; nothing survives between calls) |
| The delay-pool slice `0x190000 + 0x2000·t` (or `0x104000 + 0x2000·t` in the [E12 layout](02-memory-maps.md#custom-machine-regions)) | Per-track ring memory, in builds that have a delay pool |
| Sine table `X/Y:0x148000` | Read-only, 32,768 entries |
| Internal `X:0x257–0x6FF` | Read-only tables uploaded with the DSP2 stream, shared by convention; the shared tanh table is `X:0x280–0x680` (see [DSP programming](13-dsp-programming.md#internal-memory-tables)). Never written at run time. |
| Everything else | Not yours: dispatch tables, `Y:0x140–0x162`, other tracks' blocks, sample memory, DMA buffers, MMIO |

Rules for pool memory:

- Index by track so instances never share a ring.
- Keep a valid-sample counter instead of clearing thousands of words on init;
  return zero for history that has not been written yet.
- Wrap with modulo addressing or explicit masking. Restore `Mn` to linear before
  returning.

## Timing

The whole producer pass (16 renders plus dispatch, updates, interrupts and DMA
waits) must fit in **73,728 cycles**. Planning numbers from the emulator:

| Item | Cycles per block |
| --- | --- |
| Idle track (fallback renderer, NOP padding) | 3,438 |
| GND-SIN | ~700 |
| Stock TRX/EFM/E12/P-I voices | ~2,000–3,200 |
| GND-SW (example) | 734 default, 2,341 worst |
| NFX-GN (example) | 154 |

### The voice-link slot floor

A render's cost is not the whole story. After each render the dispatcher waits
for the **previous** voice's DMA0 transfer to finish (`P:0xCF`) before it starts
this voice's (`P:0xD1`). One 32-word voice block takes about **2,900–3,000
cycles** to cross ESSI0 to DSP1 (measured in the emulator with breakpoints at
`P:0xB4`, `P:0xB5` and `P:0xD1`):

| Track | Render | Wait for the link | Slot |
| --- | ---: | ---: | ---: |
| ~700-cycle source after a ~6,400-cycle voice | 796 | ~2,380 | ~3,300 |
| 44-cycle render (32 zeros) after a ~6,400-cycle voice | 44 | ~2,850 | ~2,990 |
| ~3,300-cycle render | ~3,300 | 12 | ~3,400 |

Here the slot is the time from one transfer start to the next. So every track
costs at least about 3,000 cycles of the pass, however cheap its render. The
budget is

    sum over the 16 tracks of max(render + ~100, ~3,000)  <=  73,728

not the sum of the renders. The fallback renderer's 3,438-cycle padding is about
one transfer time. A kit of cheap renders gains nothing below the floor. A
machine needing more than one slot's worth only gains from a second track if it
puts real work into that track's render (see
[DSP programming](13-dsp-programming.md#budgeting)).

**Measured kits.**
- Kits whose render calls sum to about 62.5k cycles have met the deadline in
  the emulator.
- A kit whose renders summed to only 50k missed about 3.5 % of blocks. Its
  seven ~6,400-cycle voices were each followed by a 44-cycle one, and every
  short one still cost a full transfer slot.

**On hardware (2026-10-05)** the floor is about **3,100** cycles. Fifteen
plain oscillators, fifteen PWM oscillators and seven two-track reverbs (with a
generator) left exactly the same load-meter headroom, because every slot sat
on the floor. The 16 slots together can use about 68k cycles per block.

The safe total on hardware is lower: the emulator charges no external-memory
wait states and no pipeline interlocks. Leave generous headroom and measure worst cases, not averages.
