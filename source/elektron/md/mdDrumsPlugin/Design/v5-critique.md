# Critique de la v5 (2026-10-06)

Maquette : `v5-test.html` (PISTE, MIX, SORTIES). Stade : affinage, avant l'implémentation RmlUi. Contrastes
calculés (formule WCAG 2.x) sur la palette de `v5-ui.css`.

## Impression d'ensemble

Grille tenue et sobre : la fenêtre se lit comme une façade de machine, sans les cartes ni les pastilles d'avant.
Le plus gros gain possible n'est pas visuel : choisir une machine et écouter une piste, les deux gestes de base
d'un module de batterie, n'ont pas encore de commande à la hauteur.

## Utilisabilité

| Constat | Gravité | Recommandation |
|---|---|---|
| Choisir parmi ~135 machines passe par une liste déroulante de 240 px, sans familles ni pas-à-pas | Critique | Un navigateur de machines par famille (TRX, EFM, E12, P-I, GND, ROM…), ouvert par-dessus les pages, plus des flèches précédent/suivant sur le sélecteur pour essayer vite |
| Écouter une piste demande de cliquer l'écran FRAPPE ; seul l'indique « cliquer pour jouer », en 10 px et 3,1:1 | Critique | Une case PLAY visible dans l'en-tête de piste (8 modules y sont vides), et le numéro de piste de la liste qui joue au clic |
| Deux volumes sans distinction : LEVEL (en-tête, fader du MIX) et VOL (page ROUTING) | Modérée | Nommer « LEVEL kit » et « VOL machine », ou ne montrer LEVEL qu'au MIX et dans la liste, VOL que dans ROUTING |
| La destination du LFO ne s'affiche que dans le titre de l'écran et ne se change nulle part | Modérée | Deux cases cliquables dans la rangée du LFO (une est vide) : piste et paramètre de destination |
| Les envois DEL et REV du MIX se tirent mais ne font rien (pas d'effets maîtres) | Modérée | Les masquer jusqu'aux effets maîtres, ou les verrouiller avec la mention « à venir » |
| Les barres de niveau de la liste des pistes ressemblent à des curseurs mais ne réagissent pas | Mineure | Les rendre déplaçables (même valeur que LEVEL) ou les remplacer par un vu-mètre |
| Pas de solo, geste courant pour régler une batterie piste par piste | Mineure | Une case S à côté de M (liste, MIX) |
| Ni réglage fin ni saisie directe de valeur ; le double-clic remet la valeur par défaut | Mineure | Maj pour le réglage fin, molette, saisie au clavier ; documenter les gestes dans une infobulle |

## Hiérarchie visuelle

- **Ce qui attire l'œil d'abord** : la forme d'onde de FRAPPE, traits blancs serrés sur fond sombre, puis les
  courbes turquoise. Ce n'est pas l'idéal : les potards et leurs valeurs, qu'on manipule, devraient passer avant.
  Atténuer la forme d'onde (#e8e8e8 vers ink-dim) laisse l'enveloppe turquoise porter la lecture.
- **Parcours de lecture** : liste à gauche, en-tête, puis SYN, EFX, ROUTING de haut en bas, écrans à droite. En Z,
  clair, et calé sur l'ordre des pages de la machine.
- **Emphase** : les titres de page (SYN, EFX, ROUTING) en 11 px semi-gras se distinguent à peine des étiquettes
  des potards en 11 px. Passer les titres en 12 px, encre pleine, ou les tourner dans une colonne de titre à gauche.
  La zone vide de l'en-tête (8 modules) affaiblit la ligne la plus importante de la vue.

## Cohérence

| Élément | Écart | Recommandation |
|---|---|---|
| Sélection | Quatre traitements : barre à gauche (liste), barre en haut (MIX), trait en bas (onglet), aplat plein (chip) | Barre d'accent pour « sélectionné » ; aplat plein réservé aux interrupteurs (chip, mute, futur solo) |
| Accent turquoise | Sert à la sélection, aux courbes, aux sorties prises, à la cible du LFO et aux chips actifs : le sens se dilue | Garder l'accent pour sélection et état actif ; sorties prises et cible LFO en encre pleine soulignée |
| Valeurs | 14 px sous les potards, 13 px au MIX, et le niveau d'en-tête dans une légende en capitales (« LEVEL 100 », 10 px) | Un style de valeur unique (14 px), y compris pour LEVEL |
| Tailles de potard | 48, 40, 36 et 24 px | Deux tailles : 48 (paramètres) et 24 (secondaires) ; PAN et LEVEL en 48 dans des cases 2 × 2 |
| Approche des capitales | 1, 2 et 3 px selon le rôle | Deux valeurs : 1 px (étiquettes), 2 px (légendes et titres) |

## Accessibilité

- **Contraste du texte**
  - Encre sur fond : 14,05:1 ; étiquettes (ink-dim) : 6,44:1. Les deux passent le niveau AA (4,5:1).
  - Turquoise sur fond : 8,68:1 ; texte sombre sur chip actif : 9,29:1 ; « M » sur mute : 7,36:1. Tout passe.
  - **Échec** : les indications en ink-faint (#6c6f75), pied de liste, « cliquer pour jouer », « sans effets
    maîtres ». Elles tombent à 3,12:1 sur le fond et 3,49:1 sur les écrans, en 10-11 px. Passer à #8a8d93 donne
    4,73:1 et 5,29:1.
- **Contraste des commandes** (3:1 visé pour un composant)
  - La piste des potards (#4a4d52) est à 1,85:1 : la course du potard se devine mal. #6a6d73 donne 3,03:1.
  - Les filets (1,42:1) et le fond de ligne sélectionnée (1,12:1) restent décoratifs : la barre d'accent porte la
    sélection, ça va.
- **Cibles** : souris, pas tactile. Toutes les zones cliquables font au moins une case de 40 × 40 px (mute, chips,
  envois) ou 80 × 40 (onglets), au-dessus des 24 px du critère WCAG 2.2.
- **Lisibilité** : 4 tailles en 10 px et 12 en 11 px. Dans une fenêtre d'hôte réduite à 75 %, le 10 px tombe à
  7,5 px. Monter le minimum à 11 px et prévoir une échelle d'interface (100, 125, 150 %).
- **Alternatives** : les écrans sont des dessins ; leurs valeurs existent déjà en texte sous les potards, rien ne se
  perd.

## Ce qui marche

- **La grille** : 32 × 19 modules de 40 px, aucune zone décalée (mesuré). Les 16 tranches du MIX en 80 px
  remplissent exactement la largeur. Le rythme est régulier et l'ensemble se lit d'un bloc.
- **Les 8 encodeurs en rangée** reprennent la façade du Machinedrum, page par page : la mémoire des gestes se
  transpose.
- **Le lien source-cible du LFO** : l'étiquette du paramètre modulé passe en turquoise. On voit ce que le LFO touche
  sans ouvrir de menu.
- **Les sorties numérotées** : piste 01 = sortie 01, rien à chercher. L'onglet SORTIES dit l'état sans pastilles.
- **Les chiffres à largeur fixe** : les valeurs qui défilent ne font pas bouger leurs voisines.

## Recommandations prioritaires

1. **Navigateur de machines** : c'est le geste de base d'un module de batterie, aujourd'hui caché dans une liste
   déroulante. Familles en colonnes, prévisualisation au survol si le moteur le permet, pas-à-pas au clavier.
2. **Écoute visible** : case PLAY dans l'en-tête de piste et clic sur le numéro de piste. Ça occupe la zone vide de
   l'en-tête et supprime une commande cachée.
3. **Contraste et tailles** : ink-faint à #8a8d93 (4,73:1), piste des potards à #6a6d73 (3,03:1), aucun texte sous
   11 px, échelle d'interface. Trois jetons à changer, toute l'interface en profite.
