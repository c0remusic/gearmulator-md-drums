# Chargement d'un Kit face à l'hôte

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
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

## Answer

Grilling du 2026-10-09 (l'utilisateur a pris les recommandations, deux tours).

- **Hôte au chargement** : comme un preset TUS. Chaque paramètre du Kit est posé en `PresetChange` (sans geste), puis
  un seul `notifyHostOfProgramChange` : Live reçoit les valeurs et « programme changé », n'enregistre pas
  d'automation. Ce que l'undo de Live en fait est à constater à l'installation.
- **Automation contre Kit chargé** : l'automation de Live gagne, comme sur tout synthé ; les paramètres non
  automatisés gardent la valeur du Kit.
- **Save allumé** : par comparaison de contenu, quand le Kit joué diffère de celui de son Slot dans la Bank (nom,
  machines, paramètres, levels, LFO, master, Links et Chokes), quelle que soit la source de l'écart (éditeur,
  automation, Push, MIDI Learn, un autre set qui a réécrit le Slot). Rien à stocker. Mute, Solo et Out ne sont pas
  dans le Kit.
- **Slot joué** : dans un chunk du Processor, à côté de l'état du Device, présent éditeur fermé. Le nom est dans le
  Kit.
- **Set et Bank** : un set garde sa propre copie du Kit (état du Device) et sonne comme enregistré ; la Bank est une
  bibliothèque. Un Slot réécrit ailleurs allume Save.
- **Vers le Device** : un dump `$52` entier au bloc audio suivant (le Device applique tout d'un coup, sans renvoyer
  les CC SYN) ; les paramètres sont posés ensuite en `PresetChange`, sans écho. Le parse du dump ne doit pas allouer
  sur le thread audio, ou une fois par chargement au plus.
- **Import** : un Kit ou les 64 d'un projet, dans les Slots vides à partir du choisi, sans boucler ; ce qui ne tient
  pas est laissé, et la note le dit (« 12 importés, 3 laissés : plus de Slot vide »). Versions 3, 4 et 64 acceptées
  (ticket 04).
- **Export** : le Kit du Slot choisi, en un dump `$52`. Le fichier Bank est déjà un `.syx` de 64 dumps.
- **Deux instances** : la Bank est relue à l'ouverture de l'overlay Kits et juste avant chaque écriture, et une
  écriture ne touche que les 1233 octets de son Slot, à leur place.
- **CC SYN après un changement de machine** : même règle que le chargement (valeurs posées, hôte prévenu sans geste,
  ces 8 seulement) ; le changement de machine lui-même reste un geste.
- **Rename** : écrit le nom dans la Bank ; si le Slot joue, le Kit joué prend le même nom (Save reste éteint).
- **Delete du Slot qui joue** : le Kit joué reste, le Slot devient « Empty », Save s'allume, Ctrl+S le réécrit ;
  second clic « Delete it ».
- **Load d'un Slot vide** : grisé.
- **Save here** sur un autre Slot : le Kit joué y est écrit (« Replace it » s'il est occupé) et ce Slot devient celui
  qui joue ; l'ancien n'est pas touché.
- Déjà fixé : Bank = un fichier de 64 dumps de 1233 octets dans le dossier data du plug-in, amorcé des 16 Kits
  d'usine aux Slots 1-16 (ticket 04) ; un Kit ne contient ni Mute, ni Solo, ni Out (HANDOFF) ; « Load anyway » quand
  le Kit joué a changé (HANDOFF).

Ticket créé : 26 (Kits et Bank, construction).
