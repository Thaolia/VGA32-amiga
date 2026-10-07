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
#include "soc/spi_reg.h"
#include "esp32/spiram.h"

#include "platform_esp32.h"
#include "video_vga.h"
#include "audio_dac.h"
#include "input_ps2.h"
#include "kbd_amiga.h"
#include "sdcard.h"
#include "disk_switch.h"
#include "serial_kbd.h"
#include "zorro.h"
#include "psram_stress.h"

#include "a500.h"            /* coeur emulateur (C++) */
/* inconditionnel : le LDF PlatformIO (deep+) ne voit pas VGA32_EMBED_ADF et retirerait
   sinfl du build par defaut ; sans ADF embarque, zsinflate est elimine au link */
#include "sinfl.h"           /* zsinflate (declaration) */
extern "C" {
  #include "m68k.h"          /* Musashi (C) */
}

/* headers generes depuis VOS fichiers (git-ignores, dans assets/) */
#include "kick_rom.h"        /* kickstart_rom[] (== ROM_SIZE) */
#if VGA32_EMBED_ADF
#include "wb_adf.h"          /* WB_ADF_SIZE, wb_adf_comp[], WB_ADF_COMP_SIZE */
#endif

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

#if VGA32_EMBED_ADF
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
#endif
/* *sd_idx = index SD de l'ADF insere (-1 : ADF embarque ou aucun) */
static bool load_workbench(int *sd_idx)
{
    *sd_idx = -1;
    uint8_t *dst = drive_alloc_adf();
    if (!dst) { Serial.println("ERREUR: alloc ADF echouee"); return false; }

    /* 1. priorite a la carte SD : disquette echangeable sans reflasher */
    sdcard_init();
    int i = sdcard_adf_find(VGA32_ADF_FILENAME);
    if (i >= 0 && sdcard_read_adf(i, dst)) {
        drive_mount_ready();
        *sd_idx = i;
        return true;
    }
#if VGA32_EMBED_ADF
    Serial.printf("[WB] %s absent/illisible sur la SD -> repli sur l'ADF embarque (wb_adf.h)\n",
                  VGA32_ADF_FILENAME);

    /* 2. repli : ADF embarque, decompresse en PSRAM */
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
#else
    /* build Kickstart seul : le buffer ADF reste alloue, disk_switch le reutilise (IO36) */
    Serial.printf("[WB] %s absent/illisible sur la SD, pas d'ADF embarque -> invite disque "
                  "(IO36 : choisir un ADF de la SD)\n", VGA32_ADF_FILENAME);
    return false;
#endif
}

#if VGA32_PROF
/* Profiling de la boucle trame : cycles CPU cumules par etape, publies toutes les
 * PROF_WINDOW trames. PROF_MARK impute le temps ecoule depuis la marque precedente. */
enum { P_FRAME, P_COPPER, P_DENISE, P_CPU, P_PAULA, P_CIA, P_POST, P_YIELD, P_N };
static const char *const PROF_NAME[P_N] = {
    "trame", "copper", "denise", "cpu", "paula", "cia", "post", "yield" };
#define PROF_WINDOW 50
static uint64_t s_prof[P_N];
static uint32_t s_prof_t;
#define PROF_MARK(slot) do { uint32_t _n = prof_ccount(); s_prof[slot] += _n - s_prof_t; \
                             s_prof_t = _n; } while (0)

static void prof_report(void)
{
    static long long blit_ns_prev;
    static uint32_t  blit_count_prev;
    const double mhz = getCpuFrequencyMhz();
    uint64_t total = 0;
    for (int i = 0; i < P_N; i++) total += s_prof[i];
    uint32_t cb_cyc, cb_lines;
    video_vga_prof_take(&cb_cyc, &cb_lines);
    long long blit_ns = blitter_ns();
    double blit_ms = (blit_ns - blit_ns_prev) / 1e6 / PROF_WINDOW;
    uint32_t blits = blit_count - blit_count_prev;
    blit_ns_prev = blit_ns;
    blit_count_prev = blit_count;

    double frame_ms = total / mhz / 1000.0 / PROF_WINDOW;
    Serial.printf("[PROF] %d tr: %.2f ms/tr (%.1f fps) |", PROF_WINDOW, frame_ms,
                  frame_ms > 0 ? 1000.0 / frame_ms : 0.0);
    for (int i = 0; i < P_N; i++)
        Serial.printf(" %s %.2f (%.0f%%)", PROF_NAME[i], s_prof[i] / mhz / 1000.0 / PROF_WINDOW,
                      total ? 100.0 * s_prof[i] / total : 0.0);
    /* blit : execute dans m68k_execute (ecriture BLTSIZE) -> inclus dans cpu.
     * vga_cb : callback Denise -> inclus dans denise. */
    Serial.printf(" | dont blit %.2f (%u/tr) | dont vga_cb %.2f, %u lignes/tr\n",
                  blit_ms, (unsigned)(blits / PROF_WINDOW),
                  cb_cyc / mhz / 1000.0 / PROF_WINDOW, (unsigned)(cb_lines / PROF_WINDOW));
    for (int i = 0; i < P_N; i++) s_prof[i] = 0;

    /* detail de denise (instrumente dans core/video.cpp) : ms/trame par sous-etape,
     * puis lignes/trame par sortie de denise_render_line */
    static const char *const DP_NAME[DP_N] = {
        "sprdma", "bgfill", "fenetre", "fetch", "skipchk", "snap", "pal", "pixels",
        "sprites", "cb" };
    static const char *const DL_NAME[DL_N] = { "bordure", "0plan", "sautees", "dessinees" };
    Serial.print("[DENISE]");
    for (int i = 0; i < DP_N; i++) {
        Serial.printf(" %s %.2f", DP_NAME[i], denise_prof_cyc[i] / mhz / 1000.0 / PROF_WINDOW);
        denise_prof_cyc[i] = 0;
    }
    Serial.print(" | lignes/tr:");
    for (int i = 0; i < DL_N; i++) {
        Serial.printf(" %s %u", DL_NAME[i], (unsigned)(denise_prof_lines[i] / PROF_WINDOW));
        denise_prof_lines[i] = 0;
    }
    Serial.println();
}
#else
#define PROF_MARK(slot) do {} while (0)
#endif

/* Fin de trame : cede le coeur (le watchdog surveille la tache idle du coeur 0) et, avec
 * VGA32_FPS_LIMIT, attend l'echeance de la trame. Echeance absolue : la moyenne fait
 * exactement 50 Hz, un depassement d'un tick est rattrape a la trame suivante. En retard
 * de plus d'une trame, on repart de maintenant au lieu d'accelerer pour rattraper. */
static void frame_end_wait(int64_t *deadline)
{
#if VGA32_FPS_LIMIT
    *deadline += VGA32_FRAME_US;
    int64_t now = esp_timer_get_time();
    int64_t wait = *deadline - now;
    if (wait <= 0) {
        if (wait < -VGA32_FRAME_US) *deadline = now;
        vTaskDelay(1);
        return;
    }
    /* vTaskDelay(n) bloque entre n-1 et n ms : floor() ne depasse pas l'echeance ;
     * le reste (< 1 ms) en attente active */
    TickType_t ticks = (TickType_t)(wait / 1000);
    vTaskDelay(ticks ? ticks : 1);
    while (esp_timer_get_time() < *deadline) { }
#else
    (void)deadline;
    vTaskDelay(1);
#endif
}

#if VGA32_DEBUG
/* Diagnostic PSRAM au boot : horloges SPI0 (cache flash + PSRAM) / SPI1 et debit mesure.
 * La frequence PSRAM est fixee par le SDK precompile (CONFIG_SPIRAM_SPEED_*) a l'init,
 * avant setup() : on la constate ici, on ne la change pas (le code s'execute via ce cache). */
static uint32_t spi_clk_mhz10(uint32_t reg)
{
    if (reg & SPI_CLK_EQU_SYSCLK) return 800;                 /* = APB 80 MHz */
    uint32_t pre = (reg >> SPI_CLKDIV_PRE_S) & SPI_CLKDIV_PRE;
    uint32_t n   = (reg >> SPI_CLKCNT_N_S) & SPI_CLKCNT_N;
    return 800 / ((pre + 1) * (n + 1));                       /* en dixiemes de MHz */
}
static void psram_diag(const uint8_t *buf, size_t len)
{
    uint32_t c0 = REG_READ(SPI_CLOCK_REG(0)), c1 = REG_READ(SPI_CLOCK_REG(1));
    uint32_t d0 = REG_READ(SPI_DATE_REG(0));
    Serial.printf("[PSRAM] SPI0 clock=%08X (%u.%u MHz) SPI1 clock=%08X (%u.%u MHz) SPI0 date[31:30]=%u%u\n",
                  (unsigned)c0, (unsigned)(spi_clk_mhz10(c0) / 10), (unsigned)(spi_clk_mhz10(c0) % 10),
                  (unsigned)c1, (unsigned)(spi_clk_mhz10(c1) / 10), (unsigned)(spi_clk_mhz10(c1) % 10),
                  (unsigned)((d0 >> 31) & 1), (unsigned)((d0 >> 30) & 1));
    /* lecture sequentielle 32 bits : len >> 32 Ko de cache -> chaque ligne de 32 o vient de la PSRAM */
    const volatile uint32_t *p = (const volatile uint32_t *)buf;
    uint32_t acc = 0;
    int64_t t = esp_timer_get_time();
    for (size_t i = 0; i < len / 4; i++) acc += p[i];
    int64_t dt = esp_timer_get_time() - t;
    /* une lecture par ligne de cache : cout d'un remplissage de ligne (latence + transfert) */
    t = esp_timer_get_time();
    for (size_t i = 0; i < len / 4; i += 8) acc += p[i];
    int64_t dl = esp_timer_get_time() - t;
    Serial.printf("[PSRAM] lecture seq %u Ko : %lld us (%.1f Mo/s) | 1 lecture/ligne : %.2f us/ligne (chk %08X)\n",
                  (unsigned)(len / 1024), (long long)dt, dt > 0 ? (double)len / dt : 0.0,
                  (double)dl / (len / 32), (unsigned)acc);
    /* meme mesure sur la flash (Kickstart embarque, 256 Ko de rodata) : ~2x plus rapide en QIO
     * qu'en DIO, ce qui verifie le mode reellement actif (l'en-tete d'image reste DIO) */
    const volatile uint32_t *f = (const volatile uint32_t *)kickstart_rom;
    t = esp_timer_get_time();
    for (size_t i = 0; i < sizeof(kickstart_rom) / 4; i++) acc += f[i];
    dt = esp_timer_get_time() - t;
    Serial.printf("[FLASH] lecture seq %u Ko : %lld us (%.1f Mo/s) (chk %08X)\n",
                  (unsigned)(sizeof(kickstart_rom) / 1024), (long long)dt,
                  dt > 0 ? (double)sizeof(kickstart_rom) / dt : 0.0, (unsigned)acc);
}
#endif

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
    int64_t frame_deadline = esp_timer_get_time();
#if VGA32_PROF
    s_prof_t = prof_ccount();
#endif
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
        PROF_MARK(P_FRAME);

        for (int line = 0; line < LINES_PAL; line++) {
            vpos = line;
            if (blit_irq_pending) { blit_irq_pending = 0; intreq_set(6); }
            if (disk_irq_pending) { disk_irq_pending = 0; intreq_set(1); }

            copper_run_line();
            PROF_MARK(P_COPPER);
            denise_render_line();          /* -> denise_line_cb (video_vga) */
            PROF_MARK(P_DENISE);
            m68k_execute(CYC_LINE);
            PROF_MARK(P_CPU);

            /* Paula avance en colorclock (~3.546 MHz), pas en cycles CPU :
             * CYC_LINE/2 avec accumulateur pour ne pas perdre le demi-cycle. */
            paula_cc_acc += CYC_LINE;
            paula_step(paula_cc_acc >> 1);
            paula_cc_acc &= 1;
            PROF_MARK(P_PAULA);

            cia_tick(CYC_LINE / 10);
            cia_tod_hsync();
            PROF_MARK(P_CIA);
        }
        cia_tod_vsync();
        intreq_set(5);                     /* VBlank */
        kbd_amiga_step();                  /* emission serie clavier (stub Phase 0) */
        disk_switch_poll();                /* bouton IO36 : changement de disquette DF0 */
        video_vga_osd_tick();              /* fin de la surimpression du nom de disquette */
        PROF_MARK(P_POST);

#if VGA32_DEBUG
        if (cur_frame == 200) {
            /* bilan memoire une fois tout alloue (tables Musashi, cache de lignes video) */
            /* taille physique de la puce (ID lu a l'init) ; l'ESP32 n'en adresse que 4 Mo.
             * Pas d'API himem : son constructeur global s'execute avant l'init PSRAM
             * paresseuse d'arduino-esp32 2.x et fait abort() (mesure). */
            static const unsigned chip_mbit[] = { 16, 32, 64 };
            esp_spiram_size_t cs = esp_spiram_get_chip_size();
            Serial.printf("[MEM] PSRAM libre %u o, plus grand bloc %u o | puce %u Mbit (%u Mo)\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                          cs <= ESP_SPIRAM_SIZE_64MBITS ? chip_mbit[cs] : 0,
                          cs <= ESP_SPIRAM_SIZE_64MBITS ? chip_mbit[cs] / 8 : 0);
        }
#if VGA32_PSRAM_STRESS
        if ((cur_frame % 500) == 0)
            Serial.printf("[CRC] trame %d : RAM Amiga %08X | stress PSRAM %u passes, %u erreurs\n",
                          cur_frame, (unsigned)psram_stress_crc(),
                          (unsigned)psram_stress_passes(), (unsigned)psram_stress_errors());
#endif
        if (cur_frame == 200 || cur_frame == 1000) {
            /* cartes Zorro : base attribuee par le Kickstart, et part de la Fast RAM deja
             * touchee par le systeme (pages de 4 Ko non nulles) */
            for (int b = 0; b < zorro_board_count(); b++) {
                uint32_t base = zorro_board_base(b), size = zorro_board_size(b), used = 0;
                for (uint32_t pg = 0; base && pg < size; pg += 4096) {
                    const uint32_t *w = (const uint32_t *)(zorro_page[(base + pg) >> 16] + ((base + pg) & 0xFFFF));
                    for (int k = 0; k < 1024; k++) if (w[k]) { used++; break; }
                }
                Serial.printf("[ZORRO] trame %d : carte %d, %u Ko, %s%06X, %u/%u pages de 4 Ko utilisees\n",
                              cur_frame, b, (unsigned)(size / 1024), base ? "base $" : "NON configuree ",
                              (unsigned)base, (unsigned)used, (unsigned)(size / 4096));
            }
        }
        if ((cur_frame % 50) == 0) {
            int64_t dt = esp_timer_get_time() - t0;
            Serial.printf("=== frame %d: %lld us (%.1f fps) | heap int %u o | psram %u o ===\n",
                          cur_frame, (long long)dt, dt > 0 ? 1000000.0 / dt : 0.0,
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
#endif
#if VGA32_PROF
        PROF_MARK(P_POST);                 /* le print "=== frame" ci-dessus reste dans post */
        if ((cur_frame % PROF_WINDOW) == 0) {
            prof_report();
            s_prof_t = prof_ccount();      /* le print [PROF] n'est impute a aucune etape */
        }
#endif
        frame_end_wait(&frame_deadline);   /* watchdog + plafond 50 trames/s */
        PROF_MARK(P_YIELD);
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
#if VGA32_DEBUG
    psram_diag(chip_ram, CHIP_SIZE);
#endif
    /* Fast RAM allouee tot : les blocs contigus n'existent plus une fois l'ADF, les tables
     * Musashi et le cache de lignes video alloues. Echec non bloquant (carte absente). La plus
     * grande d'abord : le Kickstart la place en $200000, la suivante a la suite. */
    {
        static const unsigned fast_kb[] = { VGA32_FASTRAM_KB, VGA32_FASTRAM2_KB };
        for (unsigned kb : fast_kb) {
            if (!kb) continue;
            const uint32_t sz = (uint32_t)kb * 1024u;
            uint8_t *fast = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
            if (!fast || zorro_add_ram(fast, sz) != 0) {
                Serial.printf("ERREUR: Fast RAM %u Ko non ajoutee (PSRAM ou taille invalide)\n", kb);
                if (fast) heap_caps_free(fast);
                continue;
            }
            memset(fast, 0, sz);
        }
    }

    /* 2. Kickstart + ADF (depuis headers embarques) */
    if (!load_kickstart()) { Serial.println("STOP: pas de Kickstart."); return; }
    int boot_idx;
    if (!load_workbench(&boot_idx))
        Serial.println("ATTENTION: pas de floppy, boot bloque a l'invite disque.");

    /* 3. sortie VGA (FabGL) : fixe busiestCore/quietCore et branche denise_line_cb */
    video_vga_init();

    /* bouton IO36 + tache de lecture SD : APRES video_vga_init, qui fixe busiestCore */
    disk_switch_init(boot_idx);
#if VGA32_PSRAM_STRESS
    psram_stress_start();
#endif

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
