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

    /* 8. joystick port 2 : haut + gauche + fire = 0x01 | 0x04 | 0x10 */
    sp_init(&p);
    const uint8_t jy[] = { 0xA5, 'J', 0x15, (uint8_t)('J' + 0x15) };
    CHECK(feed(&p, jy, 4, ev, 8) == 1);
    CHECK(ev[0].kind == SP_JOY && ev[0].a == 0x15);
    const uint8_t jbad[] = { 0xA5, 'J', 0x15, 0x00, 'c' };
    CHECK(feed(&p, jbad, 5, ev, 8) == 1);
    CHECK(ev[0].kind == SP_ASCII && ev[0].a == 'c');

    /* 9. file souris : appui et relâchement reçus dans le même poll -> deux trames émulées */
    sp_mouse_t m;
    int dx, dy;
    uint8_t btn;
    sp_mouse_init(&m);
    sp_mouse_push(&m, 0, 0, 1);
    sp_mouse_push(&m, 0, 0, 0);
    sp_mouse_step(&m, 100, &dx, &dy, &btn);
    CHECK(btn == 1 && dx == 0 && dy == 0);
    sp_mouse_step(&m, 100, &dx, &dy, &btn);
    CHECK(btn == 0);

    /* 10. clic après un grand mouvement : appliqué une fois le mouvement fini (bon endroit) */
    sp_mouse_init(&m);
    sp_mouse_push(&m, 120, -40, 0);
    sp_mouse_push(&m, 130, 0, 0);                  /* même état de boutons : cumulé */
    sp_mouse_push(&m, 0, 0, 2);                    /* bouton droit */
    sp_mouse_push(&m, 5, 5, 2);                    /* mouvement après le clic */
    int sx = 0, sy = 0, pressed_at = -1;
    for (int f = 0; f < 6; f++) {
        sp_mouse_step(&m, 100, &dx, &dy, &btn);
        CHECK(dx >= -100 && dx <= 100 && dy >= -100 && dy <= 100);
        if (btn == 2 && pressed_at < 0) { pressed_at = f; CHECK(sx == 250 && sy == -40); }
        sx += dx; sy += dy;
    }
    CHECK(pressed_at >= 0 && sx == 255 && sy == -35 && btn == 2);

    /* 11. file vide : rien ne bouge, l'état des boutons reste */
    sp_mouse_step(&m, 100, &dx, &dy, &btn);
    CHECK(dx == 0 && dy == 0 && btn == 2);

    /* 12. file pleine : pas de débordement, l'état final des boutons est gardé */
    sp_mouse_init(&m);
    for (int i = 0; i < 100; i++) sp_mouse_push(&m, 1, 0, (uint8_t)(i & 1));
    sx = 0;
    for (int f = 0; f < 200; f++) { sp_mouse_step(&m, 100, &dx, &dy, &btn); sx += dx; }
    CHECK(sx == 100 && btn == 1);

    if (fails) { printf("%d echec(s)\n", fails); return 1; }
    printf("serial_proto : OK\n");
    return 0;
}
