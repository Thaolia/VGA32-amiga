/* test_cia_c.cpp — Tests des interruptions CIA (core/cia.cpp) avec des stubs à la place de
 * Paula, du lecteur et des entrées : niveau /INT2-/INT6 vu par INTREQ, ICR (AbleICR). */
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include "a500.h"
extern "C" {
#include "m68k.h"
}

/* ---- stubs ---- */
uint16_t intena, intreq, dmacon, adkcon;
int vpos, hpos, cur_frame, ovl;
FILE *rdlogf;
void intreq_set(int bit) { intreq |= (uint16_t)(1 << bit); }
void mem_map_update(void) {}
void logmsg(const char *, ...) {}
extern "C" unsigned int m68k_get_reg(void *, m68k_register_t) { return 0; }
int drive_selected(void) { return 0; }
uint8_t drive_pra_bits(void) { return 0x3C; }
void drive_prb_write(uint8_t) {}
int input_lmb(void) { return 0; }
int input_joy_fire(void) { return 0; }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } \
                      else printf("OK   %s\n", #c); } while (0)

/* exec : efface un bit d'INTREQ comme le fait le 68000 (écriture INTREQ, bit 15 à 0) */
static void intreq_clear(uint16_t bits) { intreq &= (uint16_t)~bits; cia_intreq_relevel(); }

int main(void)
{
    cia_reset();

    /* timer A de CIA-A autorisé : débordement -> IR + PORTS */
    cia_write(&cia_a, 0xD, 0x81);
    cia_write(&cia_a, 0x4, 2); cia_write(&cia_a, 0x5, 0);
    cia_write(&cia_a, 0xE, 0x01);
    intreq = 0;
    cia_tick(10);
    CHECK(intreq & 0x0008);

    /* exec efface PORTS sans qu'aucun serveur ait lu l'ICR : le niveau le relève */
    intreq_clear(0x0008);
    CHECK(intreq & 0x0008);

    /* l'ICR lu (IR retombe) puis PORTS effacé : il reste effacé */
    CHECK(cia_read(&cia_a, 0xD) & 0x81);
    intreq_clear(0x0008);
    CHECK(!(intreq & 0x0008));

    /* même chose côté CIA-B / EXTER */
    cia_write(&cia_b, 0xD, 0x82);
    cia_write(&cia_b, 0x6, 2); cia_write(&cia_b, 0x7, 0);
    cia_write(&cia_b, 0xF, 0x01);
    cia_tick(10);
    CHECK(intreq & 0x2000);
    intreq_clear(0x2000);
    CHECK(intreq & 0x2000);
    cia_read(&cia_b, 0xD);
    intreq_clear(0x2000);
    CHECK(!(intreq & 0x2000));

    /* AbleICR : source en attente masquée, puis autorisée -> IR et PORTS aussitôt */
    cia_write(&cia_a, 0xD, 0x7F);                  /* tout interdire */
    cia_write(&cia_a, 0xE, 0x00);
    cia_write(&cia_b, 0xF, 0x00);
    cia_read(&cia_a, 0xD); cia_read(&cia_b, 0xD);
    intreq = 0;
    cia_write(&cia_a, 0x4, 2); cia_write(&cia_a, 0x5, 0);
    cia_write(&cia_a, 0xE, 0x01);
    cia_tick(10);
    CHECK(!(intreq & 0x0008));
    cia_write(&cia_a, 0xD, 0x81);
    CHECK(intreq & 0x0008);
    CHECK((cia_read(&cia_a, 0xD) & 0x81) == 0x81);

    /* E-clock : 2 lignes PAL de 455 cycles CPU = 91 E (45,5 par ligne, pas 2 x 45) */
    cia_write(&cia_a, 0xE, 0x00);
    cia_write(&cia_a, 0x4, 0xFF); cia_write(&cia_a, 0x5, 0xFF);
    cia_write(&cia_a, 0xE, 0x01);
    cia_tick_cpu(455); cia_tick_cpu(455);
    CHECK(cia_a.ta == 0xFFFF - 91);

    /* handshake clavier : CRA bit 6 mis puis remis à 0 sur CIA-A = un acquittement ;
       sur CIA-B, ou sans retombée, rien */
    const uint32_t hs = cia_a_kbd_handshakes;
    cia_write(&cia_a, 0xE, 0x40);
    CHECK(cia_a_kbd_handshakes == hs);
    cia_write(&cia_a, 0xE, 0x00);
    CHECK(cia_a_kbd_handshakes == hs + 1);
    cia_write(&cia_b, 0xE, 0x40); cia_write(&cia_b, 0xE, 0x00);
    CHECK(cia_a_kbd_handshakes == hs + 1);

    /* index du lecteur sur /FLG de CIA-B : ICR bit 4, EXTER seulement si autorisé */
    cia_write(&cia_b, 0xD, 0x7F); cia_read(&cia_b, 0xD); intreq = 0;
    cia_b_index_pulse();
    CHECK(!(intreq & 0x2000));
    CHECK(cia_read(&cia_b, 0xD) == 0x10);
    cia_write(&cia_b, 0xD, 0x90);
    cia_b_index_pulse();
    CHECK(intreq & 0x2000);
    CHECK(cia_read(&cia_b, 0xD) == 0x90);

    printf("cia : %s\n", fails ? "ECHEC" : "OK");
    return fails ? 1 : 0;
}
