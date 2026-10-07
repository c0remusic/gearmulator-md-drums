# MD Drums UI spec, v10

v9 (v8's three pages under a track head band in chrome), with the user's review of v9: "On a 3 fois TRX-BD, ça
manque un peu de hiérarchie encore, le bouton « jouer » sert à quoi ? Et pourquoi il est placé là ?". Mockup:
`v10-test.html` (`v10-ui.css`, `v10-ui.js`, `v6-machines.js`). Everything not listed here is as in `v9-ui-spec.md`
and `v8-ui-spec.md`.

## What changed, and why

- **The machine's name once outside the list.** It stood in the top bar ("Kit 01 · T01 · TRX-BD"), in the list, in the
  head and under SYN ("Paramètres de TRX-BD"). The top bar's selection is gone; SYN's line says "Paramètres de la
  machine". Checked: the name now appears in the list's row and in the head, nowhere else.
- **One title.** v9's head had two 32 px words (the number and the machine) and two controls as large as the pages'
  knobs. In v10 the machine is the only large word; the track's number sits over it in 11 px ("PISTE 01").
- **▶ against the name it plays.** v9's JOUER stood over ROUTING's first slot for the alignment alone, far from the
  track it plays. It plays the track (a hit at velocity 100, as a click on a number in the list does), so it now sits
  on SYN's first slot, against the track's name, and flashes in the accent when the track plays, from Live or from a
  click. Its name is "Écouter", in its tooltip and in the list's.
- **The kit level leaves the head.** It sat next to ROUTING's VOL as a second volume knob; it stays in MIX, as each
  channel's fader (`t<n>_level`).

## Grid

| Region | x (px) | y (px) | Content |
|---|---|---|---|
| Top bar (chrome) | 0-1280 | 0-40 | Name (0-240), tabs (600-840), window size, rate and latency, Main meter (960-1280) |
| Head band (chrome) | 240-1280 | 40-120 | ▶ ring 40 px centred on 280 (SYN's first knob); "PISTE 01" over the machine (32 px) from 336 (SYN's second knob); ‹ › 480-560; facts on two lines from 616 |

Checked in the browser: ▶ centred on SYN's first knob; the number and the machine start at SYN's second knob's edge;
▶, the name block, ‹ › and the facts share a centre line (80, the name block within 2 px); all 98 machine names end
before ‹; every cell on the 40 x 20 grid; shared edges in and across the pages as in v8; no text past its cell, none
under 11 px; the 32 ids of track 1 in PISTE and `t1_level` in MIX, none repeated; ▶ plays the track and flashes, and
a trig on another track does not flash it.

## Controls

As in v8 and v9, except:

| ID | Control | Position |
|---|---|---|
| `t<n>_machine` | Machine name in 32 px under "PISTE nn" (opens the browser), ‹ › step | Head band, from 336 |
| Écouter | Ring 40 px with ▶; flashes on a trig of the shown track | Head band, SYN's first slot (240-320) |
| `t<n>_level` | Fader with meter | MIX only |
