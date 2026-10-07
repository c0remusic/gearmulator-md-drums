# MD Drums style guide, v7

## Rules

- **Place on the grid, do not draw it.** 40 px modules for every position and size; no rule between cells.
- **A line must say something.** Allowed: a screen's labelled graduation, the separation of the MIX's channels, a
  table's head. Not allowed: a rule around a cell, a lattice in a screen, a separator between list rows.
- **Hierarchy by level, size and space**, never by boxes: chrome (navigation) is darker than the surface (editing),
  screens are darker still; type steps from 32 px down to 11 px; a section header line leaves its row above as space.
- **No cards, tints or pills.** Solid surface levels only; a state is a text colour or a solid switch fill.
- **One accent** (#6fd1c4) for selection and what is alive; #ff7a5c for mute.

## Type (Inter, `tnum` frozen into the bundled file)

| Level | Use | Size, weight |
|---|---|---|
| 1 | Track number and machine in the head | 32 px; number light 300 in ink-dim, machine regular in ink |
| 2 | Section name (SYN, EFX, ROUTING), machine browser title | 15 px semibold, tracking 2 px |
| 2b | What the section holds | 12 px, ink-faint |
| 3 | Values | 14 px, tabular |
| 3b | Facts line under the title | 12 px, ink-dim with values in ink |
| 4 | Labels, tab names, captions | 11 px medium, upper case, tracking 1 px, ink-dim |
| 5 | Hints, footer, graduation labels | 11 px, ink-faint |

## Spacing

- Text 16 px from a region's left edge; header lines sit 6 px above the bottom of their row.
- The track head takes three rows: two for the title, one for the facts line.
- Sections: one header row, two knob rows, one label-and-value row.

## States

| State | Look |
|---|---|
| Selected row or machine | 3 px accent bar on the left, text in ink; the selected track's row takes the surface colour |
| Selected channel (MIX) | Chrome fill, 3 px accent bar on top |
| Open tab | Ink text, 2 px accent underline under the word |
| Switch on | Solid fill: accent (shape), mute colour (M), ink (S) |
| Hover | Text to ink |
| Keyboard focus | 1 px accent ring inside the control |
| Not yet working | 35 % opacity, reason in a tooltip and, for DEL and REV, a hint line under them |
| Trig | Track number in accent for 140 ms (not under reduced motion) |
| LFO target | Its label in ink with a 2 px accent underline |
