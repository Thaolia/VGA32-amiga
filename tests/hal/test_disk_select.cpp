/* test_disk_select.cpp — Tests hôte de la logique du bouton de changement de disquette
 * (src/hal/disk_select) : anti-rebond, sélection circulaire, validation différée. */
#include <stdio.h>
#include "disk_select.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t t_press;   /* instant du dernier appui simulé */

/* appui franc : niveau bas, relâché à la trame suivante (~40 ms par trame), puis
 * pause : deux appuis successifs sont espacés de 200 ms (> DS_DEBOUNCE_MS) */
static ds_action_t press(ds_t *d, uint32_t *t, int *idx)
{
    t_press = *t;
    ds_action_t a = ds_update(d, *t, 1, idx);
    int dummy;
    CHECK(ds_update(d, *t + 40, 0, &dummy) == DS_NONE);
    *t += 200;
    return a;
}

int main(void)
{
    ds_t d;
    int idx;
    uint32_t t = 1000;

    /* 1. un appui -> affiche le disque suivant, chargement après DS_COMMIT_MS sans appui */
    ds_init(&d, 3, 0);
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 1);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS - 100, 0, &idx) == DS_NONE);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS, 0, &idx) == DS_LOAD && idx == 1);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS + 40, 0, &idx) == DS_NONE);   /* une seule fois */
    t += DS_COMMIT_MS + 80;

    /* 2. appuis rapprochés : défilement circulaire, un seul chargement (le dernier) */
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 2);
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 0);                  /* bouclage */
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS, 0, &idx) == DS_LOAD && idx == 0);
    t += DS_COMMIT_MS + 80;

    /* 3. tour complet revenant au disque courant -> pas de rechargement */
    ds_init(&d, 2, 1);
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 0);
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 1);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS, 0, &idx) == DS_NONE);
    t += DS_COMMIT_MS + 80;

    /* 4. bouton maintenu = un seul appui (détection de front) */
    ds_init(&d, 3, 0);
    CHECK(ds_update(&d, t, 1, &idx) == DS_SHOW && idx == 1);
    CHECK(ds_update(&d, t + 40, 1, &idx) == DS_NONE);
    CHECK(ds_update(&d, t + 80, 1, &idx) == DS_NONE);
    t += 120;
    CHECK(ds_update(&d, t, 0, &idx) == DS_NONE);
    t += 40;

    /* 5. rebond : nouveau front trop proche du précédent -> ignoré */
    ds_init(&d, 3, 0);
    t += 1000;
    CHECK(ds_update(&d, t, 1, &idx) == DS_SHOW && idx == 1);
    CHECK(ds_update(&d, t + 5, 0, &idx) == DS_NONE);
    CHECK(ds_update(&d, t + 10, 1, &idx) == DS_NONE);                   /* < DS_DEBOUNCE_MS */
    t += DS_COMMIT_MS + 100;

    /* 6. aucun ADF : l'appui signale la liste vide (idx = -1), jamais de chargement */
    ds_init(&d, 0, -1);
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == -1);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS, 0, &idx) == DS_NONE);

    /* 7. démarrage sur l'ADF embarqué (cur = -1) : 1er appui -> 1er ADF de la SD */
    ds_init(&d, 2, -1);
    t += DS_COMMIT_MS + 100;
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 0);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS, 0, &idx) == DS_LOAD && idx == 0);

    /* 8. échec de chargement : ds_load_failed restaure le disque courant précédent */
    ds_init(&d, 3, 0);
    t += DS_COMMIT_MS + 100;
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 1);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS, 0, &idx) == DS_LOAD && idx == 1);
    ds_load_failed(&d, 0);
    t += DS_COMMIT_MS + 100;
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 1);   /* repart de 0, pas de 1 */

    /* 9. compteur ms qui déborde (uint32) : la validation différée marche encore */
    ds_init(&d, 3, 0);
    t = 0xFFFFFFFFu - 500;
    CHECK(press(&d, &t, &idx) == DS_SHOW && idx == 1);
    CHECK(ds_update(&d, t_press + DS_COMMIT_MS, 0, &idx) == DS_LOAD && idx == 1);

    if (fails) { printf("%d echec(s)\n", fails); return 1; }
    printf("disk_select : OK\n");
    return 0;
}
