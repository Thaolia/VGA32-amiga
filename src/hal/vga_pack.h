/* vga_pack.h — Écriture de 4 pixels VGA en un seul mot de 32 bits.
 *
 * Module pur (aucune dépendance Arduino) : testé sur hôte (tests/hal/test_vga_pack).
 * FabGL entrelace les octets de chaque scanline pour le DMA : le pixel x est à l'offset x ^ 2.
 * Pour x = 4k..4k+3, les octets du mot aligné à 4k (petit-boutiste) contiennent donc, dans
 * l'ordre des adresses, les pixels 4k+2, 4k+3, 4k, 4k+1.
 * Pourquoi : -mfix-esp32-psram-cache-issue ajoute un memw après chaque écriture ; un mot au
 * lieu de 4 octets divise ce coût par 4.
 */
#ifndef VGA_PACK_H
#define VGA_PACK_H

#include <stdint.h>

/* p0..p3 = octets des pixels 4k..4k+3, dans l'ordre de l'écran */
static inline __attribute__((always_inline))
uint32_t vga_pack4(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3)
{
    return (uint32_t)p2 | (uint32_t)p3 << 8 | (uint32_t)p0 << 16 | (uint32_t)p1 << 24;
}

#endif /* VGA_PACK_H */
