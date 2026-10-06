/* audio_dac.cpp — Audio via DAC interne GPIO25 (FabGL SoundGenerator).
 *
 * PHASE 0/1 : stub no-op. Le boot Workbench n'a pas besoin de son.
 *
 * PHASE 3 : attacher au fabgl::SoundGenerator (DAC GPIO25, I2S0 — indépendant de
 * l'I2S1 de la VGA) un WaveformGenerator custom dont getSample() dépile le ring
 * Paula (paula_ring_pop), mixe en mono et ré-échantillonne vers la fréquence DAC.
 * Pré-requis d'ordre : paula_reset() doit avoir eu lieu avant le 1er getSample().
 */
#include "audio_dac.h"
#include "a500.h"

void audio_dac_init(void)
{
    /* Phase 3 : SoundGenerator + WaveformGenerator -> paula_ring_pop(). */
}
