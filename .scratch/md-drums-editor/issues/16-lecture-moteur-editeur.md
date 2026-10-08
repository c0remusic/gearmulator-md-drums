# Lecture du moteur par l'éditeur

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: 07

## Question

Comment l'éditeur lit-il, sans verrou et sans `Plugin::withDeviceLocked`, ce que le Device sait ?

- Quoi : valeurs que le Device change lui-même (SYN aux défauts d'une Machine, Kit chargé), mètres de Main et des
  Outs, capture du Hit (anneau de la sortie de la Track, `Design/HANDOFF.md`, « Hit screen »), phase et valeur des LFO
  pour l'écran ROUTING.
- Où vit l'objet partagé et qui le possède : le Device peut être remplacé par un `DummyDevice`
  (`jucePluginLib/processor.cpp:1121-1167`), l'éditeur peut s'ouvrir et se fermer à tout moment.
- Cadence de lecture, coût de l'écriture côté audio, ce qui passe plutôt par `readMidiOut` vers le Controller.

Sortie : la forme fixée pour les tranches Track, Mix et écran hit.

## Answer

### Faits relevés

- Le Device est créé par `Processor::getPlugin()` (`processor.cpp:187-248`) et remplacé sur le thread message : pont
  DSP (`setDeviceType`), reboot (`rebootDevice`), Device invalide (`recoverInvalidDevice`, après un `onDeviceInvalid`
  levé sur le thread audio) ; `DummyDevice` si sa création échoue. `Plugin::getDevice()` rend un pointeur brut sans
  synchronisation ; `getDeviceGeneration()` est atomique. Ni le Controller ni l'éditeur de MD Drums ne gardent de
  pointeur sur le Device.
- md-mm : `mdLiveDevice` met en cache des objets possédés par le Device et les re-récupère sous `withDeviceLocked` à
  chaque génération ; `MachineStatus` en atomiques, `LiveKitSnapshot` et le panneau sous mutex en `try_lock` (20 Hz).
  Ses mètres (`mdOutputMeters.h`) : un `std::atomic<float>` de crête par canal, montée par CAS côté audio, prise par
  `exchange` à 60 Hz, mesurée sur les buffers de l'hôte (bus actifs seulement) ; 20 dB/s de chute.
- MIDI out du Device vers le Controller : déjà en place pour SYN1-8 d'une nouvelle machine (`reportMachines`) ; file
  du Controller sous mutex, `try_lock` côté audio (jamais d'attente), vidée par son timer à 16 ms.
- MD Drums ne publie aujourd'hui aucun état entre threads. Ses 18 sorties à 44,1 kHz : Main, puis chaque Track seule
  (après ses effets et VOL, avant PAN).
- LFO : valeur et phase dans la mémoire de l'OS (`kLfo` + 36 × Track : sorties +$10 et +$14, phase +$20), mises à jour
  au tick (125 Hz) ; aucun accesseur. md-mm dessine une forme statique.

### Décisions

**Propriété.** Un objet `Telemetry` possédé par `mdDrums::Processor`, qui vit autant que le plug-in ; le Processor
passe son pointeur au Device à sa création. Le Device écrit, l'éditeur lit, sans verrou. Un Device remplacé laisse les
valeurs figées, un `DummyDevice` n'écrit rien : pas de logique de génération.

**Kit et valeurs changées par le Device.** Le Controller est la vérité du Kit : il envoie lui-même chaque Kit, Link et
Choke. Le Device ne renvoie par MIDI out que ce qu'il déduit (SYN1-8 aux défauts d'une machine, déjà fait) ; aucun
snapshot de Kit publié (un dump de 1 233 octets en MIDI out allouerait sur le thread audio).

**Mètres.** Crêtes des 18 sorties du Device, à 44,1 kHz, avant le resampler : un strip du Mix a son mètre même quand
l'hôte n'active pas son bus Out ; Main = le mix sec. `std::atomic<float>` montées côté audio, prises par `exchange`
côté éditeur. Montée instantanée, chute de 20 dB/s, échelle en dB de −60 à 0 dBFS, sans marque de crête (le v22 n'en
dessine pas).

**Scopes de la liste** (32 × 24 px) : historique de 32 colonnes (~1 s) tenu par l'éditeur à partir des mètres des
strips ; aucun coût côté audio.

**Écran Hit, capture live.** La sortie solo de la Track (après ses effets et VOL), dès l'échantillon où le Hit sonne
(connu exactement depuis le ticket 19 : la note plus 31), jusqu'à 0,8 s ou le Hit suivant ; colonnes min/max de 8
échantillons (4 410 paires pour 0,8 s), avec la vélocité du Hit. Dessin progressif : le coup en cours se dessine
derrière la tête de lecture, le reste de la fenêtre montre le coup précédent en `--track`. Échelle fixe comme la
maquette : ±0,5 de pleine échelle aux bords du tracé (vélocité et VOL visibles), écrêté au bord. Le second moteur
(ticket 12) reste à part.

**LFO.** Aucune valeur live : l'écran LFO dessine la forme depuis les paramètres (routines de l'OS portées, ticket 05),
comme le v22. Un point mobile resterait possible plus tard (16 valeurs publiées au tick) sans changer l'architecture.

**Cadence.** L'éditeur lit la télémétrie à la fréquence de l'écran (ticket 08), par compteurs de séquence, et ne
repeint que les canvas qui changent.

**Budget côté audio.** Mesure des 18 crêtes et capture des Tracks actives seulement, sans allocation : au plus 1 % d'un
cœur de plus, 16 Tracks frappées en continu, mesuré au garde-fou.

**Découpe.** Trois tickets `task` : 20, télémétrie, mètres et scopes ; 21, écran Hit ; 22, écrans Filter/EQ et LFO
calculés côté éditeur, avec ASSIGN et « Listen while choosing ».
