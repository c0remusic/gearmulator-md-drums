# Mix, Master, Kits et overlays du skin v22

Type: task
Status: open
Assignee: —
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
