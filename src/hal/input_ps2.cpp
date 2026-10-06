/* input_ps2.cpp — Entrée PS/2 (FabGL) : souris (Phase 1) + clavier (Phase 2).
 *
 * Souris : fabgl::Mouse -> port souris Amiga émulé (input.cpp). deltaY inversé
 *   (FabGL haut=+ / Amiga bas=+), bornage ±100/trame avec report.
 * Clavier : file VirtualKey FabGL -> table POSITIONNELLE (layout US) -> rawcode
 *   Amiga -> kbd_amiga (émulation série CIA-A). On mappe par POSITION physique :
 *   minuscule/majuscule (VK_a/VK_A) et chiffre/symbole (VK_1/VK_EXCLAIM) vont au
 *   même rawcode ; la touche Shift (VK_LSHIFT->0x60) est transmise séparément et
 *   c'est l'Amiga qui applique sa propre keymap.
 */
#include "fabgl.h"
#include "input_ps2.h"
#include "kbd_amiga.h"
#include "a500.h"

#define MOUSE_STEP_MAX 100   /* delta souris max appliqué par trame (< 127) */

/* VirtualKey FabGL -> rawcode Amiga (HRM / vérifié via Linux amikbd.c).
 * Retourne 0xFF pour une touche non mappée. */
static uint8_t vk_to_amiga(fabgl::VirtualKey vk)
{
    using namespace fabgl;
    switch (vk) {
    /* --- lettres (min. + maj. = même touche) --- */
    case VK_a: case VK_A: return 0x20;
    case VK_b: case VK_B: return 0x35;
    case VK_c: case VK_C: return 0x33;
    case VK_d: case VK_D: return 0x22;
    case VK_e: case VK_E: return 0x12;
    case VK_f: case VK_F: return 0x23;
    case VK_g: case VK_G: return 0x24;
    case VK_h: case VK_H: return 0x25;
    case VK_i: case VK_I: return 0x17;
    case VK_j: case VK_J: return 0x26;
    case VK_k: case VK_K: return 0x27;
    case VK_l: case VK_L: return 0x28;
    case VK_m: case VK_M: return 0x37;
    case VK_n: case VK_N: return 0x36;
    case VK_o: case VK_O: return 0x18;
    case VK_p: case VK_P: return 0x19;
    case VK_q: case VK_Q: return 0x10;
    case VK_r: case VK_R: return 0x13;
    case VK_s: case VK_S: return 0x21;
    case VK_t: case VK_T: return 0x14;
    case VK_u: case VK_U: return 0x16;
    case VK_v: case VK_V: return 0x34;
    case VK_w: case VK_W: return 0x11;
    case VK_x: case VK_X: return 0x32;
    case VK_y: case VK_Y: return 0x15;
    case VK_z: case VK_Z: return 0x31;
    /* --- rangée chiffres (chiffre + symbole shifté = même touche) --- */
    case VK_1: case VK_EXCLAIM:    return 0x01;
    case VK_2: case VK_AT:         return 0x02;
    case VK_3: case VK_HASH:       return 0x03;
    case VK_4: case VK_DOLLAR:     return 0x04;
    case VK_5: case VK_PERCENT:    return 0x05;
    case VK_6: case VK_CARET:      return 0x06;
    case VK_7: case VK_AMPERSAND:  return 0x07;
    case VK_8: case VK_ASTERISK:   return 0x08;
    case VK_9: case VK_LEFTPAREN:  return 0x09;
    case VK_0: case VK_RIGHTPAREN: return 0x0A;
    /* --- ponctuation --- */
    case VK_MINUS:   case VK_UNDERSCORE:   return 0x0B;
    case VK_EQUALS:  case VK_PLUS:         return 0x0C;
    case VK_BACKSLASH: case VK_VERTICALBAR: return 0x0D;
    case VK_GRAVEACCENT: case VK_TILDE:    return 0x00;
    case VK_LEFTBRACKET: case VK_LEFTBRACE: return 0x1A;
    case VK_RIGHTBRACKET: case VK_RIGHTBRACE: return 0x1B;
    case VK_SEMICOLON: case VK_COLON:      return 0x29;
    case VK_QUOTE: case VK_QUOTEDBL:       return 0x2A;
    case VK_COMMA: case VK_LESS:           return 0x38;
    case VK_PERIOD: case VK_GREATER:       return 0x39;
    case VK_SLASH: case VK_QUESTION:       return 0x3A;
    /* --- touches spéciales --- */
    case VK_SPACE:     return 0x40;
    case VK_BACKSPACE: return 0x41;
    case VK_TAB:       return 0x42;
    case VK_RETURN:    return 0x44;
    case VK_ESCAPE:    return 0x45;
    case VK_DELETE:    return 0x46;
    case VK_UP:        return 0x4C;
    case VK_DOWN:      return 0x4D;
    case VK_RIGHT:     return 0x4E;
    case VK_LEFT:      return 0x4F;
    case VK_F1:  return 0x50;  case VK_F2:  return 0x51;
    case VK_F3:  return 0x52;  case VK_F4:  return 0x53;
    case VK_F5:  return 0x54;  case VK_F6:  return 0x55;
    case VK_F7:  return 0x56;  case VK_F8:  return 0x57;
    case VK_F9:  return 0x58;  case VK_F10: return 0x59;
    /* --- modificateurs --- */
    case VK_LSHIFT:   return 0x60;
    case VK_RSHIFT:   return 0x61;
    case VK_CAPSLOCK: return 0x62;
    case VK_LCTRL: case VK_RCTRL: return 0x63;   /* Amiga : un seul Ctrl */
    case VK_LALT:     return 0x64;
    case VK_RALT:     return 0x65;
    case VK_LGUI:     return 0x66;               /* Amiga gauche */
    case VK_RGUI:     return 0x67;               /* Amiga droite */
    /* --- pavé numérique --- */
    case VK_KP_ENTER:    return 0x43;
    case VK_KP_0: return 0x0F;
    case VK_KP_1: return 0x1D;  case VK_KP_2: return 0x1E;  case VK_KP_3: return 0x1F;
    case VK_KP_4: return 0x2D;  case VK_KP_5: return 0x2E;  case VK_KP_6: return 0x2F;
    case VK_KP_7: return 0x3D;  case VK_KP_8: return 0x3E;  case VK_KP_9: return 0x3F;
    case VK_KP_PERIOD:   return 0x3C;
    case VK_KP_MINUS:    return 0x4A;
    case VK_KP_PLUS:     return 0x5E;
    case VK_KP_DIVIDE:   return 0x5C;
    case VK_KP_MULTIPLY: return 0x5D;
    default: return 0xFF;
    }
}

void input_ps2_init(void)
{
    /* clavier port 0 (GPIO33/32) + souris port 1 (GPIO26/27) */
    fabgl::PS2Controller::begin(fabgl::PS2Preset::KeyboardPort0_MousePort1);
}

void input_ps2_poll(void)
{
    /* ---- souris ---- */
    fabgl::Mouse *m = fabgl::PS2Controller::mouse();
    if (m) {
        static int pend_x = 0, pend_y = 0;
        int lb = -1, rb = -1;
        fabgl::MouseDelta d;
        while (m->deltaAvailable() && m->getNextDelta(&d, 0)) {
            pend_x += d.deltaX;
            pend_y -= d.deltaY;              /* FabGL haut=+ -> Amiga bas=+ */
            lb = d.buttons.left;
            rb = d.buttons.right;
        }
        int sx = pend_x >  MOUSE_STEP_MAX ?  MOUSE_STEP_MAX
               : pend_x < -MOUSE_STEP_MAX ? -MOUSE_STEP_MAX : pend_x;
        int sy = pend_y >  MOUSE_STEP_MAX ?  MOUSE_STEP_MAX
               : pend_y < -MOUSE_STEP_MAX ? -MOUSE_STEP_MAX : pend_y;
        pend_x -= sx;
        pend_y -= sy;
        if (sx || sy) input_mouse_delta(sx, sy);
        if (lb >= 0) { input_set_lmb(lb); input_set_rmb(rb); }
    }

    /* ---- clavier ---- */
    fabgl::Keyboard *k = fabgl::PS2Controller::keyboard();
    if (k) {
        fabgl::VirtualKeyItem item;
        while (k->virtualKeyAvailable() && k->getNextVirtualKey(&item, 0)) {
            uint8_t raw = vk_to_amiga(item.vk);
            if (raw != 0xFF)
                kbd_amiga_push(raw, item.down);
        }
    }
}
