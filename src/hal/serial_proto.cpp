/* serial_proto.cpp — Décodeur des trames d'entrée PC -> carte (cf. serial_proto.h).
 *
 * Pas de resynchro sur SYNC en cours de trame : 0xA5 est une valeur de charge
 * utile légitime (ex. relâche de H = 0x25|0x80). Une trame corrompue est rejetée
 * par la somme de contrôle et le décodeur repart hors trame.
 */
#include "serial_proto.h"

enum { ST_IDLE, ST_TYPE, ST_PAYLOAD, ST_CHK };

void sp_init(sp_parser_t *p)
{
    p->state = ST_IDLE;
    p->type = 0;
    p->len = p->n = 0;
}

int sp_feed(sp_parser_t *p, uint8_t b, sp_event_t *ev)
{
    switch (p->state) {
    case ST_IDLE:
        if (b == SP_SYNC) { p->state = ST_TYPE; return 0; }
        ev->kind = SP_ASCII;
        ev->a = b;
        return 1;

    case ST_TYPE:
        if      (b == 'K') p->len = 1;
        else if (b == 'M') p->len = 3;
        else { p->state = ST_IDLE; return 0; }   /* type inconnu : trame abandonnée */
        p->type = b;
        p->n = 0;
        p->state = ST_PAYLOAD;
        return 0;

    case ST_PAYLOAD:
        p->buf[p->n++] = b;
        if (p->n == p->len) p->state = ST_CHK;
        return 0;

    case ST_CHK: {
        p->state = ST_IDLE;
        uint8_t sum = p->type;
        for (int i = 0; i < p->len; i++) sum = (uint8_t)(sum + p->buf[i]);
        if (sum != b) return 0;                  /* trame corrompue : jetée */
        if (p->type == 'K') {
            ev->kind = SP_KEY;
            ev->a = p->buf[0] & 0x7F;
            ev->down = (p->buf[0] & 0x80) ? 0 : 1;
        } else {
            ev->kind = SP_MOUSE;
            ev->dx = (int8_t)p->buf[0];
            ev->dy = (int8_t)p->buf[1];
            ev->lmb = p->buf[2] & 1;
            ev->rmb = (p->buf[2] >> 1) & 1;
        }
        return 1;
    }
    }
    p->state = ST_IDLE;
    return 0;
}
