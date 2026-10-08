# Port du processor MD Drums sur la pile TUS

Type: task
Status: open
Assignee: —
Blocked by: 06

## Question

Porter `mdDrumsPlugin` sur `pluginLib::Processor` + Device + Controller + JSON, selon le contrat du ticket 06, avec le
comportement d'aujourd'hui : 432 paramètres, Main + 16 Outs, resampling Lagrange hors 44,1 kHz, état sauvé, et un
skin minimal à la place de l'éditeur générique de JUCE.

Fini quand : `mdDrumsProcessorTest` porté et vert, garde-fou vert, latence note→son et gigue mesurées avant et
après, poussé sur `main`. Les IDs de paramètres sont figés à partir de ce push.
