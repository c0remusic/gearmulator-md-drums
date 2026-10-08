# Coût du second moteur de l'écran hit

Type: grilling
Status: open
Assignee: —
Blocked by: —

## Question

L'écran hit re-rend le Hit pendant qu'un knob tourne avec un second `mdDrums::Engine` (22-27 ms par rendu, ~88 Mo,
~45 ms de construction, flash de 8 Mo à garder ou relire). Ce coût est-il accepté, et sous quelle forme ?

- Un moteur par instance, un moteur partagé par toutes les instances du processus, créé à la première demande et
  libéré après inactivité, ou pas de second moteur (capture live seule).
- Mémoire réelle mesurée avec 16 instances dans Live.
- Fidélité : bruit qui ne se répète pas d'un Hit à l'autre, phase d'un LFO FREE qui court entre deux aperçus.
