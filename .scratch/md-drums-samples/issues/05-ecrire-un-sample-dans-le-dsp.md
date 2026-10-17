# Écrire un Sample dans le DSP voix en marche

Type: task
Status:
Blocked by:

## Question

Mesurer ce que coûte de mettre un Sample dans le DSP voix d'un moteur qui joue, pour décider comment le faire : écrire
le répertoire et les données (jusqu'à 459 758 mots) entre deux blocs sur le thread audio, ou les préparer ailleurs et
ne faire sur le thread audio qu'un échange court ; ce que fait le JIT du dsp56300 d'une écriture en mémoire P hors du
code ; une voix qui joue le Sample remplacé pendant l'écriture ; et le moteur de la prévisualisation du Hit, qui doit
avoir les mêmes Samples. AFK : un test qui mesure, sans rien construire dans le plug-in.
