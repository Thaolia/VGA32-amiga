# Matériel — TTGO VGA32 v1.4

## Carte

- **SoC** : ESP32 classique (dual-core LX6 @240 MHz), module **WROVER avec PSRAM** (v1.4).
  La PSRAM est **obligatoire** (l'émulateur alloue ~2,4 Mo). Les révisions sans PSRAM (v1.2,
  WROOM) ne conviennent pas.
- **PSRAM** : QSPI (quad) — ~4 Mo adressables. Bande passante nettement inférieure à l'OPI octal
  du S3 d'origine : c'est le facteur limitant du framerate.
- **Connecteurs** : VGA DB15, 2× PS/2 (clavier + souris), **slot microSD embarqué**, jack audio
  3.5 mm + ampli + haut-parleur (embarqués), micro-USB (CP2104, prog/alim), connecteur batterie
  (chargeur TP4054).

## Brochage (câblé en dur sur la carte)

| Fonction | GPIO | Note |
|---|---|---|
| VGA Rouge R1/R0 | 22 / 21 | = défaut `fabgl::VGAController::begin()` |
| VGA Vert G1/G0 | 19 / 18 | |
| VGA Bleu B1/B0 | 5 / 4 | 64 couleurs = RGB222 |
| VGA HSync / VSync | 23 / 15 | |
| PS/2 clavier (port 0) CLK / DATA | 33 / 32 | géré par l'ULP (FabGL) |
| PS/2 souris (port 1) CLK / DATA | 26 / 27 | |
| Audio (DAC → ampli NS4150 + jack 3.5 mm + HP) | 25 | filtre RC + ampli **embarqués** |
| microSD embarqué (SPI) CS / CLK / MOSI / MISO | 13 / 14 / 12 / 2 | MISO = DAT0 = IO2 |
| LED embarquée | 2 | **partagée avec SD-MISO** |
| USB-série (CP2104) | 1 / 3 | TX0 / RX0 |
| Flash interne | 6-11 | inutilisables |
| PSRAM (IPS6404, interne WROVER) | 16 / 17 | réservés |

Comme ces broches correspondent aux valeurs par défaut de FabGL, le code appelle
`VGAController::begin()` **sans argument** et `PS2Controller::begin(PS2Preset::KeyboardPort0_MousePort1)`.

## Carte SD (microSD) — slot EMBARQUÉ

Le schéma officiel v1.4 (connecteur **J5**, source :
[LilyGO/FabGL · Schematic/vga32_v1.4.pdf](https://github.com/LilyGO/FabGL/blob/master/Schematic/vga32_v1.4.pdf))
montre un **slot microSD embarqué** câblé en **mode SPI** :

| SD (SPI) | GPIO |
|---|---|
| CS (DAT3) | 13 |
| CLK | 14 |
| MOSI (CMD) | 12 |
| MISO (DAT0) | 2 |

Ces broches **n'entrent en conflit NI avec la VGA, NI le PS/2, NI l'audio** → charger une disquette
depuis la SD ne coûte aucun périphérique **et sans matériel additionnel** (le slot est sur la carte).
Remarques (gérées au niveau carte) : **IO2** est partagée avec la LED embarquée (SD DAT0/MISO) ;
**IO12** est une broche de strapping (tension flash). Côté firmware : ne pas se fier aux pins par
défaut de FabGL `mountSDCard` ; passer explicitement **CS=13, SCK=14, MOSI=12, MISO=2** (bibliothèque
Arduino `SD`/`SPI`, ou SDMMC).

## GPIO libres (pour extensions)

Le header **J2** (2×4) expose **IO39, IO34, IO2, IO13, IO12, IO14** (+3V3/GND). Comme **2/12/13/14**
vont au slot SD embarqué :
- **Avec SD** : il reste surtout **IO34 / IO39** (input-only) sur le header.
- **Sans SD** : **13, 14** (full-GPIO) sont les meilleurs candidats pour une entrée custom (ex.
  clavier Amiga physique) ; prévoir ses pull-ups.
- En renonçant à un périphérique : **26/27** (souris) ou **25** (audio) se libèrent aussi.
- **34 / 39** : input-only (pas de sortie). **IO35** sert à la mesure de tension batterie (ADC).

## Audio

Sortie **embarquée** (schéma p.3) : DAC interne **GPIO25** → filtre RC (R28 270R / C13 33nF / R30
150R) → ampli **NS4150** (U8) → **jack 3.5 mm (J3)** + haut-parleur. Pas de MAX98357A I2S externe
comme sur le S3. Côté firmware : `fabgl::SoundGenerator` (DAC GPIO25, I2S0, indépendant de l'I2S1
de la VGA), mono 8 bits. L'ampli est alimenté via SPK_VDD (régulateur ME6211).

## Alimentation

Source 5 V solide recommandée (la génération VGA + PSRAM + CPU à pleine charge tire du courant).
Un condensateur de découplage généreux évite les brownouts au boot.

## Révision silicium

Vérifier via `esptool.py chip_id` (ou `pio run -t ... ` + esptool). Les ESP32 **rev1** nécessitent
le flag `-mfix-esp32-psram-cache-issue` (coûteux en débit PSRAM) ; les **rev3** s'en passent — à
décider en Phase 0 (cf. `platformio.ini` et `docs/PORTING.md`).
