# Fabrication du skin v22

Type: grilling
Status: open
Assignee: —
Blocked by: 06

## Question

Comment produire le skin RmlUi de l'éditeur v22 et le garder fidèle à `Design/HANDOFF.md` ?

- RML écrit à la main, généré par script depuis les coordonnées du HANDOFF (comme `.scratch/md-editor/tools/gen_editor.py`
  pour Gearmulator MD), ou converti depuis la maquette `v22-ui.js`.
- Contrôles : knob vectoriel sur `juceRmlUi::ElemCanvas` (`ElemKnob` dessine depuis un sprite sheet), toggles,
  écrans, comment ils se lient par `param=`.
- Police Inter embarquée (`DataProvider` charge chaque `.ttf` listé), grille 1296×824 et zoom 75-150 %.
- Vérification : contrôle automatique des boîtes, des baselines et des marges sur le rendu (comme l'audit de la
  maquette), et banc entrée→pixel.
