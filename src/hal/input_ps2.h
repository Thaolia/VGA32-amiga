/* input_ps2.h — Entrée PS/2 (FabGL) : souris (Phase 1) puis clavier (Phase 2).
 * Remplace la lecture des boutons GPIO du cabinet d'origine.
 */
#ifndef INPUT_PS2_H
#define INPUT_PS2_H

/* Initialise fabgl::PS2Controller (clavier port 0 + souris port 1). */
void input_ps2_init(void);

/* Sondage, appelé une fois par trame depuis emu_task :
 *  - Phase 1 : souris PS/2 -> port souris Amiga (input_set_lmb/rmb + quadrature)
 *  - Phase 2 : file VirtualKey -> kbd_amiga
 * Stub no-op pour l'instant (Phase 0). */
void input_ps2_poll(void);

#endif /* INPUT_PS2_H */
