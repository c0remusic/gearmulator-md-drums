# Bibliothèque mdProtocol et writer de dump de kit

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
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

## Answer

`source/elektron/md/mdProtocol/`, bibliothèque statique sans émulateur, liée par `mdLib`. Déplacés par `git mv` :
`mdtypes.h`, `mdautomation.*`, `mdmachines.*`, `mdsysexautomation.*` (tous les codecs, branches Monomachine et
namespaces gardés). `LfoSettings` passe dans `mdProtocol/mdlfosettings.h`, que `mdLib/mdlivekit.h` inclut. Les
helpers communs (`product`, `hasHeader`, `validDump`, `append7Bit`, `read7Bit`, `finishDump`) sortent de l'espace
anonyme de `mdsysexautomation.cpp` vers `md::automation::sysex::codec` (`mdsysexcodec.h`). Les includes
`mdLib/md{types,automation,machines,sysexautomation}.h` deviennent `mdProtocol/…` (59 fichiers).

Ajouté (`mdProtocol/mdkit.*`) :

- `MdKit` : le Kit entier, champs du record de $460 octets de l'OS (nom brut, 24 paramètres et level par Track, mot
  machine entier, bloc LFO de 36 octets, 32 octets master dans l'ordre du dump, Links, Chokes). `fromRecord` /
  `toRecord` ; un `MdKit` neuf porte les valeurs sûres du ticket 04 : tout à 0, état LFO = mot $0000029a aux octets
  28-31, Links et Chokes à `MdKit::Off` ($ff).
- `mdKitDump(kit, slot)` : dump version 04, révision 01, 1233 octets, nom et paramètres masqués à 7 bits, machines,
  LFO et groupes (Links puis Chokes, 32 octets en un seul run 7 bits à $4a7) packés, checksum et longueur.
- `parseMdKit(dump, &slot)` : lecture sans perte ; refuse ce que l'OS refuserait (en-tête, checksum, longueur, slot
  > 63) et les versions autres que 3 et 4 (1 et 2 rangent les LFO autrement).
- `linkChange` (`$65`) et `chokeChange` (`$66`) : `F0 00 20 3C 02 00 65|66 <track> <cible> F7`, Off envoyé en $7f.
- `md::fw::loadPatchImageFromFlash(flash, uw)` dans `mdEngine/tools/mdfw/Firmware.*` : l'image d'usine de la
  mémoire patch (section 4 UW, 3 sinon), pour les tests et l'amorce de la Bank.

Tests, au garde-fou :

- `mdProtocolKitTest` (sans émulateur, image flash seule) : les 32 Kits d'usine (16 UW, 16 non-UW), record →
  `MdKit` → record identique ; → dump de 1233 octets → `parseMdKit` champ pour champ égal, slot compris → dump identique
  octet pour octet ; `parseKitDump` (lecteur de l'éditeur) relit nom, paramètres, levels, machines, LFO et master.
  Les Chokes trouvés sont ceux de `research/06-groupes.md` (Kit 1 : piste 9 → 10) ; aucun Link ; l'état LFO et les
  octets hauts des machines de chaque Kit d'usine égalent ceux d'un `MdKit` neuf ; Kit 17 vide ; Kit de travail UW =
  Kit 1. Plus les messages `$65`/`$66` et les dumps refusés.
- `mdKitDumpFirmwareTest` (firmware complet) : les 16 Kits d'usine envoyés par le firmware, relus puis réécrits,
  sortent octet pour octet ; un Kit écrit par `mdKitDump` (nom, Links, Chokes, LFO, machine ROM-02, level changés) est
  stocké par le firmware en Kit 41 et renvoyé octet pour octet ; `$65`/`$66` sur le Kit vivant puis SAVE KIT : Links et
  Chokes relus comme envoyés, `$7f` stocké en $ff (OFF), ce que la recherche disait sans l'avoir exécuté.
