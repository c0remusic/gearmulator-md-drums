# La conversion d'un fichier lâché

Type: grilling
Status:
Blocked by: 01

## Question

Ce qu'un fichier devient pour le DSP voix (mono, codes de 12 bits par la table de l'OS, un rate en fraction de
44,1 kHz) : les formats acceptés (WAV et AIFF tels que construits, ou FLAC et MP3 aussi en changeant
`source/juce.cmake`), les rates (garder de 4 à 44,1 kHz, rééchantillonner au-dessus, ou la moitié comme l'OS au-dessus
de 48), le stéréo (somme, gauche seule), une boucle lue dans le fichier (chunk `smpl` d'un WAV) ou aucune, le gain
(tel quel, normalisé), et un fichier plus long que la place libre (refusé, coupé, rate réduit).
