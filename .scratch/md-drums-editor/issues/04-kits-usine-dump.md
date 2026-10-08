# Kits d'usine et octets non lus du dump de kit

Type: research
Status: resolved
Assignee: c0remusic (agent de recherche)
Blocked by: —

## Question

D'où viennent les valeurs du Kit 1 quand OS 1.63 démarre, la flash en contient-elle d'autres pour remplir la Bank,
et que doit écrire un writer de dump de kit dans les octets que `parseKitDump` ne lit pas ?

- Source du Kit 1 au boot (section de la flash, code d'init de l'OS) ; valeurs lues sur firmware : echo
  16 0 32 27 0 44 0 71, reverb 0 0 68 50 1 82 71 109, EQ 64×7 127, dynamix 127×6 0 0.
- Kits d'usine dans `elektron_sps1-1uw_os1.63.bin` : combien, où, quel format (`mdEngine/tools/mdfw/Firmware.h`).
- Octets d'un dump `$52` que `parseKitDump` (`mdLib/mdsysexautomation.cpp:1224-1308`) ignore : octets 5-35 de
  chaque bloc LFO de 36, les 37 octets 0x4a7-0x4cb après le master, octets de version de l'en-tête. Valeurs sûres
  pour un dump qu'une vraie Machinedrum accepte.
- Helpers déjà là pour un writer : `append7Bit`, `read7Bit`, `finishDump` (anonymes dans
  `mdsysexautomation.cpp:146-229`) ; `makeKitDump` de test (`mdJucePlugin/mdAutomationTestSupport.h:405-462`).
- Format d'un fichier Bank unique de 64 Kits qu'outils et machine réelle savent lire (dumps concaténés ?).

Lecture seule : pas de build, pas d'exécutable firmware (des scripts Python qui lisent la flash sont permis).
Livrable : `research/04-kits.md`.

## Answer

Le Kit 1 vient du fichier d'OS lui-même : la section 4 du conteneur (flash $0d761d, aPLib, 512 Kio) est une image
d'usine de la mémoire patch, que le bootloader décompresse quand le marqueur `DAVE` manque ou au FACTORY RESET (la
section 3 sert aux machines non UW). Son Kit de travail est TRX UW, identique octet pour octet au Kit 1 stocké, effets
master cités compris. Elle contient 16 Kits d'usine (TRX UW à SEACLONES), en enregistrements de $460 octets à
+$8ca + $460·n ; les Slots 17 à 64 sont vides. `md::fw::parseContainer` décompresse déjà cette section : le ticket 10
peut remplir la Bank d'usine et le Kit de départ sans firmware.
Le writer écrit l'en-tête `52 04 01 <slot>` (l'OS 1.63 refuse une version hors 1-4, et la version 01 du `makeDump` de
test lui fait lire l'ancienne mise en page), les octets 5 à 35 de chaque bloc LFO à zéro sauf $0000029a aux octets 28
à 31, les 37 octets de $4a7 comme 16 groupes de trig puis 16 de mute ($ff = OFF) en 7 bits, les octets hauts des mots
de machine à 0, et un nom terminé par NUL, complété de zéros, jamais commencé par $7f (marque d'un Slot vide).
Fichier Bank : 64 dumps de 1233 octets concaténés, slots 0 à 63 ; une vraie Machinedrum range chacun dans le Slot
qu'il nomme sans toucher le Kit joué, Machinemodule le lit comme une banque. À l'import, garder les $52 en version 3,
4 ou 64.

Détail : research/04-kits.md
