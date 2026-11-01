/* test_paula_irq_c.cpp — Interruptions AUD0-3 de Paula (core/paula.cpp), d'après le HRM et
 * l'oracle de ../KickSlopStart/docs/analysis/audio-device-v34.md §8.1 : interruption au début de
 * chaque bloc DMA (allumage puis chaque rechargement de LC/LEN), au chargement de AUDxDAT en mode
 * manuel, et après le mot en cours quand le DMA est coupé. Avance par lignes (227 color clocks),
 * comme l'émulateur. */
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include "a500.h"

uint8_t chip_ram[CHIP_SIZE];
void logmsg(const char *, ...) {}

#define CC_LINE 227
static long now_cc;                 /* color clocks écoulés */
static long irq_at[16][8];          /* instants des interruptions par bit INTREQ */
static int  irq_n[16];
void intreq_set(int bit) { if (irq_n[bit] < 8) irq_at[bit][irq_n[bit]] = now_cc; irq_n[bit]++; }

static int fails = 0;
static void check(const char *name, bool ok, long got, long want)
{
    printf("%s %s : %ld (attendu %ld)\n", ok ? "OK  " : "FAIL", name, got, want);
    if (!ok) fails++;
}
static void run(long cc) { for (long d = 0; d < cc; d += CC_LINE) { paula_step(CC_LINE); now_cc += CC_LINE; } }
static void reset_all(void) { paula_reset(); now_cc = 0; for (int b = 0; b < 16; b++) irq_n[b] = 0; }
static void near(const char *name, int bit, int k, long want, long tol)
{
    long got = irq_n[bit] > k ? irq_at[bit][k] : -1;
    check(name, got >= 0 && got >= want - tol && got <= want + tol, got, want);
}

int main()
{
    /* T1 : AUD0 500 mots, période 400, DMA allumé -> bloc de 400 000 color clocks */
    reset_all();
    paula_write(0xA4, 500); paula_write(0xA6, 400); paula_write(0xA8, 64);
    paula_write(0xA0, 0); paula_write(0xA2, 0x1000);
    paula_set_dma(0x0201);
    run(3 * 400000L + 1000);
    near("T1 DMA allume : interruption de debut de bloc", 7, 0, 0, CC_LINE);
    near("T1 1er rechargement", 7, 1, 400000L, 2 * 400 + CC_LINE);
    near("T1 2e rechargement", 7, 2, 800000L, 2 * 400 + CC_LINE);
    check("T1 nombre d'interruptions", irq_n[7] == 4, irq_n[7], 4);
    check("T1 aucun autre canal", irq_n[8] + irq_n[9] + irq_n[10] == 0, irq_n[8] + irq_n[9] + irq_n[10], 0);

    /* T2 : AUD1 mode manuel, période 1 (démarrage d'audio.device) : une interruption, puis repos */
    reset_all();
    paula_write(0xB6, 1); paula_write(0xBA, 0x4040);
    run(10 * CC_LINE);
    near("T2 manuel : interruption au chargement de AUDxDAT", 8, 0, 0, CC_LINE);
    check("T2 manuel : canal au repos ensuite", irq_n[8] == 1, irq_n[8], 1);

    /* T3 : AUD2 mode manuel, période 300 : le 2e mot (AUDxDAT réécrit) part après 600 cc */
    reset_all();
    paula_write(0xC6, 300); paula_write(0xCA, 0x1111); paula_write(0xCA, 0x2222);
    run(20 * CC_LINE);
    check("T3 manuel : deux mots, deux interruptions", irq_n[9] == 2, irq_n[9], 2);
    near("T3 manuel : 2e interruption apres un mot", 9, 1, 600, CC_LINE);

    /* T4 : AUD3 DMA coupé en cours de bloc : interruption après le mot en cours, puis repos */
    reset_all();
    paula_write(0xD4, 1000); paula_write(0xD6, 500);
    paula_write(0xD0, 0); paula_write(0xD2, 0x2000);
    paula_set_dma(0x0208);
    run(100 * CC_LINE);
    long off = now_cc;
    paula_set_dma(0x0200);
    run(50 * CC_LINE);
    check("T4 coupure : 2 interruptions (debut de bloc + apres le mot)", irq_n[10] == 2, irq_n[10], 2);
    long dt = irq_n[10] > 1 ? irq_at[10][1] - off : -1;
    check("T4 coupure : au plus un mot (2 x 500 cc) plus tard", dt >= 0 && dt <= 1000 + CC_LINE, dt, 1000);

    /* T5 : AUDxLEN = 0 vaut 65 536 mots (audio-device-v34.md §5.3, ioa_Length impair ou trop
     * long) : le bloc dure 65 536 x 2 x 124 color clocks, pas un seul mot */
    reset_all();
    paula_write(0xA4, 0); paula_write(0xA6, 124);
    paula_write(0xA0, 0); paula_write(0xA2, 0);
    paula_set_dma(0x0201);
    run(65536L * 2 * 124 + 4 * CC_LINE);
    near("T5 LEN 0 : rechargement apres 65 536 mots", 7, 1, 65536L * 2 * 124, 2 * 124 + CC_LINE);
    check("T5 LEN 0 : deux interruptions seulement", irq_n[7] == 2, irq_n[7], 2);

    return fails ? 1 : 0;
}
