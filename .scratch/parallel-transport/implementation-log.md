# Implémentation du transport parallèle — journal de reprise

Branche : `release/md-mm-alpha`, où `feat/transport-step0-dating` est
fusionnée. Submodule `source/dsp56300` : fork `c0remusic/dsp56300-md-mm`,
branche `feat/dma-de-observer`, `.gitmodules` pointe sur le fork (commit
épinglé : `git ls-tree HEAD source/dsp56300`).

Spec de référence : `docs/design/parallel-transport-spec.md`.

## État par marche

| Marche | Commit | État |
|---|---|---|
| 0a visibilité mémoire | `7dcb5521` | vert |
| 0b TimedLinkRing | `2b2dd28a` | vert |
| 0c disposal daté au pop | `21470b0d` | vert |
| 1 D=1 frame en série | `2e25a78b` | vert (D=40 casse mmSine : datation prouvée) |
| 2a miroirs/seams/SPSC | `ee006203` | vert |
| 2b datation HI08 MD + garde inline | `d5a4ccad` | vert |
| 2c worker DSP2 (`MDMM_TRANSPORT=parallel`) | `520ab7a8`…HEAD | expérimental, opt-in ; reproducteur ADC vert, perf < série |
| 3 worker paire (`MDMM_TRANSPORT=pair`) | `417faab5`…HEAD | défaut MM quand le transport parallèle est actif (`Device::preferredTransport`) ; banc MM paire 70,5 % du temps réel (2026-09-30) |

Le mode série (défaut) = 94 % realtime (pluginTester 30 s), identique à la
baseline du ticket 01. Gates série vertes à chaque commit.

## Mode parallel : où on en est

Règles de gates actuelles (`Hardware::producerTargetCycles`) :
- producteur ≤ position UC publiée (conversion rationnelle exacte
  `hostToDspDeadline`, ZÉRO lead — un mot hôte appliqué en retard dans une
  attente masquée du firmware n'est jamais acquitté → tempête de retries) ;
- producteur ≤ cible de bloc + quantum ;
- allowance hôte **seulement pendant que l'UC est bloqué** sur flux plein
  (`setHostWriteBlocked`, temps UC figé) : tête de flux = `drainStart + clamp`,
  `drainStart = max(échéance, pose précédente)` — enveloppe exacte de
  `writeWordToDsp` série. Hors blocage, aucune avance sur l'UC ;
- pose des mots hôte : au bord de lecture HRX (`setReadRxCallback` →
  `landHostToDspWordOnRead`) et chunk worker borné à l'échéance de tête ;
- mixer ≤ UC + D sur le thread audio (`schedStep`, `waitTransportImpl`),
  **strictement** (plus de `max(cap, pos + 1)`) ; aucune gate
  producteur↔mixer (cycle sinon) ;
- position UC publiée **pendant** la tranche UC (toutes les 32 instructions,
  publié ≤ réel) : le producteur court en même temps que l'UC au lieu de
  démarrer chaque tranche après elle ;
- attente lien du mixer (`linkRxAvailable`) bornée à la gate exacte du
  producteur (`hostToDspDeadline(1, UC)`) : toujours satisfaisable.

Reproducteur : `mdParallelTransportFirmwareTest` (vrai Processor+Controller,
40 s série puis 40 s parallel, garde 15 s/bloc, assertions ADC). ~2 min.
**Vert** depuis le correctif du 2026-09-23 (underflow/overflow 0/0).

Défauts 1-3 (ADC) : une seule cause racine, fermée.
- Vers 12,5 s machine, l'UC pousse une rafale de ~265-270k mots vers DSP2
  (même rafale en série), à ~66 cycles DSP/mot.
- L'ancienne allowance cumulative (`+clamp` depuis la position du producteur
  à chaque push) s'auto-entretenait tant que le flux n'était pas vide : le
  producteur prenait 16-20M cycles (~8000 frames) d'avance sur UC et mixer.
- Devant le mixer, le producteur attend sa synchro (lien, Port C) et consomme
  ~110-125 cycles/mot au lieu de 69 → avance qui s'auto-alimente.
- Conséquences : underflow ADC producteur (lit des frames non encore
  poussées) ; puis l'UC voit l'état DSP2 venu du futur, et **cesse de parler
  aux deux DSP** (compteurs de mots figés) → mixer n'horloge plus son ESSI1 →
  overflow ADC mixer. Le défaut 1 n'était pas un bug de file.
- Réfuté en chemin : compter le flux dans TXDE (sans effet, retiré — la série
  montre toujours TXDE=1 après drain) ; la granularité de pose seule (pas
  suffisante).
- Diagnostics gardés sous `MDMM_TRANSPORT_TRACE` : lignes watchdog `adc0/adc1`
  (calls/hits/behind/ahead, profondeur, overflow/underflow) et ligne
  `[worker]   stream` (profondeur, pushed/landed read/chunk/overdue).

Défaut 4 (perf) : parallèle désormais plus rapide que série, cible pas
atteinte. Indicateur : ratio mur parallèle/série **dans le même run** (le
bruit machine fait varier la série de 93 à 139 % d'un run à l'autre).
HEAD `5fc0a764` ~1,08-1,10 → 0,87-0,94 non épinglé, 0,75 threads sur un
même CCX. Cible spec étape 2 ≈ 0,62. Causes trouvées et corrigées :
- **Attente lien cachée** dans les tranches mixer (`m_signal.waitFor` direct,
  hors `waitTransportImpl`, comptée comme temps mixer) : le producteur ne
  pouvait avancer qu'après la publication UC de fin de tranche, le mixer
  (plafonné UC + D) attendait qu'il traverse toute la tranche. Fix :
  publication UC en cours de tranche.
- **Quasi-deadlock lien** : `max(mixerCap, dsp1Pos + 1.0)` laissait le mixer
  dépasser UC + D ; il réclamait un producteur au-delà de l'UC, que l'UC (même
  thread, bloqué) ne pouvait jamais ouvrir → timeout 5 ms, 76-570 fois par
  run (jusqu'à ~2,9 s de mur). Fix : plafond strict + besoin borné à la gate.
- **Faux partage à ~1 M/s** : grappe d'état lien après `m_linkRing`
  (`m_linkRxWasEnabled[2]` stocké inconditionnellement à chaque tick RX
  96 cycles par les deux threads, `m_linkLastShallow`, `m_dmaEnabled`,
  `m_ucRxDepth`) + compteurs lecture/écriture de `RingBuffer` sur une même
  ligne + entrées `TimedLinkEntry` 64 o à cheval sur deux lignes. Preuve :
  même build, seul l'épinglage change → même CCX 87 %, SMT 102 %, CCX
  différents 114 % ; mixer 4,6 → 7,5 ns/cycle. Après fix : cross-CCX mixer
  ~4,4-5,5, worker 3,1.
- `TransportSignal` : réveil perdu possible (StoreLoad) → fence seq_cst ;
  spin borné 50 µs avant park.
Reste : thread audio saturé (~95 %) par UC + mixer ; mixer 5,0 ns/cycle non
épinglé vs 3,6 même CCX. Piste suivante mesurée : placement (worker dans le
domaine L3 du thread audio → ratio 0,75), décision produit à prendre. Churn
de gate worker élevé (300k-1M/2 s, surtout du spin) — hors chemin critique.
Relâchement de timing accepté (verdict adversarial) : pendant une tranche
UC le producteur peut devancer le mixer figé jusqu'à un quantum (enveloppe
L_lead de la spec) ; mots DSP2→UC visibles en milieu de tranche. Validé :
suites série + `mdAudioFirmwareTest`/`mdMidiTimingFirmwareTest`/
`mdUwFirmwareTest` en `MDMM_TRANSPORT=parallel`.
Défaut trouvé en passant (non corrigé) : la purge sur front d'activation RX
(`linkDisposeAtConsumer`) ne peut plus se déclencher après le premier tick
(appelants seulement avec RE=1).

Diagnostics : `MDMM_TRANSPORT_TRACE=1` → watchdog 2 s (phase thread audio,
chunks, positions, clamps par site), sonde de flux hôte, traces d'attente ;
ligne `wall%` (parts de temps mur : tranches UC — attentes et tranches mixer
imbriquées incluses —, tranches mixer, bloqué par site dont lien, exec/park
worker) et ligne `ns/cycle` (coût par cycle émulé et taille des tranches).
Pour isoler un effet de placement : `SetThreadAffinityMask` sur le thread
audio (au handoff) et le worker (Ryzen 3700X : CPU logiques 0-7 = CCX0).

## Saturation CPU (après f8d8992)

`f8d8992` (2026-09-23) : worker dans la bande MMCSS « Pro Audio » (repli
TIME_CRITICAL ; `MDMM_WORKER_PRIORITY=normal|high` pour les A/B) et banc
hôte `mdParallelTransportBenchmark` (exécutable, pas un ctest). Mesures
3700X, blocs 128 à 48 kHz, 4 appelants : sans charge 82-95 % partout (série
~95 %) ; 12 threads de charge time-critical : worker normal 251 %, worker
time-critical 95 % (série 139 %) ; 12 threads MMCSS : worker MMCSS 170 %
(série 199 %). **Tous les cœurs pris (14 threads) : 353 % contre 144 % en
série.** Placement par CPU Sets essayé puis abandonné (pire sous charge).

Cause : sous saturation l'OS ne planifie pas le worker pendant des ms, et
le thread audio attend DSP2 (DspTime, lien, place du flux hôte) sans rien
faire alors qu'il a le CPU.

Branche `claude/transport-producer-help` : jeton d'exécution de DSP2
(`m_producerExec`). Le worker le prend pour chaque tranche ; le thread
audio le prend en try_lock dans `waitSignalOrHelp` (utilisé par
`waitTransportImpl` et l'attente lien de `linkRxAvailable`) quand la
position publiée du producteur n'a pas bougé depuis
`MDMM_PRODUCER_HELP_US` (défaut 50 µs, 0 = désactivé) et exécute alors les
tranches lui-même jusqu'à satisfaction, ou jusqu'à ce que le worker
reprenne le jeton. Hors jeton, seul `dspCycles[1]` publié est lu (garde et
park du worker). Objectif : sous saturation, retomber au coût série au lieu
de le dépasser. Risque restant : worker préempté en pleine tranche (jeton
tenu), borné par la taille d'une tranche (une frame).
Le compteur `helped=` de la ligne `[worker]` (`MDMM_TRANSPORT_TRACE=1`) dit
combien de tranches le thread audio a exécutées.

Mesuré le 2026-09-24 sur le 3700X (PR #1 adoptée, fast-forward) :
- Gate verte : suite série, et en `MDMM_TRANSPORT=parallel`
  `mdAudioFirmwareTest`, `mdMidiTimingFirmwareTest`, `mdUwFirmwareTest`,
  `mdParallelTransportFirmwareTest`, avec aide (défaut) et sans (`=0`).
- Banc, même exécutable, `MDMM_PRODUCER_HELP_US=0` contre défaut, 2 passes :
  sans charge 87 % contre 87 % ; 12 threads MMCSS 103 % contre 103 % ;
  **16 threads MMCSS (plus de threads que de CPU) 317 % contre 155 %,
  p99 21,9 ms contre 8,7 ms, pire bloc 48-56 ms contre 10-12 ms**.
  L'aide ne coûte rien hors saturation et divise par deux le temps quand
  les cœurs manquent — le cas d'un Live chargé.
- Le « 353 % contre 144 % » ci-dessus ne se reproduit pas sur machine
  calme (12 et 14 threads : parallèle 100-110 %, série 110-150 %). Il était
  mesuré avec un serveur vite égaré qui prenait 3,3 cœurs : la machine
  était en fait sursouscrite, régime où série et parallèle sans aide
  tombent tous deux vers 330-375 %.
- Correctifs après revue adversariale : `MDMM_PRODUCER_HELP_US` n'était lu
  que dans `enableParallelTransport` (restauration d'état projet), donc
  ignoré par une instance neuve et par le banc — lu aussi au constructeur ;
  le worker appelait `producerTargetCycles()` hors jeton, qui lit la tête
  du flux hôte pendant que le thread audio peut la dépiler (course de
  données) — la porte hors jeton est désormais `producerGateHint()`
  (valeurs publiées seules), la porte d'autorité avec l'allowance est
  calculée sous le jeton, et une allowance épuisée est mémorisée
  (position + épisode de blocage, `m_hostWriteBlockEpisode`) pour parker
  au lieu de reprendre le verrou en boucle.
- Constats de revue gardés tels quels : un worker préempté en pleine
  tranche garde le jeton (l'aide ne couvre que le worker non planifié
  entre deux tranches) ; `waitSignalOrHelp` tourne par tranches de 50 µs
  sans parker (le thread audio attend activement pendant un bloc).
- Alternative écartée : un exécuteur « help-first » plus complet (jeton
  Free/Worker/Host, bail rendu aux points sûrs, demande de cession sur
  stall, placement Auto mesuré entre threaded et épinglé), branche locale
  `wip/help-first-executor`. Aux mesures : égalité avec la PR #1 dans tous
  les scénarios, l'épinglage ne gagne jamais, cinq fois plus de code.

## Rendu asynchrone et compteur CPU du DAW (2026-09-24/25)

Cible utilisateur : le compteur CPU d'Ableton ne dépasse jamais 30 %. Ce
compteur mesure le temps passé DANS l'appel process du plugin, pas le CPU
total. Le transport parallèle seul (étape 2) plafonne vers 70-75 % : UC et
mixer restent sur le thread audio.

Comparaison avec Virus (lecture du code + mesure, `temp/cmake_virus`,
virusTestConsole instrumenté, patch non commité) : OsTIrus TI2 sur la
démo = 31 % mur, 63 % CPU total, 2 DSP à 133 MHz à 2,37 ns/cycle, le même
coût par cycle que notre worker DSP2. Virus n'émule pas de µC (C++ haut
niveau) et fait tourner chaque DSP sur son thread avec 1 bloc de latence ;
le callback audio ne fait que copier. microQ/XT/Nord (68k émulé) suivent
le même schéma avec l'UC sur son propre thread.

`md::AsyncRender` (67c327ee) : avec une latence plugin > 0, la machine
entière (ordonnanceur inchangé) tourne sur un thread de rendu MMCSS, un
pas derrière l'hôte ; `process()` dépose le bloc et lit une FIFO amorcée
de silence. `Plugin` attend un device au repos avant tout accès hors audio
(`waitDeviceIdle`, verrou relâché pendant l'attente).

Mesure (banc `--paced 1`, 128 à 48 kHz, 60 s après boot) :
parallel latence 0 = 77 % moyen / 171 % max / 1446 décrochages en 30 s ;
parallel 2 blocs = 4,4 % moyen, p99 10 %, max 21 %, 0 décrochage ;
avec 12 threads MMCSS de charge 3,6 % / p99 8 % / 9 décrochages.
Défauts plugin (MD) : parallèle + 2 blocs ; réglage « Parallel transport »
dans la page DSP/Audio ; la latence se règle dans la même page.
Vérifié sans aucune variable d'env (banc `--mode default`) : parallèle
demandé et actif, 2 blocs, 3,5 % moyen, p99 7,5 %, max 13,9 %, 0 décrochage
(4 appelants MMCSS, 20 s après 60 s de chauffe). Une valeur `latencyBlocks`
déjà sauvée dans la config reste prioritaire sur le défaut.

Pièges trouvés en route (tous des artefacts de mesure, pas du rendu) :
- banc cadencé qui « rattrape » son retard : appels enchaînés plus vite
  que le temps réel pendant le rattrapage → pipeline apparemment saturé.
  Correct = un bloc en retard est un décrochage, le suivant arrive à la
  période suivante ;
- `sleep_for(1 ms)` peut dormir un tick OS entier (15,6 ms) → faux
  décrochages ; cadencement en attente active ;
- harness en hôte non temps réel : `Processor::processBlock` fait alors
  le travail du contrôleur sur le thread audio (pics toutes les 5 s).
  Le banc cadencé passe l'hôte en temps réel, comme un DAW en lecture.

Limite : la première minute après le boot, le firmware vérifie la flash
mot à mot (boucle UC `$205ec8`, `move.w (a0),d0 / cmpi.l #$ffff`), la
machine ne tient pas le temps réel (rafales à 1,3-1,8 période). Déjà le
cas avant ; accélérer cette boucle (ou les lectures flash) est une piste.
Le profil PC du banc (`--profile N`, UC compris, opcodes bruts) l'a
révélée.

### Ableton figé par un rendu trop lent (2026-09-25)

Nouveau MM chargé dans Ableton (latence > 0, donc rendu asynchrone) : le
thread de rendu MM à 100 % d'un cœur, en retard, et Ableton « ne répond
pas », thread principal à 0 % (bloqué). Cause : `Plugin::waitDeviceIdle`
attendait `m_done == m_submitted`, verrou relâché ; le thread audio
soumet un bloc à chaque période, donc un appareil plus lent que le temps
réel n'est jamais au repos et toute opération de contrôle (sondage du
contrôleur, getState, réglages) attend sans fin. Le MD y échappait parce
que son rendu garde de la marge - sauf, en principe, pendant la minute
de vérification flash au démarrage.

Correction : `Device::finishRendering()` (attend les blocs soumis AVANT
l'appel, sans verrou), puis `pauseRendering()` (le thread de rendu
s'arrête entre deux blocs), verrou, opération, `resumeRendering()` au
destructeur du garde `Plugin::DevicePause`. Attentes bornées quel que
soit le retard. Pendant une pause, `AsyncRender::process()` n'attend
plus un rendu qui ne viendra pas : le manque sort en silence ; si les 16
emplacements sont pleins, le bloc est abandonné (MIDI reporté au suivant,
trou comblé de zéros pour garder l'alignement). Sans ce dernier point,
interblocage : thread audio bloqué sur un emplacement en tenant le
verrou, thread de contrôle bloqué sur le verrou - le test
`mdAsyncRenderTest` l'a trouvé (hôte qui rattrape son retard en rafale ;
dans un DAW, un thread interface suspendu > ~40 ms entre pause et verrou
suffirait). Mesure du test : accès de contrôle au pire 7 ms avec un
rendu à 200 % du temps réel.

Constat MM, pas encore expliqué : le MM du build installé le 22/09 (PGO,
série, latence 0) semblait ne presque rien coûter dans Ableton (threads
audio ~10 % d'un cœur au total), alors que le MM actuel sature un cœur et
que le banc mesure 140-160 % en série. Profil banc MM : UC 36 % dans sa
boucle d'attente `$2004be` (`bra *`, censée être sautée par paquets),
chaque DSP ~30 % dans une boucle d'effacement `p:$100164`.

### Coût MM en temps hôte (2026-09-25)

Attention : le profil `--profile` (PC des processeurs émulés) donne le temps
ÉMULÉ, pas le temps hôte - une boucle d'attente sautée garde son PC. Pour
le temps hôte : compteurs `MDMM_TRANSPORT_TRACE` (ns/cycle par composant)
et nouveau `--host-profile N` du banc (échantillonne le RIP des threads,
symboles DbgHelp, JIT = `[jit]`).

Série, latence 0, par seconde émulée : MD = UC 331 ms + mixer 348 +
producer 368 (~105 %) ; MM = UC 634 + mixer 288 + producer 417 (~134 %,
banc 142-167 %). UC MM : 15,8 ns/cycle contre 8,3, ~2,3× plus
d'instructions 68k interprétées (firmware plus occupé : 36 % du temps
émulé en attente contre 65 % pour le MD). DSP MM : cycles exécutés ~1,55×
ceux du MD (le MD n'exécute que ~55 % de ses cycles DSP). Tranches MM 4×
plus courtes (quantum 30 µs contre 125).

Boucle `p:$100162` = temporisation `do #$c80` + `nop`, puis effacement de
32 mots Y. Leviers essayés, sans effet mesurable (bruit ±15 %, Ableton
chargé en fond) : `MD_MAX_DO_ITERATIONS` 4/64/1024 = 166/157/171 % ;
quantum (`MD_BACKGROUND_QUANTUM_US`, nouveau) 30/60/125/250 µs =
168/177/199/163 %. Profil hôte MM : 30 % JIT, puis schedStep 6,6 %,
interpréteur 68k 6,3 %, timers SIM 4,2 %, accès mémoire 68k ~8 %,
périphériques DSP ~8 %, rééchantillonnage 48→44,1 kHz ~4 %.

Conclusion : pas de réglage, coût intrinsèque. Voie : transport parallèle
MM (producer ~0,42 s/s sur le worker) → rendu ~0,85-0,95 s/s, juste ; à
44,1 kHz et en PGO, marge probable. Stratégie déjà ratifiée :
`issues/06-strategie-mm.md` (canaris mmSine*).

### Bascule du producer MM : échec mesuré (branche locale wip/mm-parallel-handoff)

Essai minimal : retirer l'exclusion MM de `schedTryHandoffProducer` et
donner au worker la contre-pression MM comme garde fermée (seuil 4 mots,
relâche 200 000 cycles UC publiés). Bascule effective (`active=1`), mais
304 % du temps réel au mieux (série ~165 %), et quasi-interblocage dans
d'autres essais : UC bloqué 86 % du temps sur le producer (8590 délais
expirés / 2 s), attentes de lien toutes expirées, worker immobile. Le
lien MM est un aller-retour strobe → rafale : le producer lit la réponse
du mixer, le mixer ne peut devancer l'UC que de D = 1 trame, l'UC attend
le producer. C'est le point 5 du ticket 06, « seul point non prouvé » :
il ne tient pas. Piste suivante : repli D_mm = 0 du ticket, ou dater le
sens mixer → producer, ou autoriser le mixer à devancer l'UC.

Piège au passage : le marqueur de contre-pression, écrit à chaque
évaluation de la garde et placé juste après `m_schedUcCyclesDone`, a
coûté 14 % des échantillons à l'UC (53 ns/cycle au lieu de 16) par faux
partage. Toute donnée que le worker écrit en tournant doit avoir sa
propre ligne de cache.

### Voie 2 : les deux DSP sur un worker, l'UC seul (mode `pair`, 2026-09-25)

`MDMM_TRANSPORT=pair` : un worker exécute les DSP1 et DSP2 (choix du
retardataire comme l'ordonnanceur série, rattrapages de lien en ligne,
donc strobe/rafale MM inchangés), le thread audio garde l'UC seul. Opt-in,
défaut inchangé. Bascule après boot des deux DSP (schedTryHandoffPair).

Écarts au pont série trouvés et corrigés en chemin (chacun cassait le
canari GND SIN) :
- commande MM dont la vidange HORX dépasse la porte UC : le série faisait
  courir le DSP jusqu'à 4 fenêtres en avance ; ici la tête bloquée donne
  la même avance (hostToDspHeadBlockedAllowance), aussi aux mots bloqués
  sur HRX et au pair DSP (dépendance par le lien) ;
- élément devenu applicable pile sur une porte fermée : le worker applique
  les flux à chaque tour ;
- prédicat d'attente qui notifie sous le mutex du signal : exception
  std::system_error → terminate (0xC0000409). Prédicats sans effet de bord ;
- DSP→UC MM : copie de HOTX mise en file tout de suite, prise par l'UC
  quand son verrou est libre, HOTX libéré quand le DSP atteint le temps de
  la prise (accusé daté m_hostTxTakes) - rythme HTDE du série ;
- DSP en retard sur l'UC : un mot DSP arrive en retard du retard du worker.
  Borne : UC ≤ DSP le plus lent + un quantum (l'enveloppe laggard-first) ;
- avances ci-dessus (jusqu'à 5 fenêtres) : le DSP lisait l'entrée audio
  au-delà de ce que le thread audio avait mis en file → plafond à cible de
  bloc + 32 trames (marge d'entrée 64), sauf UC bloqué sur flux plein ;
- `hasSource` levé en début de processAudio avant le premier ajout
  d'entrée : un worker lisant dans l'intervalle comptait une
  sous-alimentation (mmInputFirmwareTest instable, 1 échantillon). Levé
  maintenant après l'ajout.

Tolérances mesurées (canari GND SIN) : avance UC sur DSP ≤ 60 µs (120 µs
échoue), avance DSP sur UC ≤ 30 µs (60 µs échoue). Lecture CVR (bit HC) :
exacte obligatoire (publiée à retard borné → échec). Lecture ISR : à retard
borné acceptée (instantané publié HF2/HF3 + profondeur HORX, flux daté
compté comme occupé).

Débit (banc série latence 0, MM) : série ~165 %, paire 132-145 % (bruit
±5 %). Profil par thread : ~26 000 attentes UC/s de ~11 µs, worker avec
tronçons courts dont le coût fixe égale le travail JIT. Fenêtre MM trop
étroite (30-60 µs) pour recouvrir UC et DSP avec ce coût de synchro.
Travail pur : UC ~0,6 s/s, DSP ~0,45 s/s → plafond ~70 % si recouvrement
parfait. Pistes : coût par tronçon et par synchro (portes en cache, pas de
double service/publication par tour), synchro par spin court sans noyau,
ou accélérer l'UC lui-même (interpréteur 68k : 0,6 s/s).

## Piste 1 : mesure fine du mode paire (2026-09-26)

Trace `[pair]` sous `MDMM_TRANSPORT_TRACE` (horodatage QPC par tronçon, par
tour, par attente, raison d'arrêt du laggard ; côté UC : borne d'avance,
lecture exacte, fin de bloc). Banc MM paire, latence 0 :

- worker : tronçons 75 % du mur (70 000/s, ~2 030 cycles, 5,25 ns/cycle),
  tour 2,4 %, repos 22 % dont « gather » 17 % (attente de place UC) ;
- UC : attend la borne d'avance 27 % (23 000/s, ~11 µs), lecture exacte
  5 % (5 400/s), fin de bloc 0,2 %.

Le profil hôte (échantillonnage suspend/contexte) donnait l'inverse (worker
~60 % en portes/spin). Biais d'observateur : suspendre un thread pour
l'échantillonner fait attendre l'autre, que l'échantillon suivant trouve
alors en spin. Avec deux threads couplés serré, croire la trace QPC, pas
l'échantillonneur. En série (un seul thread), l'échantillonneur reste fiable.

Le coût DSP par cycle est le même qu'en série (série tracée : mixer 4,6,
producteur 5,4 ns/cycle). Travail DSP par seconde émulée ≈ 203 M cycles ×
5,25 ns ≈ 1,07 s : **le mode paire ne pouvait pas descendre sous ~107 %**,
quelle que soit la synchro. La piste 1 seule ne suffit pas.

Où vont les cycles DSP (profil PC, positions d'arrêt ≈ cycles émulés) :
~30 % des deux DSP dans une boucle de délai `do #3200 { nop }` (p:$100162),
puis des boucles de scrutation : DSP1 DSR1 (DMA1, ~20 % dont une boucle
multi-blocs à $000087), DSP2 PDRC bit 1 (strobe du lien, 7,7 %) et DDR0
(DMA0, 6,4 %). Avec `maxDoIterations = 4` (MM), la boucle de délai sort du
JIT toutes les 4 itérations : autant d'allers-retours trampoline.

Saut de boucle NOP (sous-module DSP, `DSP::skipNopLoop`) : un corps de DO
fait de NOP applique d'un coup les itérations jusqu'à la première sortie
vers le dispatcher qui agirait (cible execUntilCycles, périphérique dû,
interruption en attente). Mêmes sorties, mêmes échéances : exécution
identique au pas à pas. `DSP_NOP_SKIP=0` le coupe. A/B sans trace, MM
série : 168-169 % → 157-161 % ; paire (trace) 137 % → 130 %, worker
5,27 → 4,54 ns/cycle.

Balayage avance UC avec le saut (15/30/45/60 µs) : 130/130/131/135 %. Plus
d'avance échange des attentes de borne contre des lectures exactes plus
longues ; le « gather » du worker reste ~23 %. Les deux threads ont chacun
~0,9 s de travail par seconde émulée ; l'UC alterne phases chargées (plus
lentes que le temps réel) et repos (sautés vite). La fenêtre de ±30 µs
n'absorbe pas ces rafales : en phase chargée le worker attend l'UC, au repos
l'UC attend le worker. Plancher de ce modèle ≈ 110 % avant pertes de synchro.

Saut des boucles de scrutation (`DSP::skipPollLoop`, même principe) : un
bloc qui rebranche sur son début en ne lisant que des registres
périphériques sans effet de bord (DMA, données ports C/D) et des registres
qu'il ne modifie pas. Trouve DSR1 (DSP1), DDR0 et PDRC (DSP2) ; la boucle
DSR1 multi-blocs ($000087) reste hors portée. Série −3 %, paire inchangée :
le worker va plus vite mais attend l'UC d'autant. Analyse registres :
`Opcodes::getRegisters` n'inscrit pas la destination d'une lecture movep,
à compléter pour l'analyse d'idempotence. Sous-module `1ba7d7d`.

Rééchantillonnage : le banc tourne à 48 kHz, le MM à 44,1 kHz ; le filtre
libresample haute qualité (`lrsFilterUp`) coûte ~15 points sur le thread
UC. Option `--rate 44100` du banc : paire 126 % (48 kHz) → 112 % (44,1 kHz).

Lots d'instructions UC (`Hardware::processUCBatch`) : `processUC` vérifie à
chaque instruction panneau, MIDI, pompe hôte, et avance la SIM après. Sans
entrée en attente et sans échéance SIM (minuterie, UART panneau), MIDI
programmé ou mot hôte dans le budget, ces vérifications ne trouvent rien et
l'avance SIM est linéaire : instructions enchaînées, SIM avancée une fois.
Un accès à une fenêtre périphérique (SIM, HI08) avance d'abord la SIM du
temps accumulé puis clôt le lot après l'instruction ; un acquittement
d'interruption aussi. Le lot s'arrête sur la boucle de repos (BRA.B -2) pour
laisser le saut de repos agir. Budget ≤ 128 cycles quand un worker suit la
position UC publiée (publication à chaque lot), 2048 sinon.
`MD_UC_BATCH=0` le coupe. A/B 44,1 kHz : série 151 → 127 %, **paire 117
→ 102 %** ; le worker (DSP) redevient le goulot (78-80 % en tronçons, UC
~30 % en attente de borne).

Piège trouvé par le gate (paire, 2 échecs GND SIN sur 5) : un réveil de
pompe hôte posé par le worker pendant un lot n'était vu qu'à la fin du lot.
L'UC prenait le mot du DSP jusqu'à 128 cycles plus tard, HOTX se libérait
plus tard et décalait le rythme HTDE du DSP : clics. Le lot s'arrête
désormais dès que le drapeau de pompe se lève (6/6 puis gate vert). Coût
~4 points en paire.

Couplage paire après lots (44,1 kHz, trace) : taille mini de tronçon
288/576/1152/2304 cycles → 99,8/102/99/99,5 % ; avance UC 45/60 µs → 106 %
(lectures exactes plus longues). Rien à gagner par ces réglages : le worker
travaille 80 % du temps, reste attente de rafales UC.
`MD_PAIR_MIN_CHUNK` reste pour ces essais.

### Piste 2 : coût côté DSP et placement (2026-09-26)

Qui fixe les échéances périphériques du MM (47 M services / ~15 s, deux
DSP) : créneaux fins du lien ESSI0 74 % (~78 cycles, travail réel), HDI08
14 % à délai 0, DMA 8 % à délai ~1.6, minuteries 3 %.
- HDI08 à délai 0 : mot hôte en attente, canal DMA récepteur coupé (DE
  effacé en fin de bloc) → `exec` redemande un service à chaque bloc
  jusqu'à ce que le firmware réarme le canal. 6,7 M services sans effet.
  Rendre MaxDelay dans ce cas : **aucun gain mesuré** (131-135 % des deux
  côtés), et pas exact (les interruptions externes ne sont traitées qu'aux
  services). Abandonné.
- DMA délai ~1 : transfert bloc déclenché par DE en mode 3D, un mot par
  service après le délai initial (canal 2, 3,7 M mots). Coût faible,
  changer le modèle serait un changement de timing. Laissé.

Profil série 44,1 kHz (après lots UC) : postes bon marché à supprimer, tous
exacts :
- `std::min({…})` à 4 éléments → dispatcher vectorisé MSVC
  `__std_minmax_disp` : 2,5 % (Peripherals56303::exec, budget de lot) ;
- `skipNopLoop` 4,1 % : divisions entières à chaque entrée de boucle
  (~3 M/s) → pas de division si pas = 1, masques pour la période de sortie
  (puissance de 2) ;
- crochet de lot UC appelé à chaque accès mémoire → déplacé dans les
  branches SIM/HI08 ;
- `transportPolicy` construisait sa struct à chaque appel → référence.
Série 128 → 121-123 %, paire ~100 → 91-97 %.

Placement des deux threads (Ryzen 7 3700X : 2 CCX de 4 cœurs, SMT) :
UC/worker sur cœurs distincts d'un même CCX **80 %**, frères SMT d'un cœur
94-96 %, CCX différents 90-91 %, placement libre 90-97 % (le « 80 % »
aléatoire vu plus tôt). `MDMM_PAIR_AFFINITY=auto` : UC gardé sur son cœur
physique au handoff, worker sur les autres cœurs du même cache L3 → 78-86 %
à 44,1 kHz, 87-94 % à 48 kHz. Opt-in : dans un DAW, les threads du moteur
partagent ces cœurs, à mesurer dans Ableton avant d'en faire le défaut.

Worker seul placé (UC libre, `=worker`) : 95-99 %, **aucun gain** : l'UC
libre migre vers le frère SMT ou l'autre CCX. Le gain exige de fixer l'UC.
Défaut retenu : placement `auto` seulement quand l'ordonnanceur tourne sur
le thread de rendu du device (latence plugin ≥ 1 bloc), jamais sur le
thread audio de l'hôte ; `MDMM_PAIR_AFFINITY=off` le coupe.

Plugin MM : réglage « Parallel transport » ajouté (page CPU Load), par
défaut actif comme sur MD ; pour le MM il choisit le mode paire.

Pourquoi pas le mode MD (producer seul sur un worker) pour le MM : essayé
(section plus haut), 304 % et quasi-interblocages. Le lien MM est un
aller-retour strobe (port C) → rafale DMA sur ESSI0 que DSP1 attend ;
coupé entre deux threads, chaque strobe devient une attente croisée
UC ↔ DSP1 ↔ DSP2.

## Test Ableton du MM (2026-09-28)

Ableton Live 12.2.1, ASIO MOTU M Series, 44,1 kHz, buffer de 256 échantillons
(5,8 ms). VST3 MM du commit 9d9b1da8 : mode paire, placement automatique,
latence du plug-in 2 blocs (réglage de l'utilisateur, conservé).

- Page DSP & Audio : « Parallel transport is running ».
- Compteur CPU d'Ableton relevé toutes les 1,5 s pendant 30 s : 13-21 %
  (moyenne ~17 %) transport arrêté, le MM jouant son propre pattern ;
  14-18 % (moyenne ~15 %) transport en lecture. Cible (30 %) tenue.
- Capture « Performance diagnostics » du plug-in, 10 min (limite atteinte) :
  103 395 callbacks hôte, 0 dépassement estimé, 23 µs par callback en moyenne
  (0,4 % du budget). Une seule perturbation : une rafale de 350 ms (~55
  callbacks) où le thread hôte a attendu le rendu jusqu'à 5,62 ms sur 5,80,
  pendant un scan disque récursif lancé à côté. Avec 1 bloc de latence, la
  même perturbation aurait probablement décroché : garder 2 blocs ici.
- Horloge : l'écran du MM reste à 120.0 pendant qu'Ableton joue à 136. Le
  plug-in envoie bien l'horloge (`synthLib::MidiClock` : 24 PPQN, START, SPP,
  STOP, seulement transport en lecture) ; c'est la réception du MM qui est
  coupée (GLOBAL = FUNCTION + KIT, puis MIDI SYNC).
- Hors plug-in : le bandeau « Certains plug-ins sont désactivés » vient
  d'Oxford Inflator (« VST3: Restore 1 failed » dans Log.txt). Le rapport de
  crash affiché par Live concerne la session précédente (25/09 → 28/09
  02:27), terminée sans dump : arrêt forcé ou blocage, rien qui désigne
  Gearmulator.
- Accords au panneau à la souris : Shift maintenu, le premier contrôle cliqué
  reste tenu jusqu'au relâchement de Shift (`ShiftPanelLatch`).

## Option « Follow host tempo » (2026-09-28)

Le plug-in envoyait déjà l'horloge de l'hôte (`synthLib::MidiClock` : 24 PPQN,
START, SPP, STOP, seulement transport en lecture), mais les machines l'ignorent
en réglage usine. L'option règle leur réception MIDI SYNC.

Offsets vérifiés contre firmware, en changeant la valeur dans le menu de la
machine puis en comparant les dumps Global :

- MM OS 1.32b, Global version 3.1, 264 octets décodés (7 bits + RLE) :
  [5] bit 0 = TEMPO SYNC EXT MIDI CLK, [6] = TRANSPORT ACCEPT (octet entier).
  Menu : FUNCTION + KIT → GLOBAL n EDIT → CONTROL → CONTROL IN. Usine : 0 / 0.
- MD OS 1.63, Global version 6.1, 197 octets bruts : octet 0xB2, bit 0 = TEMPO
  SOURCE EXTERNAL, bit 4 = CTRL IN **OFF** (inversé par rapport à mididuino,
  qui décrit la version 5). Menu : FUNCTION + PATTERN/SONG → GLOBAL → SYNC.
  Usine : horloge interne, CTRL IN on.

Écriture :

- MD : Global patché envoyé tel quel puis SET STATUS (0x71 01 slot) pour
  recharger le slot ; sans rechargement le Global est stocké mais pas actif.
- MM : un dump Global n'est accepté qu'en mode SYSEX RECEIVE, et même reçu il
  ne remplace pas les réglages actifs ; écrire la RAM du Global actif
  (0x001162A0) ne les change pas non plus. Reste le menu : macro panneau
  (`md::monomachineSyncMacro`), curseurs ramenés en butée avant chaque choix,
  valeurs bornées (deux appuis = idempotent). ~3 s d'écran de menu.

Architecture : `md::HostSync` (machine à états, vérifie sur un dump frais,
abandonne après deux essais) vit dans `md::Device`, servi dans `readMidiOut`
sur le thread de rendu. Pas de `withDeviceLocked` : il met le rendu async en
pause, impensable à 60 Hz. UI ↔ Device par `md::HostSyncControl` (atomiques).

Témoin du test firmware (`hostSyncFirmwareTest`) : les LED du séquenceur, pas
l'audio (le patch SUPERWAVES du MM tient ses notes quand le séquenceur gèle).
La LED TEMPO bat avec l'horloge reçue même arrêté : bancs 0x23 (MD) et 0x26
(MM) exclus, ainsi que 0x28 (MM, clignote seul).

Boot : le MM répond au SysEx 1,06 s après la mise sous tension
(`isFirmwareMidiReady`) mais affiche encore son logo et ignore le panneau.
Mesuré (temps émulé) : macro lancée 7 s après « MIDI prêt » = échec, 8 s =
réussite. HostSync ne touche donc à rien pendant les 10 premières secondes
d'une machine, et fait au plus trois tentatives. Le test plug-in
(`mdHostSyncPluginTest`, vrai processor + controller) l'a révélé : l'état
était lu à 2,4 s et la macro partait dans le logo.

## Monomodule (Shnolk, 2026-09-26)

Autre émulateur Monomachine, AGPL v3 : dsp56300 seulement, sans émulation du
ColdFire (séquenceur et UI natifs), le moteur son vient du fichier OS `.syx`.
Son patch dsp56300 corrige : saturation SR.SM, MPYRI et PFLUSH, extension de
signe des immédiats 24 bits dans le JIT, vecteurs d'interruption traités comme
du code, échec JIT récupérable, et sous Win64 la sauvegarde XMM6-15 sur 128
bits plus un spill mort vers un XMM callee-saved.

Notre fork a déjà MPYRI, PFLUSH/PFLUSHUN, l'extension de signe
(`getSignedOpWordB`) et `SR_SM`. L'écart XMM existe chez nous mais reste
latent : `JitStackHelper` sauve et restaure les XMM en `movq` (64 bits sur des
emplacements de 16 octets), mais sous Win64 `g_spillToNonVolatileXmms = false`
tient XMM6-15 hors des blocs. À durcir (`movdqu`) avant de réactiver ce spill.

## Course du mode paire (2026-09-28)

`mmSineFirmwareTest` en paire échouait par intermittence, commit de base
compris : 2/16 en séquentiel, 1/16 avec deux instances à la fois, 0/24 avec
six (sous charge, le défaut disparaît). Deux signatures : piste muette
(« MM note produced silence », niveau ou note-on perdu) et clics.

Rapport d'échec ajouté au test (reste) : attentes expirées par site, trames
sorties muettes et jetées, échantillons autour de la plus grande différence
seconde du rendu fautif. Instrumentation temporaire (retirée) : retard des
mots UC→DSP à leur dépôt (position publiée du DSP moins l'échéance du mot),
retard de prise des mots DSP→UC, délai d'envoi des commandes, lectures CVR
pendant qu'une commande attend dans le flux daté.

Cause 1, mots hôte en retard. À vide, le worker attend à sa porte : chaque mot
de l'UC atterrit avec toute l'avance DSP (30 µs), plus jusqu'à un tronçon
(22,7 µs) s'il arrive pendant un tronçon. Le série alternait (0 à 30 µs).
Dose-réponse (sine, 2 instances) : avance DSP 60 µs → 6/6 échecs ; 45 µs →
6/6, dont 2 pistes muettes, même signature que les échecs par défaut ; 30 µs
→ ~1/16 ; 0 µs → 0/6, et 0/6 même avec avance UC 60 µs. Débit (banc, MM
paire, 44,1 kHz, 20 s) : avance DSP 30 µs 81-87 %, 20 µs 82,7 %, 15 µs
82,9 %, 10 µs 82,6-84,2 %, 5 µs 91 %, 0 µs 105-110 % (le worker ne se
pipeline plus). La fenêtre totale compte : DSP 10 + UC 20 → 91,7 %, 5 + 20
→ 101 %. Correctif : avance DSP portée par la politique de transport, 10 µs
pour le MM ; l'UC garde son quantum (30 µs), le MD aussi (125 µs).

Cause 2, bloc court d'une trame. Avec l'avance à 10 µs : 1/24, un seul
échantillon nul en dernière position d'un bloc de 256, sinus décalé d'un
échantillon ensuite. En fin de bloc, le thread audio prend les trames que le
mixer a émises ; worker pile à la cible, la dernière trame du bloc peut ne
pas être sortie. Nombre de trames par bloc lié à l'avance réelle du worker,
d'où plus fréquent à 10 µs qu'à 30. Nouveau compteur `hostAudioUnderrunCount`
(trames sorties muettes faute de trame codec) : série 4 fixes par run (aux
reprises après `advance()` sans audio, hors fenêtre mesurée), paire 0 à 4
selon le run. Premier correctif (commit 11644327) : deux trames muettes
d'avance dans la file hôte dès le passage en paire. Insuffisant : 1/16 en
séquentiel, un échantillon nul juste avant la fenêtre mesurée. Entre deux
rendus, le test avance la machine sans audio ; les trames vidées alors sont
jetées et chaque trou décale le niveau de la file d'une trame ou deux, marche
aléatoire qui finit par vider le coussin.

Correctif retenu : chaque bloc prend exactement ses trames. Le worker date
chaque trame codec avec le tick d'horloge ESSI qui l'a émise
(`EsxiClock::getLastClock`), le thread audio prend les trames datées jusqu'à
deux trames avant la cible du bloc et laisse les suivantes au bloc suivant.
Mesures : dates espacées d'exactement 2304 cycles, rappel en retard sur son
tick de 1060 cycles au plus (une fois 1619, soit 0,7 trame), trames sorties
muettes : 0 par run en paire. Écarté en chemin : compter les trames depuis la
position du mixer au passage en paire (le codec ne démarre que bien après,
aucune trame pendant des centaines de blocs : le compte part devant et la
vidange reprend tout ce qui est là) ; arrondir le cycle du rappel sur une
grille de 2304 (retard jusqu'à une demi-trame, une trame datée deux fois).

Résiduel (avance 10 µs, vidange datée) : 1 échec sur 80 runs (16 séquentiels
+ 64 à deux instances), signature différente : 16 zéros exacts dans la sortie
même du mixer, puis ruptures à +16 et +32 trames, 0 trame hôte manquante. Une
rafale du lien DSP2→DSP1 manquée. Piste : le rendez-vous du strobe PDRC
(`setCallbackDspWrite` du port C du mixer) purge l'anneau de lien et attend
une trame fraîche quand les DMA des deux DSP sont inactifs ; en paire, l'ordre
fin DSP1/DSP2 sur le worker dépend des échéances des mots UC (fin de tronçon
sur `nextHostTransportCycle`), qui arrivent en temps réel. Heuristique réglée
sur l'ordre déterministe du série : une rafale légitime peut être purgée. À
traiter dans le modèle du lien, pas dans le transport hôte.

### Rafale de lien manquée : enquête (2026-09-28)

Build de diagnostic séparé (`temp/cmake_diag`, `MD_TRANSPORT_DIAGNOSTICS=ON`,
sans plug-ins) pour les compteurs du lien, plus sondes temporaires sur le
strobe PDRC (écriture par DSP1, première lecture du nouveau niveau par DSP2).
- ~232 000 strobes par run. À l'écriture, DSP2 est derrière DSP1 de 0 à 1
  trame ; la boîte aux lettres n'est pas datée, DSP2 voit donc le nouveau
  niveau « avant » l'écriture en temps émulé. Réaction de DSP2 : ≤ 0,02 trame
  après l'écriture en paire (avance 10 µs), ≤ 0,85 en série.
- Lectures à vide du mixer : 11 à 24 par run, en série comme en paire, toutes
  à l'époque 0 (démarrage du lien). Hors sujet.
- Avance DSP 60 µs (6/6 échecs) : strobe vu jusqu'à 3,3 trames après
  l'écriture, `mmProducerDmaInactiveDrops` ×10 (620 000 contre 45-64 000 en
  paire à 10 µs, 93 000 en série). DSP2 répond tard au strobe.

Expériences sur ce cas amplifié :
- Retenue « aller-retour » (`MD_PAIR_REPLY_US`, retirée) : tant qu'un mot
  d'un DSP va vers l'UC (copie posée, prise pas encore acquittée) et 30 µs
  d'UC après la prise, aucun DSP ne dépasse l'UC. Retenir DSP2 seul : 6/6
  échecs, pires (le mixer garde son avance et affame le lien). Retenir les
  deux : **6/6 verts** à avance 60 µs, réaction au strobe ≤ 0,015 trame.
  Mécanisme confirmé : les réponses de l'UC aux requêtes des DSP arrivent en
  retard de l'avance DSP. Mais coût au banc (MM paire, 44,1 kHz) : 92-100 %
  du temps réel selon l'avance, contre 71-84 % sans retenue. Les DSP écrivent
  à l'UC des milliers de fois par seconde (DSP2 ~5 000-9 000/s), chaque fois
  le worker attend l'UC.
- Statut HI08 daté (`MD_PAIR_DATED_STATUS`, retiré) : historique des
  changements HF2/HF3 et profondeur HORX avec le cycle DSP, lu par l'UC à son
  propre temps (plus de lecture du futur du DSP). 6/6 échecs à 60 µs : pas la
  cause.
- Avance DSP 0 avec avance UC plus grande : 92,8 % (UC 60), 91,7 % (UC 90),
  95,4 % (UC 120). Trop cher aussi.

Piste restante, non testée : les vidanges HORX longues. 65 commandes par run
attendent plus de 256 trames (règle MM « données avant commande », jusqu'à 4
clamps ≈ 3,9 ms) ; pendant ces épisodes l'allocation laisse les DSP courir
loin devant l'UC (~130 000 mots UC par run déposés avec plus de 100 µs de
retard, même à avance 10 µs). Si un aller-retour critique tombe dedans, sa
réponse arrive très en retard. À corréler avec le moment de l'échec.

Pistes écartées :
- CVR : l'UC lit HC à 0 alors que sa commande attend encore dans le flux
  (~2 M lectures par run). Lire HC à 1 tant qu'elle attend : pire (6/6 plus
  tôt ; l'UC scrute pendant toute la vidange HORX). Geler l'UC dans son
  écriture CVR jusqu'à l'envoi, comme le pont série : pire, même par défaut
  (2/2), sortie mixer presque vide, car pendant le gel l'allocation sans
  plafond laisse les DSP courir loin devant.
- Contre-pression MM : jamais active en paire (backlog ≤ 2 mots, seuil 4).
- Attentes expirées : 0 dans tous les runs. Anneau codec : SPSC correct.

Trafic mesuré, run complet du test : ~570 000 commandes et ~18 M mots de
données par DSP ; 65 commandes par run attendent plus de 256 trames (vidange
HORX jusqu'à l'échéance de 4 clamps).

### Écoute longue et mécanisme (2026-09-28/29)

Retenue levée à la réponse de l'UC (première poussée vers le DSP après la
prise) au lieu d'une fenêtre fixe : pas moins chère (banc à avance 10 µs :
94-99 % contre 89 % sans retenue et 89-96 % en fenêtre fixe). Les échanges
forment des chaînes : lever la retenue à chaque commande rend l'avance au DSP,
que le mot suivant reprend aussitôt.

Protocole mesuré (statistiques des prises côté UC) : DSP2 envoie ~2 500 fois
par seconde (une fois par rafale de 16 trames) la suite de mots 02, 00, 01,
fe ; l'UC répond par les commandes hôte 0c (toujours, 2,1 µs), 14 (une fois
sur deux, 15 µs), 10 (5 µs), 12 (7,4 µs). DSP1 envoie ~2 700 mots par
seconde (015c, 21ff-3155...), réponses 10/12/14 une fois sur deux. Après un mot de DSP2, l'UC pousse
aussi vers DSP1 en 0,8-3,2 µs.

Nouveau mode `mmAudioFirmwareTest --listen` (`MM_LISTEN_SECONDS`) : six pistes
GND SIN en boucle de 4 s (partie dense, seconde de silence, note seule), CC7
toutes les 23 ms, rendu continu ; compte les trous (zéros exacts sur les deux
canaux, 8 à 64 trames, du son de part et d'autre). Contrôle positif à avance
60 µs : 71-73 trous en 120 s, tous de 16 trames. Sans silences dans le motif :
0 trou même à 60 µs (le lien ne passe jamais au repos). Avance 10 µs (défaut) :
0 trou en 2 × 840 s ; série : 0 en 660 s.

`MM_LISTEN_LONE_MIXER` : boucle de 1 s, demi-seconde de silence puis note seule
sur une piste de DSP1 (4 à 6, DSP2 sans voix), l'état du seul échec résiduel
du test sine (piste 6 seule après un silence). Avance 60 µs : ~10 000 trous en
120 s ; 30 µs : 1 ; 20 µs : 0 ; 10 µs : 0.

Écarté sur ce déclencheur à 60 µs :
- Strobe vu tard par DSP2 (boîte aux lettres non datée, DSP2 en avance sur
  DSP1) : plafonner DSP2 à la position de DSP1 supprime les retards, pas les
  trous (8 587).
- DMA1 armé tard : de la lecture du strobe à l'armement, toujours moins d'un
  quart de trame.
- Écart entre DSP1 et DSP2 : borné à 0,25 ou 0,5 trame, ~7 700 trous.
- DSP2 attendant un mot de l'UC : file vide, aucune commande en cours dans
  97 % des échantillons pendant les retards.
- Allers-retours de DSP1 : les retenir ne change rien (4 040).

Retenue sur les allers-retours de DSP2, appliquée aux deux DSP : fenêtre fixe
30 µs → 1 trou ; fenêtre 8 µs levée à la commande → 46 ; 2 µs → 42 962 (la
retenue cesse juste avant la réponse, qui atterrit alors avec tout le retard).
Appliquée à DSP1 seul : 5 021 ; à DSP2 seul : 54 657 (DSP1 part devant et
affame le lien). Filtrée sur un seul mot : 00 → 1 717, 02, 01, fe → aucun
effet. Mécanisme : l'avance du DSP étire toute la séquence d'échanges de DSP2
avec l'UC (quatre allers-retours par rafale, chacun en retard de l'avance).
Coût de la retenue DSP2 à avance 10 µs : non mesuré proprement (machine
chargée par ailleurs, base à 97-99 % au lieu de 89 %).

Code d'expérience retiré (retenues `MD_PAIR_REPLY_*`, avance par DSP, borne
d'écart, plafond de DSP2, sondes strobe et statistiques de prises) ; le mode
`--listen` reste dans le test.

Comparaison : les autres émulateurs du dépôt couplent l'UC bien plus
lâchement (microQ/XT : resynchro toutes les 8 trames, dérive jusqu'à 16 ;
N2X : 16 et 32) ou ne l'émulent pas (Virus) ; Monomodule non plus (séquenceur
et UI natifs). Le firmware MM ne tolère pas ce lâche : rendez-vous UC ↔ DSP2
à chaque rafale.

Décision (2026-09-29) : merge vers `release/md-mm-alpha` avec ce résiduel
documenté (test de stress ~1 run sur 80 ; écoute réaliste 0 trou en 28 min ;
motif dur 0 en 120 s à 10 µs). Ensuite : mesurer la retenue DSP2 sur machine
au repos, l'activer si elle coûte au plus ~5 points.

### Retenue DSP2 : mesurée, laissée désactivée (2026-09-29)

Retenue reconstruite (le code d'expérience n'avait jamais été commité) :
`pairDspLeadUc` rend une avance nulle aux deux DSP tant qu'un mot de DSP2 est
en vol vers l'UC (copie posée, prise pas encore acquittée par le DSP), puis
pendant la fenêtre en temps UC après la prise. Tout est lu dans le worker
paire, qui possède les deux DSP. Réglage `pairHoldDsp2Microseconds` de la
politique, négatif = désactivée (MM et MD) ; `MD_PAIR_HOLD_DSP2_US`
surcharge.

Contrôle, motif dur `MM_LISTEN_LONE_MIXER`, paire, 120 s : avance 60 µs sans
retenue 8 207 trous ; avec retenue 30 µs 10 puis 0 (log précédent : 1) ;
avance par défaut 0 avec ou sans retenue.

Coût (`bench-hold-dsp2.ps1`, banc non cadencé, MM paire active, 44,1 kHz,
avance 10 µs, 5 paires de runs de 60 s alternées, charge 5,8 % au départ) :
médiane 88,6 % sans retenue, 101,3 % avec, soit **+12,7 points** (chaque
paire entre +10 et +15). Règle : au plus ~5 points. Retenue laissée
désactivée : elle fait passer le MM au-dessus du temps réel, et elle ne
corrige que le motif amplifié à 60 µs, qui ne perd rien à l'avance par
défaut.

Fausse piste de la même soirée, à ne pas refaire : `mdParallelTransportBenchmark`
écrit sa propre option `--mode` dans `MDMM_TRANSPORT` et écrase la variable.
Lancé avec `--mode parallel`, qui ne s'enclenche jamais sur le MM
(`parallel requested=1 active=0`), il mesurait le MM en SÉRIE (~102-104 %).
Ces mesures donnaient +0,2 point pour la retenue (sans objet : elle n'agit
qu'en paire), ont fait activer la retenue par défaut (`49dd22e7`, défait
depuis), et ont fait chercher une « régression » de 89 % à 102 % qui
n'était que la différence entre paire et série. Le banc HEAD contre
`7f9e71b0` (option `-CompareExe`) a bien montré l'égalité des deux, mais en
série. Le script passe désormais `--mode pair` et échoue sans `active=1`.

Mesure de charge du script : `Win32_Processor.LoadPercentage` est un
échantillon instantané (14 à 96 % d'une seconde à l'autre sur machine au
repos) ; remplacé par une moyenne `GetSystemTimes` sur 5 s. La charge de
fond pèse lourd sur le temps mur (un binaire de tests d'un autre projet à
12 % a fait monter des runs série de 104 à 154 %).

Reste : MM paire à ~89 % du temps réel en banc non cadencé, marge mince.

### Où passe le temps du worker paire (2026-09-29)

Le même binaire mesure de 68 à 94 % selon le moment de la soirée : seuls les
A/B d'une même séance comparent quelque chose.

Trace (`MDMM_TRANSPORT_TRACE`, ~70 %) : le worker est le goulot. Chunks 84-86 %
du mur à 2,9-3,7 ns par cycle DSP ; 2 × 101,6 MHz × 2,9 ns / 0,86 ≈ 68 %,
ce que mesure le banc. L'UC attend le worker 45-48 % du temps (`UC waits
lead`). Hors chunks, le worker perd ~13 % (`gather` 9 %, `turn` 3 %,
`ucGate` 1,5 %) : au mieux ~7 points.

Profil PC émulé (`--profile`) : la temporisation `p:$100162` (`do #$c80` +
`nop`) prend 24-30 % des cycles de chaque DSP ; les scrutations de
périphériques ~15 % (producer : `$00018d` DDR0, `$000195` PDRC, `$00020a`
HI08) et ~9 % (mixer : `$00017f` DSR1, `$000237` HI08). Coût hôte de la
temporisation, mesuré en ramenant son compteur à `#$10` (diagnostic, retiré) :
~2 points (A/B, 3 paires : 69,4/70,7/71,3 % contre 68,6/68,9/69,2 %). Le JIT
l'exécute presque gratuitement ; le temps libéré part dans les scrutations.

Profil hôte (`--host-profile`) du worker : `[jit]` 15 %, `waitFor` 26 %,
`pairGateCycles` 18,6 %, `dspCatchupDeadline` 13,6 %. **Non fiable** : la
mesure TSC directe de `runPairChunk` (diagnostic, retiré) donne
`execUntilCycles` = 99,3 % du chunk, `notify()` 0,2 % (52 ticks par chunk,
1,57 M réveils de l'UC pour 4,0 M chunks), service et publication 0,5 %,
~12,6 ticks TSC (~3,5 ns) par cycle DSP. Le prédicat d'attente appelle
`pairGateCycles` 10 fois plus que le chemin avant chunk (50,8 M contre 5,5 M) :
ces calculs brûlent du CPU pendant l'attente, pas du débit.

Suite : répartir `execUntilCycles` entre code JIT, périphériques (ESSI, DMA,
HI08, horloge) et rappels du lien, par sondes TSC dans `source/dsp56300`.

### Sondes TSC, sauts dans les rattrapages, PFLUSH (2026-09-30)

Sondes TSC dans `source/dsp56300` (option CMake `DSP56K_TSC_PROBES`,
désactivée par défaut ; build `temp/cmake_probe`, `DSP56K_PROBE_LEVEL` 0 =
racines seules, 1 = tout). Chaque `rdtsc` impute le temps écoulé à la portée
la plus interne ; le coût des sondes est calibré et retiré au rapport. Coût
au mur : niveau 0 +5,6 %, niveau 1 +9,7 %.

Répartition du worker paire (niveau 0, avant correctifs) : producer 36,7 % du
mur (2,90 ns/cycle), mixer propre 28,1 % (2,99), mixer rattrapé dans une
livraison de lien 16,7 % à 5,13 ns/cycle (359 k rattrapages/s), parking
14,75 %, hors chunks 2,5 %. Niveau 1 : JIT natif et dispatch ~1,5 ns/cycle
(~31 %), périphériques ~27 % (1,1 à 1,9 M appels/s, 63 à 97 ns), PFLUSH
1,1 µs par appel (~2,2 %), vérification de mode ~0,8 %.

Cause du surcoût des rattrapages : `skipNopLoop` et `skipPollLoop` sont bornés
par `m_skipLimitCycles`, non nul seulement dans `execUntilCycles`. La boucle
de rattrapage MM avance le consommateur bloc par bloc avec `exec()` : aucun
saut n'y franchissait une sortie vers le dispatcher (13 M appels imbriqués de
`skipPollLoop`, 0 actif), chaque itération de scrutation y coûtait un bloc.

Correctifs :
- A : `DSP::setSkipLimitCycles`. La boucle de rattrapage MM borne les sauts au
  plus proche de sa cible, de son clamp et du prochain item hôte, seuls points
  où elle agit entre deux blocs.
- B : PFLUSH n'émet plus rien. Le cache d'instructions n'est jamais lu
  (`InstructionCache::fetch` jamais appelé).
- Relecture adverse : A et B exacts. Elle a trouvé PDRD (`$ffffad`) parmi les
  registres scrutables, alors que le port D de DSP2 suit son compteur
  d'instructions (horloge simulée pour la sonde de rôle du boot) : une boucle
  de scrutation sur PDRD sortait trop tard. Défaut antérieur, PDRD retiré ;
  aucune boucle PDRD en régime (moins de 1 % des lectures).

Mesures (banc non cadencé, MM paire active, 44,1 kHz, paires de 30 s
alternées contre le binaire d'avant les correctifs ; charge de fond 17 à
26 %, un serveur vite d'un autre projet sur 2,7 à 3,9 cœurs, stable) :
- A+B : médiane 78,7 % → 74,5 % (−4,2 points), chacune des 6 paires gagne
  (2,0 à 7,5).
- A+B+PDRD, livré : 73,6 % → 70,5 % (−3,1 points), chacune des 6 paires
  gagne (0,9 à 4,7).
- Sondes niveau 0, A+B : rattrapé 17,0 % → 13,3 % du mur, 5,21 → 3,93
  ns/cycle, cycles sautés 20 → 35 % (comme en propre), scrutations sautées
  0 → 1,3 M, lectures MMIO imbriquées 626 k/s → 313 k/s. Parking 14,5 → 18 % :
  le worker attend davantage l'UC.
- Écoute du motif dur (`MM_LISTEN_LONE_MIXER`, paire, avance par défaut,
  120 s) : 0 trou et 0 attente expirée, sur A+B comme sur le livré.

Reste : un cycle rattrapé coûte encore 6,1 ns par cycle exécuté contre 4,4 en
propre (boucle bloc par bloc, contrôle du backlog à chaque bloc). Suite :
répartir les périphériques (~27 %).

### Avances de paire nulles : plancher de l'avance UC (2026-09-30)

Avec `MD_PAIR_LEAD_US` et `MD_PAIR_UC_LEAD_US` à 0 (vides ou « 0 »), le MM
se figeait à la bascule paire : l'UC attend que le DSP le plus lent le
dépasse (`dspMin + ucLead > ucPos`), et sans avance DSP la porte UC d'un DSP
l'arrête au plus à la position de l'UC (`dspCatchupDeadline` arrondit vers
le bas). Trace (`MDMM_TRANSPORT_TRACE`) : bascule à `uc=16177844`, égal au
`oUc` du mixer, porte du mixer à 0, `ucLead=0.000`, UC dans l'attente
« lead » 99 % du temps, ~0 % CPU. La retenue DSP2 (`MD_PAIR_HOLD_DSP2_US`
≥ 0) ramène l'avance DSP à 0 pendant un aller-retour : avec une avance UC
nulle, même blocage tôt dans le boot.

Correctif 09737e8d : quand l'avance DSP peut être nulle (`m_pairDspLeadUc`
nul ou retenue DSP2 active), `schedTryHandoffPair` porte l'avance UC à au
moins un chunk worker plus le cycle arrondi (1153 cycles DSP, 11,35 µs) et
le signale sur stderr (`[pair] UC lead raised …`) : le DSP le plus lent a
toujours un chunk entier à courir pendant que l'UC attend. Politique livrée
(avance DSP 10 µs, retenue coupée) inchangée. Plancher tranché par Antoine :
chunk + 1. Écartés : 2 cycles DSP (vivant, mais UC et worker en ping-pong à
chaque instruction ; estimé à des centaines de secondes pour 10 s d'écoute,
non mesuré) et le quantum de 30 µs (« 0 » redeviendrait l'avance par défaut
et masquerait l'expérience). Au passage : `MDMM_TRANSPORT_TRACE` vide
n'active plus la trace, `MDMM_PAIR_AFFINITY` vide garde le placement par
défaut, un côté sans chiffres de `u,w` n'épingle plus sur le CPU 0, et
`MDMM_TRANSPORT` vide laisse le mode par défaut.

Mesures (`mmAudioFirmwareTest --listen`, paire, `MM_LISTEN_LONE_MIXER`,
10 s d'écoute après les 20 s de boot, garde 90 s) :

| Avances | Avant | Après |
|---|---|---|
| deux vides | figé, tué à 90 s (1,1 s CPU) | 26,0 s, 0 trou |
| deux à 0 | figé, tué à 90 s (1,2 s CPU) | 39,6 s, 0 trou |
| retenue 30 µs, UC à 0 | figé, tué à 90 s (1,8 s CPU) | 35,0 s, 0 trou |
| non définies (témoin) | 28,5 s, 0 trou | 26,9 s, 0 trou |

Garde-fous ctest (64210a93, 8552974c) : `mmPairZeroLeadFirmwareTest` et
`mmPairHoldZeroUcLeadFirmwareTest` (écoute de 2 s, délai 120 s), ~30 s
chacun ; sur le binaire d'avant correctif, tous deux expirent à 120 s.
`mmAudioFirmwareTest` échoue désormais si `MDMM_TRANSPORT=pair` est défini
sans que le worker paire démarre : un test resté en série passerait sans
exercer les portes.

Tranché le même jour (Antoine) : un `MDMM_TRANSPORT` inconnu (faute de
frappe, « pairs », « Pair ») compte comme non défini, avec l'avertissement
`[MD] MDMM_TRANSPORT="…" ignored: not serial, parallel or pair` ; il valait
Serial en silence. Seuls `serial`, `parallel` et `pair` choisissent un mode.
Test unitaire `mdTransportModeTest` (fixture de `mdAudioQueueTest`, donc dans
la gate).

Reste :
- Coût du plancher pour les expériences à avances nulles : 39,6 s contre
  26,9 s pour la même écoute (×1,5).

### Surcharges d'environnement vides : lecteurs de mdLib corrigés (2026-09-30)

Suite de la leçon PowerShell (Leçons dures). Toute surcharge MD/MM de mdLib
passe désormais par `source/elektron/md/mdLib/mdenv.h` (`envOverride`,
`envNumber`, `envCount<T>`) : non définie, vide ou illisible = non définie ;
une valeur illisible donne une ligne `[MD] ... ignored` sur stderr. Commits :
09737e8d (mdhardware.cpp, avances de paire) ; e2baad0d (fusion b31de12a) pour
`MD_JIT_OPTIMIZER`, `MDMM_LATENCY_BLOCKS` (device et plugin via
`Device::latencyBlocksFromEnvironment()`), `MDMM_RENDER_SPIN_US`,
`MD_MAX_DO_ITERATIONS` et `DSP56K_PROBE_LEVEL` ; 8135e012 (fusion bb1b3991)
retire les copies locales de mdhardware.cpp. Ne jamais recréer de copie
locale dans un fichier qui inclut mdenv.h : les appels non qualifiés
deviennent ambigus.

Mesuré avant correctif, variable vide créée par
`SetEnvironmentVariable($n, $null)` : `MD_JIT_OPTIMIZER` activait
l'optimiseur (défaut : coupé), `MDMM_LATENCY_BLOCKS` donnait à une
configuration MD neuve une latence de 0 au lieu de 2 blocs,
`DSP56K_PROBE_LEVEL` valait 0 au lieu de 1. Hors vide :
`MDMM_LATENCY_BLOCKS=-1` donnait 4294967295 blocs et
`MD_MAX_DO_ITERATIONS=64abc` valait 64 ; les deux sont désormais ignorées.

Reste :
- Lecteurs de test et de banc qui lisent encore `""` comme une valeur :
  `MM_LISTEN_SECONDS` (mmAudioFirmwareTest.cpp:246, `strtoull("") = 0` :
  `--listen` ne rend rien et passe), `MM_LISTEN_LONE_MIXER` (:279, présence :
  vide = activé), `MDMM_BENCH_TIMELINE` et `MDMM_BENCH_JOBSERIES`
  (mdParallelTransportBenchmark.cpp:789 et 817, présence), `DSP_LOG_INVALIDOP`
  (sous-module, jitblock.cpp:245, présence). Les entrées ctest fixent
  `MM_LISTEN_SECONDS=2` et ne sont pas concernées ; un lancement à la main
  avec une variable vide l'est.
- Arbitrage laissé : `MD_JIT_OPTIMIZER` garde la convention des drapeaux du
  code (toute valeur non vide qui ne commence pas par `0` active, « false » et
  « off » compris). Option stricte : `envNumber`, non-nombre ignoré avec
  avertissement.
- Mineur : une valeur illisible de `MDMM_LATENCY_BLOCKS` avertit deux fois
  (device puis plugin).

### Périphériques : répartition fine, lien toujours occupé (2026-09-30)

Sondes niveau 2 (`DSP56K_PROBE_LEVEL=2`), dans les périphériques : horloge
série (`esxiClock`, sans les slots qu'elle lance), slots ESSI TX et RX par
port (callbacks de trame compris), HDI08, timers, DMA (sondé à l'appel dans
`Peripherals56303::exec` : `Dma` ne garde pas de référence au DSP). Compteurs
de slots ESSI par port : slots actifs, trames rendues à l'hôte, slots RX sans
mot. Coût : 37 % du mur au niveau 2 (couverture 63 à 65 %) contre 10 % au
niveau 1 ; figures corrigées, répartition à lire en relatif.

Mesures (banc non cadencé, MM paire ; charge de fond : vite d'un autre projet
sur 4 cœurs) :
- Niveau 1 : périphériques 33 % du mur worker (mixer 132 à 142 ns par passe,
  producer 108 ns).
- Niveau 2, par passe. Mixer : RX lien ESSI0 ~54 %, reste propre ~12 %, RX
  codec ~10 %, horloge ~9 %, HDI08 ~9 %. Producer : TX lien ESSI0 ~44 %,
  HDI08 ~17 %, reste propre ~15 %, horloge ~10 %, DMA ~8 %. HDI08 coûte 27 ns
  par passe dans les rattrapages contre 9 à 12 en propre (non expliqué).
- Lien : un slot toutes les 96 cycles, ~1,04 M mots par seconde émulée. Le
  producer rend une trame à chaque slot (46,8 M slots, 46,8 M trames sur le
  run), le mixer reçoit un mot sur 93,6 % de ses slots : le lien n'est jamais
  inactif, sauter les slots vides ne rapporterait rien. Chaque slot
  échantillonne le registre TX que le DMA écrit à cet instant : une passe
  périphérique par slot et par DSP est intrinsèque.
- Coût : ~46 ns par mot côté TX (producer), ~63 ns côté RX (mixer), plus
  ~35 ns de frais fixes par passe (reste propre, horloge, HDI08, timers,
  DMA) sur ~2,9 M passes par seconde émulée.

Leviers :
1. Chemin par mot côté MD (`pushToInput`, `blockingPop`, `linkRxAvailable`) :
   deux dispositions par mot, ~4 divisions en double, date du consommateur
   recalculée. Refactor pur, 3 à 5 points estimés.
2. Passes sans travail : HDI08, timers et DMA servis à chaque passe.
   Échéance par périphérique, réveil HDI08 à l'arrivée d'un mot hôte. 5 à 8 %
   estimés, invasif.
3. Rattrapage : un cycle rattrapé coûte encore 2,9 ns contre 1,9 en propre.

Levier 1 fait : avec un seul thread pour les deux DSP, la sonde de
disponibilité RX laisse au `blockingPop` du même slot sa disposition et la
position du consommateur, et `pushToInput` passe au rattrapage la position du
producer déjà calculée pour l'échéance du mot. Exact (mêmes entrées, mêmes
doubles). A/B, 6 paires de 30 s : 74,0 % → 72,9 % (−1,0 point ; écarts par
paire de −0,6 à +2,3). Moins que les 3 à 5 points estimés : les ~110 ns par
mot tiennent surtout ailleurs (logique de slot ESSI, transfert DMA déclenché
à chaque slot, `std::function`, copies d'entrée de 64 octets).

Découpage d'un mot de lien après levier 1 (niveau 2, nouvelles catégories
`essiHostTx`, `essiHostProbe`, `essiHostRx`, `essiDmaRequest` ; 2 runs sous la
charge du build d'une autre session, répartition stable) : ~120 ns par mot.
Callback MD TX (`pushToInput`, tentative de rattrapage comprise) ~34 ns,
sonde RX (`linkRxAvailable`) ~21, pop RX (`blockingPop`) ~22, transfert DMA
du slot TX ~17 et du slot RX ~22, logique de slot ESSI elle-même ~2 à 3.
Côté MD ~77 ns, les deux tiers ; l'anneau n'a pas d'instruction verrouillée.
HDI08 coûte 26 à 34 ns par passe dans les rattrapages contre 11 à 15 en
propre. Pistes : fusionner sonde et pop RX en un appel hôte (~2 points),
chemin court DMA pour les transferts d'un mot déclenchés par ESSI (~1 à 2),
entrée construite dans l'anneau et test rapide du rattrapage (~1).

### Rattrapage MM sous `execUntilCycles`, fin de la série (2026-09-30)

Levier 3 ci-dessus. La boucle de rattrapage MM (`schedCatchUpDspToDsp`,
branche `bpGate`) avançait le consommateur bloc par bloc avec `exec()` pour
tester le backlog hôte entre deux blocs. Elle passe sous
`execUntilCycles(min(cible, clamp, prochain item hôte))`, comme la branche MD.
Côté DSP, le backlog ne croît que par une écriture HOTX : pendant le
rattrapage (`Dsp::setExecExitOnHostTx`), le callback d'écriture TX du MM
appelle `DSP::requestExecExit()`, et le trampoline rend la main après le bloc
en cours, là où la boucle bloc par bloc testait le backlog.

Sous-module : la cible d'`execUntilCycles` quitte la pile du trampoline x86
(le registre `g_counter` sur ARM) pour un membre du DSP, `m_execTargetCycles`,
relu après chaque bloc (autant d'instructions sur x86, un `ldr` de plus sur
ARM). `requestExecExit()` la met à 0 et coupe `m_skipLimitCycles` : aucun saut
NOP ou de scrutation ne franchit la sortie demandée. `setSkipLimitCycles`
(correctif A) disparaît, sans appelant. Test unitaire `execExitRequest`
(`jitunittests.cpp`, dans `dsp56300_unitTests`) : une écriture HOTX dont le
callback demande la sortie arrête `execUntilCycles` dans l'état du pas à pas
par `execJit()`, loin de sa cible.

Au passage : avec le correctif A, un saut de scrutation pouvait franchir une
écriture HOTX faite dans la même passe par le dispatch d'interruption (vecteur
rapide avant le bloc de scrutation), puisque la boucle ne testait le backlog
qu'après la passe. `requestExecExit` coupe ce saut. Cas non rencontré par le
test d'exactitude ci-dessous.

Exactitude : `mmAudioFirmwareTest` en série (`MDMM_TRANSPORT=serial`) est
déterministe (deux runs HEAD identiques) ; ses 18 valeurs imprimées (RMS au
repos, RMS, RMS à niveau nul et rugosité des six pistes) sont identiques à
HEAD sur deux runs. Chemin ARM relu, non compilé ici.

Mesures (sondes niveau 0, banc non cadencé, MM paire, 3 paires alternées de
30 s contre `f60b882a` ; machine chargée par le vite d'un autre projet,
réel ~97 %) :
- Rattrapé : 5,27 → 5,03 ns/cycle (−4,6 % ; chaque run nouveau sous chaque
  run de référence, 4,92 à 5,11 contre 5,19 à 5,38), 478 → 456 ns par
  rattrapage, part du mur worker 13,8 → 13,3 %.
- Propre (mixer, producer) : inchangé, 3,80 à 3,85 ns/cycle pour le mixer.
- Temps réel du banc : 96,7 → 96,8 %, dans le bruit.

Moins que les 2 à 3 points estimés. Un rattrapage exécute ~90 cycles
(12,6 M rattrapages pour 1,14 G cycles), un slot de lien : quelques blocs à
peine, la boucle par bloc pesait peu. L'écart restant entre rattrapé et
propre (5,0 contre 3,8 ns/cycle sous cette charge, ~3 % du mur) est un coût
fixe par rattrapage (~110 ns : entrée, dates en double, garde, backlog,
service hôte), à attaquer seulement en rattrapant moins souvent, ce qui
change la synchronisation.

Série arrêtée ici (Antoine, 2026-09-30) : MM paire ~70-73 % du temps réel sur
cette machine hors charge, rendements décroissants. Pistes identifiées non
menées : HDI08 servi seulement à l'arrivée d'un mot hôte (1 à 2 points,
invasif), chemin court DMA pour les transferts d'un mot déclenchés par ESSI
(~1 point), coût fixe par rattrapage (ci-dessus).

## Leçons dures

- PowerShell 7.6 : `[Environment]::SetEnvironmentVariable($v, $null)` crée
  une variable VIDE que l'enfant voit (`getenv` rend `""`). Avec
  `MD_PAIR_LEAD_US=""` et `MD_PAIR_UC_LEAD_US=""`, les deux avances de paire
  valaient 0 : UC et worker s'attendaient à la bascule, le MM se figeait au
  boot. `MDMM_TRANSPORT=""` valait Serial. Chaque appel d'outil part d'un
  environnement vierge : ne rien « effacer », ou `Remove-Item Env:`. Une
  soirée de fausse régression, bissectée à tort dans le code (2026-09-30).
  mdLib lit désormais le vide comme non défini (section « Surcharges
  d'environnement vides ») ; l'habitude reste, les lecteurs de test non.


- `--host-profile` du banc (RIP échantillonné, sans pile) se trompe sur le
  worker paire : JIT 15 % là où la mesure TSC en trouve ~99 % dans
  `execUntilCycles`. Vérifier toute piste du profil hôte par une sonde TSC.

- `mdParallelTransportBenchmark --mode X` écrase `MDMM_TRANSPORT`. MM :
  `--mode pair`, et vérifier `active=1` dans la sortie. Une soirée de mesures
  perdue en série sans le voir (2026-09-29).

- Le test firmware `mdAudioFirmwareTest` passe en parallel : il ne déclenche
  PAS le trafic du Controller (status/dump requests, retries wall-clock 2 s)
  qui provoque le burst. Vert trompeur. Seul `mdParallelTransportFirmwareTest`
  (ou pluginTester sur le VST3) le reproduit.
- Chaîne de validation : build CIBLÉ des exécutables de gate, `ctest`
  SEULEMENT si `build_exit=0` (les 4 erreurs préexistantes de mdLibTest
  masquaient des builds rouges → tests sur binaires périmés).
- `hdi08().writeRX` bloque sur ring plein (Lock=true) : jamais depuis le
  worker sans `dataRXFull()`.
- Positions publiées avant chaque attente, des deux côtés, sinon gates
  périmées = attentes qui expirent.

## Commandes

```powershell
$env:GEARMULATOR_MD_FIRMWARE_BIN = "$env:LOCALAPPDATA\Programs\Gearmulator-Elektron\elektron_sps1-1uw_os1.63.bin"
$env:GEARMULATOR_MM_FIRMWARE_BIN = "$env:LOCALAPPDATA\Programs\Gearmulator-Elektron\elektron_sfx6-60_os1.32b.bin"
cmake --build temp\cmake_vs22 --config Release -j 6 --target mdAudioFirmwareTest mmAudioFirmwareTest mdMidiTimingTest mdHostRxTimingTest mdAudioQueueTest mdParallelTransportFirmwareTest mdUartRegisterTest mdUartCpuInterruptTest mdTurboMidiUnitTest mdSdsTransferTest mdTransportScorecardTest
cd temp\cmake_vs22
ctest -C Release -R "^(mdAudioFirmwareTest|mmAudioFirmwareTest|mdMidiTimingTest|mdMidiTimingFirmwareTest|mdHostRxTimingTest|mmSineFirmwareTest|mmSineMidiFirmwareTest|mdAudioQueueTest|mmPairZeroLeadFirmwareTest|mmPairHoldZeroUcLeadFirmwareTest)$"
ctest -C Release -V -R "^mdParallelTransportFirmwareTest$"
```

pluginTester (VST3 MD dans `bin\plugins\Release\VST3\`, ROM copiée à côté de
la DLL) : `pluginTester.exe -plugin "<...>\Gearmulator MD.vst3" -seconds 30
-blocksize 128 -samplerate 44100` ; `MDMM_TRANSPORT=parallel` pour le mode
threadé. Toujours lancer avec garde-temps (`Start-Process` + `WaitForExit`).
