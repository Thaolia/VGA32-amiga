# Amiga 500 sur TTGO VGA32 (FabGL)

Portage de l'émulateur **[Makerstown/amiga500-esp32](https://github.com/Makerstown/amiga500-esp32)**
(émulateur Amiga 500 OCS complet : CPU 68000 Musashi, chipset Agnus/Denise/Paula/2×CIA, boot
Kickstart 1.3 → Workbench) depuis l'ESP32-S3 + écran TFT vers une carte **LilyGO TTGO VGA32 v1.4**
(ESP32 classique + PSRAM) : **sortie VGA** via [FabGL](https://github.com/fdivitto/FabGL) et
**entrées PS/2** (souris, puis clavier).

> ⚠️ Ce dépôt ne contient **aucun** matériel Amiga sous copyright. Vous fournissez votre propre
> ROM Kickstart 1.3 et une image disque ADF, légalement obtenues (voir plus bas).

## État d'avancement

| Phase | Contenu | Statut |
|---|---|---|
| **Cœur Musashi** | reconstruction forme pointeur (tables en PSRAM), conf 68000-only | ✅ validé (6/6 sentinelles PC) |
| **Phase 0** | tronc : VGA FabGL + alloc PSRAM + boucle trame, mesure go/no-go | 🔧 code écrit, à flasher |
| **Phase 1 — souris** | souris PS/2 → port souris Amiga (bureau utilisable) | 🔧 implémenté, à tester sur matériel |
| **Phase 1 — SD** | chargement ADF depuis la microSD embarquée | 🔧 implémenté, à tester sur matériel |
| **Phase 2 — clavier** | clavier Amiga (nouveau) : SDR CIA-A + mapping positionnel. Entrée **PS/2 et/ou série USB** | 🔧 implémenté, à tester sur matériel |
| **Phase 3 — audio** | ring Paula → DAC GPIO25 (FabGL SoundGenerator) | 🔧 implémenté, à tester sur matériel |

## Matériel

Carte **TTGO VGA32 v1.4** (ESP32 **avec PSRAM** — obligatoire ; flash 4 Mo suffit). Connecteur VGA DB15,
2× PS/2 (clavier + souris). Audio : DAC interne GPIO25 (filtre + ampli à câbler). Détail du
câblage et des GPIO : [docs/HARDWARE.md](docs/HARDWARE.md).

## Prérequis

- [PlatformIO](https://platformio.org/) (CLI `pio`).
- Python 3 (pour les scripts `tools/`).
- Votre **ROM Kickstart 1.3** (`kick34005.A500`, 256 Ko, CRC32 `C4F0F55F`) et une **image ADF**
  (Workbench 1.3 ou autre) — depuis un Amiga que vous possédez, ou [Amiga Forever](https://www.amigaforever.com).

## Compiler et flasher

```bash
# 1. Embarquer VOS fichiers (2e argument = dossier de sortie 'assets', git-ignoré) :
python3 tools/make_kick_header.py /chemin/vers/kick34005.A500 assets   # -> assets/kick_rom.h
python3 tools/make_adf_header.py  /chemin/vers/workbench13.adf  assets # -> assets/wb_adf.h

# 2. Compiler (côté Linux/WSL2)
pio run -e ttgo-vga32

# Variante « Kickstart seul » : aucun ADF embarqué (assets/wb_adf.h inutile), firmware ~354 Ko
# plus léger. Les disquettes viennent uniquement de la SD ; sans wb.adf, boot à l'invite disque.
pio run -e ttgo-vga32-kickonly    # -> .pio/build/ttgo-vga32-kickonly/firmware.bin
```

> **Disquettes sur la carte SD** : la carte VGA32 v1.4 a un slot microSD embarqué. Formatez-la en
> **FAT32** (ou FAT16 — **pas exFAT**, non supporté) et copiez vos `.adf` (880 Ko, DD) à la racine.
> Au boot, **`wb.adf`** est inséré s'il existe (configurable via `VGA32_ADF_FILENAME`), sinon l'ADF
> embarqué sert de repli. Le **bouton K1 (IO36)** fait défiler les `.adf` de la SD (ordre
> alphabétique) : le nom s'affiche 4 s en bas de l'écran, et la disquette est insérée 1,5 s après le
> dernier appui. Le Kickstart, lui, reste embarqué (`kick_rom.h`).

### Flash & moniteur sous Linux natif

```bash
pio run -e ttgo-vga32 -t upload --upload-port /dev/ttyACM0   # ou /dev/ttyUSB0 (CP2104)
pio device monitor -p /dev/ttyACM0
```

### Flash & moniteur sous WSL2

L'accès USB série passe par **Windows** : on compile avec `pio` (Linux) puis on flashe le `.bin`
avec l'**esptool Windows**. Le projet étant sous `/mnt/c/...` (chemin Windows), les binaires de
`.pio/build/` sont accessibles côté Windows. Remplacez `COM11` par votre port.

```bash
# copier boot_app0.bin à côté des autres binaires (accessible côté Windows)
cp ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
   .pio/build/ttgo-vga32/
cd .pio/build/ttgo-vga32

# flash (offsets standard ESP32 Arduino)
python3.exe -mesptool --chip esp32 --port COM11 --baud 921600 write_flash \
  0x1000 bootloader.bin  0x8000 partitions.bin  0xe000 boot_app0.bin  0x10000 firmware.bin

# moniteur série (pyserial, fourni avec esptool)
python3.exe -mserial.tools.miniterm COM11 115200
```

Au boot, le moniteur série affiche l'alloc PSRAM, la décompression de l'ADF, le reset du 68000
(`PC=FC00D2`), puis un profiling par trame (fps, heap). Le bureau Workbench apparaît sur le
moniteur VGA.

### Clavier + souris sans PS/2 (via le câble USB)

Les adaptateurs USB→PS/2 *passifs* ne marchent qu'avec des périphériques « dual-protocol ». Sans
vrai PS/2, le clavier et la souris du PC passent par le port série :

```bash
python3 -m venv ~/.venvs/vga32-remote
~/.venvs/vga32-remote/bin/pip install pygame-ce pyserial     # ou pygame >= 2 déjà installé
~/.venvs/vga32-remote/bin/python tools/remote_input.py --port /dev/ttyACM0
```

Cliquer dans la fenêtre capture clavier + souris (touches maintenues, Ctrl/Alt/Amiga = Super,
Inser = HELP) ; **F12** libère. Options : `--mouse-scale 1.5`, `--no-log` (masque les logs série).
Se connecter ne reboote pas la carte. Un simple terminal (`pio device monitor`) permet aussi de
taper du texte (sans souris ni maintien de touche). Désactivable via `VGA32_SERIAL_KBD`.

## Harnais de régression (build PC)

Le cœur émulateur se valide hors matériel via le build PC (ground truth) :

```bash
cd tests/pc
make a500
make testvideo testblit testsprite testscroll testjoy testaudio   # 6 sentinelles, sans ROM
# avec vos fichiers dans tests/pc/ :
#   cp /chemin/kick34005.A500 kick34005.A500 ; cp /chemin/wb13.adf wb13.adf
make testboot testmfm
```

## Structure

```
src/core/     cœur émulateur (copié de l'upstream, non modifié)
src/hal/      couche matérielle VGA32 (VGA FabGL, PS/2, DAC, clavier Amiga)
src/main.cpp  point d'entrée (alloc PSRAM, ROM/ADF, boucle trame)
third_party/  Musashi (reconstruit) + sinfl
tools/        scripts d'embarquement ROM/ADF
tests/pc/     build PC + 8 sentinelles (compile src/core + third_party/musashi)
docs/         HARDWARE, PORTING, ARCHITECTURE, design
assets/       kick_rom.h / wb_adf.h (git-ignorés, à générer)
```

## Licence

[MIT](LICENSE). Émulateur © Massimiliano ; cœur CPU [Musashi](https://github.com/kstenerud/Musashi)
© Karl Stenerud ; inflate [sinfl](https://github.com/vurtun/lib) © Micha Mettke. Les ROM et images
disque restent la propriété de leurs ayants droit et ne sont **pas** incluses.
