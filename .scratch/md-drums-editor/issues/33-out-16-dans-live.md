# Out 16 hors de portée dans Live

Type: decision
Status: resolved
Assignee: c0remusic (session du 2026-10-11)
Blocked by: 32

## Question

Live ouvre au plus 16 bus de sortie par plug-in, celui de la piste compris (constaté le 2026-10-10 : « Out 01 » à
« Out 15 » seulement ; même plafond rapporté sur le forum d'Ableton et sur celui de JUCE). MD Drums déclare Main puis
Out 01 à Out 16, 17 bus : la Track 16 ne peut pas sortir seule dans Live. Le paramètre Out de chaque Track est un
booléen (Main ou son propre Out).

Options :

- **A. Out devient un choix** : Main, ou l'un de 15 Outs, n'importe quelle Track sur n'importe quel Out, comme les
  sorties individuelles assignables de la Machinedrum. 16 bus, tous atteignables dans Live. ID du paramètre inchangé,
  plage 0-1 vers 0-15 (aucun set à garder, cadrage) ; contrôle de l'éditeur en menu ; relais « Out » de Push 2 suit.
- **B. Garder 16 Outs** : la Track 16 reste sur Main dans Live ; les autres hôtes ont les 16. Documenter.
- **C. Outs stéréo par paires** (Tracks 1-2 sur Out 01/02, etc.) : 9 bus, mais une piste audio de Live reçoit deux
  Tracks, à séparer avec Utility.

Recommandation : A.

## Answer

A, choisi par l'utilisateur le 2026-10-11, construit le jour même.

- **Paramètre** : Out vaut 0 (Main) ou 1-15 (Out 01-15), même ID `7_t_1` ; le relais FocusOut de Push 2 suit.
- **Device** : rend la sortie propre de chaque Track dans un tampon, que la Télémétrie et les captures lisent comme
  avant ; chaque Out est la somme des Tracks qui le nomment ; une Track hors de Main ne quitte que Main.
- **Messages** : Out porte le numéro d'Out ; le message Mixer porte un octet par Track. L'ancienne forme (masque) se
  lit encore : Track n sur Out n, 16 sur 15.
- **Hôte** : Main puis Out 01-15, 16 bus, tous atteignables dans Live.
- **Éditeur** : la case Out d'une tranche de Mix montre l'Out, allumée hors de Main. Un clic bascule entre Main et
  l'Out naturel de la Track (son numéro, 16 vers 15), la molette parcourt Main, Out 01-15. La bande de tête dit
  « Plays on Out nn ». Mise à jour au clic même, pas à la boucle de messages.
- **Vérifié** : `mdDrumsDeviceTest` (Main, Out 01, Out 09, Track 16 sur Out 15, deux Tracks sur un Out, latence
  inchangée : Out 1,52 ms, Main 1,93 ms) ; `mdDrumsVst3HostTest` (16 bus, chaque Track sur son Out, Track 16 sur
  Out 15, Out 15 seul) ; `mdDrumsSkinTest` (clic, molette, textes).
