/* zorro.cpp — voir zorro.h. */
#include "zorro.h"
#include <stddef.h>

/* Identité de la carte : identifiant de fabricant réservé aux réalisations amateurs (« hacker »),
 * produit 1. er_Type = carte Zorro II (bits 7-6 = 11) dont la RAM est ajoutée à la liste
 * mémoire du système (bit 5), sans ROM ni chaînage, taille en bits 2-0. */
#define ZORRO_MANUFACTURER 0x07DBu
#define ZORRO_PRODUCT      0x01u
#define ERT_ZORROII        0xC0u
#define ERTF_MEMLIST       0x20u

typedef struct {
    uint8_t  *mem;
    uint32_t  size;
    uint32_t  base;        /* 0 tant que non configurée */
    uint8_t   base_lo;     /* quartet A19-A16 écrit en $4A, en attente de $48 */
    uint8_t   shut;        /* « shut up » : retirée de la chaîne sans être mappée */
} board_t;

uint8_t *zorro_page[256];
static board_t s_board[ZORRO_MAX_BOARDS];
static int     s_count;

static int size_code(uint32_t size)
{
    switch (size) {
    case 0x10000:  return 1;
    case 0x20000:  return 2;
    case 0x40000:  return 3;
    case 0x80000:  return 4;
    case 0x100000: return 5;
    case 0x200000: return 6;
    case 0x400000: return 7;
    case 0x800000: return 0;
    default:       return -1;
    }
}

int zorro_add_ram(uint8_t *mem, uint32_t size)
{
    if (!mem || size_code(size) < 0 || s_count >= ZORRO_MAX_BOARDS) return -1;
    s_board[s_count].mem = mem;
    s_board[s_count].size = size;
    s_count++;
    zorro_reset();
    return 0;
}

void zorro_reset(void)
{
    for (int i = 0; i < 256; i++) zorro_page[i] = NULL;
    for (int i = 0; i < s_count; i++) {
        s_board[i].base = 0;
        s_board[i].base_lo = 0;
        s_board[i].shut = 0;
    }
}

/* carte qui répond actuellement en $E80000 : la première ni configurée ni retirée */
static board_t *current(void)
{
    for (int i = 0; i < s_count; i++)
        if (!s_board[i].base && !s_board[i].shut) return &s_board[i];
    return NULL;
}

/* Valeur logique du registre (quartet) à l'offset off. Les registres à partir de $04 sont lus
 * inversés sur le bus : on rend la valeur logique, l'inversion est faite par l'appelant. */
static uint8_t reg_nibble(const board_t *b, uint32_t off)
{
    const uint8_t type = ERT_ZORROII | ERTF_MEMLIST | (uint8_t)size_code(b->size);
    switch (off) {
    case 0x00: return type >> 4;
    case 0x02: return type & 0xF;
    case 0x04: return ZORRO_PRODUCT >> 4;
    case 0x06: return ZORRO_PRODUCT & 0xF;
    case 0x10: return (ZORRO_MANUFACTURER >> 12) & 0xF;
    case 0x12: return (ZORRO_MANUFACTURER >> 8) & 0xF;
    case 0x14: return (ZORRO_MANUFACTURER >> 4) & 0xF;
    case 0x16: return ZORRO_MANUFACTURER & 0xF;
    default:   return 0;   /* flags, numéro de série, vecteur diag : 0 */
    }
}

int zorro_cfg_read8(uint32_t a, uint8_t *v)
{
    board_t *b = current();
    if (!b) return 0;
    uint32_t off = (a - ZORRO_CFG_BASE) & 0xFFFF;
    if (off >= 0x80) { *v = 0xFF; return 1; }
    uint8_t n = reg_nibble(b, off & 0xFE);
    if (off >= 0x04) n = (uint8_t)(~n & 0xF);
    *v = (uint8_t)(n << 4 | 0x0F);
    return 1;
}

int zorro_cfg_write8(uint32_t a, uint8_t v)
{
    board_t *b = current();
    if (!b) return 0;
    uint32_t off = (a - ZORRO_CFG_BASE) & 0xFFFF;
    if (off == 0x4A) {
        b->base_lo = v >> 4;
    } else if (off == 0x48) {
        uint32_t base = ((uint32_t)(v >> 4) << 20) | ((uint32_t)b->base_lo << 16);
        /* Zorro II : base alignée sur la taille (la carte de 8 Mo va en $200000), sous les
         * 16 Mo, jamais en page 0 (chip RAM). Sinon la carte reste à l'écoute. */
        int ok = (b->size == 0x800000u) ? (base == 0x200000u)
                                        : (base != 0 && !(base & (b->size - 1)));
        if (!ok || base + b->size > 0x1000000u) return 1;
        b->base = base;
        for (uint32_t p = 0; p < b->size >> 16; p++)
            zorro_page[(base >> 16) + p] = b->mem + (p << 16);
    } else if (off == 0x4C) {
        b->shut = 1;
    }
    return 1;
}

uint32_t zorro_board_base(int i) { return (i >= 0 && i < s_count) ? s_board[i].base : 0; }
uint32_t zorro_board_size(int i) { return (i >= 0 && i < s_count) ? s_board[i].size : 0; }
int      zorro_board_count(void)  { return s_count; }
