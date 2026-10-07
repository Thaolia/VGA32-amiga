/* test_paula_c.cpp — Test différentiel de Paula (core/paula.cpp) : 4 canaux, périodes et
 * longueurs aléatoires (dont période 0 : canal activé avant l'écriture de PER), volumes,
 * DMA basculé en cours de route, avance par tranches de color clocks variables. Écrit le WAV
 * produit dans argv[1] : deux implémentations équivalentes donnent des fichiers identiques
 * (make testpaula compare l'implémentation courante à une référence figée). */
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include "a500.h"

uint8_t chip_ram[CHIP_SIZE];
void logmsg(const char *, ...) {}

static uint32_t s_rng = 0x12345678u;
static uint32_t rnd(uint32_t n) { s_rng = s_rng * 1103515245u + 12345u; return (s_rng >> 8) % n; }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    for (uint32_t i = 0; i < CHIP_SIZE; i++) chip_ram[i] = (uint8_t)rnd(256);
    paula_reset();
    /* canal 2 activé avant toute écriture de PER : période 0 */
    paula_set_dma(0x0200 | 0x4);
    paula_write(0xC8, 40);                               /* AUD2VOL */
    for (int seg = 0; seg < 120; seg++) {
        for (int k = 0; k < 6; k++) {
            int c = (int)rnd(4);
            uint32_t base = 0xA0 + 16u * (uint32_t)c;
            switch (rnd(5)) {
            case 0: { uint32_t lc = rnd(CHIP_SIZE) & ~1u;
                      paula_write(base + 0x0, (uint16_t)(lc >> 16)); paula_write(base + 0x2, (uint16_t)lc); break; }
            case 1: paula_write(base + 0x4, (uint16_t)(rnd(4) ? 1 + rnd(400) : rnd(3))); break;
            case 2: paula_write(base + 0x6, (uint16_t)(rnd(5) ? 100 + rnd(600) : rnd(4))); break;
            case 3: paula_write(base + 0x8, (uint16_t)rnd(70)); break;
            case 4: paula_write(base + 0xA, (uint16_t)rnd(65536)); break;
            }
        }
        if (rnd(3) == 0) paula_set_dma((uint16_t)((rnd(4) ? 0x0200 : 0) | rnd(16)));
        /* ~1 trame en tranches de 1 à 500 color clocks (227/228 par ligne dans l'émulateur) */
        for (int done = 0; done < 70937; ) {
            int n = 1 + (int)rnd(rnd(4) ? 228 : 500);
            paula_step(n);
            done += n;
        }
    }
    return paula_write_wav(argv[1]) == 0 ? 0 : 1;
}
