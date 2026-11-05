/* serial_kbd.cpp — Clavier + souris via le port série USB.
 *
 * Deux modes coexistent sur le même flux (démultiplexés par serial_proto) :
 *  - trames binaires de tools/remote_input.py : touches enfoncées/relâchées
 *    (rawcode exact, maintien et modificateurs inclus) + souris + joystick port 2 ;
 *  - texte d'un terminal : caractères convertis en frappes Amiga (rawcode + Shift),
 *    injectées dans kbd_amiga (down puis up, entourés de Shift si nécessaire).
 * Réutilise toute la Phase 2 (encodage SDR + émulation série CIA-A).
 *
 * Mapping ASCII -> touche PHYSIQUE US + état Shift (comme un vrai clavier US).
 * Gère aussi les séquences ESC [ A/B/C/D du terminal -> curseurs Amiga.
 *
 * Limites du mode texte (levées par les trames binaires) : pas de maintien de touche
 * (chaque caractère = press+release), Shift déduit du caractère, pas de Ctrl/Alt/Amiga.
 */
#include <Arduino.h>
#include "serial_kbd.h"
#include "serial_proto.h"
#include "kbd_amiga.h"
#include "a500.h"

#define MOUSE_STEP_MAX 100   /* delta souris max appliqué par trame (< 127), comme le PS/2 */

/* ASCII -> (rawcode Amiga, shift). Retourne false si non mappé. */
static bool ascii_to_amiga(char c, uint8_t *raw, bool *shift)
{
    *shift = false;
    /* lettres : rawcode positionnel US (ordre a..z) */
    static const uint8_t L[26] = {
        0x20, 0x35, 0x33, 0x22, 0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28, 0x37,
        0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14, 0x16, 0x34, 0x11, 0x32, 0x15, 0x31
    };
    if (c >= 'a' && c <= 'z') { *raw = L[c - 'a']; return true; }
    if (c >= 'A' && c <= 'Z') { *raw = L[c - 'A']; *shift = true; return true; }

    switch (c) {
    case '1': *raw = 0x01; return true;  case '!': *raw = 0x01; *shift = true; return true;
    case '2': *raw = 0x02; return true;  case '@': *raw = 0x02; *shift = true; return true;
    case '3': *raw = 0x03; return true;  case '#': *raw = 0x03; *shift = true; return true;
    case '4': *raw = 0x04; return true;  case '$': *raw = 0x04; *shift = true; return true;
    case '5': *raw = 0x05; return true;  case '%': *raw = 0x05; *shift = true; return true;
    case '6': *raw = 0x06; return true;  case '^': *raw = 0x06; *shift = true; return true;
    case '7': *raw = 0x07; return true;  case '&': *raw = 0x07; *shift = true; return true;
    case '8': *raw = 0x08; return true;  case '*': *raw = 0x08; *shift = true; return true;
    case '9': *raw = 0x09; return true;  case '(': *raw = 0x09; *shift = true; return true;
    case '0': *raw = 0x0A; return true;  case ')': *raw = 0x0A; *shift = true; return true;
    case '-': *raw = 0x0B; return true;  case '_': *raw = 0x0B; *shift = true; return true;
    case '=': *raw = 0x0C; return true;  case '+': *raw = 0x0C; *shift = true; return true;
    case '\\':*raw = 0x0D; return true;  case '|': *raw = 0x0D; *shift = true; return true;
    case '`': *raw = 0x00; return true;  case '~': *raw = 0x00; *shift = true; return true;
    case '[': *raw = 0x1A; return true;  case '{': *raw = 0x1A; *shift = true; return true;
    case ']': *raw = 0x1B; return true;  case '}': *raw = 0x1B; *shift = true; return true;
    case ';': *raw = 0x29; return true;  case ':': *raw = 0x29; *shift = true; return true;
    case '\'':*raw = 0x2A; return true;  case '"': *raw = 0x2A; *shift = true; return true;
    case ',': *raw = 0x38; return true;  case '<': *raw = 0x38; *shift = true; return true;
    case '.': *raw = 0x39; return true;  case '>': *raw = 0x39; *shift = true; return true;
    case '/': *raw = 0x3A; return true;  case '?': *raw = 0x3A; *shift = true; return true;
    case ' ':  *raw = 0x40; return true;            /* espace */
    case '\r': case '\n': *raw = 0x44; return true; /* Return */
    case '\b': case 0x7F: *raw = 0x41; return true; /* Backspace */
    case '\t': *raw = 0x42; return true;            /* Tab */
    default: return false;
    }
}

/* émet une frappe complète (avec Shift éventuel) vers kbd_amiga */
static void emit(uint8_t raw, bool shift)
{
    if (raw == 0xFF) return;
    if (shift) kbd_amiga_push(0x60, 1);   /* LShift down */
    kbd_amiga_push(raw, 1);               /* touche down */
    kbd_amiga_push(raw, 0);               /* touche up */
    if (shift) kbd_amiga_push(0x60, 0);   /* LShift up */
}

void serial_kbd_poll(void)
{
    static int esc = 0;   /* 0 = normal, 1 = ESC reçu, 2 = ESC[ reçu */
    static sp_parser_t parser = { 0, 0, 0, 0, { 0, 0, 0 } };
    /* input.device lit un delta 8 bits et les boutons une fois par vblank : les trames souris
     * passent par une file (serial_proto), un pas borné et une transition au plus par trame. */
    static sp_mouse_t mouse = { {}, 0, 0, 0 };

    while (Serial.available() > 0) {
        int ci = Serial.read();
        if (ci < 0) break;

        sp_event_t ev;
        if (!sp_feed(&parser, (uint8_t)ci, &ev)) continue;
        if (ev.kind == SP_KEY)   { kbd_amiga_push(ev.a, ev.down); continue; }
        if (ev.kind == SP_MOUSE) {
            sp_mouse_push(&mouse, ev.dx, ev.dy, (uint8_t)(ev.lmb | (ev.rmb << 1)));
            continue;
        }
        if (ev.kind == SP_JOY) {
            input_set_joy(ev.a & 1, (ev.a >> 1) & 1, (ev.a >> 2) & 1, (ev.a >> 3) & 1, (ev.a >> 4) & 1);
            continue;
        }
        char c = (char)ev.a;

        if (esc == 1) {                   /* après ESC : séquence ou ESC isolé */
            if (c == '[') { esc = 2; continue; }
            emit(0x45, false);            /* ESC isolé */
            esc = 0;
            /* et on traite c normalement ci-dessous */
        } else if (esc == 2) {            /* ESC [ X : curseurs */
            esc = 0;
            uint8_t raw = 0xFF;
            if      (c == 'A') raw = 0x4C;   /* haut */
            else if (c == 'B') raw = 0x4D;   /* bas */
            else if (c == 'C') raw = 0x4E;   /* droite */
            else if (c == 'D') raw = 0x4F;   /* gauche */
            emit(raw, false);
            continue;
        }

        if (c == 0x1B) { esc = 1; continue; }

        uint8_t raw; bool shift;
        if (ascii_to_amiga(c, &raw, &shift))
            emit(raw, shift);
    }

    int sx, sy;
    uint8_t btn;
    static uint8_t btn_prev = 0;
    sp_mouse_step(&mouse, MOUSE_STEP_MAX, &sx, &sy, &btn);
    if (btn != btn_prev) {               /* sur changement seulement : la souris PS/2 règle aussi les boutons */
        btn_prev = btn;
        input_set_lmb(btn & 1);
        input_set_rmb((btn >> 1) & 1);
    }
    if (sx || sy) input_mouse_delta(sx, sy);
}
