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
  écrasée ?) et des 608 valeurs publiées au chargement d'un Kit depuis l'éditeur (undo, automation).
