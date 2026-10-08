# Lecture du moteur par l'éditeur

Type: grilling
Status: open
Assignee: —
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
