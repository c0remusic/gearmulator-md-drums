# MD Drums style guide, v8

## Rules

- **Share edges.** A grid that is not drawn only shows through the edges things share. In a page, the title, the group
  names, the knobs' circles and the screen start on one line and end on one line; across pages, the same rows start at
  the same height. A new element takes an existing edge before it gets a new one.
- **One anatomy, repeated.** Every page: title, knobs in two rows of four, screen. Every knob: control, label, value.
  The head's JOUER and level follow the knob's anatomy, on the slots of the page under them.
- **Group by name and rule.** A group's name sits over its first knob; a rule runs to the edge of its last knob. One
  knob alone needs no group. A rule never closes a box.
- **A line must say something**: a group's extent, a screen's labelled graduation, MIX's channels, a table's head.
- **Space ranks**: 32 px between knobs of a group, 72 px between pages, 40 px between the head and the pages.
- **No cards, tints or pills.** Solid surface levels only; a state is a text colour or a solid fill.
- **One accent** (#6fd1c4) for selection and what is alive; #ff7a5c for mute.

## Type (Inter, `tnum` frozen into the bundled file)

| Level | Use | Size, weight |
|---|---|---|
| 1 | Track number and machine | 32 px; number light 300 in ink-dim, machine regular in ink |
| 2 | Page titles (SYN, EFX, ROUTING), browser title | 20 px semibold; page names tracked 1.5 px |
| 2b | What the page holds | 12 px, ink-faint |
| 3 | Group names (AM, FILTRE, CANAL...), family names | 11 px semibold, upper case, tracked 1 px, ink |
| 4 | Values | 14 px, tabular |
| 5 | Knob labels, tab names, captions | 11 px medium, upper case, tracked 1 px, ink-dim |
| 6 | Hints, footer, graduation labels | 11 px, ink-faint |

## Spacing

- 40 px columns, 20 px rows. A page is four 80 px slots; its content starts 16 px in.
- Knob: 48 px circle centred in a 80 x 60 cell, then label and value in 40 px.
- Page: title 40, group name 20, knobs 60, labels 40, 20, group name 20, knobs 60, labels 40, 20, screen 200.

## States

| State | Look |
|---|---|
| Selected track | Row in the surface colour with a 3 px accent bar; its number and machine in ink |
| Selected machine (browser) | 3 px accent bar on the left, text in ink |
| Open tab | Ink text, 2 px accent underline under the word |
| Switch on | Solid fill: accent (shape), mute colour (M), ink (S) |
| Hover | Text to ink |
| Keyboard focus | 1 px accent ring inside the control |
| Not yet working | 35 % opacity, reason in a tooltip; the group's name in ink-faint (ENVOIS) |
| Trig | Track number in accent for 140 ms (not under reduced motion) |
| LFO target | Its label in ink with a 2 px accent underline |
