# Section master du DSP1 exécutée seule

Type: research
Status: resolved
Assignee: c0remusic (agent de recherche)
Blocked by: —

## Question

Que faut-il pour faire tourner la section master du programme DSP1 d'OS 1.63 (`fw::Firmware::dspB`,
P:0x342-0x980) sur un DSP56300 émulé, alimentée par les sorties du `Mixer` de mdEngine (Main sec, envoi REV, envoi
DEL), sans le reste du programme ?

- Points d'entrée et de sortie, entrées par frame (X:0x180 sec, X:0x1C0 REV, X:0x600 DEL selon
  `.scratch/uc-less-engines/machinedrum-kit/09-dsp1-audio-path.md`), sorties, dépendances DMA, ESSI, HI08 ou
  interruptions, initialisation requise, mémoire externe des lignes de retard (97 020 mots d'écart).
- Comment les 32 octets du Kit deviennent Y:$150-$18c : routine de l'OS (68k) qui les écrit, et si mdEngine peut
  l'appeler comme il appelle déjà les routines machine, LFO et lissage de l'OS. Le lissage des niveaux couvre déjà
  32 octets de master (`HostModel.cpp:26-28`).
- Coût mesuré ou estimé par bloc de 32 échantillons (Machinemodule annonce ~9 740 instructions), mémoire, et latence
  ajoutée.
- Ordre des effets (Echo et Reverb sur les Sends, retour sur Main, puis EQ et Dynamix), et ce que fait le firmware
  d'une Track sur son Out (elle nourrit encore ses Sends : `Mixer.cpp:60-61`).

Sources : `.scratch/uc-less-engines/` (machinedrum-kit, machinemodule `HANDOFF.md:1429-1438`, `docs_PROTOCOL.md`),
`source/elektron/md/mdEngine/` (`tools/mdfw`, `Mixer.*`, `HostModel.*`), désassembleur du sous-module dsp56300.
Lecture seule : pas de build, pas d'exécutable firmware. Livrable : `research/03-master-dsp1.md`.

## Answer

La section master (P:$342-$970) tourne seule : du code linéaire à boucles DO, sans `jsr`, périphérique ni
interruption. Elle lit les trois bus que le `Mixer` produit déjà (sec X:$180, REV X:$1C0, DEL X:$600) et rend le Main
en Y:$000-$03F. Pour le ticket 09, un harnais sur le modèle de `VoiceEngine` suffit : un DSP56300 dont la mémoire
externe est partagée entre P, X et Y au-delà de $020000 (P = XY = $200000 mots, environ 9 Mo), l'image `dspB`
complète, une partie du boot exécutée une fois (sinus, deux effacements, trois inits d'effet), puis à chaque bloc :
écrire les bus, envoyer « go », lire Y:$000-$03F. Les 32 octets du Kit passent par le lissage $100029E, que mdEngine
appelle déjà, puis par quatre corps de code inline dans la routine de tick, un effet par tick à tour de rôle
($20B4AE, $20B604, $20B74E, $20B940). MachineRunner peut les appeler si l'on change leurs sorties en `rts` ; leurs
blocs de 9, 11, 10 et 8 mots vont en Y:$150, $17A, $170 et $185. Coût constant : environ 9 740 instructions DSP par
bloc de 32, soit 3 à 5 voix qui sonnent en permanence ; temps hôte à mesurer. Latence ajoutée nulle dans le même
`render` que le `Mixer`, 32 échantillons sur un worker décalé d'un bloc ; le Dynamix retarde de toute façon le Main
de 16 échantillons, comme la machine. Ordre vérifié : Echo sur DEL, Gate Box sur REV plus DVOL fois l'Echo, retours
sur le Main sec, puis EQ et Dynamix. Le firmware coupe les Sends d'une Track routée hors MAIN ; `dryMute` les garde,
sans rien demander au master. L'exactitude bit à bit reste à prouver contre le firmware complet.

Détail : research/03-master-dsp1.md
