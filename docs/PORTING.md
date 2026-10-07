# Portage — amiga500-esp32 (ESP32-S3 + TFT) → TTGO VGA32 (ESP32 + FabGL)

Ce document trace les décisions de portage et les points à vérifier empiriquement.

## Cartographie S3 → VGA32

| Aspect | Upstream (ESP32-S3) | Portage (VGA32) |
|---|---|---|
| Cœur émulateur | `esp32/*.cpp` + `a500.h` | **copié tel quel** dans `src/core/` |
| CPU Musashi | tables en PSRAM (m68kops.c absent du dépôt) | **reconstruit**, forme pointeur, PSRAM |
| Vidéo | TFT ST7796 SPI 320×480, bandes DMA + dirty-check + shadow PSRAM | **VGA FabGL** 320×240 64 c., blit direct dans le FB interne, **sans shadow** |
| Couleur | LUT 12 bits → RGB565 (byte-swap) | LUT 12 bits → **RGB222** via `createRawPixel` |
| Audio | I2S → MAX98357A externe | **DAC interne GPIO25** via `SoundGenerator` (Phase 3) |
| Entrée | boutons GPIO du cabinet | **PS/2** souris (P1) + clavier Amiga émulé (P2) |
| Ordonnancement | émulation sur loopTask, audio sur core 0 | émulation sur `quietCore()`, VGA sur `busiestCore()` (FabGL) |
| Build | Arduino IDE, core 2.x | **PlatformIO**, arduino-esp32 2.0.17 (platform 6.10.0) |

## Reconstruction Musashi (point critique)

Le dépôt public **ne contient pas** `m68kcpu.c` / `m68kops.c` / `m68kfpu.c` dans `esp32/` ;
l'`esp32/m68kops.h` committé déclare les tables d'opcodes en **forme pointeur** :

```c
extern unsigned char (*m68ki_cycles)[0x10000];
extern void (**m68ki_instruction_jump_table)(void);
```

Raison : la jump table (0x10000 × 4 = **256 Ko**) + la table de cycles (`NUM_CPU_TYPES=5` × 64 Ko
= **320 Ko**) ne tiennent pas en RAM interne → l'auteur les allouait dynamiquement en PSRAM, mais
le `.c` correspondant n'est pas publié.

**Solution appliquée** (`third_party/musashi/`) : on part du `m68kops.c` généré complet et du
`m68kcpu.c` de `pc/musashi/`, patchés de façon minimale :
- définitions des deux tables passées en **forme pointeur** (initialisées à 0) ;
- allocation paresseuse en tête de `m68ki_build_opcode_table()` via
  `heap_caps_malloc(MALLOC_CAP_SPIRAM)` sur `ARDUINO`, `malloc` sur PC (macro `MUSASHI_TBL_ALLOC`) ;
- `m68kcpu.c` : ses deux `extern` tableau alignés sur la forme pointeur.
- conf **68000-only** via `third_party/musashi/m68kconf.h` (= les flags `-D` du build PC).

**Validation** : le build PC compile avec cet artefact exact et passe **6/6 sentinelles
synthétiques** (CPU/vidéo/blitter/sprites/scroll/joystick/audio). `testboot`/`testmfm` exigent un
Kickstart/ADF réels.

**Perf** : la jump table en PSRAM est lue à chaque dispatch d'instruction (via le cache de données
ESP32). C'est la taxe inhérente à Musashi sur ESP32 et le cœur du risque perf — mesuré en Phase 0.

## Vidéo — détails FabGL

- `fabgl::VGAController` (64 couleurs RGB222), `getScanline(y)` pour écrire une ligne.
- **Entrelacement DMA** : le pixel X est à l'octet `row[X ^ 2]` (`VGA_PIXELINROW`). Toujours
  écrire `row[x ^ 2]`.
- `g_row_onscreen[vpos] = true` est remis par le callback (requis par l'optimisation de skip de
  `core/video.cpp`, qui saute le recalcul d'une ligne inchangée déjà à l'écran).
- Le framebuffer FabGL persiste entre trames → suppression du shadow buffer + dirty-check de
  l'upstream (gain ~300 Ko PSRAM, simplification).

## Replis si Phase 0 = NO-GO ou image incorrecte

- **Framerate trop bas / RAM interne insuffisante** : `VGADirectController` + buffer d'écran en
  PSRAM (libère les ~80 Ko du FB interne), au prix d'un callback scanline couplé au timing vidéo.
- **Texte Workbench (hires 640) trop flou en 320 décimé** : `VGA16Controller` 640×256 (net, mais
  16 couleurs — l'EHB 64 c. devient faux).
- **Image pas centrée** : ajuster `VGA32_VSTART` dans `platform_esp32.h`.
- **Écran ne tient pas le timing** : modeline custom 320×256, ou repli `VGA_640x240_60Hz` crop.
- **Silicium rev1** : `-mfix-esp32-psram-cache-issue` est actif (carte de référence = rev v1.1).
  Sur une carte rev3, le retirer pour récupérer du débit PSRAM.

## Mesures Phase 0 (carte de référence, 2026-10-06)

ESP32-PICO-D4 rev v1.1, flash 4 Mo (partitions `huge_app.csv` du framework : app 3 Mo), fix
PSRAM actif. Boot Kickstart 1.3 jusqu'à la demande de disquette (ADF embarqué vierge, pas de SD) :
**14,6 à 21,1 fps** (trames 50-200), **heap interne libre ~170 Ko**, PSRAM libre 1,1 Mo après
allocations, `[RESET]` OK. Critères chiffrés GO atteints (≥ 10 fps, ≥ 40 Ko).

Avec l'ADF Workbench 1.3.2 embarqué (350 Ko compressé, firmware 1,24 Mo / 3 Mo) : boot disquette
(129 lectures de pistes en 90 s), **12 à 25 fps pendant les accès disque, ~38 fps au repos**,
heap interne stable à 169,6 Ko. Aspect du bureau et stabilité VGA : à confirmer sur le moniteur.

## Chargement ADF depuis la carte SD (implémenté — Phase 1)

Implémenté dans `src/hal/sdcard.cpp` (`sdcard_init` + `sdcard_read_adf`), appelé en priorité par `load_workbench()`
de `main.cpp` ; l'ADF embarqué (`wb_adf.h`) n'est plus qu'un repli. Plus souple (changement de
disquette sans reflasher) **et sans matériel additionnel** : slot microSD embarqué (SPI, CS=13
CLK=14 MOSI=12 MISO=2, cf. `docs/HARDWARE.md`), sans conflit VGA/PS2/audio. Mécanisme : `SPIClass`
sur HSPI avec ces 4 broches explicites (pas les pins par défaut de FabGL) → `SD.begin` (la SD
**reste montée**) → liste des `.adf` de la racine à 901120 o, triés par nom → lecture de `wb.adf`
s'il est présent, par blocs de 512 o directement dans `drive_alloc_adf()` → `drive_mount_ready()`.
FAT16/FAT32 seulement (pas d'exFAT dans le FatFs du framework). Le Kickstart reste embarqué
(petit, requis tôt au boot). ⚠️ IO2 partagée avec la LED, IO12 = strapping (gérés au niveau carte).

## Changement de disquette au bouton IO36 (implémenté, validé sur la carte le 2026-10-06)

Bouton **K1** du schéma v1.4 (S_VP = GPIO36, pull-up 10K externe, appui = 0). Chaque appui affiche
le nom de l'ADF suivant de la SD (ordre alphabétique, circulaire) **en surimpression 4 s** en bas
de l'écran VGA ; la disquette est insérée **1,5 s après le dernier appui** (on peut défiler sans
tout charger). Découpage :
- `disk_select.cpp` : logique pure (anti-rebond 150 ms, sélection, validation différée,
  débordement de `millis()`), testée hôte (`tests/hal/test_disk_select.cpp`).
- `disk_switch.cpp` : glue. Le lecteur n'est manipulé que par `emu_task` (`drive_eject` →
  lecture → `drive_mount_ready`) ; la lecture SD tourne dans une tâche `adfload` (cœur VGA,
  priorité 1) pendant que DF0 est éjecté (`drive_adf()` = NULL, le cœur n'y touche pas). Deux
  files FreeRTOS = transfert + barrière mémoire inter-cœurs. Une sélection validée pendant une
  lecture est enchaînée ensuite. Échec de lecture → DF0 reste vide, message « Erreur SD ».
- Cœur : **un seul hook** `drive_eject()` (`core/drive.cpp`, `#ifdef ARDUINO`) : `disk_present = 0`,
  `/CHNG` latché bas jusqu'au prochain step disque inséré → trackdisk voit le changement.
- OSD (`video_vga.cpp`) : police `fabgl::FONT_8x8`, bandeau 12 lignes à `VGA32_OSD_Y`. Le cœur
  saute les lignes inchangées : on garde une copie des pixels Amiga **sous** le cadre (mise à jour
  par `denise_cb`), on redessine le texte sur chaque ligne rendue, et on restaure exactement
  l'image à l'échéance.

**Mesuré — `denise_cb` doit être en IRAM.** Ajouter les deux appels par trame (17 µs mesurés) a
fait passer la trame 150 du boot de 42 ms à **194 ms** (reproductible à la µs) et le repos de
34,6 à 32,3 fps, alors que le padding de `main.cpp` ne changeait rien. Avec `IRAM_ATTR` sur
`denise_cb` et `osd_draw_row` : retour exact aux valeurs de référence (24,0 fps trame 150,
34,6 fps au repos). Lecture : le callback par ligne, en flash, entre en conflit de cache
(flash et PSRAM partagent le cache) selon l'agencement du binaire. Garder le code vidéo
chaud en IRAM.

## Mémoire : Fast RAM Zorro II, PSRAM de 8 Mo, himem (2026-10-07)

**Mesures** (bilan `[MEM]` à la trame 200, firmware normal) : PSRAM libre 1 156 471 o, plus grand
bloc 1 146 868 o ; puce **64 Mbit = 8 Mo** (`esp_spiram_get_chip_size`), dont l'ESP32 n'adresse que
4 Mo. Occupation des 4 Mo visibles : chip 512 Ko, slow 512 Ko, ROM 256 Ko, ADF 880 Ko, tables Musashi
576 Ko, cache de lignes + ring audio ≈ 220 Ko.

**Fast RAM** : `src/hal/zorro.*` (module pur, `tests/hal/test_zorro`) émule une chaîne de cartes RAM
Zorro II en autoconfig (spécification publique du HRM : quartets en $E80000, base écrite en $4A puis
$48, fabricant « hacker » 0x07DB). `memory.cpp` n'a que des hooks : table `zorro_page[256]` (pages de
64 Ko) dans les accès 8/16 bits, espace de config en $E8xxxx, `zorro_reset()` au reset. `main.cpp`
alloue `VGA32_FASTRAM_KB` (défaut 1024) **juste après** chip/slow/ROM, sinon le bloc contigu n'existe
plus. Validé sur la carte : le Kickstart configure la carte en **$200000**, le système l'utilise
(`[ZORRO]` : 12-13 pages de 4 Ko touchées à la trame 1000). PSRAM restante : 107 Ko.

**Les 4 Mo cachés (himem) : bloqués avec arduino-esp32 2.0.17.** Lier l'API `esp_himem_*` fait
planter au boot (`E spiram: SPI RAM not initialized`, `abort()` dans `esp_himem_init`, backtrace
décodée) : son constructeur global s'exécute avant l'init PSRAM paresseuse d'Arduino
(`initArduino`). Un constructeur `psramInit()` de priorité 101 ne passe pas avant : ESP-IDF appelle
le tableau `__init_array` **à l'envers** et la priorité ne réordonne pas ce tableau (ordre relevé
dans l'ELF). Reste possible : notre propre commutation de bancs (registres MMU du cache PSRAM, deux
cœurs, vidage de cache) pour y ranger l'ADF (+880 Ko visibles, −256 Ko de fenêtres → fast RAM de
1,5 Mo). Autre gain simple non fait : `m68ki_cycles` alloue 5 types de CPU (320 Ko) pour un seul
utilisé (−256 Ko possibles).

**Coût perf : effet de cache chaotique.** Au repos, le calcul par trame a valu 12,3 ms sans Fast RAM
et 16,5 ms avec, sur un même binaire ; 12,3 ms avec 1 Mo alloué mais non déclaré (pas un décalage
d'adresses) ; firmware de profiling : courbes identiques avec ou sans, bloc par bloc (même travail).
Accesseurs mémoire en IRAM : 19,2 ms (pire, annulé). Après l'ajout suivant (lecteur), 12,3 ms avec
Fast RAM. Lecture : flash et PSRAM partagent le cache de l'ESP32 ; selon où tombent le code chaud et
les structures que l'OS place en Fast RAM, les conflits de cache coûtent de 0 à ~7 ms par trame, sans
lien avec le travail fait. Toujours sous 20 ms au repos (50 fps). Pas de correctif fiable sans outil
de mesure des défauts de cache.

## Lecteur de disquette : position de rotation et WORDSYNC (2026-10-07)

Spécification salle blanche et règle de licence (WinUAE est GPL) : `docs/FLOPPY_SPEC.md`.
`disk.cpp` : la lecture part du mot sous la tête (temps émulé `cur_frame × 312 + vpos` lignes,
65/32 mots par ligne, modulo la révolution) ; avec ADKCON WORDSYNC, elle commence juste après le
prochain mot DSKSYNC (non stocké), sans DSKBLK si la piste n'en contient pas. DMA toujours
instantané. `tests/pc` `make testdisk` (stubs) : 12 contrôles, 6 échouent sur l'ancienne version.
Carte : Workbench boote (130 lectures de piste, aucune sans sync), repos atteint vers la trame 1450.

## Optimisations Denise + limiteur 50 Hz (2026-10-07)

Mesures sur la carte, firmware normal (Workbench embarqué, sans SD), temps réel horodaté côté PC
par bloc de 50 trames (`=== frame`) ; « boot » = temps pour aller de la trame 50 à la trame 1350
(émulation déterministe, même travail).

| Étape | Boot 50→1350 | Bureau au repos |
|---|---|---|
| Référence | 63,1 s | 28,9 ms/trame (34,6 fps) |
| 1. pas de remplissage de fond en tête de ligne (`ARDUINO`) | 49,2 s | 17,8 ms |
| 2. écritures par mots : `fb_line_fill` (2 px) + `denise_cb` (4 px, `vga_pack4`) | 44,5 s | 14,2 ms |
| 3. limiteur 50 Hz | — | 20,0 ms réels, **12,3 ms de calcul** |

- 1 : `core/video.cpp`, remplissage gardé pour le PC seulement (voir commentaire). Chaque chemin
  ARDUINO qui livre `fb_line` le remplit ou écrase tous les pixels lus.
- 2 : `fb_line` aligné sur 4, écrit via un type `may_alias`. `src/hal/vga_pack.h` (pur, testé :
  `tests/hal/test_vga_pack`, comparé octet par octet à `row[x ^ 2]`) range 4 pixels dans un mot en
  respectant l'entrelacement FabGL ; `video_vga_init` vérifie l'alignement des scanlines (sinon
  repli octet par octet, message au boot). `denise_cb` reste en IRAM, sans appel externe.
  **Image à valider visuellement** (lores Kickstart et hires Workbench) : le log série ne voit pas
  un ordre d'octets faux.
- 3 : `frame_end_wait` (`main.cpp`) : échéance absolue (moyenne exacte de 50 Hz, un dépassement de
  tick est rattrapé), retard > 1 trame → on repart de maintenant (pas d'accélération de
  rattrapage). Plus lent que 50 fps : même comportement qu'avant (`vTaskDelay(1)`).
  Pour mesurer la perf, lire le temps de calcul du log `=== frame` (attente exclue), plus le
  temps réel.

## PSRAM : 80 MHz (mesuré au boot)

`psram_diag()` (`main.cpp`, sous `VGA32_DEBUG`) : `SPI0 clock=80000000 (80.0 MHz)`
(`CLK_EQU_SYSCLK`), bits `SPI_DATE_REG(0)[31:30]=00` (flash et PSRAM à la même vitesse), lecture
séquentielle de 512 Ko à **23,2 Mo/s**, 1,2 µs par ligne de cache de 32 o. Cohérent avec du QSPI à
80 MHz (40 Mo/s brut). Le SDK précompilé fixe `CONFIG_SPIRAM_SPEED_80M` et la flash à 80 MHz
(l'en-tête 40 MHz de `esp32dev` ne vaut que pour le bootloader). 80 MHz est le maximum de la PSRAM
quad sur ESP32 : rien à gagner ; la fréquence ne se change pas à l'exécution (le code s'exécute via
ce même cache SPI0).

## Profiling de la boucle trame (`ttgo-vga32-prof`, 2026-10-07)

`VGA32_PROF=1` : `main.cpp` lit le compteur de cycles (`ccount`) entre chaque étape de la boucle
ligne et publie toutes les 50 trames une ligne `[PROF]` : ms/trame par étape (`trame`, `copper`,
`denise`, `cpu`, `paula`, `cia`, `post`, `yield`), plus deux sous-totaux : `blit` (compteur
`blitter_ns()` du cœur, inclus dans `cpu`) et `vga_cb` (callback Denise → framebuffer, inclus dans
`denise`, avec le nombre de lignes réellement écrites). Aucune retouche de `src/core/`.

**Coût** : +2,1 ms/trame au repos (34,6 → 32,2 fps), alors que le calcul ajouté pèse ~0,1 ms →
effet de disposition du binaire sur le cache flash/PSRAM (même signature que l'épisode `IRAM_ATTR`
ci-dessous). OFF par défaut ; build par défaut vérifié identique (sections ELF) à celui d'avant.

**Mesures (Workbench 1.3 embarqué, sans SD, 90 s, fenêtres de 50 trames)** :

| Phase | ms/trame | denise | cpu (dont blit) | vga_cb (lignes écrites) | reste |
|---|---|---|---|---|---|
| Écran Kickstart (boot) | 57-65 | 28,6 | 27-36 (0) | 5,4 (312/312) | ~1,4 |
| Chargement disque WB | 52-61 | 27,8 | 22-31 (0,5-1,6 ; pic 3,7) | 1,0 (117/312) | ~2 |
| Bureau au repos | 33-37 | 27,4 | 3-7 (0) | 1,0 (117/312) | ~2 |

Lecture :
- **Denise est le premier goulot : ~27,5 ms/trame quasi constant** (75-85 % au repos, ~50 % sinon),
  soit ~85 µs (~20 000 cycles) par ligne Amiga.
- **Le skip de lignes inchangées ne fait presque rien gagner** : il n'évite que la conversion VGA
  (`vga_cb` passe de 5,4 à 1,0 ms) ; les ~26,5 ms restantes sont dépensées dans
  `denise_render_line` **avant** la décision de skip, même pour les lignes sautées.
- **Le CPU 68000 (Musashi) est le second** : 3-7 ms au repos, 22-36 ms pendant le boot et les
  accès disque. Le blitter est marginal (≤ 1,6 ms en moyenne, pic 3,7 ms).
- Copper, Paula, CIA, entrées, `vTaskDelay` : 1,4 à 2,1 ms au total, rien à gagner là.
- Plafond théorique si Denise devenait gratuit : ~6-10 ms/trame au repos → 50 fps atteignables.

### Détail de Denise (ligne `[DENISE]`, même env)

`core/video.cpp` est instrumenté sous `#if defined(ARDUINO) && VGA32_PROF` (marques `DPROF()` sans
point-virgule : build normal vérifié identique, sections ELF). Bureau Workbench au repos
(lignes/trame : 5 bordure, 96 sans bitplan, 195 sautées, 16 dessinées), ms/trame :

| bgfill | cb (+ refill bordure/0 plan) | pixels | skipchk | fetch | sprdma | autres |
|---|---|---|---|---|---|---|
| **10,5** | 4,7 | 3,7 | 2,4 | 1,4 | 0,4 | ~0,5 |

À l'écran Kickstart (312 lignes de bordure) : bgfill 10,5 + cb 16,8 (refill 640 px + conversion).

**Cause racine mesurée** : chaque écriture mémoire coûte ~12,6 cycles. Le désassemblage de la boucle
de fond montre `s16i` + **`memw`** à chaque pixel : `-mfix-esp32-psram-cache-issue` (obligatoire, puce
rev1) ajoute une barrière après TOUTE écriture, même en DRAM interne (`fb_line` est en
`0x3FFC6140`), et `-Os` (posé après `-O2` par le framework, donc gagnant) garde le compteur de
boucle sur la pile. Remplir 640 pixels coûte donc ~34 µs. L'hypothèse « cache expulsé par Musashi »
n'est pas nécessaire pour expliquer les chiffres.

**Gaspillage identifié** : sur la carte, le remplissage de fond en tête de `denise_render_line`
(`bgfill`, 10,5 ms/trame) est du **travail mort** : chaque chemin qui livre `fb_line` à la VGA le
remplit de nouveau (`BG_FLUSH`, 0 bitplan) ou écrase tous les pixels lus (ligne dessinée), et les
lignes sautées ne l'utilisent pas. Il ne sert qu'au harnais PC (`row` = framebuffer complet).

Pistes, par gain estimé (non implémentées) :
1. Ne pas faire `bgfill` sous `ARDUINO` : environ −10,5 ms/trame (repos 28 → ~18 ms).
2. Remplissages/écritures en mots de 32 bits (2 pixels par `memw`) dans `BG_FLUSH`, le chemin
   0 bitplan et `denise_cb` ; ou ne pas renvoyer une ligne de fond identique à la précédente.
3. Boucle pixels (~230 µs par ligne dessinée) : sortir l'écriture `color_diag_max_idx` de la
   boucle (un `memw` de plus par pixel) et écrire 2 pixels par mot.
4. Effet global du `memw` sur Musashi (chaque écriture 68000 passe par du C instrumenté) : à
   mesurer séparément.

## Variante Kickstart seul (`ttgo-vga32-kickonly`)

`VGA32_EMBED_ADF` (`platform_esp32.h`, défaut 1) : à 0, `main.cpp` n'inclut pas `wb_adf.h` et n'a
plus de repli embarqué ; sans `wb.adf` sur la SD, `load_workbench` rend `false` → Kickstart à
l'invite disque, IO36 charge un ADF de la SD (le buffer ADF reste alloué). Env pio dédié
`ttgo-vga32-kickonly` (`-DVGA32_EMBED_ADF=0`) : Flash 889 257 o contre 1 243 661 o.
- `#include "sinfl.h"` reste **inconditionnel** : le LDF PlatformIO (`deep+`) évalue les `#if` sans
  voir la macro (définie dans un header) et retirait `sinfl` du build par défaut → `undefined
  reference to zsinflate`. Dans la variante, `zsinflate` non référencé est éliminé au link.
- Build par défaut inchangé : sections chargées de l'ELF identiques avant/après.
- **Validé sur la carte (2026-10-07, sans SD)** : `[RESET] PC=FC00D2`, Kickstart jusqu'à l'accès
  disque (step piste 0/1, « nessun disco »), aucun panic ; trames 50/100/150 = 19,4 / 14,6 / 24,0 fps,
  identiques au firmware par défaut mesuré dans les mêmes conditions. Heap interne libre 154 272 o
  dans les **deux** firmwares : la baisse depuis les ~170 Ko de la Phase 1 vient des phases 2-3,
  pas de cette variante.

## Harnais PC : source unique (2026-10-07)

**Constat** : `tests/pc/` compilait sa propre copie du cœur (`tests/pc/src/*.cpp`, héritée du `pc/`
upstream) et de Musashi (`tests/pc/musashi/`). 7 fichiers cœur sur 10 divergeaient de `src/core/` ;
surtout, le chemin de rendu PC de `src/core/video.cpp` n'avait ni scroll fin (BPLCON1) ni sprites,
et le chemin `ARDUINO` (celui de la carte) n'était jamais exécuté sur PC. Les sentinelles validaient
donc un autre code que celui de la carte.

**Correctif** : le harnais compile `src/core/` et `third_party/musashi/` directement ; copies supprimées.
Retouches du cœur, toutes neutres pour la cible :
- `video.cpp` : la boucle bitplanes/scroll/sprites de la carte devient commune ; sous `#ifndef ARDUINO`,
  doublage des pixels lores dans `fb[]` (640 colonnes) et sprites sur l'écran 0 bitplane.
- `memory.cpp` : slow RAM statique sur PC (pointeur `slow_ram`, comme sur la carte).
- `custom.cpp` / `cia.cpp` / `a500.h` : diagnostics de blocage du harnais (`intreq_src_count`,
  `cia_b_dump`, déclaration `paula_write_wav`) sous `#ifndef ARDUINO`.
- Musashi : le harnais force la conf 68000 par `-D` (même résultat que `third_party/musashi/m68kconf.h`) ;
  la PMMU, active dans l'ancienne copie, est désormais OFF comme sur la carte (sans effet sur un 68000).

**Vérifié** :
- 6 sentinelles : mêmes 68 contrôles `OK` qu'avant (8/29/7/5/13/6).
- Mutation volontaire du scroll dans la boucle commune → `testscroll` échoue : le harnais voit bien
  le code de la carte.
- Cible : sections chargées de l'ELF (`.flash.text`, `.iram0.text`, `.flash.rodata`, `.dram0.data`,
  `.flash.appdesc`) identiques octet pour octet avant/après. Le `firmware.bin` diffère quand même,
  car il embarque le SHA-256 de l'ELF (debug et numéros de ligne compris).
- Pour garder un code machine identique, `int pixels` et le `return;` final du chemin `ARDUINO`
  restent en place : les retirer (no-ops) changeait l'allocation de registres de
  `denise_render_line` (même taille, ordre différent), ce qui aurait exigé une re-mesure perf.

## Clavier Amiga (Phase 2 — implémenté)

Constat : `core/cia.cpp` possédait déjà l'IRQ série (`icr_set(&cia_a, 0x08)` → PORTS → INT2) mais
le SDR (reg 0xC) était stubé (lecture = 0). Ajouts (retouches HAL, guardées `#ifdef ARDUINO`) :
- `core/a500.h` : champ `sdr` dans `cia_t` + hook `cia_a_kbd_shift_in`.
- `core/cia.cpp` : reg 0xC lit `c->sdr` ; `cia_a_kbd_shift_in(v)` dépose l'octet et lève l'IRQ série.
- `src/hal/kbd_amiga.cpp` : FIFO de rawcodes ; encodage **`SDR = ~((rawcode<<1) | relâche)`** (vérifié
  sur le driver Linux `amikbd.c`) ; cadencement handshake simplifié (un code par IRQ série acquittée).
- `src/hal/input_ps2.cpp` : table VirtualKey FabGL → rawcode Amiga **positionnelle** (HRM / amikbd.c ;
  min./maj. et chiffre/symbole → même touche physique, l'Amiga applique sa keymap via Shift).

Non implémenté volontairement (démarrage simple) : la séquence power-up `0xFD`/`0xFE`. Si KS 1.3
n'enregistre pas les touches au boot, l'émettre en premier (dans `kbd_amiga.cpp`) est le correctif.

## Clavier série USB (implémenté)

Pour les configs **sans clavier PS/2** (l'ESP32 classique n'a pas d'USB host ; un adaptateur
USB→PS/2 passif ne marche qu'avec un clavier dual-protocol). `src/hal/serial_kbd.cpp` lit les
caractères du port série (CP2104) et les convertit en frappes Amiga via une table **ASCII→rawcode
US** (+ Shift déduit du caractère), plus les séquences `ESC[A/B/C/D` → curseurs. Réutilise toute la
Phase 2 (`kbd_amiga` + SDR CIA-A). Appelé chaque trame dans `emu_task`, en parallèle du PS/2 ;
flag `VGA32_SERIAL_KBD`. Limites du mode texte : pas de maintien de touche (chaque caractère =
press+release), pas de Ctrl/Alt/Amiga isolés (modificateurs déduits du caractère seulement).

### Protocole binaire clavier + souris (`tools/remote_input.py`)

Lève ces limites et ajoute la souris. Trame : `0xA5 TYPE PAYLOAD CHK` (CHK = somme 8 bits de
TYPE+PAYLOAD) ; `'K'` = rawcode | 0x80 si relâchée ; `'M'` = dx, dy (int8, bas = +), boutons (b0 G,
b1 D). Décodeur pur `src/hal/serial_proto.cpp` (testé hôte : `tests/hal`, `make test`) ; tout octet
hors trame retombe dans le mode texte, donc un terminal reste utilisable. Choix :
- **Pas de resynchro sur `0xA5` en cours de trame** : c'est une donnée légitime (relâche de H =
  `0x25|0x80`). Une trame corrompue est jetée par la somme de contrôle.
- Souris : deltas cumulés, appliqués bornés ±100 par trame émulée (`input.device` lit un delta
  8 bits par vblank), comme le PS/2.
- Côté PC : scancodes USB HID (positionnels, indépendants du layout du PC) → rawcode Amiga ;
  Inser → HELP ; F12 libère la capture ; perte de focus → relâche de toutes les touches.
- **Ouverture du port sans reboot** : laisser DTR et RTS actifs. Linux les lève ensemble à l'ouverture
  (neutre) ; les baisser via pyserial passe par un instant « RTS seul » qui tire EN à 0 (mesuré :
  reboot à chaque connexion avant correctif, aucun après).
- `pygame` (>= 2, ex. paquet système) ou **`pygame-ce`** (wheels pip pour Python 3.14). Le mode souris
  relatif s'obtient par curseur caché + `set_grab` (les deux) ; `set_relative_mode` n'existe que
  dans pygame-ce, donc appelé seulement s'il est présent.

### Adaptateurs USB→PS/2 passifs

Constaté sur la carte de référence : clavier + souris USB filaires via adaptateur passif →
`[PS2] clavier: ABSENT, souris: ABSENTE` (aucune réponse au reset PS/2, LED éteintes). Ces
périphériques ne parlent qu'USB ; l'init PS/2 attend alors ~7 s avant d'abandonner (boot plus long).

## Audio (Phase 3 — implémenté)

`src/hal/audio_dac.cpp` : `WaveformGenerator` custom dont `getSample()` dépile le ring Paula
(`paula_ring_pop`, 44100 Hz stéréo 16 b), downmix mono 8 bits, attaché à `fabgl::SoundGenerator`
(DAC GPIO25, I2S0 — indépendant de l'I2S1 de la VGA). **Ordre critique** : démarré depuis `emu_task`
APRÈS `paula_reset` (le ring doit exister). Fréquence **44100 Hz** = celle du ring (pas de
ré-échantillonnage). Mono 8 bits = qualité modeste ; niveau = décalage `>> 9` dans `getSample`
(tunable). ⚠ Non testé sur matériel : si le son est muet/haché, le DAC interne tourne souvent
mieux plus bas — repli `SoundGenerator(16000, …)` + décimation du ring (~2,75:1).
