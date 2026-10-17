# Ce que Live donne en glissant, et la place libre

Type: research
Status: resolved
Assignee: c0remusic (session du 2026-10-16)
Blocked by:

## Question

Ce qu'une fenêtre de plug-in reçoit dans Live 12, sous Windows et macOS, quand l'utilisateur y glisse un sample du
navigateur, un fichier de l'Explorer ou du Finder, un clip audio d'une piste, un sample de Simpler ou d'un pad de Drum
Rack ; les formats que lit le JUCE de ce dépôt ; ce que le vrai Machinedrum UW accepte (rates, mono, 12 bits, place,
boucles) ; la place libre dans le DSP voix du moteur après la banque d'usine.

## Answer

Recherche : [research/01-glisser-depuis-live.md](../research/01-glisser-depuis-live.md) (chaque fait marqué vérifié,
mesuré ou déduit, avec ses sources).

- Le navigateur de Live et l'Explorer ou le Finder arrivent au plug-in comme un glisser de fichiers du système (notes
  de version de Live 10.0.2 ; un développeur JUCE le rapporte sous macOS). Sous Windows, rien n'arrive si Live tourne
  en administrateur (UIPI). Certains samples de Packs protégés ne se lisent pas hors de Live.
- Un clip, un sample de Simpler ou un pad de Drum Rack : rien de documenté par Ableton, des échecs rapportés. S'il
  arrive un chemin, c'est le fichier source, sans la région, le warp ni le gain du clip. À mesurer : ticket 07.
- La pile : `RmlComponent` accepte tout glisser de fichiers et `RmlDrag` le donne à l'élément sous le curseur qui est
  un `juceRmlUi::DragTarget` (`canDropFiles`, `dropFiles`).
- JUCE 7.0.10 : `source/juce.cmake:84-87` coupe FLAC, Ogg, MP3 et Windows Media ; sous Windows ne restent que WAV et
  AIFF, macOS ajoute ce que lit Core Audio. `synthLib::WavReader` refuse `WAVE_FORMAT_EXTENSIBLE`.
- Le vrai UW MKII : 48 Samples ROM, 4 machines RAM, 2,5 Mo partagés ; 4 à 48 kHz (au-dessus, la moitié), mono, joué en
  12 bits ; chargé par SDS (C6).
- La banque d'usine, lue dans le flash image : 32 Samples de 11 à 44,1 kHz, de P:$150000 à $18FC12 (261 138 mots) ; le
  plus long, ROM-28, 2,40 s à 32 kHz, en boucle. ROM-33 à ROM-48 vides.
- Place libre sous $200000 : 459 758 mots, 919 516 codes de 12 bits, 20,85 s à 44,1 kHz pour tous les Samples ensemble
  (41,7 s à 22,05 kHz) ; l'OS 1.63 s'arrête vers $1FFA00 (20,78 s). L'OS ignore une boucle de 150 échantillons ou moins.
