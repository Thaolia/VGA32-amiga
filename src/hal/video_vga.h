/* video_vga.h — Sortie vidéo VGA (FabGL) pour le portage VGA32.
 * Remplace le pilote SPI/ST7796 de l'ancien a500_esp32.ino.
 */
#ifndef VIDEO_VGA_H
#define VIDEO_VGA_H

/* Initialise le contrôleur VGA FabGL (64 couleurs), construit la LUT couleur
 * OCS 12 bits -> RGB222, et branche le callback Denise (denise_line_cb).
 * À appeler dans setup(), AVANT de démarrer l'émulateur. */
void video_vga_init(void);

#endif /* VIDEO_VGA_H */
