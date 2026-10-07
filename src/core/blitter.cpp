/* Blitter OCS — Fase 0. Ground truth: HRM cap. 6 + spec empirica dal log [BLT]
 * (i blit reali del KS 1.3: coppie area-mode EA/2A con FWM/LWM, e line mode
 *  CON1 bit0 per il disegno della grafica insert-disk).
 * Modello funzionale: il blit viene eseguito interamente alla scrittura di
 * BLTSIZE (BBUSY mai visibile: DMACONR risponde gia' 0). A fine blit viene
 * alzato INTREQ bit 6 (BLIT).
 * Area mode: canali A/B/C/D, minterm LF completo, shift ASH/BSH con carry
 * tra word, FWM/LWM, ascending/descending, fill inclusivo/esclusivo (DESC).
 * Line mode: accumulatore BLTAPTL, ottanti SUD/SUL/AUL, SING, texture BLTBDAT.
 */
#include "a500.h"
#include <time.h>
extern "C" {
#include "m68k.h"
}

static long long blit_total_ns = 0;
long long blitter_ns(void) { return blit_total_ns; }

/* accesso word alla chip RAM (indirizzi word-aligned, mask 512KB).
 * Portage VGA32 : une lecture et une écriture 16 bits (au lieu de 2 octets ; chaque écriture
 * coûte un memw sur la cible). Résultats identiques : make testblitdiff. */
static inline uint16_t rd16(uint32_t a)
{
    return __builtin_bswap16(*(const amiga_u16_t *)&chip_ram[a & 0x7FFFE]);
}
static inline void wr16(uint32_t a, uint16_t v)
{
    wr_be16(&chip_ram[a & 0x7FFFE], v);
}

/* Portage VGA32 : minterm sans branchement. Les 8 bits de LF deviennent 8 masques (0 ou
 * 0xFFFF) une fois par blit ; par mot, c choisit entre les termes « c = 1 » (bits 7,5,3,1) et
 * « c = 0 » (bits 6,4,2,0) des quatre combinaisons de a et b. Même table de vérité que
 * l'ancienne évaluation terme à terme (8 tests par mot). */
typedef struct { uint16_t m[8]; } minterm_t;
static inline minterm_t minterm_prep(uint8_t lf)
{
    minterm_t t;
    for (int i = 0; i < 8; i++) t.m[i] = (lf >> i) & 1 ? 0xFFFF : 0;
    return t;
}
static inline uint16_t minterm(const minterm_t *t, uint16_t a, uint16_t b, uint16_t c)
{
    const uint16_t ab = a & b, anb = a & ~b, nab = ~a & b, nanb = ~(a | b);
    const uint16_t hi = (ab & t->m[7]) | (anb & t->m[5]) | (nab & t->m[3]) | (nanb & t->m[1]);
    const uint16_t lo = (ab & t->m[6]) | (anb & t->m[4]) | (nab & t->m[2]) | (nanb & t->m[0]);
    return (uint16_t)((c & hi) | (~c & lo));
}

/* Portage VGA32 : remplissage (fill) par table, 8 bits à la fois au lieu de 16 itérations par
 * mot. Entrée : mode (0 = inclusif IFE, 1 = exclusif EFE), état de remplissage, octet ; sortie :
 * octet rempli | état final << 8. Règle bit à bit inchangée (du LSB vers le MSB). */
static uint16_t fill_tab[2][2][256];
static void fill_tab_init(void)
{
    static int done = 0;
    if (done) return;
    for (int mode = 0; mode < 2; mode++)
        for (int fc0 = 0; fc0 < 2; fc0++)
            for (int v = 0; v < 256; v++) {
                int fc = fc0, out = 0;
                for (int i = 0; i < 8; i++) {
                    int bit = (v >> i) & 1;
                    int ob = mode == 0 ? (bit | fc) : (fc & ~bit);
                    out |= ob << i;
                    fc ^= bit;
                }
                fill_tab[mode][fc0][v] = (uint16_t)(out | fc << 8);
            }
    done = 1;
}
static inline uint16_t fill_word(int mode, int *fc, uint16_t d)
{
    uint16_t lo = fill_tab[mode][*fc][d & 0xFF];
    uint16_t hi = fill_tab[mode][lo >> 8][d >> 8];
    *fc = hi >> 8;
    return (uint16_t)((lo & 0xFF) | (hi & 0xFF) << 8);
}

static uint32_t pt(uint32_t off_h)   /* puntatore 20 bit da coppia H/L */
{
    return (((uint32_t)custom_get(off_h) << 16) | custom_get(off_h + 2)) & 0x7FFFF;
}

/* ---------------- area mode ---------------- */
static void blit_area(uint16_t bltsize)
{
    uint16_t con0 = custom_get(0x040), con1 = custom_get(0x042);
    int w = bltsize & 0x3F;         if (!w) w = 64;
    int h = (bltsize >> 6) & 0x3FF; if (!h) h = 1024;
    int ash = con0 >> 12, bsh = con1 >> 12;
    int usea = con0 & 0x800, useb = con0 & 0x400, usec = con0 & 0x200, used = con0 & 0x100;
    const minterm_t mt = minterm_prep((uint8_t)con0);
    int desc = con1 & 0x002;
    int ife  = con1 & 0x008, efe = con1 & 0x010, fci = (con1 & 0x004) ? 1 : 0;
    const int fill = ife || efe, fill_mode = ife ? 0 : 1;
    if (fill) fill_tab_init();
    uint16_t fwm = custom_get(0x044), lwm = custom_get(0x046);
    uint32_t apt = pt(0x050), bpt = pt(0x04C), cpt = pt(0x048), dpt = pt(0x054);
    int16_t amod = (int16_t)custom_get(0x064), bmod = (int16_t)custom_get(0x062);
    int16_t cmod = (int16_t)custom_get(0x060), dmod = (int16_t)custom_get(0x066);
    uint16_t adat = custom_get(0x074), bdat = custom_get(0x072), cdat = custom_get(0x070);
    int dir = desc ? -1 : 1;

    uint16_t aprev = 0, bprev = 0;
    for (int y = 0; y < h; y++) {
        int fc = fci;
        for (int x = 0; x < w; x++) {
            uint16_t araw = usea ? rd16(apt) : adat;
            if (x == 0)     araw &= fwm;
            if (x == w - 1) araw &= lwm;
            uint16_t ahold, bhold;
            if (!desc) {
                ahold = (uint16_t)((((uint32_t)aprev << 16) | araw) >> ash);
            } else {
                ahold = (uint16_t)(((((uint32_t)araw << 16) | aprev) >> (16 - ash)));
                if (ash == 0) ahold = araw;
            }
            aprev = araw;
            uint16_t braw = useb ? rd16(bpt) : bdat;
            if (!desc) {
                bhold = (uint16_t)((((uint32_t)bprev << 16) | braw) >> bsh);
            } else {
                bhold = (uint16_t)(((((uint32_t)braw << 16) | bprev) >> (16 - bsh)));
                if (bsh == 0) bhold = braw;
            }
            if (useb) bprev = braw;
            uint16_t chold = usec ? rd16(cpt) : cdat;
            uint16_t dhold = minterm(&mt, ahold, bhold, chold);

            if (fill)           /* fill (significativo in DESC): bit da LSB a MSB */
                dhold = fill_word(fill_mode, &fc, dhold);

            if (used) wr16(dpt, dhold);
            if (usea) apt += (uint32_t)(dir * 2);
            if (useb) bpt += (uint32_t)(dir * 2);
            if (usec) cpt += (uint32_t)(dir * 2);
            if (used) dpt += (uint32_t)(dir * 2);
        }
        if (usea) apt += (uint32_t)(dir * amod);
        if (useb) bpt += (uint32_t)(dir * bmod);
        if (usec) cpt += (uint32_t)(dir * cmod);
        if (used) dpt += (uint32_t)(dir * dmod);
    }
    /* writeback dei puntatori (l'hardware li lascia al valore finale) */
    custom_set_latch(0x050, (uint16_t)(apt >> 16)); custom_set_latch(0x052, (uint16_t)apt);
    custom_set_latch(0x04C, (uint16_t)(bpt >> 16)); custom_set_latch(0x04E, (uint16_t)bpt);
    custom_set_latch(0x048, (uint16_t)(cpt >> 16)); custom_set_latch(0x04A, (uint16_t)cpt);
    custom_set_latch(0x054, (uint16_t)(dpt >> 16)); custom_set_latch(0x056, (uint16_t)dpt);
}

/* ---------------- line mode ----------------
 * HRM: BLTAPTL = 4*dy - 2*dx (accumulatore, signed), BLTAMOD = 4*(dy-dx),
 * BLTBMOD = 4*dy, BLTCMOD = larghezza raster in byte, lunghezza = h di
 * BLTSIZE (w deve essere 2). Ottanti: SUD(0x10) SUL(0x08) AUL(0x04),
 * SIGN iniziale in CON1 bit6, SING(0x02) = un pixel per riga raster.
 * Regola: se SIGN=0 (acc >= 0) avanza anche l'asse minore; l'asse maggiore
 * avanza sempre; poi acc += (SIGN=0 ? AMOD : BMOD).
 */
static void blit_line(uint16_t bltsize)
{
    uint16_t con0 = custom_get(0x040), con1 = custom_get(0x042);
    int len = (bltsize >> 6) & 0x3FF; if (!len) len = 1024;
    int x   = con0 >> 12;                 /* pixel di partenza nella word */
    int bsh = con1 >> 12;                 /* rotazione texture */
    const minterm_t mt = minterm_prep((uint8_t)con0);
    int sign = (con1 & 0x040) ? 1 : 0;
    int sing = con1 & 0x002, sud = con1 & 0x010, sul = con1 & 0x008, aul = con1 & 0x004;
    uint32_t cpt = pt(0x048);             /* dest = C = D */
    int16_t amod = (int16_t)custom_get(0x064), bmod = (int16_t)custom_get(0x062);
    int16_t cmod = (int16_t)custom_get(0x060);
    uint16_t adat = custom_get(0x074) & custom_get(0x044);   /* ADAT & FWM */
    uint16_t bdat = custom_get(0x072);
    int16_t acc = (int16_t)custom_get(0x052);                /* BLTAPTL */
    int dot_row = 0;

    for (int i = 0; i < len; i++) {
        if (!sing || !dot_row) {
            uint16_t am = adat;
            if (i == len - 1) am &= custom_get(0x046);   /* LWM sull'ultimo pixel */
            uint16_t ahold = (uint16_t)(am >> x);
            uint16_t bhold = (uint16_t)((bdat >> bsh) | (bdat << (16 - bsh)));
            if (bsh == 0) bhold = bdat;
            uint16_t chold = rd16(cpt);
            wr16(cpt, minterm(&mt, ahold, bhold, chold));
            dot_row = 1;
        }
        bdat = (uint16_t)((bdat << 1) | (bdat >> 15));       /* texture avanza per pixel */

        /* ottanti (ground truth WinUAE blitter.cpp):
           SUD=1: maggiore=X, minore=Y | SUD=0: maggiore=Y, minore=X
           direzione minore = SUL, direzione maggiore = AUL */
        if (!sign) {                                          /* passo asse minore */
            if (sud) { if (sul) { cpt -= (uint32_t)cmod; dot_row = 0; }
                       else     { cpt += (uint32_t)cmod; dot_row = 0; } }
            else     { if (sul) { if (x-- == 0) { x = 15; cpt -= 2; } }
                       else     { if (++x == 16) { x = 0; cpt += 2; } } }
        }
        if (sud) {                                            /* passo asse maggiore */
            if (aul) { if (x-- == 0) { x = 15; cpt -= 2; } }
            else     { if (++x == 16) { x = 0; cpt += 2; } }
        } else {
            if (aul) { cpt -= (uint32_t)cmod; } else { cpt += (uint32_t)cmod; }
            dot_row = 0;
        }
        acc = (int16_t)(acc + (sign ? bmod : amod));
        sign = acc < 0;
    }
    custom_set_latch(0x048, (uint16_t)(cpt >> 16));
    custom_set_latch(0x04A, (uint16_t)cpt);
    custom_set_latch(0x052, (uint16_t)acc);
}

int blit_irq_pending = 0;   /* completamento differito alla prossima scanline:
                               l'int sincrono verrebbe cancellato dall'ack del
                               server QBlit che arriva DOPO l'avvio del blit */

void blitter_do(uint16_t bltsize)
{
    /* Portage VGA32 : chronométrage (blitter_ns) réservé au PC et au build de profiling :
     * deux clock_gettime par blit coûtent sur la cible (176 blits/trame mesurés en jeu). */
#if !defined(ARDUINO) || (defined(VGA32_PROF) && VGA32_PROF)
    struct timespec a, c;
    clock_gettime(CLOCK_MONOTONIC, &a);
#endif
    if (custom_get(0x042) & 0x0001) blit_line(bltsize);
    else                            blit_area(bltsize);
#if !defined(ARDUINO) || (defined(VGA32_PROF) && VGA32_PROF)
    clock_gettime(CLOCK_MONOTONIC, &c);
    blit_total_ns += (c.tv_sec-a.tv_sec)*1000000000LL + (c.tv_nsec-a.tv_nsec);
#endif
    blit_irq_pending = 1;
}
