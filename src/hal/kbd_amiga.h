/* kbd_amiga.h — Émulation du clavier Amiga (NOUVEAU sous-système).
 *
 * L'émulateur d'origine n'a AUCUN clavier. Ce module ajoute les deux moitiés :
 *  A. le MPU clavier côté CIA-A (série KCLK->CNT / KDAT->SP, handshake, INT2) ;
 *  B. la table VirtualKey FabGL (positionnelle, layout US) -> rawcode Amiga.
 * Phase 2.
 */
#ifndef KBD_AMIGA_H
#define KBD_AMIGA_H

#include <stdint.h>

/* Réinitialise l'état clavier (FIFO, machine à états, séquence power-up 0xFD/0xFE). */
void kbd_amiga_init(void);

/* Pousse un événement touche (rawcode Amiga positionnel) dans la FIFO.
 * down=1 : touche enfoncée ; down=0 : relâchée (le bit 0x80 est géré en interne). */
void kbd_amiga_push(uint8_t rawcode, int down);

/* Fait avancer l'émission série vers CIA-A. Appelé périodiquement par emu_task. */
void kbd_amiga_step(void);

#endif /* KBD_AMIGA_H */
