/* test_denise_c.cpp — Test différentiel du rendu de ligne (core/video.cpp) : 120 trames
 * aléatoires (lores/hires, 0-6 plans, scroll fin BPLCON1 par playfield, palette, pointeurs et
 * modulos aléatoires), empreinte FNV-64 des framebuffers complets (PPM) affichée. Deux
 * implémentations équivalentes donnent la même empreinte (make testdenise compare à celle de
 * l'implémentation d'origine, calcul bit par bit). */
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include "a500.h"

uint8_t chip_ram[CHIP_SIZE] __attribute__((aligned(4)));
uint16_t intena, intreq, dmacon, adkcon;
int vpos, hpos, cur_frame;
uint32_t bpl_pt[6];
static uint16_t s_regs[0x100];
uint16_t custom_get(uint32_t off) { return s_regs[off >> 1]; }
void custom_write(uint32_t off, uint16_t v) { s_regs[(off & 0x1FE) >> 1] = v; }
void intreq_set(int) {}
int copper_writing_unused;
void logmsg(const char *, ...) {}

static uint32_t s_rng = 0xDE15Eu;
static uint32_t rnd(uint32_t n) { s_rng = s_rng * 1103515245u + 12345u; return (s_rng >> 8) % n; }

int main(void)
{
    for (uint32_t i = 0; i < CHIP_SIZE; i++) chip_ram[i] = (uint8_t)rnd(256);
    uint64_t hsh = 1469598103934665603ull;
    for (int f = 0; f < 120; f++) {
        const int hires = (int)rnd(2), np = (int)rnd(7);
        s_regs[0x100 >> 1] = (uint16_t)(hires << 15 | np << 12);          /* BPLCON0 */
        s_regs[0x102 >> 1] = (uint16_t)rnd(256);                            /* BPLCON1 */
        s_regs[0x08E >> 1] = 0x2C81; s_regs[0x090 >> 1] = 0x2CC1;          /* DIWSTRT/STOP */
        s_regs[0x108 >> 1] = (uint16_t)(((int)rnd(81) - 40) & ~1);         /* BPL1MOD */
        s_regs[0x10A >> 1] = (uint16_t)(((int)rnd(81) - 40) & ~1);         /* BPL2MOD */
        for (int c = 0; c < 32; c++) s_regs[(0x180 >> 1) + c] = (uint16_t)rnd(4096);
        for (int p = 0; p < 6; p++) bpl_pt[p] = rnd(CHIP_SIZE) & ~1u;
        dmacon = 0x0300;                                                    /* DMAEN | BPLEN */
        for (vpos = 0; vpos < 312; vpos++) denise_render_line();
        const char *path = "test_denise.ppm";
        if (video_write_ppm(path) != 0) return 1;
        FILE *in = fopen(path, "rb");
        if (!in) return 1;
        int ch;
        while ((ch = fgetc(in)) != EOF) { hsh ^= (uint8_t)ch; hsh *= 1099511628211ull; }
        fclose(in);
    }
    printf("%016llx\n", (unsigned long long)hsh);
    return 0;
}
