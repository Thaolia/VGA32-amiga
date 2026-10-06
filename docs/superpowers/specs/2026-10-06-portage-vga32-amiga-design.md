# Design — Portage Amiga500-ESP32 → TTGO VGA32 v1.4 (FabGL)

*Date : 2026-10-06. Statut : design validé, implémentation Phase 0 en cours.*

## Contexte et intention

`Makerstown/amiga500-esp32` (MIT) est un **émulateur Amiga 500 OCS complet** (CPU 68000
via Musashi, chipset Agnus/Denise/Paula/2×CIA, boot Kickstart 1.3 → Workbench), conçu pour
**ESP32-S3 N16R8 (8 Mo PSRAM OPI)** avec sortie **TFT SPI ST7796** + audio **I2S (MAX98357A)**
et entrée par **boutons physiques**. Il n'a **aucune gestion clavier**.

**Objectif** : faire tourner cet émulateur sur une carte **TTGO VGA32 v1.4** (ESP32 classique
WROVER, ~4 Mo PSRAM QSPI) avec sortie **VGA via FabGL** et entrées **PS/2** (souris puis
clavier). **Critère de succès validé avec l'utilisateur** : booter Kickstart 1.3 → bureau
Workbench, **utilisable à la souris**, framerate bas accepté (10-20 fps). Carte confirmée **v1.4
avec PSRAM** ; ROM Kickstart 1.3 + ADF **disponibles** côté utilisateur.

## Contraintes structurantes (vérifiées sur sources primaires)

1. **Framebuffer VGA obligatoirement en RAM interne.** Sur ESP32 classique, le DMA I2S qui
   génère la VGA **ne peut pas lire la PSRAM** ; FabGL rescanne le framebuffer en continu → il
   reste en RAM interne. La PSRAM porte le reste de l'émulateur.
2. **La VGA FabGL monopolise un cœur en continu** (I2S1 + DMA + APLL) — contrairement au TFT
   SPI d'origine (rafales). L'émulateur ne dispose que du cœur « calme » (`CoreUsage::quietCore()`).
3. **PSRAM QSPI quad ≈ ¼ de la bande passante de l'OPI octal du S3** → le ~31 fps du S3 n'est
   **pas** garanti. D'où une **Phase 0 go/no-go** avant tout le reste.
4. **Versions** : l'émulateur exige `arduino-esp32 2.x` (I2S legacy `driver/i2s.h`) et FabGL
   exige `≤ 2.0.17` → on épingle **2.0.17** (platform-espressif32 6.10.0). Build **PlatformIO**.

## Découvertes pendant l'exploration

- **Architecture « dual-target » existante** : le cœur chipset est partagé entre un build PC
  (`pc/`, *ground truth* + 10 sentinelles) et le build ESP32 (`esp32/`). Le portage VGA32 est
  une **3ᵉ couche plateforme** ; le cœur se réutilise tel quel.
- **Le callback vidéo** `denise_line_cb(vpos, pixels, w)` livre un `uint16_t` par pixel =
  **couleur OCS 12 bits**. `w`=320 (lores) ou 640 (hires, à décimer). `g_row_onscreen[vpos]`
  doit être remis à `true` par la couche plateforme (optimisation de skip de `video.cpp`).
- **⚠ Intégration Musashi incomplète dans le dépôt public** : `esp32/` ne contient ni
  `m68kcpu.c`, ni `m68kops.c`, ni `m68kfpu.c` ; l'`esp32/m68kops.h` committé déclare les tables
  en **forme pointeur** (jump table 256 Ko + cycles 320 Ko, trop gros pour la RAM interne, donc
  alloués dynamiquement en PSRAM) mais le `.c` correspondant **manque**. → **Reconstruit** (voir
  ci-dessous), car l'auteur l'aurait rencontré aussi : l'utilisateur ne l'a probablement pas.

## Architecture retenue

### Structure (PlatformIO)

```
src/core/     cœur émulateur copié tel quel depuis esp32/*.cpp + a500.h
src/hal/      couche plateforme VGA32 (NOUVELLE)
src/main.cpp  alloc PSRAM, chargement ROM/ADF, init FabGL, emu_task
third_party/musashi/   Musashi reconstruit (pointeur+malloc) + conf 68000-only
third_party/sinfl/     inflate ADF
tools/        make_kick_header.py / make_adf_header.py (inchangés)
assets/       kick_rom.h / wb_adf.h (git-ignorés, générés par l'utilisateur)
tests/pc/     harnais de régression PC (10 sentinelles)
```

### Couche HAL (remplace a500_esp32.ino)

| Fichier | Rôle | Phase |
|---|---|---|
| `src/main.cpp` | alloc PSRAM, ROM/ADF, init FabGL, `emu_task` (boucle trame) sur `quietCore()` | 0 |
| `src/hal/video_vga.cpp` | `VGAController` 64 c., LUT 4096→RGB222, blit ligne Denise dans le FB interne | 0 |
| `src/hal/platform_esp32.h` | broches, modeline, mappage écran, assignation cœurs | 0 |
| `src/hal/input_ps2.cpp` | souris (P1) puis clavier (P2) PS/2 → cœur | 1/2 |
| `src/hal/kbd_amiga.cpp` | émulation clavier Amiga (CIA-A série) + table VirtualKey→rawcode | 2 |
| `src/hal/audio_dac.cpp` | ring Paula → SoundGenerator DAC GPIO25 | 3 |

### Décisions techniques

- **Musashi reconstruit** : `m68kops.c`/`m68kcpu.c` patchés en forme pointeur, tables allouées
  dans `m68ki_build_opcode_table()` via `heap_caps_malloc(MALLOC_CAP_SPIRAM)` (sur `ARDUINO`) ou
  `malloc` (PC). Conf **68000-only** (`esp32/m68kconf.h`, = flags `-D` du build PC, déjà prouvés
  par les sentinelles). **Validé : 6/6 sentinelles synthétiques vertes sur PC avec cet artefact.**
- **VGAController 64 couleurs à 320×240** (`QVGA_320x240_60Hz`), framebuffer ~80 Ko RAM interne,
  pas de double-buffering. Écriture ligne via `getScanline(y)` avec entrelacement `row[x ^ 2]`
  (piège DMA FabGL). LUT OCS 12 bits → `createRawPixel(RGB222(r>>2,g>>2,b>>2))`. **Suppression du
  shadow buffer + dirty-check** d'origine (le FB FabGL persiste) → ~300 Ko PSRAM économisés.
  Bascule documentée : VGA16 640×256 si le texte hires est trop flou en 320 décimé.
- **Ordonnancement** : VGA sur `busiestCore()` (FabGL), `emu_task` sur `quietCore()`. FB interne
  ⇒ le DMA vidéo ne touche jamais la PSRAM ⇒ l'émulateur peut marteler la PSRAM sans bloquer la
  VGA. `vTaskDelay(1)`/trame (watchdog). PS/2 par l'ULP, audio par I2S0+DAC (indép. de l'I2S1 VGA).

## Phases et séquencement

- **Phase 0 (tronc + spike, SÉQUENTIEL)** : scaffold + Musashi + `video_vga` + `main`. Affiche le
  boot Workbench réel sur VGA. **Go/no-go chiffré** (fps ≥ 10, heap interne ≥ 40 Ko, sync stable).
- Puis **parallélisable** (fichiers disjoints, agents parallèles) : **Souris (P1)**, **Audio (P3)**,
  **Build/docs (continu)**. **Clavier (P2)** séquentiel après le tronc (couplé à `cia.cpp`).

### Go/no-go Phase 0

- **GO** : fps soutenu ≥ 10 (cible 15) **ET** RAM interne libre ≥ 40 Ko **ET** VGA stable.
- **CONDITIONNEL (5-10 fps)** : dégrader (lores forcé, frameskip, moins de bitplans), re-mesurer.
- **NO-GO (< 5 fps / sync instable / RAM < ~20 Ko)** : replis = `VGADirectController` + shadow
  PSRAM, baisse résolution/profondeur ; au pire, conclure que l'OPI octal du S3 reste requis.

## Risques

1. Perf PSRAM QSPI mono-cœur (n°1) → tranché par Phase 0.
2. Stabilité sync VGA sous charge PSRAM → FB interne + ISR IRAM mitigent.
3. `core/cia.cpp` modélise-t-il le port série SP/CNT ? → à vérifier avant P2 (clavier).
4. `third_party/musashi/m68kmmu.h` : on vendor la version PC complète (prouvée) ; un stub 150 o
   existait côté esp32 — repli si la version complète pose souci à la compilation ESP32.
5. FabGL 1.0.9 vs arduino-esp32 2.0.17 : compilation à confirmer (build de fumée Phase 0).
6. Modeline 320×240 : repli crop/letterbox si l'écran ne tient pas le timing.

## Licence

MIT (émulateur © Massimiliano, Musashi © Karl Stenerud). ROM Kickstart / Workbench **jamais**
inclus (copyright) — fournis par l'utilisateur via `tools/`.
