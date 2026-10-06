/* platform_esp32.h — Constantes matérielles du portage TTGO VGA32 v1.4.
 *
 * Regroupe tout ce qui, dans l'ancien a500_esp32.ino, était des #define épars
 * (broches ST7796/I2S/boutons). Ici : sortie VGA FabGL, entrée PS/2, audio DAC,
 * et les paramètres de mappage écran à régler en Phase 0.
 */
#ifndef PLATFORM_ESP32_H
#define PLATFORM_ESP32_H

/* ---- Sortie VGA (FabGL) ----
 * Les broches sont câblées en dur sur la carte VGA32 et correspondent EXACTEMENT
 * aux valeurs par défaut de fabgl::VGAController::begin() sans argument :
 *   Rouge R1/R0 = GPIO22/21, Vert G1/G0 = GPIO19/18, Bleu B1/B0 = GPIO5/4,
 *   HSync = GPIO23, VSync = GPIO15.
 * On appelle donc begin() sans paramètre (cf. video_vga.cpp). 64 couleurs (RGB222).
 */

/* Modeline de départ. QVGA_320x240_60Hz : 320x240 en DoubleScan, bien supportée.
 * L'Amiga PAL « utile » fait ~256 lignes lores : on en affiche 240 (crop/letterbox).
 * Bascule possible en Phase 0 : modeline custom 320x256, ou VGA16 640x256 si le
 * texte Workbench (hires) est trop flou en 320 sous-échantillonné. */
#define VGA32_MODELINE      QVGA_320x240_60Hz
#define VGA32_ACTIVE_W      320     /* largeur framebuffer FabGL (octets/pixel) */
#define VGA32_ACTIVE_H      240     /* hauteur framebuffer FabGL */

/* Première ligne Amiga (vpos) affichée en haut de l'écran VGA.
 * L'ancien pilote TFT utilisait dy = v - 44 + 56. Sur VGA 240 lignes on recadre
 * la fenêtre visible PAL (~44..300) : à AFFINER visuellement en Phase 0. */
#define VGA32_VSTART        44

/* ---- Entrée PS/2 (FabGL, géré par l'ULP) ----
 * Clavier port 0 : CLK=GPIO33, DATA=GPIO32.  Souris port 1 : CLK=GPIO26, DATA=GPIO27.
 * Gérées par fabgl::PS2Controller (pins par défaut VGA32). Pas de #define ici :
 * on passe par PS2Controller::begin(PS2Preset::KeyboardPort0_MousePort1).
 */

/* ---- Carte SD embarquee (SPI, schema v1.4) ----
 * Slot microSD cable sur ces 4 broches (pas les defauts FabGL). Aucun conflit
 * avec VGA/PS2/audio. IO2 partagee avec la LED, IO12 = strapping (geres carte). */
#define VGA32_SD_CS        13
#define VGA32_SD_SCK       14
#define VGA32_SD_MOSI      12
#define VGA32_SD_MISO       2
#define VGA32_ADF_SIZE     901120u          /* ADF DD standard (= ADF_SIZE de drive.cpp) */
#define VGA32_ADF_FILENAME "wb.adf"         /* ADF insere au boot s'il est sur la SD */

/* ---- Bouton de changement de disquette ----
 * K1 du schema v1.4 : S_VP = GPIO36 (input-only), pull-up 10K externe, appui = 0. */
#define VGA32_DISK_BTN_GPIO   36
#define VGA32_OSD_MS          4000u          /* duree d'affichage du nom en surimpression */
#define VGA32_OSD_Y           224            /* 1re ligne du bandeau (framebuffer 240 lignes) */
#define LOADER_TASK_STACK     6144           /* FatFs LFN sur pile (CONFIG_FATFS_LFN_STACK) */

/* ---- Audio (Phase 3) ----
 * La VGA32 n'a pas de DAC I2S externe : FabGL sort le son sur le DAC interne GPIO25
 * (+ filtre passe-bas/ampli externes à câbler). Mono 8 bits. */
#define VGA32_AUDIO_DAC_GPIO   25

/* ---- Clavier via le port série USB (CP2104) ----
 * Permet de taper dans l'Amiga depuis le terminal série du PC quand on n'a pas
 * de clavier PS/2 (ou pas d'adaptateur USB->PS/2 actif). Fonctionne EN PARALLÈLE
 * du clavier PS/2 (les deux alimentent kbd_amiga). 0 pour désactiver. */
#define VGA32_SERIAL_KBD   1

/* ---- Timing trame PAL (repris des #define de l'ancien a500_esp32.ino) ----
 * 1 trame PAL = 312 lignes de 455 cycles CPU. Non présents dans a500.h. */
#define LINES_PAL   312
#define CYC_LINE    455

/* ---- Ordonnancement ----
 * FabGL épingle la génération VGA sur CoreUsage::busiestCore() ; l'émulateur tourne
 * sur CoreUsage::quietCore() (choisi dans main.cpp). Pile généreuse : Musashi +
 * chipset récursif, et la décompression ADF initiale a besoin de ~40 Ko ponctuels. */
#define EMU_TASK_STACK      24576
#define DECOMP_TASK_STACK   40960

/* ---- Instrumentation Phase 0 ----
 * _DEBUG=1 active les prints série de profiling (fps, heap). À 0, le chemin critique
 * ne fait aucun print (règle projet : print() bloque l'UART TX sans terminal).
 * Les prints d'ERREUR (alloc PSRAM, etc.) restent toujours actifs. */
#ifndef VGA32_DEBUG
#define VGA32_DEBUG   1
#endif

#endif /* PLATFORM_ESP32_H */
