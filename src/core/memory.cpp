/* Mappa memoria A500 (24 bit):
 *   0x000000-0x07FFFF  Chip RAM 512KB   (ROM in overlay finché OVL=1)
 *   0xBFD000-0xBFDF00  CIA-B (byte alto, indirizzi pari, reg = A8-A11)
 *   0xBFE001-0xBFEF01  CIA-A (byte basso, indirizzi dispari, reg = A8-A11)
 *   0xDFF000-0xDFF1FF  registri custom (word)
 *   0xF80000-0xFBFFFF  mirror Kickstart
 *   0xFC0000-0xFFFFFF  Kickstart 1.3 256KB (vettore reset -> 0xFC00D2)
 * OVL: CIA-A PA0. Al reset DDRA=0 -> pin in pull-up -> overlay attivo.
 * Il Kickstart imposta DDRA bit0=1 e PRA bit0=0 -> overlay rimosso.
 */
#include "a500.h"
#include "zorro.h"     /* portage VGA32 : Fast RAM Zorro II autoconfig (src/hal/zorro) */
#include <string.h>
#include <stdarg.h>
extern "C" {
#include "m68k.h"
}

#ifdef ARDUINO
uint8_t *chip_ram = nullptr;   // 512KB in PSRAM (allocata in setup)
uint8_t *slow_ram = nullptr;   // 512KB slow RAM @ 0xC00000 (PSRAM)
uint8_t *kick_rom = nullptr;   // 256KB in PSRAM (caricata in setup)
#else
uint8_t chip_ram[CHIP_SIZE] __attribute__((aligned(4)));   /* wr_be16 : écritures 16 bits */
static uint8_t slow_ram_pc[SLOW_SIZE];   /* harnais PC (tests/pc) */
uint8_t *slow_ram = slow_ram_pc;
uint8_t kick_rom[ROM_SIZE];
#endif
int ovl = 1;

FILE *tracef = NULL;

#ifdef ARDUINO
#include <Arduino.h>
void logmsg(const char *fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Serial.print(buf);
}
#else
void logmsg(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    if (tracef) { va_start(ap, fmt); vfprintf(tracef, fmt, ap); va_end(ap); }
}
#endif

#ifndef ARDUINO
int mem_load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { logmsg("ERRORE: impossibile aprire %s\n", path); return -1; }
    size_t n = fread(kick_rom, 1, ROM_SIZE, f);
    fclose(f);
    if (n != ROM_SIZE) { logmsg("ERRORE: dimensione ROM %zu != 262144\n", n); return -1; }
    return 0;
}
#endif

/* Portage VGA32 : table des pages de 64 Ko lisibles directement (chip RAM ou ROM en overlay,
 * slow RAM, ROM et son miroir) : une lecture 68000 y trouve sa mémoire en une consultation au
 * lieu d'une chaîne de comparaisons. À reconstruire à chaque changement d'overlay. La Fast RAM
 * (zorro_page), les CIA et les registres custom restent traités après, comme avant. */
static const uint8_t *rd_page[256];
void mem_map_update(void)
{
    for (int i = 0; i < 256; i++) rd_page[i] = nullptr;
    for (int p = 0; p < (int)(CHIP_SIZE >> 16); p++)
        rd_page[p] = ovl ? &kick_rom[(p << 16) & (ROM_SIZE - 1)] : &chip_ram[p << 16];
    if (slow_ram)
        for (int p = 0; p < (int)(SLOW_SIZE >> 16); p++) rd_page[(SLOW_BASE >> 16) + p] = &slow_ram[p << 16];
    for (int p = 0xF8; p < 0x100; p++) rd_page[p] = &kick_rom[(p << 16) & (ROM_SIZE - 1)];
}

void mem_reset(void)
{
    /* Su Arduino chip_ram e' un puntatore PSRAM: sizeof(chip_ram) resetterebbe
       solo 4/8 byte. Serve sempre la dimensione reale della Chip RAM. */
    if (chip_ram) memset(chip_ram, 0, CHIP_SIZE);
    if (slow_ram) memset(slow_ram, 0, SLOW_SIZE);
    zorro_reset();     /* cartes d'extension non configurées : le Kickstart les reconfigure */
    ovl = 1;
    mem_map_update();
}

/* log una-tantum per accessi a zone non mappate */
static void unmapped(const char *op, uint32_t a)
{
    static uint32_t seen[64]; static int nseen = 0;
    uint32_t page = a >> 16;
    for (int i = 0; i < nseen; i++) if (seen[i] == page) return;
    if (nseen < 64) seen[nseen++] = page;
    logmsg("[MEM] %s non mappato: %06X (PC=%06X)\n", op, a,
           m68k_get_reg(NULL, M68K_REG_PPC));
}

static uint8_t read8(uint32_t a)
{
    a &= 0xFFFFFF;
    { const uint8_t *r = rd_page[a >> 16]; if (r) return r[a & 0xFFFF]; }
    { const uint8_t *z = zorro_page[a >> 16]; if (z) return z[a & 0xFFFF]; }
    if ((a & 0xFFF000) == 0xBFE000 && (a & 1)) return cia_read(&cia_a, (a >> 8) & 0xF);
    if ((a & 0xFFF000) == 0xBFD000 && !(a & 1)) return cia_read(&cia_b, (a >> 8) & 0xF);
    if ((a & 0xFFF000) == 0xDFF000) {
        uint16_t w = custom_read(a & 0x1FE);
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if ((a & 0xFF0000) == ZORRO_CFG_BASE) { uint8_t v; if (zorro_cfg_read8(a, &v)) return v; }
    unmapped("R8", a);
    return 0;
}

static void write8(uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
    if (a < CHIP_SIZE) { if (!ovl) chip_ram[a] = v; return; }  /* scritture in overlay ignorate */
    if (slow_ram && a >= SLOW_BASE && a < SLOW_BASE + SLOW_SIZE) { slow_ram[a - SLOW_BASE] = v; return; }
    { uint8_t *z = zorro_page[a >> 16]; if (z) { z[a & 0xFFFF] = v; return; } }
    if ((a & 0xFFF000) == 0xBFE000 && (a & 1)) { cia_write(&cia_a, (a >> 8) & 0xF, v); return; }
    if ((a & 0xFFF000) == 0xBFD000 && !(a & 1)) { cia_write(&cia_b, (a >> 8) & 0xF, v); return; }
    if ((a & 0xFFF000) == 0xDFF000) {
        /* scrittura byte su registro word: replica sul bus (comportamento reale indefinito,
           qui replichiamo il byte — il KS non lo fa mai sui registri critici) */
        custom_write(a & 0x1FE, (uint16_t)v << 8 | v);
        return;
    }
    if (a >= 0xF80000) return;  /* ROM: scritture ignorate */
    if ((a & 0xFF0000) == ZORRO_CFG_BASE && zorro_cfg_write8(a, v)) return;
    unmapped("W8", a);
}

/* Portage VGA32 : profiling des accès mémoire du 68000 (env ttgo-vga32-prof). Les accesseurs
 * s'appellent alors *_impl et une enveloppe compte appels et cycles ; hors profiling, MEMFN(x)
 * vaut x et le code est inchangé. */
#if defined(ARDUINO) && defined(VGA32_PROF) && VGA32_PROF
#define MEM_PROF 1
#define MEMFN(name) name##_impl
#else
#define MEM_PROF 0
#define MEMFN(name) name
#endif
#if MEM_PROF
uint32_t m68k_mem_prof_n[MP_N], m68k_mem_prof_cyc[MP_N];
#endif

extern "C" {

unsigned int MEMFN(m68k_read_memory_8)(unsigned int a) { return read8(a); }

unsigned int MEMFN(m68k_read_memory_16)(unsigned int a)
{
    a &= 0xFFFFFE;
    {   /* chip/ROM/slow : a pair, donc a+1 reste dans la même page de 64 Ko */
        const uint8_t *r = rd_page[a >> 16];
        if (r) { r += a & 0xFFFF; return (unsigned)r[0] << 8 | r[1]; }
    }
    {   /* Fast RAM : a pair, donc a+1 reste dans la même page de 64 Ko */
        const uint8_t *z = zorro_page[a >> 16];
        if (z) { z += a & 0xFFFF; return (unsigned)z[0] << 8 | z[1]; }
    }
    if ((a & 0xFFF000) == 0xDFF000) return custom_read(a & 0x1FE);
    return (unsigned)read8(a) << 8 | read8(a + 1);
}

unsigned int MEMFN(m68k_read_memory_32)(unsigned int a)
{
    return MEMFN(m68k_read_memory_16)(a) << 16 | MEMFN(m68k_read_memory_16)(a + 2);
}

void MEMFN(m68k_write_memory_8)(unsigned int a, unsigned int v) { write8(a, (uint8_t)v); }

void MEMFN(m68k_write_memory_16)(unsigned int a, unsigned int v)
{
    a &= 0xFFFFFE;
    if (a < CHIP_SIZE) {
        if (!ovl) wr_be16(&chip_ram[a], (uint16_t)v);
        return;
    }
    {
        uint8_t *z = zorro_page[a >> 16];
        if (z) { wr_be16(z + (a & 0xFFFF), (uint16_t)v); return; }
    }
    if (slow_ram && a >= SLOW_BASE && a < SLOW_BASE + SLOW_SIZE) { wr_be16(&slow_ram[a - SLOW_BASE], (uint16_t)v); return; }
    if ((a & 0xFFF000) == 0xDFF000) { custom_write(a & 0x1FE, (uint16_t)v); return; }
    write8(a, (uint8_t)(v >> 8)); write8(a + 1, (uint8_t)v);
}

void MEMFN(m68k_write_memory_32)(unsigned int a, unsigned int v)
{
    MEMFN(m68k_write_memory_16)(a, v >> 16);
    MEMFN(m68k_write_memory_16)(a + 2, v & 0xFFFF);
}

#if MEM_PROF
static inline uint32_t mp_cc(void) { uint32_t c; __asm__ __volatile__("rsr %0, ccount" : "=a"(c)); return c; }
#define MP_READ(kind, sz) unsigned int m68k_read_memory_##sz(unsigned int a) { \
    uint32_t t = mp_cc(); unsigned int r = m68k_read_memory_##sz##_impl(a); \
    m68k_mem_prof_cyc[kind] += mp_cc() - t; m68k_mem_prof_n[kind]++; return r; }
#define MP_WRITE(kind, sz) void m68k_write_memory_##sz(unsigned int a, unsigned int v) { \
    uint32_t t = mp_cc(); m68k_write_memory_##sz##_impl(a, v); \
    m68k_mem_prof_cyc[kind] += mp_cc() - t; m68k_mem_prof_n[kind]++; }
MP_READ(MP_R8, 8)   MP_READ(MP_R16, 16)   MP_READ(MP_R32, 32)
MP_WRITE(MP_W8, 8)  MP_WRITE(MP_W16, 16)  MP_WRITE(MP_W32, 32)
#endif

/* letture per il disassembler (senza effetti collaterali sui CIA/custom) */
unsigned int m68k_read_disassembler_16(unsigned int a)
{
    a &= 0xFFFFFE;
    if (a < CHIP_SIZE && !ovl) return (unsigned)chip_ram[a] << 8 | chip_ram[a + 1];
    const uint8_t *p = &kick_rom[a & (ROM_SIZE - 1)];
    return (unsigned)p[0] << 8 | p[1];
}
unsigned int m68k_read_disassembler_32(unsigned int a)
{
    return m68k_read_disassembler_16(a) << 16 | m68k_read_disassembler_16(a + 2);
}

} /* extern C */
