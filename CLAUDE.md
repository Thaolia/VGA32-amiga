# CLAUDE.md — Amiga 500 sur TTGO VGA32 (FabGL)

OS opérationnel du projet. Portage de l'émulateur Amiga 500 `amiga500-esp32` (ESP32-S3 + TFT)
vers la carte **TTGO VGA32 v1.4** (ESP32 classique + PSRAM) avec sortie **VGA (FabGL)** et
entrées **PS/2**. Objectif : booter Workbench 1.3, bureau utilisable à la souris.

## Discipline de développement (ordre impératif)

- **Ground truth d'abord.** Toute modif du cœur chipset se valide sur le build PC
  (`tests/pc/`, 10 sentinelles) AVANT la cible. Ne jamais toucher la cible sur une régression PC.
- **Mesurer, pas deviner.** Les décisions perf partent des chiffres de profiling série
  (fps, heap), jamais d'une impression. Les corrections de bug partent d'une valeur loggée.
- **Un changement à la fois**, vérifié, avant le suivant.
- **Avant d'écrire du code qui touche une signature cœur** (`denise_line_cb`, API input, ring
  Paula, CIA) : lire le fichier réel dans `src/core/` — ne pas supposer.

## Phase 0 = porte go/no-go (NE PAS la court-circuiter)

- Le tronc (Musashi + `video_vga` + `main`) est **séquentiel, un seul propriétaire**. Ne PAS
  lancer de construction parallèle (souris/audio/clavier) avant un **GO** mesuré.
- **GO** si fps soutenu ≥ 10 ET heap interne libre ≥ 40 Ko ET VGA stable sous charge PSRAM.
- **CONDITIONNEL (5-10 fps)** : dégrader (lores forcé, frameskip, moins de bitplans), re-mesurer.
- **NO-GO (<5 fps / sync instable / heap <20 Ko)** : rapporter, appliquer un repli documenté dans
  `docs/PORTING.md` (VGADirectController + shadow PSRAM, baisse résolution), NE PAS forcer.

## Build & flash (voir README pour le détail)

- Build : `pio run -e ttgo-vga32` (côté Linux/WSL2).
- **Flash sous WSL2 : via l'esptool Windows** (l'accès USB série passe par Windows), PAS
  `pio ... -t upload`. Copier `boot_app0.bin` dans `.pio/build/ttgo-vga32/`, puis depuis ce
  dossier : `python3.exe -mesptool --chip esp32 --port COM<x> --baud 921600 write_flash
  0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin`.
  Moniteur : `python3.exe -mserial.tools.miniterm COM<x> 115200`. Détail dans le README.
- Les scripts d'embarquement prennent le dossier de sortie en 2e arg : `... <fichier> assets`.
- **Le firmware ne compile/boote qu'avec `assets/kick_rom.h` et `assets/wb_adf.h` générés** depuis
  les fichiers légaux de l'utilisateur (`tools/make_kick_header.py`, `make_adf_header.py`). Ces
  fichiers sont git-ignorés. Un placeholder bidon (zéros) peut exister pour un test de compilation
  seulement — il NE boote PAS ; ne jamais le confondre avec un vrai ROM.
- Régression cœur : dans `tests/pc/`, `make a500 && make testvideo testblit testsprite testscroll testjoy testaudio`
  (6 sentinelles sans ROM). `testboot`/`testmfm` exigent le Kickstart/ADF de l'utilisateur.

## Routage agent seul vs sous-agents parallèles

- **Seul** : tronc Phase 0, et tout ce qui touche `src/core/*` ou l'ordonnancement des cœurs
  (couplé, non parallélisable).
- **Sous-agents parallèles** (APRÈS un GO, fichiers disjoints) : Souris (`input_ps2`), Audio
  (`audio_dac`), Build/docs. Clavier (`kbd_amiga` + `cia.cpp`) reste séquentiel (couplage CIA).

## Conventions de code

- Commentaires **en français** ; expliquer le POURQUOI non-évident, pas le QUOI.
- `src/core/*` : copié de l'upstream, à **ne pas** réécrire. Seules retouches admises : hooks HAL.
  Garder les `.c` Musashi compilés en **C** (le code suppose la liaison C ; `m68k.h` sous `extern "C"`).
- `a500.h` et le cœur se compilent en **C++** : inclure `a500.h` SANS `extern "C"` ; seul `m68k.h` l'exige.
- Prints en chemin critique sous `#if VGA32_DEBUG` (`print()` bloque l'UART TX sans terminal) ;
  prints d'ERREUR non gardés.
- Framebuffer FabGL : toujours écrire `row[x ^ 2]` (entrelacement DMA), jamais `row[x]`.
- Correctness > maintainability > performance > brièveté. Pas de code mort.

## Quality gates (avant de déclarer « fait »)

- Cœur modifié → les 6 sentinelles PC synthétiques repassent au vert.
- Build cible → `pio run -e ttgo-vga32` compile sans tirer d'API arduino-esp32 3.x.
- Changement observable → vérifié sur le log série / le moniteur VGA réel, pas supposé.
- Après toute implémentation : mettre à jour `docs/ARCHITECTURE.md` et `docs/PORTING.md` dans la
  MÊME session.

## Où vit quoi (liens simples, lus à la demande)

- Câblage carte, pinout VGA/PS2/DAC, GPIO libres : `docs/HARDWARE.md`
- Décisions de portage, cartographie S3→VGA32, reconstruction Musashi, replis : `docs/PORTING.md`
- Vue d'ensemble des modules et du flux : `docs/ARCHITECTURE.md`
- Design validé (contexte, phases, go/no-go) : `docs/superpowers/specs/2026-10-06-portage-vga32-amiga-design.md`

## Copyright

Aucun matériel Amiga (ROM, Workbench, jeux) dans le dépôt. JAMAIS committer `kick_rom.h`/`wb_adf.h`
ni un `.rom`/`.adf`. Émulateur MIT © Massimiliano ; Musashi MIT © Karl Stenerud.
