# Mix, Master, Kits et overlays du skin v22

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: 17

## Question

Finir le skin v22 statique sur le socle du ticket 17, puis retirer le skin minimal.

- **Mix** : 16 strips (numéro, machine, PAN bipolaire, mètre sans source, fader `vknob` vertical lié à Level, valeur,
  M, S, Out), hairlines, strip de la Track montrée.
- **Master** : RHYTHM ECHO, GATE BOX, EQ et DYNAMIX, 8 knobs chacun liés aux 32 paramètres Master (page 8) ; bande
  Sends, knobs de 28 px DEL et REV de chaque Track.
- **Kits** : overlay sous la barre du haut, 64 Slots, panneau du Slot choisi, actions et seconds clics, statiques.
- **Overlays de Track** : browser de machines, menu cible du LFO, ASSIGN armé, chips LFO (Shape 1, Shape 2, Mode) ;
  cachés par `display: none`.
- **Boîtes de référence** : `Design/v22-boxes.json` étendu à chaque vue et overlay de la maquette, même export ;
  l'audit du ticket 17 les couvre toutes.
- **Retrait du minimal** : `skins/mdDrumsMinimal/`, son `addSkin` et `.scratch/md-drums-editor/tools/gen_minimal.py`
  supprimés ; le test « 576 liés » de `mdDrumsProcessorTest` devient « 576 atteignables » : chaque paramètre a un
  contrôle dans le skin v22, Track par Track (`setCurrentPart` sur chacune des 16 parts pour les contrôles liés à
  `partCurrent`).

Fini quand : les 576 paramètres sont atteignables dans le skin v22 ; l'audit couvre toutes les vues et overlays et est
vert ; le banc entrée→pixel reste vert ; garde-fou vert ; skin minimal et `gen_minimal.py` retirés, `CLAUDE.md` à jour ;
poussé sur `main`.

## Answer

Le skin v22 est complet et statique ; le minimal et son générateur sont retirés, le JSON des paramètres s'édite à la
main.

**Vues** (RML écrit à la main, `skins/mdDrums/mdDrums.rml`, 754 lignes) :
- **Mix** : 16 strips liés à `part0`-`part15` : numéro, machine, PAN (`vknob` bipolaire), mètre (sans source), fader
  de Level (`vknob` en variante `fader` : rail de 2 px, cap de 48 x 8, drag sur la hauteur de 492 px, soit 3,9 px par
  pas comme la maquette), valeur, M, S, Out ; hairlines aux milieux des gouttières ; strip montré éclairé, sa barre
  accent.
- **Master** : les quatre effets liés aux 32 paramètres Master (`part0`), conteneurs de 272 px à décalages relatifs
  (+28, +60, +116, +140, +212, +228), EQ LG, HG, PG bipolaires ; sends DEL et REV de chaque Track en knobs de 28 px.
- **Kits** : sous la rangée du Kit (qui reste), 64 Slots en quatre colonnes, panneau du Slot avec les machines du Kit
  joué (`Machine_text` de chaque part), actions et note ; contenus fixes (« Empty ») jusqu'à la tranche Kits.
- **Browser de machines** : à la place des bandes ; 82 cellules, une par machine offerte (GND, TRX, EFM, E12, P-I,
  ROM-01 à 32), boutons radio liés à `Machine` : un clic charge la machine, ses SYN par défaut avec ; Cancel ramène la
  machine d'avant, Keep ferme ; aperçu (nom, famille, case « Listen while choosing », écran vide).
- **Menu cible du LFO** : sur l'écran LFO ; 16 Tracks (`LfoTrack`) et 24 paramètres (`LfoParam`) en boutons radio,
  nommés d'après la machine de la Track visée ; choisir le paramètre ferme le menu, Done aussi.

**Comportement** (`mdDrumsTrackView.*`) : la Track montrée se choisit aussi par les numéros et machines des strips et
des sends ; le nom de machine ouvre et ferme le browser, la cible du LFO le menu, la rangée du Kit les Kits ; un onglet
les ferme tous, et changer de Track ferme le browser (comme la maquette). ASSIGN reste inerte : son armement et le clic
sur un knob viennent avec la tranche LFO.

**Référence** : `Design/v22-export-boxes.js` exporte maintenant six vues (Track, Mix, Master, Kits, browser, menu LFO)
dans `v22-boxes.txt` en sections `[vue]`, depuis `Design/v22-export.html` servie en HTTP ; il réapplique l'écart des
marges décidé après le ticket 17 (x 240). Section Track identique à l'export précédent. Les éléments larges comme leur
texte (cible du LFO, Assign, Done du menu) sont vérifiés par leur rangée et leur bord droit.

**`mdDrumsSkinTest`** (rendu logiciel, 100 %) :
- 881 boîtes sur 881 identiques à la maquette, vue par vue ;
- 685 baselines de texte sur le pas de 4, aucun texte qui déborde (titres à 22 px dans une rangée de 24 : ligne de
  24 ; cellules de machine : bordure basse dans la boîte, ligne de 22) ;
- 576 paramètres sur 576 ont un contrôle, Track par Track ;
- entrée→pixel : drag 4,0 à 5,4 ms en moyenne (pire 4,6 à 6,4), clic sur une ligne 13 à 17 ms (le document a
  quadruplé ; seuil 33 ms).

`mdDrumsProcessorTest` vérifie que le skin v22 s'ouvre à 1296 x 824 ; le compte des 576 est passé au test du skin.
