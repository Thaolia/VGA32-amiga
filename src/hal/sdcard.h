/* sdcard.h — Chargement d'une disquette ADF depuis la carte microSD embarquee.
 * Permet de changer de disquette sans reflasher (vs ADF embarque dans wb_adf.h).
 */
#ifndef SDCARD_H
#define SDCARD_H

/* Monte la SD, lit <path> (ADF DD de 901120 o) dans le buffer PSRAM du drive,
 * et marque le disque present. Retourne true si monte, false sinon (carte
 * absente, fichier absent, mauvaise taille) -> l'appelant peut basculer sur
 * l'ADF embarque. */
bool sdcard_load_adf(const char *path);

#endif /* SDCARD_H */
