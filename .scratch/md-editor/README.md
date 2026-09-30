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
| `screenshots/` | Captures de l'état actuel, produites par les tests (sans ROM : les valeurs sont à 0). |

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

Rien n'a été vérifié contre un vrai firmware : il n'y a pas de ROM dans l'environnement des sessions.

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

- Captures d'écran : `MD_EDITOR_TEST_PNG=/chemin/prefixe build/.../mdEditorSectionTest` écrit `-son`, `-steps`, `-picker`, `-mix`, `-master`, `-panel`, `-stacked`, `-curves`.
- ASan comme la CI : configurer avec `-fsanitize=address,undefined` et lancer `ctest -R "mdAutomationMidiTest|mdAutomationArchitectureTest|mdAutomationParameterTest"`.
- Avec une ROM, les tests firmware (`*FirmwareTest`, `mdAutomationRobustnessTest` sans option) tournent ; sans ROM ils sont ignorés.

### Échecs connus, sans lien avec l'éditeur

- `test_mdJucePlugin_VST` et `test_mmJucePlugin_VST` (format VST2) plantent dans le pluginTester. Valgrind place le dépassement de tas dans le code d'hébergement VST2 de JUCE (`SpeakerMappings::VstSpeakerConfigurationHolder`), côté programme de test. Les variantes VST3 passent, et la CI ne teste que le VST3.
- Quatre tests firmware ne compilent pas avec la configuration locale des sessions (`pmr` SysexBuffer contre `std::vector`) : `sdsFirmwareTest`, `mmSysexWorkflowTest`, `mmSysexExportFirmwareTest`, `userSysexFirmwareTest`. Les tests qui dépendent de leurs binaires apparaissent « Not Run ».
- Les tests de l'éditeur sous UBSan signalent des alertes dans la destruction des documents RmlUi et dans `WidgetScroll`. Elles existent déjà sans l'éditeur (`mdLcdEditorPointerTest` non modifié donne les mêmes). La CI ne passe pas ces tests sous sanitizers.

## À vérifier en premier avec une ROM

1. `$5B` : le firmware accepte-t-il l'assignation, et que deviennent les paramètres de la piste ?
2. Dump de pattern `$67` : les octets lus correspondent-ils au décodeur (trigs, masques, lignes de lock) ? Un vrai backup `.syx` peut être passé à `mdAutomationMidiTest <backup MD> <backup MM>`.
3. Écriture du pattern : le firmware garde-t-il un pattern envoyé dans son slot ? La ligne d'info de PAS affiche « refusé par le firmware » si la relecture diffère. Écrire pendant la lecture fait-il un raté audible ?
4. Une requête de kit pour le slot courant rend le kit stocké, pas le kit en cours (machines changées en face avant invisibles jusqu'au prochain chargement de kit). Vérifier que le dump de pattern, lui, reflète bien l'état en cours.

## Travail restant

Le périmètre demandé est « tout câbler ». Ordre proposé, du plus court au plus long.

### 1. Écran KIT / PATTERN de la barre du haut

- Élément `mdEdScreen`, aujourd'hui un texte fixe inactif.
- Le contrôleur connaît déjà le kit courant (`m_currentKit`, réponse de statut `$72` paramètre Kit) et le pattern (`m_pattern->slot`, ou statut Pattern).
- Nom du kit : MD, 16 octets ASCII à 0x0a du dump de kit (`$52`), juste avant les paramètres (0x1a) ; MM, 11 premiers octets du contenu décodé (`decodeMonomachinePayload`), avant les niveaux (0x0b).
- Affichage proposé : `KIT 03 · NOM · PATTERN B03`. Numérotation : kits 1–64 (MD) ou 1–128 (MM), patterns A01–H16.

### 2. Effets master (MD)

- Vue MASTER : aujourd'hui 4 blocs inactifs de 3 ou 4 contrôles. Chaque effet a 8 paramètres (noms d'après MCL, `MDParams.h`) :
  - RHYTHM ECHO : TIME MOD MFRQ FB FLTF FLTW MONO LEV
  - GATE BOX REVERB : DVOL PRED DEC DAMP HP LP GATE LEV
  - EQ : LF LG HF HG PF PG PQ GAIN
  - DYNAMIX : ATCK REL TRHD RTIO KNEE HP OUTG MIX
- Lecture, dans le dump de kit MD : 32 octets bruts juste après la section LFO. Offsets calculés à partir de MCL (`MDKit::fromSysex`) :
  - nom : 0x0a, 16 octets bruts ;
  - paramètres : 0x1a, 16 × 24 octets bruts (déjà lus) ;
  - niveaux : 0x19a, 16 octets bruts (déjà lus) ;
  - machines : 0x1aa, 64 octets en groupes 7 bits, soit 74 octets (déjà lus) ;
  - LFO : 0x1f4, 16 × 36 octets en 7 bits, soit 659 octets ;
  - effets master : 0x487, 32 octets bruts, dans l'ordre **reverb, echo, EQ, dynamix** (8 chacun) ;
  - groupes trig/mute : 0x4a7, 32 octets en 7 bits ;
  - puis somme de contrôle, longueur et F7, pour une longueur totale de 0x4d1.
  - À confirmer sur un vrai dump : l'ordre vient des noms de membres de MCL.
- Écriture : un message par paramètre, `F0 00 20 3C 02 00 <id> <param 0..7> <valeur> F7`, avec `id` = `$5D` echo, `$5E` gate box reverb, `$5F` EQ, `$60` dynamix (`MDParams.h`, `MD::setFXParam` en 3 octets de données). MCL envoie parfois une forme à 4 octets (0x7F en plus) et une forme groupée `0x70 <id> 8 valeurs` : ce sont des extensions de son firmware, à ne pas utiliser.
- Même politique que les machines : un dump appliqué remplace, un dump d'inspection du même slot ne remplit que l'inconnu.
- UI proposée : 4 blocs de 6 colonnes, 8 knobs en 2 rangées (hauteur 208 dp chacun, la vue tient en 468 dp). Knobs non liés à un paramètre hôte (ce ne sont pas des paramètres du plug-in), valeurs posées par le C++ et événements `Change` renvoyés au contrôleur.

### 3. Sortie par piste (MD) et bus (MM)

- **MD** : routage dans le Global, 16 octets bruts à 0x0a..0x19 du dump Global (`$50`), avant la table des notes (0x1a, 128 octets en 7 bits), ce qui place le canal de base à 0xad comme dans `parseGlobalDump`. Valeurs : 0 à 5 = sorties A à F, 6 = MAIN. Écriture : `F0 00 20 3C 02 00 5C <piste> <sortie> F7` (`MD_SET_TRACK_ROUTING_ID`, `MD::setTrackRouting`). Le contrôleur lit déjà les dumps Global (canal de base, sync) : y ajouter le routage.
  - UI : colonne SORTIE de MIX (`selector()` dans `gen_editor.py`, aujourd'hui des `div.mdEdSeg.mdEdOff`), à rendre cliquable avec l'état courant surligné.
- **MM** : format inconnu. MCL ne connaît qu'un octet `globalRouting` dans le Global et déclare `MNM_SET_TRACK_ROUTING_ID = 0x5C` avec des drapeaux `AB=1, CD=2, EF=4`, sans jamais s'en servir. Il faut le manuel MM (annexe SysEx) ou un dump Global réel à comparer avant/après un changement de routage. En attendant, garder la colonne BUS inactive.

### 4. Vu-mètres, pistes routées, « ACTIF DANS LE DAW »

- Niveaux crête/RMS des trois paires de sorties du plug-in, mesurés dans le processeur audio. Il faut les publier par des atomiques, sans verrou sur le fil audio (règle du dépôt), puis les lire dans le minuteur de présentation de l'éditeur (`Editor::timerCallback`, 16 ms).
- Pistes routées vers chaque paire : dérivées du routage du point 3.
- « ACTIF DANS LE DAW » : état des bus de sortie côté hôte (`AudioProcessor::getBus(false, i)->isEnabled()`). Voir `mdAudioIoLayoutTest.cpp` pour la disposition des bus.

### 5. OPTIONS et SYSTÈME

- OPTIONS (barre du haut, `div.mdEdSeg.mdEdOff`) : ouvrir le menu existant du plug-in (échelle 50–125 %, skin, réglages ; voir `jucePluginEditorLib/pluginEditorState.cpp`, menu d'échelle vers la ligne 340, et les réglages MD `mdSettingsPanelFeel`, `mdSettingsAudioInput`).
- SYSTÈME : page qui montre et édite ce que le plug-in expose déjà : canal MIDI de base, horloge et transport entrants (`GlobalSync`, `withGlobalSync`), réglages du panneau et de l'entrée audio.

### 6. JOUER et BIBLIO (long, à découper)

- JOUER : lanes de pattern (MD) et piano roll (MM) d'après les planches 6/6b, puis arrangement. Il faudra le pattern complet (64 pas, swing, accent, slide ; `MdPatternEditor` garde déjà tous les octets) et le format de pattern du MM, absent pour l'instant. Pour le MM, voir `MNMPattern` dans MCL.
- BIBLIO : liste des kits (requêtes `$53` par slot, noms), banque de samples UW (MD, transferts SDS : voir `sdsTransferTest` et `mdLib` SDS) et formes d'onde DigiPRO (MM, voir `mmSysexWorkflowTest`).

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
