# Gigue note→son à zéro

Type: task
Status: open
Assignee: —
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
