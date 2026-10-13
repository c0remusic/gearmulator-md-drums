# Valeurs du plug-in dites à Live sans édition

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-13)
Blocked by: 32

## Question

Trouvé en revalidant les tickets 32 et 33 dans Live par script (2026-10-13), et laissé ouvert par le ticket 32 (« ce
que Live fait des SYN1-8 renvoyés et des valeurs publiées au chargement d'un Kit ») : le plug-in disait à l'hôte les
valeurs qu'il change de lui-même (relais de Push 2 au chargement et à chaque changement de Track montrée, SYN1-8 d'une
machine changée, tous les paramètres d'un Kit chargé) par `performEdit`, sans geste. Que fait Live de ces éditions ?

## Answer

**Mesuré dans Live 12.2.1 avec le build `812551fa`** (pont `TupleMCP`, pistes « ZZ test ») :

- Au chargement, Live garde une entrée temporaire « LFOM » : le dernier des 32 relais que le plug-in pose sur la
  Track 1. C'est ce que fait Live d'un paramètre réglé dans la fenêtre d'un plug-in.
- **Arrangement Record et Automation Arm allumés, le seul chargement de MD Drums enregistre l'automation des 32
  relais** et les ajoute au panneau du device. Même mécanisme pour un changement de Track montrée (relais), de machine
  (SYN1-8) et pour un Kit chargé, dont `notifyHostOfProgramChange` appelait les listeners des 2705 paramètres.
- Undo : sain. Le premier undo après le chargement retire MD Drums ; ces éditions sans geste ne font pas de pas d'undo.

Cause : l'emballage VST3 de JUCE change les listeners d'un paramètre appelés sur le fil des messages en `performEdit`.
Le JUCE du dépôt relit déjà la valeur du paramètre dans `getParamNormalized` : un
`restartComponent(kParamValuesChanged)` suffit pour que l'hôte relise tout.

**Corrigé** : une valeur que le plug-in change de lui-même passe en `PresetChange` (aucun listener de l'hôte), puis
`Controller::tellHost` (`updateHostDisplay` avec `programChanged`, comme la pile TUS après un preset) fait relire
toutes les valeurs à l'hôte, une fois par tick du timer du Controller pour les relais et les SYN1-8, une fois par Kit
ou état. Les relais prennent les valeurs de la Track 1 dès la construction du Controller, avant toute lecture de
l'hôte. Restent des éditions : un geste dans l'éditeur, et « Prepare Push list » (voulu, pour le mode Configure).

**Vérifié** :

- `mdDrumsProcessorTest` compte exactement : Kit chargé, 0 édition et 2 relectures ; machine changée, SYN1 de 43 à
  64, 0 édition ; relais réglé par l'hôte, autre Track montrée, paramètre de la Track montrée automatisé : 0 édition.
- `mdDrumsVst3HostTest` classe ce que l'hôte entend : édition, ou relecture (l'hébergement de JUCE relit tout, appelle
  les listeners de ce qui diffère et annonce le redémarrage dans le même message). Le build `812551fa` échoue au
  chargement (« FocusTrack … FocusLFOM » en éditions) ; le nouveau : 0 édition au chargement, pendant l'automation, au
  changement de machine (SYN1-8 relus), à la restauration d'un état, pendant les Locks.
