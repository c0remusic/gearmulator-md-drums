# Jouer la Track, vélocité, pas à pas sur la machine

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: 22

## Question

Ce que le HANDOFF spécifie de la bande de tête et du browser et qu'aucun ticket n'a encore placé (carte, « Tranches
de construction après le skin »), sans décision nouvelle : tout est dans `Design/HANDOFF.md` (« Head band »,
« Interaction ») et la maquette (`v22-ui.js`).

- **Touche play** : joue la Track montrée à la vélocité de la bande (`Controller::audition`, ticket 22) ; s'allume
  seulement pendant l'appui, jamais sur un Hit (déjà en RCSS).
- **Vélocité** : la valeur se règle par un drag vertical, 1,5 px par pas, de 1 à 127 ; la barre en dessous
  proportionnelle (64 px à 127) ; gardée dans les réglages du plug-in, comme « Listen while choosing ».
- **Espace**, rien de focalisé : joue la Track montrée. La maquette joue à 100, la touche play à la vélocité de la
  bande : Espace prend la vélocité de la bande, comme la touche qu'elle remplace.
- **Nom de machine** : molette, ou ↑ ↓ quand il a le focus, la machine suivante ou précédente dans l'ordre du browser
  (GND, TRX, EFM, E12, P-I, ROM 01-32), en boucle ; avec l'écoute cochée, la Track joue, comme dans la maquette.
- **Browser ouvert** : ↑ ↓ la machine suivante, ← → la première machine de la famille suivante, Enter garde
  (« Keep »), Esc annule (fait au ticket 22).

Fini quand : chaque geste vérifié dans `mdDrumsSkinTest` ; garde-fou vert ; poussé sur `main`.

## Answer

Tout dans `mdDrumsTrackView.*`, le clavier passant par l'écouteur de l'éditeur à la racine du contexte, en capture
(`Editor::create`), avant les raccourcis de la pile.

- **Touche play** : joue à l'appui (Mousedown), pas au relâchement, par `Controller::audition` à la vélocité de la
  bande ; `:active` l'allume pendant l'appui.
- **Vélocité** : drag vertical sur la valeur (`drag: drag`), 1,5 px par pas, 1 à 127 ; barre `round(v × 64 / 127)` ;
  clé `playVelocity` des réglages du plug-in.
- **Espace** : joue la Track montrée à la vélocité de la bande. RmlUi donne le focus à tout élément cliqué, donc
  « rien de focalisé » devient « pas un champ de texte » (`input`, `textarea`) ; aucun n'existe encore.
- **Nom de machine** : molette, ou ↑ ↓ quand il a le focus ; l'ordre est celui des cellules du browser, lues dans le
  RML (`mach_N`), en boucle ; une machine hors du browser repart de sa place dans l'ordre des ids. Avec l'écoute
  cochée, la Track joue (vélocité 100, comme la maquette).
- **Browser** : ↑ ↓ machine suivante, ← → première machine de la famille voisine, Enter garde.

**Mesures** (`mdDrumsSkinTest`) : drag de 15 px vers le haut, 100 → 110, barre 55 px ; touche play et Espace mettent
en attente la Track montrée à 110 ; molette TRX-B2 → EFM-BD → TRX-B2 ; dans le browser ↓ 28 → 32, → 48 (E12), ← ← 16
(TRX), Enter ferme en gardant TRX-BD.
