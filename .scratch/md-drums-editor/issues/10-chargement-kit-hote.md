# Chargement d'un Kit face à l'hôte

Type: grilling
Status: open
Assignee: —
Blocked by: 04, 06

## Question

Que voit l'hôte quand un Kit se charge, et comment le Kit et les paramètres hôte restent cohérents ?

- Charger un Kit change 528 paramètres (31 par Track + 32 Master, ticket 06) : notifier l'hôte pour chacun
  (historique d'annulation de Live, automation en écriture), ou non ; ce qui se passe quand l'automation de Live
  repousse un paramètre juste après le chargement. Même règle pour les 8 CC que le Device renvoie quand une Machine
  change (SYN1-8 aux défauts, ticket 06).
- Le Device ne garde ni le Slot ni « modifié » (ticket 06) : où les garder (chunk du Controller, état de l'éditeur).
- « Kit modifié » (Save allumé) : ce qui le déclenche (geste dans l'éditeur, automation, Push, MIDI Learn).
- Ce que l'état du plug-in garde (Kit joué, son Slot, son nom, modifié ou non) face au fichier Bank partagé entre
  sets : un Slot modifié dans un autre set.
- Import `.syx` (un Kit ou les 64 d'un projet, dans les Slots vides à partir du choisi) et export.
