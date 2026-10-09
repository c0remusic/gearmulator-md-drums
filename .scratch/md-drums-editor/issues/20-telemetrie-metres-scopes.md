# Télémétrie, mètres et scopes

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
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

## Answer

- **`mdDrums::Telemetry`** (`mdDrums/mdDrumsTelemetry.h`, sans JUCE) : 18 `std::atomic<float>` de crête (montée par CAS
  borné à 4 essais, prise par `exchange`) et 16 compteurs de Hits. `mdDrums::Processor` la possède en `shared_ptr` et
  la passe à chaque Device qu'il crée (`createDevice`) : le Device la partage, elle reste valide quel que soit l'ordre
  de destruction ; le `DummyDevice` n'écrit rien.
- **Device** : après chaque rendu (coupé à chaque événement), la crête de chacune des 18 sorties ; à chaque note,
  `Engine::trigger` rend les Tracks frappées (la Track et son Link, aucune muette) et chacune compte un Hit.
- **`MeterView`** (`mdDrumsPlugin/mdDrumsMeterView.*`) : un timer à la fréquence de l'écran lit la télémétrie ;
  mètre du Main (deux barres) et des 16 strips sur canvas, montée instantanée, chute de 20 dB/s, −60 à 0 dBFS ; scopes
  de la liste : 32 colonnes d'un 32e de seconde, le niveau le plus haut de chacune, traits centrés en encre pour la
  Track montrée et en `#a7aaaf` pour les autres, comme la maquette ; numéros de la Track (liste, Mix, sends) en accent
  140 ms à chaque Hit. Un canvas n'est repeint que quand ce qu'il montre bouge (pixel de mètre, colonne de scope) et
  jamais caché.

**Mesures** :
- `mdDrumsDeviceTest` : une note 36 donne une crête au Main (0,089) et à Out 01 (0,252), aucune aux 15 autres, un Hit
  compté ; une Track muette n'en compte pas. La passe de crête seule coûte 0,027 % d'un cœur ; le Device entier,
  16 Tracks frappées, 20,01 % d'un cœur avec, 19,92 % sans (côte à côte, bloc par bloc). Aucune allocation (le test des
  32 blocs tourne avec la télémétrie).
- `mdDrumsSkinTest` : crêtes injectées, le mètre de Track 1 à −6 dBFS monte à 0,900 de sa hauteur, le Main à 1,000 et
  0,667 (0 et −20 dBFS), au pixel près ; une seconde plus tard 0,566 (−26 dBFS) ; le Hit allume le numéro et l'éteint
  après 140 ms ; une demi-seconde de signal remplit la moitié droite du scope. Entrée→pixel inchangé (drag 3,4 ms,
  clic 12,7 ms).

Dans Live : à vérifier à l'écoute par l'utilisateur.
