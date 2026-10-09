# Mesures hors Live : instances, mémoire, CPU

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 28, 30

## Question

Avant la validation dans Live, ce qui se mesure sans Live : plusieurs instances de MD Drums dans un seul processus,
comme Live les charge.

- Mémoire par instance (Device, moteur, master), puis avec un éditeur ouvert et l'aperçu de l'écran Hit (moteur
  partagé), pour 1, 4 et 16 instances.
- CPU de chaque instance sur un motif chargé (Hits sur les 16 Tracks, effets master actifs), à 44,1 kHz par blocs de
  128 : part du temps réel, pire bloc face à son échéance.
- Ce qui reste à constater dans Live seul : son compteur CPU, l'undo, Ctrl+S, Push 2, les Outs depuis un Drum Rack.

Fini quand : mesure gardée (`mdDrumsInstancesTest`, hors garde-fou), chiffres dans la réponse et la carte, poussé.

## Answer

Mesuré le 2026-10-09 sur ce PC (Ryzen 7 3700X), `mdDrumsInstancesTest 16 10` : 16 instances dans un processus,
préparées à 44,1 kHz, blocs de 128, motif chargé (quatre Hits par double croche à 120 BPM sur les 16 Tracks, effets
master du Kit d'usine 1), rendues l'une après l'autre sur un thread en priorité temps critique.

- **Trouvé en mesurant** : 63 blocs sur 55 120 au-delà de leur échéance (2,9 ms), jusqu'à 9,6 ms, tous au premier Hit
  d'une machine. `mdDrumsFirstHitTest` les attribue au DSP de voix : son JIT compile le code d'une machine à sa
  première exécution, 1,5 à 8 ms dans un bloc de 32 échantillons ; une Track qui reprend une machine déjà jouée ne
  coûte rien. Dans Live à 128 échantillons, un décrochage au premier coup de chaque machine, y compris au browser.
- **Corrigé** : `EngineT::warmUpVoices` à la construction du moteur du Device : chaque machine audio (sauf RAM-R/P)
  joue un Hit à ses SYN1-8 par défaut pendant 4 blocs sur le DSP de voix seul, mots calculés par une copie de l'OS ;
  puis le DSP retrouve sa mémoire P, X, Y et ses registres (`VoiceEngine::saveState`, `restoreState`), le code compilé
  gardé. 177 ms à la construction. Premier Hit : pire bloc 8,17 ms avant, 0,64 ms après ; échantillons identiques à un
  moteur non chauffé (0 sur 2 260 992, `mdDrumsFirstHitTest`, au garde-fou).
- **Après correction** : 199 Mo par instance (+4,7 Mo de code compilé) ; 16 instances 385 % du temps réel, soit
  3,85 cœurs, une instance 23,7 à 24,6 % ; bloc d'une instance médiane 0,67 ms, 99 % 1,06 ms, 99,9 % 1,22 ms, pire
  2,48 ms ; **0 bloc sur 55 120 au-delà de l'échéance**.
- **Éditeurs** : le premier 11 Mo ; avec l'aperçu de l'écran Hit 99 Mo (moteur partagé, construit d'avance) ; quatre
  éditeurs 135 Mo (12 Mo par éditeur de plus).
- **Reste à constater dans Live seul** : son compteur CPU et ses threads, l'undo après un Kit, Ctrl+S, Push 2 (liste
  en Configure), les Outs depuis un Drum Rack.
