# Links et Chokes dans l'éditeur

Type: grilling
Status: resolved
Assignee: c0remusic (session du 2026-10-09)
Blocked by: 06, 15, 26

## Question

Où et comment l'éditeur règle-t-il le Link et le Choke de chaque Track ?

Faits :

- Le Device les joue comme OS 1.63 (ticket 15, prouvé contre le firmware) et les reçoit en `$65`/`$66` ; ce sont des
  valeurs du Kit, pas des paramètres hôte (ticket 06). Le Controller ne les envoie pas encore ; `playedKit` les tient
  dans son Kit de base, donc un Kit chargé les porte déjà, dans les Slots comme dans le set.
- Le HANDOFF n'en dit rien. La bande de tête de l'onglet Track laisse libres les colonnes 8 à 11 (x 576-880) sur les
  lignes de « Family » et « Output » (y 84 et 124).
- Le menu de cible du LFO (grille de 16 numéros de Track en deux rangées de 8, cellule choisie remplie d'accent) existe
  déjà et ferme sur Esc.
- Un Link va à un seul niveau ; une Track vers elle-même ne fait rien ; Link et Choke d'une Track en Mute ne partent pas.

Sortie : placement, forme du menu, ce qu'affichent les autres vues, comportement face au Kit et à l'hôte ; ticket de
construction.

## Answer

Grilling du 2026-10-09 (l'utilisateur a pris les recommandations).

- **Place** : bande de tête de l'onglet Track, colonnes 8-11 (x 576-880), aux hauteurs de Family et Output (y 84 et
  124), même typo (14 px ink-dim, la cible en ink) : « Also strikes track 05 ▾ » ou « Strikes no other track ▾ »,
  « Silences track 10 ▾ » ou « Silences no track ▾ ». Un clic sur l'une ou l'autre ouvre le menu.
- **Menu** : un seul, « Links and Chokes », par-dessus l'écran Hit (400 × 216), sur la géométrie du menu de cible du
  LFO : en tête « Track 01 », « Done » ; sections « Strikes » et « Silences », chacune avec « Off » et 16 numéros sur
  deux rangées de 8, la cible remplie d'accent, la Track elle-même grisée et inerte. Done ou Esc ferment ; un choix ne
  ferme pas (on règle souvent les deux).
- **Kit et hôte** : valeurs du Kit (ticket 06) : Save s'allume, le Slot et le set les gardent ; pas de paramètre hôte,
  ni automation ni undo de Live ; un changement part au Device en `$65`/`$66` au bloc audio suivant.
- **Ailleurs** : rien ; les Hit lights montrent déjà un Link qui frappe.
- **Son** : aucun au choix d'une cible.

Ticket créé : 30 (construction).
