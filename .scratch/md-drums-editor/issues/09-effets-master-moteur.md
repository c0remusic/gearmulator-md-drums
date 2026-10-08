# Effets master dans le moteur

Type: grilling
Status: open
Assignee: —
Blocked by: 03

## Question

Comment l'émulation de la section master du DSP1 entre dans mdEngine et MD Drums, d'après la recherche du ticket 03 ?

- Second DSP56300 émulé qui ne joue que la section master, ou autre découpe ; thread (celui du rendu, ou un worker en
  pipeline) et latence ajoutée.
- Écriture des 32 valeurs : routine de l'OS appelée par le harnais 68k de mdEngine, ou écriture directe en Y.
- Budget CPU accepté pour 16 Tracks + master, mesuré sur la machine de référence.
- Preuve de fidélité : comparaison avec le firmware complet (comme `mdEngineFirmwareTest` le fait pour les Tracks).
