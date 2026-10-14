# Push : une page par Track, pour automatiser en direct

Type: task
Status: in progress
Assignee: c0remusic (session du 2026-10-14)
Blocked by: 34, 35

## Question

L'utilisateur veut régler MD Drums en direct depuis Push pour enregistrer l'automation, dans le plug-in lui-même (pas par
un device Max for Live). La liste Push du ticket 27 passe par les relais, qui visent la Track montrée : une automation
enregistrée sur un relais rejoue sur la Track montrée au moment de la lecture, pas sur celle d'origine. Un device Max ne
convient pas non plus : Push ne montre que des paramètres Max automatisables, donc un geste s'enregistrerait deux fois
(mesuré par md-drums-seq, issue 25).

Un VST3 ne nomme pas ses pages Push et ne choisit pas celle qui s'affiche ; Push montre la liste « Configure » du
device, 128 paramètres au plus, par pages de 8.

## Answer

Choix de l'utilisateur (2026-10-14) :

- **« Prepare Push list »** déplace désormais 128 paramètres des Tracks elles-mêmes : 16 pages, une par Track, Level,
  FLTF, FLTW, FLTQ, DIST, SRR, DEL, REV. L'automation enregistrée depuis Push tombe sur le paramètre de la Track.
  L'ancienne liste (32 relais, 32 effets master) reste dans une seconde entrée du menu.
- **Noms** : les paramètres de Track s'appellent « T03 FLTF » au lieu de « Track 3 FLTF », que Push tronquait à
  « TRACK 3 FL ». Les IDs ne changent pas : les sets et leurs automations restent valides.
