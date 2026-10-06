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

## Chargement ADF depuis la carte SD (implémenté — Phase 1)

Implémenté dans `src/hal/sdcard.cpp` (`sdcard_load_adf`), appelé en priorité par `load_workbench()`
de `main.cpp` ; l'ADF embarqué (`wb_adf.h`) n'est plus qu'un repli. Plus souple (changement de
disquette sans reflasher) **et sans matériel additionnel** : slot microSD embarqué (SPI, CS=13
CLK=14 MOSI=12 MISO=2, cf. `docs/HARDWARE.md`), sans conflit VGA/PS2/audio. Mécanisme : `SPIClass`
sur HSPI avec ces 4 broches explicites (pas les pins par défaut de FabGL) → `SD.begin` → lecture du
fichier `/wb.adf` (901120 o, vérif de taille) par blocs de 512 o directement dans `drive_alloc_adf()`
→ `drive_mount_ready()`. Le Kickstart reste embarqué (petit, requis tôt au boot). ⚠️ IO2 partagée
avec la LED, IO12 = strapping (gérés au niveau carte).

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
flag `VGA32_SERIAL_KBD`. Limites : pas de maintien de touche (chaque caractère = press+release),
pas de Ctrl/Alt/Amiga isolés (modificateurs déduits du caractère seulement).

## Audio (Phase 3 — implémenté)

`src/hal/audio_dac.cpp` : `WaveformGenerator` custom dont `getSample()` dépile le ring Paula
(`paula_ring_pop`, 44100 Hz stéréo 16 b), downmix mono 8 bits, attaché à `fabgl::SoundGenerator`
(DAC GPIO25, I2S0 — indépendant de l'I2S1 de la VGA). **Ordre critique** : démarré depuis `emu_task`
APRÈS `paula_reset` (le ring doit exister). Fréquence **44100 Hz** = celle du ring (pas de
ré-échantillonnage). Mono 8 bits = qualité modeste ; niveau = décalage `>> 9` dans `getSample`
(tunable). ⚠ Non testé sur matériel : si le son est muet/haché, le DAC interne tourne souvent
mieux plus bas — repli `SoundGenerator(16000, …)` + décimation du ring (~2,75:1).
