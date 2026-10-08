# Fabrication du skin v22

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
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

## Answer

### Faits relevés

- La maquette v22 est positionnée en absolu depuis des constantes (`X(c)`, `W(n)`, `BAND`, `v22-ui.js:13-21`) et
  donne à chaque contrôle l'ID de son paramètre (`t1_SYN1`, `t1_out`, `m_echo_TIME`, `v22-ui.js:42`). Ses y sont ceux
  d'une barre de 40 px (`v22-ui.js:16-21`) : tout ce qui est sous la barre est 16 px plus haut que dans le HANDOFF
  (barre de 56). Le script de l'audit du 2026-10-07 n'a pas été gardé ; seuls ses résultats sont écrits
  (`v22-ui-spec.md`, « Checked »).
- `ElemKnob` ne dessine que par sprite sheet (`juceRmlUi/rmlElemKnob.cpp:195-222`) ; aucun knob vectoriel n'existe en
  RmlUi. Il garde la liaison `param=` (via `ElemValue`), le drag (`speed`, `speedScaleShift`...), la molette et le
  double-clic. `ElemCanvas` dessine par `juce::Graphics` et ne repeint que sur `repaint()`.
- TUS met déjà tout le document à l'échelle : clic droit « GUI Scale », 50 à 300 %, mémorisé par utilisateur sous la
  clé `scale` du fichier de config (`pluginEditorState.cpp:341-358`, `pluginEditorWindow.cpp:57-67`).
- `RmlComponent` charge chaque `.ttf` des données (`juceRmlComponent.cpp:147-156`), jamais un `.otf`. Inter n'est ni
  dans le dépôt ni sur le PC. Le RmlUi du dépôt n'applique aucune feature OpenType (ni `font-variant`, ni HarfBuzz) :
  les chiffres tabulaires du HANDOFF ne s'obtiennent pas par RCSS.
- Rendu : GL3 si disponible, sinon logiciel ; 30 fps par défaut dans les deux, réglable
  (`juceRmlComponent.cpp:76-101`) ; rendu à la demande (`enqueueUpdate` à chaque entrée), vsync actif. L'écran de
  référence tourne à 164 Hz.
- Gearmulator MD met tout le comportement en C++ par ID d'élément, le RML reste statique. Lua ne sait pas changer de
  part (`rmlLuaParameters.cpp:378-391` : `getCurrentPart`, pas de `set`).
- Le dépôt n'a aucun banc entrée→pixel ; `mdEditorFluidityTest` mesure le coût d'une frame, pas le délai d'un geste.

### Décisions

**Fabrication.** Le RML et le RCSS du skin v22 sont écrits à la main, pas générés. Le RCSS porte la grille en classes
(`.c1`-`.c16` pour `left`, `.w1`-`.w16` pour la largeur) et des conteneurs absolus pour les bandes, dont les enfants
sont placés en décalages relatifs (+28, +60, +84, +156, +172) : chaque nombre du HANDOFF est écrit une fois.
`parameterDescriptions_mddrums.json` s'édite aussi à la main désormais (IDs figés, ajouts en fin de liste ; nombre et
IDs vérifiés par `mdDrumsProcessorTest`) ; `gen_minimal.py` disparaît avec le skin minimal.

**Comportement** en vues C++ par zone (liste des Tracks, head band, bandes, écrans, Mix, Master, Kits), branchées par
ID d'élément, comme Gearmulator MD. Pas de Lua.

**Knob.** Un élément `vknob`, sous-classe d'`ElemKnob` (liaison, drag, double-clic hérités), qui dessine sa géométrie
du HANDOFF sur un `ElemCanvas` interne au lieu d'un sprite, net à tout zoom, avec l'état « édité » (valeur différente
du défaut) sans frames supplémentaires. Règles du HANDOFF : 1,5 px par pas, Shift quatre fois plus fin, molette 2 pas
(1 avec Shift), ↑ ↓ 1 pas (10 avec Shift), double-clic au défaut. Le même élément, en variante verticale, sert de
fader au Mix.

**Inter 4.1** (github.com/rsms/inter, OFL, archive de 33,7 Mo, téléchargement accordé) : quatre TTF statiques (400,
500, 600, 700), réduits au latin, chiffres tabulaires gelés dans les glyphes par défaut avec fonttools (4.63, déjà
installé), licence OFL livrée avec le skin.

**Zoom.** Le contrôle Size parcourt 75, 100, 125 et 150 % et écrit la clé `scale` de TUS : Size et le clic droit
restent d'accord, et le choix est mémorisé par utilisateur. Le clic droit garde ses valeurs.

**Fluidité.** Rendu à la demande, plafonné à la fréquence de l'écran (164 Hz ici), rien quand rien ne bouge. Cible :
deux frames d'écran au plus de l'entrée au pixel en GL3 (~12 ms à 164 Hz). Au garde-fou, en rendu logiciel headless,
un banc injecte des drags de knob et échoue au-delà de 33 ms. Le coût est mesuré dans Live avec plusieurs instances
avant validation ; repli à 60 Hz si le GPU ou le CPU de Live en souffre.

**Vérification.** Les boîtes de la maquette v22 sont exportées une fois depuis un navigateur, par ID, recalées de
16 px, et commitées en JSON ; un test compare les boîtes RmlUi du skin à 100 % à ce fichier, puis applique les règles
du HANDOFF : boîtes et baselines (mesurées par une sonde 0 x 0 dans la ligne du texte) sur des multiples de 4, 16 px
aux bords et dans les écrans, 28 px de part et d'autre des règles. Il entre au garde-fou. Pas de capture côte à côte.

**Contenu.** Le skin v22 est statique et complet : onglets Track, Mix et Master, Kits, et tous les overlays du HANDOFF
(browser de machines, menu cible du LFO, ASSIGN armé, seconds clics des Kits), cachés par `display: none` et montrés
par les tranches. Deux comportements y entrent pour que le plug-in reste jouable dès le premier ticket : les onglets
(`tabgroup`, RML seul) et le changement de Track montrée (clic sur une ligne de la liste, ‹ ›, `setCurrentPart`). Un
test vérifie que chacun des 576 paramètres est atteignable par un contrôle, Track par Track (les bandes sont liées à
`partCurrent`), à la place de « 576 liés » du skin minimal.

**Découpe.** Deux tickets `task` : 17, le socle et l'onglet Track ; 18, Mix, Master, Kits et overlays, qui supprime le
skin minimal et `gen_minimal.py`. Le minimal reste jusqu'au 18 pour que Level, Out et les sends restent éditables
dans Live entre les deux. Les tranches suivantes n'ajoutent que du C++ : écrans, browser, menu LFO, Kits.
