# Écrans Filter/EQ et LFO, ASSIGN, écoute du browser

Type: task
Status: open
Assignee: —
Blocked by: 18

## Question

Les écrans calculés côté éditeur et ce qui reste inerte dans le skin v22.

- **Filter and EQ** : réponse de 20 Hz à 20 kHz (graduations 100, 1k, 10k ; ligne « 0 dB »), 2 px accent, point EQ en
  carré de 6 × 6 encre, note « SRR n » quand SRR > 0 ; la vraie réponse d'un `TrackFx` privé à l'éditeur sur une
  impulsion (~0,5 ms, ticket 05), recalculée quand un paramètre d'EFX change.
- **LFO** : la forme des routines de l'OS portées (ticket 05, bit-exact), zoomée sur ce que l'onde parcourt (valeur du
  knob visé ± LFOD, bornée 0-127), valeurs haute et basse en 12 px, « LFOD 0: no modulation » ; corrections du HANDOFF
  que le ticket 05 a relevées (amplitude ±126, scie qui descend deux fois par période, RMP et EXP en one-shot relancés
  à chaque Hit, RND à 8 pas, LFOM qui fond vers la forme 2 inversée).
- **ASSIGN** : armé, il lit « Cancel » en accent, le titre « Click a knob to modulate », chaque knob un anneau accent
  de 1 px (`vknob`) ; un clic sur un knob, de cette Track ou d'une autre, règle `LfoTrack` et `LfoParam` du LFO qui a
  armé ; Esc et Cancel désarment.
- **Noms soulignés** : le nom d'un paramètre qu'un LFO module, souligné en accent.
- **Listen while choosing** : la case du browser ; cochée, choisir une machine joue la Track (vélocité 100).

Fini quand : les deux écrans suivent les knobs sans allocation sur le thread audio et dans le budget entrée→pixel ;
ASSIGN et l'écoute fonctionnent dans Live ; garde-fou vert ; poussé sur `main`.
