/* test_blit_c.cpp — Test différentiel du blitter (core/blitter.cpp) : 3000 blits aléatoires,
 * mode zone (minterms, décalages A/B, masques premier/dernier mot, modulos, ascendant et
 * descendant, remplissage inclusif/exclusif) et mode ligne (octants, SING, texture). Écrit
 * dans argv[1] la chip RAM finale suivie des registres réécrits : deux implémentations
 * équivalentes produisent des fichiers identiques (make testblitdiff compare à une empreinte
 * figée de l'implémentation d'origine). */
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include "a500.h"

uint8_t chip_ram[CHIP_SIZE] __attribute__((aligned(4)));
static uint16_t s_regs[0x100];
uint16_t custom_get(uint32_t off) { return s_regs[off >> 1]; }
void custom_set_latch(uint32_t off, uint16_t v) { s_regs[off >> 1] = v; }
void logmsg(const char *, ...) {}

static uint32_t s_rng = 0xC0FFEEu;
static uint32_t rnd(uint32_t n) { s_rng = s_rng * 1103515245u + 12345u; return (s_rng >> 8) % n; }
static void setpt(uint32_t off, uint32_t a) { s_regs[off >> 1] = (uint16_t)(a >> 16); s_regs[(off + 2) >> 1] = (uint16_t)a; }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    for (uint32_t i = 0; i < CHIP_SIZE; i++) chip_ram[i] = (uint8_t)rnd(256);
    for (int n = 0; n < 3000; n++) {
        const int line = rnd(8) == 0;
        uint16_t con0 = (uint16_t)(rnd(16) << 12 | rnd(16) << 8 | rnd(256));
        uint16_t con1 = (uint16_t)(rnd(16) << 12);
        if (line) {
            con1 |= 0x0001 | (uint16_t)(rnd(2) << 6) | (uint16_t)(rnd(2) << 1) | (uint16_t)(rnd(8) << 2);
            con0 |= 0x0B00;                                  /* A, C, D comme le Kickstart */
        } else {
            con1 |= (uint16_t)(rnd(2) << 1);                 /* DESC */
            switch (rnd(6)) {                                /* remplissage : IFE / EFE, FCI */
            case 0: con1 |= 0x0008 | (uint16_t)(rnd(2) << 2); break;
            case 1: con1 |= 0x0010 | (uint16_t)(rnd(2) << 2); break;
            default: break;
            }
        }
        s_regs[0x040 >> 1] = con0; s_regs[0x042 >> 1] = con1;
        s_regs[0x044 >> 1] = rnd(4) ? 0xFFFF : (uint16_t)rnd(65536);   /* FWM */
        s_regs[0x046 >> 1] = rnd(4) ? 0xFFFF : (uint16_t)rnd(65536);   /* LWM */
        setpt(0x050, rnd(CHIP_SIZE) & ~1u); setpt(0x04C, rnd(CHIP_SIZE) & ~1u);
        setpt(0x048, rnd(CHIP_SIZE) & ~1u); setpt(0x054, rnd(CHIP_SIZE) & ~1u);
        for (uint32_t m = 0x060; m <= 0x066; m += 2) s_regs[m >> 1] = (uint16_t)(((int)rnd(401) - 200) & ~1);
        s_regs[0x070 >> 1] = (uint16_t)rnd(65536); s_regs[0x072 >> 1] = (uint16_t)rnd(65536);
        s_regs[0x074 >> 1] = (uint16_t)rnd(65536);
        if (line) s_regs[0x052 >> 1] = (uint16_t)((int)rnd(2001) - 1000);   /* accumulateur */
        /* zone : h 1-40 lignes, w 0-63 mots (0 = 64) ; ligne : longueur 1-200, w = 2 */
        uint16_t bltsize = line ? (uint16_t)((1 + rnd(200)) << 6 | 2)
                                : (uint16_t)(((1 + rnd(40)) << 6) | rnd(64));
        blitter_do(bltsize);
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 1;
    fwrite(chip_ram, 1, CHIP_SIZE, f);
    fwrite(s_regs, 1, sizeof s_regs, f);
    fclose(f);
    printf("blit diff : %d blits, empreinte ecrite dans %s\n", 3000, argv[1]);
    return 0;
}
