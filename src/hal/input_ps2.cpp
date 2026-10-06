/* input_ps2.cpp — Entrée PS/2 (FabGL).
 *
 * PHASE 1 (souris) : fabgl::Mouse -> port souris Amiga déjà émulé (input.cpp).
 *   Déplacement -> input_mouse_delta() (compteurs mx/my, JOY0DAT) ;
 *   boutons -> input_set_lmb()/input_set_rmb().
 * PHASE 2 (clavier) : dépiler la file VirtualKey -> kbd_amiga (à venir).
 *
 * Conversion d'axes : FabGL rapporte deltaY positif vers le HAUT ; côté Amiga le
 * compteur vertical croît vers le BAS -> on inverse Y.
 * Bornage : input.device calcule un delta 8 bits SIGNÉ à chaque vblank, donc le
 * mouvement appliqué par trame doit rester sous ±127. On borne à ±100 et on
 * reporte le reste à la trame suivante (pas de saut arrière sur mouvement rapide).
 */
#include "fabgl.h"
#include "input_ps2.h"
#include "a500.h"

#define MOUSE_STEP_MAX 100   /* delta max appliqué par trame (< 127) */

void input_ps2_init(void)
{
    /* clavier port 0 (GPIO33/32) + souris port 1 (GPIO26/27) */
    fabgl::PS2Controller::begin(fabgl::PS2Preset::KeyboardPort0_MousePort1);
}

void input_ps2_poll(void)
{
    fabgl::Mouse *m = fabgl::PS2Controller::mouse();
    if (!m) return;

    static int pend_x = 0, pend_y = 0;
    int lb = -1, rb = -1;

    /* draine tous les paquets souris accumulés depuis la trame précédente */
    fabgl::MouseDelta d;
    while (m->deltaAvailable() && m->getNextDelta(&d, 0)) {
        pend_x += d.deltaX;
        pend_y -= d.deltaY;              /* FabGL haut=+ -> Amiga bas=+ */
        lb = d.buttons.left;
        rb = d.buttons.right;
    }

    /* applique le mouvement borné, reporte le reste */
    int sx = pend_x >  MOUSE_STEP_MAX ?  MOUSE_STEP_MAX
           : pend_x < -MOUSE_STEP_MAX ? -MOUSE_STEP_MAX : pend_x;
    int sy = pend_y >  MOUSE_STEP_MAX ?  MOUSE_STEP_MAX
           : pend_y < -MOUSE_STEP_MAX ? -MOUSE_STEP_MAX : pend_y;
    pend_x -= sx;
    pend_y -= sy;
    if (sx || sy) input_mouse_delta(sx, sy);

    /* boutons : seulement si un paquet a été lu cette trame */
    if (lb >= 0) {
        input_set_lmb(lb);
        input_set_rmb(rb);
    }
}
