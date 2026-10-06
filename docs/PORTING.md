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
- **Silicium rev1** : ajouter `-mfix-esp32-psram-cache-issue` dans `platformio.ini` (coût débit).

## Option : charger l'ADF depuis la carte SD (au lieu de l'embarquer)

Faisable et plus souple que `wb_adf.h` embarqué (changement de disquette sans reflasher), **et sans
matériel additionnel** : le schéma v1.4 confirme un **slot microSD embarqué** (SPI, CS=13 CLK=14
MOSI=12 MISO=2, cf. `docs/HARDWARE.md`) qui n'entre en conflit ni avec la VGA, ni le PS/2, ni
l'audio. Le cœur expose déjà l'API : `drive_alloc_adf()` → lire le fichier `.adf` de la SD dans ce
buffer PSRAM → `drive_mount_ready()`. Implémentation (Phase 1+) : monter la SD via la lib Arduino
`SD`/`SPI` en passant explicitement ces 4 broches (pas les pins par défaut de FabGL), puis remplacer
`load_workbench()` de `main.cpp`. Le Kickstart, lui, reste pertinent à embarquer (petit, requis tôt
au boot). ⚠️ IO2 partagée avec la LED, IO12 = strapping (gérés au niveau carte).

## À vérifier avant la Phase 2 (clavier)

`core/cia.cpp` modélise-t-il le port série SP/CNT (registre SDR, flag SP de l'ICR bit 3, INT2) ?
Si non, `kbd_amiga.cpp` devra l'ajouter à `cia.cpp` — à coordonner, car `cia.cpp` fait partie du
tronc. L'upstream ne gère que les boutons feu sur CIA-A PRA, pas le clavier.
