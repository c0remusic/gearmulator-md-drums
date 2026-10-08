# Réponses filtre, EQ et formes LFO pour les écrans

Type: research
Status: resolved
Assignee: c0remusic (agent de recherche)
Blocked by: —

## Question

Que peut donner le moteur pour dessiner les écrans de la bande EFX (filtre et EQ) et de la bande ROUTING (LFO) à
partir de son propre calcul, au lieu des modèles de la maquette ?

- Filtre (FLTF, FLTW, FLTQ) et EQ (EQF, EQG) de la chaîne de Track (`mdEngine/.../TrackFx.*`, tables lues dans
  `fw::Firmware::dspB`) : coefficients accessibles, réponse en fréquence calculable hors audio, coût.
- LFO : tables de formes de l'OS (TRI SAW SQR RMP EXP RND), combinaison shape1/shape2, modes FREE TRIG HOLD, et
  comment la routine LFO de l'OS que mdEngine appelle produit la valeur modulée (`HostModel.cpp`, `setLfo`).
- Ce qui reste un modèle faute de données.

HANDOFF : « draw the engine's responses where it can give them, the MD's LFO shapes from its own tables ».
Lecture seule. Livrable : `research/05-ecrans.md`.

## Answer

Filtre et EQ : le moteur peut donner la vraie réponse. Les deux étages sont linéaires et leurs coefficients viennent
des tables de l'OS que `TrackFx::Tables` contient déjà. Un `TrackFx` privé, côté éditeur, sur une impulsion
(`stopAfter = AfterFilter2`, lecture de `scratch()`), suivi d'une FFT, donne la réponse bit-exacte en moins d'une
milliseconde, sans verrou ni snapshot. Il suffit d'un accès aux `Tables`, immuables, que `EngineT` garde privées. Une
formule flottante tirée des mêmes tables peut ensuite servir, une fois prouvée contre la mesure. Les commentaires
inversent les deux sections : la section à FLTF + FLTW est un passe-bas 4 pôles, celle à FLTF un passe-haut.

LFO : l'OS n'a pas de table de formes, mais six petites routines, plus une décroissance et une formule de mélange,
toutes décodées. Un portage C++ vérifié bit-exact contre l'OS dessine l'onde réelle sans toucher au moteur. Plusieurs
faits contredisent la maquette : la scie descend deux fois par période, RMP et EXP sont des one-shots, RND a une
amplitude moitié, et LFOM fond vers l'inverse de la forme 2. Trouvé en passant : RND ne bouge pas dans mdEngine, faute
de graine ; un chargement de kit qui copie les 36 octets du bloc LFO la fournit.

Restent des modèles : AMD, SRR et DIST, non linéaires et non dessinés, une réalisation de RND, et la vue statique de
TRIG et HOLD, qui dépendent des frappes. Les chiffres Hz et dB du doc sont à confirmer par la mesure.

Détail : research/05-ecrans.md
