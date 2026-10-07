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
#include "vga_pack.h"
#include "a500.h"   /* coeur compile en C++ : liaison C++, PAS d'extern "C" ici */

/* confirmée côté cœur par video.cpp : indexée par vpos Amiga (0..311), remise à
 * true ici quand la ligne est posée à l'écran, ce dont dépend l'optimisation de
 * skip de denise_render_line(). */
extern bool g_row_onscreen[];

static fabgl::VGAController s_vga;
static uint8_t s_lut[4096];   /* OCS 12 bits -> octet pixel natif FabGL (RGB222 + sync) */
static bool    s_row32;       /* scanlines alignées sur 4 : écriture par mots (vga_pack4) */

/* ---- Surimpression (OSD) ----
 * Le cœur saute les lignes inchangées : un texte posé une fois reste donc affiché,
 * et une ligne redessinée par Denise l'efface. On garde une copie des pixels Amiga
 * SOUS le cadre (mise à jour par denise_cb) : redessiner le texte par-dessus à
 * chaque ligne rendue, puis restaurer exactement l'image à l'échéance.
 * Tout se passe dans emu_task (OSD + callback Denise) : pas de verrou. */
#define OSD_H        12                       /* 8 lignes de glyphe + 2 de marge haut/bas */
#define OSD_MAXCH    39                       /* (39 * 8 + 8) = 320 px de large */
static_assert(VGA32_OSD_Y + OSD_H <= VGA32_ACTIVE_H, "bandeau OSD hors framebuffer");
static char     s_osd_text[OSD_MAXCH + 1];
static int      s_osd_len;
static int      s_osd_x0, s_osd_w;            /* cadre en pixels */
static bool     s_osd_on;
static uint32_t s_osd_until;                  /* millis() d'échéance */
static uint8_t  s_osd_bak[OSD_H][VGA32_ACTIVE_W];

static void IRAM_ATTR osd_draw_row(int r)
{
    uint8_t *row = s_vga.getScanline(VGA32_OSD_Y + r);
    uint8_t bg = s_lut[0x000], fg = s_lut[0xFFF];
    for (int x = 0; x < s_osd_w; x++) row[(s_osd_x0 + x) ^ 2] = bg;
    int gy = r - 2;
    if (gy < 0 || gy >= 8) return;
    const uint8_t *font = fabgl::FONT_8x8.data;
    for (int c = 0; c < s_osd_len; c++) {
        uint8_t bits = font[(uint8_t)s_osd_text[c] * 8 + gy];
        int x = s_osd_x0 + 4 + c * 8;
        for (int b = 0; b < 8; b++)
            if (bits & (0x80 >> b)) row[(x + b) ^ 2] = fg;
    }
}

static void osd_restore(void)
{
    for (int r = 0; r < OSD_H; r++) {
        uint8_t *row = s_vga.getScanline(VGA32_OSD_Y + r);
        for (int x = 0; x < s_osd_w; x++) row[(s_osd_x0 + x) ^ 2] = s_osd_bak[r][x];
    }
    s_osd_on = false;
}

void video_vga_osd_show(const char *text, uint32_t ms)
{
    if (s_osd_on) osd_restore();
    s_osd_len = 0;
    for (; text[s_osd_len] && s_osd_len < OSD_MAXCH; s_osd_len++) {
        char c = text[s_osd_len];
        s_osd_text[s_osd_len] = (c >= 32 && c < 127) ? c : '?';
    }
    if (text[s_osd_len]) s_osd_text[OSD_MAXCH - 1] = '~';   /* tronqué */
    s_osd_text[s_osd_len] = 0;
    s_osd_w = s_osd_len * 8 + 8;
    s_osd_x0 = (VGA32_ACTIVE_W - s_osd_w) / 2;
    for (int r = 0; r < OSD_H; r++) {
        uint8_t *row = s_vga.getScanline(VGA32_OSD_Y + r);
        for (int x = 0; x < s_osd_w; x++) s_osd_bak[r][x] = row[(s_osd_x0 + x) ^ 2];
        osd_draw_row(r);
    }
    s_osd_until = millis() + ms;
    s_osd_on = true;
}

void video_vga_osd_tick(void)
{
    if (s_osd_on && (int32_t)(millis() - s_osd_until) >= 0) osd_restore();
}

/* callback appelé par denise_render_line() pour chaque ligne video Amiga.
 * w = 320 (lores, 1:1) ou 640 (hires, décimation /2). pixels[x] & 0xFFF = couleur. */
#if VGA32_PROF
static uint32_t s_prof_cb_cyc, s_prof_cb_lines;
void video_vga_prof_take(uint32_t *cycles, uint32_t *lines)
{
    *cycles = s_prof_cb_cyc;  s_prof_cb_cyc = 0;
    *lines  = s_prof_cb_lines; s_prof_cb_lines = 0;
}
#endif

static void IRAM_ATTR denise_cb(int v, const uint16_t *pixels, int w)
{
#if VGA32_PROF
    uint32_t prof_t0 = prof_ccount();
#endif
    int dy = v - VGA32_VSTART;
    if (dy >= 0 && dy < VGA32_ACTIVE_H) {
        uint8_t *row = s_vga.getScanline(dy);
        if (s_row32 && w >= 320) {
            /* 4 pixels par mot de 32 bits : 1 memw au lieu de 4 */
            uint32_t *row32 = (uint32_t *)row;
            const int step = (w >= 640) ? 2 : 1;      /* hires : 1 pixel sur 2 */
            for (int k = 0; k < VGA32_ACTIVE_W / 4; k++) {
                const uint16_t *p = pixels + 4 * k * step;
                row32[k] = vga_pack4(s_lut[p[0] & 0xFFF], s_lut[p[step] & 0xFFF],
                                     s_lut[p[2 * step] & 0xFFF], s_lut[p[3 * step] & 0xFFF]);
            }
        } else if (w >= 640) {
            /* hires : on décime 640 -> 320 (1 pixel sur 2) */
            for (int x = 0; x < VGA32_ACTIVE_W; x++)
                row[x ^ 2] = s_lut[pixels[x * 2] & 0xFFF];
        } else {
            /* lores : 1:1, on borne si la ligne fournie est plus courte */
            for (int x = 0; x < VGA32_ACTIVE_W; x++)
                row[x ^ 2] = s_lut[pixels[x < w ? x : w - 1] & 0xFFF];
        }
        int r = dy - VGA32_OSD_Y;
        if (s_osd_on && r >= 0 && r < OSD_H) {
            for (int x = 0; x < s_osd_w; x++) s_osd_bak[r][x] = row[(s_osd_x0 + x) ^ 2];
            osd_draw_row(r);
        }
    }
    /* ligne Amiga confirmée affichée (requis par le skip-calcul de video.cpp) */
    if (v >= 0 && v < 312)
        g_row_onscreen[v] = true;
#if VGA32_PROF
    s_prof_cb_cyc += prof_ccount() - prof_t0;
    s_prof_cb_lines++;
#endif
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

    /* écran noir au démarrage ; vérifie au passage l'alignement des scanlines (le DMA I2S
     * les veut alignées sur 4, mais on ne parie pas dessus pour les écritures par mots) */
    static_assert(VGA32_ACTIVE_W % 4 == 0, "largeur VGA non multiple de 4");
    s_row32 = true;
    for (int y = 0; y < VGA32_ACTIVE_H; y++) {
        uint8_t *row = s_vga.getScanline(y);
        if ((uintptr_t)row & 3) s_row32 = false;
        for (int x = 0; x < VGA32_ACTIVE_W; x++)
            row[x ^ 2] = s_lut[0];
    }
    if (!s_row32)
        Serial.println("ATTENTION: scanlines VGA non alignees sur 4 -> ecriture octet par octet");

    denise_line_cb = denise_cb;
}
