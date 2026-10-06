/* kbd_amiga.cpp — Émulation clavier Amiga (Phase 2). Stub pour l'instant.
 *
 * À IMPLÉMENTER (Phase 2) :
 *  A. Côté CIA-A : charger le rawcode (inversé, complément à 1, MSB d'abord,
 *     décalé << 1 | updown) dans le SDR de CIA-A, lever le flag SP de l'ICR
 *     (bit 3) et asserter INT2. Auto-acquitter le handshake (impulsion SP, >=85us).
 *     Émettre 0xFD/0xFE au reset. ⚠ VÉRIFIER d'abord si core/cia.cpp modélise déjà
 *     le port série SP/CNT ; sinon ce module doit l'ajouter à cia.cpp.
 *  B. Table positionnelle VirtualKey FabGL (layout US) -> rawcode (HRM App. F) :
 *     '1'..'0'=0x01..0x0A, Q..P=0x10.., A..L=0x20.., Z..M=0x31.., Space=0x40,
 *     Backspace=0x41, Tab=0x42, Return=0x44, Esc=0x45, Del=0x46, curseurs=0x4C..0x4F,
 *     F1..F10=0x50.., LShift=0x60, Ctrl=0x63, LAlt=0x64, LAmiga=0x66, RAmiga=0x67.
 */
#include "kbd_amiga.h"

void kbd_amiga_init(void) { /* Phase 2 */ }
void kbd_amiga_push(uint8_t rawcode, int down) { (void)rawcode; (void)down; /* Phase 2 */ }
void kbd_amiga_step(void) { /* Phase 2 */ }
