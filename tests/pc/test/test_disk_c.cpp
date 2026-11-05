/* test_disk_c.cpp — Tests du DMA disque (core/disk.cpp) avec des stubs à la place du lecteur,
 * de Agnus et de la chip RAM : position de rotation et WORDSYNC (docs/FLOPPY_SPEC.md §A3, A4,
 * B5, B6, B12, F1) ; écriture d'une piste et reconversion en ADF (§E), avec un encodeur MFM
 * propre au test ; interruption DSKSYN (§B7) ; cylindres 80-83 au-delà de l'ADF (§D6). */
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "a500.h"

/* ---- stubs ---- */
uint8_t chip_ram[CHIP_SIZE];
uint16_t intena, intreq, dmacon, adkcon;
int vpos, hpos, cur_frame;
static uint16_t s_regs[0x100];
uint16_t custom_get(uint32_t off) { return s_regs[off >> 1]; }
static uint8_t s_adf[901120];
static int s_nodisk = 0;
const uint8_t *drive_adf(void) { return s_nodisk ? nullptr : s_adf; }
static int s_wp = 0;
uint8_t *drive_adf_rw(void) { return s_nodisk || s_wp ? nullptr : s_adf; }
int drive_write_protected(void) { return s_wp; }
static int s_cyl = 0;
int drive_track(void) { return s_cyl; }
int drive_side(void) { return 0; }
static int s_motor = 1, s_sel = 1, s_index = 0;
int drive_motor_on(void) { return s_motor; }
int drive_selected(void) { return s_sel; }
void cia_b_index_pulse(void) { s_index++; }
void logmsg(const char *, ...) {}
static int s_dsksyn = 0;
void intreq_set(int bit) { intreq |= (uint16_t)(1 << bit); if (bit == 12) s_dsksyn++; }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } \
                      else printf("OK   %s\n", #c); } while (0)

static uint16_t w16(uint32_t a) { return (uint16_t)(chip_ram[a] << 8 | chip_ram[a + 1]); }
static uint32_t l32(uint32_t a) { return (uint32_t)w16(a) << 16 | w16(a + 2); }

/* avance la rotation jusqu'au temps (frame, ligne) depuis le mot 0 (trames de 313 lignes) */
static void seek_time(int frame, int line)
{
    disk_rotation_reset();
    for (int i = 0; i < frame * 313 + line; i++) disk_line_tick();
}

/* lit len mots à DSKPT = 0 au temps (frame, ligne) ; rend 1 si DSKBLK est levé */
static int read_at(int frame, int line, uint16_t len)
{
    memset(chip_ram, 0, 0x10000);
    seek_time(frame, line);
    disk_irq_pending = 0;
    disk_dsklen_write(0x8000 | len);
    disk_dsklen_write(0x8000 | len);
    return disk_irq_pending;
}

/* secteur dont l'en-tête commence en chip_ram[0] (premier mot = 2e 4489) */
static int sector_after_sync(void)
{
    uint32_t info = ((l32(2) & 0x55555555u) << 1) | (l32(6) & 0x55555555u);
    if ((info >> 24) != 0xFF || ((info >> 16) & 0xFF) != 0) return -1;   /* format, piste 0 */
    return (int)((info >> 8) & 0xFF);
}

/* ---- encodeur MFM AmigaDOS du test (indépendant de disk.cpp) ---- */
static uint16_t s_trk[8000];
static int s_nw, s_nbit, s_prev;
static uint16_t s_acc;
static void emit(int bit)
{
    s_acc = (uint16_t)(s_acc << 1 | bit);
    if (++s_nbit == 16) { s_trk[s_nw++] = s_acc; s_nbit = 0; }
}
static void dbit(int b) { emit(!s_prev && !b); emit(b); s_prev = b; }
static void raw(uint16_t w) { for (int i = 15; i >= 0; i--) emit((w >> i) & 1); s_prev = w & 1; }
static void oddeven(const uint8_t *d, int n)
{
    for (int half = 0; half < 2; half++)
        for (int i = 0; i < n; i++)
            for (int b = 7 - half; b >= 0; b -= 2) dbit((d[i] >> b) & 1);
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t ck(const uint8_t *d, int n)            /* XOR des longs MFM masqués = données seules */
{
    uint32_t c = 0;
    for (int i = 0; i < n; i += 4) {
        const uint32_t v = (uint32_t)d[i] << 24 | d[i + 1] << 16 | d[i + 2] << 8 | d[i + 3];
        c ^= ((v >> 1) ^ v) & 0x55555555u;
    }
    return c;
}
/* piste façon trackdisk : gap de 830 mots $AAAA, 11 secteurs à partir de first, 1 mot de fin */
static void build_track(int track, int first, const uint8_t (*data)[512], int corrupt_sector)
{
    s_nw = s_nbit = s_prev = 0;
    for (int i = 0; i < 830; i++) raw(0xAAAA);
    for (int n = 0; n < 11; n++) {
        const int s = (first + n) % 11;
        uint8_t z[2] = { 0, 0 };
        oddeven(z, 2);                                     /* prégap -> AAAA AAAA */
        raw(0x4489); raw(0x4489);
        uint8_t hdr[20] = { 0xFF, (uint8_t)track, (uint8_t)s, (uint8_t)(11 - n) };
        uint8_t c[4];
        oddeven(hdr, 4); oddeven(hdr + 4, 16);
        be32(c, ck(hdr, 20)); oddeven(c, 4);
        be32(c, ck(data[s], 512)); oddeven(c, 4);
        oddeven(data[s], 512);
        if (s == corrupt_sector) s_trk[s_nw - 3] ^= 0x0100;
    }
    raw(0xAAAA);
}
static void write_track(int frame, int line)
{
    for (int i = 0; i < s_nw; i++) { chip_ram[0x20000 + 2 * i] = (uint8_t)(s_trk[i] >> 8); chip_ram[0x20001 + 2 * i] = (uint8_t)s_trk[i]; }
    s_regs[0x020 >> 1] = 0x0002; s_regs[0x022 >> 1] = 0x0000;   /* DSKPT = $20000 */
    seek_time(frame, line);
    disk_irq_pending = 0;
    disk_dsklen_write((uint16_t)(0xC000 | s_nw));
    disk_dsklen_write((uint16_t)(0xC000 | s_nw));
    s_regs[0x020 >> 1] = 0; s_regs[0x022 >> 1] = 0;
}

int main(void)
{
    for (uint32_t i = 0; i < sizeof s_adf; i++) s_adf[i] = (uint8_t)(i * 7);
    s_regs[0x07E >> 1] = 0x4489;                       /* DSKSYNC AmigaDOS */

    /* sans WORDSYNC : la lecture démarre à la position de rotation, donc deux instants
       différents donnent deux contenus différents (au temps 0 : prégap 0xAAAA) */
    adkcon = 0;
    CHECK(read_at(0, 0, 100) == 1);
    CHECK(w16(0) == 0xAAAA && w16(4) == 0x4489);
    uint8_t first[200]; memcpy(first, chip_ram, sizeof first);
    CHECK(read_at(0, 200, 100) == 1);
    CHECK(memcmp(first, chip_ram, sizeof first) != 0);

    /* avec WORDSYNC : premier mot stocké = 2e 4489, puis un en-tête de secteur valide */
    adkcon = 0x0400;
    CHECK(read_at(0, 0, 600) == 1);
    CHECK(w16(0) == 0x4489 && sector_after_sync() == 0);
    /* plus tard dans la révolution : un autre secteur (les loaders qui relisent un secteur
       en boucle voient défiler la piste) */
    CHECK(read_at(0, 300, 600) == 1);
    int s1 = sector_after_sync();
    CHECK(w16(0) == 0x4489 && s1 > 0 && s1 <= 10);
    CHECK(read_at(3, 0, 600) == 1);
    int s2 = sector_after_sync();
    CHECK(s2 >= 0 && s2 <= 10 && s2 != s1);

    /* WORDSYNC sur une valeur absente de la piste : rien n'est transféré, pas de DSKBLK */
    s_regs[0x07E >> 1] = 0x1234;
    CHECK(read_at(0, 0, 600) == 0);
    CHECK(w16(0) == 0 && w16(100) == 0);

    /* lecteur vide (§B12) : sans WORDSYNC le DMA finit (zéros écrits, DSKBLK) ; avec WORDSYNC
       aucun sync n'arrive, il ne finit jamais. Trackdisk 1.3 lit sans WORDSYNC et attend DSKBLK
       sans délai maximal : sans fin de DMA, une éjection pendant la lecture le bloque. */
    s_nodisk = 1;
    adkcon = 0;
    memset(chip_ram, 0xEE, 0x10000);
    seek_time(0, 0); disk_irq_pending = 0;
    disk_dsklen_write(0x8000 | 100); disk_dsklen_write(0x8000 | 100);
    CHECK(disk_irq_pending == 1);
    CHECK(w16(0) == 0 && w16(198) == 0 && w16(200) == 0xEEEE);
    adkcon = 0x0400;
    s_regs[0x07E >> 1] = 0x4489;
    CHECK(read_at(0, 0, 600) == 0);
    s_nodisk = 0;

    /* index (§A4) : une impulsion par révolution de 6 334 mots (3 118,2 lignes à 65/32 mots par
       ligne), seulement lecteur sélectionné, moteur en marche et disque présent */
    seek_time(0, 0); s_index = 0;
    for (int i = 0; i < 3118; i++) disk_line_tick();
    CHECK(s_index == 0);
    disk_line_tick();
    CHECK(s_index == 1);
    for (int i = 0; i < 10 * 3119; i++) disk_line_tick();
    CHECK(s_index == 11);
    s_index = 0;
    s_nodisk = 1; for (int i = 0; i < 3200; i++) disk_line_tick(); s_nodisk = 0;
    s_motor = 0;  for (int i = 0; i < 3200; i++) disk_line_tick(); s_motor = 1;
    s_sel = 0;    for (int i = 0; i < 3200; i++) disk_line_tick(); s_sel = 1;
    CHECK(s_index == 0);

    /* écriture (§E) : piste 0 réécrite (6 815 mots, premier secteur 3, position quelconque) ->
       ADF mis à jour, relecture du nouveau contenu */
    static uint8_t data[11][512];
    for (int s = 0; s < 11; s++) for (int k = 0; k < 512; k++) data[s][k] = (uint8_t)(s * 31 + k * 3 + 1);
    for (uint32_t i = 0; i < sizeof s_adf; i++) s_adf[i] = (uint8_t)(i * 7);
    disk_track_invalidate();
    adkcon = 0; s_regs[0x07E >> 1] = 0x4489;
    build_track(0, 3, data, -1);
    CHECK(s_nw == 6815);
    write_track(1, 37);
    CHECK(disk_irq_pending == 1);
    CHECK(memcmp(s_adf, data, sizeof data) == 0);
    CHECK(s_adf[sizeof data] == (uint8_t)(sizeof data * 7));   /* piste 1 intacte */
    adkcon = 0x0400;
    CHECK(read_at(2, 100, 600) == 1);
    const int sr = sector_after_sync();
    const uint32_t d0 = ((l32(58) & 0x55555555u) << 1) | (l32(58 + 512) & 0x55555555u);
    CHECK(sr >= 0 && sr <= 10 && d0 == ((uint32_t)data[sr < 0 ? 0 : sr][0] << 24 | data[sr < 0 ? 0 : sr][1] << 16 |
                                        data[sr < 0 ? 0 : sr][2] << 8 | data[sr < 0 ? 0 : sr][3]));

    /* tout ou rien : un secteur à la somme de contrôle fausse -> ADF inchangé */
    for (uint32_t i = 0; i < sizeof s_adf; i++) s_adf[i] = (uint8_t)(i * 7);
    disk_track_invalidate();
    adkcon = 0;
    for (int s = 0; s < 11; s++) data[s][0] ^= 0x5A;
    build_track(0, 0, data, 5);
    write_track(0, 0);
    CHECK(disk_irq_pending == 1);
    CHECK(s_adf[0] == 0 && s_adf[512] == (uint8_t)(512 * 7));

    /* disque protégé : rien n'est écrit, le DMA se termine */
    build_track(0, 0, data, -1);
    s_wp = 1;
    disk_track_invalidate();
    write_track(0, 0);
    CHECK(disk_irq_pending == 1);
    CHECK(s_adf[0] == 0);
    s_wp = 0;

    /* changement de disque : la piste MFM en cache est reconstruite depuis le nouvel ADF */
    adkcon = 0x0400;
    CHECK(read_at(0, 0, 600) == 1 && sector_after_sync() == 0);
    const uint32_t before = l32(58);
    for (uint32_t i = 0; i < sizeof s_adf; i++) s_adf[i] = (uint8_t)(i * 13 + 5);
    disk_track_invalidate();
    CHECK(read_at(0, 0, 600) == 1 && sector_after_sync() == 0 && l32(58) != before);

    /* DSKSYN (§B7) : levé au passage des syncs, sans DMA ; 11 secteurs = 11 paires de 4489, au
       plus une interruption par ligne -> entre 11 et 22 par révolution */
    adkcon = 0; s_regs[0x07E >> 1] = 0x4489;
    seek_time(0, 0); s_dsksyn = 0;
    for (int i = 0; i < 3119; i++) disk_line_tick();
    CHECK(s_dsksyn >= 11 && s_dsksyn <= 22);
    adkcon = 0x0200;                                   /* MSBSYNC : inhibé */
    s_dsksyn = 0; for (int i = 0; i < 3119; i++) disk_line_tick();
    CHECK(s_dsksyn == 0);
    adkcon = 0; s_motor = 0;
    s_dsksyn = 0; for (int i = 0; i < 3119; i++) disk_line_tick();
    CHECK(s_dsksyn == 0);
    s_motor = 1; s_regs[0x07E >> 1] = 0x1234;          /* sync absent de la piste */
    s_dsksyn = 0; for (int i = 0; i < 3119; i++) disk_line_tick();
    CHECK(s_dsksyn == 0);
    s_regs[0x07E >> 1] = 0x4489;

    /* cylindre 80 (§D6, trackdisk accepte TD_SEEK 901 120) : au-delà de l'ADF, piste sans flux ;
       une écriture n'y touche pas l'ADF */
    s_cyl = 80;
    disk_track_invalidate();
    adkcon = 0;
    memset(chip_ram, 0xEE, 0x10000);
    seek_time(0, 0); disk_irq_pending = 0;
    disk_dsklen_write(0x8000 | 100); disk_dsklen_write(0x8000 | 100);
    CHECK(disk_irq_pending == 1 && w16(0) == 0 && w16(198) == 0);
    adkcon = 0x0400;
    CHECK(read_at(0, 0, 600) == 0);
    static uint8_t adf_copy[901120];
    memcpy(adf_copy, s_adf, sizeof s_adf);
    adkcon = 0;
    build_track(160, 0, data, -1);
    write_track(0, 0);
    CHECK(disk_irq_pending == 1 && memcmp(adf_copy, s_adf, sizeof s_adf) == 0);
    s_cyl = 0;
    disk_track_invalidate();

    printf("disk : %s\n", fails ? "ECHEC" : "OK");
    return fails ? 1 : 0;
}
