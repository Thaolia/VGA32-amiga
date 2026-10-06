/* disk_switch.h — Changement de disquette DF0 au bouton IO36, nom affiché à l'écran.
 *
 * Chaque appui affiche le nom de l'ADF suivant de la SD (VGA32_OSD_MS) ; l'ADF est
 * inséré DS_COMMIT_MS après le dernier appui (logique pure : disk_select).
 * Le chargement SD tourne dans une tâche dédiée, hors du cœur émulateur :
 * DF0 est éjecté pendant la lecture, puis réinséré (/CHNG -> trackdisk le voit).
 */
#ifndef DISK_SWITCH_H
#define DISK_SWITCH_H

/* cur = index SD de l'ADF inséré au boot (-1 : ADF embarqué ou aucun).
 * À appeler dans setup(), après sdcard_init() et le montage du disque de boot. */
void disk_switch_init(int cur);

/* Lit le bouton, pilote l'OSD et les chargements. Une fois par trame, depuis emu_task. */
void disk_switch_poll(void);

#endif /* DISK_SWITCH_H */
