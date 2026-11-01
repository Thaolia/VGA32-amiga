/* test_kbd_amiga.cpp — Cadencement du MPU clavier (src/hal/kbd_amiga.cpp) avec des stubs
 * CIA-A : code suivant après le handshake (CRA bit 6), réémission sans handshake. */
#include <cstdio>
#include <cstdint>
#include "kbd_amiga.h"
#include "a500.h"

/* ---- stubs CIA-A ---- */
cia_t cia_a;
uint32_t cia_a_kbd_handshakes;
static int s_sent;
static uint8_t s_last;
void cia_a_kbd_shift_in(uint8_t sdr) { s_sent++; s_last = sdr; }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint8_t sdr_of(uint8_t raw, int down) { return (uint8_t)~((raw << 1) | (down ? 0 : 1)); }

int main(void)
{
    kbd_amiga_init();
    kbd_amiga_push(0x20, 1);                       /* A enfoncée */
    kbd_amiga_push(0x20, 0);                       /* A relâchée */

    kbd_amiga_step();
    CHECK(s_sent == 1 && s_last == sdr_of(0x20, 1));

    /* sans handshake, l'ICR lu ne suffit plus : rien ne part tant que le délai court */
    for (int f = 0; f < 7; f++) kbd_amiga_step();
    CHECK(s_sent == 1);
    /* au bout du délai : le MÊME code est réémis */
    kbd_amiga_step();
    CHECK(s_sent == 2 && s_last == sdr_of(0x20, 1));

    /* handshake de l'hôte : le code suivant part à la trame suivante */
    cia_a_kbd_handshakes++;
    kbd_amiga_step();
    CHECK(s_sent == 3 && s_last == sdr_of(0x20, 0));

    /* acquitté, FIFO vide : plus rien n'est émis */
    cia_a_kbd_handshakes++;
    for (int f = 0; f < 20; f++) kbd_amiga_step();
    CHECK(s_sent == 3);

    /* reset clavier Ctrl-Amiga-Amiga (keyboard-handshake-v34.md §5, HRM) : $78 à la place de la
       3e touche ; 1er $78 acquitté -> 2e $78, reset à la fin de son handshake */
    kbd_amiga_init(); s_sent = 0;
    kbd_amiga_push(0x63, 1); kbd_amiga_push(0x66, 1); kbd_amiga_push(0x67, 1);
    kbd_amiga_step(); CHECK(s_sent == 1 && s_last == sdr_of(0x63, 1));
    cia_a_kbd_handshakes++; kbd_amiga_step(); CHECK(s_sent == 2 && s_last == sdr_of(0x66, 1));
    cia_a_kbd_handshakes++; kbd_amiga_step(); CHECK(s_sent == 3 && s_last == sdr_of(0x78, 1));
    cia_a_kbd_handshakes++; kbd_amiga_step(); CHECK(s_sent == 4 && s_last == sdr_of(0x78, 1));
    /* reset handlers en cours : CRA bit 6 tenu, ni réémission ni reset avant 10 s */
    for (int f = 0; f < 100; f++) kbd_amiga_step();
    CHECK(s_sent == 4 && !kbd_amiga_reset_requested());
    cia_a_kbd_handshakes++; kbd_amiga_step();
    CHECK(kbd_amiga_reset_requested());
    CHECK(!kbd_amiga_reset_requested());            /* consommé */
    for (int f = 0; f < 20; f++) kbd_amiga_step();
    CHECK(s_sent == 4);                             /* plus rien à émettre */

    /* machine plantée : 1er $78 sans handshake -> reset au bout de ~250 ms, sans réémission */
    kbd_amiga_init(); s_sent = 0;
    kbd_amiga_push(0x67, 1); kbd_amiga_push(0x66, 1); kbd_amiga_push(0x63, 1);
    kbd_amiga_step(); cia_a_kbd_handshakes++;
    kbd_amiga_step(); cia_a_kbd_handshakes++;
    kbd_amiga_step(); CHECK(s_sent == 3 && s_last == sdr_of(0x78, 1));
    for (int f = 0; f < 11; f++) kbd_amiga_step();
    CHECK(!kbd_amiga_reset_requested());
    for (int f = 0; f < 3; f++) kbd_amiga_step();
    CHECK(kbd_amiga_reset_requested() && s_sent == 3);

    /* deux des trois touches seulement : codes ordinaires */
    kbd_amiga_init(); s_sent = 0;
    kbd_amiga_push(0x63, 1); kbd_amiga_push(0x66, 1); kbd_amiga_push(0x20, 1);
    for (int k = 0; k < 3; k++) { kbd_amiga_step(); cia_a_kbd_handshakes++; }
    CHECK(s_sent == 3 && s_last == sdr_of(0x20, 1) && !kbd_amiga_reset_requested());

    printf("kbd_amiga : %s\n", fails ? "ECHEC" : "OK");
    return fails ? 1 : 0;
}
