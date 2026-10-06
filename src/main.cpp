/*
 * ============================================================================
 *  Amiga 500 Emulator — portage TTGO VGA32 v1.4 (ESP32 classique + FabGL)
 *
 *  Portage de Makerstown/amiga500-esp32 (ESP32-S3 + TFT ST7796) vers la carte
 *  TTGO VGA32 : sortie VGA (FabGL, 64 couleurs) au lieu du TFT SPI, entrées PS/2
 *  au lieu des boutons du cabinet.
 *
 *  Émulateur © Massimiliano (MIT). Musashi © Karl Stenerud (MIT). sinfl © Micha
 *  Mettke. Ce firmware ne contient AUCUN matériel Amiga sous copyright : générez
 *  assets/kick_rom.h et assets/wb_adf.h depuis VOS fichiers légaux (tools/).
 *
 *  Ce main.cpp remplace a500_esp32.ino : alloc PSRAM, chargement ROM/ADF, init
 *  FabGL, et la boucle trame déplacée dans emu_task épinglée sur le cœur « calme »
 *  (la génération VGA de FabGL monopolisant l'autre cœur en continu).
 * ============================================================================
 */
#include <Arduino.h>
#include "fabgl.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "platform_esp32.h"
#include "video_vga.h"
#include "audio_dac.h"
#include "input_ps2.h"
#include "kbd_amiga.h"
#include "sdcard.h"
#include "serial_kbd.h"

#include "a500.h"            /* coeur emulateur (C++) */
#include "sinfl.h"           /* zsinflate (declaration) */
extern "C" {
  #include "m68k.h"          /* Musashi (C) */
}

/* headers generes depuis VOS fichiers (git-ignores, dans assets/) */
#include "kick_rom.h"        /* kickstart_rom[] (== ROM_SIZE) */
#include "wb_adf.h"          /* WB_ADF_SIZE, wb_adf_comp[], WB_ADF_COMP_SIZE */

/* ---- globals possedes par la couche plateforme (comme l'ancien .ino) ---- */
extern uint8_t *slow_ram;    /* defini dans core/memory.cpp (non declare dans a500.h) */
int cur_frame = 0;           /* reference par le coeur (blitlog) ; a500.h: extern */

/* prototypes non declares dans a500.h (l'ancien .ino les redeclarait aussi) */
void cia_tod_hsync(void);
void cia_tod_vsync(void);

/* ---- chargement Kickstart depuis l'en-tete embarque ---- */
static bool load_kickstart(void)
{
    if (sizeof(kickstart_rom) != ROM_SIZE) {
        Serial.printf("ERREUR: header ROM %d octets != %d\n",
                      (int)sizeof(kickstart_rom), (int)ROM_SIZE);
        return false;
    }
    memcpy(kick_rom, kickstart_rom, ROM_SIZE);
    Serial.printf("Kickstart charge depuis header: %d octets\n", (int)ROM_SIZE);
    return true;
}

/* ---- decompression de l'ADF Workbench (zlib) en PSRAM ----
 * zsinflate consomme beaucoup de pile (tables Huffman) : on le lance dans une
 * tache dediee a grande pile, pinnee sur le coeur calme. */
static volatile int s_wb_result = -999;
static void decomp_task(void *arg)
{
    uint8_t *dst = (uint8_t *)arg;
    s_wb_result = zsinflate(dst, WB_ADF_SIZE, wb_adf_comp, WB_ADF_COMP_SIZE);
    vTaskDelete(NULL);
}
static bool load_workbench(void)
{
    /* 1. priorite a la carte SD : disquette echangeable sans reflasher */
    if (sdcard_load_adf(VGA32_ADF_FILENAME)) return true;
    Serial.println("[WB] SD indisponible -> repli sur l'ADF embarque (wb_adf.h)");

    /* 2. repli : ADF embarque, decompresse en PSRAM */
    uint8_t *dst = drive_alloc_adf();
    if (!dst) { Serial.println("ERREUR: alloc ADF echouee"); return false; }
    s_wb_result = -999;
    xTaskCreatePinnedToCore(decomp_task, "decomp", DECOMP_TASK_STACK, dst, 5,
                            nullptr, fabgl::CoreUsage::quietCore());
    while (s_wb_result == -999) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_wb_result != (int)WB_ADF_SIZE) {
        Serial.printf("ERREUR: decompression ADF (out=%d attendu %u)\n",
                      s_wb_result, (unsigned)WB_ADF_SIZE);
        return false;
    }
    drive_mount_ready();
    Serial.printf("ADF monte: %u octets\n", (unsigned)WB_ADF_SIZE);
    return true;
}

/* ---- tache emulateur : boucle trame, sur le coeur calme ---- */
static void emu_task(void *arg)
{
    (void)arg;
    /* init du coeur sur CE coeur (Musashi alloue sa table d'opcodes en PSRAM) */
    mem_reset();
    cia_reset();
    custom_reset();
    paula_reset();
    audio_dac_init();        /* apres paula_reset : le ring Paula est alloue */
    kbd_amiga_init();
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    Serial.printf("[RESET] PC=%06X (attendu FC00D2)\n",
                  m68k_get_reg(NULL, M68K_REG_PC));

    int paula_cc_acc = 0;
    for (;;) {
        int64_t t0 = esp_timer_get_time();
        cur_frame++;

        input_ps2_poll();        /* souris + clavier PS/2 */
#if VGA32_SERIAL_KBD
        serial_kbd_poll();       /* clavier via port serie USB (sans PS/2) */
#endif
        input_frame(cur_frame);
        copper_vblank();
        sprite_vblank();

        for (int line = 0; line < LINES_PAL; line++) {
            vpos = line;
            if (blit_irq_pending) { blit_irq_pending = 0; intreq_set(6); }
            if (disk_irq_pending) { disk_irq_pending = 0; intreq_set(1); }

            copper_run_line();
            denise_render_line();          /* -> denise_line_cb (video_vga) */
            m68k_execute(CYC_LINE);

            /* Paula avance en colorclock (~3.546 MHz), pas en cycles CPU :
             * CYC_LINE/2 avec accumulateur pour ne pas perdre le demi-cycle. */
            paula_cc_acc += CYC_LINE;
            paula_step(paula_cc_acc >> 1);
            paula_cc_acc &= 1;

            cia_tick(CYC_LINE / 10);
            cia_tod_hsync();
        }
        cia_tod_vsync();
        intreq_set(5);                     /* VBlank */
        kbd_amiga_step();                  /* emission serie clavier (stub Phase 0) */

#if VGA32_DEBUG
        if ((cur_frame % 50) == 0) {
            int64_t dt = esp_timer_get_time() - t0;
            Serial.printf("=== frame %d: %lld us (%.1f fps) | heap int %u o | psram %u o ===\n",
                          cur_frame, (long long)dt, dt > 0 ? 1000000.0 / dt : 0.0,
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
#endif
        vTaskDelay(1);                     /* nourrit le watchdog du coeur calme */
    }
}

void setup(void)
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n==== Amiga 500 sur TTGO VGA32 (FabGL) ====");
    Serial.printf("CPU %d MHz, PSRAM libre %d Ko\n",
                  getCpuFrequencyMhz(), (int)(ESP.getFreePsram() / 1024));

    /* 1. RAM Amiga en PSRAM */
    chip_ram = (uint8_t *)heap_caps_malloc(CHIP_SIZE, MALLOC_CAP_SPIRAM);
    slow_ram = (uint8_t *)heap_caps_malloc(SLOW_SIZE, MALLOC_CAP_SPIRAM);
    kick_rom = (uint8_t *)heap_caps_malloc(ROM_SIZE,  MALLOC_CAP_SPIRAM);
    if (!chip_ram || !slow_ram || !kick_rom) {
        Serial.println("ERREUR: alloc PSRAM echouee (PSRAM activee ?)");
        return;
    }
    memset(chip_ram, 0, CHIP_SIZE);
    memset(slow_ram, 0, SLOW_SIZE);

    /* 2. Kickstart + ADF (depuis headers embarques) */
    if (!load_kickstart()) { Serial.println("STOP: pas de Kickstart."); return; }
    if (!load_workbench())  Serial.println("ATTENTION: pas de floppy, boot bloque a l'invite disque.");

    /* 3. sortie VGA (FabGL) : fixe busiestCore/quietCore et branche denise_line_cb */
    video_vga_init();

    /* 4. entree PS/2 (souris + clavier). L'audio demarre dans emu_task APRES
     *    paula_reset (le ring Paula doit exister avant la 1ere consommation). */
    input_ps2_init();

    /* 5. emulateur sur le coeur calme (FabGL occupe l'autre avec la VGA).
     * On logge les deux coeurs : si quietCore == busiestCore, l'emulateur se
     * retrouve en contention avec la VGA -> fps bas a tort attribue a la PSRAM. */
    Serial.printf("[CORES] VGA(busiest)=%d, emu(quiet)=%d\n",
                  fabgl::CoreUsage::busiestCore(), fabgl::CoreUsage::quietCore());
    xTaskCreatePinnedToCore(emu_task, "emu", EMU_TASK_STACK, nullptr, 5,
                            nullptr, fabgl::CoreUsage::quietCore());
    Serial.println("Emulateur demarre. Le bureau devrait apparaitre sur le moniteur VGA.");
}

void loop(void)
{
    vTaskDelay(portMAX_DELAY);   /* la loopTask Arduino n'a rien a faire */
}
