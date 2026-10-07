# MD Drums v20 — style guide

v19's, with these changes:

- **Grid:** 16 columns × 64 px, 16 px gutters, 8 px margins; multiples of 4 for everything, across and down (boxes,
  paddings, margins, gaps, text starts, baselines); the only exceptions are 1-3 px vertical paddings that put a font's
  baseline on the 4 px step in its box (measured; the user chose aligned baselines over 4 px paddings there), and
  1-2 px strokes, centred. Screens and rules run to the window's edge like backgrounds; their content keeps the columns. Place by columns, never by pixels: a box spans whole columns (or puts one side on a column
  edge when narrower than one). The overlay (`Afficher la grille`) shows the columns filled in red and the baseline in blue, as Figma does.
- **Knobs** fill their column: 64 × 64, label and value centred under them (11 px caps, 13 px value).
- **Screens** keep 16 px inside their edge for titles and plots; the head band's text over the screens' column uses
  the same inset, so it lines up with the screens' titles.
- **Type:** Inter; 34 px the machine's name (26 px inside the `ends` field), 21 px the bands' titles, 13 px values and
  meta, 11 px labels; nothing under 11 px.
- **Language:** English throughout the plug-in.
- **No status bar:** shortcuts live in tooltips; the bands run to the window's bottom.
- **Rules:** under the top bar and over every band, none elsewhere; screens fill their bands.
- **Measured paddings:** head band 20 / 20 around its text; bands 36 over the title's capitals and under the values'
  baseline; screens 16 over their title's capitals and to its left; track rows 16 under the baseline.
- **States:** a play control's border and glyph light in accent for 140 ms on each hit (no fill); in LFO assign mode every target knob is ringed in
  accent (2 px on hover); a modulated knob's label is underlined in accent.
