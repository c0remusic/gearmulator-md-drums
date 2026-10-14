# Flash image depuis le plug-in, version, notes muettes, mises à jour

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-14)
Blocked by: 34

## Question

Retours des premiers testeurs de la pré-release `md-drums-v0.1.0` (Discord, 2026-10-14) : « le plugin ne produit aucun
son ». Cause confirmée par l'utilisateur : la flash image n'était pas trouvée. Sans elle, la pile TUS remplace le
Device par un DummyDevice muet et ouvre, deux secondes après le chargement, une boîte JUCE souvent cachée derrière
l'hôte ; l'éditeur ne dit rien. Une note hors 36-51 et 64-87 (le clavier d'ordinateur de Live part de C3) ne fait rien
non plus, sans rien dire. Demandés en même temps :

- charger la flash image depuis le plug-in ;
- afficher la version en permanence (l'hôte montre 2.2.9, celle de la base Gearmulator ; Fors, regardé par
  l'utilisateur, ne la montre qu'un instant à l'ouverture, ce que ses utilisateurs regrettent) ;
- une mise à jour automatique : l'utilisateur a choisi « prévenir seulement », actif par défaut, désactivable.

## Answer

Construit le 2026-10-14 :

- **Version** : `MD_DRUMS_VERSION` dans le CMake du plug-in (0.1.1, la prochaine), à côté du logo.
- **Ligne d'état** dans la barre du haut (colonnes 7-11), la plus importante seule :
  1. pas de flash image : « No Machinedrum flash image: choose the file », un clic ouvre un sélecteur ; le fichier
     choisi (8 Mo, un OS que `loadFirmwareFromFlash` lit) fait aussitôt le Device, les paramètres suivant son Kit, puis
     est écrit sous `elektron_sps1-1uw_os1.63.bin` dans le dossier `roms` du plug-in (seulement si le Device démarre :
     un fichier qui ne démarre pas ne remplace jamais une bonne copie) ; un fichier refusé est dit huit secondes ; aussi
     dans le menu des réglages, « Load Machinedrum flash image… ». La boîte de la pile (« copiez le firmware et
     rechargez ») ne s'ouvre plus pour une flash image manquante (`Processor::onDeviceCreationFailed`) ;
  2. une note qui ne joue rien, cinq secondes : « C3 plays nothing: Tracks are C1 to D#2 » (noms de notes de Live) ;
  3. une version plus récente : « MD Drums 0.1.2 is out », un clic ouvre la page de la release.
- **Mises à jour** : à l'ouverture d'un éditeur, une fois par processus et au plus une fois par jour (la config garde
  l'heure de la tentative, réussie ou non : hors ligne, l'éditeur suivant attend aussi), la liste des releases du dépôt
  sur l'API de GitHub, sur un thread que les éditeurs tiennent (`juce::SharedResourcePointer` : le dernier fermé
  l'arrête sur le thread des messages, jamais au déchargement du module) ; la plus haute version `md-drums-vX.Y.Z`
  hors brouillons, pré-releases comprises, faite de chiffres et de points seulement ; le résultat est partagé par
  tous les éditeurs du processus. Rien ne s'installe. « Check for updates once a day » dans le menu des réglages,
  coupé dans un Processor de test. Une build de release prend sa version en `-DMD_DRUMS_VERSION` (le workflow macOS
  la passe), pour que les noms de fichiers et l'éditeur disent la même.
- **Remplacer un Device** : `synthLib::Plugin::setDevice` passe l'état de l'ancien au nouveau. Le Device muet de la
  pile (`pluginLib::DummyDevice`) garde désormais l'état qu'on lui donne : un set ouvert, ou sauvé, sans flash image
  garde son Kit, que le Device fait ensuite reçoit ; sans état, le nouveau joue le Kit 1 d'usine. L'écran Filter et
  EQ prend les tables de l'OS au premier Device venu (avant, un éditeur ouvert sans flash image restait sans courbe
  jusqu'à sa réouverture).
- **Chemins** : les dossiers de la pile et le fichier choisi sont en UTF-8, lus par `std::filesystem::u8path` (un
  dossier utilisateur accentué se lisait dans la page de code ANSI).
- **Vérifié** :
  - `mdDrumsUpdateTest` (nouveau, au garde-fou, sans réseau ni plug-in) : 0.1.10 après 0.1.9 ; la plus récente d'une
    liste figée de GitHub est `md-drums-v0.1.10`, pré-release, devant un brouillon, une autre étiquette et une version
    à balises ; rien d'une liste vide, d'un refus de l'API ou d'une réponse cassée ; version 0.1.1 ;
  - `mdDrumsDeviceTest` : la note 60 compte, pas la 36 (Hit) ni la 81 (Lock) ;
  - `mdDrumsSkinTest` : version 0.1.1 près du logo, ligne vide avec un Device, « C3 plays nothing: Tracks are C1 to
    D#2 » après la note 60, 384 dp dans sa boîte de 384 ;
  - `mdDrumsProcessorTest` : depuis le Device muet (`setDeviceType(Dummy)`, l'état d'un testeur sans flash image), un
    set ouvert garde son Kit (EFM-BD sur la Track 1), et un set sauvé là aussi ; un fichier qui n'est pas une flash
    image est refusé (« That file is not an 8 MB flash image ») ; la vraie refait le Device, qui reçoit le Kit du set
    (EFM-BD, pas le TRX-B2 du Kit 1 d'usine), le paramètre bougé entre-temps revient, l'image est écrite dans `roms`
    d'un dossier de données temporaire et la note 36 sonne (pic 0,129) ;
  - `mdDrumsVst3HostTest` : éditeur ouvert en 422 ms avec la ligne d'état, aucun bloc en retard.
