# Écran Hit

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
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

## Answer

- **Capture** (`mdDrumsTelemetry.h`) : deux emplacements par Track ; `beginCapture` met l'identifiant de l'emplacement
  libre à 0, remet son compte à zéro, puis publie un nouvel identifiant et l'emplacement courant ; `addColumn` écrit
  la colonne puis publie le compte (release). `readCapture` copie les colonnes déjà publiées (seulement les nouvelles
  pour le même Hit) et relit l'identifiant : changé, la copie est rejetée et la lecture suivante la reprend. 4 410
  colonnes de deux `int16` par emplacement, ~560 Ko en tout, alloués avec la télémétrie.
- **Device** : à chaque note, chaque Track frappée (la Track et son Link) compte un Hit et ouvre une capture qui
  commence à la position de la note plus `Engine::SampleAccurateDelay` ; après chaque rendu, les captures actives
  prennent min et max de 8 échantillons de la sortie solo, jusqu'à 0,8 s ou le Hit suivant.
- **`HitView`** (`mdDrumsPlugin/mdDrumsHitView.*`) : timer à la fréquence de l'écran ; fenêtre de 0,1 à 0,8 s ajustée
  à 1,1 fois la partie audible (enveloppe à attaque instantanée et relâche de 30 ms au-dessus de 3 % du pic, comme la
  maquette), qui garde celle du Hit précédent pendant que le nouveau joue s'il était plus long ; graduations tous les
  25 à 200 ms et leurs mots en spans RML (`hit_tick1`-`8`) ; forme d'onde en colonnes min/max, le Hit courant en encre,
  le précédent en `--track` au-delà ; enveloppe en accent 2 px ; tête de lecture accent tant que la capture grandit
  (50 ms sans colonne : arrêtée) ; note « Last hit, velocity N » ou « Not played yet » ; échelle fixe ±0,5. Le canvas
  n'est repeint que quand la capture, la fenêtre ou la Track montrée changent.

**Mesures** :
- `mdDrumsDeviceTest` : un Hit à l'échantillon 100, vélocité 110 : 495 colonnes comparées à Out 01 depuis la note plus
  31, 0 différente. Passe de crête et 16 captures à la fois, pire cas : 0,233 % d'un cœur ; Device entier avec
  télémétrie 19,89 %, sans 19,51 %. Aucune allocation.
- `mdDrumsSkinTest` : capture injectée (0,4 décroissant, 600 colonnes) : fenêtre 0,2 s, note, forme en encre (0,95),
  tête de lecture au pixel attendu, graduations « 0 » à « 150 ms », tête arrêtée 200 ms après.

Dans Live : à vérifier par l'utilisateur.
