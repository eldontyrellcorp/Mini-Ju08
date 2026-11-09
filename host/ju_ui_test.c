/* Scripted input through the panel UI (firmware/src/ju_ui.c): button routing, dials, restore, patch load and write. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ju_api.h"
#include "ju_presets.h"
#define FELUCCA_FLASH 0
static unsigned str_len(const char *s) { return (unsigned)strlen(s); }
static void fmt_int(char *b, int32_t v) { sprintf(b, "%d", (int)v); }
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
#define section(x)
#include "ju_canvas.c"
static int8_t ju_octave;
#include "ju_ui.c"
static int stored_patch = -1, stored_cur;
static void ju_store_current(void) { stored_cur++; }
static int ju_store_patch(int idx) { stored_patch = idx; return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint32_t T = 1000;
static void step(const ju_in_t *in) { T += 20; ui_update(in, T); }
static ju_in_t press(int b) { ju_in_t i; memset(&i, 0, sizeof i); i.press = BTN(b); i.held = BTN(b); return i; }
static ju_in_t turn(int k, int d) { ju_in_t i; memset(&i, 0, sizeof i); i.knob[k] = d; return i; }
static void blocks(int n) { int i; for (i = 0; i < n; i++) { int32_t u[32] = {0}, l[32] = {0}; ju_prepare_block(); ju_render(u, l); } }

int main(void)
{
    ju_in_t in;
    int v0;
    ju_init();
    memcpy(ju_bank, JU_PRESETS, sizeof ju_bank);
    ju_patch[0] = ju_bank[8]; ju_patch[1] = ju_bank[0];
    ui.patch_idx[0] = 8; ui.patch_idx[1] = 0;
    memcpy(ui.saved_global, ju_global, G_COUNT);

    /* page and group buttons */
    in = press(B_ENV); step(&in);  CHECK(ui.page == PAGE_QLINK && ui.qgroup == 2);
    in = press(B_ENV); step(&in);  CHECK(ui.qgroup == 3);
    in = press(B_LFO); step(&in);  CHECK(ui.qgroup == 5);
    in = press(B_SEQ); step(&in);  CHECK(ui.qgroup == 4);
    in = press(B_SCL); step(&in);  CHECK(ui.qgroup == 7);
    in = press(B_EDIT); step(&in); CHECK(ui.page == PAGE_PANEL);
    in = press(B_GLO); step(&in);  CHECK(ui.page == PAGE_PERF);
    in = press(B_FX); step(&in);   CHECK(ui.page == PAGE_FX);
    in = press(B_SAVE); step(&in); CHECK(ui.page == PAGE_PATCH);
    in = press(B_HOME); step(&in); CHECK(ui.page == PAGE_QLINK);
    in = press(B_HOME); step(&in); CHECK(ui.qgroup == 0 && ui.page == PAGE_QLINK);   /* HOME again: next group (7 -> 0) */

    /* SELECT steps the group, in both directions, and leaves any other page */
    memset(&in, 0, sizeof in); in.sel = 1; step(&in); CHECK(ui.qgroup == 1);
    in.sel = -1; step(&in); step(&in); CHECK(ui.qgroup == 7);
    ui.page = PAGE_PERF; in.sel = 1; step(&in); CHECK(ui.page == PAGE_QLINK);

    /* a dial: FILTER, KNOB 1 is the cutoff */
    ui_group_to(0);
    v0 = ju_patch[0].v[P_VCF_CUT];
    in = turn(0, 1); step(&in); CHECK(ju_patch[0].v[P_VCF_CUT] > v0);
    CHECK(q_edited(0));
    /* REC held + the knob: back to the saved value */
    memset(&in, 0, sizeof in); in.held = BTN(B_REC); in.knob[0] = 1; step(&in);
    CHECK(ju_patch[0].v[P_VCF_CUT] == ju_bank[8].v[P_VCF_CUT] && !q_edited(0));
    /* the knob goes down and stops at 0 */
    ju_patch[0].v[P_VCF_CUT] = 2; in = turn(0, -5); step(&in); CHECK(ju_patch[0].v[P_VCF_CUT] == 0);

    /* EFFECTS dials: turning a send up switches the effect on, to zero off */
    ui_group_to(6);
    ju_patch[0].fx.revOn = 0; ju_patch[0].fx.r[RV_SEND] = 0;
    in = turn(0, 1); step(&in); CHECK(ju_patch[0].fx.revOn && ju_patch[0].fx.r[RV_SEND] > 0);
    in = turn(0, -100); step(&in); CHECK(!ju_patch[0].fx.revOn && ju_patch[0].fx.r[RV_SEND] == 0);
    /* PLAY: portamento time up switches portamento on */
    ui_group_to(7);
    ju_global[G_PORTA_TIME] = 0; ju_global[G_PORTA_MODE] = PM_OFF;
    in = turn(1, 1); step(&in); CHECK(ju_global[G_PORTA_MODE] == PM_ON && ju_global[G_PORTA_TIME] > 0);

    /* PRESETS: the next patch, loaded by the audio at the next block */
    memset(&in, 0, sizeof in); in.preset = 1; step(&in);
    CHECK(ui.patch_idx[0] == 9 && ju_load_req[0] == 10);
    blocks(1); CHECK(!ju_load_req[0] && memcmp(&ju_patch[0], &ju_bank[9], sizeof(ju_patch_t)) == 0);
    in.preset = -1; step(&in); in.preset = -1; step(&in); blocks(1); CHECK(ui.patch_idx[0] == 7);
    /* ALGORITHM flips the part being edited */
    memset(&in, 0, sizeof in); in.algo = 1; step(&in); CHECK(ju_global[G_PANEL] == 1);
    step(&in); CHECK(ju_global[G_PANEL] == 0);

    /* the PATCHES page: KNOB 2 jumps a row, OCT+ loads; WRITE then a slot, OCT+ writes */
    in = press(B_SAVE); step(&in);
    ui.focus[PAGE_PATCH] = 20;
    in = turn(1, 1); step(&in); CHECK(ui.focus[PAGE_PATCH] == 28);
    in = press(B_OCTUP); step(&in); CHECK(ui.patch_idx[0] == 28 && ju_load_req[0] == 29);
    ui.focus[PAGE_PATCH] = 64;
    in = press(B_OCTUP); step(&in); CHECK(ui.write_mode);
    ui.focus[PAGE_PATCH] = 40; ju_patch[0].v[P_VCF_CUT] = 123;
    in = press(B_OCTUP); step(&in);
    CHECK(!ui.write_mode && stored_patch == 40 && ju_bank[40].v[P_VCF_CUT] == 123);
    ui.write_mode = 1; in = press(B_OCTDN); step(&in); CHECK(!ui.write_mode);        /* OCT- cancels */

    /* octave of the keys: not on the PATCHES page, clamped, both buttons at once reset it */
    in = press(B_HOME); step(&in);
    in = press(B_OCTUP); step(&in); step(&in); CHECK(ju_octave == 2);
    for (v0 = 0; v0 < 6; v0++) { in = press(B_OCTUP); step(&in); }
    CHECK(ju_octave == 3);
    memset(&in, 0, sizeof in); in.held = BTN(B_OCTUP) | BTN(B_OCTDN); step(&in); CHECK(ju_octave == 0);

    /* ARP toggles; PLAY asks for all notes off */
    in = press(B_ARP); step(&in); CHECK(ju_global[G_ARP_ON] == 1);
    in = press(B_ARP); step(&in); CHECK(ju_global[G_ARP_ON] == 0);
    ju_panic_req = 0; in = press(B_PLAY); step(&in); CHECK(ju_panic_req == 1);

    /* the globals save themselves after two seconds once nothing sounds */
    ju_global[G_VOLUME] = 150; ui.page = PAGE_PERF; ui.save_at = T + 100;
    stored_cur = 0;
    memset(&in, 0, sizeof in); T += 3000; ui_update(&in, T);
    CHECK(stored_cur == 1 && ui.saved_global[G_VOLUME] == 150);

    /* the frame is drawn the same whether it is drawn band by band or all at once (the hash of an unchanged band is stable) */
    cv_force = 1; cv_present(ui_draw);
    {
        uint32_t h1[CV_NBANDS];
        memcpy(h1, cv_hash, sizeof h1);
        cv_force = 0; CHECK(cv_present(ui_draw) == 0);
        CHECK(memcmp(h1, cv_hash, sizeof h1) == 0);
    }
    printf("ui logic: %s\n", fails ? "FAILED" : "all checks passed");
    return fails ? 1 : 0;
}
