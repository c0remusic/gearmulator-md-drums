# Test hôte VST3 : le binaire que Live charge

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-10)
Blocked by: 31

## Question

Hors carte, demandé après le ticket 31 : en attendant la validation dans Live, charger le vrai binaire
`MD Drums.vst3` par l'hébergement VST3 de JUCE, comme un hôte, et vérifier la couche que `mdDrumsProcessorTest` ne
voit pas (le wrapper VST3 entre le processeur et l'hôte).

Fini quand : `mdDrumsVst3HostTest` au garde-fou, constats corrigés ou portés à la liste de Live, poussé.

## Answer

`mdDrumsVst3HostTest` (au garde-fou, sa cible construit `mdDrumsPlugin_VST3`) charge le bundle avec un dossier de
données à lui (`GEARMULATOR_DATA_ROOT`), jamais celui de l'utilisateur. Mesuré le 2026-10-10 sur ce PC :

- **Identité** : « MD Drums » de c0remusic, instrument, hash de classe `beaa512f` figé (Live retrouve le plug-in d'un
  set par lui).
- **Paramètres** : 2705 vus par l'hôte, 609 automatisables, les 608 et Bypass ; les 2080 « MIDI CC » et 16 « MIDI PC »
  de l'émulation MIDI de JUCE n'ont pas `kCanAutomate`, Live ne les liste pas. Les 608 IDs VST3 sont les hashes des
  IDs figés `page_part_index`, recalculés depuis le JSON.
- **Bus** : Main stéréo actif, Out 01-16 mono éteints par défaut ; les 16 pris, chaque Track sur le canal de son Out ;
  Out 16 seul, son canal suit Main ; une Track sur un Out que l'hôte n'a pas pris ne sonne nulle part.
- **Latence** : 51 échantillons à 44,1 kHz, 88 à 48 kHz (rééchantillonneur), 51 au retour.
- **Blocs** : mêmes échantillons en blocs de 128 en temps réel et en tailles aléatoires 1-128 hors temps réel, 6 s.
- **Ce que le plug-in dit à l'hôte** : au chargement, les 32 relais (Track montrée) ; pendant une automation de SYN1,
  Level et Echo TIME, rien d'autre que le relais de SYN1 ; à la restauration d'un état, rien. Exception voulue
  (ticket 10) : changer la machine d'une Track renvoie ses SYN1-8, sans geste.
- **Éditeur** par IPlugView : 1296×824, ouvert en 313 ms, dessiné dans la fenêtre de l'hôte (1639 couleurs lues par
  `PrintWindow`), un thread audio jouant pendant que l'hôte automatise SYN1.
- **Trouvé** : le premier bloc après le chargement coûtait 27 ms, une coupure à l'insertion de MD Drums pendant la
  lecture. Cause : le JIT du DSP master compile tout le code de la section master au premier bloc (26 ms sur les 27,
  `mdDrumsFirstHitTest`). **Corrigé** par `MasterEngine::warmUp` à la construction du moteur du Device : 64 blocs
  (bruit fort sur les trois bus puis silence), puis mémoire et registres du DSP remis, comme pour les voix (ticket 31).
  33 ms de plus au chargement. Premier bloc 26,77 → 0,25 ms ; Main identique (0 échantillon sur 370 816, 282 578 non
  muets) ; les 32 paramètres master parcourus, aucun bloc master au-delà de 0,12 ms. Par le VST3 : pire bloc 1,58 ms,
  aucun sur 2068 au-delà de 2,9 ms, ni sur 1038 éditeur ouvert. Le test échoue au-delà de 10 ms. Latence inchangée.
- **Reste pour Live** : ce que Live fait des SYN1-8 renvoyés quand l'hôte change une machine (automation de SYN
  écrasée ?) et des 608 valeurs publiées au chargement d'un Kit depuis l'éditeur (undo, automation). Réponse au
  ticket 34 : Live les enregistrait ; elles passent désormais par une relecture.

## Passage dans Live par script (2026-10-10, soir)

Piloté par le pont LOM `TupleMCP` (Remote Script de l'utilisateur, NDJSON sur 127.0.0.1:39041), Live 12.2.1, sur des
pistes « ZZ test » supprimées ensuite.

- **Incident** : un preset de rack `.adg` fabriqué à la main (pour configurer les paramètres de MD Drums) a fait planter
  Live au chargement, violation d'accès dans le code de Live (`Ableton Live 12 Suite.exe+0x3a553b7`, lecture à
  l'adresse 0). Set ouvert non sauvé ; récupération refusée ensuite par l'utilisateur. Règle : ne plus charger de
  preset Live écrit à la main.
- **Live ne montre que les paramètres configurés** (plus de 64 paramètres : panneau vide) : sans liste Push préparée,
  le script n'atteint ni Machine, ni SYN, ni Out. Au chargement, les relais créent une entrée temporaire « LFOM ».
- **Mesuré** : latence vue par Live 51 échantillons ; chargement 0,94 s, duplication de piste ~1 s ; insertion pendant
  la lecture sans perte de niveau sur la piste qui joue (pic CPU de Live 57 %) ; 8 instances sur le motif chargé : CPU
  de Live 45 % en moyenne, pics 50-59 %, les 8 sonnent ; journal de Live sans erreur pour MD Drums (lignes « not
  implemented » d'info, comme les UADx).
- **Out 16 hors de portée** : Live offre « Out 01-MD Drums » à « Out 15-MD Drums ». Live ouvre au plus 16 bus de sortie
  par plug-in, celui de la piste compris ; MD Drums en déclare 17. Ticket 33.
- **Locks** (signalés par la session md-drums-seq) : rejoués par `mdDrumsVst3HostTest` avec les notes du séquenceur
  (note 81 vélocité 1 sur le canal de la Track, au même échantillon que le Hit, Lock d'abord, relâchés une double
  croche après) : VOL à 0 rend le Hit muet, le Hit suivant rejoue le Kit, rien n'est signalé à l'hôte. Le Lock sans
  effet vu dans Live vient d'un canal perdu avant MD Drums : la session md-drums-seq a mesuré que Live donne le canal 1
  aux notes d'un device Max for Live ; le temps 1 muet venait de md-drums-seq.
- **ADR 0003 amendée** (option B, choisie par l'utilisateur le 2026-10-11, transmise par la session md-drums-seq) : une
  Lock sur le canal 1 va avec la note de Hit reçue juste avant au même échantillon, quelle que soit sa Track ; sans
  elle, Track 1 comme avant ; canaux 2-16 inchangés. `Device::apply` ; vérifié par `mdDrumsDeviceTest` (Lock canal 1
  après le Hit de Track 3 = Lock sur le canal 3 ; un échantillon plus tard, perdue ; deux Tracks au même échantillon)
  et `mdDrumsVst3HostTest` (motif de md-drums-seq, tout sur le canal 1, Hit d'abord).
- **Live tient la DLL** : Live ne décharge pas un plug-in retiré du set ; tant qu'il tourne, `mdDrumsPlugin_VST3` ne se
  lie pas (LNK1104), donc le garde-fou non plus.
- **Reste dans Live** : automation des SYN quand une machine change (exige la liste Push dans le set), undo après Kit,
  Ctrl+S, Push 2, fluidité.
