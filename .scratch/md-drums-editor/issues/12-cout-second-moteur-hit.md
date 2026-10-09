# Coût du second moteur de l'écran hit

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: —

## Question

L'écran hit re-rend le Hit pendant qu'un knob tourne avec un second `mdDrums::Engine` (22-27 ms par rendu, ~88 Mo,
~45 ms de construction, flash de 8 Mo à garder ou relire). Ce coût est-il accepté, et sous quelle forme ?

- Un moteur par instance, un moteur partagé par toutes les instances du processus, créé à la première demande et
  libéré après inactivité, ou pas de second moteur (capture live seule).
- Mémoire réelle mesurée avec 16 instances dans Live.
- Fidélité : bruit qui ne se répète pas d'un Hit à l'autre, phase d'un LFO FREE qui court entre deux aperçus.

## Answer

Grilling du 2026-10-09 (l'utilisateur a pris les recommandations), sur une mesure faite pour lui
(`mdDrumsPreviewTest`, hors garde-fou) : le moteur MD Drums complet coûte 166 Mo, se construit en 54 ms et rend un Hit
de 0,8 s en 53-88 ms ; mdEngine seul (sans master : l'écran Hit montre la sortie propre de la Track) coûte 84 Mo, 22 ms,
25-37 ms. Un Hit refrappé sur un même moteur diffère (bruit, phases FM, état de la voix : 34 905 échantillons sur
35 280 pour EFM-BD) ; un moteur neuf redonne le même.

- **Forme** : un mdEngine sans master, partagé par toutes les instances du processus, créé à la première demande d'un
  éditeur ouvert, libéré après 30 s sans demande ; il rend sur un thread à lui, la dernière demande seulement.
- **Image** : voix neuve à chaque rendu, même réglage même image ; l'écran montre « un Hit avec ces réglages », le vrai
  coup reprend la place dès qu'il sonne ; un LFO FREE part de sa phase de départ.
- **Bascule** : un changement du son de la Track montrée (machine, 24 paramètres, level, LFO) lance un rendu à la
  vélocité du dernier coup (sinon celle de la bande de tête), note « Preview, velocity 100 » ; un vrai coup reprend la
  capture live, note « Last hit, velocity 110 ».
- **Aperçu du browser** : la machine choisie est rendue à ses SYN par défaut et aux autres paramètres de la Track, même
  sans écoute ; l'écoute cochée la fait sonner et la capture prend le relais. Pas de rendu au survol.
- Mémoire réelle avec 16 instances dans Live : à mesurer à l'installation.

Ticket créé : 28 (second moteur de l'écran Hit et de l'aperçu, construction).
