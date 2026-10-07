/* zorro.h — Cartes de Fast RAM Zorro II en autoconfig (expansion A500).
 *
 * Module pur (aucune dépendance Arduino) : testé sur hôte (tests/hal/test_zorro).
 * Implémenté d'après la spécification AutoConfig publique (Amiga Hardware Reference Manual) :
 * tant qu'elle n'est pas configurée, la carte courante répond en $E80000-$E8FFFF par des
 * quartets (poids fort de chaque octet pair) ; le système écrit sa base en $4A (A19-A16)
 * puis $48 (A23-A20), ce qui la configure, et la carte suivante de la chaîne apparaît.
 */
#ifndef ZORRO_H
#define ZORRO_H

#include <stdint.h>

#define ZORRO_MAX_BOARDS 4
#define ZORRO_CFG_BASE   0xE80000u

/* Ajoute une carte RAM à la chaîne. size : puissance de 2 de 64 Ko à 8 Mo.
 * Retourne 0, ou -1 si la taille est invalide ou la chaîne pleine. */
int zorro_add_ram(uint8_t *mem, uint32_t size);

/* Reset Amiga : toutes les cartes redeviennent non configurées (le Kickstart les reconfigure). */
void zorro_reset(void);

/* Table des pages de 64 Ko de l'espace 24 bits : pointeur vers le début de la page en Fast RAM,
 * ou NULL. Chemin chaud : uint8_t *p = zorro_page[a >> 16]; if (p) ... p[a & 0xFFFF]. */
extern uint8_t *zorro_page[256];

/* Accès à l'espace de configuration ($E80000-$E8FFFF). Retourne 1 si une carte non configurée
 * y répond (et *v est rempli pour une lecture), 0 sinon (zone vide). */
int zorro_cfg_read8(uint32_t a, uint8_t *v);
int zorro_cfg_write8(uint32_t a, uint8_t v);

/* Base attribuée à la carte i (0 si non configurée) et sa taille, pour les logs. */
uint32_t zorro_board_base(int i);
uint32_t zorro_board_size(int i);
int      zorro_board_count(void);

#endif /* ZORRO_H */
