# Push : une page par Track, pour automatiser en direct

Type: task
Status: resolved
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

Construit le 2026-10-14 :

- `Controller::preparePushList` touche, chacun dans un geste et dans l'ordre, Level, FLTF, FLTW, FLTQ, DIST, SRR, DEL et
  REV de la Track 1, puis de la Track 2, jusqu'à la 16 (`Controller::PushTrackParameters`, 128) ; « Prepare Push list:
  a page per Track » dans le menu des réglages. L'ancienne liste devient `preparePushRelays`, « Prepare Push list: Track
  shown and master ».
- Le formateur de parties du Controller nomme les paramètres de Track « T01 » à « T16 ».
- `docs/md-drums-push2.md` décrit les deux listes.
- **Vérifié** (`mdDrumsProcessorTest`) : la liste par Track déplace les 128 dans l'ordre attendu, de « T01 Level » à
  « T16 REV », 128 gestes fermés ; celle des relais ses 64 comme avant ; le premier paramètre s'appelle « T01 Machine »
  et le dernier d'une Track « T16 Out », IDs inchangés. Reste à valider dans Live avec Push (l'utilisateur).
