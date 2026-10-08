# Push 2 face aux paramètres d'un VST3 dans Live

Type: research
Status: resolved
Assignee: c0remusic (agent de recherche)
Blocked by: —

## Question

Comment Live 12 montre-t-il les paramètres d'un plug-in VST3 sur Push 2 (mode contrôleur), et qu'est-ce qu'un
plug-in peut y changer ?

- Limite de paramètres exposés automatiquement (128 ?), rôle de « Configure », ordre des paramètres, banques de 8,
  longueur des noms affichés sur l'écran de Push.
- Effet des groupes ou units VST3, des noms de paramètres changeant à la volée, des paramètres ajoutés après coup.
- Un plug-in peut-il définir ses propres banques pour Push, ou suivre une sélection (par exemple la Track affichée
  dans l'éditeur) ?
- Pads en mode Drum avec un instrument plug-in : notes envoyées (36-51 attendues), vélocité, couleurs.
- Ce qui est mémorisé dans le set (Configure) et ce qui se perd quand le plug-in change ses paramètres.

MD Drums aura plus de 500 paramètres (16 Tracks × 34, plus 32 de master). Notes 36-51 déclenchent les Tracks 1-16.

Sources : manuels Live et Push, base de connaissance Ableton (outils `mcp__Ableton_Knowledge__*`), puis le web en
datant chaque source. Livrable : `research/02-push2.md`.

## Answer

Push 2 montre la liste « Configure » du plug-in, telle que Live l'ordonne, découpée en banques de 8 nommées
« Bank 1 » à « Bank 16 » : le plug-in ne peut ni nommer ces banques, ni les définir, ni choisir celle qui s'affiche.
Au-delà de 64 paramètres, MD Drums s'ouvre avec un panneau vide (« No parameter mapped » sur Push) ; la liste tient
au plus 128 paramètres par instance (128 cases fixes dans les sets Live 12.2.1), mémorisés dans le set par ID VST3 et
position. Le design du ticket 11 doit donc livrer une liste prête (configuration par défaut ou preset de Rack), en
128 paramètres au plus. Les units VST3 (groupes JUCE) n'y changent rien. Ajouter des paramètres en fin de liste ne casse
rien ; changer un ID string désactive la case. Le renommage à la volée n'est pas confirmé en VST3. Sur l'écran, le nom
s'affiche en capitales, environ 10 lisibles sur 111 px ; une valeur texte qui contient des chiffres est réduite au
nombre (« PTCH 64 » devient « 64 », « ROM-01 » devient « -1 »). Pour des banques nommées ou qui suivent la Track de
l'éditeur, il faut passer par des Macros de Rack (16), un device Max for Live (`live.banks`) ou des paramètres relais.
Le mode Drum de Push exige un Drum Rack autour de MD Drums, avec une chaîne en Receive « All Notes » : les 16 pads par
défaut envoient alors 36-51, vélocité incluse ; MD Drums seul sur la piste donne le mode mélodique (C1 = 36 en bas à
gauche, en gamme).

Détail : research/02-push2.md
