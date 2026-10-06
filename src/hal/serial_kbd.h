/* serial_kbd.h — Clavier + souris via le port série USB.
 *
 * Alternative/complément au PS/2 : trames binaires de tools/remote_input.py
 * (clavier complet + souris) ou caractères tapés dans un terminal série
 * (pio device monitor / miniterm), injectés comme le PS/2.
 */
#ifndef SERIAL_KBD_H
#define SERIAL_KBD_H

/* Lit les octets disponibles sur Serial : frappes -> kbd_amiga, souris -> port souris.
 * À appeler une fois par trame depuis emu_task. Aucun init (Serial déjà démarré). */
void serial_kbd_poll(void);

#endif /* SERIAL_KBD_H */
