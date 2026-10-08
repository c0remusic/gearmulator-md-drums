# Pile TUS pour un moteur sans firmware

Type: research
Status: resolved
Assignee: c0remusic (agent de recherche)
Blocked by: —

## Question

Que demande la pile TUS à un plug-in, et comment `mdDrums::Engine` (pas de ROM DSP56300 à booter, rendu par blocs
de 32 échantillons à 44,1 kHz, 16 sorties mono en plus de Main) y entre ?

- Ce qu'exigent `pluginLib::Processor` (`source/jucePluginLib/processor.h`), `jucePluginEditorLib::Processor` et
  `synthLib::Device` : chargement de ROM, threads DSP, fréquence d'échantillonnage et resampling, entrée et sortie
  MIDI, `getState`/`setState`, blocs de latence, disposition des bus.
- Le JSON de paramètres du Controller : pages, `parts`, `midipackets`, `regions`, `valueList`, et comment un
  changement de paramètre devient un message MIDI vers le Device. Ce que `part` donne pour 16 Tracks.
- Comment le MIDI de l'hôte (notes 36-51) atteint le Device, et ce que la pile ajoute au chemin note→son.
- Le plug-in TUS le plus proche comme modèle (un Device sans firmware, ou le plus mince), avec ses fichiers.
- Ce que la pile apporte déjà pour le zoom de l'éditeur (75-150 %) et les réglages par utilisateur.

Faits à confronter : MD Drums aujourd'hui est un `juce::AudioProcessor` + APVTS (`mdDrumsProcessor.h:20`),
432 paramètres `t<n>_<name>`, bus « Output » + « Track 1 »..« Track 16 ». Les IDs JUCE de pluginLib sont
`page_part_index` (`jucePluginLib/parameter.cpp:373`).

Livrable : `research/01-pile-tus.md`, avec `fichier:ligne` pour chaque fait.

## Answer

Un `synthLib::Device` n'a besoin ni de DSP56300 ni de ROM à booter : la pile ne lie aucun émulateur, et `DummyDevice`
comme le Device du JE-8086 (un H8S) le montrent. Le contrat tient en huit méthodes pures (taux, validité, état,
canaux, horloge DSP, `sendMidi`, `processAudio`, `readMidiOut`) ; un Device synchrone n'a aucun thread à fournir.
Le blocage dur : `TAudioOutputs` plafonne à 12 canaux (`synthLib/audioTypes.h:9`), le Processor jette les canaux
au-delà et `Plugin` déborderait ses tableaux ; MD Drums en rend 18 (Main + 16 Outs). Il faut élargir le type dans
`synthLib` ou écrire les Outs depuis l'enveloppe de `processBlock`. Latence : rien ajouté à 44,1 kHz, mais un bloc
rapporté par défaut (`getDefaultLatencyBlocks()` = 1) que le Device doit soit retarder lui-même, soit ramener à 0 ;
tous les MIDI d'un bloc arrivent avant son audio, donc le découpage à l'échantillon revient au Device. L'état sauvé
est celui du Device, pas les valeurs des paramètres : le Controller les reconstruit après chargement en demandant des
dumps, donc le Device porte le Kit, mutes, solos et Outs. 16 Tracks = 16 parts, plafond exact du Controller et du
binding ; les effets master passent en `NonPartSensitive`. Le Device ne voit pas l'état des bus : `t<n>_out` est la
bonne voie. Zoom et réglages par utilisateur existent (`scale` dans le fichier de config, `evSetGuiScale` public).

Détail : research/01-pile-tus.md
