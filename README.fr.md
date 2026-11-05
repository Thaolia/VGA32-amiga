# Amiga 500 sur TTGO VGA32 (FabGL)

[English](README.md) · **Français**

Portage de l'émulateur **[Makerstown/amiga500-esp32](https://github.com/Makerstown/amiga500-esp32)**
(émulateur Amiga 500 OCS complet : CPU 68000 Musashi, chipset Agnus/Denise/Paula/2×CIA, boot
Kickstart 1.3 → Workbench) depuis l'ESP32-S3 + écran TFT vers une carte **LilyGO TTGO VGA32 v1.4**
(ESP32 classique + PSRAM) : **sortie VGA** via [FabGL](https://github.com/fdivitto/FabGL), clavier et
souris en PS/2 ou depuis le PC par le câble USB.

> ⚠️ Ce dépôt ne contient **aucun** matériel Amiga sous copyright. Le build par défaut utilise
> **[KickSlopStart](https://github.com/Thaolia/KickSlopStart)** (Kickstart 1.3 clean-room, MIT),
> récupéré automatiquement ; vos disquettes ADF viennent de la carte SD. Les autres variantes
> embarquent votre propre ROM Kickstart 1.3 et un ADF, légalement obtenus (voir plus bas).

## Flasher sans compiler (navigateur)

Chaque commit sur `main` publie une [release](https://github.com/Thaolia/VGA32-amiga/releases/latest)
contenant **une image unique** `VGA32-amiga-kss-kickonly.bin` (bootloader + partitions + appli,
env `ttgo-vga32-kss-kickonly`).

1. Télécharger la dernière image :
   **[VGA32-amiga-kss-kickonly.bin](https://github.com/Thaolia/VGA32-amiga/releases/latest/download/VGA32-amiga-kss-kickonly.bin)**
2. Ouvrir **[ESPTool de Spacehuhn](https://esptool.spacehuhn.com/)** (navigateur avec Web Serial : Chrome, Edge…) et
   cliquer sur *Connect* (choisir le port de la carte).
3. Choisir le `.bin` en mode fichier unique, **offset `0`** (l'image démarre à 0x0), puis *Program*.
4. Cliquer sur *Disconnect* (ou fermer l'onglet) : sinon le port série reste verrouillé par le
   navigateur et `remote_input.py` ne peut pas l'ouvrir.

Mettez vos `.adf` sur la microSD (`wb.adf` est inséré au boot, voir plus bas).

## État d'avancement

| Phase | Contenu | Statut |
|---|---|---|
| **Cœur Musashi** | reconstruction forme pointeur (tables en PSRAM), conf 68000-only | ✅ validé (sentinelles PC) |
| **Phase 0** | tronc : VGA FabGL + alloc PSRAM + boucle trame, mesure go/no-go | ✅ GO mesuré, Workbench 1.3 boote |
| **Phase 1** | souris, chargement ADF depuis la microSD, changement de disquette (IO36) | ✅ validé sur la carte |
| **Phase 2 — clavier** | clavier Amiga émulé (SDR CIA-A, mapping positionnel), PS/2 ou série USB, reset Ctrl-Amiga-Amiga | ✅ validé sur la carte |
| **Phase 3 — audio** | ring Paula → DAC GPIO25 | ✅ validé sur la carte |
| **Actuel** | maintenance + perf, fidélité matérielle (analyses KickSlopStart) | 🔧 en cours |

## Matériel

Carte **TTGO VGA32 v1.4** (ESP32 **avec PSRAM** — obligatoire ; flash 4 Mo suffit). Connecteur VGA DB15,
2× PS/2 (clavier + souris). Audio : DAC interne GPIO25 (filtre + ampli à câbler). Détail du
câblage et des GPIO : [docs/HARDWARE.md](docs/HARDWARE.md).

## Prérequis

- [PlatformIO](https://platformio.org/) (CLI `pio`).
- Python 3 (pour les scripts `tools/`).
- Accès réseau au premier build par défaut (téléchargement de KickSlopStart, ensuite en cache).
- Pour les variantes `ttgo-vga32` / `-kickonly` / `-prof` / `-stress` seulement : votre **ROM
  Kickstart 1.3** (`kick34005.A500`, 256 Ko, CRC32 `C4F0F55F`) et une **image ADF** (Workbench 1.3
  ou autre) — depuis un Amiga que vous possédez, ou [Amiga Forever](https://www.amigaforever.com).

## Compiler et flasher

```bash
# Build par défaut (= ttgo-vga32-kss-kickonly) : KickSlopStart, aucun fichier à fournir.
# La dernière release KickSlopStart est téléchargée et vérifiée (SHA-256) à chaque build dans
# build/kss/ (repli sur ce cache hors-ligne) ; KSS_TAG=v0.2.1 pio run  -> version figée.
pio run
pio run -t mergebin   # -> .pio/build/<env>/firmware-merged.bin, image unique à flasher à 0x0
pio run -e ttgo-vga32-kss   # KickSlopStart + VOTRE assets/wb_adf.h embarqué

# Variantes avec VOTRE Kickstart 1.3 :
# 1. Embarquer VOS fichiers (2e argument = dossier de sortie 'assets', git-ignoré) :
python3 tools/make_kick_header.py /chemin/vers/kick34005.A500 assets   # -> assets/kick_rom.h
python3 tools/make_adf_header.py  /chemin/vers/workbench13.adf  assets # -> assets/wb_adf.h

# 2. Compiler (côté Linux/WSL2)
pio run -e ttgo-vga32

# Variante « Kickstart seul » : aucun ADF embarqué (assets/wb_adf.h inutile), firmware ~354 Ko
# plus léger. Les disquettes viennent uniquement de la SD ; sans wb.adf, boot à l'invite disque.
pio run -e ttgo-vga32-kickonly    # -> .pio/build/ttgo-vga32-kickonly/firmware.bin

# Variante profiling : ligne [PROF] toutes les 50 trames (temps par étape, voir docs/PORTING.md)
pio run -e ttgo-vga32-prof
```

> **Disquettes sur la carte SD** : la carte VGA32 v1.4 a un slot microSD embarqué. Formatez-la en
> **FAT32** (ou FAT16 — **pas exFAT**, non supporté) et copiez vos `.adf` (880 Ko, DD) à la racine.
> Au boot, **`wb.adf`** est inséré s'il existe (configurable via `VGA32_ADF_FILENAME`), sinon l'ADF
> embarqué sert de repli. Le **bouton K1 (IO36)** fait défiler les `.adf` de la SD (ordre
> alphabétique) : le nom s'affiche 4 s en bas de l'écran, et la disquette est insérée 1,5 s après le
> dernier appui. Le Kickstart, lui, reste embarqué (`kick_rom.h`).

### Flash & moniteur sous Linux natif

```bash
pio run -t upload --upload-port /dev/ttyACM0   # ou /dev/ttyUSB0 (CP2104) ; -e <env> pour une variante
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
Inser = HELP) ; **F12** libère ; **F11** envoie Ctrl-Amiga-Amiga (reset clavier : redémarrage à
chaud, RAM conservée, comme sur un vrai A500). Une manette branchée au PC pilote le joystick
Amiga (port 2 : stick gauche ou croix, bouton 0 = fire). Options : `--mouse-scale 1.5`,
`--joy-keys` (flèches + Ctrl droit = joystick au lieu des touches), `--no-log` (masque les logs série).
Se connecter ne reboote pas la carte. Un simple terminal (`pio device monitor`) permet aussi de
taper du texte (sans souris ni maintien de touche). Désactivable via `VGA32_SERIAL_KBD`.

## Harnais de régression (build PC)

Le cœur émulateur se valide hors matériel via le build PC (ground truth) :

```bash
cd tests/pc
make a500
make testvideo testblit testsprite testscroll testjoy testaudio testreset testlace testcopper \
     testregs testsysreset                                  # 11 sentinelles synthétiques, sans ROM
make testdisk testpaula testblitdiff testdenise testcia     # tests de modules à empreinte figée
make -C ../hal test                                        # logique HAL pure
# avec vos fichiers dans tests/pc/ :
#   cp /chemin/kick34005.A500 kick34005.A500 ; cp /chemin/wb13.adf wb13.adf
make testboot testmfm
```

## Structure

```
src/core/     cœur émulateur (copié de l'upstream, retouches de portage commentées)
src/hal/      couche matérielle VGA32 (VGA FabGL, PS/2, DAC, clavier Amiga)
src/main.cpp  point d'entrée (alloc PSRAM, ROM/ADF, boucle trame)
third_party/  Musashi (reconstruit) + sinfl
tools/        scripts d'embarquement ROM/ADF, récupération KickSlopStart, image fusionnée
tests/pc/     build PC + sentinelles (compile src/core + third_party/musashi)
tests/hal/    tests hôte de la logique HAL pure
docs/         HARDWARE, PORTING, ARCHITECTURE, design
assets/       kick_rom.h / wb_adf.h (git-ignorés, à générer)
build/kss/    ROM + kick_rom.h KickSlopStart (git-ignoré, rempli au build des envs -kss)
.github/      release binaire à chaque commit sur main
```

## Licence

[MIT](LICENSE). Émulateur © Massimiliano ; cœur CPU [Musashi](https://github.com/kstenerud/Musashi)
© Karl Stenerud ; inflate [sinfl](https://github.com/vurtun/lib) © Micha Mettke. Les ROM et images
disque restent la propriété de leurs ayants droit et ne sont **pas** incluses.
