/* Copper + Denise (perimetro Fase 0) -> dump PPM dell'ultimo frame.
 * Semplificazioni dichiarate (da rimuovere in fasi successive):
 *   - Copper eseguito con granularita di linea (WAIT valutato solo in verticale;
 *     la posizione orizzontale del WAIT è ignorata)
 *     [Portage VGA32 : position horizontale suivie, couleurs écrites en cours de ligne]
 *   - Fetch bitplane standard lores 320px (DDFSTRT/STOP non interpretati:
 *     assunta finestra standard 0x38/0xD0 come quella del KS)
 *     [Portage VGA32 : désormais interprétés pour le fetch, cf. denise_render_line]
 *   - Niente sprite, niente HAM, niente dual playfield, niente hires
 *     [Portage VGA32 : sprites attachés, priorités BPLCON2, HAM, double playfield, DIW horizontal]
 * Sufficiente e corretto per la schermata insert-disk del KS 1.3.
 */
#include "a500.h"
#include <string.h>
#ifdef ARDUINO
#include "esp_heap_caps.h"
#endif

/* Portage VGA32 : profiling interne de denise_render_line (env pio ttgo-vga32-prof, flag
 * -DVGA32_PROF=1 visible ici car passé en ligne de commande). Les marques DPROF()/DPROF_LINE()
 * s'écrivent SANS point-virgule : hors profiling elles disparaissent sans laisser de ';' vide,
 * pour que le build normal reste token-identique (un token de plus suffit à changer
 * l'allocation de registres de cette fonction chaude). */
#if defined(ARDUINO) && defined(VGA32_PROF) && VGA32_PROF
uint32_t denise_prof_cyc[DP_N];
uint32_t denise_prof_lines[DL_N];
static inline uint32_t dprof_cc(void)
{
    uint32_t c;
    __asm__ __volatile__("rsr %0, ccount" : "=a"(c));
    return c;
}
#define DPROF_START()   uint32_t dprof_t = dprof_cc();
#define DPROF(slot)     { uint32_t dprof_n = dprof_cc(); denise_prof_cyc[slot] += dprof_n - dprof_t; \
                          dprof_t = dprof_n; }
#define DPROF_LINE(k)   denise_prof_lines[k]++;
#else
#define DPROF_START()
#define DPROF(slot)
#define DPROF_LINE(k)
#endif

/* stato copper */
static uint32_t cop_pc;
static int      cop_stopped;      /* WAIT mai soddisfatto (fine lista) */
static int      coplog = 0;       /* log delle prime istruzioni        */
/* Portage VGA32 : position horizontale du copper, en color clocks dans la ligne (227 en PAL).
 * Une instruction coûte 4 color clocks (2 mots lus sur les cycles pairs), le réveil après un
 * WAIT 2 de plus : l'instruction qui suit WAIT $FFDF s'évalue ainsi à la ligne 256 (HRM). */
#define COP_LINE_CC 227
static int      cop_h_carry;      /* position de reprise sur la ligne suivante (peut être < 0) */
static int      cop_waiting;      /* WAIT en attente (cop_pc pointe après lui) */
static int      cop_wvp, cop_wve, cop_whp;   /* cible du WAIT : V, masque V, H masqué */
/* Portage VGA32 : écritures du copper différées à leur position dans la ligne (couleurs dans la
 * partie visible, registres de plans après la fin du fetch), appliquées par Denise ; x en pixels
 * lores, 0 = bord gauche de la fenêtre standard ($81). */
#define COP_EV_MAX 96
static struct { int16_t x; uint16_t reg, val; } cop_ev[COP_EV_MAX];
static int      cop_nev, cop_ev_done;

/* framebuffer 320 x 312 (finestra PAL completa), 12 bit -> 24 bit in uscita */
#ifdef ARDUINO
#endif
#define FB_W 640                  /* larghezza hires; in lores i pixel si raddoppiano */
#define FB_H 312
#ifdef ARDUINO
bool g_row_onscreen[FB_H] = {false};  // riga confermata sul display dal dirty-check
#endif
/* Portage VGA32 : écritures de pixels par mots de 32 bits (2 pixels par memw, inséré après
 * chaque écriture par -mfix-esp32-psram-cache-issue). may_alias : écriture légale d'un
 * tableau uint16_t via un pointeur 32 bits. */
typedef uint32_t __attribute__((may_alias)) fb_word_t;
#ifdef ARDUINO
// niente framebuffer intero (400KB non stanno in DRAM): una riga alla volta.
static uint16_t fb_line[FB_W] __attribute__((aligned(4)));
static inline void fb_line_fill(uint16_t c)
{
    fb_word_t *p = (fb_word_t *)fb_line;
    const uint32_t v = c | ((uint32_t)c << 16);
    for (int i = 0; i < FB_W / 2; i++) p[i] = v;
}
// callback fornito dallo sketch: riceve (riga_video, pixel RGB565, larghezza)
void (*denise_line_cb)(int vpos, const uint16_t *pixels, int w) = nullptr;
// converte Amiga 12-bit (0x0RGB) in RGB565
static inline uint16_t amiga_to_565(uint16_t c) {
    uint8_t r = ((c >> 8) & 0xF) * 17, g = ((c >> 4) & 0xF) * 17, b = (c & 0xF) * 17;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
#else
static uint16_t fb[FB_H][FB_W] __attribute__((aligned(4)));   /* colore RGB 4:4:4 */
#endif

/* Portage VGA32 : conversion planaire -> chunky par tables, 8 pixels à la fois. c2p_hi[b]
 * range les bits 7..4 de l'octet de plan b (pixels 0..3) et c2p_lo[b] les bits 3..0 (pixels
 * 4..7), un pixel par octet (octet k = pixel k, petit-boutiste) ; décalé de p bits, le plan p
 * pose son bit dans chaque octet. En DRAM (pas en flash : lus à chaque groupe de 8 pixels). */
static uint32_t c2p_hi[256], c2p_lo[256];
static void c2p_init(void)
{
    static int done = 0;
    if (done) return;
    for (int b = 0; b < 256; b++) {
        uint32_t h = 0, l = 0;
        for (int k = 0; k < 4; k++) {
            h |= (uint32_t)((b >> (7 - k)) & 1) << (8 * k);
            l |= (uint32_t)((b >> (3 - k)) & 1) << (8 * k);
        }
        c2p_hi[b] = h; c2p_lo[b] = l;
    }
    done = 1;
}

/* Décale un plan de sc bits vers la droite (scroll fin BPLCON1) : le bit du pixel x devient
 * celui du pixel x - sc de la source. Portage VGA32 : src est précédé de LINE_PRE octets, ceux
 * lus avant la fenêtre (DDFSTRT avancé pour le défilement) ou 0 ; sc <= 30 (hires). */
#define LINE_PRE 4
static void plane_shift(uint8_t *dst, const uint8_t *src, int nbytes, int sc)
{
    const int q = sc >> 3, r = sc & 7;
    for (int i = 0; i < nbytes; i++) {
        const int k = i - q;
        const uint8_t cur  = src[k];
        const uint8_t prev = src[k - 1];
        dst[i] = r ? (uint8_t)((cur >> r) | (prev << (8 - r))) : cur;
    }
}

/*
 * Palette Amiga OCS/ECS:
 * - COLOR00..COLOR31 sono 12-bit RGB (0x0RGB).
 * - con 5 bitplane il pixel puo' usare indici 0..31.
 * - con 6 bitplane, in modalita' EHB, gli indici 32..63 sono half-bright
 *   di COLOR00..COLOR31.
 *
 * La vecchia versione usava array da 16 colori: con Holiday Lemmings
 * (schermata a 5 bitplane / 32 colori) idx poteva superare 15 e leggere
 * fuori dalla palette locale, producendo forme corrette ma colori sbagliati.
 */
static inline uint16_t half_bright_12(uint16_t c)
{
    uint16_t r = (c >> 8) & 0x0F;
    uint16_t g = (c >> 4) & 0x0F;
    uint16_t b = (c >> 0) & 0x0F;
    r >>= 1; g >>= 1; b >>= 1;
    return (uint16_t)((r << 8) | (g << 4) | b);
}

static inline uint16_t palette_entry_12(int idx)
{
    idx &= 63;
    if (idx < 32)
        return (uint16_t)(custom_get(0x180 + (uint32_t)idx * 2) & 0x0FFF);
    return half_bright_12(custom_get(0x180 + (uint32_t)(idx - 32) * 2));
}

#ifdef ARDUINO
static uint8_t  color_diag_max_idx = 0;
static uint16_t color_diag_last_bplcon0 = 0;
static int      color_diag_last_nplanes = 0;
#endif

void video_color_diag_dump(void)
{
#ifdef ARDUINO
    uint16_t bplcon0 = custom_get(0x100);
    int nplanes = (bplcon0 >> 12) & 7;
    logmsg("[COLOR-DIAG] BPLCON0=%04X nplanes=%d lastBPLCON0=%04X lastN=%d max_color_index=%u\n",
           bplcon0, nplanes, color_diag_last_bplcon0, color_diag_last_nplanes, color_diag_max_idx);
    for (int i = 0; i < 32; i += 8) {
        logmsg("[COLOR-DIAG] COLOR%02d-%02d: %03X %03X %03X %03X %03X %03X %03X %03X\n",
               i, i + 7,
               custom_get(0x180 + (uint32_t)(i + 0) * 2) & 0x0FFF,
               custom_get(0x180 + (uint32_t)(i + 1) * 2) & 0x0FFF,
               custom_get(0x180 + (uint32_t)(i + 2) * 2) & 0x0FFF,
               custom_get(0x180 + (uint32_t)(i + 3) * 2) & 0x0FFF,
               custom_get(0x180 + (uint32_t)(i + 4) * 2) & 0x0FFF,
               custom_get(0x180 + (uint32_t)(i + 5) * 2) & 0x0FFF,
               custom_get(0x180 + (uint32_t)(i + 6) * 2) & 0x0FFF,
               custom_get(0x180 + (uint32_t)(i + 7) * 2) & 0x0FFF);
    }
#endif
}

static inline uint32_t chip_r16(uint32_t a)
{
    a &= 0x7FFFE;
    return (uint32_t)chip_ram[a] << 8 | chip_ram[a + 1];
}

void copper_vblank(void)
{
    cop_pc = ((uint32_t)custom_get(0x080) << 16 | custom_get(0x082)) & 0x7FFFE;
    cop_stopped = 0;
    cop_waiting = 0;
    cop_h_carry = 0;
}

void copper_jmp(int which)   /* strobe COPJMP1/2 */
{
    uint32_t off = which == 1 ? 0x080 : 0x084;
    cop_pc = ((uint32_t)custom_get(off) << 16 | custom_get(off + 2)) & 0x7FFFE;
    cop_stopped = 0;
    cop_waiting = 0;
}

int copper_writing = 0;   /* attribuzione log: 1 durante i MOVE del copper */

/* Portage VGA32 : 1 si le faisceau (vpos, *h) a atteint la cible du WAIT (*h avancé au réveil),
 * 0 s'il faut attendre une ligne suivante. Comparaison sur V7-V0 comme le copper. */
static int cop_wait_done(int *h)
{
    const int vb = vpos & cop_wve, vt = cop_wvp & cop_wve;
    if (vb < vt) return 0;
    if (vb == vt && *h < cop_whp) {
        if (cop_whp >= COP_LINE_CC) return 0;
        *h = cop_whp + 2;
    }
    return 1;
}

/* Portage VGA32 : 1 si l'écriture est différée (cf. cop_ev), 0 s'il faut l'écrire tout de suite */
static int cop_defer(uint32_t reg, uint16_t v, int h)
{
    const int x = 2 * h - 0x80;
    const int color = reg >= 0x180 && reg <= 0x1BE;
    const int display = (reg >= 0x08E && reg <= 0x094) || (reg >= 0x0E0 && reg <= 0x0F6) ||
                        (reg >= 0x100 && reg <= 0x10A);
    if (!((color && x > 0) || (display && h > 0xD8)) || cop_nev >= COP_EV_MAX) return 0;
    cop_ev[cop_nev].x = (int16_t)x;
    cop_ev[cop_nev].reg = (uint16_t)reg;
    cop_ev[cop_nev].val = v;
    cop_nev++;
    return 1;
}

/* Portage VGA32 : applique les écritures différées jusqu'à la position x incluse */
static void cop_ev_apply(int x)
{
    while (cop_ev_done < cop_nev && cop_ev[cop_ev_done].x <= x) {
        copper_writing = 1;
        custom_write(cop_ev[cop_ev_done].reg, cop_ev[cop_ev_done].val);
        copper_writing = 0;
        cop_ev_done++;
    }
}

/* Portage VGA32 : une écriture différée tombe dans la partie visible de la ligne */
static inline int cop_ev_visible(void)
{
    return cop_ev_done < cop_nev && cop_ev[cop_ev_done].x < 320;
}

/* esegue il copper per la linea corrente (vpos globale) */
void copper_run_line(void)
{
    cop_nev = cop_ev_done = 0;
    if (cop_stopped) return;
    if (!((dmacon & 0x0200) && (dmacon & 0x0080))) return;  /* DMAEN+COPEN */
    int h = cop_h_carry;
    cop_h_carry = 0;
    if (cop_waiting) {
        if (!cop_wait_done(&h)) return;
        cop_waiting = 0;
    }
    for (int guard = 0; guard < 512; guard++) {   /* anti-loop */
        if (h + 4 >= COP_LINE_CC) { cop_h_carry = h - COP_LINE_CC; return; }
        h += 4;
        uint16_t ir1 = (uint16_t)chip_r16(cop_pc);
        uint16_t ir2 = (uint16_t)chip_r16(cop_pc + 2);
        if (!(ir1 & 1)) {                          /* MOVE */
            uint32_t reg = ir1 & 0x1FE;
            /* CDANG (hardware reale): MOVE sotto 0x080 senza COPCON bit0
               e' illegale e FERMA il copper fino al prossimo vblank ; portage VGA32 : sur OCS,
               CDANG ne débloque que $040-$07E, $000-$03E reste interdit (custom-chips-v34 §5.8) */
            if (reg < 0x040 || (reg < 0x080 && !(custom_get(0x02E) & 1))) {
                if (coplog < 24) { logmsg("[COP] %06X MOVE ILLEGALE %03X (CDANG): copper fermato\n", cop_pc, reg); coplog++; }
                cop_stopped = 1;
                return;
            }
            cop_pc = (cop_pc + 4) & 0x7FFFE;
            if (coplog < 24) { logmsg("[COP] %06X MOVE %03X,%04X\n", cop_pc - 4, reg, ir2); coplog++; }
            if (!cop_defer(reg, ir2, h)) {
                copper_writing = 1;
                custom_write(reg, ir2);
                copper_writing = 0;
            }
        } else if (!(ir2 & 1)) {                   /* WAIT */
            int vp = (ir1 >> 8) & 0xFF;
            int ve = ((ir2 >> 8) & 0x7F) | 0x80;
            if (coplog < 24) { logmsg("[COP] %06X WAIT v=%02X ve=%02X\n", cop_pc, vp, ve); coplog++; }
            if (ir1 == 0xFFFF && ir2 == 0xFFFE) { cop_stopped = 1; return; } /* fine lista */
            cop_pc = (cop_pc + 4) & 0x7FFFE;
            cop_wvp = vp; cop_wve = ve; cop_whp = ir1 & ir2 & 0xFE;
            if (!cop_wait_done(&h)) { cop_waiting = 1; return; }
        } else {                                   /* SKIP */
            int vp = (ir1 >> 8) & 0xFF;
            int ve = ((ir2 >> 8) & 0x7F) | 0x80;
            const int vb = vpos & ve, vt = vp & ve, hp = ir1 & ir2 & 0xFE;
            cop_pc = (cop_pc + 4) & 0x7FFFE;
            if (vb > vt || (vb == vt && h >= hp)) cop_pc = (cop_pc + 4) & 0x7FFFE;
        }
    }
    cop_stopped = 1;  /* guard scattato: lista degenere, fermo il copper */
}

/* rendering di una linea nel framebuffer */
/* ============ SPRITE HARDWARE (8 canali DMA) ============
 * Lista in Chip RAM per sprite: POS,CTL poi coppie DATA,DATB per riga,
 * chiusa da 0,0 (o nuova POS,CTL per riuso). Il write di SPRxPTL arma
 * il canale. Colori: sprite 0-1 -> COLOR17-19, 2-3 -> 21-23, ecc.
 * Priorita': per ora sprite sopra il playfield (BPLCON2 in futuro). */
enum { SPR_OFF, SPR_FETCH, SPR_WAIT, SPR_ACTIVE };
static struct {
    uint32_t pc;              /* puntatore DMA corrente */
    int state;
    int vstart, vstop, hstart;
    int att;                  /* portage VGA32 : SPRxCTL bit 7 (sprite impair attaché au pair) */
    uint16_t data, datb;      /* parole della riga corrente */
} spr[8];

static uint16_t spr_rd16(uint32_t a) {
    a &= 0x7FFFE;
    return ((uint16_t)chip_ram[a] << 8) | chip_ram[a + 1];
}

void sprite_arm(int n)   /* chiamata al write di SPRxPTL */
{
    uint32_t ph = custom_get(0x120 + n * 4), pl = custom_get(0x122 + n * 4);
    spr[n].pc = (((uint32_t)ph << 16) | pl) & 0x7FFFE;
    spr[n].state = SPR_FETCH;
}

void sprite_vblank(void)  /* inizio frame */
{
    for (int i = 0; i < 8; i++)
        if (spr[i].state == SPR_ACTIVE)
            spr[i].state = SPR_OFF;   /* sprite lasciato a meta': senza riarmo resta spento */
        /* SPR_WAIT resta WAIT: il fetch e' fatto, aspetta il vstart del NUOVO frame.
           E' il caso del riarmo a fine frame (riga ~256): il fetch avviene subito,
           lo sprite parte alla riga giusta del frame successivo. */
}

/* aggiorna il DMA sprite per la riga corrente (vpos). Chiamata PRIMA del render. */
static void sprite_dma_line(void)
{
    if (!((dmacon & 0x0200) && (dmacon & 0x0020))) return;   /* DMAEN+SPREN */
    for (int i = 0; i < 8; i++) {
        if (spr[i].state == SPR_FETCH) {
            uint16_t pos = spr_rd16(spr[i].pc), ctl = spr_rd16(spr[i].pc + 2);
            spr[i].pc += 4;
            if (pos == 0 && ctl == 0) { spr[i].state = SPR_OFF; continue; }
            spr[i].vstart = (pos >> 8) | ((ctl & 0x04) << 6);          /* +E8 come bit 8 */
            spr[i].vstop  = (ctl >> 8) | ((ctl & 0x02) << 7);
            spr[i].hstart = ((pos & 0xFF) << 1) | (ctl & 0x01);
            spr[i].att = (ctl >> 7) & 1;
            spr[i].state = SPR_WAIT;
        }
        if (spr[i].state == SPR_WAIT && vpos == spr[i].vstart)
            spr[i].state = SPR_ACTIVE;
        if (spr[i].state == SPR_ACTIVE) {
            if (vpos == spr[i].vstop) {
                spr[i].state = SPR_FETCH;      /* fine: rifetch POS/CTL (riuso) */
                /* riprocesso subito il fetch per questa stessa riga */
                uint16_t pos = spr_rd16(spr[i].pc), ctl = spr_rd16(spr[i].pc + 2);
                spr[i].pc += 4;
                if (pos == 0 && ctl == 0) { spr[i].state = SPR_OFF; continue; }
                spr[i].vstart = (pos >> 8) | ((ctl & 0x04) << 6);
                spr[i].vstop  = (ctl >> 8) | ((ctl & 0x02) << 7);
                spr[i].hstart = ((pos & 0xFF) << 1) | (ctl & 0x01);
                spr[i].att = (ctl >> 7) & 1;
                spr[i].state = (vpos == spr[i].vstart) ? SPR_ACTIVE : SPR_WAIT;
                if (spr[i].state != SPR_ACTIVE) continue;
            }
            /* riga attiva: fetcha le parole dati */
            spr[i].data = spr_rd16(spr[i].pc);
            spr[i].datb = spr_rd16(spr[i].pc + 2);
            spr[i].pc += 4;
        }
    }
}

/* sovrappone gli sprite attivi alla riga corrente nel buffer row (Amiga 12-bit).
 * width_mode: 0 = buffer 640 con pixel lores raddoppiati (PC e hires),
 *             1 = buffer 320 lores netto (Arduino lores).
 * Portage VGA32 : idx (index de playfield par pixel de row) et front (pour chaque index, masque
 * des paires de sprites devant le playfield, BPLCON2) ; idx NULL = sprites tous devant. */
static inline void spr_put(uint16_t *row, int lores_narrow, int xl, uint16_t col, int pair,
                           const uint8_t *idx, const uint8_t *front)
{
    if (lores_narrow) {
        if (xl < 0 || xl >= 320) return;
        if (!idx || ((front[idx[xl]] >> pair) & 1)) row[xl] = col;
    } else {
        int xf = xl * 2;
        if (xf < 0 || xf >= FB_W - 1) return;
        if (!idx || ((front[idx[xf]] >> pair) & 1)) row[xf] = col;
        if (!idx || ((front[idx[xf + 1]] >> pair) & 1)) row[xf + 1] = col;
    }
}

static inline int spr_pix(int i, int b)
{
    return (((spr[i].datb >> (15 - b)) & 1) << 1) | ((spr[i].data >> (15 - b)) & 1);
}

static void sprite_overlay(uint16_t *row, int lores_narrow, const uint8_t *idx, const uint8_t *front)
{
    if (!((dmacon & 0x0200) && (dmacon & 0x0020))) return;
    for (int pr = 3; pr >= 0; pr--) {       /* sprite 0 = priorita' massima: disegnato ultimo */
        const int e = 2 * pr, o = e + 1;
        const int ea = spr[e].state == SPR_ACTIVE, oa = spr[o].state == SPR_ACTIVE;
        if (oa && spr[o].att) {
            /* Portage VGA32 : paire attachée, pixel de 4 bits (impair = bits 3-2) -> COLOR16-31 */
            const int xe = spr[e].hstart - 0x81, xo = spr[o].hstart - 0x81;
            const int x0 = ea && xe < xo ? xe : xo, x1 = (ea && xe > xo ? xe : xo) + 16;
            for (int xl = x0; xl < x1; xl++) {
                int pix = 0;
                if (ea && xl >= xe && xl < xe + 16) pix = spr_pix(e, xl - xe);
                if (xl >= xo && xl < xo + 16) pix |= spr_pix(o, xl - xo) << 2;
                if (pix) spr_put(row, lores_narrow, xl, custom_get(0x180 + (16 + pix) * 2), pr, idx, front);
            }
            continue;
        }
        for (int i = o; i >= e; i--) {
            if (spr[i].state != SPR_ACTIVE) continue;
            int x0 = spr[i].hstart - 0x81;      /* coordinate lores relative al bordo sinistro */
            for (int b = 0; b < 16; b++) {
                int pix = spr_pix(i, b);
                if (!pix) continue;             /* 0 = trasparente */
                uint16_t col = custom_get(0x180 + (16 + (i >> 1) * 4 + pix) * 2);
                spr_put(row, lores_narrow, x0 + b, col, pr, idx, front);
            }
        }
    }
}


/* Portage VGA32 : index de couleur d'un pixel en double playfield (HRM) : PF1 = plans 1,3,5
 * (COLOR00-07), PF2 = plans 2,4,6 (COLOR08-15), 0 = transparent ; PF2PRI met PF2 devant. */
static inline int dpf_color(int i, int pf2pri)
{
    const int c1 = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4);
    const int c2 = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4);
    if (pf2pri) return c2 ? 8 + c2 : c1;
    return c1 ? c1 : (c2 ? 8 + c2 : 0);
}

static void build_pal(uint16_t *pal, int ncol, int dpf, int pf2pri)
{
    for (int i = 0; i < ncol; i++) pal[i] = palette_entry_12(dpf ? dpf_color(i, pf2pri) : i);
}

/* Portage VGA32 : COLOR00 sur [0, w) en suivant les écritures du copper en cours de ligne
 * (scale = pixels de row par pixel lores) */
static void bg_fill_ev(uint16_t *row, int w, int scale)
{
    int x = 0;
    while (x < w) {
        int end = w;
        if (cop_ev_done < cop_nev && cop_ev[cop_ev_done].x * scale < end) end = cop_ev[cop_ev_done].x * scale;
        const uint16_t bg = custom_get(0x180);
        for (; x < end; x++) row[x] = bg;
        if (end < w) cop_ev_apply(cop_ev[cop_ev_done].x);
    }
}

/* Portage VGA32 : fenêtre horizontale DIWSTRT/DIWSTOP (HSTOP + $100) : COLOR00 hors fenêtre,
 * playfield et sprites compris ; x 0-319 = fenêtre standard $81-$1C1. */
static void diw_h_mask(uint16_t *row, int w, int scale)
{
    int x0 = (custom_get(0x08E) & 0xFF) - 0x81;
    int x1 = ((custom_get(0x090) & 0xFF) | 0x100) - 0x81;
    if (x0 <= 0 && x1 >= 320) return;
    x0 = x0 < 0 ? 0 : x0 > 320 ? 320 : x0;
    x1 = x1 < x0 ? x0 : x1 > 320 ? 320 : x1;
    const uint16_t bg = custom_get(0x180);
    for (int x = 0; x < x0 * scale && x < w; x++) row[x] = bg;
    for (int x = x1 * scale; x < w; x++) row[x] = bg;
}

static void denise_line(void);

void denise_render_line(void)
{
    denise_line();
    cop_ev_apply(0x7FFF);   /* portage VGA32 : écritures du copper après la partie visible */
}

static void denise_line(void)
{
    if (vpos >= FB_H) return;
    DPROF_START()
    sprite_dma_line();   /* DMA sprite: avanza SEMPRE, anche se il playfield e' saltato */
    DPROF(DP_SPRDMA)
    /* Portage VGA32 : en entrelacé, seule la trame longue est affichée (sortie VGA de 312
     * lignes) : image stable au lieu de deux trames décalées d'une ligne en alternance. Le
     * copper recharge les pointeurs de plans à chaque trame. */
    if (!lof && (custom_get(0x100) & 0x0004)) return;
    const int ev = cop_ev_visible();   /* portage VGA32 : couleur changée en cours de ligne */
    uint16_t bg = custom_get(0x180);              /* COLOR00 */
#ifdef ARDUINO
    /* Portage VGA32 : pas de remplissage de fond ici. Chaque chemin qui livre fb_line au
     * callback le remplit lui-même (BG_FLUSH, 0 bitplan) ou écrase tous les pixels lus
     * (ligne dessinée) ; une ligne sautée ne s'en sert pas. Mesuré : 10,5 ms/trame perdus
     * (memw après chaque écriture, -mfix-esp32-psram-cache-issue). */
    uint16_t *row = fb_line;
#else
    uint16_t *row = fb[vpos];
    for (int x = 0; x < FB_W; x++) row[x] = bg;
#endif
    DPROF(DP_BGFILL)

#ifdef ARDUINO
#define BG_FLUSH() do { DPROF(DP_WIN) if (denise_line_cb) { \
    if (ev) bg_fill_ev(fb_line, 640, 2); else fb_line_fill(bg); \
    denise_line_cb(vpos, fb_line, 640); } DPROF(DP_CB) DPROF_LINE(DL_BORDER) } while(0)
#else
#define BG_FLUSH() do { if (ev) bg_fill_ev(row, FB_W, 2); } while(0)
#endif

    if (!((dmacon & 0x0200) && (dmacon & 0x0100))) { BG_FLUSH(); return; }

    uint16_t diwstrt = custom_get(0x08E), diwstop = custom_get(0x090);
    int vstart = diwstrt >> 8;
    int vstop  = diwstop >> 8; if (!(vstop & 0x80)) vstop += 256;
    if (vpos < vstart || vpos >= vstop) { BG_FLUSH(); return; }

    uint16_t bplcon0 = custom_get(0x100);
    int hires   = (bplcon0 >> 15) & 1;
    int nplanes = (bplcon0 >> 12) & 7;
    if (nplanes == 0) {
        /* zero bitplane: la schermata e' fatta di solo copper-color + sprite
           (es. cracktro/starfield). Riempio con lo sfondo e disegno gli sprite. */
#ifdef ARDUINO
        DPROF(DP_WIN)
        if (denise_line_cb) {
            if (ev) bg_fill_ev(fb_line, 640, 2); else fb_line_fill(bg);
            sprite_overlay(fb_line, 0, nullptr, nullptr);   /* stelle/testo della cracktro */
            diw_h_mask(fb_line, 640, 2);
            denise_line_cb(vpos, fb_line, 640);
        }
        DPROF(DP_CB)
        DPROF_LINE(DL_NOPLANES)
#else
        if (ev) bg_fill_ev(row, FB_W, 2);
        sprite_overlay(row, 0, nullptr, nullptr);   /* harnais PC : row est déjà remplie avec bg */
        diw_h_mask(row, FB_W, 2);
#endif
        return;
    }
    if (hires && nplanes > 4) nplanes = 4;    /* hires: max 4 bitplane */
    if (nplanes > 6) { BG_FLUSH(); return; }

    int bytes_per_row = hires ? 80 : 40;      /* word DMA per riga di bitplane */
    int pixels        = hires ? 640 : 320;
    DPROF(DP_WIN)

    /* Portage VGA32 : DDFSTRT/DDFSTOP interprétés (HRM : lores (stop-strt)/8+1 mots, hires
     * (stop-strt)/4+2). La sortie reste la fenêtre standard (lores 0x38, hires 0x3C) : en
     * overscan les mots hors fenêtre sont lus (le pointeur avance du fetch réel, sinon l'image
     * se décale de ligne en ligne) mais non affichés ; une fenêtre plus étroite laisse du
     * COLOR00. DDF non programmé (stop <= strt) : fenêtre standard, comme avant. Les LINE_PRE
     * octets lus juste avant la fenêtre (DDFSTRT avancé d'un mot pour le défilement fin)
     * précèdent line[p] : le défilement en fait entrer les bits à gauche. */
    uint8_t lbuf[6][LINE_PRE + 80];
    uint8_t *line[6];
    for (int p = 0; p < nplanes; p++) {
        line[p] = lbuf[p] + LINE_PRE;
        memset(lbuf[p], 0, LINE_PRE);
    }
    const int ddf_unit = hires ? 4 : 8, ddf_std = hires ? 0x3C : 0x38;
    const int ddfstrt = custom_get(0x092) & (hires ? 0xFC : 0xF8);
    const int ddfstop = custom_get(0x094) & (hires ? 0xFC : 0xF8);
    if (ddfstop > ddfstrt &&
        (ddfstrt != ddf_std || ddfstop != ddf_std + (bytes_per_row / 2 - (hires ? 2 : 1)) * ddf_unit)) {
        const int fetch_bytes = 2 * ((ddfstop - ddfstrt) / ddf_unit + (hires ? 2 : 1));   /* <= 130 */
        const int skip = 2 * (ddf_std - ddfstrt) / ddf_unit;   /* octets fetchés avant la fenêtre */
        uint8_t tmp[130];
        for (int p = 0; p < nplanes; p++) {
            const uint32_t pt = bpl_pt[p] & 0x7FFFF;
            if (skip >= 0 && skip + bytes_per_row <= fetch_bytes && pt + (uint32_t)fetch_bytes <= 0x80000) {
                memcpy(line[p], &chip_ram[pt + (uint32_t)skip], bytes_per_row);   /* overscan : bloc PSRAM */
                for (int k = 1; k <= LINE_PRE && k <= skip; k++) line[p][-k] = chip_ram[pt + (uint32_t)(skip - k)];
            } else {
                for (int b = 0; b < fetch_bytes; b++) tmp[b] = chip_ram[(pt + (uint32_t)b) & 0x7FFFF];
                for (int b = -LINE_PRE; b < bytes_per_row; b++) {
                    const int s = b + skip;
                    line[p][b] = (s >= 0 && s < fetch_bytes) ? tmp[s] : 0;
                }
            }
            bpl_pt[p] = pt + (uint32_t)fetch_bytes + (uint32_t)(int16_t)custom_get((p & 1) ? 0x10A : 0x108);
        }
    } else
    for (int p = 0; p < nplanes; p++) {
        uint32_t pt = bpl_pt[p] & 0x7FFFF;
        /* lettura a BLOCCO invece che byte-per-byte: la PSRAM e' molto piu'
           veloce con memcpy che con tanti accessi singoli. Gestisco il wrap. */
        if (pt + (uint32_t)bytes_per_row <= 0x80000) {
            memcpy(line[p], &chip_ram[pt], bytes_per_row);
        } else {
            for (int b = 0; b < bytes_per_row; b++)
                line[p][b] = chip_ram[(pt + (uint32_t)b) & 0x7FFFF];
        }
        bpl_pt[p] = pt + (uint32_t)bytes_per_row + (uint32_t)(int16_t)custom_get((p & 1) ? 0x10A : 0x108);
    }   /* piani 1,3,5 (indice pari) -> BPL1MOD; 2,4,6 -> BPL2MOD */
    DPROF(DP_FETCH)

    /* BPLCON1: scroll orizzontale fine (PF1 dispari, PF2 pari) ; portage VGA32 : en pixels
     * lores, donc 2 pixels par pas en hires */
    uint16_t bplcon1 = custom_get(0x102);
    int scroll_pf1 = bplcon1 & 0x0F;
    int scroll_pf2 = (bplcon1 >> 4) & 0x0F;
    if (hires) { scroll_pf1 *= 2; scroll_pf2 *= 2; }

    /* Portage VGA32 : modes et priorités. Double playfield (BPLCON0 bit 10) et PF2PRI : table de
     * palette ; HAM (bit 11, lores) et sprites derrière le playfield (BPLCON2) : index par pixel. */
    const uint16_t bplcon2 = custom_get(0x104);
    const int dpf = (bplcon0 >> 10) & 1;
    const int ham = !hires && !dpf && ((bplcon0 >> 11) & 1);
    const int pf2pri = (bplcon2 >> 6) & 1;
    const int code1 = (bplcon2 & 7) > 4 ? 4 : (bplcon2 & 7);
    const int code2 = ((bplcon2 >> 3) & 7) > 4 ? 4 : ((bplcon2 >> 3) & 7);
    const int code_min = dpf && code1 < code2 ? code1 : code2;
    int spr_on = 0, need_prio = 0;
    if ((dmacon & 0x0200) && (dmacon & 0x0020))
        for (int i = 0; i < 8; i++)
            if (spr[i].state == SPR_ACTIVE) { spr_on = 1; if ((i >> 1) >= code_min) need_prio = 1; }

#ifdef ARDUINO
    /* SKIP sui DATI (non sui puntatori: quelli cambiano ogni frame nel Workbench).
       Confronto i bitplane grezzi + palette con il frame precedente. Se identici,
       i pixel saranno identici -> salto calcolo e disegno.
       Portage VGA32 : la clé compte aussi BPLCON0-2 et la fenêtre horizontale, et une ligne
       où un sprite était dessiné est redessinée (sinon le pointeur laisse une traînée). */
    // buffer snapshot in PSRAM (156KB: troppi per la DRAM interna)
    static uint8_t  (*prev_line)[6][LINE_PRE + 80] = nullptr;
    static uint16_t (*prev_pal)[64] = nullptr;
    static uint8_t  prev_np[FB_H];
    static bool     prev_valid[FB_H] = {false};
    static bool     prev_spr[FB_H];
    static uint64_t prev_key[FB_H];
    extern bool g_row_onscreen[FB_H];
    if (!prev_line) {
        prev_line = (uint8_t(*)[6][LINE_PRE + 80]) heap_caps_malloc(FB_H*6*(LINE_PRE + 80), MALLOC_CAP_SPIRAM);
        prev_pal  = (uint16_t(*)[64])   heap_caps_malloc(FB_H*64*2, MALLOC_CAP_SPIRAM);
        if (!prev_line || !prev_pal) { return; }  // fallback: se PSRAM esaurita, non skippo
    }

    const uint64_t key = (uint64_t)bplcon0 << 48 | (uint64_t)bplcon1 << 32 | (uint64_t)bplcon2 << 16 |
                         (uint64_t)(diwstrt & 0xFF) << 8 | (diwstop & 0xFF);
    int sk_ncol = 1 << nplanes;
    if (sk_ncol > 64) sk_ncol = 64;
    bool data_same = prev_valid[vpos] && prev_np[vpos] == nplanes && g_row_onscreen[vpos] &&
                     !ev && !spr_on && !prev_spr[vpos] && prev_key[vpos] == key;
    if (data_same)
        for (int p = 0; p < nplanes; p++)
            if (memcmp(prev_line[vpos][p], line[p] - LINE_PRE, LINE_PRE + bytes_per_row) != 0) { data_same = false; break; }
    if (data_same)
        for (int i = 0; i < sk_ncol; i++)
            if (prev_pal[vpos][i] != palette_entry_12(i)) { data_same = false; break; }

    DPROF(DP_SKIPCHK)
#if defined(VGA32_PROF) && VGA32_PROF
    if (data_same) { DPROF_LINE(DL_SKIPPED) return; }
#else
    if (data_same) return;   // riga identica e gia' sul display: niente da fare
#endif

    // dati cambiati: salvo lo snapshot per il prossimo frame e procedo a disegnare
    prev_valid[vpos] = !ev;   /* écritures du copper en cours de ligne : pas de snapshot */
    prev_np[vpos] = nplanes;
    prev_spr[vpos] = spr_on;
    prev_key[vpos] = key;
    for (int p = 0; p < nplanes; p++) memcpy(prev_line[vpos][p], line[p] - LINE_PRE, LINE_PRE + bytes_per_row);
    for (int i = 0; i < sk_ncol; i++) prev_pal[vpos][i] = palette_entry_12(i);
    g_row_onscreen[vpos] = false;   // sara' ri-confermata dal dirty-check nel callback
    DPROF(DP_SNAP)
#else
    (void)spr_on;
#endif

    /* palette precalcolata fuori dal loop: le tinte non cambiano pixel-per-pixel.
       Un accesso array invece di custom_get() 640 volte. */
    uint16_t pal[64];
    int ncol = 1 << nplanes;
    if (ncol > 64) ncol = 64;
    build_pal(pal, ncol, dpf, pf2pri);
    DPROF(DP_PAL)
#ifdef ARDUINO
    color_diag_last_bplcon0 = bplcon0;
    color_diag_last_nplanes = nplanes;
#endif

    /* Boucle commune cible + harnais PC (tests/pc) : le PC exécute le même code que la carte.
       Arduino: in lores calcolo 320 pixel UNA volta (niente raddoppio: denise_cb
       decima comunque). In hires i 640 pixel servono tutti. Meta' del lavoro. */
    int outpx = hires ? 640 : 320;
    /* Portage VGA32 : même résultat que le calcul bit par bit d'origine (pixel x = bits des
     * plans au rang x - scroll), mais par groupes de 8 pixels : plans décalés une fois par
     * ligne, tables c2p, 2 pixels par écriture 32 bits ; le maximum d'index du diagnostic
     * couleur est tenu en registre et écrit une fois par ligne. */
    c2p_init();
    uint8_t shifted[6][80];
    const uint8_t *pl[6];
    for (int p = 0; p < nplanes; p++) {
        const int sc = (p & 1) ? scroll_pf2 : scroll_pf1;
        if (sc) { plane_shift(shifted[p], line[p], bytes_per_row, sc); pl[p] = shifted[p]; }
        else    pl[p] = line[p];
    }
    const int need_idx = ham || ev || need_prio;
    uint8_t front[64];
    alignas(4) uint8_t idx[640];
    if (!need_idx) {
        uint8_t maxidx = 0;
        fb_word_t *out = (fb_word_t *)row;
        for (int i = 0; i < outpx / 8; i++) {
            uint32_t h = 0, l = 0;
            for (int p = 0; p < nplanes; p++) {
                const uint8_t b = pl[p][i];
                h |= c2p_hi[b] << p;
                l |= c2p_lo[b] << p;
            }
            const uint32_t m = h | l;                 /* maximum exact seulement si nécessaire */
            if (m) for (int k = 0; k < 4; k++) {
                const uint8_t a = (uint8_t)(h >> (8 * k)), c = (uint8_t)(l >> (8 * k));
                if (a > maxidx) maxidx = a;
                if (c > maxidx) maxidx = c;
            }
            out[4 * i]     = pal[h & 0xFF]         | (uint32_t)pal[(h >> 8) & 0xFF] << 16;
            out[4 * i + 1] = pal[(h >> 16) & 0xFF] | (uint32_t)pal[h >> 24] << 16;
            out[4 * i + 2] = pal[l & 0xFF]         | (uint32_t)pal[(l >> 8) & 0xFF] << 16;
            out[4 * i + 3] = pal[(l >> 16) & 0xFF] | (uint32_t)pal[l >> 24] << 16;
        }
#ifdef ARDUINO
        if (maxidx > color_diag_max_idx) color_diag_max_idx = maxidx;
#else
        (void)maxidx;
#endif
    } else {
        /* Portage VGA32 : index par pixel, puis couleurs par segments entre les écritures du
         * copper (palette recalculée à chaque segment). HAM : 00 = COLOR(0-15), 01 = bleu,
         * 10 = rouge, 11 = vert modifiés sur la couleur du pixel précédent (COLOR00 au départ). */
        uint32_t *iw = (uint32_t *)idx;
        for (int i = 0; i < outpx / 8; i++) {
            uint32_t h = 0, l = 0;
            for (int p = 0; p < nplanes; p++) {
                const uint8_t b = pl[p][i];
                h |= c2p_hi[b] << p;
                l |= c2p_lo[b] << p;
            }
            iw[2 * i] = h;
            iw[2 * i + 1] = l;
        }
        const int scale = hires ? 2 : 1;
        uint16_t hold = bg & 0xFFF;
        int x = 0;
        while (x < outpx) {
            int end = outpx;
            if (cop_ev_visible() && cop_ev[cop_ev_done].x * scale < end) end = cop_ev[cop_ev_done].x * scale;
            if (x > 0) build_pal(pal, ham ? 16 : ncol, dpf, pf2pri);
            if (ham) {
                for (; x < end; x++) {
                    const int v = idx[x], d = v & 15;
                    switch (v >> 4) {
                    case 0: hold = pal[d]; break;
                    case 1: hold = (uint16_t)((hold & 0xFF0) | d); break;
                    case 2: hold = (uint16_t)((hold & 0x0FF) | d << 8); break;
                    default: hold = (uint16_t)((hold & 0xF0F) | d << 4); break;
                    }
                    row[x] = hold;
                }
            } else {
                for (; x < end; x++) row[x] = pal[idx[x]];
            }
            if (end < outpx) cop_ev_apply(cop_ev[cop_ev_done].x);
        }
        if (need_prio)
            for (int i = 0; i < 64; i++) {
                int code = 4;
                if (!dpf) { if (i) code = code2; }
                else if (i & 0x3F) {
                    const int o1 = i & 0x15, o2 = i & 0x2A;
                    code = (o2 && (pf2pri || !o1)) ? code2 : code1;
                }
                front[i] = (uint8_t)((1 << code) - 1);
            }
    }
    DPROF(DP_PIXELS)
    sprite_overlay(row, !hires, need_prio ? idx : nullptr, front);   /* sprite sopra il playfield */
    diw_h_mask(row, outpx, hires ? 2 : 1);
    DPROF(DP_SPRITES)
#ifdef ARDUINO
    if (denise_line_cb) denise_line_cb(vpos, row, outpx);
    DPROF(DP_CB)
    DPROF_LINE(DL_DRAWN)
    return;
#else
    /* fb[] fait 640 colonnes : en lores chaque pixel est doublé, de droite à gauche
       pour ne pas écraser la source (2x >= x). */
    if (!hires)
        for (int x = pixels - 1; x >= 0; x--) row[2 * x + 1] = row[2 * x] = row[x];
#endif
}

#ifndef ARDUINO
int video_write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) {
            uint16_t c = fb[y][x];
            uint8_t rgb[3] = { (uint8_t)(((c >> 8) & 15) * 17),
                               (uint8_t)(((c >> 4) & 15) * 17),
                               (uint8_t)((c & 15) * 17) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return 0;
}
#endif
