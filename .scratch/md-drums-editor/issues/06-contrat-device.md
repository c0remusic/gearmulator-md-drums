# Contrat du Device MD Drums

Type: grilling
Status: open
Assignee: —
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
