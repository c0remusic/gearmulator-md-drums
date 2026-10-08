# Port du processor MD Drums sur la pile TUS

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: 06, 13, 14, 15

## Question

Porter `mdDrumsPlugin` sur `pluginLib::Processor` + Device + Controller + JSON, selon le contrat du ticket 06 et
l'ADR 0002 :

- un Device qui parle le protocole de la Machinedrum, étendu de la SysEx privée `F0 7D …` (Solo, Out, tempo), rend
  de façon synchrone, découpe ses blocs aux offsets des notes, rapporte 0 bloc et ses 0,48 ms internes ;
- le JSON de 592 paramètres, pages et ordre du ticket 06 ; un Controller à automation temps réel sans verrou ;
- l'état en dump `$52` + SysEx privée, relu par `onStateLoaded` ;
- Machine qui remet SYN1-8 aux défauts et renvoie 8 CC ; Links et Chokes branchés (dump, `$65`/`$66`) ;
  `setLfo` dans `mdDrums::Engine` ;
- bus « Out 01 » à « Out 16 », `t<n>_out` qui décide ; resampler de la pile ; identité c0remusic/`C0rm`/`Mddr` ;
- un rendu sans allocation (le `std::vector` de `MachineRunner::call`, `mdEngine/engine/MachineRunner.cpp:146`) ;
- un skin minimal à la place de l'éditeur générique de JUCE.

Les 32 paramètres Master existent et sont sauvés dans le Kit, muets jusqu'au ticket 09.

Fini quand : `mdDrumsProcessorTest` porté et vert, un test pilote le Device par octets MIDI seuls, garde-fou vert,
latence note→son et gigue mesurées avant et après à 44,1 et 48 kHz, poussé sur `main`. Les IDs de paramètres sont
figés à partir de ce push.

## Answer

**Paramètres : 576, pas 592.** Par Track, Machine + 24 paramètres + 5 LFO + Level + Mute + Solo + Out font 34, pas
35 : le contrat du ticket 06 comptait un de trop (le ticket 10 donne bien 31 par Track dans le Kit, 31 + Mute, Solo,
Out = 34). 16 × 34 + 32 Master = 576. IDs `page_part_index` (pages du ticket 06), figés à partir de ce push ; ordre
hôte : Track 1 (Machine `5_0_0` … Out `7_0_1`), …, Track 16, puis Master `8_0_0` à `8_0_31`. Parts « Track n »,
Master « Master ». Liste : `mdDrumsPlugin/Design/parameter-spec.md`.

**Bibliothèque `mdDrums`** (sans JUCE) :

- `mdDrums::Device` (`mdDrumsDevice.*`), `synthLib::Device` : notes 36-51 de toute source et tout canal ; du seul
  éditeur (source Editor) les CC de la MD (canaux 1-4), `$5B`, `$5D-$60`, `$62`, `$65`/`$66`, `$52`, et les messages
  propres `F0 7D 4D 44 44` (Solo, Out, tempo, masques) ; CC et SysEx de l'hôte ignorés. `process` découpe le bloc à
  l'offset de chaque événement ; 0 bloc ; latence interne rapportée 21 échantillons ; 18 sorties. Démarre sur le Kit 1
  d'usine (TRX UW) tiré de l'image flash. `$5B` remet SYN1-8 aux défauts de la machine et renvoie, en fin de bloc, les
  SYN1-8 laissés par le bloc en 8 CC. Mute et Solo donnent un mute effectif ; Out sort la Track de Main. Une exception
  du moteur rend `isValid()` faux. Les 32 valeurs Master vont dans le Kit, muettes jusqu'au ticket 09.
- `mdDrumsMessages.*` : les messages propres (`Raw`, sans allocation), le format d'état (dump `$52` slot 0 + message
  masques), le Kit d'usine, et `parameterMessage` / `parameterValue` : le paramètre hôte en message de la MD, et sa
  valeur lue dans un état.
- `mdDrums::Engine` gagne `setLfo` et `loadLfo` (bloc LFO de 36 octets copié dans le LFO courant comme LOAD KIT,
  `HostModel::loadLfo`) ; `MachineRunner::call` empile ses arguments sans `std::vector`.

**Plug-in `mdDrumsPlugin`** : `jucePluginEditorLib::Processor` ; `juce_add_plugin` propre (c0remusic, `C0rm`, `Mddr`)
avec les defines de `createJucePlugin` ; bus « Main » puis « Out 01 » à « Out 16 » (offsets logiques 0, 2-17) ; image
flash cherchée dans `Documents/c0remusic/MD Drums/roms/` puis comme `findFlashImage`, absente : `DeviceException`
(`FirmwareMissing`) et dialogue de la pile. `processBpm` envoie le tempo en message propre quand il change. Le
Controller (`mdDrumsController.*`) : chaque changement (hôte, éditeur, MIDI Learn) va dans un slot atomique et un
bit « sale » ; `processRealtimeParameterChanges` les envoie au début du bloc audio suivant par
`tryAddRealtimeMidiEvent`, sans verrou ni allocation. Les CC du Device (SYN1-8 d'une nouvelle machine) reviennent en
valeurs de paramètres sans notifier l'hôte (règle du ticket 10 à fixer). L'état : le Processor intercepte le chunk
`MIDI` avant la pile et le donne au Controller, qui relit Kit et masques dans `onStateLoaded` sans toucher le Device.
SYN1-8 affichent le nom de la machine (« PTCH 64 »). Skin minimal RmlUi `skins/mdDrumsMinimal/` (généré par
`.scratch/md-drums-editor/tools/gen_minimal.py` avec le JSON) : 16 rangées de 34 valeurs déplaçables, menu machine,
boutons M/S/O, rangée Master ; 1296×824, les 576 paramètres liés.

**Tests**, au garde-fou :

- `mdDrumsDeviceTest` (Device seul, octets MIDI seuls, par `synthLib::Plugin`) : Kit 1 au démarrage ; notes 36-51
  seules ; CC de l'hôte ignorés, de l'éditeur appliqués ; `$5B` et ses 8 CC ; `$5D`, `$62`, `$65`, `$66` dans le Kit ;
  Mute, Solo, Out ; état aller-retour ; dump `$52` joué ; 471 valeurs de paramètres hôte envoyées par
  `parameterMessage` et relues par `parameterValue` ; 32 blocs avec note, CC et `$62` sans allocation.
- `mdDrumsProcessorTest` porté : 576 paramètres, IDs et ordre ; départ sur le Device ; note 36 sonne, note 60 non ;
  automation de la Machine jusqu'au Device, SYN1 « PTCH … » ; Out sort la Track de Main avec ou sans bus ; état
  rechargé dans un second processor, paramètres compris ; éditeur ouvert (rendu logiciel), 576 paramètres liés.

**Allocation** : aucune en régime établi. Le premier Hit d'une machine en fait 417 (le JIT du DSP56300 compile son
code au premier passage), le premier CC 2, puis zéro.

**Latence note→son** (TRX-BD sur la Track 1, 48 notes à des échantillons pseudo-aléatoires de blocs hôte de 512,
jusqu'au premier échantillon non nul ; avant : processor APVTS + Lagrange, mesuré par un exécutable temporaire) :

| Hôte | Avant | Après | Latence rapportée avant / après |
|---|---|---|---|
| 44,1 kHz | 36-67, moyenne 51,0 (1,16 ms), gigue 31 (0,70 ms) | identique | 0 / 21 |
| 48 kHz | 44-76, moyenne 60,4 (1,26 ms), gigue 32 (0,67 ms) | 53-87, moyenne 69,0 (1,44 ms), gigue 34 (0,71 ms) | 0 / 55 |

À 48 kHz, +8,6 échantillons en moyenne : le resampler Legacy de la pile (délai 32 échantillons, rapporté) contre
l'interpolateur Lagrange d'avant (non rapporté). La latence est désormais déclarée à l'hôte.
