/* test_vga_pack.cpp — Tests hôte de vga_pack4 (src/hal/vga_pack.h) : le mot de 32 bits doit
 * produire en mémoire exactement les octets de la boucle de référence row[x ^ 2] = p[x].
 * Hôte petit-boutiste requis (x86/ARM), comme l'ESP32. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "vga_pack.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    const uint16_t one = 1;
    CHECK(*(const uint8_t *)&one == 1);          /* hôte petit-boutiste */

    enum { W = 320 };
    uint8_t px[W], ref[W], got[W];
    srand(12345);
    for (int run = 0; run < 100; run++) {
        for (int x = 0; x < W; x++) px[x] = (uint8_t)rand();
        for (int x = 0; x < W; x++) ref[x ^ 2] = px[x];          /* référence (video_vga.cpp) */
        for (int k = 0; k < W / 4; k++) {
            uint32_t w = vga_pack4(px[4 * k], px[4 * k + 1], px[4 * k + 2], px[4 * k + 3]);
            memcpy(&got[4 * k], &w, 4);
        }
        CHECK(memcmp(ref, got, W) == 0);
    }
    /* cas lisible : pixels 0,1,2,3 -> octets en mémoire 2,3,0,1 */
    uint32_t w = vga_pack4(0xA0, 0xA1, 0xA2, 0xA3);
    uint8_t b[4];
    memcpy(b, &w, 4);
    CHECK(b[0] == 0xA2 && b[1] == 0xA3 && b[2] == 0xA0 && b[3] == 0xA1);

    printf("vga_pack : %s\n", fails ? "ECHEC" : "OK");
    return fails ? 1 : 0;
}
