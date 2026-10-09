# Voix en parallèle : mesure de rentabilité

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 24

## Question

`ParallelVoiceEngine` (mdEngine) répartit les 16 voix du DSP2 sur des threads ; MD Drums ne l'utilise pas. Le ticket 09
demande de mesurer si c'est rentable, une fois le master construit.

- Mesurer 1, 2 et 4 groupes de voix, 16 Tracks jouées et master actif, sur la machine de référence (Ryzen 7 3700X,
  8 cœurs / 16 threads) : temps sur le thread audio rapporté au temps réel, CPU total de tous les threads, gigue du
  temps de rendu par bloc (pires blocs compris), latence note→son.
- Vérifier que le son reste identique (bit-exact) à celui d'un seul groupe.
- Activer seulement si le temps sur le thread audio baisse nettement sans que la gigue ou la latence se dégradent ; sinon
  noter les chiffres et laisser le rendu sur un seul thread.

Fini quand : chiffres des trois configurations notés ici ; décision prise et appliquée ; garde-fou vert ; poussé sur
`main`.

## Answer

Mesuré le 2026-10-09 sur la machine de référence (Ryzen 7 3700X), `mdDrumsParallelTest` (mesure, hors garde-fou) :
16 Tracks TRX et EFM frappées tour à tour, si bien qu'après les 16 premiers Hits chaque voix rend chaque bloc, master
compris ; mêmes blocs et mêmes Hits pour chaque configuration ; deux tours, 10 s puis 20 s d'audio.

| Configuration | Thread appelant (temps réel) | CPU du processus (un cœur) | Main contre un seul DSP |
|---|---|---|---|
| Un DSP de voix (aujourd'hui) | 19,6-19,9 % | 19,5-19,7 % | référence |
| `ParallelVoiceEngine`, 1 groupe | 20,6-20,9 % | 20,0-20,5 % | identique |
| 2 groupes | 21,4-21,7 % | 31,4-31,6 % | 1 248 431 échantillons sur 1 763 968 diffèrent (20 s) |
| 4 groupes | 16,7-17,5 % | 36,6-41,8 % | idem |

- **Pires blocs** : le p99 par bloc de 32 reste entre 261 et 304 µs partout ; les maxima (435 µs à 2,7 ms) et le pire
  bloc hôte de 256 échantillons (27 à 87 % de son temps) changent d'un tour à l'autre dans toutes les configurations,
  le processus n'étant pas en priorité temps réel : ils mesurent l'ordonnanceur de Windows, pas le moteur. Un seul tour
  a vu 2,7 ms, à 4 groupes.
- **Latence** : inchangée par construction, le rendu reste synchrone dans le bloc.
- **Pourquoi si peu** : chaque groupe exécute tout le programme du DSP de voix pour son bloc (les voix inactives coûtent
  peu, pas rien) et les effets de ses Tracks, puis le thread appelant attend le plus lent ; le reste du bloc (tick de
  l'OS, mixage, master) reste en série. À 2 groupes, le réveil et l'attente mangent probablement le gain.
- **Main** : à partir de 2 groupes, les voix d'un même DSP ne partagent plus son état (le firmware a ses voix qui se
  poussent l'une l'autre, `mdEngineFirmwareTest`) ; probablement la cause, non tracée.

**Décision** : pas rentable, non activé. Le critère était une baisse nette du temps sur le thread audio sans dégrader
la gigue ni la latence : au mieux −2,5 points à 4 groupes, pour un CPU total presque doublé, trois threads à réveiller
tous les 0,73 ms dans un hôte chargé, et un Main qui n'est plus celui du firmware. MD Drums garde un seul DSP de voix,
20 % du temps réel, sous le plafond de 30 %.
