# Écran Hit

Type: task
Status: open
Assignee: —
Blocked by: 20

## Question

L'écran Hit du HANDOFF (« Hit », bande SYN), en capture live comme le ticket 16 l'a fixé.

- **Capture** dans la `Telemetry` (ticket 20) : pour chaque Track, la sortie solo (après effets et VOL) dès
  l'échantillon où le Hit sonne (la note plus `Engine::SampleAccurateDelay`, ticket 19), jusqu'à 0,8 s ou le Hit suivant
  de la Track ; colonnes min/max de 8 échantillons (4 410 paires), la vélocité du Hit et un compteur de séquence ; deux
  tampons par Track (coup en cours, coup précédent), publiés sans verrou. Les Hits d'un Link capturent aussi leur cible.
- **Dessin** sur le canvas de `screen_hit` : fenêtre ajustée au coup (0,1, 0,2, 0,3, 0,4, 0,6 ou 0,8 s, graduations en
  ms) ; forme d'onde en colonnes min/max, échelle fixe ±0,5 aux bords du tracé, écrêtée au bord ; dessin progressif,
  le coup en cours en encre derrière une tête de lecture accent de 1 px qui traverse en temps réel, le reste en coup
  précédent `--track` ; enveloppe d'amplitude en accent 2 px (attaque instantanée, relâche de 30 ms, comme la
  maquette) ; titre « Hit », note « Last hit, velocity 110 » ou « Not played yet ».
- **Mesures** : coût de la capture côté audio (avec le ticket 20, au plus 1 % d'un cœur), aucune allocation ; un test
  qui joue un Hit et compare la capture à la sortie solo rendue.

Fini quand : l'écran Hit montre chaque coup de la Track montrée dans Live ; garde-fou vert ; poussé sur `main`.
