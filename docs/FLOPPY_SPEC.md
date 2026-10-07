# Lecteur de disquette Amiga : spécification de comportement (salle blanche)

## Provenance et règle de licence

Le projet est sous licence MIT. WinUAE (`disk.cpp`, référence d'exactitude pour le lecteur) est sous
**GPL-2.0** : en copier ou en adapter du code rendrait le projet GPL. Méthode suivie (« Chinese wall ») :

1. **Rédaction** : un agent distinct a lu `disk.cpp` de WinUAE (téléchargé hors du dépôt, jamais
   commité) et a rédigé ce document, avec l'interdiction d'y mettre du code, du pseudo-code ligne à
   ligne, des extraits ou des identifiants internes de WinUAE. Il ne décrit que des comportements
   matériels observables, avec les noms de registres publics du Hardware Reference Manual.
2. **Implémentation** : `src/core/disk.cpp` est écrit à partir de ce document et du HRM seulement,
   sans relire WinUAE.
3. Toute évolution du lecteur passe par ce document. Ne jamais coller de code GPL dans le dépôt, ni
   « s'en inspirer » ligne à ligne.

Légende : **[HW]** = fait matériel documenté ou dérivé des horloges ; **[WU]** = choix
d'implémentation de WinUAE, libre ; **[?]** = incertain.

## État d'implémentation

| § | Comportement | État |
|---|---|---|
| A3, B5 | Position de rotation continue, lecture depuis le mot sous la tête | **fait** (2026-10-07) |
| B6 | WORDSYNC : attente de DSKSYNC, sync non stocké, premier mot = mot suivant | **fait** (2026-10-07) |
| B8, F4 | Durée réaliste du DMA (DSKBLK après attente du sync + len × 31,6 µs) | à faire (instantané aujourd'hui, comme le mode Kickstart accéléré de WinUAE) |
| B7 | Interruption DSKSYNC (INTREQ bit 12) au passage des syncs | à faire |
| A4 | Index une fois par révolution → CIA-B /FLG | à faire (le CIA n'a pas d'entrée /FLG) |
| B9, B10, B3 | Abandon (bit 15 à 0), mise à jour de longueur, len = 0, DMA suspendu sans DMAEN/DSKEN | à faire |
| C | DSKBYTR complet | à faire |
| D | Délai /RDY, moteur latché sur /SEL, /CHNG, DF1-3 absents | partiel (voir `drive.cpp`) |
| E | Écriture DMA + décodage AmigaDOS + validation tout ou rien | à faire (disque protégé aujourd'hui) |

Tests : `tests/pc`, `make testdisk` (stubs, sans ROM ni ADF) : position de rotation, WORDSYNC,
sync absent.

## A. Timing du flux disque

- **A1. Cellule de bit (MFM, DD, ADKCON FAST = 1)** : 7 color clocks = 14 cycles CPU ≈ 1,97 µs en PAL
  [HW ; HRM : 2 µs nominal]. Un mot = 16 cellules = 112 CCK ≈ 31,6 µs. Une ligne PAL (227-227,5 CCK)
  ≈ 32,5 cellules ≈ 2,03 mots. Une trame (312 lignes) ≈ 10 100 cellules.
- **A2. Révolution** : 300 tr/min = 200 ms [HW], soit ≈ 101 340 cellules = 6 334 mots à la cadence
  Paula PAL. Piste AmigaDOS : 11 secteurs × 544 mots = 5 984 mots + gap. WinUAE prend un gap de
  350 mots pour faire exactement 6 334 mots [WU]. Notre révolution (12 798 octets, gap 830) dure
  ≈ 202 ms à cadence Paula : dans la tolérance mécanique. Deux choix valables : garder la cadence
  (la période suit le buffer) ou mettre la cellule à l'échelle pour 200 ms (choix de WinUAE) [WU].
- **A3. Position de rotation** : compteur de bits modulo la longueur de piste, qui avance avec le temps
  émulé, indépendamment du DMA. Elle avance moteur en marche, sélectionné ou non ; moteur arrêté,
  WinUAE la fige [WU]. Changement de piste ou de face : position conservée (ou mise à l'échelle si les
  longueurs diffèrent) [WU]. Options facultatives [WU] : quelques bits aléatoires au démarrage du
  moteur ou à un pas (casse l'alignement des mots), gigue de vitesse dans le gap.
- **A4. Index** : une impulsion par révolution, à une position fixe (WinUAE : début du gap, avant le
  secteur 0) [WU]. Effet : front sur CIA-B /FLG, ICR bit 4 de CIA-B, interruption niveau 6 (INTREQ
  bit 13, EXTER) si démasquée [HW]. Émise seulement par un lecteur sélectionné dont le moteur tourne
  [HW]. Front instantané suffisant (/FLG est sur front) ; anti-rebond de 2 lignes chez WinUAE [WU].
  Existe aussi pendant une écriture.

## B. DMA de lecture

- **B1.** DSKLEN : bit 15 DMAEN, bit 14 WRITE, bits 13-0 = nombre de mots (0 à 16 383) [HW].
- **B2. Démarrage** : écriture de DSKLEN avec bit 15 à 1 alors que la valeur précédemment écrite avait
  aussi bit 15 à 1 [HW]. Lecture si bit 14 = 0 ; écriture si bit 14 = 1 dans les deux écritures. En
  fin de DMA, la valeur mémorisée retombe à 0 : il faut de nouveau deux écritures.
- **B3.** Le démarrage ne teste pas DMACON, mais aucun mot n'est transféré (et le compteur ne baisse
  pas) tant que DMAEN et DSKEN ne sont pas à 1 [HW]. Le flux défile quand même : données perdues,
  reprise au rétablissement.
- **B4. Cadence** : un mot toutes les 16 cellules (≈ 31,6 µs, ≈ 2 par ligne), via une FIFO de 3 mots
  vidée par les 3 slots DMA disque d'Agnus ; DSKPT avance de 2 par mot et ignore le bit 0 [HW].
  Écrire chaque mot en chip RAM à son assemblage est équivalent pour un émulateur.
- **B5. Sans WORDSYNC** (ADKCON bit 10 = 0) : transfert immédiat depuis la position courante.
  L'alignement des mots est arbitraire sur le vrai matériel ; aligner sur le buffer pré-encodé est
  une simplification acceptable.
- **B6. Avec WORDSYNC** : aucun mot transféré tant que le registre à décalage 16 bits n'est pas devenu
  égal à DSKSYNC (comparé à chaque cellule, tout alignement) [HW]. Une égalité déjà présente au
  démarrage ne compte pas. Le sync déclencheur n'est **pas** transféré : le premier mot stocké est le
  suivant (piste AmigaDOS `AAAA AAAA 4489 4489 …` : le 1er 4489 déclenche, le 2e est le premier mot
  stocké, puis l'info-long). Chaque égalité remet le compteur de bits à zéro, y compris pendant le
  transfert (réalignement) ; un sync aligné en cours de transfert est stocké comme une donnée.
  Le mode Kickstart accéléré de WinUAE a le même effet : cherche le sync depuis la position
  courante, saute ce mot, copie len mots en bouclant sur la piste.
- **B7. Interruption DSKSYNC** (INTREQ bit 12) : sur front, quand le registre devient égal à DSKSYNC ;
  ne se répète pas tant que l'égalité dure (deux 4489 consécutifs = deux interruptions). Levée avec ou
  sans DMA, avec ou sans WORDSYNC ; inhibée pendant une écriture et en mode MSBSYNC (ADKCON bit 9,
  GCR). Écrire dans DSKSYNC une valeur égale au registre courant la lève aussi (si non armée).
  ≈ 22 interruptions par révolution sur piste AmigaDOS.
- **B8. Fin** : à len mots, DSKBLK (INTREQ bit 1), arrêt, DSKLEN interne à 0. Durée réaliste : attente
  du sync (jusqu'à ≈ 28 ms sur ADF) + len × 31,6 µs (6 400 mots ≈ 202 ms ≈ 10 trames).
- **B9. DSKLEN réécrit pendant une lecture** : bit 15 à 0 → abandon immédiat sans DSKBLK [HW ; certains
  jeux le font] ; bit 15 à 1 et bit 14 à 0 → seul le compteur restant change, sans redémarrage [WU,
  conforme au HRM].
- **B10. len = 0** : DSKBLK immédiat sans WORDSYNC ; avec WORDSYNC, au sync suivant.
- **B11.** [?] Particularité Paula modélisée par WinUAE : FIFO vide au dernier mot → fin signalée sans
  écrire ce mot. Le mode accéléré écrit bien len mots. Recommandation : écrire len mots.
- **B12. Pas de données** : lecteur sélectionné moteur arrêté → bits aléatoires [WU] ; aucun lecteur
  → niveau constant [WU] ; disque absent → zéros ; piste hors image → bruit. Sans WORDSYNC le DMA
  finit avec des déchets ; avec WORDSYNC il ne finit jamais (le logiciel sort par timeout).
- **B13.** ADKCON FAST = 0 : cellule de 4 µs (GCR) ; lire du MFM DD ainsi = une cellule sur deux [WU].
  Priorité faible.

## C. DSKBYTR ($DFF01A, lecture seule)

- Bits 7-0 DATA : octet de poids faible du registre à décalage, capturé toutes les 8 cellules (frontières
  alignées sur le compteur de mot, donc sur le dernier sync avec WORDSYNC).
- Bit 15 DSKBYTE : à 1 à chaque capture, remis à 0 par la lecture de DSKBYTR [HW]. Pendant une
  écriture : à 1 à chaque octet avec DATA = 0 ; pas mis à jour si DSKLEN a WRITE sans DMAEN ;
  registre et DSKBYTE effacés au démarrage d'une écriture.
- Bit 14 DMAON : DMA disque armé par DSKLEN (même en attente de sync) ET DMACON DMAEN + DSKEN [HW].
- Bit 13 DISKWRITE : bit 14 de la dernière valeur écrite dans DSKLEN [HW].
- Bit 12 WORDEQUAL : registre = DSKSYNC à l'instant de la lecture (dure une cellule, ≈ 2 µs) [HW].
- L'émulateur doit amener le flux à l'instant exact de la lecture avant de répondre.

## D. Lecteur (CIA-B PRB en sortie, CIA-A PRA en entrée)

Lignes d'état sur CIA-A PRA, actives basses : bit 5 /RDY, bit 4 /TK0, bit 3 /WPRO, bit 2 /CHNG.
CIA-B PRB : bit 7 /MTR, bits 6-3 /SEL3-/SEL0 (bit 3 = DF0), bit 2 /SIDE, bit 1 /DIR, bit 0 /STEP.
/INDEX arrive sur CIA-B /FLG.

- **D1.** Un lecteur ne répond que /SEL bas ; sinon PRA bits 5-2 à 1 (0x3C au repos).
- **D2. Moteur** : bascule mise à jour seulement sur front descendant de /SEL [HW] : démarre si /MTR
  est bas dans l'ancienne OU la nouvelle valeur de PRB ; sinon s'arrête si /MTR était haut dans
  l'ancienne. Changer /MTR sans front sur /SEL n'a aucun effet.
- **D3. /RDY moteur en marche** : bas seulement disque présent et délai écoulé (WinUAE : 18 trames +
  0-511 lignes [WU] ; réel : quelques centaines de ms). Pas de progression sans disque ; insertion
  moteur en marche → délai relancé ; /RDY inactif dès l'arrêt du moteur.
- **D4. /RDY moteur arrêté (ID)** : registre série de 32 bits, poids fort d'abord, avancé à chaque
  front descendant de /SEL, bit courant sur /RDY (1 = bas) ; remis à zéro au passage marche → arrêt.
  3,5" DD = 0xFFFFFFFF, 3,5" HD = 0xAAAAAAAA, absent = 0 [HW]. DF0 interne d'A500 : pas d'ID ;
  moteur arrêté, /RDY = état prêt réel, donc inactif. WinUAE garde /RDY actif quelques CCK après
  l'arrêt pour un jeu précis [WU]. Cible : DF0 sans ID, DF1-DF3 absents.
- **D5.** /TK0 actif au cylindre 0, disque présent ou non [HW].
- **D6. Pas** : sur front montant de /STEP, pour les lecteurs sélectionnés juste avant ; /DIR = 1 vers
  l'extérieur (borné à 0, pas ignorés en silence), /DIR = 0 vers l'intérieur. WinUAE borne à 80 + 3
  [WU]. Délais réels ≈ 3 ms par pas + ≈ 15 ms de stabilisation [HW] ; WinUAE ne les impose pas
  [WU]. Recommandation : accepter tous les pas. Un pas disque présent remet /CHNG inactif.
- **D7. Face** : /SIDE bas = face 1, haut = face 0 [HW] ; piste = cylindre × 2 + face ; changement
  immédiat, position de rotation conservée.
- **D8. /CHNG** : actif à l'éjection et disque absent ; après insertion, actif jusqu'au premier pas avec
  disque [HW]. WinUAE retarde l'insertion de ≈ 2 s après une éjection [WU] ; au reset disque présent,
  /CHNG inactif [WU].
- **D9. /WPRO** : actif si disque protégé [HW] ou absent [WU].

## E. DMA d'écriture et reconversion en ADF

- **E1.** Double écriture de DSKLEN bits 15 et 14 à 1. len = 1 : fin immédiate sans écriture [WU].
  Avec WORDSYNC, attente d'un sync dans le flux lu (trackdisk le désactive pour écrire).
- **E2.** Un mot lu en chip RAM à DSKPT toutes les 16 cellules, écrit sur la piste à la position
  courante (granularité mot). Le logiciel fournit le MFM complet, horloges comprises. Écrire dans le
  buffer MFM circulaire de la piste, pour que les secteurs non réécrits restent intacts. WinUAE ajoute
  un mot 0x5555 avant le premier et après chaque mot écrit [WU] ; longueur de piste nominale pendant
  l'écriture [WU].
- **E3.** Fin à len mots : DSKBLK, DSKLEN à 0. Abandon (bit 15 à 0) : pas de DSKBLK, mais tentative de
  validation si quelque chose a été écrit.
- **E4. Décodage AmigaDOS** (format public) : piste traitée comme circulaire ; chercher 0x4489 à tous les
  décalages de bit ; sauter les 4489 consécutifs ; puis info-long (impairs puis pairs), label 16 octets
  (4 longs impairs puis 4 pairs), checksum d'en-tête, checksum des données, données (128 longs impairs
  puis 128 pairs). Décodage : masque 0x55555555, valeur = (impair << 1) | pair. Info : octet 3 = 0xFF,
  octet 2 = piste (cyl × 2 + face), octet 1 = secteur (0-10), octet 0 = secteurs avant le gap.
  Checksums = XOR des longs MFM masqués (info + label ; données). Rejets : secteur > 10, checksum faux,
  piste ≠ piste physique ; reprendre au sync suivant. Label non nul ou format ≠ 0xFF non bloquants.
- **E5. Validation** : seulement en fin (ou abandon) d'écriture, et seulement si les 11 secteurs sont
  valides (tout ou rien) → 11 × 512 octets à l'offset piste × 5 632. Sinon ne rien modifier. Disque
  protégé ou piste inexistante : ignorer et recharger la piste depuis l'image.

## F. Cas particuliers (loaders de jeux et démos)

- **F1.** La donnée lue dépend de la position de rotation au moment de DSKLEN ; les loaders qui relisent
  un secteur (len ≈ 544-1 100 mots) en boucle supposent que les lectures successives diffèrent.
- **F2.** Plus d'une révolution : la lecture boucle sur la piste (données répétées en ADF) ; len max
  16 383 mots ≈ 2,6 révolutions. trackdisk lit plus d'une révolution puis cherche les syncs lui-même.
- **F3.** Pas de pistes longues en ADF ; ne jamais tronquer.
- **F4. Risques du DSKBLK instantané** : (a) DSKBLK effacé ou handler installé APRÈS le lancement →
  interruption perdue, blocage ; (b) sondage de DMAON ou chronométrage de la lecture ; (c) décodage
  pendant que le DMA progresse ; (d) pas de DSKSYNC pendant la lecture ; (e) lectures répétées à la
  même position. WinUAE accélère lui aussi le Kickstart (copie d'un coup, DSKBLK 1-2 lignes plus
  tard), mais en respectant position et sync : compromis admis s'il respecte (a) et F1.

## G. Écarts priorisés (au 2026-10-07, avant correctifs)

1. Position de rotation continue, lecture depuis la position courante, en boucle. Impact élevé, coût
   faible, perf négligeable. **Fait.**
2. WORDSYNC. Impact élevé, coût faible à moyen (recherche par mot si DSKSYNC = 0x4489 et buffer aligné ;
   sinon table des positions de sync sur les 16 décalages, construite paresseusement). **Fait** (recherche
   alignée sur les mots).
3. Durée réaliste du DMA (risques F4). Impact moyen à élevé, coût faible.
4. Interruption DSKSYNC. Impact moyen, coût faible.
5. Index → CIA-B /FLG. Impact moyen, coût faible (entrée /FLG à ajouter au CIA).
6. Cas limites DSKLEN/DMACON (B3, B9, B10). Impact moyen, coût faible.
7. DSKBYTR complet. Impact faible à moyen, coût faible à moyen.
8. Lignes d'état (/RDY, moteur latché, /CHNG, DF1-3 absents). Impact moyen pour le changement de disque.
9. Écriture DMA + décodage + validation (sauvegardes, Workbench). Coût moyen à élevé ; persistance à part.
10. Fidélité au bit (alignement arbitraire, aléas, gigue, FAST = 0, MSBSYNC). Impact faible pour l'ADF.
    Modèle par mot ≈ 16 fois moins cher qu'un modèle bit à bit (≈ 1-3 % d'un cœur).

Architecture recommandée : position de rotation = fonction du temps émulé modulo la longueur de piste ;
à chaque ligne, avancer de 32,5 bits et traiter les événements franchis (mots de DMA, syncs, index,
fin de DMA) ; DSKBYTR et DSKLEN se calculent à la demande à partir de cette position.
