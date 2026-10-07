/* test_disk_c.cpp — Tests de la lecture DMA disque (core/disk.cpp) avec des stubs à la place
 * du lecteur, de Agnus et de la chip RAM : position de rotation et WORDSYNC
 * (docs/FLOPPY_SPEC.md §A3, B5, B6, F1). */
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
const uint8_t *drive_adf(void) { return s_adf; }
int drive_track(void) { return 0; }
int drive_side(void) { return 0; }
int drive_motor_on(void) { return 1; }
void logmsg(const char *, ...) {}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } \
                      else printf("OK   %s\n", #c); } while (0)

static uint16_t w16(uint32_t a) { return (uint16_t)(chip_ram[a] << 8 | chip_ram[a + 1]); }
static uint32_t l32(uint32_t a) { return (uint32_t)w16(a) << 16 | w16(a + 2); }

/* lit len mots à DSKPT = 0 au temps (frame, ligne) ; rend 1 si DSKBLK est levé */
static int read_at(int frame, int line, uint16_t len)
{
    memset(chip_ram, 0, 0x10000);
    cur_frame = frame; vpos = line;
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

    printf("disk : %s\n", fails ? "ECHEC" : "OK");
    return fails ? 1 : 0;
}
