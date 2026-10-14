# Où une Choke coupe : HostModel le dit, le moteur aligne

Type: task
Status: resolved
Assignee: c0remusic (session du 2026-10-14)
Blocked by: 19, 29

## Question

Revue d'architecture du 2026-10-14 (candidat retenu par l'utilisateur, « Strong ») : `mdDrums::Engine` refait après
coup les règles de Link, de Mute et de Choke de `HostModel`. `Engine::trigger` recalcule les Tracks frappées et retient
le retard de la Choke depuis `link()`, `choke()` et `muted()` ; `renderBlock` devine qu'une Choke a eu lieu sur un front
du drapeau `choked()`. Cette copie a dérivé de `HostModel`, qui décide *quand* une Choke éteint sa cible pendant le
tick. Trois écarts, relevés à la lecture, aucun couvert par un test :

1. **Cible plus basse** : `HostModel` pose le drapeau au bloc du Hit mais ne met les mots de la cible à zéro qu'au tick
   suivant (11 blocs, comme l'OS). Le moteur coupe la voie au front du drapeau, puis continue d'ajouter la cible avec
   ses mots inchangés : un trou jusqu'à 30 échantillons dans une Track qui joue encore.
2. **Choke d'un Link** : `HostModel` applique la Choke de chaque Track frappée, Link compris ; le moteur ne retient le
   retard que pour la Choke de la Track jouée. La cible d'un Link n'est jamais coupée là où sonne le Hit lié.
3. **Retard resté** : `chokeDelay` ne s'efface que sur un front ; une Choke vers une Track déjà coupée laisse un retard
   pour un front plus tard.

Et un écart accepté au ticket 19 : une cible qui attend moins que le Hit qui la coupe s'arrête jusqu'à 31 échantillons
*avant* ce Hit. Le ticket 36 va faire différer les retards jusqu'à 63.

## Answer

Choix de l'utilisateur (2026-10-14), sur recommandation :

- **Cible plus basse** : aucune coupe dans les voies ; la cible s'arrête au tick suivant, à son propre retard, sans trou
  (l'heure de ce tick est arbitraire, 3 à 11 blocs sur le firmware).
- **Coupe exacte dans les deux sens** : la Choke coupe sa cible exactement là où sonne le Hit qui coupe. Une cible qui
  attend plus perd sa queue après ce point (comme aujourd'hui) ; une cible qui attend moins garde, dans le bloc de la
  Choke, les gains de son bloc précédent jusqu'à ce point. Mémoire de taille fixe, rien d'alloué sur le thread audio.
- **HostModel le dit** : `HostModel::trigger` renvoie les Tracks qu'il a frappées (la Track et son Link, sans les
  muettes) ; chaque bloc laisse un rapport de taille fixe, la cible coupée dans ce bloc et la Track dont le Hit l'a
  coupée, écrit là où `HostModel` met ses mots à zéro. Le moteur ne lit plus `link`, `choke`, `muted` ni `choked` ;
  `wasChoked` et `chokeDelay` disparaissent. Modification locale de mdEngine, notée dans son README.
- **Tests d'abord** dans `mdDrumsEngineTest` : cible plus basse sans trou, Choke d'un Link coupée où sonne le Hit lié,
  retard resté, cible qui attend moins que le Hit ; la sortie propre de la cible comparée au moteur sans précision à
  l'échantillon, décalée. Latence avant/après (inchangée attendue) ; `mdEngineFirmwareGroups` reste vert, les
  échantillons de `HostModel` ne bougent pas.
- **Ordre** : après le commit de l'audit de simplification (qui touche `mdDrumsEngine.cpp`), avant le ticket 36.

Construit le 2026-10-14 :

- `HostModel::trigger` renvoie le masque des Tracks frappées ; `HostModel::cutBy(t)` dit, pour le bloc rendu, la Track
  dont le Hit a coupé `t` (rempli dans `applyChoke` pour une cible plus haute, vidé à chaque bloc). `muted()` et
  `choked()`, que plus rien ne lisait, disparaissent de `HostModel`.
- `Engine::State::renderBlock` : pour chaque coupe rapportée, la voie de la cible est vidée de `start + delay[Hit]`
  jusqu'au plus long retard (`SampleAccurateDelay`) ; une cible qui attend moins que le Hit garde les mots de son bloc
  précédent jusqu'à ce point (`lastWords`), sauf si ce Hit la frappe aussi. `Engine::trigger` pose l'attente de chaque
  Track du masque. `chokeDelay` et `wasChoked` disparaissent.
- **Vérifié** (`mdDrumsEngineTest`, `chokes()`, E12-RC tenu sur sa sortie propre, contre le même moteur sans Choke) :
  avant le correctif, la cible qui attend moins que le Hit se taisait 27 échantillons trop tôt ; après, les cinq cas
  (cible plus haute qui attend plus, qui attend moins, cible plus basse, Choke d'un Link, Choke après une Choke sur une
  Track déjà coupée) sont identiques au moteur sans Choke jusqu'au point de coupe, et muets à partir de lui, exactement
  là où sonne le Hit qui coupe ; la cible plus basse se tait 251 échantillons plus tard, au tick suivant, sans trou.
  Latence inchangée : 85 échantillons de la note au son sur TRX-BD, gigue 0 ; Main identique au mélangeur du moteur
  (`equivalence`).
