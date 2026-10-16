# TRX-BD sur la grille

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-14)
Blocked by: 35

## Question

Mesuré dans Live par md-drums-seq (6d98ce8, issue 04, 2026-10-14) : MD Drums joué par MD Drums Seq et par un clip de
Live tombent au même échantillon ; sur le Kit 1 d'usine, la Track 1 (TRX-B2) démarre 2 échantillons après la grille
avec les 51 annoncés. Mais TRX-BD démarre 32 échantillons plus tard que les autres machines (31 + 36 contre 31 + 4,
`mdDrumsEngineTest`) : après la compensation de Live, il tombe 0,73 ms après la grille, et après tout autre son sur le
même pas (un sample de 909 sur une autre piste dans le cas de l'utilisateur). Les machines ROM démarreraient à 12.
Déjà connu : les Outs sortent 16 échantillons avant Main (anticipation de Dynamix), donc en avance sur la latence
annoncée.

Demande de l'utilisateur : que chaque voix attende comme TRX-BD, pour que toutes les machines démarrent au même délai,
et annoncer 32 échantillons de plus (83 sur Main à 44,1 kHz) ; les ROM pareil ; les Outs à décider.

## Answer

Construit le 2026-10-16, en autonomie (l'utilisateur dormait), sur les recommandations de la carte :

- **Où** : le retard s'ajoute du côté des sorties, dans `Engine::trigger` : chaque Track frappée (la sienne et son Link)
  attend le reste des 31 échantillons, puis ce que la voix de sa machine a d'avance sur la plus lente
  (`Engine::AlignedStart` − `Engine::voiceStart`, lu par `HostModel::machine`, accesseur local de mdEngine). Les Locks
  ne bougent pas : ils restent liés au bloc du Hit. Le mode par blocs (`setSampleAccurate(false)`, tests) ne retient
  rien.
- **Table** : statique, mesurée par `mdDrumsEngineTest` (« voice starts ») sur la sortie propre de chaque machine, à un
  Hit répété (le premier Hit après un changement de machine démarre parfois plus tard : P-I ML 260 échantillons, TRX-MA
  27, le son de la machine). 82 machines sonnent : la plupart démarrent 4 à 8 échantillons après leur bloc (comptées 4),
  les ROM 12, TRX-BD et TRX-S2 36. TRX-XT (40), TRX-XC (68) et TRX-CB (102) démarrent après TRX-BD : elles ne sont
  pas retenues et sonnent aussi tard que sur la machine (les aligner aurait coûté jusqu'à 1,5 ms de plus). Les machines
  muettes (INP, MID, CTR, RAM, ROM-33 à -48) comptent 4. Le test exige le point exact pour TRX-BD, TRX-S2, ROM-01 et
  -16, à 4 échantillons près pour une machine de chaque famille, et leur propre départ pour les trois tardives.
- **Outs** : la sortie propre de chaque Track attend aussi les 16 de Dynamix, comme Main : un Hit sonne au même
  échantillon sur Main et sur son Out. La latence annoncée passe de 51 à 83 (`Engine::HitDelay` = 31 + 36 + 16).
- **Captures** : l'éditeur capture un Hit 4 échantillons avant qu'il sonne (`Engine::CaptureDelay`, 79), pour toutes
  les machines ; HitPreview saute autant.
- L'anneau des voies passe de 64 à 128 échantillons.

Mesuré, avant et après :
- TRX-BD sur Out 01 : 67 → 83 ; sur Main 85 → 85 (Main prend 2 échantillons de plus au seuil de -100 dBFS) ; une
  machine à 4 sur Main : 51 → 83. Gigue 0 à 44,1 kHz, avant comme après.
- Latence annoncée : 51 → 83 à 44,1 kHz, 88 → 123 à 48 kHz (avec le rééchantillonneur).
- Les Chokes coupent toujours à l'échantillon où sonne le Hit qui coupe (+0), une Track plus basse au tick suivant
  (+283 au lieu de +251, le Hit qui coupe sonnant 32 plus tard).
