/* video_vga.cpp — Sortie VGA FabGL (64 couleurs RGB222).
 *
 * Remplace denise_cb()/flush_band()/le pilote ST7796 SPI de l'ancien .ino.
 *
 * Principe : le cœur émulateur appelle denise_line_cb(vpos, pixels, w) pour
 * chaque ligne rendue. On convertit chaque pixel (couleur OCS 12 bits) via une
 * LUT et on l'écrit DIRECTEMENT dans le framebuffer FabGL (RAM interne, relu en
 * continu par le DMA vidéo). Pas de shadow buffer ni de dirty-check : le
 * framebuffer EST l'image affichée et persiste entre les trames — ce qui
 * économise ~300 Ko de PSRAM par rapport au pilote TFT d'origine.
 *
 * Piège FabGL vérifié dans vgabasecontroller.h : les pixels d'une scanline sont
 * stockés en ordre entrelacé dans les mots DMA 32 bits ; l'octet du pixel X est
 * à l'offset (X ^ 2). On écrit donc row[x ^ 2], jamais row[x].
 */
#include "fabgl.h"
#include "platform_esp32.h"
#include "video_vga.h"
#include "a500.h"   /* coeur compile en C++ : liaison C++, PAS d'extern "C" ici */

/* confirmée côté cœur par video.cpp : indexée par vpos Amiga (0..311), remise à
 * true ici quand la ligne est posée à l'écran, ce dont dépend l'optimisation de
 * skip de denise_render_line(). */
extern bool g_row_onscreen[];

static fabgl::VGAController s_vga;
static uint8_t s_lut[4096];   /* OCS 12 bits -> octet pixel natif FabGL (RGB222 + sync) */

/* callback appelé par denise_render_line() pour chaque ligne video Amiga.
 * w = 320 (lores, 1:1) ou 640 (hires, décimation /2). pixels[x] & 0xFFF = couleur. */
static void denise_cb(int v, const uint16_t *pixels, int w)
{
    int dy = v - VGA32_VSTART;
    if (dy >= 0 && dy < VGA32_ACTIVE_H) {
        uint8_t *row = s_vga.getScanline(dy);
        if (w >= 640) {
            /* hires : on décime 640 -> 320 (1 pixel sur 2) */
            for (int x = 0; x < VGA32_ACTIVE_W; x++)
                row[x ^ 2] = s_lut[pixels[x * 2] & 0xFFF];
        } else {
            /* lores : 1:1, on borne si la ligne fournie est plus courte */
            for (int x = 0; x < VGA32_ACTIVE_W; x++)
                row[x ^ 2] = s_lut[pixels[x < w ? x : w - 1] & 0xFFF];
        }
    }
    /* ligne Amiga confirmée affichée (requis par le skip-calcul de video.cpp) */
    if (v >= 0 && v < 312)
        g_row_onscreen[v] = true;
}

void video_vga_init(void)
{
    s_vga.begin();                     /* broches VGA par défaut = câblage VGA32 */
    s_vga.setResolution(VGA32_MODELINE);

    /* LUT couleur : OCS 12 bits (4 bits/canal) -> RGB222 (2 bits/canal).
     * On garde les 2 bits de poids fort de chaque canal. createRawPixel ajoute
     * les bits de synchro : ne JAMAIS coder en dur l'ordre des bits FabGL. */
    for (int c = 0; c < 4096; c++) {
        uint8_t r = (c >> 8) & 0xF;
        uint8_t g = (c >> 4) & 0xF;
        uint8_t b =  c       & 0xF;
        s_lut[c] = s_vga.createRawPixel(fabgl::RGB222(r >> 2, g >> 2, b >> 2));
    }

    /* écran noir au démarrage */
    for (int y = 0; y < VGA32_ACTIVE_H; y++) {
        uint8_t *row = s_vga.getScanline(y);
        for (int x = 0; x < VGA32_ACTIVE_W; x++)
            row[x ^ 2] = s_lut[0];
    }

    denise_line_cb = denise_cb;
}
