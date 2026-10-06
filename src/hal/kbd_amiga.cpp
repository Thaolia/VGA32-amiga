/* kbd_amiga.cpp — Émulation du MPU clavier Amiga côté CIA-A (Phase 2).
 *
 * L'émulateur d'origine n'a aucun clavier. Ici on reproduit la moitié
 * « matérielle » : une FIFO de rawcodes (poussés par input_ps2 depuis le clavier
 * PS/2) est émise un code à la fois dans le registre série SDR de CIA-A, ce qui
 * lève l'interruption série (ICR bit 3 -> PORTS -> INT2) lue par keyboard.device.
 *
 * Encodage (vérifié sur le driver Linux amikbd.c : hôte fait rawcode = ~SDR,
 * relâche = rawcode & 1, keycode = rawcode >> 1) :
 *     SDR = ~((rawcode << 1) | (relache ? 1 : 0))
 *
 * Cadencement = handshake simplifié : on n'envoie le code suivant que lorsque
 * l'IRQ série précédente a été acquittée (lecture de l'ICR par keyboard.device,
 * qui remet icr_data à 0). Tant que KS n'a pas activé le clavier, les codes
 * s'accumulent sans déborder (pas de flood).
 *
 * NON implémenté volontairement (démarrage simple) : la séquence power-up
 * 0xFD/0xFE. KS 1.3 accepte en général les keycodes directs. Si des touches ne
 * sont pas prises en compte au boot, l'émettre ici en PREMIER est le correctif.
 */
#include "kbd_amiga.h"
#include "a500.h"

#define KBD_FIFO 32

static uint8_t s_raw[KBD_FIFO];
static uint8_t s_down[KBD_FIFO];
static int s_head, s_tail, s_count;

void kbd_amiga_init(void)
{
    s_head = s_tail = s_count = 0;
}

void kbd_amiga_push(uint8_t rawcode, int down)
{
    if (s_count >= KBD_FIFO) {          /* plein : on jette le plus ancien */
        s_head = (s_head + 1) % KBD_FIFO;
        s_count--;
    }
    s_raw[s_tail]  = rawcode & 0x7F;
    s_down[s_tail] = down ? 1 : 0;
    s_tail = (s_tail + 1) % KBD_FIFO;
    s_count++;
}

void kbd_amiga_step(void)
{
    /* handshake : attendre l'acquittement de l'IRQ série précédente */
    if (cia_a.icr_data & 0x08) return;
    if (s_count == 0) return;

    uint8_t raw  = s_raw[s_head];
    uint8_t down = s_down[s_head];
    s_head = (s_head + 1) % KBD_FIFO;
    s_count--;

    uint8_t up  = down ? 0 : 1;                       /* bit 0 = relâche */
    uint8_t sdr = (uint8_t)~(((raw << 1) | up));      /* inversé (keyboard active-low) */
    cia_a_kbd_shift_in(sdr);
}
