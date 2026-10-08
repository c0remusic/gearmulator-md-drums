# Map : éditeur de MD Drums

Label: wayfinder:map

## Destination

MD Drums livré avec l'éditeur v22 complet (Track, Mix, Master, Kits) sur la pile TUS, effets master audibles, mappé
pour Push 2. Fini quand : garde-fou vert avec les nouveaux tests, poussé sur `main`, installé dans
`C:\Program Files\Common Files\VST3\` et joué par l'utilisateur dans Live, latence note→son et fluidité entrée→pixel
mesurées avant et après. Pas de tag.

## Notes

- **Exécution dans la carte** (dérogation à « plan, don't do », décidée au cadrage du 2026-10-08) : les tickets
  `task` construisent. Un ticket par session, sauf la recherche.
- Domaine : `CONTEXT.md` (glossaire : Track, Machine, Hit, Link, Choke, Kit, Bank, Slot, Main, Out, Mute, Solo,
  Send, Master effects ; MD Drums ≠ Gearmulator MD). Design approuvé :
  `source/elektron/md/mdDrumsPlugin/Design/HANDOFF.md` (v22). Pile : `docs/adr/0001-md-drums-on-the-tus-stack.md` ;
  Device : `docs/adr/0002-md-drums-device-speaks-machinedrum-midi.md`.
- Skills : `grilling` + `domain-modeling` par défaut ; `codebase-design` pour les tickets de design ; `tdd` ou
  `implement` pour les tickets `task` ; `research` pour la recherche.
- Garde-fou du `CLAUDE.md` avant chaque commit et push ; chaque tranche ajoute ses tests au garde-fou. Push sans
  demander une fois vert. Jamais de tag.
- **Latence** : tout changement du chemin de rendu (port, master) rapporte latence note→son et gigue, avant et après
  (0,48 ms mesurée sur le moteur aujourd'hui).
- **Fluidité** : chaque clic et drag instantané, mesuré entrée→pixel ; jamais `Plugin::withDeviceLocked` par frame
  ou par clic.
- Recherches : un doc par ticket dans `research/` à côté de cette carte (pas de branche `research/*`).
- Contraintes établies : `mdLib` ne peut pas être lié à MD Drums (Musashi : une classe CPU par exécutable) ; la pile
  TUS génère les IDs JUCE en `page_part_index` ; aucun set Live MD Drums à préserver, IDs figés dès le premier push
  du port.
- **Cadrage (2026-10-08)** :
  - pile TUS complète (ADR 0001) ;
  - effets master par émulation du code DSP1 de la ROM, pas de réécriture ;
  - écran hit : capture live certaine, second moteur derrière une décision de coût ;
  - zoom 75-150 %, mémorisé par utilisateur ;
  - Bank : un seul fichier, 64 Slots, dans le dossier data du plug-in ;
  - Push 2, en mode contrôleur ;
  - ordre : recherches en parallèle, contrat du Device, port, nouveaux paramètres, Track, Mix, Kits, Master, hit,
    Push, installation et mesures. Revu au ticket 06 : `mdProtocol`, `synthLib` à 18 et Links/Chokes avant le port,
    qui emporte les nouveaux paramètres.

## Decisions so far

<!-- une ligne par ticket résolu : [titre](issues/NN-slug.md) : gist -->

- [Pile TUS pour un moteur sans firmware](issues/01-pile-tus-moteur-sans-firmware.md) : un Device sans DSP56300
  tient (modèle JE-8086) ; obstacle, 12 sorties au plus dans `synthLib` pour 18 rendues ; latence par défaut d'un
  bloc à ramener à 0 ; l'état est celui du Device ; 16 Tracks = 16 parts ; garder `juce_add_plugin` pour l'identité.
- [Réponses filtre, EQ et formes LFO pour les écrans](issues/05-reponses-ecrans.md) : filtre et EQ vrais par un
  `TrackFx` privé sur impulsion (~0,5 ms, accès aux tables à ouvrir) ; LFO par portage des six routines de l'OS,
  vérifié bit-exact ; la maquette se trompe sur les formes et sur l'amplitude (±126, pas ±64).
- [Kits d'usine et octets non lus du dump de kit](issues/04-kits-usine-dump.md) : Kit 1 vient de l'image d'usine de
  la mémoire patch dans le fichier OS (section 4), avec 16 Kits d'usine pour amorcer la Bank ; un dump version 4 avec
  valeurs sûres connues pour chaque octet ignoré ; fichier Bank = 64 dumps concaténés (78 912 octets, sans en-tête).
- [Push 2 face aux paramètres d'un VST3 dans Live](issues/02-push2-parametres-vst3.md) : un plug-in ne choisit ni
  banques, ni ordre, ni noms sur Push ; au-delà de 64 paramètres Live n'en montre aucun sans « Configure » (128 au
  plus) ; leviers : liste prête (Rack et Macros), paramètres relais, titres courts sans chiffres dans les valeurs,
  IDs stables ; pads 36-51 seulement dans un Drum Rack.
- [Section master du DSP1 exécutée seule](issues/03-section-master-dsp1.md) : P:$342-$970 tourne seule (aucun
  appel, aucun périphérique), entrées = buffers du `Mixer`, sortie Main en Y:$000 ; mémoire externe partagée
  (~9 Mo), init partielle du boot ; 32 octets du Kit vers Y:$150-$18C décodés via 4 routines de l'OS appelables ;
  ~9 740 instructions par bloc ; Dynamix retarde Main de 16 échantillons comme la machine. Lu, pas exécuté.
- [Contrat du Device MD Drums](issues/06-contrat-device.md) : le Device parle le MIDI de la Machinedrum, étendu de
  SysEx privée pour Solo, Out et tempo (ADR 0002) ; notes seules depuis l'hôte ; synchrone, 0 bloc, rien n'alloue ;
  `synthLib` à 18 sorties ; resampler de la pile (Legacy, +0,67 ms hors 44,1 kHz) ; 592 paramètres figés au premier
  push, pages 0-4 = CC de la MD ; état = dump `$52` + SysEx privée ; codecs dans `mdProtocol` ; Links et Chokes joués
  comme l'OS ; Mute et Solo perdent les Hits.
- [synthLib à 18 sorties](issues/14-synthlib-18-sorties.md) : `synthLib::MaxAudioOutputs` = 18 ; les 18 canaux
  sortent alignés à la latence rapportée dans les trois modes ; Legacy confirmé à 0,67 ms à 48 kHz et gardé par
  défaut (5,8 % d'un cœur à 48 kHz pour 18 canaux, contre 27,9 % en MameHq).

## Not yet specified

- **Tranches de construction après le port** : onglet Track, Mix, Kits, Master, écran hit, Push (les nouveaux
  paramètres entrent dans le port, ticket 06). Chacune devient un ticket `task` quand la fabrication du skin (08) et
  la lecture du moteur par l'éditeur (16) ont fixé leur forme.
- **Édition des Links et Chokes dans l'éditeur** : absente du HANDOFF ; valeurs du Kit envoyées en `$65`/`$66`, sans
  paramètre hôte (ticket 06). À placer avec la tranche Track.
- **Écran LFO à corriger dans le HANDOFF** (d'après le ticket 05) : amplitude réelle ±126 à LFOD 127, scie qui
  descend deux fois par période, RMP et EXP en one-shot relancés à chaque Hit, RND à 8 pas, LFOM qui fond vers la
  forme 2 inversée. À reprendre avec la tranche Track.
- **LFO RND figé dans mdEngine** (lu, pas exécuté, ticket 05) : ses deux mots d'état ne sont jamais semés, d'où un
  décalage constant. Le chargement du bloc LFO de 36 octets d'un Kit le corrige (tickets 04 et 07, où le Device
  applique le dump `$52`) ; sinon une graine.
- **Rendu RmlUi** (logiciel ou GL) et banc de mesure entrée→pixel.
- **Tranche Kits** : writer et format de la Bank fixés par le ticket 04 ; reste la forme du chargement face à l'hôte
  (ticket 10) avant d'en faire un ticket `task`.
- **Installation et validation dans Live** : mesures finales (latence, fluidité, CPU avec le master), liste des tests
  ajoutés au garde-fou.

## Out of scope

- Gearmulator MD (`mdJucePlugin`) : reste chez md-mm, intact.
- Tag et release publique.
- Migration de sets Live enregistrés avec MD Drums : aucun set à garder (cadrage).
- Conflit de la touche Espace avec le transport de Live : jugé sans importance (cadrage).
- Push 3 autonome : ne charge pas de VST3.
