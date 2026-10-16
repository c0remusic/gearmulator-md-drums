# Le LFO sur les knobs : voir la valeur modulée bouger

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-16)
Blocked by: 16, 22

## Question

Demande de l'utilisateur (2026-10-16) : « voir comment le LFO module un paramètre en faisant bouger le paramètre
(knob), pour voir comment il modifie la valeur ». Aujourd'hui l'écran LFO trace la forme (`mdDrumsLfo.*`, routines de
l'OS portées), ASSIGN cercle le knob visé (`Knob::setTarget`), mais rien ne montre la valeur que joue le moteur.

## Piste

- Le moteur a la valeur jouée : `HostModel::voiceParams(track)`, les 24 valeurs de la voix après lissage et LFO.
- La Device l'écrit dans la Telemetry sur le thread audio, sans verrou, une fois par bloc rendu, pour les 16 Tracks
  (24 octets chacune, comme les crêtes des sorties).
- Le cadre de l'éditeur la lit pour la Track montrée : un knob dont la valeur jouée diffère de la sienne dessine un
  second repère sur son anneau (la valeur jouée), qui bouge à la cadence d'affichage ; sa valeur à lui ne bouge pas
  (c'est le paramètre de l'hôte). Plusieurs LFO sur un même paramètre : la somme, puisque c'est la valeur jouée.
- Coût à mesurer : écriture audio (16 × 24 octets par bloc), lecture et dessin par cadre (`mdDrumsSkinTest`, « frame »).

## Answer

Choix de l'utilisateur (2026-10-16) : juste après le ticket des slots, sur recommandation.

Construit le 2026-10-16 :

- `Engine::played(track)` : les 24 mots de la voix (`HostModel::voiceParams`, valeur << 7, après lissage et LFO).
- `Telemetry::setPlayed` / `played` : la Device les écrit à la fin de chaque rendu, 16 Tracks, atomiques sans verrou.
- `Knob::setPlayed` : un point accent de 5 px sur la piste du knob à la valeur jouée ; l'aiguille reste à la valeur
  du paramètre. `LfoView::followPlayed` le donne aux knobs dont le nom est souligné (une Track, n'importe laquelle,
  y vise un LFO avec de la profondeur), l'enlève aux autres ; rien n'est écrit quand rien ne change.
- Mesuré : `mdDrumsDeviceTest`, triangle libre à pleine profondeur sur FLTF de Track 1 : la valeur jouée va de 0 à
  127,99, FLTW reste à la sienne ; la Device avec Telemetry 25,15 % d'un cœur contre 24,99 % sans (avant : 24,81
  contre 24,61), 0 allocation. `mdDrumsSkinTest` : le point suit la Telemetry (40, puis 100,5), FLTF reste à 64,
  13 pixels accent dans le knob, aucun sans profondeur ; cadre inactif 1,0 µs en moyenne, inchangé.
