# Links et Chokes dans l'éditeur : construction

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 29

## Question

Construire ce que le ticket 29 a fixé : `Controller::setLink`, `setChoke` (Kit de base, `$65`/`$66` au Device), les
deux lignes de la bande de tête, le menu « Links and Chokes » par-dessus l'écran Hit, Esc et Done.

Fini quand : `mdDrumsProcessorTest` montre le Device et l'état suivre un Link et un Choke, et Save s'allumer ;
`mdDrumsSkinTest` ouvre le menu, choisit, voit les lignes suivre, la Track elle-même inerte, Esc fermer, les lignes sur
la grille de 4 px ; garde-fou vert ; poussé sur `main`.

## Answer

Construit le 2026-10-09.

- `Controller::setLink`, `setChoke`, `link`, `choke` : Kit de base du Controller (donc `playedKit`, Save, Slots, set),
  `$65`/`$66` au Device par `addMidiEvent` ; une Track visée par elle-même devient Off.
- `LinksView` (`mdDrumsLinksView.*`) : les deux lignes de la bande de tête (`link_text`, `choke_text`, colonne 8,
  y 84 et 124), le menu `linksmenu` par-dessus l'écran Hit (`TrackView::Overlay::Links`), cellules `links_link*` et
  `links_choke*`, « Off », la Track elle-même `self` (grisée, inerte) ; minuterie à 10 Hz pour suivre un Kit chargé ou
  un état rendu.
- Vérifié : `mdDrumsProcessorTest` (Device et état suivent un Link et un Choke, le Kit joué diffère, Off) ;
  `mdDrumsSkinTest` (lignes sur la grille de 4 px, menu ouvert par l'une ou l'autre ligne, cellule→pixel 10,4 ms, Track
  elle-même inerte, Save allumé, un choix garde le menu ouvert, Esc et Done ferment) ; image `linksmenu` ajoutée aux
  vues que `MD_DRUMS_SKIN_PNG` écrit.
