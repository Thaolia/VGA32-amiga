/* opc_hist.c — Histogramme des opcodes 68000 exécutés (mesure ponctuelle, VGA32_OPC_HIST).
 *
 * Sert à choisir les handlers Musashi à placer en IRAM. Le compteur (256 Ko) est en PSRAM : il est
 * incrémenté ici, hors de la bibliothèque Musashi, car celle-ci est compilée sans le contournement
 * PSRAM rev1 et ne doit jamais écrire en PSRAM (voir musashi_tables.c).
 */
#include "platform_esp32.h"
#if VGA32_OPC_HIST
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"

extern void (**m68ki_instruction_jump_table)(void);
static uint32_t *s_hist;

void vga32_opc_hit(unsigned op)
{
    if (s_hist) s_hist[op & 0xFFFF]++;
}

void opc_hist_reset(void)
{
    if (!s_hist) s_hist = (uint32_t *)heap_caps_malloc(0x10000 * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!s_hist) { printf("ERREUR: histogramme d'opcodes non alloue\n"); return; }
    memset(s_hist, 0, 0x10000 * sizeof(uint32_t));
}

/* Agrège par handler (plusieurs opcodes partagent un handler) et affiche les n plus fréquents :
 * adresse du handler, exécutions, % et % cumulé. Les noms se retrouvent dans l'ELF (nm). */
void opc_hist_dump(int n)
{
    enum { SLOTS = 4096 };
    typedef struct { void (*h)(void); uint32_t cnt; } agg_t;
    if (!s_hist) return;
    agg_t *a = (agg_t *)heap_caps_calloc(SLOTS, sizeof(agg_t), MALLOC_CAP_SPIRAM);
    if (!a) { printf("ERREUR: agregation non allouee\n"); return; }
    uint64_t total = 0;
    for (unsigned op = 0; op < 0x10000; op++) {
        uint32_t c = s_hist[op];
        if (!c) continue;
        total += c;
        void (*h)(void) = m68ki_instruction_jump_table[op];
        unsigned k = ((uintptr_t)h >> 2) & (SLOTS - 1);
        while (a[k].h && a[k].h != h) k = (k + 1) & (SLOTS - 1);
        a[k].h = h;
        a[k].cnt += c;
    }
    printf("[OPC] %llu instructions\n", (unsigned long long)total);
    uint64_t cum = 0;
    for (int r = 0; r < n; r++) {
        int best = -1;
        for (int k = 0; k < SLOTS; k++)
            if (a[k].cnt && (best < 0 || a[k].cnt > a[best].cnt)) best = k;
        if (best < 0) break;
        cum += a[best].cnt;
        printf("[OPC] %3d %08x %9u %5.2f%% %6.2f%%\n", r + 1, (unsigned)(uintptr_t)a[best].h,
               (unsigned)a[best].cnt, 100.0 * a[best].cnt / total, 100.0 * cum / total);
        a[best].cnt = 0;
    }
    heap_caps_free(a);
}
#endif /* VGA32_OPC_HIST */
