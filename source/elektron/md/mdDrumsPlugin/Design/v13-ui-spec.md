# MD Drums UI spec, v13

v12, with the user's review of v12: "Je vois pas l'intérêt de SORTIES si c'est déjà affiché dans MIX et qu'on peut pas
choisir". Mockup: `v13-test.html` (`v13-ui.css`, `v13-ui.js`, `v6-machines.js`). Everything not listed here is as in
`v12-ui-spec.md` and the specs before it.

## What changed, and why

- **No SORTIES view.** A track's output is not a choice: track n goes to Out n once the host takes that output, to
  Main otherwise. SORTIES only listed that, and MIX already showed it under each channel. The top bar keeps PISTE and
  MIX (600-760).
- **MIX names the output as the host does**: "Out 01" when the host has taken it, "Main" otherwise (v12: "→ 01").
- **SORTIES' help moves into that line's tooltip**: for a taken output, that the track has left Main and carries the
  track after its effects and VOL, before PAN; for Main, how to take the output in Live (an audio track, Audio From
  "MD Drums", then "Out nn", Monitor on In).
- If outputs become a choice (several tracks on one output, as groups in the host), a routing view comes back with it.

Checked in the browser: two tabs; MIX's last line reads Out 01, Out 02, then Main, with the tooltips above; no text
past its cell; `t1_level` on MIX's first fader.
