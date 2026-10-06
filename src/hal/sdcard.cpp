/* sdcard.cpp — Disquettes ADF sur la carte microSD embarquee (SPI).
 *
 * Brochage v1.4 (schema officiel, cf. docs/HARDWARE.md) : CS=13 SCK=14 MOSI=12
 * MISO=2, sur HSPI. Indépendant de la VGA (I2S1), du PS/2 (ULP) et du bus PSRAM.
 *
 * La SD reste montee apres le boot : le bouton IO36 (disk_switch) relit un autre
 * ADF a la demande. FatFs du framework : FAT16/FAT32 seulement (FF_FS_EXFAT = 0),
 * noms longs actives (LFN 255).
 *
 * Lecture par blocs de 512 octets (regle projet SD) directement dans le buffer
 * de destination, pas de double tampon.
 */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>
#include <cstdlib>
#include <strings.h>

#include "sdcard.h"
#include "platform_esp32.h"

#define SD_MAX_ADF   32      /* entrees listees (au-dela : ignorees, loggees) */
#define SD_NAME_MAX  128     /* nom de fichier max, '\0' inclus */

static char s_names[SD_MAX_ADF][SD_NAME_MAX];
static int  s_count = 0;
static bool s_mounted = false;

static bool has_adf_ext(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && strcasecmp(n + l - 4, ".adf") == 0;
}

static int cmp_name(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

int sdcard_init(void)
{
    static SPIClass sdspi(HSPI);
    sdspi.begin(VGA32_SD_SCK, VGA32_SD_MISO, VGA32_SD_MOSI, VGA32_SD_CS);

    s_count = 0;
    if (!SD.begin(VGA32_SD_CS, sdspi, 20000000)) {
        Serial.println("[SD] pas de carte ou montage echoue (FAT16/FAT32 requis)");
        return 0;
    }
    s_mounted = true;

    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("[SD] racine illisible");
        return 0;
    }
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        const char *n = f.name();
        /* "._xxx" : métadonnées macOS, pas des disquettes */
        if (f.isDirectory() || n[0] == '.' || !has_adf_ext(n)) continue;
        if (f.size() != VGA32_ADF_SIZE) {
            Serial.printf("[SD] ignore %s : %u o (DD 901120 attendu)\n", n, (unsigned)f.size());
            continue;
        }
        if (strlen(n) >= SD_NAME_MAX) { Serial.printf("[SD] ignore (nom trop long) : %s\n", n); continue; }
        if (s_count >= SD_MAX_ADF)     { Serial.printf("[SD] ignore (> %d ADF) : %s\n", SD_MAX_ADF, n); continue; }
        strcpy(s_names[s_count++], n);
    }
    root.close();

    qsort(s_names, s_count, SD_NAME_MAX, cmp_name);
    Serial.printf("[SD] %d ADF trouve(s)\n", s_count);
    for (int i = 0; i < s_count; i++) Serial.printf("[SD]   %d: %s\n", i, s_names[i]);
    return s_count;
}

int sdcard_adf_count(void) { return s_count; }

const char *sdcard_adf_name(int i)
{
    return (i >= 0 && i < s_count) ? s_names[i] : nullptr;
}

int sdcard_adf_find(const char *name)
{
    if (name[0] == '/') name++;
    for (int i = 0; i < s_count; i++)
        if (strcasecmp(s_names[i], name) == 0) return i;
    return -1;
}

bool sdcard_read_adf(int i, uint8_t *dst)
{
    if (!s_mounted || i < 0 || i >= s_count || !dst) return false;

    char path[SD_NAME_MAX + 1];
    path[0] = '/';
    strcpy(path + 1, s_names[i]);

    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[SD] ouverture impossible : %s\n", path);
        return false;
    }
    if (f.size() != VGA32_ADF_SIZE) {
        Serial.printf("[SD] taille ADF %u != %u : %s\n",
                      (unsigned)f.size(), (unsigned)VGA32_ADF_SIZE, path);
        f.close();
        return false;
    }

    /* lecture par chunks de 512 o max, directement dans dst */
    size_t off = 0;
    while (off < VGA32_ADF_SIZE) {
        size_t want = VGA32_ADF_SIZE - off;
        if (want > 512) want = 512;
        int n = f.read(dst + off, want);
        if (n <= 0) {
            Serial.printf("[SD] lecture interrompue a l'offset %u : %s\n", (unsigned)off, path);
            f.close();
            return false;
        }
        off += (size_t)n;
    }
    f.close();
    Serial.printf("[SD] ADF lu : %s\n", path);
    return true;
}
