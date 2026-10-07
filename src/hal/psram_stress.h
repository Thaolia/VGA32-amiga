/* psram_stress.h — Validation d'une stratégie de contournement PSRAM (puce ESP32 rev1).
 *
 * Actif seulement avec VGA32_PSRAM_STRESS (env pio ttgo-vga32-stress). Deux détecteurs :
 *  - une tâche sur le cœur 1 martèle un tampon PSRAM (écritures 8/16/32 bits mélangées,
 *    lecture-modification-écriture, relecture) pendant que le cœur 0 émule : l'accès concurrent
 *    des deux cœurs au cache est ce qui déclenche le défaut rev1 ;
 *  - psram_stress_crc() : CRC32 de la chip RAM et de la Fast RAM, à comparer entre deux builds
 *    aux mêmes trames (l'émulation sans entrée est déterministe).
 */
#ifndef PSRAM_STRESS_H
#define PSRAM_STRESS_H

#include <stdint.h>

void     psram_stress_start(void);          /* crée la tâche (cœur 1) */
uint32_t psram_stress_passes(void);
uint32_t psram_stress_errors(void);
uint32_t psram_stress_crc(void);            /* chip RAM + cartes Zorro configurées */

#endif /* PSRAM_STRESS_H */
