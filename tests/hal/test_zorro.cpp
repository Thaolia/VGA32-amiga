/* test_zorro.cpp — Tests hôte de l'autoconfig Zorro II (src/hal/zorro) : identité lue en
 * quartets, configuration par écriture de la base, mapping des pages, chaînage, reset. */
#include <stdio.h>
#include <string.h>
#include "zorro.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* lecture d'un registre logique comme le fait expansion.library : quartet de poids fort des
 * octets en off et off+2, inversés à partir de $04 */
static uint8_t rd_reg(uint32_t off)
{
    uint8_t hi, lo;
    CHECK(zorro_cfg_read8(ZORRO_CFG_BASE + off, &hi));
    CHECK(zorro_cfg_read8(ZORRO_CFG_BASE + off + 2, &lo));
    uint8_t v = (uint8_t)((hi & 0xF0) | (lo >> 4));
    return off >= 0x04 ? (uint8_t)~v : v;
}

static void configure(uint32_t base)
{
    zorro_cfg_write8(ZORRO_CFG_BASE + 0x4A, (uint8_t)((base >> 16) << 4));
    zorro_cfg_write8(ZORRO_CFG_BASE + 0x48, (uint8_t)(base >> 16));
}

static uint8_t ram1[0x100000], ram2[0x80000];

int main(void)
{
    uint8_t v;
    /* sans carte : la zone de config est vide */
    CHECK(!zorro_cfg_read8(ZORRO_CFG_BASE, &v));
    CHECK(zorro_add_ram(ram1, 12345) == -1);          /* taille invalide */

    CHECK(zorro_add_ram(ram1, sizeof ram1) == 0);
    CHECK(zorro_add_ram(ram2, sizeof ram2) == 0);

    /* 1re carte : Zorro II, ajoutée à la liste mémoire, 1 Mo (code 5) ; fabricant 0x07DB */
    CHECK(rd_reg(0x00) == 0xE5);
    CHECK(rd_reg(0x04) == 0x01);
    CHECK((rd_reg(0x10) << 8 | rd_reg(0x14)) == 0x07DB);
    CHECK(rd_reg(0x08) == 0x80);                       /* flags : espace 8 Mo (ERFF_MEMSPACE) */
    CHECK(zorro_page[0x20] == NULL);

    configure(0x200000);
    CHECK(zorro_board_base(0) == 0x200000);
    CHECK(zorro_page[0x20] == ram1 && zorro_page[0x2F] == ram1 + 0xF0000 && zorro_page[0x30] == NULL);

    /* la 2e carte apparaît : 512 Ko (code 4) ; une base mal alignée est refusée */
    CHECK(rd_reg(0x00) == 0xE4);
    configure(0x340000);
    CHECK(zorro_board_base(1) == 0);
    configure(0x300000);
    CHECK(zorro_board_base(1) == 0x300000);
    CHECK(zorro_page[0x30] == ram2 && zorro_page[0x37] == ram2 + 0x70000 && zorro_page[0x38] == NULL);

    /* plus rien à configurer */
    CHECK(!zorro_cfg_read8(ZORRO_CFG_BASE, &v));

    /* écriture/lecture via la table de pages */
    uint32_t a = 0x2ABCDE;
    zorro_page[a >> 16][a & 0xFFFF] = 0x5A;
    CHECK(ram1[0xABCDE] == 0x5A);

    /* reset : tout redevient non configuré, la 1re carte répond de nouveau */
    zorro_reset();
    CHECK(zorro_page[0x20] == NULL && zorro_board_base(0) == 0);
    CHECK(rd_reg(0x00) == 0xE5);

    /* shut up : carte retirée de la chaîne sans être mappée */
    zorro_cfg_write8(ZORRO_CFG_BASE + 0x4C, 0);
    CHECK(rd_reg(0x00) == 0xE4);
    CHECK(zorro_page[0x20] == NULL);

    printf("zorro : %s\n", fails ? "ECHEC" : "OK");
    return fails ? 1 : 0;
}
