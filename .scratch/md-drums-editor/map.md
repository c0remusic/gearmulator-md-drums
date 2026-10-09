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
  (`mdDrumsEngineTest` : depuis le ticket 19, 1,52 ms sans gigue sur TRX-BD à 44,1 kHz, 35 échantillons déclarés ;
  depuis le ticket 24, Main 16 échantillons de plus, Dynamix, 51 déclarés ;
  avant, 1,16 ms en moyenne et 0,68 ms de gigue ; les 0,48 ms de départ comptaient depuis le bloc de 32 de la note).
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
- [Bibliothèque mdProtocol et writer de dump de kit](issues/13-bibliotheque-mdprotocol.md) : codecs sortis de `mdLib`
  par `git mv` ; `MdKit` garde chaque octet du dump, `mdKitDump` écrit la version 04 ; 32 Kits d'usine aller-retour
  champ pour champ, dumps du firmware octet pour octet, Kit écrit accepté et renvoyé tel quel par le firmware ;
  `$65`/`$66` avec OFF = $7f vérifiés sur firmware.
- [Links et Chokes dans mdEngine](issues/15-links-chokes-mdengine.md) : joués comme l'OS (Link à un niveau, Choke
  au même tick ou au suivant, Track étouffée refrappée muette jusqu'au tick suivant, Mute qui perd les Hits, level
  recalé à chaque Hit) ; identiques au firmware sauf deux instants fixés par son UC (coupe un bloc plus tard, tick
  suivant à 11 blocs contre 3 à 11) ; latence inchangée, 1,16 ms en moyenne sur TRX-BD mesurée depuis la note.
- [Port du processor MD Drums sur la pile TUS](issues/07-port-pile-tus.md) : `mdDrums::Device` parle le MIDI de la MD
  et démarre sur le Kit 1 d'usine ; Controller à slots atomiques vidés au bloc audio suivant ; état = dump `$52` +
  masques, relu par le Controller sans toucher le Device ; **576 paramètres, pas 592** (34 par Track : le contrat en
  comptait 35), IDs figés ; skin minimal généré ; un Kit chargé recopie ses blocs LFO de 36 octets dans le LFO
  courant, mot $0000029a compris (effet sur le LFO RND figé non mesuré) ; latence identique à 44,1 kHz (1,16 ms),
  +8,6 échantillons à 48 kHz (resampler Legacy, désormais rapporté).
- [Fabrication du skin v22](issues/08-fabrication-skin.md) : RML et RCSS écrits à la main (classes de grille,
  décalages relatifs), JSON des paramètres aussi ; comportement en vues C++ par ID ; knob `vknob` sous-classe
  d'`ElemKnob` dessinée sur `ElemCanvas` ; Inter 4.1 en 4 TTF à chiffres tabulaires gelés ; Size 75-150 % sur la clé
  `scale` de TUS ; rendu à la demande à la fréquence de l'écran, cible 2 frames entrée→pixel en GL3, 33 ms au
  garde-fou en logiciel ; audit des boîtes contre celles exportées de la maquette + règles du HANDOFF ; skin statique
  complet (onglets, Kits, overlays) en deux tickets, 17 puis 18, le minimal retiré au 18.
- [Socle du skin v22 et onglet Track](issues/17-socle-skin-onglet-track.md) : skin v22 par défaut ; Inter en 4 TTF
  (chiffres seuls gelés tabulaires, `tnum` élargissant aussi tiret et espace ; table `kern` recopiée du GPOS) ;
  `<vknob>` et `<vglyph>` sur canvas ; 213 boîtes identiques à la maquette, 159 baselines sur le pas de 4 (ligne de
  22 px pour un texte de 14 px sur 24) ; 464 paramètres de l'onglet atteignables ; entrée→pixel en logiciel 4-8 ms
  pour un drag, 7-10 ms pour un changement de Track ; GL3 et CPU dans Live à mesurer. Après lui, la zone d'édition
  part de x 240 (16 px de chaque côté des knobs, choix de l'utilisateur).
- [Mix, Master, Kits et overlays](issues/18-mix-master-kits-overlays.md) : skin v22 complet et statique, minimal et
  générateur retirés ; fader = `vknob` en variante ; browser et menu LFO en boutons radio liés à Machine, LfoTrack,
  LfoParam ; 881 boîtes de six vues identiques à la maquette, 685 baselines sur le pas de 4, 576 paramètres sur 576
  atteignables ; clic 13-17 ms entrée→pixel en logiciel.
- [Gigue note→son à zéro](issues/19-gigue-note-son.md) : retard par Track sur ses contributions au mix (gains, sends,
  sortie solo), 31 moins l'attente du bloc ; somme modulo 56 bits comme le DSP, identique au mixeur du moteur ; Link
  au même retard, Choke coupé au retard du Hit ; TRX-BD 37-67 → 67-67 échantillons à 44,1 kHz (gigue 0), 53-87 →
  86-87 à 48 kHz ; latence déclarée 21 → 35 ; CPU inchangé.
- [Lecture du moteur par l'éditeur](issues/16-lecture-moteur-editeur.md) : `Telemetry` possédée par le Processor,
  écrite par le Device sans verrou ; Controller vérité du Kit, MIDI out pour SYN1-8 seulement ; mètres des 18 sorties
  du Device (chute 20 dB/s, −60 à 0 dBFS), scopes tenus par l'éditeur ; écran Hit en capture live progressive (min/max
  de 8 échantillons, échelle fixe ±0,5) ; LFO sans valeur live ; ≤ 1 % d'un cœur côté audio ; tickets 20, 21, 22.
- [Télémétrie, mètres et scopes](issues/20-telemetrie-metres-scopes.md) : `Telemetry` partagée Processor-Device
  (crêtes, Hits) ; `MeterView` sur canvas à la fréquence de l'écran ; passe de crête 0,027 % d'un cœur, Device 20,01 %
  contre 19,92 % ; mètres, chute, lumières et scopes vérifiés au pixel.
- [Écran Hit](issues/21-ecran-hit.md) : capture à deux emplacements par Track, contrôlée par identifiant ; identique à
  la sortie solo depuis la note plus 31 ; `HitView` progressif avec fenêtre ajustée et tête de lecture ; 16 captures
  à la fois 0,23 % d'un cœur.
- [Écrans Filter/EQ et LFO, ASSIGN, écoute](issues/22-ecrans-filtre-lfo-assign.md) : réponse depuis les mots de
  coefficients du DSP (pas d'impulsion : le passe-haut bas sonne des secondes), à 0,054 dB de la chaîne, 46 µs la
  courbe ; LFO porté des routines de l'OS, 0 différence sur 2 259 ticks et 670 Hits ; ASSIGN par anneau sur les knobs,
  Esc pour ASSIGN et les overlays ; noms soulignés ; écoute ordonnée après la machine choisie (`Controller::audition`).
- [Jouer la Track, vélocité, pas à pas sur la machine](issues/23-jouer-et-choisir.md) : touche play à l'appui et
  Espace à la vélocité de la bande (drag 1,5 px par pas, gardée dans les réglages) ; molette et flèches sur le nom de
  machine, flèches et Enter dans le browser, dans l'ordre de ses cellules. Ajouté après le ticket 22 : déjà spécifié
  par le HANDOFF, sans ticket.
- [Effets master dans le moteur](issues/09-effets-master-moteur.md) : second DSP56300 qui ne joue que P:$342-$970,
  synchrone dans `EngineT::render` après le mixeur ; coefficients par les quatre corps de l'OS dans `MachineRunner`
  (sorties en `rts`) ; Main 16 échantillons après les Outs, latence déclarée 35 → 51 ; plafond 30 % du temps réel sur
  le thread audio dans les tests, validé dans Live ; bit-exact contre le firmware à paramètres fixes ;
  `setMaster(0-31, 0-127)` sans bypass ; voix en parallèle mesurées à part (ticket 25).
- [Effets master dans mdEngine et MD Drums](issues/24-effets-master.md) : `MasterEngine` (boot partiel du programme
  DSP1, stub HI08) ; mots de l'OS par ses corps dans `MachineRunner`, identiques au firmware ; Main échantillon pour
  échantillon le firmware sur 7 scénarios depuis l'état de son DSP1, insensible aux registres et au scratch ; latence
  déclarée 51, Main 85 contre Out 67 à 44,1 kHz ; 16 Tracks et master 20 % d'un cœur ; traîne de quelques LSB sur Main
  comme la machine, silence pris sous −100 dBFS.
- [Voix en parallèle : mesure de rentabilité](issues/25-voix-en-parallele.md) : `ParallelVoiceEngine` pas rentable,
  non activé ; thread audio 19,6-19,9 % aujourd'hui, 21,4-21,7 % à 2 groupes, 16,7-17,5 % à 4 pour un CPU total de
  36,6-41,8 % ; Main différent dès 2 groupes ; mesure gardée (`mdDrumsParallelTest`, hors garde-fou).

## Not yet specified

- **Tranches de construction après le skin** : skin statique (17, 18), gigue (19), lecture du moteur (16), télémétrie
  (20), écran Hit (21), écrans Filter/EQ et LFO, ASSIGN, écoute (22), jeu et pas à pas (23), effets master (24) et
  mesure des voix en parallèle (25) faits ; restent les Kits et leur Bank, Ctrl+S compris (après le ticket 10), Push
  (après le ticket 11) et le second moteur de l'écran Hit et de l'aperçu du browser (ticket 12).
- **Édition des Links et Chokes dans l'éditeur** : absente du HANDOFF ; valeurs du Kit envoyées en `$65`/`$66`, sans
  paramètre hôte (ticket 06). À placer avec la tranche Track.
- **LFO RND figé dans mdEngine** (lu, pas exécuté, ticket 05) : ses deux mots d'état ne sont jamais semés, d'où un
  décalage constant. Le chargement du bloc LFO de 36 octets d'un Kit le corrige (tickets 04 et 07, où le Device
  applique le dump `$52`) ; sinon une graine.
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
