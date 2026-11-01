/* test_denise_modes_c.cpp — Modes de Denise (core/video.cpp) ajoutés au portage, valeurs
 * attendues calculées à la main d'après le HRM : double playfield et PF2PRI, HAM, priorités
 * sprites/playfield (BPLCON2), sprites attachés, fenêtre horizontale DIW, défilement fin avec
 * mot lu avant la fenêtre (DDFSTRT avancé), pas de défilement hires (2 pixels hires). */
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "a500.h"

uint8_t chip_ram[CHIP_SIZE] __attribute__((aligned(4)));
uint16_t intena, intreq, dmacon, adkcon;
int vpos, hpos, cur_frame;
int lof = 1;
uint32_t bpl_pt[6];
static uint16_t s_regs[0x100];
uint16_t custom_get(uint32_t off) { return s_regs[off >> 1]; }
void custom_write(uint32_t off, uint16_t v) { s_regs[(off & 0x1FE) >> 1] = v; }
void intreq_set(int) {}
void logmsg(const char *, ...) {}

static uint8_t ppm[640 * 312 * 3];
static int fails = 0;

static void frame(void)
{
    sprite_vblank();
    for (vpos = 0; vpos < 312; vpos++) denise_render_line();
    if (video_write_ppm("test_denise_modes.ppm") != 0) { printf("FAIL ppm\n"); fails++; return; }
    FILE *f = fopen("test_denise_modes.ppm", "rb");
    for (int nl = 0; nl < 3; ) { int ch = fgetc(f); if (ch == '\n') nl++; }
    if (fread(ppm, 1, sizeof ppm, f) != sizeof ppm) fails++;
    fclose(f);
}

/* couleur 12 bits du pixel (x en pixels du framebuffer de 640 : lores = 2x) */
static uint16_t at(int x, int y)
{
    const uint8_t *p = &ppm[3 * (y * 640 + x)];
    return (uint16_t)((p[0] / 17) << 8 | (p[1] / 17) << 4 | (p[2] / 17));
}

static void check(const char *name, int x, int y, uint16_t want)
{
    const uint16_t got = at(x, y);
    printf("%s %s : %03X (attendu %03X)\n", got == want ? "OK  " : "FAIL", name, got, want);
    if (got != want) fails++;
}

/* une ligne de plans répétée sur tout l'écran (modulo = -octets lus) */
static void setup(uint16_t bplcon0, int bytes, const uint8_t (*rows)[48], int np)
{
    memset(chip_ram, 0, CHIP_SIZE);
    memset(s_regs, 0, sizeof s_regs);
    s_regs[0x100 >> 1] = bplcon0;
    s_regs[0x08E >> 1] = 0x2C81; s_regs[0x090 >> 1] = 0x2CC1;
    s_regs[0x108 >> 1] = s_regs[0x10A >> 1] = (uint16_t)-bytes;
    s_regs[0x104 >> 1] = 0x0024;
    static const uint16_t col[32] = { 0x000, 0xF00, 0x880, 0x088, 0x333, 0x00F, 0x444, 0x555,
                                      0x666, 0x0F0, 0x777, 0x999, 0xAAA, 0xBBB, 0xCCC, 0xDDD,
                                      0x123, 0xF0F, 0x234, 0x345, 0x456, 0x567, 0x678, 0x789,
                                      0x89A, 0x0FF, 0x9AB, 0xABC, 0xBCD, 0xFF0, 0xCDE, 0xDEF };
    for (int c = 0; c < 32; c++) s_regs[(0x180 >> 1) + c] = col[c];
    for (int p = 0; p < np; p++) {
        bpl_pt[p] = 0x10000u + 0x1000u * (uint32_t)p;
        memcpy(&chip_ram[bpl_pt[p]], rows[p], (size_t)bytes);
    }
    dmacon = 0x0300;
}

/* sprite n : une ligne (vstart = y), mots data/datb, x en pixels lores de la fenêtre */
static void sprite(int n, int y, int x, uint16_t data, uint16_t datb, int att)
{
    const uint32_t a = 0x40000u + 0x100u * (uint32_t)n;
    const int hs = 0x81 + x;
    const uint16_t pos = (uint16_t)(y << 8 | (hs >> 1)), ctl = (uint16_t)((y + 1) << 8 | (att << 7) | (hs & 1));
    const uint16_t w[6] = { pos, ctl, data, datb, 0, 0 };
    for (int i = 0; i < 6; i++) { chip_ram[a + 2 * i] = (uint8_t)(w[i] >> 8); chip_ram[a + 2 * i + 1] = (uint8_t)w[i]; }
    s_regs[(0x120 >> 1) + 2 * n] = (uint16_t)(a >> 16);
    s_regs[(0x122 >> 1) + 2 * n] = (uint16_t)a;
    sprite_arm(n);
    dmacon |= 0x0020;
}

int main()
{
    static uint8_t rows[6][48];
    const int Y = 0x40;

    /* T1 double playfield 2+2 : px0 PF1+PF2, px1 PF1, px2 PF2, px3 aucun */
    memset(rows, 0, sizeof rows);
    rows[0][0] = 0xC0; rows[1][0] = 0xA0;
    setup(0x4400, 40, rows, 4);
    frame();
    check("T1 DPF px0 PF1 devant", 0, Y, 0xF00);
    check("T1 DPF px1 PF1", 2, Y, 0xF00);
    check("T1 DPF px2 PF2 = COLOR09", 4, Y, 0x0F0);
    check("T1 DPF px3 transparent", 6, Y, 0x000);
    setup(0x4400, 40, rows, 4);
    s_regs[0x104 >> 1] = 0x0064;                        /* PF2PRI */
    frame();
    check("T1 DPF px0 PF2PRI : PF2 devant", 0, Y, 0x0F0);

    /* T2 HAM 6 plans : COLOR02, bleu 5, rouge C, vert 1, puis COLOR00 */
    memset(rows, 0, sizeof rows);
    rows[0][0] = 0x50; rows[1][0] = 0x80; rows[2][0] = 0x60;
    rows[3][0] = 0x20; rows[4][0] = 0x50; rows[5][0] = 0x30;
    setup(0x6800, 40, rows, 6);
    frame();
    check("T2 HAM px0 palette", 0, Y, 0x880);
    check("T2 HAM px1 bleu", 2, Y, 0x885);
    check("T2 HAM px2 rouge", 4, Y, 0xC85);
    check("T2 HAM px3 vert", 6, Y, 0xC15);
    check("T2 HAM px4 COLOR00", 8, Y, 0x000);

    /* T3 BPLCON2 PF2P=2 : paires 0-1 devant, 2-3 derrière ; playfield opaque x 0-31 */
    memset(rows, 0, sizeof rows);
    rows[0][0] = rows[0][1] = rows[0][2] = rows[0][3] = 0xFF;
    setup(0x1200, 40, rows, 1);
    s_regs[0x104 >> 1] = 0x0010;
    sprite(0, Y, 8, 0xFFFF, 0, 0);
    sprite(6, Y, 24, 0xFFFF, 0, 0);
    frame();
    check("T3 sprite 0 devant le playfield", 2 * 10, Y, 0xF0F);
    check("T3 sprite 6 derriere le playfield opaque", 2 * 28, Y, 0xF00);
    check("T3 sprite 6 sur playfield transparent (COLOR29)", 2 * 35, Y, 0xFF0);

    /* T4 sprites 0+1 attachés : pixel 4 bits = 1001 -> COLOR25 */
    memset(rows, 0, sizeof rows);
    setup(0x1200, 40, rows, 1);
    sprite(0, Y, 50, 0xFFFF, 0, 0);
    sprite(1, Y, 50, 0, 0xFFFF, 1);
    frame();
    check("T4 paire attachee", 2 * 55, Y, 0x0FF);

    /* T5 fenêtre horizontale x 16-303 */
    memset(rows, 0xFF, sizeof rows);
    setup(0x1200, 40, rows, 1);
    s_regs[0x08E >> 1] = 0x2C91; s_regs[0x090 >> 1] = 0x2CB1;
    frame();
    check("T5 DIW avant HSTART", 2 * 10, Y, 0x000);
    check("T5 DIW dans la fenetre", 2 * 20, Y, 0xF00);
    check("T5 DIW dernier pixel", 2 * 303, Y, 0xF00);
    check("T5 DIW apres HSTOP", 2 * 310, Y, 0x000);

    /* T6 DDFSTRT $30 (un mot avant la fenêtre) et BPLCON1 = 4 : ses 4 derniers bits à gauche */
    memset(rows, 0, sizeof rows);
    rows[0][0] = rows[0][1] = 0xFF;
    setup(0x1200, 42, rows, 1);
    s_regs[0x092 >> 1] = 0x0030; s_regs[0x094 >> 1] = 0x00D0;
    s_regs[0x102 >> 1] = 0x0004;
    frame();
    check("T6 defilement : mot lu avant la fenetre", 2 * 3, Y, 0xF00);
    check("T6 defilement : suite", 2 * 4, Y, 0x000);

    /* T7 hires, BPLCON1 = 1 : décalage de 2 pixels hires */
    memset(rows, 0, sizeof rows);
    rows[0][0] = 0xFF;
    setup(0x9200, 80, rows, 1);
    s_regs[0x102 >> 1] = 0x0001;
    frame();
    check("T7 hires px1", 1, Y, 0x000);
    check("T7 hires px2", 2, Y, 0xF00);
    check("T7 hires px9", 9, Y, 0xF00);
    check("T7 hires px10", 10, Y, 0x000);

    return fails ? 1 : 0;
}
