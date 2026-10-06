/* disk_switch.cpp — Changement de disquette DF0 au bouton IO36 (cf. disk_switch.h).
 *
 * Répartition des tâches :
 *  - emu_task (disk_switch_poll) : bouton, OSD, drive_eject/drive_mount_ready. Le
 *    lecteur n'est manipulé que par la tâche qui exécute le cœur : pas de course
 *    avec la DMA disque émulée.
 *  - tâche "adfload" (cœur VGA, priorité basse) : lecture SD dans le buffer ADF,
 *    pendant que DF0 est éjecté (drive_adf() == NULL, le cœur n'y touche pas).
 * Les deux files FreeRTOS servent aussi de barrière mémoire entre les cœurs.
 */
#include <Arduino.h>
#include "fabgl.h"
#include "disk_switch.h"
#include "disk_select.h"
#include "sdcard.h"
#include "video_vga.h"
#include "platform_esp32.h"
#include "a500.h"

static ds_t          s_ds;
static QueueHandle_t s_req;      /* emu_task -> chargeur : index ADF */
static QueueHandle_t s_res;      /* chargeur -> emu_task : 1 succès, 0 échec */
static bool          s_busy;     /* lecture en cours (DF0 éjecté) */
static int           s_loading;  /* index en cours de lecture */
static int           s_next = -1;/* index validé pendant une lecture, à enchaîner */
static uint8_t      *s_buf;      /* buffer ADF du lecteur (PSRAM), obtenu dans setup() */

static void loader_task(void *arg)
{
    (void)arg;
    for (;;) {
        int idx;
        if (xQueueReceive(s_req, &idx, portMAX_DELAY) != pdTRUE) continue;
        int ok = sdcard_read_adf(idx, s_buf) ? 1 : 0;
        xQueueSend(s_res, &ok, portMAX_DELAY);
    }
}

/* nom sans extension, préfixé du lecteur */
static void show_name(const char *prefix, int idx)
{
    char buf[48];
    const char *n = sdcard_adf_name(idx);
    int len = (int)strlen(n) - 4;                 /* liste filtrée : finit par ".adf" */
    snprintf(buf, sizeof buf, "%s%.*s", prefix, len, n);
    video_vga_osd_show(buf, VGA32_OSD_MS);
}

static void start_load(int idx)
{
    drive_eject();                                /* avant toute écriture du buffer */
    s_busy = true;
    s_loading = idx;
    xQueueSend(s_req, &idx, portMAX_DELAY);
    Serial.printf("[DSW] chargement DF0 <- %s\n", sdcard_adf_name(idx));
}

void disk_switch_init(int cur)
{
    pinMode(VGA32_DISK_BTN_GPIO, INPUT);          /* pull-up 10K externe (R9) */
    ds_init(&s_ds, sdcard_adf_count(), cur);
    s_buf = drive_alloc_adf();                    /* déjà alloué au boot dans le cas normal */
    s_req = xQueueCreate(1, sizeof(int));
    s_res = xQueueCreate(1, sizeof(int));
    if (!s_buf || !s_req || !s_res ||
        xTaskCreatePinnedToCore(loader_task, "adfload", LOADER_TASK_STACK, nullptr, 1,
                                nullptr, fabgl::CoreUsage::busiestCore()) != pdPASS) {
        Serial.println("ERREUR: init changement de disquette (file/tache)");
        s_ds.count = 0;                           /* bouton inactif : liste vide */
    }
}

void disk_switch_poll(void)
{
    int idx;
    switch (ds_update(&s_ds, millis(), digitalRead(VGA32_DISK_BTN_GPIO) == LOW, &idx)) {
    case DS_SHOW:
        Serial.printf("[DSW] bouton -> selection %d (%s)\n", idx, idx < 0 ? "liste vide" : sdcard_adf_name(idx));
        if (idx < 0) video_vga_osd_show("Aucun .adf sur la SD", VGA32_OSD_MS);
        else         show_name("DF0: ", idx);
        break;
    case DS_LOAD:
        if (s_busy) s_next = idx;                 /* enchaîné à la fin de la lecture */
        else        start_load(idx);
        break;
    case DS_NONE:
        break;
    }

    int ok;
    if (s_busy && xQueueReceive(s_res, &ok, 0) == pdTRUE) {
        s_busy = false;
        if (s_next >= 0) {                        /* sélection plus récente : on la lit */
            int n = s_next;
            s_next = -1;
            start_load(n);
        } else if (ok) {
            drive_mount_ready();
        } else {
            /* buffer partiellement écrasé : DF0 reste vide */
            ds_load_failed(&s_ds, -1);
            show_name("Erreur SD: ", s_loading);
        }
    }
}
