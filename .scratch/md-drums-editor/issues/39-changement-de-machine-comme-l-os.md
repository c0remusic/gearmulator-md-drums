# Changement de machine et `$62` comme l'OS 1.63

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-16)
Blocked by: 06, 13, 26

## Question

Revue d'architecture du 2026-10-14, candidat « codec des changements de Kit » : avant d'en faire un module
(décoder, encoder et appliquer un changement de Kit sans allouer), vérifier ce que fait l'OS de chaque message.
Désassemblage des gestionnaires de l'OS 1.63 dans l'image flash dépaquetée (`$5B` à `$2052fe`, `$62` à `$205852`,
`$65`/`$66` à `$205a4a`), et `mdPlayheadProbe --lfo` relancé une fois. Trois affirmations du dépôt sont fausses :

1. **`$5B` remet 24 paramètres, pas 8.** Sans octet de portée (le message que tout le monde envoie), l'OS prend la
   portée 2 : SYN1-8 aux défauts de la machine, puis la page effets à 0 0 64 64 0 127 0 0 (`$246148`) et la page
   routage à 0 127 64 0 0 64 0 0 (`$246150`). Portée 0 : SYN1-8 seuls ; portée 1 : avec les effets. Familles à part :
   MID (`$246258`, `$246260`), CTR-AL (64 partout, `$246268`), CTR-8P (ses huit boutons prennent les valeurs des
   paramètres qu'ils visent, ses pages sont ces cibles), CTR-RE/GB/EQ/DX (SYN1-8 = le bloc master qu'ils pilotent,
   table `$2462db`). Le dépôt le savait (`.scratch/md-editor/README.md:69`, « 16 valeurs sur 25 pour TRX-SD ») mais le
   Device ne remettait que SYN1-8.
2. **`$62` hors plage est ignoré, pas borné.** Chaque champ teste sa plage et sort sans rien écrire. La sonde envoie des
   valeurs croissantes : la dernière valeur dans la plage reste, ce qui ressemble à une borne. Les champs 5 à 7 existent
   aussi : LFOS, LFOD, LFOM (paramètres 21-23), dans le Kit et l'état courant.
3. **Le mot de machine est un mot de 32 bits pour l'OS.** `$5B` l'écrit en entier (`move.l d6,$1a2(a0)`), l'OS le lit
   en entier partout ailleurs. `setKitValue` gardait les trois octets hauts.

Et deux détails de lecture : le drapeau UW n'ajoute 128 que s'il vaut 1 (le Device ajoutait 128 pour tout octet non
nul) ; l'OS prend aussi un `$5B` de 10 octets (sans drapeau UW) et de 12 (avec la portée).

## Answer

Choix de l'utilisateur (2026-10-15, « ok pour tes recos »), les six recommandations, toutes « comme le firmware » :

- **Q1** : `$5B` remet les 24 paramètres de la portée ; le Device renvoie les 24 en CC.
- **Q2** : le mot de machine entier, partout (`setKitValue` comme le Device).
- **Q3** : `$62` hors plage ignoré, au Device comme à l'encodeur (`parameterMessage` n'envoie rien).
- **Q4** : les champs 5 à 7 de `$62` décodés (LFOS, LFOD, LFOM).
- **Q5** : un seul ensemble de machines, les descripteurs du moteur (`Engine::machines`, 135 ids) : l'encodeur envoie
  tout id 0-191, le Device ne prend que ceux de son OS ; le Kit joué (`Controller::playedKit`) ignore les autres comme
  le Device, l'ensemble venant de `Processor::getMachines`.
- **Q6** : décoder exactement comme l'OS 1.63 : drapeau UW égal à 1, `$5B` de 10, 11 ou 12 octets.

Construit le 2026-10-16 (session autonome, recommandations de la session) :

- `Engine::machineDefaults(machine, kit, track, scope)` : les 24 paramètres que `$5B` laisse à la Track, d'après le
  Kit (CTR-8P lit les paramètres que visent ses boutons, dans l'ordre de l'OS, ses propres SYN1 compris ; CTR-RE/GB/EQ/DX
  lisent le bloc master). Les tables de l'OS sont des constantes de `Engine` ; `mdDrumsEngineTest` (`osTables`) les
  compare à l'image de l'OS.
- `Device` : `$5B` de 10 à 12 octets, drapeau UW égal à 1, portée ; les 24 paramètres renvoyés en CC en fin de bloc.
  `$62` : hors plage ignoré, champs 5 à 7.
- `messages::parameterMessage` : toute machine 0-191, rien pour un réglage LFO hors plage. `messages::setKitValue` :
  mot entier, et l'ensemble des machines du Device en option (`MachineSet`).
- `HitView` : une machine choisie s'entend avec ses 24 paramètres avant que le Device les renvoie.
- Tests : `mdDrumsDeviceTest` (`assignAndLfo` : formes de `$5B`, portées, UW, MID, CTR-AL, CTR-RE, CTR-8P, machine
  absente, `$62`), `mdDrumsProcessorTest` et `mdDrumsVst3HostTest` (les 16 paramètres effets et routage suivent la
  machine, relus par l'hôte), `mdDrumsHitPreviewTest` et `mdDrumsSkinTest` (aperçu avec les 24).

Laissé de côté, noté dans `Engine::machineDefaults` :

- CTR-AL vise aussi le LFO de sa Track sur « piste 17 » (valeur 16), et une machine qui la remplace ramène le LFO sur sa
  Track. Aucun paramètre hôte ne tient cette destination (LfoTrack 0-15), et le moteur ne joue aucune machine CTR.
- TRX-S2 (29) : la table d'ids de l'OS (`$252092`) la renvoie à l'enregistrement vide de GND---, dont `$5B` prend les
  défauts ; le moteur lui donne ceux de son descripteur. Le navigateur ne la propose pas.
- Le module de codec lui-même (décoder, encoder, appliquer sans allouer) reste le candidat d'architecture : ce ticket
  n'en fixe que le comportement.

Avant / après, à l'oreille : un changement de machine remet désormais filtre, EQ, distorsion, VOL, PAN, envois et LFO
de la Track aux valeurs de l'OS, comme sur la Machinedrum ; avant, la machine nouvelle gardait les effets de
l'ancienne.
