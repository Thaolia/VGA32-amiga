# Architecture

## Vue d'ensemble

```
          ┌──────────────────────── cœur calme (quietCore) ───────────────────────┐
          │  emu_task (src/main.cpp)                                               │
          │    boucle trame PAL : 312 lignes × 455 cycles                          │
          │      copper_run_line → denise_render_line → m68k_execute               │
          │                     → paula_step → cia_tick → cia_tod_hsync            │
          │                                                                        │
          │   ┌─ src/core/ (émulateur, copié tel quel) ──────────────┐             │
          │   │ memory  cia  custom  blitter  video  paula  disk      │             │
          │   │ drive  input         + Musashi (third_party/musashi)  │             │
          │   └───────────────────────────────────────────────────────┘           │
          └───────────┬───────────────────────┬───────────────────┬───────────────┘
                       │ denise_line_cb         │ paula_ring_pop    │ input_set_* / kbd
                       ▼                        ▼                   ▼
            src/hal/video_vga.cpp     src/hal/audio_dac.cpp   src/hal/input_ps2.cpp
             (LUT 12b→RGB222,          (ring→DAC, Phase 3)      src/hal/kbd_amiga.cpp
              blit FB interne)                                   (Phases 1/2)
                       │                        │                   ▲
                       ▼                        ▼                   │ PS/2 (ULP)
          ┌──────── cœur occupé (busiestCore) ────────┐    fabgl::Mouse / Keyboard
          │  FabGL VGAController : I2S1 + DMA + APLL   │
          │  rescanne le FB interne → sortie VGA DB15  │
          └───────────────────────────────────────────┘
```

## Modules

### Cœur émulateur — `src/core/` (ne pas réécrire)
Copié de l'upstream (variantes `esp32/`). Chipset OCS complet, portable (compile aussi sur PC).
API dans `a500.h`. Globals RAM (`chip_ram`/`slow_ram`/`kick_rom`) **définis** dans `memory.cpp`,
alloués en PSRAM par `main.cpp`. Hook vidéo : `denise_line_cb`. Interruptions : `irq_update` →
`m68k_set_irq` (pas de callback INT_ACK, cf. `m68kconf.h` 68000-only).

### CPU — `third_party/musashi/`
Musashi 4.5, conf 68000-only, tables d'opcodes en forme pointeur allouées en PSRAM (voir
`docs/PORTING.md`). Compilé en **C**. `library.json` ne compile que `m68kcpu.c`, `m68kops.c`,
`softfloat/softfloat.c` (`m68kfpu.c` est `#include` par `m68kcpu.c`, pas compilé seul).

### Couche matérielle — `src/hal/`
- `platform_esp32.h` : broches, modeline VGA, mappage écran (`VGA32_VSTART`), timing trame,
  tailles de pile, flag `VGA32_DEBUG`.
- `video_vga.cpp` : `VGAController` 64 c. ; LUT couleur ; callback Denise (blit `row[x^2]`,
  `g_row_onscreen`).
- `input_ps2.cpp` : **souris PS/2 (P1, implémentée)** → `input_mouse_delta`/`input_set_lmb/rmb`
  du cœur ; clavier (P2) à venir. Bornage ±100/trame avec report.
- `sdcard.cpp` : **chargement ADF depuis la microSD embarquée (P1, implémenté)** → buffer PSRAM du
  drive (`drive_alloc_adf` + `drive_mount_ready`), lecture par blocs de 512 o.
- `kbd_amiga.cpp` : clavier Amiga émulé — CIA-A série + table VirtualKey→rawcode. *(stub)*
- `audio_dac.cpp` : ring Paula → `SoundGenerator` DAC GPIO25. *(stub)*

### Point d'entrée — `src/main.cpp`
`setup()` : alloc PSRAM, chargement ROM (`kick_rom.h`), ADF (**SD `/wb.adf` en priorité**, sinon
`wb_adf.h` décompressé via `zsinflate`), `video_vga_init()`, `input_ps2_init()`, création de
`emu_task` sur `quietCore()`. `loop()` idle. `emu_task` sonde la souris (`input_ps2_poll`) chaque trame.
`emu_task()` : init cœur (dont `m68k_init` qui alloue les tables Musashi en PSRAM) + boucle trame.

## Flux d'une trame

1. `emu_task` : `input_ps2_poll` → `input_frame` → `copper_vblank` → `sprite_vblank`.
2. Pour chaque ligne (0..311) : IRQ blit/disk différées → `copper_run_line` → `denise_render_line`
   (→ `denise_line_cb` → `video_vga` écrit la ligne dans le FB interne) → `m68k_execute(455)` →
   `paula_step` (colorclock) → `cia_tick` → `cia_tod_hsync`.
3. Fin de trame : `cia_tod_vsync` → `intreq_set(5)` (VBlank) → `kbd_amiga_step` → profiling
   (`VGA32_DEBUG`) → `vTaskDelay(1)`.
4. En parallèle, FabGL (cœur occupé) rescanne en continu le FB interne et génère le signal VGA.

## Dual-target / régression

`tests/pc/` est le *ground truth* : mêmes sources chipset, rendu PPM/BMP/WAV, 10 sentinelles.
Toute modif du cœur doit garder les sentinelles vertes avant d'atteindre la cible.
