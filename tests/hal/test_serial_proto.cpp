/* test_serial_proto.cpp — Tests hôte du décodeur de trames série (src/hal/serial_proto).
 * Les vecteurs binaires correspondent à ceux produits par tools/remote_input.py
 * (encode_key / encode_mouse) : toute divergence casse le lien PC -> carte. */
#include <stdio.h>
#include <string.h>
#include "serial_proto.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* injecte une suite d'octets, collecte les événements produits */
static int feed(sp_parser_t *p, const uint8_t *b, int n, sp_event_t *out, int max)
{
    int k = 0;
    for (int i = 0; i < n; i++) {
        sp_event_t ev;
        if (sp_feed(p, b[i], &ev) && k < max) out[k++] = ev;
    }
    return k;
}

int main(void)
{
    sp_parser_t p;
    sp_event_t ev[8];

    /* 1. ASCII hors trame -> transmis tel quel (compat terminal) */
    sp_init(&p);
    const uint8_t txt[] = { 'a', '\r', 0x1B, '[', 'A' };
    CHECK(feed(&p, txt, 5, ev, 8) == 5);
    CHECK(ev[0].kind == SP_ASCII && ev[0].a == 'a');
    CHECK(ev[2].kind == SP_ASCII && ev[2].a == 0x1B);

    /* 2. touche 'A' (0x20) enfoncée puis relâchée */
    sp_init(&p);
    const uint8_t kd[] = { 0xA5, 'K', 0x20, (uint8_t)('K' + 0x20) };
    const uint8_t ku[] = { 0xA5, 'K', 0xA0, (uint8_t)('K' + 0xA0) };
    CHECK(feed(&p, kd, 4, ev, 8) == 1);
    CHECK(ev[0].kind == SP_KEY && ev[0].a == 0x20 && ev[0].down == 1);
    CHECK(feed(&p, ku, 4, ev, 8) == 1);
    CHECK(ev[0].kind == SP_KEY && ev[0].a == 0x20 && ev[0].down == 0);

    /* 3. souris dx=-5 dy=+3 boutons G+D */
    sp_init(&p);
    const uint8_t ms[] = { 0xA5, 'M', 0xFB, 0x03, 0x03, (uint8_t)('M' + 0xFB + 0x03 + 0x03) };
    CHECK(feed(&p, ms, 6, ev, 8) == 1);
    CHECK(ev[0].kind == SP_MOUSE && ev[0].dx == -5 && ev[0].dy == 3);
    CHECK(ev[0].lmb == 1 && ev[0].rmb == 1);

    /* 4. somme de contrôle fausse -> trame jetée, puis resynchro sur la suivante */
    sp_init(&p);
    const uint8_t bad[] = { 0xA5, 'K', 0x20, 0x00, 0xA5, 'K', 0x21, (uint8_t)('K' + 0x21) };
    CHECK(feed(&p, bad, 8, ev, 8) == 1);
    CHECK(ev[0].kind == SP_KEY && ev[0].a == 0x21);

    /* 5. type inconnu -> abandon de la trame, l'octet suivant redevient ASCII */
    sp_init(&p);
    const uint8_t unk[] = { 0xA5, 'Z', 'b' };
    CHECK(feed(&p, unk, 3, ev, 8) == 1);
    CHECK(ev[0].kind == SP_ASCII && ev[0].a == 'b');

    /* 6. 0xA5 dans la charge utile est une DONNÉE (relâche de H = 0x25|0x80),
     *    pas une resynchro : la robustesse repose sur la somme de contrôle */
    sp_init(&p);
    const uint8_t hu[] = { 0xA5, 'K', 0xA5, (uint8_t)('K' + 0xA5) };
    CHECK(feed(&p, hu, 4, ev, 8) == 1);
    CHECK(ev[0].kind == SP_KEY && ev[0].a == 0x25 && ev[0].down == 0);

    /* 7. trames collées */
    sp_init(&p);
    uint8_t two[8];
    memcpy(two, kd, 4); memcpy(two + 4, ku, 4);
    CHECK(feed(&p, two, 8, ev, 8) == 2);
    CHECK(ev[0].down == 1 && ev[1].down == 0);

    if (fails) { printf("%d echec(s)\n", fails); return 1; }
    printf("serial_proto : OK\n");
    return 0;
}
