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
