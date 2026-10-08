# Télémétrie, mètres et scopes

Type: task
Status: open
Assignee: —
Blocked by: 16

## Question

Construire la télémétrie que le ticket 16 a fixée, et les mètres et scopes de l'éditeur dessus.

- **`Telemetry`** possédée par `mdDrums::Processor`, son pointeur passé au Device à sa création
  (`Processor::createDevice`) ; le `DummyDevice` n'écrit rien. Côté audio, après chaque rendu : la crête de chacune des
  18 sorties du Device (44,1 kHz), montée dans un `std::atomic<float>` (CAS borné comme `mdOutputMeters.h` de md-mm) ;
  aucune allocation, aucun verrou.
- **Éditeur** : lecture à la fréquence de l'écran (un timer de l'éditeur, ou le rendu à la demande du ticket 08), prise
  par `exchange`. Mètre du Main (barre du haut, deux barres de 2 px, remplissage accent) et des 16 strips du Mix
  (`stripN_meter`, remplissage du bas) : montée instantanée, chute de 20 dB/s, échelle en dB de −60 à 0 dBFS, sans
  marque de crête. Dessin sur canvas (le rendu logiciel ignore les transformations), repeint seulement quand la valeur
  affichée bouge.
- **Scopes** de la liste des Tracks (`rowN_scope`, 32 × 24 px) : historique de 32 colonnes (~1 s) tenu par l'éditeur à
  partir des mètres des strips.
- **Mesures** : coût côté audio avec et sans télémétrie, 16 Tracks frappées en continu (au plus 1 % d'un cœur de plus),
  aucune allocation (`mdDrumsDeviceTest`) ; entrée→pixel du banc du ticket 17 inchangé ; un test qui joue une note et
  lit la crête de sa Track et du Main.

Fini quand : mètres et scopes bougent avec le son dans Live ; coût et allocations mesurés au garde-fou ; garde-fou
vert ; poussé sur `main`.
