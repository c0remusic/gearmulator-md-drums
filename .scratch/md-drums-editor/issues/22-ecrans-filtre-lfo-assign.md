# Écrans Filter/EQ et LFO, ASSIGN, écoute du browser

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: 18

## Question

Les écrans calculés côté éditeur et ce qui reste inerte dans le skin v22.

- **Filter and EQ** : réponse de 20 Hz à 20 kHz (graduations 100, 1k, 10k ; ligne « 0 dB »), 2 px accent, point EQ en
  carré de 6 × 6 encre, note « SRR n » quand SRR > 0 ; la vraie réponse d'un `TrackFx` privé à l'éditeur sur une
  impulsion (~0,5 ms, ticket 05), recalculée quand un paramètre d'EFX change.
- **LFO** : la forme des routines de l'OS portées (ticket 05, bit-exact), zoomée sur ce que l'onde parcourt (valeur du
  knob visé ± LFOD, bornée 0-127), valeurs haute et basse en 12 px, « LFOD 0: no modulation » ; corrections du HANDOFF
  que le ticket 05 a relevées (amplitude ±126, scie qui descend deux fois par période, RMP et EXP en one-shot relancés
  à chaque Hit, RND à 8 pas, LFOM qui fond vers la forme 2 inversée).
- **ASSIGN** : armé, il lit « Cancel » en accent, le titre « Click a knob to modulate », chaque knob un anneau accent
  de 1 px (`vknob`) ; un clic sur un knob, de cette Track ou d'une autre, règle `LfoTrack` et `LfoParam` du LFO qui a
  armé ; Esc et Cancel désarment.
- **Noms soulignés** : le nom d'un paramètre qu'un LFO module, souligné en accent.
- **Listen while choosing** : la case du browser ; cochée, choisir une machine joue la Track (vélocité 100).

Fini quand : les deux écrans suivent les knobs sans allocation sur le thread audio et dans le budget entrée→pixel ;
ASSIGN et l'écoute fonctionnent dans Live ; garde-fou vert ; poussé sur `main`.

## Answer

- **Filter and EQ** (`mdDrums/mdDrumsFilterResponse.*`, `mdDrumsPlugin/mdDrumsFilterView.*`) : la réponse vient des mots
  de coefficients que le DSP calcule lui-même. Un `TrackFx` privé passe un bloc sur les tables du moteur (partagées
  par `EngineT::tables()`, sans copie ni second décodage de l'OS) ; le passe-bas et le passe-haut laissent leurs cibles
  dans l'état de la Track (`y[$2a-$2c]`, `y[$2e-$30]`), l'EQ ses cinq mots (`TrackFx::eqWords`, ajouté à mdEngine). Le
  produit EQ × passe-bas² × passe-haut² s'évalue à toute fréquence en double ; une courbe de 369 pixels coûte 46 µs.
  La réponse impulsionnelle du ticket 05 a été écartée : le passe-haut à FLTF 0 sonne encore après 16 384 échantillons
  et la récursion en virgule fixe garde un décalage propre (+6 dB faux à 20 Hz). 0 dB = une Track intacte à 1 kHz, si
  bien que le filtre ouvert montre sa propre chute. Graduations 100, 1k, 10k, ligne « 0 dB » (y 107), +24 à −29 dB
  (la maquette), courbe accent 2 px bornée au tracé, point EQ 6 × 6 encre sur la courbe à l'angle des pôles (boost) ou
  des zéros (coupe), note « SRR n ». Recalcul au dessin, une fois par frame au plus.
- **LFO** (`mdDrums/mdDrumsLfo.*`, `mdDrumsPlugin/mdDrumsLfoView.*`) : les routines de l'OS portées depuis leur
  désassemblage (oscillateur $1000088, onde $204c94 et ses six formes, apply $1000332 et $10001e8), bit-exact. Tracé :
  un Hit de la Track au bord gauche puis course libre (HOLD aussi : l'écran montre ce que les Hits échantillonnent),
  1 à 6 périodes selon LFOS (5 pour la chute entière d'un RMP, 8 pour un EXP), une valeur par tick, zoom sur les
  valeurs atteintes, nommées en haut et en bas de la colonne d'étiquettes ; « LFOD 0: no modulation ». Le RND
  dessine une réalisation à graine fixe. Tempo : celui de l'hôte, 125 avant.
- **ASSIGN** : « Assign » arme (« Cancel » en accent, titre « Click a knob to modulate » qui ouvre le menu) ; chaque
  knob des bandes, sauf les slots SYN que la machine n'utilise pas, porte un anneau accent de 1 px hors de sa boîte
  (2 px au survol, `Knob::setTarget`) ; un clic, sur cette Track ou une autre montrée entre-temps, règle `LfoTrack` et
  `LfoParam` du LFO qui a armé, sans bouger le knob, puis remontre sa Track. Cancel, Esc, un onglet ou un overlay
  désarment. **Esc** ferme aussi les overlays (le browser comme Cancel), avant les réglages de la pile.
- **Noms soulignés** : le nom visé par un LFO de profondeur non nulle, en encre, souligné accent 2 px à 4 px sous sa
  ligne de base (les noms passent dans un `<span>`).
- **Listen while choosing** : cochée par défaut, gardée dans les réglages du plug-in ; une machine choisie joue la
  Track (vélocité 100) par `Controller::audition`, envoyée au bloc audio suivant après les changements de paramètres
  publiés avant elle : le Hit sonne la machine choisie.

**Mesures** :
- `mdDrumsScreensTest` (nouveau, au garde-fou) : port LFO contre les routines de l'OS dans le moteur, 108 paires de
  formes × modes, 24 840 blocs, 2 259 ticks, 670 Hits : 0 différence (phase, deux formes, sorties tenues, générateur
  RND, valeur écrite). Réponse contre la chaîne jouant des sinus (1,5 s d'établissement), 11 réglages, 207 points
  au-dessus de −40 dB : à 0,054 dB au pire (0,013 dB hors d'un point à −37 dB). Chiffres : intact −0,53 dB à 20 Hz,
  −2,22 à 10 kHz, −4,70 à 20 kHz ; passe-haut à −3 dB FLTF 32 94 Hz, 64 656 Hz, 96 4,1 kHz, 112 9,2 kHz, 127
  14,5 kHz (le ticket 05 estimait 727 Hz, 5,4, 11,5 et 15,6 kHz) ; passe-bas FLTW 64 136 Hz, 96 1,11 kHz, 112
  3,23 kHz, 127 12,35 kHz ; FLTQ 127 +18,3 dB, 64 +4,8 dB ; EQG 127 +14,9 dB et EQG 0 −14,8 dB à EQF 64, +12,3 dB à
  EQF 127 (la division du DSP sature : −1,55 dB au continu, celui de la chaîne).
- `mdDrumsSkinTest` : courbes, pic résonant et point EQ au pixel, « SRR 12 » ; onde LFO pleine hauteur, 0 à 127 ;
  soulignement aux rangées 564-565 ; ASSIGN (anneau, choix sur la Track 3, retour à la Track 1, knob immobile), Esc,
  case d'écoute ; textes sur le pas de 4. Drag d'un knob avec recalcul du filtre : 5,0 ms en moyenne, 5,5 ms au pire.
- `mdDrumsProcessorTest` : TRX-B2 choisie puis jouée depuis l'éditeur dans le même bloc : énergie 31,178 contre
  31,203 pour la même note de l'hôte (EFM-BD 9,054) ; 48 kHz 33,961 contre 33,960. Latence inchangée (67-67 à
  44,1 kHz).

Dans Live : à vérifier par l'utilisateur.
