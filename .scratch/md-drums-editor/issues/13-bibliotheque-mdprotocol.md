# Bibliothèque mdProtocol et writer de dump de kit

Type: task
Status: open
Assignee: —
Blocked by: —

## Question

Sortir de `mdLib` les codecs de la Machinedrum vers `source/elektron/md/mdProtocol/`, bibliothèque statique sans
émulateur, selon le contrat du ticket 06 : fichiers déplacés tels quels (`git mv`, branches Monomachine et namespaces
gardés) : `mdtypes.h`, `mdautomation.*`, `mdmachines.*`, le dump `$52`, `$5B`, `$5D-$60`, `$62`, `LfoSettings`
(aujourd'hui dans `mdlivekit.h`, que `LiveKit` garde dans `mdLib`), les helpers 7 bits et checksum. `mdLib` la lie.

Ajouter :

- le writer de dump de kit, version 04, avec les valeurs sûres du ticket 04 pour les octets que `parseKitDump` ignore
  (`research/04-kits.md`, « Bytes `parseKitDump` does not read ») ;
- la lecture et l'écriture des Links et Chokes (37 octets à `$4a7`, `research/06-groupes.md`) dans le dump ;
- les messages `$65` et `$66` (Link et Choke d'une Track, `research/06-groupes.md`).

Fini quand : les 32 Kits d'usine de l'image OS (sections 3 et 4, ticket 04), écrits en dump puis relus, redonnent
champ pour champ leur record, Links et Chokes compris ; un dump reçu d'une Machinedrum fait l'aller-retour octet pour
octet ; ces tests sont ajoutés au garde-fou ; garde-fou vert ; poussé sur `main`.
