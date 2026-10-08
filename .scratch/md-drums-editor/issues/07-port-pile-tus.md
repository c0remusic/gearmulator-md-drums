# Port du processor MD Drums sur la pile TUS

Type: task
Status: open
Assignee: —
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
