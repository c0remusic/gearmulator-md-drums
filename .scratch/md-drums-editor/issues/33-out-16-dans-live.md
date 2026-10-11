# Out 16 hors de portée dans Live

Type: decision
Status: open
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
