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
 * Cadencement = handshake du MPU réel : le code suivant ne part qu'après l'impulsion
 * KDAT de l'hôte (CRA bit 6 mis puis remis à 0, compté par cia.cpp). keyboard.device
 * et les intros qui lisent SDR sans lire l'ICR (NOMAD) la produisent toutes deux.
 * Sans handshake après KBD_ACK_FRAMES trames (le MPU réel attend 143 ms), le même
 * code est réémis : rien n'est perdu tant que KS n'a pas activé le clavier.
 *
 * NON implémenté volontairement (démarrage simple) : la séquence power-up
 * 0xFD/0xFE. KS 1.3 accepte en général les keycodes directs. Si des touches ne
 * sont pas prises en compte au boot, l'émettre ici en PREMIER est le correctif.
 */
#include "kbd_amiga.h"
#include "a500.h"

#define KBD_FIFO       32
#define KBD_ACK_FRAMES 8          /* 160 ms > délai d'attente du handshake du MPU réel */

/* Reset clavier (HRM, keyboard-handshake-v34.md §5) : la 3e touche de Ctrl + Amiga + Amiga
 * devient l'avertissement $78. Sans handshake en 250 ms (machine plantée), /RESET tout de suite ;
 * sinon un 2e $78, dont keyboard.device tient le handshake (CRA bit 6) jusqu'au dernier reset
 * handler, 10 s au plus, puis /RESET. */
#define KBD_RESET_WARN      0x78
#define KBD_WARN1_FRAMES    13    /* 260 ms */
#define KBD_WARN2_FRAMES    500   /* 10 s */
#define KBD_CTRL            0x63
#define KBD_LAMIGA          0x66
#define KBD_RAMIGA          0x67

static uint8_t s_raw[KBD_FIFO];
static uint8_t s_down[KBD_FIFO];
static int s_head, s_tail, s_count;
static bool s_wait_ack;           /* code de tête émis, handshake attendu */
static int s_wait_frames;
static uint32_t s_hs_at_send;
static int s_held;                /* bits : Ctrl, Amiga gauche, Amiga droite enfoncées */
static int s_warn;                /* avertissements $78 en file ou émis (0 = pas de reset) */
static bool s_reset_req;

void kbd_amiga_init(void)
{
    s_head = s_tail = s_count = 0;
    s_wait_ack = false;
    s_held = 0;
    s_warn = 0;
    s_reset_req = false;
}

int kbd_amiga_reset_requested(void)
{
    if (!s_reset_req) return 0;
    s_reset_req = false;
    return 1;
}

static void enqueue(uint8_t rawcode, int down)
{
    if (s_count >= KBD_FIFO) {          /* plein : on jette le plus ancien */
        s_head = (s_head + 1) % KBD_FIFO;
        s_count--;
        s_wait_ack = false;             /* le code en attente d'acquittement vient de partir */
    }
    s_raw[s_tail]  = rawcode & 0x7F;
    s_down[s_tail] = down ? 1 : 0;
    s_tail = (s_tail + 1) % KBD_FIFO;
    s_count++;
}

void kbd_amiga_push(uint8_t rawcode, int down)
{
    rawcode &= 0x7F;
    const int bit = rawcode == KBD_CTRL ? 1 : rawcode == KBD_LAMIGA ? 2 : rawcode == KBD_RAMIGA ? 4 : 0;
    if (bit) s_held = down ? (s_held | bit) : (s_held & ~bit);
    if (s_warn) return;                     /* séquence de reset en cours : clavier muet */
    if (bit && down && s_held == 7) {
        s_warn = 1;
        enqueue(KBD_RESET_WARN, 1);
        return;
    }
    enqueue(rawcode, down);
}

static void system_reset(void)
{
    s_head = s_tail = s_count = 0;
    s_wait_ack = false;
    s_warn = 0;
    s_held = 0;
    s_reset_req = true;
}

static void send_head(void)
{
    uint8_t up  = s_down[s_head] ? 0 : 1;                       /* bit 0 = relâche */
    uint8_t sdr = (uint8_t)~(((s_raw[s_head] << 1) | up));      /* inversé (keyboard active-low) */
    s_hs_at_send  = cia_a_kbd_handshakes;
    s_wait_frames = 0;
    s_wait_ack    = true;
    cia_a_kbd_shift_in(sdr);
}

void kbd_amiga_step(void)
{
    const bool warn_head = s_warn && s_count == 1 && s_raw[s_head] == KBD_RESET_WARN;
    if (s_wait_ack) {
        if (cia_a_kbd_handshakes == s_hs_at_send) {
            ++s_wait_frames;
            if (warn_head) {                                      /* pas de réémission de $78 */
                if (s_wait_frames >= (s_warn == 1 ? KBD_WARN1_FRAMES : KBD_WARN2_FRAMES))
                    system_reset();
            } else if (s_wait_frames >= KBD_ACK_FRAMES) {
                send_head();                                      /* pas d'acquittement : réémission */
            }
            return;
        }
        s_wait_ack = false;                                       /* acquitté : code consommé */
        s_head = (s_head + 1) % KBD_FIFO;
        s_count--;
        if (warn_head) {
            if (s_warn == 2) { system_reset(); return; }          /* fin des reset handlers */
            s_warn = 2;
            enqueue(KBD_RESET_WARN, 1);                           /* 2e avertissement */
        }
    }
    if (s_count > 0) send_head();
}
