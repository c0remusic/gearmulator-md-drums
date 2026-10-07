# MD Drums UI spec, v19

v18, with the user's review: "on a du white space et une barre inutile au-dessus de SYN". Mockup: `v19-test.html`
(`v19-ui.css`, `v19-ui.js`, `v6-machines.js`). Everything not listed here is as in `v18-ui-spec.md` and the specs
before it.

## What changed

- **No space and no rule between the head band and SYN.** The head band's edge (chrome to surface) is the separation;
  SYN starts at y 120, right under it, and its screen has no top rule either.
- **The three bands share the height from the head band to the footer**: 640 px, 213.3 px each (SYN 120-333.3, EFX
  333.3-546.7, ROUTING 546.7-760). The 1280 x 800 window's extra height goes into the bands, not into empty strips.
- **Each band's content in its middle**: 33.7 px above the title's text and under the values (the title's row is
  28 px: 21 px text on 6 px of padding).
- **Rules**: between SYN and EFX, between EFX and ROUTING, under ROUTING, and at x 880 on the screens' left.
- **The track list's header is as tall as the head band** (40-120): its rows start at 120, where the bands start; its
  footer is level with the editing surface's (760-800).
- **The browser's preview screen sits exactly in ROUTING's screen cell** (880-1280, 546.7-760).

Checked in the browser: every cell on the 40 px columns; the bands at 120-333.3-546.7-760, 33.7 px of space above
each title and under each row of values; the track rows at 120-760; the preview screen at (880, 546.7)-(1280, 760);
no text past its cell, none under 11 px.
