/* Disco: codifica MFM AmigaDOS + DMA verso chip RAM + interrupt DSKBLK.
 * Ground truth: HRM app. C + formato settore AmigaDOS standard:
 *   per settore (1088 byte MFM):
 *     4 byte pre-gap (0xAA), 2 word sync 4489,
 *     info (FF,track,sector,fino_al_gap) odd/even,
 *     label 16 byte odd/even, checksum header, checksum dati,
 *     512 byte dati: 256 word dispari poi 256 pari.
 *   checksum = XOR dei longword MFM & 0x55555555 sulla regione.
 *   clock MFM: bit di clock a 1 solo tra due bit dati a 0.
 * DMA: DSKLEN va scritto DUE volte con bit15 per partire (sicurezza HW).
 *   Lettura: riempie DSKPT con len word dal flusso traccia (rivoluzione
 *   ripetuta ciclicamente). A fine trasferimento: DSKBLK (INTREQ bit 1),
 *   differito alla scanline successiva (stessa lezione del blitter).
 */
#include "a500.h"
#include <string.h>
#include <stdio.h>

#define SEC_MFM   1088
#define NSEC      11
#define GAP_BYTES 830                 /* riempitivo 0xAA tra fine e inizio */
#define REV_BYTES (NSEC * SEC_MFM + GAP_BYTES)

int disk_irq_pending = 0;

static uint8_t rev[REV_BYTES];        /* una rivoluzione MFM della traccia */
static int rev_track = -1;            /* traccia attualmente codificata    */

/* Portage VGA32 : rotation continue du disque (docs/FLOPPY_SPEC.md §A1-A3). Cellule MFM de
 * 7 CCK, ligne PAL de 227,5 CCK : 32,5 bits = 65/32 mots par ligne. Le temps émulé est
 * cur_frame * 312 + vpos lignes ; la rotation ne s'arrête pas avec le moteur (simplification
 * sans effet observable : la position de départ d'une lecture reste quelconque). */
#define REV_WORDS (REV_BYTES / 2)
static_assert(REV_BYTES % 2 == 0, "revolution MFM non alignee sur les mots");
static uint32_t head_word(void)
{
    const uint64_t lines = (uint64_t)(uint32_t)cur_frame * 312u + (uint32_t)vpos;
    return (uint32_t)((lines * 65u / 32u) % REV_WORDS);
}
static inline uint16_t rev_word(uint32_t w) { return (uint16_t)(rev[2 * w] << 8 | rev[2 * w + 1]); }

/* ---- encoder MFM a livello di bit ---- */
static uint8_t *mp;                   /* puntatore di scrittura nel buffer */
static uint32_t mbits;                /* accumulatore bit                  */
static int      mn;                   /* bit accumulati                    */
static int      mprev;                /* ultimo bit dati emesso            */

static void mfm_reset(uint8_t *dst) { mp = dst; mbits = 0; mn = 0; mprev = 0; }

static void mfm_bit(int b)            /* emette clock+dato */
{
    int clock = (!mprev && !b) ? 1 : 0;
    mbits = (mbits << 2) | (uint32_t)(clock << 1) | (uint32_t)b;
    mprev = b;
    mn += 2;
    while (mn >= 8) {
        mn -= 8;
        *mp++ = (uint8_t)(mbits >> mn);
    }
}

static void mfm_raw16(uint16_t w)     /* word letterale (sync 4489) */
{
    for (int i = 15; i >= 0; i--) {
        mbits = (mbits << 1) | ((w >> i) & 1);
        mn++;
        while (mn >= 8) { mn -= 8; *mp++ = (uint8_t)(mbits >> mn); }
    }
    mprev = w & 1;
}

/* emette i bit dispari (7,5,3,1) poi pari (6,4,2,0) di un blocco (AmigaDOS) */
static void mfm_oddeven(const uint8_t *d, int len)
{
    for (int half = 0; half < 2; half++)          /* 0 = dispari, 1 = pari */
        for (int i = 0; i < len; i++)
            for (int b = 7 - half; b >= 0; b -= 2)
                mfm_bit((d[i] >> b) & 1);
}

/* checksum AmigaDOS: XOR dei longword MFM & 0x55555555 su [from, to) */
static uint32_t mfm_cksum(const uint8_t *from, const uint8_t *to)
{
    uint32_t c = 0;
    for (const uint8_t *p = from; p < to; p += 4)
        c ^= ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
              (uint32_t)p[2] << 8  |  p[3]) & 0x55555555u;
    return c;
}

static void enc_long(uint32_t v)      /* long odd/even (per i checksum) */
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                     (uint8_t)(v >> 8),  (uint8_t)v };
    mfm_oddeven(b, 4);
}

static void build_revolution(int track)
{
    const uint8_t *adf = drive_adf();
    memset(rev, 0, sizeof rev);
    mfm_reset(rev);
    for (int s = 0; s < NSEC; s++) {
        mfm_bit(0); mfm_bit(0); mfm_bit(0); mfm_bit(0);   /* pre-gap:      */
        mfm_bit(0); mfm_bit(0); mfm_bit(0); mfm_bit(0);   /* 0x00 -> 0xAA  */
        mfm_bit(0); mfm_bit(0); mfm_bit(0); mfm_bit(0);
        mfm_bit(0); mfm_bit(0); mfm_bit(0); mfm_bit(0);
        mfm_raw16(0x4489); mfm_raw16(0x4489);
        uint8_t *hdr_start = mp;
        uint8_t info[4] = { 0xFF, (uint8_t)track, (uint8_t)s, (uint8_t)(NSEC - s) };
        mfm_oddeven(info, 4);
        uint8_t label[16] = { 0 };
        mfm_oddeven(label, 16);
        uint8_t *hdr_end = mp;                            /* info+label = 40 byte MFM */
        uint32_t hck = mfm_cksum(hdr_start, hdr_end);
        enc_long(hck);
        uint8_t *dck_pos = mp;                            /* placeholder checksum dati */
        enc_long(0);
        uint8_t *data_start = mp;
        mfm_oddeven(adf + ((uint32_t)track * NSEC + (uint32_t)s) * 512, 512);
        uint32_t dck = mfm_cksum(data_start, mp);
        uint8_t *save = mp; int sn = mn, sp = mprev;      /* riscrivo il checksum */
        mfm_reset(dck_pos); mprev = 1;                    /* prev = ultimo bit hck */
        enc_long(dck);
        mp = save; mn = sn; mprev = sp;
    }
    while (mp < rev + REV_BYTES) *mp++ = 0xAA;            /* gap finale    */
    rev_track = track;
}

/* dump di una rivoluzione codificata (per la validazione offline) */
#ifndef ARDUINO
int disk_mfmdump(int track, const char *path)
{
    if (!drive_adf()) { logmsg("[DSK] mfmdump: nessun disco\n"); return -1; }
    build_revolution(track);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(rev, 1, REV_BYTES, f);
    fclose(f);
    logmsg("[DSK] rivoluzione traccia %d scritta in %s (%d byte)\n", track, path, REV_BYTES);
    return 0;
}
#endif

/* chiamata dal latch DSKLEN in custom.cpp */
void disk_dsklen_write(uint16_t v)
{
    static int armed = 0;
    if (!(v & 0x8000)) { armed = 0; return; }
    if (!armed) { armed = 1; return; }
    armed = 0;

    int len = v & 0x3FFF;                                 /* in word       */
    uint32_t pt = (((uint32_t)custom_get(0x020) << 16) | custom_get(0x022)) & 0x7FFFE;
    if (v & 0x4000) {                                     /* scrittura: WP */
        logmsg("[DSK] scrittura richiesta ignorata (disco write-protected)\n");
        disk_irq_pending = 1;
        return;
    }
    if (!drive_adf() || !drive_motor_on()) {
        logmsg("[DSK] lettura senza disco/motore: nessun dato\n");
        return;
    }
    int track = drive_track() * 2 + drive_side();
    if (rev_track != track) build_revolution(track);
    /* Portage VGA32 (docs/FLOPPY_SPEC.md §A3, B5, B6) : la lecture part du mot sous la tête,
     * pas du début de la piste ; avec WORDSYNC, elle commence juste après le prochain mot
     * DSKSYNC (non stocké). Pas de sync sur la piste : le DMA ne finit jamais, pas de DSKBLK. */
    uint32_t w = head_word();
    if (adkcon & 0x0400) {                                /* ADKCON WORDSYNC */
        const uint16_t sync = custom_get(0x07E);
        uint32_t i = 1;                                   /* un sync déjà présent ne compte pas */
        while (i <= REV_WORDS && rev_word((w + i) % REV_WORDS) != sync) i++;
        if (i > REV_WORDS) {
            logmsg("[DSK] WORDSYNC : sync %04X absent de la piste %d, DMA en attente\n", sync, track);
            return;
        }
        w = (w + i + 1) % REV_WORDS;
    }
    for (int k = 0; k < len; k++) {
        const uint32_t s = (w + (uint32_t)k) % REV_WORDS;
        chip_ram[(pt + 2u * (uint32_t)k) & 0x7FFFF]      = rev[2 * s];
        chip_ram[(pt + 2u * (uint32_t)k + 1u) & 0x7FFFF] = rev[2 * s + 1];
    }
    logmsg("[DSK] lettura traccia %d (cil %d lato %d): %d word -> %05X, DSKBLK\n",
           track, drive_track(), drive_side(), len, pt);
    disk_irq_pending = 1;                                 /* DSKBLK differito */
}
