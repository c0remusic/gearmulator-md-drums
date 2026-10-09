# Effets master dans mdEngine et MD Drums

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 09

## Question

Construire ce que le ticket 09 a fixé : la section master du DSP1 de l'OS 1.63 (P:$342-$970) jouée par un second
DSP56300 émulé, dans `EngineT::render`, et les 32 paramètres Master de MD Drums audibles.

- **Harnais** (mdEngine) : DSP56300 avec Peripherals56303, mémoire externe pontée à $020000, P = XY = $200000 ; tous les
  records de `fw::Firmware::dspB` chargés ; boot partiel exécuté une fois (table de sinus, effacements, inits Echo,
  Dynamix, Gate Box ; recherche 03, section 5) ; P:$971 vers un stub d'attente ; par bloc, bus X:$180, $1C0, $600 écrits
  depuis `Mixer::Output`, Main lu en Y:$000-$03F.
- **Coefficients** : corps de l'OS dans `MachineRunner`, sorties en `rts`, un effet par tick (`tick & 3`), après le
  lissage ; cibles en $1000F6C + k ; mots écrits en Y entre deux blocs.
- **API** : `mdDrums::Engine::setMaster(0-31, 0-127)`, ordre du dump de Kit ; Echo TIME sur `setTempo` ; pas de
  bypass. Le Device route `$5D-$60` et le dump `$52` vers elle.
- **Latence** : Main 16 échantillons après les Outs ; latence déclarée 35 → 51.
- **Fidélité** : Main bit-exact contre le firmware complet, paramètres fixes, une fixture comme
  `mdEngineFirmwareHits` ; écarts expliqués seulement.
- **CPU** : temps de rendu sur le thread audio sous 30 % du temps réel, garde dans un test.

Fini quand : Main du moteur identique à celui du firmware sur les scénarios ; latence note→son et gigue rapportées
avant et après ; CPU sous le plafond ; garde-fou vert ; poussé sur `main`.

## Answer

- **Harnais** (`mdEngine/engine/MasterEngine.*`) : un second DSP56300, Peripherals56303, mémoire externe pontée à $020000,
  P = XY = $200000 ; tous les records de la section 2 chargés ; l'entrée du programme (P:$24) puis son boot, dont on
  saute l'état des Tracks (P:$100031 vers $100058) et les périphériques (P:$100065 en `rts`) ; P:$2E et P:$971 vers un
  stub HI08 au modèle de `VoiceEngine` (le DSP signale la fin du bloc, attend « go », repart en P:$342). Par bloc : les
  trois bus écrits en X:$180, $1C0, $600, Main lu en Y:$000-$03F. `dynamicFastInterrupts` est nécessaire (sans lui,
  l'entrée tourne en rond en P:$24).
- **Coefficients** (`HostModel`) : `setMaster(0-31, 0-127)` pose les cibles en $1000F6C + k, lissées par `$100029E` ;
  chaque tick, après ce lissage, un des quatre corps de l'OS ($20B4AE Echo, $20B604 Dynamix, $20B74E EQ, $20B940 Gate
  Box) selon `tick & 3`, sorties patchées en `rts` ; ses mots mis en attente sont repris une fois (`takeMasterWords`)
  et écrits en Y avant le bloc suivant du master.
- **API** : `EngineT::enableMaster` crée le harnais ; avec le mixeur, `render` le joue dans `Output::master`, sinon
  l'appelant (`processMaster`). `mdDrums::Engine::setMaster`, `MasterCount`, `MasterDelay` = 16,
  `masterDefaults()` = Kit 1 d'usine ; Main sort du master, les Outs non. Le Device envoie `$5D-$60` et les 32 octets
  du Kit (déjà dans l'ordre du moteur, Gate Box d'abord). Pas de bypass.
- **Fidélité** (`mdEngineFirmwareMaster`, `mdEngineFirmwareTest`) : 7 scénarios sur le firmware (Kit 1, gate ouvert,
  chaque envoi seul, EQ et Dynamix qui compriment, Echo filtré mono réinjecté, Gate Box nourrie par l'Echo). Les mots de
  l'OS calculés par le moteur égalent ceux du DSP1 du firmware, mot pour mot. `MasterEngine` repris de l'état du DSP1
  du firmware avant le Hit (X et Y internes, mémoire externe de $1000DA à $14FFFF, pris pendant que le DSP attend son
  bloc suivant) et nourri des bus du moteur donne Main **échantillon pour échantillon** dans les 7, 5 blocs après l'état
  (stable sur 3 tours) ; idem avec registres et scratch (X et Y $000-$0FF) tirés au hasard avant chaque bloc : la
  section ne dépend de rien de ce que les parties sautées laissent. Depuis sa propre histoire, le moteur diffère : les
  anneaux de l'Echo et de la Gate Box et le sinus de sa prise modulée sont là où les blocs écoulés les ont mis, et un
  anneau qui boucle saute à son début, si bien qu'une lecture qui franchit la fin tombe un échantillon à côté (2e écho
  d'un échantillon en retard). Écart non tracé, côté firmware : avec EFM-BD et TRX-SD, Main du firmware a différé dans
  certains tours de −75 dB dès le premier échantillon, même écart quels que soient les réglages du master, donc dans ce
  qui entre dans son master ; TRX-BD, EFM-CB et P-I-MT jamais. Le lissage de l'OS s'arrête 3 sous une cible atteinte
  par en dessous, pile dessus par au-dessus : le test rejoue le chemin des valeurs du firmware.
- **Latence** : déclarée 35 → 51 échantillons. Main de TRX-BD, premier échantillon au-dessus de −100 dBFS : 85-85 à
  44,1 kHz (Out 01 67-67, gigue 0) ; 115-119 à 48 kHz (Out 01 86-87). Main garde une traîne de quelques LSB après un
  Hit (EQ et Dynamix), comme la machine (« idle outputs hold one LSB ») : les tests prennent le silence sous
  −100 dBFS sur Main, les Outs restent à zéro exact.
- **CPU** : 16 Tracks jouées et master 19,6-20,3 % d'un cœur (`mdDrumsEngineTest`, plafond 30 % en garde) ; Device
  avec télémétrie 22,3 % (20,0 % avant le master).
- Tests adaptés : `mdDrumsEngineTest` (équivalence de Main avec le mixeur du moteur et son master, les bus retardés de
  31 pour l'échantillon près : 0 différence sur 200 Hits), `mdDrumsDeviceTest` et `mdDrumsProcessorTest` (silence sous
  −100 dBFS, Mute et Solo lus sur Out 01, envois de la Track 1 à 0 là où Main doit se taire).
