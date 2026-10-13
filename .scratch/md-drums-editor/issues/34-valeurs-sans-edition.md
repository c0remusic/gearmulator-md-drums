# Valeurs du plug-in dites à Live : édition ou relecture

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-13)
Blocked by: 32

## Question

Trouvé en revalidant les tickets 32 et 33 dans Live par script (2026-10-13), et laissé ouvert par le ticket 32 (« ce
que Live fait des SYN1-8 renvoyés et des valeurs publiées au chargement d'un Kit ») : le plug-in disait à l'hôte les
valeurs qu'il change de lui-même (relais de Push 2 au chargement et à chaque changement de Track montrée, SYN1-8 d'une
machine changée, tous les paramètres d'un Kit chargé) par `performEdit`, sans geste. Que fait Live de ces éditions, et
comment les dire autrement ?

## Answer

Deux façons de dire une valeur à l'hôte VST3 :

- **Édition** (`performEdit`) : l'emballage VST3 de JUCE en fait une de chaque listener d'un paramètre appelé sur le fil
  des messages.
- **Relecture** (`restartComponent(kParamValuesChanged)`, `updateHostDisplay` avec `programChanged` dans JUCE) : l'hôte
  relit toutes les valeurs. Le JUCE du dépôt relit déjà la valeur du paramètre dans `getParamNormalized`.

**Mesuré dans Live 12.2.1** (pont `TupleMCP` ; l'éditeur piloté par le serveur MCP de MD Drums, dont les outils DOM
passent par le fil des messages, `enableMcpServer` allumé le temps des essais puis rendu ; pistes « ZZ test ») :

- **Éditions** (build `812551fa`) :
  - au chargement, Live garde une entrée temporaire « LFOM », le dernier des 32 relais posés sur la Track 1 ;
  - Arrangement Record et Automation Arm allumés, **le seul chargement enregistre l'automation des 32 relais** et les
    ajoute au panneau. Un Kit chargé passait par le même chemin pour ses 2705 paramètres (`notifyHostOfProgramChange`
    appelait tous les listeners) ;
  - undo sain : le premier undo après le chargement retire MD Drums.
- **Relecture** (build `362c62a8`, tout en relecture) :
  - au chargement, Live n'expose que « Device On » ;
  - Slot 04 chargé depuis l'overlay : Live lit « Track 1 FLTF » à 0,1969 = 25/127. **Live suit** ;
  - mais pendant qu'il enregistre, Live écrit l'automation de **tous** les paramètres de son panneau à chaque
    relecture, changés ou non : « Track 1 FLTF », inchangé, enregistré quand une autre Track est montrée, quand FLTW
    est tourné (le relais FLTW suit), quand la machine change ; un Kit enregistre FLTF sans rien ajouter au panneau ;
    enregistrement Session dans un clip pareil (enveloppe FLTF dans le clip). Rien ne change, rien ne s'écrit. Avec la
    liste Push configurée, chaque bouton tourné écrirait 64 couloirs plats.
  - Live ne met pas `kRecording` dans le `ProcessContext` : la relecture du correctif suivant, retenue tant que l'hôte
    enregistre, partait en plein enregistrement (Live lisait déjà 25/127 pendant). Retenue retirée.

**Retenu** : à chaque cas le moyen qui écrit le moins dans Live.

- **Relecture pour ce qui change en bloc** : un Kit, un état, les SYN1-8 d'une machine (valeurs posées en
  `PresetChange`, sans listener de l'hôte, puis une relecture à la fin du tick suivant du timer du Controller,
  `Controller::tellHost` la demande). Rien ajouté au panneau, comme le voulait le ticket 10 (« l'automation de Live gagne »).
- **Édition, sans geste, pour un paramètre qui en suit un autre** : un relais qui suit son paramètre de la Track
  montrée (bouton tourné, automation, autre Track montrée), un paramètre qui suit son relais réglé par Push. Live
  n'écrit que celui-là. Dans le tick d'une relecture, les relais suivent sans édition, dans la relecture.
- **Rien au chargement** : les relais prennent les valeurs de la Track 1 dans la construction du Controller, dans une
  relecture.
- Restent des éditions avec geste : un geste dans l'éditeur, « Prepare Push list » (voulu, pour le mode Configure).

**Vérifié** :

- `mdDrumsProcessorTest` : Kit chargé, 0 édition et 1 relecture ; machine changée, SYN1 de 43 à 64, 1 relecture, 0
  édition ; relais réglé par l'hôte, son paramètre en édition ; autre Track montrée, 17 relais en éditions ; paramètre
  de la Track montrée automatisé, son relais en édition.
- `mdDrumsVst3HostTest` classe ce que l'hôte entend : édition, ou relecture (l'hébergement de JUCE relit tout, appelle
  les listeners de ce qui diffère et annonce le redémarrage dans le même message). Le build `812551fa` échoue au
  chargement (« FocusTrack … FocusLFOM » en éditions) ; le nouveau : 0 édition au chargement, au changement de machine
  (SYN1-8 relus), à la restauration d'un état, pendant les Locks ; pendant l'automation de SYN1 de la Track montrée,
  le seul relais FocusSYN1.
- Le build retenu n'est pas encore passé dans Live (il y faut un redémarrage de Live) : ses deux moyens, si.
