/* serial_kbd.h — Clavier via le port série USB (CP2104).
 *
 * Alternative/complément au clavier PS/2 : les caractères tapés dans le terminal
 * série du PC (pio device monitor / miniterm) sont convertis en frappes Amiga et
 * injectés dans kbd_amiga, exactement comme le clavier PS/2.
 */
#ifndef SERIAL_KBD_H
#define SERIAL_KBD_H

/* Lit les octets disponibles sur Serial et pousse les frappes dans kbd_amiga.
 * À appeler une fois par trame depuis emu_task. Aucun init (Serial déjà démarré). */
void serial_kbd_poll(void);

#endif /* SERIAL_KBD_H */
