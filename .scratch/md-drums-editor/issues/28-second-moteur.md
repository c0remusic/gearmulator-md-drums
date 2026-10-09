# Second moteur de l'écran Hit et de l'aperçu du browser

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 12

## Question

Construire ce que le ticket 12 a fixé.

- **Moteur d'aperçu** : un mdEngine sans master, partagé par les instances du processus, créé à la première demande
  d'un éditeur ouvert, libéré après 30 s sans demande ; un thread à lui, la dernière demande seulement ; voix neuve à
  chaque rendu (même réglage, même image) ; 0,8 s de la sortie propre de la Track en colonnes min/max de 8 échantillons,
  comme la capture.
- **Écran Hit** : un changement du son de la Track montrée (machine, 24 paramètres, level, LFO) demande un rendu à la
  vélocité du dernier coup (sinon celle de la bande de tête), note « Preview, velocity n » ; un vrai coup reprend la
  capture live.
- **Aperçu du browser** : la machine choisie rendue à ses SYN par défaut et aux autres paramètres de la Track.
- Rien sur le thread audio ; le résultat revient au fil message sans risque si l'éditeur s'est fermé entre-temps.

Fini quand : rendu identique à lui-même pour un même réglage ; écran Hit et aperçu vérifiés dans `mdDrumsSkinTest` ;
temps d'un rendu et mémoire mesurés ; garde-fou vert ; poussé sur `main`.

## Answer

Construit le 2026-10-09.

- `mdDrums::HitPreview` (`mdDrums/mdDrumsHitPreview.*`) : rend le Hit d'une Track pour le Kit joué sur un moteur sans
  master (`Engine(os, bank, false)`, Main = mix sec), capturé comme le Device capture un Hit (`Telemetry::level`
  partagé). Moteur neuf à chaque rendu. Un par processus, partagé par les éditeurs (`Client`) : un thread à lui, la
  dernière demande de chacun, le moteur suivant bâti pendant qu'aucune n'attend, moteur et OS libérés après 30 s sans
  demande. `applyKit` sort du Device pour que les deux jouent un Kit de la même façon.
- Vérifié (`mdDrumsHitPreviewTest`, au garde-fou) : colonne pour colonne le premier Hit d'un Device qui vient de charger
  le Kit, sur 4 Tracks du Kit d'usine 1 (machines 28, 18, 23, 156) à 4 vélocités ; une machine choisie (EFM-SD,
  ROM-01, P-I MT) rendue avec ses SYN1-8 par défaut, 0 colonne d'écart (800 à 3231 avec les SYN d'avant) ; même
  demande, même Hit ; seule la dernière demande en attente est rendue ; moteur bâti d'avance ; libération après
  l'inactivité, retour à la demande suivante.
- Mesuré : moteur sans master 83,5 Mo, construit en 19,5 ms ; un Hit rendu en 37 ms ; première demande 119 ms (image
  flash lue, OS décodé, moteur construit) ; sur moteur bâti d'avance 43 ms.
- `HitView` : demande un aperçu quand Machine, les 24 paramètres, le LFO ou Level de la Track montrée changent, tant
  qu'un écran se voit et qu'aucun Hit de la Track ne joue ; vélocité du dernier coup, sinon de la bande de tête ; note
  « Preview, velocity n » jusqu'au prochain coup. Une machine choisie seule prend ses SYN1-8 par défaut avant le
  rapport du Device (500 ms au plus), d'où `Processor::getMachines`. L'écran d'aperçu du browser dessine la même chose,
  avec ses graduations (`preview_tick1`-`8`).
- `mdDrumsSkinTest` : un knob tourné montre son aperçu (« Preview, velocity 110 ») en ~120 ms la première fois ; un vrai
  coup reprend l'écran ; dans le browser, EFM-SD choisi sans écoute s'affiche, identique au rendu du moteur avec ses
  SYN1-8 par défaut.
- Mémoire réelle avec 16 instances dans Live : un seul moteur d'aperçu par processus ; à constater à l'installation.
