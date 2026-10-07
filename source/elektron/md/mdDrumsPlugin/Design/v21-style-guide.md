# MD Drums v21 — style guide

v20's (grid, spacing, rules, screens), with:

- **Type scale:** 12 / 14 / 22 / 34, even sizes only (the user). 12: labels, captions, chips, track numbers. 14: values,
  machine names in lists, body text, screen titles' controls. 22: band titles, the browser's title, the play velocity.
  34: the machine's name.
- **Case:** the Machinedrum's own names in capitals with 1 px tracking (`.code`, knob labels, band titles, family and
  mode codes); everything else in sentence case without tracking.
- **Colour roles:** ink = content and chosen options; ink-dim = secondary text and data plots; ink-faint = values at
  their default, hints; `--track` = every control's border and an unheard waveform; accent = live state only.
- **Knobs:** track arc `--track` 2 px; value arc ink 3 px only when the value differs from the default, from the
  minimum or, for bipolar parameters, from the top; pointer ink when edited, ink-dim at the default.
- **Chosen option:** ink fill, chassis text. Hover on an option: panel fill.
- **Controls:** one border colour (`--track`), 24 px minimum target; the play key lights its border and glyph in accent
  for 140 ms on a hit.
- **Words:** plain, sentence case, no middle dots or arrows; a value says what it is ("Plays on Out 01", "Last hit,
  velocity 110").
