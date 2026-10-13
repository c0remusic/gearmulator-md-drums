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

**Hybride `674e3908` passé dans Live** (relais en éditions, le reste en relecture) :

- un bouton tourné dans l'éditeur fait éditer son relais juste après : Live prend le relais « FLTF » pour le paramètre
  touché en dernier, au lieu de « Track 1 FLTF ». Choisir au toucher le paramètre à automatiser tombe sur le relais, et
  enregistrer un bouton écrit aussi son relais ;
- une autre Track montrée pendant l'enregistrement écrit les relais qui changent et les ajoute au panneau ;
- undo sain : le premier undo après un Kit chargé ramène le Kit d'avant (FLTF 25 vers 46), le second le bouton tourné
  (46 vers 31) ;
- SYN1 automatisé, machine changée pendant la lecture : SYN1 prend la valeur de la nouvelle machine (64), et Live met
  l'automation de SYN1 en reprise en main (`automation_state` 2) jusqu'à « Re-Enable Automation ».

**Retenu par l'utilisateur (C, 2026-10-14)** :

- **Ce qui suit un paramètre, l'hôte ne l'apprend pas** : un relais qui suit son paramètre de la Track montrée (bouton
  tourné, automation, MIDI), un paramètre qui suit son relais réglé par Push. Live garde « Track 1 FLTF » comme dernier
  touché et n'enregistre que le bouton tourné. Prix : Push 2 montre l'ancienne valeur du relais jusqu'au prochain
  changement de Track ou à la prochaine relecture ; tourner alors cet encodeur part de l'ancienne valeur.
- **Les relais en éditions, sans geste, quand la Track montrée change** (éditeur ou relais Track réglé par Push) : Push
  montre la nouvelle Track. Pendant un enregistrement, Live écrit les relais qui changent.
- **Relecture pour ce qui change en bloc** : un Kit, un état, les SYN1-8 d'une machine (valeurs posées en
  `PresetChange`, sans listener de l'hôte, puis une relecture à la fin du tick suivant du timer du Controller,
  `Controller::tellHost` la demande). Rien ajouté au panneau. Les relais suivent dans la relecture.
- **Rien au chargement** : les relais prennent les valeurs de la Track 1 dans la construction du Controller, dans une
  relecture.
- Restent des éditions avec geste : un geste dans l'éditeur, « Prepare Push list » (voulu, pour le mode Configure).

**Vérifié** :

- `mdDrumsProcessorTest` : Kit chargé, 0 édition et 1 relecture ; machine changée, SYN1 de 43 à 64, 1 relecture, 0
  édition ; relais réglé par l'hôte, son paramètre suit sans rien dire à l'hôte ; autre Track montrée, les relais en
  éditions ; paramètre de la Track montrée automatisé, son relais suit sans rien dire.
- `mdDrumsVst3HostTest` classe ce que l'hôte entend : édition, ou relecture (l'hébergement de JUCE relit tout, appelle
  les listeners de ce qui diffère et annonce le redémarrage dans le même message). Le build `812551fa` échoue au
  chargement (« FocusTrack … FocusLFOM » en éditions) ; le nouveau : 0 édition au chargement, pendant l'automation, au
  changement de machine (SYN1-8 relus), à la restauration d'un état, pendant les Locks ; le relais Track réglé par
  l'hôte, les relais en éditions et aucun paramètre de Track.
