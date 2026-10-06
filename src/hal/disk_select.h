/* disk_select.h — Logique du bouton de changement de disquette (IO36).
 *
 * Module pur (aucune dépendance Arduino) : testé sur hôte (tests/hal/).
 * Chaque appui fait défiler la sélection (circulaire) et demande son affichage ;
 * la disquette n'est chargée que DS_COMMIT_MS après le DERNIER appui, pour pouvoir
 * défiler plusieurs images sans les charger toutes depuis la SD.
 */
#ifndef DISK_SELECT_H
#define DISK_SELECT_H

#include <stdint.h>

#define DS_DEBOUNCE_MS  150u    /* écart min. entre deux appuis acceptés */
#define DS_COMMIT_MS   1500u    /* délai sans appui avant de charger la sélection */

typedef enum { DS_NONE, DS_SHOW, DS_LOAD } ds_action_t;

typedef struct {
    int      count;       /* nombre d'ADF disponibles */
    int      cur;         /* disque inséré (-1 = ADF embarqué / aucun de la liste) */
    int      sel;         /* sélection en cours de défilement */
    int      pending;     /* sélection affichée, pas encore chargée */
    int      prev_down;
    uint32_t last_press;  /* ms du dernier appui accepté */
    int      any_press;
} ds_t;

void ds_init(ds_t *d, int count, int cur);

/* À appeler à chaque trame. down = bouton enfoncé (niveau logique déjà inversé).
 * DS_SHOW : afficher *idx (-1 = liste vide) ; DS_LOAD : charger *idx. */
ds_action_t ds_update(ds_t *d, uint32_t now_ms, int down, int *idx);

/* Le chargement demandé a échoué : le disque inséré redevient prev_cur. */
void ds_load_failed(ds_t *d, int prev_cur);

#endif /* DISK_SELECT_H */
