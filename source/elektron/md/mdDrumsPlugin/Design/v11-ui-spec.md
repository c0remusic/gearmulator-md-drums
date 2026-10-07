# MD Drums UI spec, v11

v10, with the user's review of v10: "enlever le texte inutile à côté de SYN, EFX etc. ; pourquoi il n'y a des barres
que dans EFX et ROUTING et pas dans SYN ?". Four ways of showing the groups were compared side by side in
`v11-var-groups.html` (A: no rules or names; B: names without rules; C: v10's; D: rules in SYN as well); the user
chose D. Mockup: `v11-test.html` (`v11-ui.css`, `v11-ui.js`, `v6-machines.js`; the frame's `data-groups="all"`).
Everything not listed here is as in `v10-ui-spec.md` and the specs before it.

## What changed, and why

- **Page titles alone.** "Paramètres de la machine", "Effets de la piste" and "Canal, envois et LFO" are gone: the
  titles SYN, EFX and ROUTING are the Machinedrum's own page names.
- **A rule over each row of SYN.** EFX and ROUTING name their fixed groups over their knobs (AM, EQ, FILTRE; CANAL,
  ENVOIS, LFO); SYN's eight parameters change with the machine (of 98 machines, only PTCH and DEC hold their places,
  in 89 and 93 of them), so it has no group to name. Its two rows now carry an unnamed rule each, from the first
  knob's edge to the fourth's, so that the three pages share one anatomy: title, rule, four knobs, rule, four knobs,
  screen.

## Grid

| Region | x (px) | y (px) | Content |
|---|---|---|---|
| SYN's rules | 256-544 | 210 and 350 (rows at 200-220 and 340-360) | Unnamed, `rule` colour |

Checked in the browser: the three titles alone; SYN's rules from 256 to 544, on the same rows as EFX's and ROUTING's
group names; in each page one left edge and one right edge; every cell on the 40 x 20 grid; no text past its cell,
none under 11 px; the 32 ids of track 1 in PISTE, none repeated.

## Groups compared (v11-var-groups.html)

| Option | Shows | Chosen |
|---|---|---|
| A | No rules, no names: groups read from the Machinedrum's names (AMD/AMF, FLTF/FLTW/FLTQ, LFOS/LFOD/LFOM) | |
| B | Names without rules | |
| C | Names and rules in EFX and ROUTING (v10) | |
| D | C, plus an unnamed rule over each row of SYN | yes |
