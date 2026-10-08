# Banques Push 2 de MD Drums

Type: grilling
Status: open
Assignee: —
Blocked by: 02, 06

## Question

Que montrent les 8 encodeurs de Push 2 sur MD Drums, et dans quel ordre ?

- Banques fixes par Track (SYN, EFX, ROUTING), banques qui suivent la Track affichée dans l'éditeur (paramètres
  relais), ou configuration laissée à l'utilisateur via « Configure ».
- Place des Master effects, de Level, Mute, Solo, Out.
- Paramètres à ajouter au JSON et noms courts lisibles sur l'écran de Push. Le ticket 06 fixe 592 paramètres, par
  Track : Machine, 24 paramètres, 5 LFO, Level, Mute, Solo, Out ; puis 32 Master. Résolu avant le port (07), ce
  ticket peut ajouter ses paramètres relais au JSON du premier push ; après, ils viennent en `version` 1, en fin de
  liste.
