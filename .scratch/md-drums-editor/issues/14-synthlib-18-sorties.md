# synthLib à 18 sorties

Type: task
Status: open
Assignee: —
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
