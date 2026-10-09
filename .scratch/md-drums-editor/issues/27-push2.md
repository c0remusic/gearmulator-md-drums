# Push 2 : relais, liste et noms

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 11

## Question

Construire ce que le ticket 11 a fixé.

- **Relais** : 32 paramètres hôte en fin de liste (IDs nouveaux, anciens figés) qui visent la Track montrée : Track
  (1-16), Machine, Level, Mute, Solo, Out, LFO Shape 1, LFO Shape 2, SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST VOL
  PAN DEL REV LFOS LFOD LFOM. Un relais tourné (Push, automation) pose le vrai paramètre sans geste ; quand la Track
  montrée change, ou qu'un de ses vrais paramètres bouge, les relais suivent et préviennent l'hôte sans geste. Rien
  n'alloue ni ne verrouille sur le thread audio.
- **Track montrée** : celle du Controller, que l'éditeur et le relais Track suivent dans les deux sens ; dans l'état.
- **Noms** : relais aux noms de la Machinedrum ; Master renommés pour l'écran de Push (« ECHO TIME »…), IDs inchangés.
- **« Prepare Push list »** : commande du menu des réglages de l'éditeur qui bouge, dans l'ordre des 8 banques, les 64
  paramètres de la liste (gestes begin, valeur, end), pour Live en mode Configure.
- **Documentation** : liste, mise en place dans Live (Configure, « Save as Default Configuration »), automation des
  relais, Drum Rack pour les pads, limites (valeurs réduites à leurs chiffres).

Fini quand : relais et Track montrée vérifiés dans `mdDrumsProcessorTest` (dans les deux sens, état, sans allocation sur
le thread audio) ; commande de liste testée (ordre et gestes) ; garde-fou vert ; poussé sur `main`. Le passage dans Live
et sur Push 2 se constate à l'installation.

## Answer

- **Relais** : 32 paramètres hôte en page 9 (`9_0_0` à `9_0_31`, après les 576, IDs anciens intacts),
  `NonPartSensitive`, aux noms de la Machinedrum. Un relais posé par l'hôte passe par `Controller::sendParameterChange`
  comme toute valeur hôte (n'importe quel fil) : sa valeur va au slot du vrai paramètre de la Track montrée (le Device
  l'a au bloc suivant) et un bit en attente ; sur le fil message (`syncRelays`, au timer du Controller), le vrai
  paramètre la prend (`setValueFromSynth`, origine Midi : hôte prévenu sans geste, sans renvoi au Device). Puis chaque
  relais prend ce que tient la Track montrée quand son paramètre a bougé depuis la dernière valeur donnée (comparaison
  à cette valeur, pas au relais : un tour en cours n'est jamais défait). Aucune allocation quand l'hôte pose un relais
  (mesuré).
- **Track montrée** : la partie courante du Controller ; le relais Track la change, l'éditeur la suit
  (`onCurrentPartChanged`) ; chunk « FOCS » de l'état.
- **Noms** : `DrumsParameter::getName` rend le nom affiché seul pour les paramètres sans Track (Master, relais) ; Master
  renommés « Echo TIME », « GBox DVOL », « EQ LG », « Dyn ATCK » (Push les met en capitales).
- **« Prepare Push list (Live in Configure mode) »** : entrée du menu des réglages (clic droit) ; les 32 relais puis les
  32 Master, chacun posé à sa propre valeur dans un geste (begin, valeur, end).
- **Documentation** : `docs/md-drums-push2.md` (liste, mise en place, automation des relais, Drum Rack, limites).
- **Mesures** (`mdDrumsProcessorTest`) : 608 paramètres, relais après les Master ; FLTF posé par son relais arrive au
  paramètre et au Device ; le relais Track montre la Track 6 et les relais en prennent les valeurs ; FLTF de la Track
  montrée bougé ailleurs, son relais suit et l'hôte est prévenu ; état ; liste Push : 64 paramètres dans l'ordre, 64
  gestes. `mdDrumsSkinTest` : les relais exclus du contrôle « chaque paramètre a un contrôle » (ils n'en ont pas, par
  construction).
- À constater dans Live : que Configure prend la liste par la commande, l'écran de Push, les Outs depuis un Drum Rack.
