# Gigue note→son à zéro

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: 18

## Question

Une note sonne 36 à 67 échantillons après elle à 44,1 kHz (TRX-BD, `mdDrumsProcessorTest`) : 31 échantillons de
gigue, 0,70 ms. La voix de DSP2 rend par blocs de 32 échantillons et une note attend le début du bloc suivant ; seule
la partie constante est déclarée à l'hôte (21 échantillons). Seul, un kick ne le laisse pas entendre ; superposé à un
autre son dans Live, sa phase change d'un coup à l'autre et le filtrage en peigne avec, ce que l'utilisateur relève
(2026-10-08).

Retarder chaque Track d'une ligne de retard variable entre la sortie de sa voix et sa chaîne d'effets
(`m_voiceOut` vers `fxTrack`, `mdEngine/engine/MdEngine.h:118`) : une note décale sa Track de 32 moins sa position dans
le bloc, pour que le son démarre à la note plus une constante. La latence déclarée croît de 32 échantillons (0,73 ms),
que Live compense.

- Au retrigger, le retard saute : la queue précédente de la Track perd ou répète jusqu'à 31 échantillons ; mesurer le
  clic et le traiter (fondu court, ou saut au seul moment où la nouvelle voix démarre).
- Links, Chokes, LFO TRIG et le niveau recalé à chaque Hit suivent le même décalage que le Hit qui les cause.
- Sends : le retard précède les envois DEL et REV, une Track sur son Out compris.
- `mdEngineFirmwareTest` reste valable : notes alignées sur les blocs, retard constant.

Fini quand : note→son constant à l'échantillon près sur 48 notes à des positions aléatoires, à 44,1 et 48 kHz (avant :
0,70 ms de gigue) ; latence déclarée exacte ; clic de retrigger mesuré et sous un seuil fixé avant ; garde-fou vert ;
poussé sur `main`.

## Answer

Le retard se met sur les **contributions** de chaque Track au mix, pas sur la sortie de sa voix : les produits que
le mixer DSP somme (gains L et R, sends REV et DEL, sortie solo avant son limiteur). Le niveau qu'un Hit recale, PAN,
les sends et la séparation sur un Out suivent donc le Hit exactement. `mdDrums::Engine` (`mdDrumsEngine.cpp`) mixe
lui-même : `Mixer::gains` et `Mixer::mainSample` sont les étapes de `Mixer::process`, sorties telles quelles
(`process` les utilise, bit-exact), et `EngineT::mixOn = false` saute le mixeur du moteur. Chaque Track a un anneau de
64 échantillons ; ses contributions y sont ajoutées (somme modulo 56 bits, comme `add` du DSP) à 31 moins l'attente du
bloc, puis le bloc de sortie somme les 16 Tracks et passe le limiteur.

- **Hit** : la note tombe `used` échantillons dans le bloc ; le Hit sonne au bloc suivant, 32 − `used` plus tard ; la
  Track attend `used` − 1 de plus, à partir du bloc où le Hit atterrit. Tout Hit sonne 31 échantillons après sa note,
  plus le départ propre de sa voix.
- **Link** : la cible prend le retard de la source (même bloc, même vitesse), sauf muette.
- **Choke** : quand le moteur pose le Choke, la queue de la cible qui sonnerait après le Hit qui la coupe est retirée
  (cible coupée au retard du Hit). Si la cible attendait moins que le Hit, sa queue s'arrête jusqu'à 31 échantillons
  plus tôt que ce retard : le moteur l'a déjà mise à zéro ; écart dans le bloc que ce Choke avait déjà (ticket 15).
- **LFO TRIG** : exact pour un LFO qui module sa propre Track ; vers une autre Track, l'effet arrive au retard de
  celle-ci, à 31 échantillons au plus du Hit qui relance le LFO.
- **Automation** : un paramètre change au bloc suivant du moteur, puis au retard de sa Track.
- `setSampleAccurate(false)` rend l'ancien comportement (retards nuls), pour comparer.

**Mesures** (`mdDrumsEngineTest`, `mdDrumsDeviceTest`, `mdDrumsProcessorTest`) :

| | avant | après |
|---|---|---|
| TRX-BD à 44,1 kHz, 48 notes | 37-67 échantillons, moyenne 51,4, gigue 30 (0,68 ms) | 67-67, gigue 0 |
| à 48 kHz, à travers le plug-in | 53-87, gigue 34 | 86-87, gigue 1 (0,02 ms, le resampler) |
| latence déclarée | 21 (44,1 kHz), 55 (48 kHz) | 35, 71 |

- Départ propre des voix après les 31 échantillons : 4 ou 5 pour TRX-SD, -CH, -CP, EFM-BD, -SD, -HH, E12-BD, -SD,
  -CH, P-I-BD, -HH, GND-SN ; 12 pour ROM-01 ; 36 pour TRX-BD, qui sonne un bloc après son trigger. La latence déclarée
  prend le cas courant : 31 + 4 = 35.
- Mix : contre le mixeur du moteur (celui que `mdEngineFirmwareTest` compare au firmware), 200 Hits de 16 Tracks aux
  vélocités variées, une Track pannée et une avec un send : 0 échantillon différent sur 19 200, immédiatement sans
  retard, 31 échantillons plus tard avec.
- Clic de retrigger, seuil fixé avant la mesure : un E12-RC tenu (HOLD et DEC 127) refrappé à chacune des 32 places
  d'un bloc ; dans les 128 échantillons depuis le trigger, aucun pas d'échantillon plus raide que 1,5 fois le plus raide
  du même retrigger sans retard. Mesuré : 1,000 partout, c'est-à-dire que la jointure ne dépasse jamais l'attaque du
  nouveau Hit.
- CPU, 16 Tracks frappées tous les 64es de seconde : 19 à 21 % d'un cœur, avec et sans retards (bruit de mesure).
