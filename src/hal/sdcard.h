/* sdcard.h — Disquettes ADF sur la carte microSD embarquee (FAT16/FAT32, pas exFAT).
 * Permet de changer de disquette sans reflasher (vs ADF embarque dans wb_adf.h).
 */
#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

/* Monte la SD (reste montee) et liste les .adf de la racine de taille DD
 * (901120 o), tries par nom. Retourne leur nombre (0 si carte absente). */
int sdcard_init(void);

int sdcard_adf_count(void);

/* Nom de fichier de l'ADF i (sans '/'), ou nullptr si i hors liste. */
const char *sdcard_adf_name(int i);

/* Index de l'ADF nomme name (casse ignoree, '/' initial tolere), ou -1. */
int sdcard_adf_find(const char *name);

/* Lit l'ADF i (901120 o) dans dst par blocs de 512 o. Ne monte PAS le disque :
 * l'appelant decide (drive_mount_ready). Utilisable depuis n'importe quelle tache. */
bool sdcard_read_adf(int i, uint8_t *dst);

#endif /* SDCARD_H */
