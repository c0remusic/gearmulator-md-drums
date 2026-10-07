# MD Drums UI spec, v18

v17, with the user's request: "on peut aussi faire des lignes entre les catégories qui servent de cadre aux écrans ?".
Mockup: `v18-test.html` (`v18-ui.css`, `v18-ui.js`, `v6-machines.js`). Everything not listed here is as in
`v17-ui-spec.md` and the specs before it.

## What changed

- **A rule between the categories**, across the editing width (240-1280): over SYN (y 160), between SYN and EFX (360),
  between EFX and ROUTING (560), under ROUTING (760), in `rule` (#55585e).
- **The bands touch**, 200 px each (SYN 160-360, EFX 360-560, ROUTING 560-760); inside, 20 px of space, the
  title (20-60), the group rule (60-80), the knobs (80-140), labels and values (140-180), 20 px of space.
- **Each screen fills its band's cell** in the screen column (880-1280, 400 x 200): the rules between the categories
  are its top and bottom, a rule at x 880 its left side. Its title starts at 896, the head's facts too.
- **The browser**: its title on SYN's title line (180-220); the preview's screen in ROUTING's screen cell (880-1280,
  560-760), framed the same way; "Écouter en choisissant" moves above it (500-540).

Checked in the browser: every cell on the 40 x 20 grid; the bands at 160-360, 360-560, 560-760; each screen at
880-1280 over its band's height; the screens' titles and the head's facts at x 896; no text past its cell, none
under 11 px; the browser on the grid, its preview screen in ROUTING's screen cell.
