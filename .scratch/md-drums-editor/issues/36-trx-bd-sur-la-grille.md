# TRX-BD sur la grille

Type: task
Status: in progress
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

À établir : carte du chemin des Hits et des départs de voix (workflow de lecture), puis conception, construction et
mesures avant/après.
