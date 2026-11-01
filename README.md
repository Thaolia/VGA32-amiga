# Amiga 500 on TTGO VGA32 (FabGL)

**English** · [Français](README.fr.md)

Port of the **[Makerstown/amiga500-esp32](https://github.com/Makerstown/amiga500-esp32)** emulator
(complete OCS Amiga 500 emulator: Musashi 68000 CPU, Agnus/Denise/Paula/2×CIA chipset, Kickstart 1.3
→ Workbench boot) from the ESP32-S3 + TFT screen to a **LilyGO TTGO VGA32 v1.4** board (classic
ESP32 + PSRAM): **VGA output** through [FabGL](https://github.com/fdivitto/FabGL), keyboard and mouse
over PS/2 or from the PC through the USB cable.

> ⚠️ This repository contains **no** copyrighted Amiga material. The default build uses
> **[KickSlopStart](https://github.com/Thaolia/KickSlopStart)** (clean-room Kickstart 1.3, MIT),
> fetched automatically; your ADF disks come from the SD card. The other variants embed your own
> legally obtained Kickstart 1.3 ROM and an ADF (see below).

## Flash without building (browser)

Every commit on `main` publishes a [release](https://github.com/Thaolia/VGA32-amiga/releases/latest)
containing **a single image** `VGA32-amiga-kss-kickonly.bin` (bootloader + partitions + app,
env `ttgo-vga32-kss-kickonly`).

1. Download the latest image:
   **[VGA32-amiga-kss-kickonly.bin](https://github.com/Thaolia/VGA32-amiga/releases/latest/download/VGA32-amiga-kss-kickonly.bin)**
2. Open **[Spacehuhn's ESPTool](https://esptool.spacehuhn.com/)** (a browser with Web Serial:
   Chrome, Edge…) and click *Connect* (pick the board's port).
3. Choose the `.bin` in single-file mode, **offset `0`** (the image starts at 0x0), then *Program*.
4. Click *Disconnect* (or close the tab): otherwise the browser keeps the serial port locked and
   `remote_input.py` cannot open it.

Put your `.adf` files on the microSD card (`wb.adf` is inserted at boot, see below).

## Status

| Phase | Content | Status |
|---|---|---|
| **Musashi core** | pointer-form rebuild (tables in PSRAM), 68000-only config | ✅ validated (PC sentinels) |
| **Phase 0** | backbone: FabGL VGA + PSRAM allocation + frame loop, go/no-go measurement | ✅ GO measured, Workbench 1.3 boots |
| **Phase 1** | mouse, ADF loading from the microSD, disk swap (IO36) | ✅ validated on the board |
| **Phase 2 — keyboard** | emulated Amiga keyboard (CIA-A SDR, positional mapping), PS/2 or USB serial, Ctrl-Amiga-Amiga reset | ✅ validated on the board |
| **Phase 3 — audio** | Paula ring → GPIO25 DAC | ✅ validated on the board |
| **Current** | maintenance + performance, hardware accuracy (KickSlopStart analyses) | 🔧 ongoing |

## Hardware

**TTGO VGA32 v1.4** board (ESP32 **with PSRAM** — mandatory; 4 MB flash is enough). DB15 VGA
connector, 2× PS/2 (keyboard + mouse). Audio: internal DAC on GPIO25 (filter + amplifier to wire).
Wiring and GPIO details: [docs/HARDWARE.md](docs/HARDWARE.md) (in French).

## Requirements

- [PlatformIO](https://platformio.org/) (`pio` CLI).
- Python 3 (for the `tools/` scripts).
- Network access on the first default build (KickSlopStart download, cached afterwards).
- Only for the `ttgo-vga32` / `-kickonly` / `-prof` / `-stress` variants: your **Kickstart 1.3
  ROM** (`kick34005.A500`, 256 KB, CRC32 `C4F0F55F`) and an **ADF image** (Workbench 1.3 or
  other) — from an Amiga you own, or [Amiga Forever](https://www.amigaforever.com).

## Build and flash

```bash
# Default build (= ttgo-vga32-kss-kickonly): KickSlopStart, no file to provide.
# The latest KickSlopStart release is downloaded and verified (SHA-256) on every build into
# build/kss/ (falls back to that cache offline); KSS_TAG=v0.2.1 pio run  -> pinned version.
pio run
pio run -t mergebin   # -> .pio/build/<env>/firmware-merged.bin, single image to flash at 0x0
pio run -e ttgo-vga32-kss   # KickSlopStart + YOUR embedded assets/wb_adf.h

# Variants with YOUR Kickstart 1.3:
# 1. Embed YOUR files (2nd argument = output folder 'assets', git-ignored):
python3 tools/make_kick_header.py /path/to/kick34005.A500 assets   # -> assets/kick_rom.h
python3 tools/make_adf_header.py  /path/to/workbench13.adf  assets # -> assets/wb_adf.h

# 2. Build (Linux/WSL2 side)
pio run -e ttgo-vga32

# "Kickstart only" variant: no embedded ADF (assets/wb_adf.h not needed), firmware ~354 KB
# lighter. Disks come from the SD only; without wb.adf, boots to the insert-disk prompt.
pio run -e ttgo-vga32-kickonly    # -> .pio/build/ttgo-vga32-kickonly/firmware.bin

# Profiling variant: [PROF] line every 50 frames (time per stage, see docs/PORTING.md)
pio run -e ttgo-vga32-prof
```

> **Disks on the SD card**: the VGA32 v1.4 board has an on-board microSD slot. Format it as
> **FAT32** (or FAT16 — **not exFAT**, unsupported) and copy your `.adf` files (880 KB, DD) to the
> root. At boot, **`wb.adf`** is inserted if present (configurable through `VGA32_ADF_FILENAME`),
> otherwise the embedded ADF is the fallback. The **K1 button (IO36)** cycles through the SD's
> `.adf` files (alphabetical order): the name is shown for 4 s at the bottom of the screen, and the
> disk is inserted 1.5 s after the last press. The Kickstart itself stays embedded (`kick_rom.h`).

### Flash & monitor on native Linux

```bash
pio run -t upload --upload-port /dev/ttyACM0   # or /dev/ttyUSB0 (CP2104); -e <env> for a variant
pio device monitor -p /dev/ttyACM0
```

### Flash & monitor on WSL2

USB serial access goes through **Windows**: build with `pio` (Linux), then flash the `.bin` with
the **Windows esptool**. Since the project lives under `/mnt/c/...` (a Windows path), the
`.pio/build/` binaries are reachable from Windows. Replace `COM11` with your port.

```bash
# copy boot_app0.bin next to the other binaries (reachable from Windows)
cp ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
   .pio/build/ttgo-vga32/
cd .pio/build/ttgo-vga32

# flash (standard ESP32 Arduino offsets)
python3.exe -mesptool --chip esp32 --port COM11 --baud 921600 write_flash \
  0x1000 bootloader.bin  0x8000 partitions.bin  0xe000 boot_app0.bin  0x10000 firmware.bin

# serial monitor (pyserial, shipped with esptool)
python3.exe -mserial.tools.miniterm COM11 115200
```

At boot, the serial monitor shows the PSRAM allocation, the ADF decompression, the 68000 reset
(`PC=FC00D2`), then per-frame profiling (fps, heap). The Workbench desktop appears on the VGA
monitor.

### Keyboard + mouse without PS/2 (through the USB cable)

Passive USB→PS/2 adapters only work with "dual-protocol" devices. Without real PS/2, the PC's
keyboard and mouse go through the serial port:

```bash
python3 -m venv ~/.venvs/vga32-remote
~/.venvs/vga32-remote/bin/pip install pygame-ce pyserial     # or an installed pygame >= 2
~/.venvs/vga32-remote/bin/python tools/remote_input.py --port /dev/ttyACM0
```

Clicking in the window captures keyboard + mouse (held keys, Ctrl/Alt/Amiga = Super,
Insert = HELP); **F12** releases; **F11** sends Ctrl-Amiga-Amiga (keyboard reset: warm reboot,
RAM kept, as on a real A500). Options: `--mouse-scale 1.5`, `--no-log` (hides serial logs).
Connecting does not reboot the board. A plain terminal (`pio device monitor`) can also type text
(no mouse, no held keys). Can be disabled through `VGA32_SERIAL_KBD`.

## Regression harness (PC build)

The emulator core is validated off-hardware through the PC build (ground truth):

```bash
cd tests/pc
make a500
make testvideo testblit testsprite testscroll testjoy testaudio testreset testlace testcopper \
     testregs testsysreset                                  # 11 synthetic sentinels, no ROM
make testdisk testpaula testblitdiff testdenise testcia     # module tests with frozen fingerprints
make -C ../hal test                                        # pure HAL logic
# with your files in tests/pc/:
#   cp /path/kick34005.A500 kick34005.A500 ; cp /path/wb13.adf wb13.adf
make testboot testmfm
```

## Layout

```
src/core/     emulator core (copied from upstream, porting changes commented)
src/hal/      VGA32 hardware layer (FabGL VGA, PS/2, DAC, Amiga keyboard)
src/main.cpp  entry point (PSRAM allocation, ROM/ADF, frame loop)
third_party/  Musashi (rebuilt) + sinfl
tools/        ROM/ADF embedding scripts, KickSlopStart fetch, merged image
tests/pc/     PC build + sentinels (compiles src/core + third_party/musashi)
tests/hal/    host tests of the pure HAL logic
docs/         HARDWARE, PORTING, ARCHITECTURE, design (in French)
assets/       kick_rom.h / wb_adf.h (git-ignored, to generate)
build/kss/    KickSlopStart ROM + kick_rom.h (git-ignored, filled by the -kss env builds)
.github/      binary release on every commit to main
```

## License

[MIT](LICENSE). Emulator © Massimiliano; [Musashi](https://github.com/kstenerud/Musashi) CPU core
© Karl Stenerud; [sinfl](https://github.com/vurtun/lib) inflate © Micha Mettke. ROMs and disk
images remain the property of their respective owners and are **not** included.
