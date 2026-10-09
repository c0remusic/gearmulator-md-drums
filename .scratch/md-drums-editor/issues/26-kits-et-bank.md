# Kits et Bank

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 10

## Question

Construire la tranche Kits que les tickets 04 et 10 ont fixée, sur l'overlay statique du skin v22 (ticket 18).

- **Bank** : un fichier de 64 dumps `$52` de 1233 octets dans le dossier data du plug-in, amorcé des 16 Kits d'usine
  (Slots 1-16) quand il manque ; relu à l'ouverture de l'overlay et avant chaque écriture ; une écriture ne touche que
  son Slot.
- **Overlay** : Slots 01-64 (nom ou « Empty »), Slot choisi et Slot joué marqués, panneau du Slot choisi (nom, 16
  machines), Load, Save here, Rename (16 caractères, en place), Delete, second clic pour ce qui détruit, note de la
  dernière action ; navigation clavier (clic, double-clic et Enter, flèches) et Esc ; Import et Export `.syx`.
- **Bande de tête** : Slot et nom du Kit joué ; Save allumé quand le Kit joué diffère de son Slot ; Ctrl+S.
- **Chargement** : dump `$52` entier vers le Device au bloc suivant, paramètres en `PresetChange` puis
  `notifyHostOfProgramChange` ; sans allocation sur le thread audio, ou une par chargement.
- **État** : Slot joué dans un chunk du Processor.
- **CC SYN** renvoyés après un changement de machine : l'hôte prévenu sans geste.

Fini quand : les gestes du HANDOFF (« Kits », « Interaction ») fonctionnent dans `mdDrumsSkinTest` ; Kit chargé et
paramètres cohérents dans `mdDrumsProcessorTest` ; Bank lue et écrite à deux instances sans perte ; entrée→pixel
mesuré ; garde-fou vert ; poussé sur `main`.

## Answer

- **Bank** (`mdDrums/mdDrumsBank.*`) : `Bank.syx` dans le dossier data du plug-in (un dossier temporaire à lui pour un
  Processor de test), 64 dumps `$52` de 1233 octets, le Slot n à n fois 1233 ; un Slot vide est un dump dont le nom
  commence par $7f. Un fichier absent est écrit avec les 16 Kits d'usine (TRX UW à SEACLONES, Slot 1 = le Kit 1 du boot)
  et 48 Slots vides. Lecture à l'ouverture de l'overlay et avant chaque écriture ; une écriture réécrit les seuls octets
  de son Slot. `parseSyx` lit les Kits d'un `.syx` (vides écartés).
- **Kit joué** (`Controller`) : le dernier chargé (nom, Links, Chokes, état courant des LFO) avec les valeurs actuelles
  des paramètres (`playedKit`), et son Slot (`playedSlot`), gardé dans un chunk « KSLT » de l'état du plug-in.
  `loadKit` : un dump `$52` au Device (sans allocation sur le thread audio : `parseMdKit` lit dans des tableaux fixes),
  les changements en attente des paramètres du Kit abandonnés, les paramètres posés en `PresetChange` puis un
  `notifyHostOfProgramChange`. `renameKit` : le nom au Device par un message MD Drums (`KitName`, 16 octets), la
  Machinedrum ne nommant un Kit que dans son dump. Les CC SYN renvoyés après un changement de machine préviennent
  l'hôte (`sendValueChangedMessageToListeners`, sans geste).
- **Vue** (`mdDrumsKitsView.*`) : bande de tête (Slot, nom, Save allumé quand le Kit joué diffère de son Slot, relu
  10 fois par seconde), overlay (Slots, choisi éclairé et joué marqué comme la maquette, panneau avec les 16
  machines, Load, Save here, Rename dans un champ en place, Delete, second clic « Load anyway », « Replace it »,
  « Delete it » avec Cancel, note), flèches et Enter, Esc (renommage, question, puis overlay), Ctrl+S, Import et Export
  `.syx` par le sélecteur de fichiers de JUCE.
- **Mesures** : `mdDrumsBankTest` (nouveau, au garde-fou) : nouvelle Bank de 78 912 octets, 16 Kits d'usine, chaque
  Slot un dump que l'OS 1.63 prend pour son slot ; deux Banks sur un fichier gardent les Slots l'une de l'autre ; Slot
  vidé relu vide ; 17 Kits lus du fichier comme `.syx`. `mdDrumsProcessorTest` : Slot 04 (P-I UW) chargé en un dump,
  Device, paramètres et Kit joué le suivent, mixer gardé ; un paramètre changé le fait différer ; Slot et Kit revenus de
  l'état ; nom transmis ; SYN1 notifié à l'hôte après un changement de machine. `mdDrumsSkinTest` : 881 boîtes toujours
  identiques à la maquette ; un Slot cliqué éclairé avec son panneau 7,9-8,0 ms entrée→pixel ; tous les gestes ci-dessus.
- Reste à constater dans Live (installation) : ce que l'undo fait d'un chargement, et Ctrl+S face aux raccourcis de
  l'hôte.
