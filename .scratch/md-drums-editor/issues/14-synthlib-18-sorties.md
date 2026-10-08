# synthLib à 18 sorties

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: —

## Question

Élargir `synthLib` de 12 à 18 canaux de sortie (ADR 0001, contrat du ticket 06) sous une constante nommée unique :
`TAudioOutputsT` (`source/synthLib/audioTypes.h:9`), le tableau littéral de 12 de `resamplerInOut.cpp:118`, et ce qui
dérive du type : les buffers de rejet de `Plugin` (`plugin.h:144`), le resampler (`resampler.cpp:53`), le bridge
(`bridge/bridgeLib/audioBuffers.h:44`, `bridge/server/clientConnection.h:55`), le filtre `logical < outputs.size()`
de `jucePluginLib/processor.cpp:822`.

Mesurer le coût CPU du resampler en mode Legacy sur 18 canaux, de 44,1 à 48 kHz, avant de garder Legacy comme mode
par défaut (délai calculé : 32 échantillons, 0,67 ms, à vérifier par la mesure).

Fini quand : un test fait passer 18 canaux par `Plugin` et le resampler, impulsions alignées sur tous les canaux et
délai rapporté exact ; `resamplerTimingTest` vert ; ce test est ajouté au garde-fou ; garde-fou vert ; coût CPU noté
dans le ticket ; poussé sur `main`.

## Answer

`synthLib` porte 18 canaux de sortie sous `synthLib::MaxAudioOutputs` (`source/synthLib/audioTypes.h`), à côté de
`MaxAudioInputs` (4). Seuls deux endroits écrivaient le nombre en dur : le préchauffage de `ResamplerInOut::recreate`
(`std::max(MaxAudioInputs, MaxAudioOutputs)` buffers) et `Device::dummyProcess` (initialiseur de 12 pointeurs,
devenu `{ptr, ptr}`). Le reste dérive déjà du type (`Plugin::m_discardOutputBuffers`, l'assert de
`Resampler::process`, les ring buffers et buffers du bridge, le filtre de `Processor::processBlock`). Le bridge envoie
`min(outputs.size(), getChannelCountOut())` canaux : rien à changer côté fil.

Test : `synthLibOutputChannelsTest` (`source/synthLib/synthLibTest/outputChannelsTest.cpp`), au garde-fou avec
`synthLibResamplerTimingTest`. Un Device rend une impulsion sur ses 18 sorties à l'échantillon natif de chaque note ;
à 44,1, 48, 88,2 et 96 kHz, dans les trois modes, blocs fixes et variables (0 à 512), les 18 canaux sortent leur pic
sur le même échantillon hôte, à la latence rapportée par `Plugin::getLatencyMidiToOutput()` près : un échantillon tôt
au plus (le délai du filtre est fractionnaire et rapporté arrondi au-dessus, `resamplerInOut.cpp:152`), un ou deux
tard (la note tombe sur l'échantillon natif suivant).

Délai mesuré (pic de l'impulsion, en échantillons hôte) contre rapporté :

| Mode | 48 kHz | 88,2 kHz | 96 kHz |
|---|---|---|---|
| Legacy | 31-32 / 32 (0,67 ms) | 57 / 56 | 63-65 / 63 |
| MameHq | 219 / 219 (4,56 ms) | 399 / 398 | 438-439 / 438 |
| MameLofi | 4-5 / 4 | 7 / 6 | 9-10 / 9 |

Le délai calculé de Legacy à 48 kHz (32 échantillons, 0,67 ms) est confirmé.

Coût CPU du resampler seul (Device qui rend du silence, 18 canaux, blocs de 512, Release, AMD Ryzen 7 3700X, mesuré
sur 60 s d'audio ; au garde-fou, le test en mesure 10 s et l'imprime) :

| Hôte | Legacy | MameHq |
|---|---|---|
| 44,1 kHz | 0 (pas de resampler) | 0 |
| 48 kHz | 58 ms par seconde d'audio (5,8 % d'un cœur) | 279 ms (27,9 %) |
| 96 kHz | 128 ms (12,8 %) | 607 ms (60,7 %) |

Legacy reste le mode par défaut : 5 fois moins cher que MameHq, 7 fois moins de délai. Son coût croît avec le nombre
de canaux, et `Plugin` rééchantillonne les 18 même quand l'hôte n'active aucun Out (buffers de rejet,
`plugin.cpp:141-143`) : environ 3,2 ms par seconde et par canal à 48 kHz. Le port (ticket 07) peut le réduire en
annonçant moins de canaux tant qu'aucun Out n'est actif, si la mesure du plug-in entier le demande.
