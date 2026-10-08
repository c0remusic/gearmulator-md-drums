# Contrat du Device MD Drums

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: 01

## Question

Que reçoit le `synthLib::Device` de MD Drums et comment pilote-t-il `mdDrums::Engine` ?

- Entrée : le protocole MIDI de la Machinedrum (CC des paramètres, `$5B` machine, `$5D-$60` master, dump de kit
  `$52`), ou des appels directs depuis les paramètres ; ce que chaque voie coûte en latence et en code.
- JSON de paramètres : pages, 16 parts pour les Tracks, page master (32), paramètres ajoutés (Out, solo, LFO ×5) ;
  ordre figé dès le premier push du port.
- Où vit le codec de kit sorti de `mdLib` (nom de la bibliothèque, ce qu'elle emporte), et ce que `mdLib` en garde.
- API à ajouter à `mdDrums::Engine` (LFO via `HostModel::setLfo`, solo, master) et ce qui reste sur le thread audio.
- Budget de latence : rester à ~0,48 ms note→son, bus et resampling compris.
- Issus du ticket 01 : 18 sorties contre 12 dans `synthLib` (`TAudioOutputs`, `audioTypes.h:9`) : élargir le type
  dans `synthLib` ou écrire les Outs depuis le `processBlock` du produit ; `getDefaultLatencyBlocks()` à 0 ;
  resampler de la pile (latence rapportée) contre Lagrange d'aujourd'hui ; état du Device (Kit, mutes, solos, Outs)
  dont le Controller reconstruit les paramètres ; `AutomationParameter` sans verrou pour l'automation de Live ;
  identité C0rm/Mddr hors de `createJucePlugin`.

Sortie : le contrat écrit (ADR si un choix est difficile à défaire), qui fixe la forme du port (ticket 07).

## Answer

Le Device de MD Drums est une Machinedrum vue par MIDI (ADR 0002) : il reçoit le protocole de la machine, étendu
d'une SysEx privée, rend de façon synchrone sur le thread audio et porte toute la vérité sonore ; les paramètres hôte
la reflètent.

**Entrée.** De l'hôte : les notes seules, 36-51 sur tout canal pour les Tracks 1-16, vélocité sur le level, note-off
ignoré ; CC et SysEx venus de l'hôte ignorés (pas de `controllerMap` : la copie MIDI vers le Controller passe par un
try-lock qui perd des messages) ; Program Change laissé au ticket 10. Du Controller (source Editor) : CC de la MD sur
les canaux 1-4 (24 paramètres, level CC 8-11, mute CC 12-15), `$5B` Machine, `$5D-$60` Master, `$62` LFO,
`$65`/`$66` Link et Choke, `$52` Kit ; SysEx privée `F0 7D …` pour Solo, Out et tempo (envoyé depuis `processBpm` à
chaque changement ; `F8`/`FA`/`FC` ignorés). Tout sauf le dump tient dans les 64 octets SysEx en ligne de
`SMidiEvent` : aucune allocation. L'automation hôte passe par le chemin sans verrou d'`AutomationParameter` et le
drain temps réel du Controller, sur le modèle de `mdController`.

**Sorties et rendu.** `synthLib` élargi de 12 à 18 canaux (Main + Out 01-16) sous une constante nommée unique ; bus
« Out 01 » à « Out 16 » ; `t<n>_out` décide, quel que soit l'état du bus. Rendu synchrone dans `processAudio`, découpé
aux offsets des notes ; `getDefaultLatencyBlocks()` = 0 et aucun bouton de blocs dans le skin ; les 0,48 ms du moteur
déclarées par `getInternalLatencyMidiToOutput()`. Rien n'alloue sur le thread audio (le vecteur de
`MachineRunner::call` part au port). Hors 44,1 kHz : resampler de la pile, Legacy par défaut (+0,67 ms à 48 et 96 kHz,
calculé, rapporté à l'hôte), mode réglable ; le budget de 0,48 ms vaut à 44,1 kHz. Une exception du moteur dans
`processAudio` rend `isValid()` faux. Le ticket 09 peut rouvrir le choix du thread si le master l'exige.

**Mute, Solo, Link, Choke** (définis dans `CONTEXT.md`). Le Device calcule un mute effectif (Mute, ou Solo ailleurs
sans le sien) ; une Track en mute effectif perd ses Hits, comme sur l'OS, donc ni Link ni Choke. Links et Chokes sont
joués tels que l'OS les joue (`research/06-groupes.md`), prouvés contre le firmware ; ce sont des valeurs du Kit, pas
des paramètres hôte.

**Machine.** `$5B` remet SYN1-8 aux défauts de la Machine, comme l'OS ; le Device renvoie 8 CC sortants et le
Controller met les paramètres à jour (notification de l'hôte : règle du ticket 10). La Machine prend effet au Hit
suivant.

**Paramètres** (JSON, figés au premier push du port). Pages 0-4 de `mdautomation` (0 SYN1-8, 1 AMD…SRR, 2 DIST…LFOM,
3 Level, 4 Mute), pour que `(page, part, index)` donne le CC sans table ; page 5 Machine (id MD brut 0-191, trous
ignorés par le Device), page 6 LFO (0 Track cible, 1 paramètre cible, 2 forme 1, 3 forme 2, 4 mode), page 7 (0 Solo,
1 Out), page 8 Master `NonPartSensitive` (Echo 0-7, Reverb 8-15, EQ 16-23, Dynamix 24-31). Ordre vu par l'hôte, par
Track : Machine, 24 paramètres, 5 LFO, Level, Mute, Solo, Out (35) ; puis les 32 Master. 592 paramètres dès le
premier push ; les Master restent muets jusqu'au ticket 09. Parts « Track n », Master « Master ».

**État.** Dump `$52` version 04 (Kit complet, Links et Chokes compris) + SysEx privée (masques mute, solo, Out). Slot
et « modifié » appartiennent à la fonction Kits (ticket 10). Après un chargement, `onStateLoaded` du Controller parse
ces octets (chunk `MIDI`) sans toucher le Device.

**Bibliothèques et moteur.** `mdProtocol`, sans émulateur, reçoit de `mdLib` ses fichiers déplacés tels quels :
`mdtypes.h`, `mdautomation.*`, `mdmachines.*`, les codecs `$52` (+ writer neuf, version 04), `$5B`, `$5D-$60`, `$62`,
`LfoSettings`, les helpers 7 bits et checksum ; `mdLib` la lie. Le codec privé vit dans `mdDrums`. `mdDrums::Engine`
gagne `setLfo` (vers `HostModel::setLfo`), Links et Chokes (ticket 15), l'API master (ticket 09) ; pas d'API solo.

**Identité.** `juce_add_plugin` propre (c0remusic, `C0rm`, `Mddr`) + les defines que pose `createJucePlugin` ;
dossier data `Documents/c0remusic/MD Drums/` ; image OS cherchée dans son `roms/`, puis comme `findFlashImage` ;
absente : `DeviceException(FirmwareMissing)`, dialogue de la pile et `DummyDevice`.

ADR : `docs/adr/0002-md-drums-device-speaks-machinedrum-midi.md` ; conséquences ajoutées à
`docs/adr/0001-md-drums-on-the-tus-stack.md`. Découpe : tickets 13, 14 et 15 avant le port (07), 16 après.

Détail des groupes : research/06-groupes.md
