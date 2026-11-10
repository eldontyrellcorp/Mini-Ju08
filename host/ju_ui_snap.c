/* Draws every page of the panel UI (firmware/src/ju_ui.c, through the band canvas ju_canvas.c) into PNG-ready PPM files:
 * the 240 x 240 layout can be looked at without the device, and is checked for what a layout bug looks like (text
 * running off the screen is not visible here, but a band that differs from a full draw would be). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ju_api.h"
#include "ju_presets.h"
#define FELUCCA_FLASH 0
static unsigned str_len(const char *s) { return (unsigned)strlen(s); }
static void fmt_int(char *b, int32_t v) { sprintf(b, "%d", (int)v); }
static uint16_t shot[240 * 240];
static int blits;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t j;
    blits++;
    for (j = 0; j < h; j++) memcpy(&shot[(y + j) * 240 + x], px + j * w, w * 2);
}
#define section(x)
#include "ju_canvas.c"
static int8_t ju_octave;
#include "ju_ui.c"
static void ju_store_current(void) {}
static int ju_store_patch(int idx) { (void)idx; return 0; }
static int ju_store_seq(int part) { (void)part; return 0; }

static void save(const char *name)
{
    FILE *f = fopen(name, "wb");
    int i;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = (uint16_t)((shot[i] >> 8) | (shot[i] << 8));       /* back from the panel's byte order */
        unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void frame(const char *name, uint32_t now)
{
    ju_in_t in;
    memset(&in, 0, sizeof in);
    ui_update(&in, now);
    cv_force = 1;
    cv_present(ui_draw);
    save(name);
}
int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[256];
    int i, g;
    ju_init();
    memcpy(ju_bank, JU_PRESETS, sizeof(ju_bank));
    ju_patch[0] = ju_bank[8]; ju_patch[1] = ju_bank[0];
    ui.patch_idx[0] = 8;
    ju_global[G_CH_U] = 1; ju_global[G_CH_L] = 2;
    memcpy(ui.saved_global, ju_global, G_COUNT);
    ju_note_on(0xFF, 60); ju_note_on(0xFF, 64); ju_note_on(0xFF, 67);          /* some voices lit */
    for (i = 0; i < 400; i++) { int32_t u[32] = {0}, l[32] = {0}; ju_prepare_block(); ju_render(u, l); }
    ui.cpu = 37;
    for (g = 0; g < 8; g++) {
        ui.page = PAGE_QLINK; ui.qgroup = g; ui.overview = 0; ui.last_dial = g % 4; ui.last_dial_at = 1000;
        snprintf(path, sizeof path, "%s/qlink%d.ppm", dir, g);
        frame(path, 1100);
    }
    ui.qgroup = 3; ui.overview = 1; ui.overview_at = 1100;
    snprintf(path, sizeof path, "%s/overview.ppm", dir); frame(path, 1200);
    ui.overview = 0;
    for (g = 0; g < NPANELPAGES; g++) { ui.page = PAGE_PANEL; ui.focus[PAGE_PANEL] = sp_first(PAGE_PANEL, g); snprintf(path, sizeof path, "%s/panel%d.ppm", dir, g); frame(path, 5000); }
    for (g = 0; g < NPERFPAGES; g++) { ui.page = PAGE_PERF; ui.focus[PAGE_PERF] = sp_first(PAGE_PERF, g); snprintf(path, sizeof path, "%s/perf%d.ppm", dir, g); frame(path, 5000); }
    ui.page = PAGE_PANEL; ui_focus_on(PAGE_PANEL, P_VCF_CUT); snprintf(path, sizeof path, "%s/panel.ppm", dir); frame(path, 5000);
    ui.page = PAGE_PERF; ui_focus_on(PAGE_PERF, PG(G_ARP_MODE)); snprintf(path, sizeof path, "%s/perf.ppm", dir); frame(path, 5000);
    ju_patch[0].fx.mfxOn = 1; ju_patch[0].fx.revOn = 1;
    for (g = 0; g < 4; g++) { fxrow_t rows[FX_MAXROWS]; int st[FX_MAXPAGES + 1]; int n = fx_rows(&ju_patch[0].fx, rows), np = fx_paginate(rows, n, st); ui.page = PAGE_FX; ui.focus[PAGE_FX] = st[g < np ? g : 0] + 1; snprintf(path, sizeof path, "%s/fx%d.ppm", dir, g); frame(path, 5000); }
    ju_patch[0].fx.mfxType = MFX_PCH2; ju_patch[0].fx.mfxOn = 1; ju_patch[0].fx.revOn = 1;
    ui.page = PAGE_FX; ui.focus[PAGE_FX] = 2; snprintf(path, sizeof path, "%s/fx2.ppm", dir); frame(path, 5000);
    {   /* the sequencer: a pattern with notes, rests and ties, the cursor and a playing step */
        static const int nn[16] = {48, 0, 55, 0, 60, 0, 63, 0, 58, 58, 0, 0, 53, 0, 0, 65};
        static const char *tt = "NRNTNRNRNTRRNRRN";
        for (int i = 0; i < 16; i++) { ju_pat[0][0].note[i] = (uint8_t)(nn[i] ? nn[i] : 60); ju_pat[0][0].type[i] = tt[i] == 'N' ? SQ_NOTE : tt[i] == 'T' ? SQ_TIE : SQ_REST; }
        ju_pat[0][0].len = 16; ju_seq_cursor[0] = 6; ju_seq_run = 1; ju_seq_pos[0] = 9;
        ui.page = PAGE_SEQ; ui.seq_sub = 0; snprintf(path, sizeof path, "%s/seq0.ppm", dir); frame(path, 5000);
        ju_pat[0][0].len = 64; ju_seq_cursor[0] = 40; ju_seq_run = 0; ju_seq_rec = 1; snprintf(path, sizeof path, "%s/seq1.ppm", dir); frame(path, 5000);
        ju_seq_rec = 0; ui.seq_sub = 1; ju_pat[0][0].len = 16; snprintf(path, sizeof path, "%s/seq2.ppm", dir); frame(path, 5000);
        ui.seq_sub = 0; ui.page = PAGE_QLINK;
    }
    ui.page = PAGE_PATCH; ui.focus[PAGE_PATCH] = 25; snprintf(path, sizeof path, "%s/patch.ppm", dir); frame(path, 5000);
    ui.write_mode = 1; ui.focus[PAGE_PATCH] = 64; snprintf(path, sizeof path, "%s/patchw.ppm", dir); frame(path, 5000);
    printf("frames written to %s\n", dir);
    return 0;
}
