# Éditeur MD/MM : état du travail et reprise en local

Ce dossier rassemble ce qu'il faut pour reprendre l'éditeur des plug-ins Machinedrum (MD) et Monomachine (MM) sans le contexte des sessions Claude : ce qui est fait, où c'est dans le code, comment régénérer et tester, et ce qui reste, avec les formats SysEx déjà trouvés.

Branche : `claude/md-editor-rml`, PR c0remusic/gearmulator-md-mm#4 (brouillon), base `release/md-mm-alpha`.

## Contenu du dossier

| Chemin | Rôle |
| --- | --- |
| `tools/gen_editor.py` | Génère la partie éditeur des deux skins (barre du haut + éditeur). Les skins ne s'éditent pas à la main : on modifie ce script. |
| `tools/splice_editor.py` | Remplace la barre du haut et l'éditeur dans `mdDefault.rml` et `mmSfx60.rml` par la sortie du script. |
| `mockups/md-editor.html` | Maquette haute fidélité (look « Châssis »), ouvrir dans un navigateur. |
| `mockups/md-wireframes.html` | Wireframes des planches 0 à 7 (référence de mise en page et d'interaction). |
| `screenshots/` | Captures de l'état actuel, produites par les tests sans ROM (kit synthétique : valeurs à 64). |

Régénérer les skins :

```sh
cd .scratch/md-editor/tools
python3 gen_editor.py md && python3 gen_editor.py mm && python3 splice_editor.py && rm -f *.rml.txt
```

Le script refuse de générer une vue plus haute que la page de 538 dp (message d'erreur explicite).

## Ce qui est fait (sur la branche)

Commits, du plus ancien au plus récent : éditeur RML MD puis MM, sélecteur de machine, correctif ASan (`std::sort`), grille de pas en lecture, mise en page 606 dp, écriture des trigs et des locks, courbes.

- **Fenêtre** : 1100 × 606 dp, une barre de 36 dp au-dessus de la face avant (1100 × 570, inchangée). La bascule FACE AVANT / ÉDITEUR et les catégories sont gérées par `ViewLayout` (`mdViewLayout.*`). Un seul des deux est dessiné à la fois.
- **Hauteur libre** (MD et MM seulement) : attribut `resizableheight` sur le `<body>`. Code partagé touché : `juceRmlUi/juceRmlComponent.*` (le body prend la hauteur de la fenêtre), `jucePluginEditorLib/pluginEditorWindow.cpp` et `pluginEditorState.*` (la largeur donne l'échelle, la hauteur est gardée dans la config sous la clé `height`). Les autres synthés ne sont pas concernés. À partir de 1176 dp, face avant et éditeur sont empilés.
- **Mise en page** : grille de 12 colonnes (16 dp de marge, colonnes de 78, gouttières de 12). Chaque vue tient dans la page de 538 dp sans défilement.
- **Sélecteur de machine** : `mdMachinePicker.*`, table `mdLib/mdmachines.*` (142 machines MD, 20 MM, ids et noms d'après MCL), `$5B` ASSIGN MACHINE, machines lues dans le dump de kit.
- **Grille de pas (MD)** : `mdStepGrid.*`. Lecture du pattern courant (`$70` statut puis `$68`/`$67`), clic = pas sélectionné, double-clic = trig, contrôles qui écrivent les locks pendant qu'un pas avec trig est sélectionné, double-clic sur un lock = effacement. Écriture du dump complet puis relecture (`Controller::sendPattern`, `getPatternWrite`). Codec du dump : `MdPatternEditor` dans `mdLib/mdsysexautomation.*`.
- **Courbes** : `mdCurveView.*`. Rangée COURBES sous SON quand la page a la place (fenêtre ≥ 652 dp sur MD, ≥ 736 dp sur MM). Formes seulement, pas d'échelles réelles.
- **Assignation de machine** : `Controller::assignMachine` n'envoie que `$5B`. Les valeurs que le firmware change sans le dire (MD : synthèse, effets et routage ; MM : synthèse) deviennent inconnues (`isValueKnown`, `getValueStateRevision`) au lieu d'être écrasées par le cache. `mdUnreadValues.*` les grise (classe `mdEdUnread`, valeur affichée « — »), avec les lignes de résumé et les courbes qui les montrent (attribut `data-unread` posé par `gen_editor.py`). Elles redeviennent connues avec un dump de kit appliqué, une valeur envoyée par le firmware, ou une écriture de l'hôte ou de l'éditeur. Un instantané d'état pris pendant ce temps n'est pas complet : le restaurer n'écrase pas ce que le firmware tient.

Les sessions cloud n'ont pas de ROM ; les points vérifiés depuis sur un vrai firmware sont dans « Vérifié avec une ROM ».

## Construire et tester en local

Linux, GCC 13, build Release avec JUCE (même configuration que les sessions) :

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build mdEditorSectionTest mmEditorSectionTest mdAutomationMidiTest mdAutomationRobustnessTest mdAutomationParameterTest
build/source/elektron/md/mdJucePlugin/mdEditorSectionTest
build/source/elektron/md/mdJucePlugin/mmEditorSectionTest
build/source/elektron/md/mdLibTest/mdAutomationMidiTest
build/source/elektron/md/mdJucePlugin/mdAutomationRobustnessTest --architecture-only
```

- Captures d'écran : `MD_EDITOR_TEST_PNG=/chemin/prefixe build/.../mdEditorSectionTest` écrit `-unread`, `-mix`, `-system`, `-play` (MD), `-library`, `-son`, `-steps`, `-picker`, `-master`, `-panel`, `-stacked`, `-curves`. Avant chaque capture, le test fait ce que les minuteurs font dans le plug-in (valeurs poussées vers l'interface, composants de l'éditeur rafraîchis) : sans cela, textes et knobs gardent leur valeur de départ.
- Windows (VS 2022, `temp/cmake_vs22`) : régénérer les skins avec `PYTHONUTF8=1`, sinon Python lit les skins en cp1252. Les tests firmware du plug-in (`Harness`) cherchent la ROM dans le dossier de données du plug-in ou à côté de leur exécutable, pas dans `GEARMULATOR_*_FIRMWARE_BIN` : copier les `.bin` dans `…/mdJucePlugin/Release/`.
- ASan comme la CI : configurer avec `-fsanitize=address,undefined` et lancer `ctest -R "mdAutomationMidiTest|mdAutomationArchitectureTest|mdAutomationParameterTest"`.
- Avec une ROM, les tests firmware (`*FirmwareTest`, `mdAutomationRobustnessTest` sans option) tournent ; sans ROM ils sont ignorés.

### Échecs connus, sans lien avec l'éditeur

- `test_mdJucePlugin_VST` et `test_mmJucePlugin_VST` (format VST2) plantent dans le pluginTester. Valgrind place le dépassement de tas dans le code d'hébergement VST2 de JUCE (`SpeakerMappings::VstSpeakerConfigurationHolder`), côté programme de test. Les variantes VST3 passent, et la CI ne teste que le VST3.
- Quatre tests firmware ne compilent pas avec la configuration locale des sessions (`pmr` SysexBuffer contre `std::vector`) : `sdsFirmwareTest`, `mmSysexWorkflowTest`, `mmSysexExportFirmwareTest`, `userSysexFirmwareTest`. Les tests qui dépendent de leurs binaires apparaissent « Not Run ».
- Les tests de l'éditeur sous UBSan signalent des alertes dans la destruction des documents RmlUi et dans `WidgetScroll`. Elles existent déjà sans l'éditeur (`mdLcdEditorPointerTest` non modifié donne les mêmes). La CI ne passe pas ces tests sous sanitizers.

## Vérifié avec une ROM (2026-10-01)

`mdEditorFirmwareTest` (mdLibTest) pose ces questions au vrai firmware, SPS-1UW OS 1.63 et SFX-60 OS 1.32b, avec le codec de l'éditeur. Il tourne en ~50 s et s'ignore sans `GEARMULATOR_MD_FIRMWARE_BIN` / `GEARMULATOR_MM_FIRMWARE_BIN`.

1. `$5B` est accepté : MD table normale (TRX-XC → TRX-SD) et table UW avec le drapeau (TRX-XT → ROM-01), MM sans initialisation des pages (SWAVE-SAW → DPRO-DDRW). Le kit sauvé porte la nouvelle machine.
   - MD : l'assignation remet les valeurs par défaut de la machine sur les pages synthèse, effets et routage (16 valeurs sur 25 pour TRX-SD, 13 pour ROM-01), pas le niveau. Le contrôleur renvoyait ensuite les valeurs de son cache ; tranché le 2026-10-01 : il ne le fait plus (voir « Assignation de machine » plus haut).
   - MM (forme sans initialisation) : 2 valeurs sur 57 changent, toutes deux sur la page synthèse (SYN B et SYN F pour SWAVE-SAW → DPRO-DDRW) ; les autres pages sont gardées.
2. Dump `$67` : `MdPatternEditor` réencode octet pour octet le dump réel (5 410 octets, pattern A01 d'usine, 32 pas). Les trigs d'usine se lisent de façon cohérente avec le kit (T1 TRX-B2 sur 1 et 17, T9 TRX-CH très dense). Des trigs posés en face avant (enregistrement en grille, pas 1 et 5) et un lock (TRIG 5 tenu, DATA ENTRY B tourné) se lisent exactement là où le décodeur les attend : piste 1, pas 1 et 5, paramètre 2 au pas 5.
3. Écriture : le firmware garde le pattern envoyé tel quel (relu identique), à l'arrêt comme en lecture. En lecture, l'écriture est suivie de 75 à 81 ms de silence complet (0 ms sur les 3 s d'avant) : raté audible dans l'émulation. Non vérifié sur une vraie machine.
4. Une requête de kit rend le kit stocké : la machine assignée n'y apparaît qu'après sauvegarde. Le dump de pattern rend l'état en cours : trigs et lock posés en face avant, non sauvés, y figurent.

5. Effets master : 32 octets bruts à 0x487 du dump de kit, dans l'ordre **reverb, echo, EQ, dynamix**. `$5D` (echo), `$5E` (reverb), `$5F` (EQ) et `$60` (dynamix) `<paramètre> <valeur>` écrivent là où le décodeur lit (paramètre n de l'effet n changé, kit sauvé, relu : exactement ces 4 octets et la somme de contrôle changent). Valeurs du kit d'usine 1 : echo 16 0 32 27 0 44 0 71, reverb 0 0 68 50 1 82 71 109, EQ 64 ×7 puis 127, dynamix 127 ×6 puis 0 0.
6. Routage des pistes : 16 octets bruts à 0x0a du dump Global (0 à 5 = A à F, 6 = MAIN ; tout en MAIN en usine). `$5C <piste> <sortie>` change l'octet de la piste, et une requête Global le montre aussitôt, sans sauvegarde : le Global est vivant.

Non vérifié : l'ordre des pistes par la face avant (DOWN en enregistrement en grille ne change pas de piste) ; il repose sur la cohérence du pattern d'usine avec son kit.

## Travail restant

Le périmètre demandé est « tout câbler ». Ordre proposé, du plus court au plus long.

### 1. Écran KIT / PATTERN de la barre du haut (fait)

- `mdKitPatternScreen.*` : première ligne `KIT 03 · PATTERN B04`, seconde ligne le nom du kit (deux lignes : l'écran fait 2 colonnes, un nom de 16 caractères ne tient pas sur une ligne à côté des numéros). Couleur de la maquette « Châssis » (`#ffb347`).
- Nom lu par `parseKitDump` (`KitDump::name`) : MD, 16 octets à 0x0a du dump ; MM, 11 premiers octets décodés. Confirmé sur les kits d'usine : « TRX UW » (MD), « SUPERWAVES » (MM).
- Le contrôleur garde kit, nom et pattern (`getCurrentKit`, `getKitName`, `getCurrentPattern`, `getSelectionRevision`). Le statut Pattern est désormais sondé toutes les 5 s sur les deux modèles (le MM répond aussi).
- Limite : une requête de kit rend le kit stocké, donc un nom changé en face avant et non sauvé ne s'affiche pas.

### 2. Effets master (MD) (fait)

- Vue MASTER : 4 blocs de 6 colonnes, 8 knobs chacun en 2 rangées (208 dp, la vue fait 480 dp). Noms d'après MCL (`MDParams.h`) :
  - RHYTHM ECHO : TIME MOD MFRQ FB FLTF FLTW MONO LEV
  - GATE BOX REVERB : DVOL PRED DEC DAMP HP LP GATE LEV
  - EQ : LF LG HF HG PF PG PQ GAIN
  - DYNAMIX : ATCK REL TRHD RTIO KNEE HP OUTG MIX
- Lecture : `KitDump::masterEffects`, 32 octets bruts à 0x487 du dump de kit MD (longueur 0x4d1), dans l'ordre reverb, echo, EQ, dynamix (confirmé, voir « Vérifié avec une ROM » point 5). Pour mémoire, les sections du dump : nom 0x0a (16 octets bruts), paramètres 0x1a (16 × 24), niveaux 0x19a (16), machines 0x1aa (64 octets en 7 bits, 74), LFO 0x1f4 (16 × 36 en 7 bits, 659), effets master 0x487 (32 bruts), groupes trig/mute 0x4a7 (32 en 7 bits), puis somme de contrôle, longueur et F7.
- Écriture : `masterEffectChange`, `F0 00 20 3C 02 00 <id> <param 0..7> <valeur> F7` avec `id` = `$5D` echo, `$5E` gate box reverb, `$5F` EQ, `$60` dynamix (confirmé). MCL envoie parfois une forme à 4 octets (0x7F en plus) et une forme groupée `0x70 <id> 8 valeurs` : extensions de son firmware, non utilisées.
- Contrôleur : `getMasterEffect`, `setMasterEffect`, `getMasterEffectRevision`. Même politique que les machines : un dump appliqué remplace, un dump d'inspection du même slot ne remplit que l'inconnu.
- `mdMasterEffectsView.*` : knobs non liés à un paramètre hôte (ce ne sont pas des paramètres du plug-in), valeurs posées par le C++, « — » et grisés tant qu'inconnus ; un mouvement envoie la valeur.
- Limite : un effet changé en face avant n'apparaît qu'au prochain chargement de kit (la requête rend le kit stocké).

### 3. Sortie par piste (MD) (fait) et bus (MM)

- **MD** : `GlobalDump::trackOutputs`, 16 octets bruts à 0x0a du dump Global (0 à 5 = A à F, 6 = MAIN), et `trackRouting` (`$5C <piste> <sortie>`), confirmés (point 6). Le Global est vivant et relu à chaque sondage de 5 s : un routage changé en face avant apparaît en 5 s au plus.
  - Contrôleur : `getTrackOutput`, `setTrackOutput`, `getRoutingRevision`. Un dump Global demandé avant une écriture de routage ne l'annule pas (il ne la contient pas encore) ; le suivant fait foi.
  - UI : colonne SORTIE de MIX (`mdTrackRoutingView.*`), segments cliquables `mdEdOut<piste>_<sortie>`, la sortie choisie en orange, la rangée grisée tant que le routage est inconnu.
- **MM** : format inconnu. MCL ne connaît qu'un octet `globalRouting` dans le Global et déclare `MNM_SET_TRACK_ROUTING_ID = 0x5C` avec des drapeaux `AB=1, CD=2, EF=4`, sans jamais s'en servir. Il faut le manuel MM (annexe SysEx) ou un dump Global réel à comparer avant/après un changement de routage. En attendant, la colonne BUS reste inactive.

### 4. Vu-mètres, pistes routées, « ACTIF DANS LE DAW » (fait)

- Mesure : `OutputMeters` (`mdOutputMeters.h`), dans `AudioPluginAudioProcessor::processBlock` après le traitement, pour chaque bus de sortie activé par l'hôte : crête et RMS du bloc, publiés par atomiques (maximum depuis la dernière lecture, quelques essais au plus, sans verrou ni allocation sur le fil audio). `pluginLib::Processor::processBlock` est passé de privé à protégé pour pouvoir l'envelopper.
- Affichage : `mdOutputMetersView.*`, lu dans le minuteur de présentation (16 ms). Barre = RMS, trait = crête tenue 1 s ; les deux tombent de 20 dB/s ; échelle −60 à 0 dBFS ; orange à partir de −1 dBFS ; crête du bus en dB (texte rafraîchi 5 fois par seconde au plus). Les deux vu-mètres d'un bus sont un canvas dessiné par JUCE : pas de relayout de la page à 60 Hz, et le même rendu avec OpenGL ou le rendu logiciel (qui ignore `transform`). Rien n'est redessiné tant que MIX est caché.
- Pistes routées (MD) : « MAIN : 13 pistes · A : 9 », « C : 5 », plages « 2–4 » ; « routage : en attente du Global » tant qu'il est inconnu. MM : « routage des pistes : inconnu sur le MM ».
- « ACTIF DANS LE DAW » : `getBus(false, i)->isEnabled()`, en orange si actif. Des pistes routées vers un bus coupé dans le DAW donnent « pistes muettes : activer Out C/D dans le DAW » (planche 7).
- Non fait : niveau de l'entrée audio A/B (planche 10).

### 5. OPTIONS et SYSTÈME (fait)

- OPTIONS (barre du haut) : bouton qui ouvre le menu du plug-in (`Editor::openMenu`, comme un clic droit) : échelle, verrous de régions, enregistrement RAM, diagnostics, fichier SysEx, réglages.
- SYSTÈME (`mdSystemPage.*`, planche 10) : une ligne par sujet, libellé 3 colonnes, état 7, action 2 :
  - RÉGLAGES GLOBAUX : Global et canaux MIDI (`getCurrentGlobal`, canal de base ; MD 4 canaux, MM 6) ; réglage sur la face avant ;
  - SYNCHRO DAW : « suivre le tempo de l'hôte » (`FollowHostTempoConfigKey`) et l'état de `HostSync` ;
  - TRANSPORT PARALLÈLE (`ParallelTransportConfigKey`) et son état ;
  - ENREGISTREMENT RAM (MD) : queues complètes ou finalisation d'origine, grisé si la machine ne le permet pas ;
  - TRANSFERT SYSEX : fichier, reprise ou annulation selon l'état du transfert ;
  - STOCKAGE MACHINE : chargement d'une image (avec la confirmation existante) ;
  - PLUG-IN : fenêtre de réglages.
- L'état est relu deux fois par seconde au plus, seulement pendant que la page est affichée (certains états prennent le verrou du device).
- Les états sont en français ; la fenêtre de réglages existante reste en anglais.

### 6. JOUER et BIBLIO (première partie faite)

- JOUER, MD (`mdPatternView.*`, planches 1 et 2) : le pattern courant, 16 pistes sur les 32 premiers pas (trig en blanc, pas avec locks encadré en bleu, au-delà de la longueur grisé) ; un clic sur un nom de piste en fait la piste éditée, un double-clic sur un pas pose ou retire un trig (écrit au firmware comme PAS). En dessous, la lane de la piste éditée pour un des 24 paramètres (boutons avec le nombre de locks) : barre grise = valeur du kit, orange avec un point = lock, seulement sur les pas qui jouent ; OUVRIR DANS SON montre la piste dans SON. Lane et vu-mètres sont des canvas dessinés par JUCE : le rendu logiciel de RmlUi (`RendererJuce`) ignore `transform`.
- JOUER, MM : pas encore. Le format du pattern du MM n'est pas décodé (MCL : `MNMPattern`) ; le piano roll en dépend.
- Reste à faire dans JOUER : pas 33 à 64, accent, slide, swing (octets gardés par `MdPatternEditor`, pas décodés ni vérifiés sur un vrai dump), tête de lecture, arrangement.
- BIBLIO, KITS (`mdLibraryView.*`, planche 8) : LIRE LES KITS demande chaque kit stocké au firmware, un par un (`Controller::readKitLibrary`, `$53` ; un kit sans réponse en 2 s est sauté), puis montre numéro et nom, le kit chargé en ambre ; un clic montre les machines d'un kit sans le charger. Lecture seule : le chargement d'un kit (geste face avant ou SysEx) et les tags viendront plus tard. Durée : environ 0,4 s par kit MD sur la ligne MIDI.
- Reste à faire dans BIBLIO : banque de samples UW (MD, transferts SDS : voir `sdsTransferTest` et `mdLib` SDS) et formes d'onde DigiPRO (MM, voir `mmSysexWorkflowTest`).

### Petits restes de l'éditeur

- Destination du LFO MD : les octets 0 et 1 de chaque bloc LFO de 36 octets du kit (piste et paramètre destination) permettent d'afficher « destination : piste N, PARAM » au lieu de « à venir ». Écriture : `$62` (`MD_SET_LFO_PARAM_ID`), à confirmer dans MCL (`MD::setLFOParam`).
- Courbes non dessinées : forme du LFO (MD : mélange de deux formes ; MM : noms des formes non documentés), enveloppes des machines MD (le sens de PARAM 1–8 dépend de la machine), REL de l'ampli MM.
- Libellés des paramètres de machine : génériques (PARAM 1–8, SYN A–H). Une table par machine (d'après MCL `MDParams` / `MNMParams`) permettrait d'afficher les vrais noms.
- Patterns de 64 pas : seuls les 32 premiers pas sont affichés et éditables.
- Machine changée en face avant : invisible jusqu'au prochain chargement de kit.

## PR et branches

- c0remusic/gearmulator-md-mm#4 : cette branche. CI verte sur les commits précédents ; le contrôle d'intégration passe depuis le pin DSP `e282cff`.
- c0remusic/gearmulator-md-mm#3 (`claude/param-value-texts`, textes de pan L64–C–R63) : `release/md-mm-alpha` y a été fusionnée pour récupérer le pin DSP. Le commentaire de la PR qui expliquait l'échec du contrôle d'intégration n'est plus à jour une fois ce contrôle vert.
- c0remusic/dsp56300-md-mm#2 : fusionnée. c0remusic/dsp56300-md-mm#1 (observateur DE du DMA) : ouverte, CI verte, sans conflit.

## Références

- MCL / MegaCommand (github.com/jmamma/MCL, licence BSD) : `src/mcl/Drivers/MD/MDMessages.cpp` (formats de kit et de Global), `MDPattern.cpp` (pattern), `MDParams.h` (ids SysEx, noms des effets), `MNM/MNMMessages.cpp` (MM). Toutes les connaissances de format de ce dossier en viennent, sauf ce que les tests firmware du dépôt confirment déjà.
- Le manuel OS 1.63 du MD n'a pas pu être téléchargé depuis l'environnement des sessions (403) : à consulter pour confirmer les points marqués « à confirmer ».
