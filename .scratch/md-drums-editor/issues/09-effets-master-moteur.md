# Effets master dans le moteur

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 03

## Question

Comment l'émulation de la section master du DSP1 entre dans mdEngine et MD Drums, d'après la recherche du ticket 03 ?

- Second DSP56300 émulé qui ne joue que la section master, ou autre découpe ; thread (celui du rendu, ou un worker en
  pipeline) et latence ajoutée.
- Écriture des 32 valeurs : routine de l'OS appelée par le harnais 68k de mdEngine, ou écriture directe en Y.
- Budget CPU accepté pour 16 Tracks + master, mesuré sur la machine de référence.
- Preuve de fidélité : comparaison avec le firmware complet (comme `mdEngineFirmwareTest` le fait pour les Tracks).
- Fixé par le ticket 06 : les 32 paramètres Master sont en page 8, `NonPartSensitive`, et arrivent au Device en
  `$5D-$60` ; ils existent et sont sauvés dans le Kit dès le port (07), muets jusqu'ici. Reste à fixer ici l'API
  master de `mdDrums::Engine`, et si le rendu synchrone sur le thread audio tient avec le master.

## Answer

Grilling du 2026-10-09, sur la recherche 03.

- **Exécution** : un second DSP56300 émulé ne joue que la section master (P:$342-$970) et tourne dans
  `EngineT::render`, sur le thread audio, juste après `m_mixer.process` : il lit les trois bus du bloc (Main sec, REV,
  DEL) et rend le Main du même bloc. Aucune latence de scheduling, pas de worker. Harnais sur le modèle de
  `VoiceEngine` (recherche 03, section 10) : records de `dspB` chargés, morceaux du boot nécessaires exécutés une fois,
  P:$971 patché vers un stub d'attente.
- **Coefficients** : les quatre corps de l'OS ($20B4AE Echo, $20B604 Dynamix, $20B74E EQ, $20B940 Gate Box) exécutés
  dans `MachineRunner`, sorties patchées en `rts` ($20B5F8, $20B742, $20B934, $20BA4C), un effet par tick selon
  `tick & 3` comme l'OS, après le lissage des niveaux. Les mots mis en attente sont lus par `peek32`, masqués à 24 bits
  et écrits en Y:$150, $17A, $170 ou $185 entre deux blocs. Les 32 cibles sont posées en $1000F6C + k ; pas de
  traduction C++ des corps.
- **Dynamix** : Main sort 16 échantillons après les Outs, comme sur la machine. Rien n'est compensé.
- **Latence déclarée** : celle de Main, 35 → 51 échantillons (1,16 ms à 44,1 kHz). Les Outs arrivent donc 0,36 ms en
  avance sur la grille de l'hôte.
- **Budget CPU** : jugé sur le thread audio, en temps de rendu rapporté au temps réel. Plafond de 30 % dans les tests
  (aujourd'hui ~20 % pour 16 Tracks, ticket 20), garde automatique. Validation finale dans Live, compteur en pic, sur
  le set de l'utilisateur, à l'installation.
- **Fidélité** : bit-exact visé contre le Main du firmware complet, scénarios à paramètres fixes (Echo, Gate Box, EQ,
  Dynamix), fixture firmware comme `mdEngineFirmwareHits`. Écart admis seulement s'il est expliqué (instants
  d'écriture des paramètres : le firmware les reçoit en cours de bloc par interruption, le harnais entre deux blocs).
- **API** : `mdDrums::Engine::setMaster(index 0-31, valeur 0-127)`, dans l'ordre du dump de Kit (Gate Box d'abord),
  lissé comme l'OS ; Echo TIME suit `setTempo` ; pas de bypass. Les 32 paramètres Master existent déjà (page 8, `$5D-$60`,
  ticket 06) et deviennent audibles.
- **Multithreading** : `ParallelVoiceEngine` (voix réparties sur des threads, déjà dans mdEngine, inutilisé par MD
  Drums) mesuré à part, juste après la construction du master : 1, 2 et 4 groupes de voix ; temps sur le thread audio,
  CPU total, gigue, latence. Activé seulement si c'est rentable.
- Déjà fixé ailleurs : une Track sur son Out garde ses Sends (`CONTEXT.md`, recherche 03 section 9) ; le master ne lit
  que les trois bus.

Tickets créés : 24 (effets master, construction), 25 (voix en parallèle, mesure).
