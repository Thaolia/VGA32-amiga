/* audio_dac.h — Sortie audio via DAC interne GPIO25 (FabGL SoundGenerator).
 * Remplace l'I2S/MAX98357A de l'ancien .ino (la VGA32 n'a pas de DAC I2S externe).
 * Phase 3.
 */
#ifndef AUDIO_DAC_H
#define AUDIO_DAC_H

/* Initialise la sortie audio DAC. Stub no-op en Phase 0/1 (boot silencieux OK). */
void audio_dac_init(void);

#endif /* AUDIO_DAC_H */
