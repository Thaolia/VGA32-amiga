/* psram_stress.cpp — voir psram_stress.h. */
#include "platform_esp32.h"
#if VGA32_PSRAM_STRESS
#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "psram_stress.h"
#include "zorro.h"
#include "a500.h"

#define STRESS_BYTES 0x10000u           /* 64 Ko : la PSRAM restante est comptée */

static volatile uint32_t s_passes, s_errors;
static uint8_t *s_buf;

/* xorshift32 : motif reproductible, régénéré pour la vérification */
static inline uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void stress_task(void *)
{
    volatile uint32_t *w32 = (volatile uint32_t *)s_buf;
    volatile uint16_t *w16 = (volatile uint16_t *)s_buf;
    volatile uint8_t  *w8  = (volatile uint8_t *)s_buf;
    const uint32_t nw = STRESS_BYTES / 4;
    uint32_t seed = 0x1234567u;
    for (;;) {
        /* 1. écriture : chaque mot de 32 bits écrit en 32, 2×16 ou 4×8 bits selon le motif */
        uint32_t s = seed;
        for (uint32_t i = 0; i < nw; i++) {
            uint32_t v = xs(&s);
            switch (v & 3) {
            case 0: case 1: w32[i] = v; break;
            case 2: w16[2 * i] = (uint16_t)v; w16[2 * i + 1] = (uint16_t)(v >> 16); break;
            default: w8[4 * i] = (uint8_t)v; w8[4 * i + 1] = (uint8_t)(v >> 8);
                     w8[4 * i + 2] = (uint8_t)(v >> 16); w8[4 * i + 3] = (uint8_t)(v >> 24); break;
            }
        }
        /* 2. lecture-modification-écriture immédiatement relue (écriture puis lecture sur la
         *    même ligne de cache : la séquence sensible du défaut rev1) */
        s = seed;
        for (uint32_t i = 0; i < nw; i++) {
            uint32_t v = xs(&s);
            uint32_t r = w32[i];
            if (r != v) s_errors = s_errors + 1;
            w32[i] = r ^ 0xA5A5A5A5u;
            if (w32[i] != (v ^ 0xA5A5A5A5u)) s_errors = s_errors + 1;
        }
        /* 3. vérification complète, lecture octet par octet */
        s = seed;
        for (uint32_t i = 0; i < nw; i++) {
            uint32_t v = xs(&s) ^ 0xA5A5A5A5u;
            uint32_t r = (uint32_t)w8[4 * i] | (uint32_t)w8[4 * i + 1] << 8 |
                         (uint32_t)w8[4 * i + 2] << 16 | (uint32_t)w8[4 * i + 3] << 24;
            if (r != v) s_errors = s_errors + 1;
        }
        s_passes = s_passes + 1;
        seed = xs(&seed) | 1;
        vTaskDelay(1);                   /* laisse tourner adfload et IDLE1 */
    }
}

void psram_stress_start(void)
{
    s_buf = (uint8_t *)heap_caps_malloc(STRESS_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_buf) { Serial.println("ERREUR: tampon de stress PSRAM non alloue"); return; }
    if (xTaskCreatePinnedToCore(stress_task, "pstress", 4096, nullptr, 1, nullptr, 1) != pdPASS)
        Serial.println("ERREUR: tache de stress PSRAM non creee");
}

uint32_t psram_stress_passes(void) { return s_passes; }
uint32_t psram_stress_errors(void) { return s_errors; }

uint32_t psram_stress_crc(void)
{
    uint32_t crc = esp_rom_crc32_le(0, chip_ram, CHIP_SIZE);
    for (int b = 0; b < zorro_board_count(); b++) {
        uint32_t base = zorro_board_base(b), size = zorro_board_size(b);
        if (!base) continue;
        for (uint32_t off = 0; off < size; off += 0x10000)
            crc = esp_rom_crc32_le(crc, zorro_page[(base + off) >> 16], 0x10000);
    }
    return crc;
}
#endif /* VGA32_PSRAM_STRESS */
