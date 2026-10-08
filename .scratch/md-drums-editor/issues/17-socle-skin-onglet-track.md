# Socle du skin v22 et onglet Track

Type: task
Status: open
Assignee: —
Blocked by: 08

## Question

Construire le socle du skin v22 tel que le ticket 08 l'a fixé, et l'onglet Track dessus, statique.

- **Inter 4.1** : archive `Inter-4.1.zip` de github.com/rsms/inter (release v4.1, 33,7 Mo, accordé au ticket 08),
  téléchargée dans un dossier vide du scratchpad. Quatre TTF statiques (400, 500, 600, 700), réduits au latin, chiffres
  tabulaires gelés dans les glyphes par défaut avec fonttools ; le script qui le fait dans
  `.scratch/md-drums-editor/tools/`. Les TTF et la licence OFL dans le dossier du skin ; `NOTICE.md` les cite.
- **`vknob`** : élément sous-classe d'`ElemKnob`, enregistré par l'éditeur de MD Drums (instancer, comme
  `mdPixelPerfectPanel.cpp:126-127`), qui dessine sur un `ElemCanvas` interne la géométrie du HANDOFF (« Controls » :
  piste de 270°, arc, pointeur, état édité, bipolaire depuis 64 ; tailles 64 et 28 px) et ne repeint que quand sa
  valeur, son défaut ou sa taille changent. Règles d'entrée du HANDOFF (« Interaction ») : 1,5 px par pas, Shift quatre
  fois plus fin, molette 2 pas (1 avec Shift), ↑ ↓ 1 pas (10 avec Shift) sur le knob focalisé, double-clic au défaut.
  Variante verticale pour le fader du Mix, utilisée au ticket 18.
- **Skin** `skins/mdDrums/` (RML et RCSS écrits à la main), premier skin de `addSkin`, donc chargé par défaut ; le
  minimal reste second. RCSS : tokens de couleur du HANDOFF, classes de grille `.c1`-`.c16` et `.w1`-`.w16`, conteneurs
  de bande absolus et enfants en décalages relatifs. Barre du haut complète (logo, onglets, Size, label et mètre Main
  sans source pour l'instant) ; onglet Track : head band, liste des Tracks, bandes SYN, EFX et ROUTING liées à
  `partCurrent`, cadres des trois écrans vides. Onglets Mix et Master présents mais vides (`tabgroup`).
- **Comportement** : Size parcourt 75, 100, 125, 150 % et écrit la clé `scale` de TUS ; un clic sur une ligne de la
  liste ou sur ‹ › change la Track montrée (`setCurrentPart`) ; M et S des lignes liés à Mute et Solo de leur Track.
  Rendu plafonné à la fréquence de l'écran (`getAcceleratedRefreshRateHz`), à la demande.
- **Boîtes de référence** : la maquette `Design/v22-test.html` ouverte dans un navigateur, onglet Track, Track 1, les
  boîtes de chaque élément à ID (et des éléments sans ID que l'audit vise) exportées, recalées de 16 px sous la barre
  (la maquette dessine une barre de 40 px, le HANDOFF 56), dans `Design/v22-boxes.json` avec le script d'export.
- **Audit** : un test charge le skin en rendu logiciel à 100 %, compare chaque boîte de `v22-boxes.json` à la boîte
  RmlUi de même ID (0,5 px de tolérance, comme `requireRect` de `mdEditorSectionTest.cpp:245-253`), puis vérifie les
  règles du HANDOFF : boîtes et baselines sur des multiples de 4 (sonde 0 x 0 dans la ligne du texte), 16 px aux bords
  et dans les écrans, 28 px de part et d'autre des règles.
- **Banc entrée→pixel** : un test injecte des drags et des clics sur un knob et une ligne de liste, en rendu logiciel
  headless, et mesure le temps jusqu'à la frame qui montre le changement ; échec au-delà de 33 ms. GL3 mesuré à la main
  dans Live (cible : deux frames d'écran, ~12 ms à 164 Hz), avec plusieurs instances, coût CPU noté.

Fini quand : le plug-in ouvre le skin v22 dans Live, onglet Track éditable Track par Track ; l'audit et le banc sont
verts et ajoutés au garde-fou ; aucun `Plugin::withDeviceLocked` par frame ou par clic ; garde-fou vert ; le
`CLAUDE.md` décrit le skin écrit à la main ; poussé sur `main`.
