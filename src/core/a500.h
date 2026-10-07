/* a500_phase0 — Fase 0: boot Kickstart 1.3 fino a "insert disk"
 * Validazione offline su PC. Ground truth: MAME (driver a500) + HRM.
 * CPU: Musashi (stesso core di MAME).
 */
#ifndef A500_H
#define A500_H

#include <stdint.h>
#include <stdio.h>
#ifdef ARDUINO
// su Arduino i log diagnostici file-based sono disattivati: rdlogf/blitlogf/tracef
// restano NULL, i blocchi if(...) sono sempre falsi. fprintf su di essi mai eseguito.
#endif

/* ---- memoria ---- */
#define CHIP_SIZE 0x80000
#define SLOW_BASE 0xC00000
#define SLOW_SIZE 0x80000
#define ROM_SIZE  0x40000   /* 256KB Kickstart 1.3 @ 0xFC0000 (mirror 0xF80000) */

#ifdef ARDUINO
extern uint8_t *chip_ram;   // PSRAM
extern uint8_t *kick_rom;   // PSRAM
#else
extern uint8_t chip_ram[CHIP_SIZE];
extern uint8_t kick_rom[ROM_SIZE];
#endif
extern int     ovl;         /* 1 = ROM overlay a 0x000000 (stato di reset) */

int  mem_load_rom(const char *path);
void mem_reset(void);

/* ---- CIA 8520 ---- */
typedef struct {
    uint8_t  pra, prb, ddra, ddrb;
    uint16_t ta, tb, ta_latch, tb_latch;
    uint8_t  cra, crb;
    uint8_t  icr_data, icr_mask;
    uint8_t  sdr;              /* registre serie (clavier Amiga, portage VGA32) */
    uint32_t tod, tod_latch, tod_alarm;
    int      tod_latched, tod_running;
    const char *name;
    int      irq_intreq_bit;   /* 3 = PORTS (CIA-A), 13 = EXTER (CIA-B) */
} cia_t;

extern cia_t cia_a, cia_b;

void    cia_reset(void);
uint8_t cia_read (cia_t *c, int reg);
void    cia_write(cia_t *c, int reg, uint8_t v);
void    cia_tick (int eclocks);      /* E-clock = CPU/10 */
#ifdef ARDUINO
void    cia_a_kbd_shift_in(uint8_t sdr_value);  /* clavier Amiga -> SP CIA-A (portage VGA32) */
#endif

/* ---- chipset custom (0xDFF000) ---- */
extern uint16_t intena, intreq, dmacon, adkcon;
extern int      vpos, hpos;          /* beam counter PAL: 312 linee */

uint16_t custom_read (uint32_t off);          /* off = addr & 0x1FE */
void     custom_write(uint32_t off, uint16_t v);
void     custom_reset(void);
uint16_t custom_get  (uint32_t off);          /* latch senza effetti collaterali */
void     custom_set_latch(uint32_t off, uint16_t v);  /* writeback puntatori blitter */
void     blitter_do(uint16_t bltsize);        /* esegue un blit (area o line) */
extern int blit_irq_pending;                  /* done-int differito di una linea */
long long blitter_ns(void);                   /* tempo cumulato nel blitter (benchmark) */
void     irq_update(void);           /* ricalcola IPL verso Musashi */
void     intreq_set(int bit);        /* usato da CIA/vblank */

extern uint32_t bpl_pt[6];           /* puntatori bitplane live */
extern uint32_t blit_count;          /* blit totali richiesti (diagnostica) */
extern FILE *blitlogf;               /* log completo blit (--blitlog) */
extern FILE *rdlogf;                 /* log letture registri (--rdlog) */
extern int   cur_frame;              /* frame corrente (per il blitlog) */

/* ---- copper + denise (video.cpp) ---- */
void copper_vblank(void);
void copper_jmp(int which);
void copper_run_line(void);
void denise_render_line(void);
void paula_reset(void);
void paula_step(int colorclocks);
void paula_write(uint32_t off, uint16_t v);
void paula_set_dma(uint16_t dmacon);
int  paula_ring_pop(int16_t *l, int16_t *r);
void sprite_arm(int n);
void sprite_vblank(void);
#ifdef ARDUINO
extern void (*denise_line_cb)(int vpos, const uint16_t *pixels, int w);
#if defined(VGA32_PROF) && VGA32_PROF
/* portage VGA32 : profiling interne de denise_render_line (cycles et lignes par sortie) */
enum { DP_SPRDMA, DP_BGFILL, DP_WIN, DP_FETCH, DP_SKIPCHK, DP_SNAP, DP_PAL, DP_PIXELS,
       DP_SPRITES, DP_CB, DP_N };
enum { DL_BORDER, DL_NOPLANES, DL_SKIPPED, DL_DRAWN, DL_N };
extern uint32_t denise_prof_cyc[DP_N];
extern uint32_t denise_prof_lines[DL_N];
#endif
#else
int  paula_write_wav(const char *path);   /* harnais PC (tests/pc) */
void cia_b_dump(void);
#endif
int  video_write_ppm(const char *path);

/* ---- drive DF0 (fase 0: presente, senza disco) ---- */
void    drive_prb_write(uint8_t prb);   /* agganciato a CIA-B PRB */
int     drive_insert_adf(const char *path);
#ifdef ARDUINO
int     drive_set_adf(const uint8_t *data, uint32_t len);
uint8_t *drive_alloc_adf(void);
void    drive_mount_ready(void);
void    drive_eject(void);
#endif
int     drive_track(void);
int     drive_side(void);
int     drive_motor_on(void);
const uint8_t *drive_adf(void);

/* ---- disco: MFM + DMA (disk.cpp) ---- */
void disk_dsklen_write(uint16_t v);
extern int disk_irq_pending;         /* DSKBLK differito di una linea */
int  disk_mfmdump(int track, const char *path);

/* ---- mouse porta 0 (input.cpp) ---- */
int      input_load_script(const char *path);
void     input_frame(int frame);
uint16_t input_joy0dat(void);
uint16_t input_joy1dat(void);
void     input_set_joy(int up, int down, int left, int right, int fire);
int      input_joy_fire(void);
void     input_set_lmb(int v);
void     input_set_rmb(int v);
int      input_lmb(void);
int      input_rmb(void);
#ifdef ARDUINO
void     input_mouse_delta(int dx, int dy);   /* portage VGA32 : souris PS/2 */
#endif
uint8_t drive_pra_bits(void);           /* bit 2-5 di CIA-A PRA   */

/* ---- portage VGA32 : écriture d'un mot gros-boutiste (ordre Amiga) en une seule écriture ----
 * Sur la cible, chaque écriture est suivie d'un memw (-mfix-esp32-psram-cache-issue) : une
 * écriture 16 bits au lieu de 2 octets divise ce coût par deux. p doit être pair (adresses
 * Amiga paires, bases de RAM alignées). may_alias : accès légal à un tableau d'octets. */
typedef uint16_t __attribute__((may_alias)) amiga_u16_t;
static inline void wr_be16(uint8_t *p, uint16_t v) { *(amiga_u16_t *)p = __builtin_bswap16(v); }

/* ---- log & trace ---- */
extern FILE *tracef;
void logmsg(const char *fmt, ...);

#endif
