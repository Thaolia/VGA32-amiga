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
- `video_vga.cpp` : `VGAController` 64 c. ; LUT couleur ; callback Denise en **IRAM** (blit
  `row[x^2]`, `g_row_onscreen`) ; surimpression texte 8x8 (`video_vga_osd_show/tick`) avec
  sauvegarde/restauration des pixels sous le cadre.
- `input_ps2.cpp` : **souris PS/2 (P1, implémentée)** → `input_mouse_delta`/`input_set_lmb/rmb`
  du cœur ; clavier (P2) à venir. Bornage ±100/trame avec report.
- `sdcard.cpp` : **ADF depuis la microSD embarquée (P1, implémenté)** — montage (FAT16/32) et
  liste triée des `.adf` DD de la racine au boot ; lecture par blocs de 512 o dans un buffer donné.
- `disk_switch.cpp` + `disk_select.cpp` : **changement de disquette au bouton IO36** — logique pure
  (anti-rebond, sélection circulaire, validation 1,5 s après le dernier appui) + glue : OSD du
  nom, `drive_eject` → tâche `adfload` (lecture SD sur le cœur VGA) → `drive_mount_ready`.
- `kbd_amiga.cpp` : **clavier Amiga émulé (P2, implémenté)** — FIFO de rawcodes → SDR de CIA-A
  (`cia_a_kbd_shift_in`) + IRQ série (INT2). Table VirtualKey→rawcode positionnelle dans
  `input_ps2.cpp`. `core/cia.cpp` : SDR (reg 0xC) lit désormais le registre + nouveau hook.
- `serial_kbd.cpp` : **clavier + souris via port série USB (implémenté)** — octets de `Serial`
  démultiplexés par `serial_proto.cpp` : trames binaires de `tools/remote_input.py` (rawcode
  exact → `kbd_amiga`, souris → `input_mouse_delta`/`input_set_lmb/rmb`, bornage ±100/trame) ;
  octets hors trame = mode texte (table ASCII→rawcode US + ESC[ → `kbd_amiga`). En parallèle du PS/2.
- `serial_proto.cpp` : décodeur pur (sans Arduino) des trames `0xA5 TYPE PAYLOAD CHK`, testé hôte
  dans `tests/hal/`.
- `audio_dac.cpp` : **audio (P3, implémenté)** — `WaveformGenerator` custom → ring Paula
  (`paula_ring_pop`, 44100 Hz) downmixé mono 8 bits → `SoundGenerator` DAC GPIO25. Démarré depuis
  `emu_task` après `paula_reset`.

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

`tests/pc/` est le *ground truth* : rendu PPM/BMP/WAV, 8 sentinelles (6 synthétiques + `testboot`/`testmfm`
qui exigent Kickstart/ADF). Il compile **directement** `src/core/` et `third_party/musashi/` (source
unique) ; seuls `tests/pc/src/main.cpp` (boucle PC, sorties PPM/WAV) et `hook_decl.h` lui sont propres.
Le rendu de ligne (`denise_render_line`) exécute la même boucle bitplanes/scroll/sprites sur PC et sur
la carte ; le PC double ensuite les pixels lores dans `fb[]`. Non couvert : tout bloc `#ifdef ARDUINO`.
Toute modif du cœur doit garder les sentinelles vertes avant d'atteindre la cible.
