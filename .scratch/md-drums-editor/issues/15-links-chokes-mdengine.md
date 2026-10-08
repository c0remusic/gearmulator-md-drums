# Links et Chokes dans mdEngine

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-08)
Blocked by: —

## Question

Jouer les Links et les Chokes dans mdEngine comme OS 1.63 (`research/06-groupes.md`, contrat du ticket 06) : deux
tables de 16 octets, un drapeau Choke distinct du Mute, un Link à un seul niveau (non récursif, auto-référence sans
effet), la coupe et la levée du drapeau au rythme de l'OS (même tick ou tick suivant selon les numéros de Track), une
Track en Mute qui perd ses Hits, et le level recalé sur la valeur du Kit à chaque Hit. Les exposer dans
`mdDrums::Engine`.

Commencer par l'expérience propre que propose la recherche : une Track étouffée puis refrappée perd-elle jusqu'à un
tick d'attaque sur le firmware ? Reproduire ce que le firmware fait.

Fini quand : comparaison au firmware, sur le modèle de `mdEngineFirmwareTest`, d'un Kit d'usine à Choke (charleston
fermé vers ouvert, Track 9 vers 10), d'un Link et d'une Track en Mute frappée : échantillons identiques ; tests ajoutés
au garde-fou ; latence note→son et gigue mesurées avant et après ; garde-fou vert ; poussé sur `main`.

## Answer

### L'expérience, sur le firmware

`mdTrigLatencyFirmwareTest --groups` joue six scénarios (`g_groupScenarios`, `mdLibTest/hitParameters.h`) sur des
Tracks neuves (le bruit et les oscillateurs d'une voix gardent leur état d'un Hit à l'autre), `hitParameters`, level
100, chaque Track sur sa sortie C à F. A et B portent aussi le mix Main : les queues des scénarios précédents y
fuient, d'où C à F. Mesures (sorties 24 bits, échantillons exacts) :

- **Choke dans le tick du Hit** (Kit 1, CH 9 → OH 10) : l'OH s'arrête au bord de bloc, 36 échantillons avant le
  premier échantillon du CH (37 pour un P-I-MT). DSP1 applique les mots du UC un bloc avant que l'audio de DSP2 du
  même tick ne lui arrive.
- **Track étouffée puis refrappée : oui, elle perd son attaque.** Ses mots du tick du Hit sont encore ceux du Choke
  (OS $20b210 avant $20b3e2) : un TRX-BD étouffé puis refrappé reste muet 10 blocs (320 échantillons) au premier
  enregistrement, 8 blocs au second, puis le reste de son Hit arrive tel quel.
- **Choke vers une Track plus basse** (6 → 5) : coupée au tick suivant, ici 3 blocs après le début du Hit source.
- **Le tick du firmware n'a pas de période fixe** : 3 à 11 blocs mesurés, selon la charge du UC.
- **Link** (13 → 14, vélocité 80) : les deux Tracks sonnent au même échantillon, la cible à la vélocité de la
  source. Un Link ne va qu'à un niveau.
- **Track en Mute frappée** : rien ne sonne, ni elle, ni son Link ; son Choke ne coupe pas sa cible.
- **Les voix simultanées du firmware se poussent** : son P-I-MT joué après un TRX-BD diffère de −79 dB du même
  Hit joué après un EFM-CB ; les deux MT d'après un BD sont identiques entre eux. Les Tracks du moteur sont
  indépendantes : ses trois MT sont identiques au MT d'après le CB.

### Ce que joue mdEngine (`HostModel`)

- `setLink`, `setChoke` (0-15, `kOff`) : deux tables de 16 octets ; drapeau Choke (`m_choked`, le $1001510 de l'OS)
  distinct du Mute.
- `trigger` : une Track en Mute perd son Hit, donc ni Link ni Choke ($20ccf6) ; le Link frappe sa cible à la même
  vélocité dans le même bloc, une seule fois (sa propre cible de Link ne part pas) ; un Link vers une Track en Mute
  perd aussi son Hit (le Mute de MD Drums perd tous les Hits ; l'OS, lui, ne vérifie pas le mute utilisateur de la
  cible, recherche 06, « Not settled »).
- Chaque Hit recale le level lissé sur le level du Kit, cible et valeur lissée ($20b022, lu sur l'OS : `move.w
  level << 7`).
- Le pas « mute group » du tick ($20b3a2) après les mots de la Track : cible plus haute coupée dans le bloc du Hit,
  cible plus basse au tick suivant du moteur, Track frappée pendant son Choke muette jusqu'au tick suivant, puis
  drapeau levé ; auto-référence sans effet.
- `mdDrums::Engine::setLink`, `setChoke` ; `setMute` documenté (perd les Hits).

Deux instants diffèrent du firmware, tous deux fixés par son UC et non par le son :
- la coupe dans le tick du Hit arrive dans le bloc du Hit, un bloc après celle du firmware (la cible sonne 32
  échantillons de plus). La reproduire retarderait chaque Hit d'un bloc (+0,73 ms de latence) ; refusé.
- ce que l'OS change au tick suivant arrive au tick suivant du moteur (11 blocs), pas à celui du firmware (3 à 11
  blocs selon sa charge).

### Comparaison (`mdEngineFirmwareTest`, fixture `mdEngineFirmwareGroups`)

Chaque scénario sur un moteur neuf, les Hits suivants placés au bloc qui laisse le moindre résidu (les notes du
firmware attendent son tick, celles du moteur partent au bloc suivant). Sur les échantillons où les deux sonnent ou
se taisent ensemble :

| Scénario | Résidu | Échantillons où un seul sonne |
|---|---|---|
| Choke 9 → 10 (Kit d'usine 1) | identique, identique | 32 (un bloc, la coupe) |
| Track étouffée refrappée | identique ; MT −76,5 dB | 256 (la coupe, puis le retour au tick) |
| Refrappée sans Choke | identique ; MT −76,5 dB | 0 |
| Link 13 → 14, vélocité 80 | −74,0 et −67,7 dB | 0 |
| Track en Mute frappée | identique ; deux sorties muettes des deux côtés | 0 |
| Choke 6 → 5 | −114 dB ; identique | 255 (la coupe au tick) |

Plancher des Tracks qui sonnent ensemble : −60 dB (`g_concurrentResidualFloorDb`), au-dessus du −67,7 dB mesuré ;
bornes des échantillons à part par scénario (`apartBlocks`). 19 s de test.

### Latence note→son

`mdDrumsEngineTest`, TRX-BD, 48 notes à des échantillons pseudo-aléatoires de blocs hôte de 256, jusqu'au premier
échantillon non nul. Avant et après : 37 à 67 échantillons, moyenne 51,4 (1,16 ms), gigue 30 (0,68 ms). Inchangée : les Links, Chokes et
le recalage du level ne touchent pas au chemin du trigger. Le TRX-BD sonne un bloc après son trigger (un CH, dans le
bloc même). Les 0,48 ms du `CLAUDE.md` venaient de Machinemodule, qui compte depuis le début du bloc de 32 qui
contient la note.

Tests au garde-fou : `mdEngineFirmwareTest` (avec la fixture `mdEngineFirmwareGroups`) et `mdDrumsEngineTest`
(Link, Choke, Mute par `mdDrums::Engine`, et la latence).
