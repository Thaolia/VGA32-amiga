/* sdcard.cpp — Chargement ADF depuis la carte microSD embarquee (SPI).
 *
 * Brochage v1.4 (schema officiel, cf. docs/HARDWARE.md) : CS=13 SCK=14 MOSI=12
 * MISO=2, sur HSPI. Indépendant de la VGA (I2S1), du PS/2 (ULP) et du bus PSRAM.
 *
 * Lecture par blocs de 512 octets (regle projet SD) directement dans le buffer
 * PSRAM du drive (drive_alloc_adf), pas de double tampon.
 */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>

#include "sdcard.h"
#include "platform_esp32.h"
#include "a500.h"

bool sdcard_load_adf(const char *path)
{
    static SPIClass sdspi(HSPI);
    sdspi.begin(VGA32_SD_SCK, VGA32_SD_MISO, VGA32_SD_MOSI, VGA32_SD_CS);

    if (!SD.begin(VGA32_SD_CS, sdspi, 20000000)) {
        Serial.println("[SD] pas de carte ou montage echoue");
        return false;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[SD] fichier absent: %s\n", path);
        SD.end();
        return false;
    }
    size_t sz = f.size();
    if (sz != VGA32_ADF_SIZE) {
        Serial.printf("[SD] taille ADF %u != %u (DD 880 Ko attendu)\n",
                      (unsigned)sz, (unsigned)VGA32_ADF_SIZE);
        f.close();
        SD.end();
        return false;
    }

    uint8_t *dst = drive_alloc_adf();
    if (!dst) {
        Serial.println("[SD] alloc buffer ADF PSRAM echouee");
        f.close();
        SD.end();
        return false;
    }

    /* lecture par chunks de 512 o max, directement en PSRAM */
    size_t off = 0;
    while (off < VGA32_ADF_SIZE) {
        size_t want = VGA32_ADF_SIZE - off;
        if (want > 512) want = 512;
        int n = f.read(dst + off, want);
        if (n <= 0) {
            Serial.printf("[SD] lecture interrompue a l'offset %u\n", (unsigned)off);
            f.close();
            SD.end();
            return false;
        }
        off += (size_t)n;
    }
    f.close();
    SD.end();

    drive_mount_ready();
    Serial.printf("[SD] ADF monte depuis %s (%u o)\n", path, (unsigned)VGA32_ADF_SIZE);
    return true;
}
