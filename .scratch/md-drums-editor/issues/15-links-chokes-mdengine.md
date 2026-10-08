# Links et Chokes dans mdEngine

Type: task
Status: open
Assignee: —
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
