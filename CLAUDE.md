# CLAUDE.md — Amiga 500 sur TTGO VGA32 (FabGL)

OS opérationnel du projet. Portage de l'émulateur `amiga500-esp32` (ESP32-S3 + TFT) vers la
**TTGO VGA32 v1.4** (ESP32 classique rev1 + PSRAM), sortie **VGA (FabGL)**. Phases 0-3 livrées
(GO mesuré, Workbench 1.3 boote, souris/clavier/audio/changement de disquette). Mode actuel :
**maintenance + perf** — toute modif doit préserver les valeurs de référence ci-dessous.

## Principes (ordre impératif)

- **Ground truth d'abord.** Une modif chipset se valide sur le build PC (`tests/pc/`) AVANT la
  cible. Ne jamais flasher une régression PC.
- **Mesurer, pas deviner.** Décision perf = chiffres série (fps, heap) avant/après. Correction de
  bug = partir d'une valeur loggée, pas d'une hypothèse.
- **Un changement à la fois**, vérifié, avant le suivant.
- **Lire le fichier réel** avant de toucher une signature cœur (`denise_line_cb`, API input, ring
  Paula, CIA, `serial_proto`) — ne pas supposer.
- **Le matériel tranche.** Un changement observable n'est « fait » qu'après lecture du log série
  ou observation du moniteur VGA ; une compilation propre est nécessaire, jamais suffisante.

## Harnais PC : copie divergente — piège critique

- `tests/pc/` compile **sa propre copie** `tests/pc/src/*.cpp`, PAS `src/core/`. Les deux arbres
  divergent (constaté 2026-10-07 : 7/10 fichiers, dont `video.cpp` et la sémantique TOD de `cia.cpp`).
- Donc : modifier `src/core/X` **sans** reporter dans `tests/pc/src/X` = les sentinelles testent
  autre chose que la cible. Toute modif d'un fichier cœur se reporte dans les deux arbres, ou
  le rapport dit explicitement « non couvert par le harnais PC ».
- Ne jamais écrire « les sentinelles passent » comme preuve qu'une modif `src/core/` est saine
  sans avoir vérifié que le fichier touché est identique (`cmp src/core/X tests/pc/src/X`) ou
  que la divergence ne concerne que des hooks HAL.

## Garde-fous perf (valeurs de référence, voir `docs/PORTING.md`)

- Référence : boot ~24 fps trame 150, ~34,6 fps au repos, 12-25 fps en accès disque, heap interne
  libre ~170 Ko. Après toute modif du chemin vidéo/CPU : re-mesurer et comparer à ces chiffres.
- Régression ≥ 5 % ou trame qui explose → chercher d'abord un conflit de cache flash/PSRAM :
  tout code appelé par ligne (`denise_cb`, `osd_draw_row`, callbacks vidéo) doit être `IRAM_ATTR`.
- Plancher absolu : ≥ 10 fps soutenu ET heap interne ≥ 40 Ko ET VGA stable. Sous ce plancher :
  appliquer un repli de `docs/PORTING.md` (lores forcé, frameskip…), ne pas forcer.
- Ne JAMAIS retirer `-mfix-esp32-psram-cache-issue` sans avoir mesuré la révision silicium
  (`esptool chip_id`) : la carte de référence est rev1 → corruption PSRAM sans ce flag.

## Build & flash

- Build : `pio run -e ttgo-vga32`. Le chemin de `pio` et le port série dépendent de la machine :
  voir `CLAUDE.local.md` (WSL2 : esptool Windows, cf. README).
- Toolchain figée : `espressif32@6.10.0` → arduino-esp32 **2.0.17** (FabGL + I2S legacy). Ne
  jamais utiliser d'API arduino-esp32 3.x ni monter la plateforme sans le demander.
- Le firmware ne boote qu'avec `assets/kick_rom.h` et `assets/wb_adf.h` générés depuis les
  fichiers légaux de l'utilisateur (`tools/make_kick_header.py`, `tools/make_adf_header.py`, dossier
  de sortie en 2e arg : `... <fichier> assets`). Un placeholder (zéros) compile mais NE boote PAS —
  ne jamais conclure à un bug de boot sans vérifier lequel est présent.
- Ouvrir le port série (pyserial, miniterm) **reset la carte** sauf si DTR/RTS restent asserted.
  Ne pas interpréter un reboot à la connexion comme un crash ; pour un reset voulu : pulse RTS, DTR bas.
- `VGA32_DEBUG` vaut **1 par défaut** (`src/hal/platform_esp32.h`) : les mesures fps/heap supposent
  ce mode. Pour une mesure « production », compiler avec `-DVGA32_DEBUG=0` et le dire.

## Entrées : l'UART, pas le PS/2

- Les clavier/souris de l'utilisateur sont USB derrière des adaptateurs PS/2 passifs → le PS/2
  ne fonctionne pas sur son poste. Le chemin d'entrée réel est l'UART : `tools/remote_input.py`
  (PC, encodeur) → UART → `src/hal/serial_kbd.cpp` (clavier + souris) qui démultiplexe via
  `serial_proto.*` (trames `0xA5`, octets hors trame = ASCII terminal).
- Ne pas déboguer `input_ps2` / `kbd_amiga` côté PS/2 sur le matériel de l'utilisateur ; tester
  via `remote_input.py`. Toute modif du protocole série = modif des DEUX côtés + `tests/hal`.
- Changement de disquette : bouton IO36 (`disk_switch`, `disk_select`) — logique pure testée dans
  `tests/hal`.

## Routage agent seul vs sous-agents

- **Seul** : tout ce qui touche `src/core/*`, `tests/pc/src/*`, l'ordonnancement des cœurs/tâches,
  `video_vga.cpp` (chemin chaud IRAM), et le couple `kbd_amiga` + `cia.cpp` (couplage CIA).
- **Sous-agents parallèles** (fichiers disjoints uniquement) : `audio_dac`, `sdcard`/`disk_*`,
  `tools/*.py`, docs. Chaque sous-agent reçoit la consigne de ne pas toucher `src/core/`.
- Recherche transverse (« où est géré X ? ») sur plus de ~3 fichiers → agent Explore.

## Conventions de code

- Commentaires **en français** ; expliquer le POURQUOI non-évident, pas le QUOI.
- `src/core/*` : copié de l'upstream, à **ne pas** réécrire. Seules retouches admises : hooks HAL.
  Les commentaires italiens de l'upstream restent tels quels.
- Musashi vit dans `third_party/musashi` (non modifié, configuré par `-D`) : compilé en **C** ;
  `m68k.h` s'inclut sous `extern "C"`. `a500.h` et le cœur sont du **C++** : inclure `a500.h` SANS
  `extern "C"`.
- Prints en chemin critique sous `#if VGA32_DEBUG` ; prints d'ERREUR non gardés.
- Framebuffer FabGL : toujours `row[x ^ 2]` (entrelacement DMA), jamais `row[x]`.
- Code appelé par ligne vidéo ou depuis une ISR : `IRAM_ATTR`, pas d'allocation, pas de print.
- Gros buffers (ROM, ADF décompressé, chip RAM) en PSRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)`
  avec test `!= NULL` ; piles de tâches en DRAM interne.

## Quality gates (avant de déclarer « fait »)

1. Cœur modifié → `cd tests/pc && make a500 && make testvideo testblit testsprite testscroll
   testjoy testaudio` (6 sentinelles synthétiques, sans ROM) au vert, ET le fichier touché reporté
   dans `tests/pc/src/` (cf. piège ci-dessus). `testboot`/`testmfm` exigent `kick34005.A500` /
   `wb13.adf` de l'utilisateur dans `tests/pc/` : les signaler comme non exécutés s'ils manquent.
2. HAL pure modifiée (`serial_proto`, `disk_select`) → `make -C tests/hal test` au vert.
3. Cible → `pio run -e ttgo-vga32` compile sans warning nouveau.
4. Changement observable → log série lu (fps/heap comparés à la référence) ou moniteur VGA.
   Si l'utilisateur doit observer lui-même, le dire explicitement : « non vérifié sur matériel ».
5. Docs mises à jour dans la MÊME session : `docs/ARCHITECTURE.md`, `docs/PORTING.md`, et ce
   fichier si une règle a changé.

## Où vit quoi

Chemins simples, lus à la demande — volontairement PAS en `@import`, qui chargerait tout au démarrage.

- Câblage, pinout VGA/PS2/DAC, GPIO libres, pont USB (CH9102/CP2104) : `docs/HARDWARE.md`
- Décisions de portage, mesures, replis, historique perf : `docs/PORTING.md`
- Modules et flux : `docs/ARCHITECTURE.md`
- Design initial (phases, go/no-go) : `docs/superpowers/specs/2026-10-06-portage-vga32-amiga-design.md`
- Spécificités de la machine locale (chemin pio, port série, OS) : `CLAUDE.local.md` (non versionné)

## Auto-amélioration

- Une erreur qui se reproduit deux fois, ou un piège matériel découvert (reset série, cache,
  rev silicium…) → proposer une ligne ici dans la même session.
- Un fait (valeur mesurée, pinout, commande) va dans `docs/`, pas ici ; ici seulement l'instruction.
- Si une règle de ce fichier contredit le code ou les docs : le signaler à l'utilisateur et
  proposer la correction, ne pas suivre silencieusement la version fausse.
- Audit périodique : `/claude-md-optimizer`.

## Copyright

Aucun matériel Amiga (ROM, Workbench, jeux) dans le dépôt. JAMAIS committer `kick_rom.h`/`wb_adf.h`
ni un `.rom`/`.adf`/`.A500`. Émulateur MIT © Massimiliano ; Musashi MIT © Karl Stenerud.
