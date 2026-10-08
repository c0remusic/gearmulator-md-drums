# Socle du skin v22 et onglet Track

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
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

## Answer

Le skin v22 (`source/elektron/md/mdDrumsPlugin/skins/mdDrums/`) est le skin par défaut ; le minimal reste second,
choisissable, jusqu'au ticket 18. L'onglet Track est construit, statique sauf ce que le ticket demandait.

**Inter.** `Inter-4.1.zip` (33,7 Mo, release v4.1 de github.com/rsms/inter) ; `.scratch/md-drums-editor/tools/make_inter.py`
en tire Regular, Medium, SemiBold et Bold (`usWeightClass` 400 à 700, famille « Inter »), réduits à Latin-1 et
quelques signes (236 glyphes, 36 à 38 Ko chacun). Seuls les chiffres 0-9 sont gelés en tabulaires : la feature `tnum`
d'Inter élargit aussi l'espace, le tiret et la ponctuation (le tiret passe de 942 à 1328 unités), ce qui aurait changé
tous les libellés. Le RmlUi du dépôt ne crénelle qu'avec une table `kern` : le script y recopie les paires GPOS des
caractères ASCII (909 paires en Regular, 1 095 dans les autres). `▾` (U+25BE) manque à Inter : la maquette le prenait
dans une police de secours. Licence OFL livrée (`Inter-OFL.txt`, sans nom de police réservé) et citée dans `NOTICE.md`.

**Éléments.** `<vknob>` (`mdDrumsKnob.*`), sous-classe d'`ElemKnob` : géométrie de `knobSvg` de la maquette (rayon
size/2 - 3, piste de 2 px, arc de 3 px depuis le minimum ou depuis 64 avec l'attribut `bipolar`, pointeur de 35 % du
rayon à 5 px du bord), dessinée sur un `ElemCanvas` aligné sur les pixels, repeinte seulement quand la valeur, la plage
ou le défaut changent. Drag à 1,5 px par pas (`speed` = plage × 1,5), Shift ×0,25, molette 2 pas (1 avec Shift, axe
horizontal accepté car Shift tourne certaines molettes), ↑ ↓ 1 pas (10 avec Shift). `<vglyph>` (`mdDrumsGlyph.*`)
dessine le triangle de la touche play, le chevron du nom de machine et `▾`, dans sa couleur RCSS. Les deux sont
enregistrés dans `onRmlContextCreated`, avant le chargement du document.

**Skin.** RML et RCSS écrits à la main : classes `.c1`-`.c16` et `.w1`-`.w16`, bandes en conteneurs sans boîte (ils ne
prennent aucun clic) liés à `partCurrent`, enfants à +28, +60, +84, +156, +172 ; lignes de la liste liées à
`part0`-`part15`. Un texte de 14 px sur une rangée de 24 prend une ligne de 22 px pour poser sa baseline sur le pas de
4, comme les 2-3 px de padding de la maquette (logo : 54 sur 56 ; valeurs : 14 sur 16). Les valeurs passent d'encre
pâle à encre quand elles quittent leur défaut (`data-class-edited`). Onglets par `tabgroup` ; Mix et Master vides.

**Comportement** (`mdDrumsTrackView.*`) : clic sur le numéro ou la machine d'une ligne, ‹ ›, changent la Track montrée
(`setCurrentPart`) ; la ligne s'allume et sa marque suit ; le head band dit « Track 01 », la famille de la machine en
anglais (« modelled analog » : les textes de `mdmachines` sont ceux, français, de md-mm), « Plays on Main » ou « Out
01 » d'après le paramètre Out ; SYN1-8 prennent les noms de la machine, « — » estompé et sans valeur pour un slot
inutilisé. Size parcourt 75, 100, 125, 150 % et écrit la clé `scale` de TUS (`evSetGuiScale`, comme sa page de
réglages). Le rendu accéléré est plafonné à la fréquence de l'écran principal (`verticalFrequencyHz`, 30 à 300, 60 si
inconnue). Aucun `withDeviceLocked`.

**Référence.** `Design/v22-export-boxes.js` exporte les boîtes de la maquette servie en HTTP, onglet Track, Track 01 :
213 boîtes dans `Design/v22-boxes.txt` (une ligne « clé x y largeur hauteur », plus simple à lire en C++ que le JSON
prévu), vérifiées identiques au DOM après écriture. La cible du LFO et Assign, larges comme leur texte, n'y sont pas :
le test vérifie leur rangée et le bord droit d'Assign (1280).

**`mdDrumsSkinTest`** (au garde-fou ; rendu logiciel, 100 %) :
- 213 boîtes sur 213 identiques à la maquette, à 0,5 px ;
- 159 baselines de texte, toutes sur un multiple de 4 (sonde inline-block 0 x 0), aucun texte qui déborde ;
- 464 paramètres sur 464 atteignables Track par Track (les 24, formes et mode du LFO, Mute, Solo) ; Machine, cible du
  LFO, Level et Out viendront avec le browser, le menu LFO et le Mix ;
- entrée→pixel, de l'événement à la fin du rendu logiciel de la frame qui le montre : drag d'un knob 3,4 à 5,9 ms en
  moyenne, 4,3 à 7,9 ms au pire selon les passages ; clic sur une ligne (les 24 knobs se relient et se repeignent)
  6,9 à 10,1 ms. Seuil 33 ms. L'attente de la frame suivante, une période au plus, n'y est pas.

`mdDrumsProcessorTest` vérifie que le skin par défaut est le v22, puis charge le minimal pour ses 576 liés.

**Reste à mesurer, à la main dans Live :** ouverture du skin, GL3 entrée→pixel (cible ~12 ms à 164 Hz), coût CPU avec
plusieurs instances.
