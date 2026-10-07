# MD Drums UI spec, v12

v11 (rules over every row, page titles alone), with the user's questions on v11: "Comment on explore les samples ?"
and, on the ROM machines, "Cacher ROM". Mockup: `v12-test.html` (`v12-ui.css`, `v12-ui.js`, `v6-machines.js`).
Everything not listed here is as in `v11-ui-spec.md` and the specs before it.

## What the samples are

Measured on the full firmware (OS 1.63, `mdTrigLatencyFirmwareTest --hits`, one hit at velocity 100 on output A):

| Machines | What they play | Measured |
|---|---|---|
| E12 (16) | 12-bit samples that come with the OS (a 21-entry descriptor table in DSP2's program, `P:$103D7B`) | E12-BD peak -13.1 dBFS, below -40 dB of its peak after 190 ms; SD, CH, BC decay the same way |
| ROM-01..48 | The UW bank: 48 slots for the owner's own samples (SDS transfers, 4-character names by SysEx `$73`) | ROM-01 to -32 give a flat buzz (rms -36 to -65 dBFS, no decay over 1 s), ROM-33 and -48 silence (-138 dBFS). **Corrected in v15:** the bank is not empty; it sits at 0x100000 (magic 0xABCD, count 32, about 970 KiB of 16-bit audio), and the buzz came from the bench's parameters (every SYN knob at 64: STRT = END = 64 and RTRG = 64 loop a sliver from the middle of the sample) |
| TRX-BD, for comparison | | Peak -7.8 dBFS, below -40 dB of its peak after 219 ms |

## What changed, and why

- **ROM is not offered.** Its slots hold nothing usable in the flash image MD Drums runs; the browser, ‹ › and the
  arrows step through the 50 machines of GND, TRX, EFM, E12 and P-I. It comes back with sample loading.
- **The browser plays what it loads, and stays open.** v11's took a click to load, closed, then took ▶ to hear: three
  gestures per machine. Now a click loads the machine and plays it at velocity 100; ↑ ↓ choose the neighbouring
  machine and play it, ← → the first machine of the neighbouring family. Garder (Entrée) closes and keeps; Annuler
  (Échap) goes back to the machine the browser opened on. Choosing another track closes it and keeps.
- **A preview column**, where ROM was, built like a page: APERÇU over the machine's name (20 px), its family, its eight
  parameters as SYN lays them out (2 x 4, unused ones dimmed), a switch "Écouter en choisissant" (on; off for when a
  clip is playing in Live), and the hit screen at the pages' screen place (976-1264, 480-700).
- **‹ › play the machine they reach**, like the browser.
- In the plug-in, the preview screen draws the engine's rendered hit; the mockup draws the same synthetic curve as
  the SYN screen.

## Browser grid

| Region | x (px) | y (px) | Content |
|---|---|---|---|
| Title | 240-920 | 160-200 | "Choisir une machine", the track, the machine under the pointer |
| Annuler, Garder | 960-1280 | 160-200 | Right-aligned; Garder in the accent |
| GND, TRX | 240-320, 320-560 | 200-440 | Family name over its machines; TRX in three sub-columns of 80 px |
| EFM, E12, P-I | 600-680, 680-840, 840-920 | 200-580 | E12 in two sub-columns |
| Preview | 960-1280 | 200-700 | APERÇU 200-220; name 220-260; family 260-280; parameters 300-340; switch 360-400; screen 480-700 |
| Footer | 240-1280 | 720-760 | The browser's gestures |

Checked in the browser: 50 machines offered, no ROM; a click on E12-BD loads it, plays it (velocity 100) and leaves
the browser open; ↓ gives E12-SD, → P-I-BD, ← ← EFM-BD, with the focus on the current machine; Échap returns to
TRX-BD and closes; Garder keeps P-I-MT; › then gives P-I-ML; every browser cell on the 40 x 20 grid; no text past its
cell for any of the 50 machines; the preview screen at the pages' screen place.
