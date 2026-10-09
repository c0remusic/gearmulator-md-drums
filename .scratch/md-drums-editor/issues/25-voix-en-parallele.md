# Voix en parallèle : mesure de rentabilité

Type: task
Status: open
Assignee: —
Blocked by: 24

## Question

`ParallelVoiceEngine` (mdEngine) répartit les 16 voix du DSP2 sur des threads ; MD Drums ne l'utilise pas. Le ticket 09
demande de mesurer si c'est rentable, une fois le master construit.

- Mesurer 1, 2 et 4 groupes de voix, 16 Tracks jouées et master actif, sur la machine de référence (Ryzen 7 3700X,
  8 cœurs / 16 threads) : temps sur le thread audio rapporté au temps réel, CPU total de tous les threads, gigue du
  temps de rendu par bloc (pires blocs compris), latence note→son.
- Vérifier que le son reste identique (bit-exact) à celui d'un seul groupe.
- Activer seulement si le temps sur le thread audio baisse nettement sans que la gigue ou la latence se dégradent ; sinon
  noter les chiffres et laisser le rendu sur un seul thread.

Fini quand : chiffres des trois configurations notés ici ; décision prise et appliquée ; garde-fou vert ; poussé sur
`main`.
