/* disk_select.cpp — Logique du bouton de changement de disquette (cf. disk_select.h).
 *
 * Les durées sont comparées par soustraction non signée (now - last) : correct
 * même quand le compteur ms 32 bits déborde (~49 jours).
 */
#include "disk_select.h"

void ds_init(ds_t *d, int count, int cur)
{
    d->count = count;
    d->cur = cur;
    d->sel = cur;
    d->pending = 0;
    d->prev_down = 0;
    d->last_press = 0;
    d->any_press = 0;
}

ds_action_t ds_update(ds_t *d, uint32_t now_ms, int down, int *idx)
{
    int edge = down && !d->prev_down;
    d->prev_down = down;

    if (edge && (!d->any_press || now_ms - d->last_press >= DS_DEBOUNCE_MS)) {
        d->any_press = 1;
        d->last_press = now_ms;
        if (d->count <= 0) { *idx = -1; return DS_SHOW; }
        int from = d->pending ? d->sel : d->cur;
        d->sel = (from + 1) % d->count;          /* cur = -1 -> premier de la liste */
        d->pending = 1;
        *idx = d->sel;
        return DS_SHOW;
    }

    if (d->pending && now_ms - d->last_press >= DS_COMMIT_MS) {
        d->pending = 0;
        if (d->sel == d->cur) return DS_NONE;     /* tour complet : rien à recharger */
        d->cur = d->sel;
        *idx = d->cur;
        return DS_LOAD;
    }
    return DS_NONE;
}

void ds_load_failed(ds_t *d, int prev_cur)
{
    d->cur = prev_cur;
    d->sel = prev_cur;
}
