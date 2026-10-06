/* input_ps2.cpp — Entrée PS/2 (FabGL).
 *
 * PHASE 0 : stub no-op (l'émulateur boote au bureau Workbench à la souris, mais
 * Phase 0 ne vise que l'image + la mesure de framerate).
 *
 * PHASE 1 (souris) : initialiser PS2Controller, lire fabgl::Mouse
 *   (getNextStatus) et piloter le port souris Amiga déjà émulé (input.cpp) :
 *   boutons -> input_set_lmb()/input_set_rmb(), déplacement -> quadrature JOY0DAT.
 * PHASE 2 (clavier) : dépiler la file VirtualKey de fabgl::Keyboard et la passer
 *   à kbd_amiga (mapping positionnel US -> rawcode Amiga).
 */
#include "input_ps2.h"
#include "a500.h"

void input_ps2_init(void)
{
    /* Phase 1/2 : PS2Controller::begin(PS2Preset::KeyboardPort0_MousePort1). */
}

void input_ps2_poll(void)
{
    /* Phase 1/2 : lecture souris/clavier ici. */
}
