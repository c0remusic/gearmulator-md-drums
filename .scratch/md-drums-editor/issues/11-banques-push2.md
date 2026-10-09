# Banques Push 2 de MD Drums

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 02, 06

## Question

Que montrent les 8 encodeurs de Push 2 sur MD Drums, et dans quel ordre ?

- Banques fixes par Track (SYN, EFX, ROUTING), banques qui suivent la Track affichée dans l'éditeur (paramètres
  relais), ou configuration laissée à l'utilisateur via « Configure ».
- Place des Master effects, de Level, Mute, Solo, Out.
- Paramètres à ajouter au JSON et noms courts lisibles sur l'écran de Push. Le ticket 06 fixe 592 paramètres, par
  Track : Machine, 24 paramètres, 5 LFO, Level, Mute, Solo, Out ; puis 32 Master. Résolu avant le port (07), ce
  ticket peut ajouter ses paramètres relais au JSON du premier push ; après, ils viennent en `version` 1, en fin de
  liste.

## Answer

Grilling du 2026-10-09 (l'utilisateur a pris les recommandations, deux tours).

- **Approche** : des paramètres relais, ajoutés en fin de liste (IDs nouveaux, les anciens figés), qui visent la Track
  montrée ; les 32 Master en direct. Une seule liste Configure sert toutes les Tracks.
- **Liste, 8 banques, 64 paramètres** (le seuil où Live remplit seul un panneau) :
  1. Track (relais du choix de la Track montrée, 1-16), Machine, Level, Mute, Solo, Out, LFO Shape 1, LFO Shape 2 ;
  2. SYN1-8 ;
  3. EFFECTS : AMD AMF EQF EQG FLTF FLTW FLTQ SRR ;
  4. ROUTING : DIST VOL PAN DEL REV LFOS LFOD LFOM ;
  5. à 8. Echo, Gate Box, EQ, Dynamix (paramètres Master existants).
  Banques 2 à 4 = les pages d'encodeurs de la Machinedrum. Cible et mode du LFO restent à l'éditeur (ASSIGN).
- **Relais tourné** : il pose le vrai paramètre de la Track montrée, l'hôte prévenu sans geste. Live armé enregistre
  l'automation sur le relais, qui signifie « la Track montrée » ; documenté : l'automation d'une Track précise passe par
  ses propres paramètres (éditeur, piste du paramètre). Push sert à jouer en direct.
- **Track montrée** : une seule, celle du Controller ; le relais Track et l'éditeur se suivent dans les deux sens et
  l'état la garde. Quand elle change, ou quand un vrai paramètre de la Track montrée bouge, les relais prennent les
  nouvelles valeurs et préviennent l'hôte sans geste, pour l'écran de Push.
- **Arrivée dans Live** : une commande de l'éditeur (« Prepare Push list », dans le menu des réglages), Live en mode
  Configure : elle bouge chaque paramètre de la liste dans l'ordre, et Live les ajoute ainsi ; puis « Save as Default
  Configuration » une fois. À vérifier dans Live ; repli : procédure manuelle documentée.
- **Noms** : relais aux noms de la Machinedrum (« SYN1 », « FLTF », « LEVEL », « TRACK »…) ; Master renommés pour
  l'écran (« ECHO TIME », « GBOX DVOL », « EQ LG », « DYN ATCK »), IDs inchangés. Push montre « -2 » pour TRX-B2
  (valeur réduite à ses chiffres) : limite de Live, documentée.
- **Pads** : montage documenté, MD Drums dans un Drum Rack, chaîne en Receive « All Notes » ; les 16 pads envoient
  36-51 avec la vélocité. À vérifier à l'installation : les Outs atteignables depuis un Drum Rack.

Ticket créé : 27 (Push 2, construction).
