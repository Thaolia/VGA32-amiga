/* video_vga.h — Sortie vidéo VGA (FabGL) pour le portage VGA32.
 * Remplace le pilote SPI/ST7796 de l'ancien a500_esp32.ino.
 */
#ifndef VIDEO_VGA_H
#define VIDEO_VGA_H

#include <stdint.h>

/* Initialise le contrôleur VGA FabGL (64 couleurs), construit la LUT couleur
 * OCS 12 bits -> RGB222, et branche le callback Denise (denise_line_cb).
 * À appeler dans setup(), AVANT de démarrer l'émulateur. */
void video_vga_init(void);

/* Surimpression d'une ligne de texte (8x8, ASCII, tronquée à 39 caractères)
 * en bas de l'écran pendant ms millisecondes. Remplace le message en cours.
 * À appeler depuis emu_task uniquement (même tâche que le callback Denise). */
void video_vga_osd_show(const char *text, uint32_t ms);

/* Retire la surimpression à échéance. À appeler une fois par trame depuis emu_task. */
void video_vga_osd_tick(void);

#if VGA32_PROF
/* Cycles CPU passés dans le callback Denise (conversion -> framebuffer VGA) et nombre de
 * lignes écrites depuis le dernier appel ; remet les compteurs à zéro. */
void video_vga_prof_take(uint32_t *cycles, uint32_t *lines);
#endif

#endif /* VIDEO_VGA_H */
