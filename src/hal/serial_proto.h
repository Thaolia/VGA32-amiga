/* serial_proto.h — Décodeur des trames d'entrée PC -> carte sur l'UART USB.
 *
 * Module pur (aucune dépendance Arduino) : testé sur hôte (tests/hal/).
 * Format d'une trame : SYNC(0xA5) TYPE PAYLOAD... CHK, CHK = somme 8 bits de TYPE+PAYLOAD.
 *   'K' : 1 octet  = rawcode Amiga (bits 0-6) | 0x80 si relâchée
 *   'M' : 3 octets = dx (int8), dy (int8, convention Amiga : bas = +), boutons (b0 G, b1 D)
 * Tout octet hors trame est rendu tel quel (SP_ASCII) : un terminal série reste utilisable.
 * L'encodeur côté PC est tools/remote_input.py.
 */
#ifndef SERIAL_PROTO_H
#define SERIAL_PROTO_H

#include <stdint.h>

#define SP_SYNC 0xA5

typedef enum { SP_ASCII, SP_KEY, SP_MOUSE } sp_kind_t;

typedef struct {
    sp_kind_t kind;
    uint8_t   a;          /* SP_ASCII : octet ; SP_KEY : rawcode Amiga */
    uint8_t   down;       /* SP_KEY : 1 enfoncée, 0 relâchée */
    int8_t    dx, dy;     /* SP_MOUSE */
    uint8_t   lmb, rmb;   /* SP_MOUSE */
} sp_event_t;

typedef struct {
    uint8_t state;        /* 0 hors trame, 1 attend TYPE, 2 lit PAYLOAD, 3 attend CHK */
    uint8_t type;
    uint8_t len, n;
    uint8_t buf[3];
} sp_parser_t;

void sp_init(sp_parser_t *p);

/* Consomme un octet. Retourne 1 si *ev a été rempli, 0 sinon. */
int sp_feed(sp_parser_t *p, uint8_t b, sp_event_t *ev);

#endif /* SERIAL_PROTO_H */
