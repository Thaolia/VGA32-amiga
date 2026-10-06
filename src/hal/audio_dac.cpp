/* audio_dac.cpp — Audio via DAC interne GPIO25 (FabGL SoundGenerator). Phase 3.
 *
 * Remplace l'I2S/MAX98357A d'origine : la VGA32 sort le son sur le DAC interne
 * GPIO25 (ampli NS4150 + jack + HP embarqués, cf. docs/HARDWARE.md).
 *
 * Un WaveformGenerator custom tire les échantillons du ring Paula (44100 Hz
 * stéréo 16 bits) au rythme imposé par la SoundGenerator (44100 Hz aussi -> pas
 * de ré-échantillonnage), les downmixe en mono 8 bits signés attendus par le DAC.
 *
 * ⚠ Ordre : paula_reset() (alloc du ring) DOIT précéder audio_dac_init() ->
 * appelé depuis emu_task après le reset du chipset, jamais depuis setup().
 */
#include "fabgl.h"
#include "audio_dac.h"
#include "platform_esp32.h"
#include "a500.h"

/* générateur branché au ring Paula */
class PaulaGenerator : public fabgl::WaveformGenerator {
public:
    void setFrequency(int value) { (void)value; }   /* sans objet pour un flux */

    int getSample() {
        if (!enabled()) return 0;
        int16_t l = 0, r = 0;
        paula_ring_pop(&l, &r);              /* 0 si vide -> silence */
        /* downmix mono + 16->8 bits signés (plage ±127) */
        return ((int)l + (int)r) >> 9;
    }
};

static fabgl::SoundGenerator *s_sg = nullptr;
static PaulaGenerator s_paula;

void audio_dac_init(void)
{
    if (s_sg) return;                        /* idempotent */
    s_sg = new fabgl::SoundGenerator(44100, GPIO_NUM_25, fabgl::SoundGenMethod::DAC);
    s_paula.enable(true);
    s_sg->attach(&s_paula);
    s_sg->play(true);
}
