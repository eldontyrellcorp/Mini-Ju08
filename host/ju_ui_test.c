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
static int stored_seq[2];
static int ju_store_seq(int part) { stored_seq[part]++; return 0; }
static int stored_song;
static int ju_store_song(void) { stored_song++; return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint32_t T = 1000;
static void step(const ju_in_t *in) { T += 20; ui_update(in, T); }
static ju_in_t press(int b) { ju_in_t i; memset(&i, 0, sizeof i); i.press = BTN(b); i.held = BTN(b); return i; }
static ju_in_t release(int b) { ju_in_t i; memset(&i, 0, sizeof i); i.released = BTN(b); return i; }
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
    in = press(B_SCL); step(&in);  CHECK(ui.qgroup == 7);
    in = press(B_EDIT); step(&in); CHECK(ui.page == PAGE_PANEL);
    in = press(B_GLO); step(&in);  CHECK(ui.page == PAGE_PERF);
    in = press(B_FX); step(&in);   CHECK(ui.page == PAGE_FX);
    in = press(B_SAVE); step(&in); CHECK(ui.page == PAGE_PATCH);
    in = press(B_HOME); step(&in); CHECK(ui.page == PAGE_QLINK);
    in = press(B_HOME); step(&in); CHECK(ui.qgroup == 0 && ui.page == PAGE_QLINK);   /* HOME again: next group (7 -> 0) */

    /* the EDIT and GLO series: pages of a few controls, KNOB 1 crosses into the next page, KNOB 3 and the button jump a page */
    in = press(B_EDIT); step(&in); in = press(B_EDIT); step(&in);
    CHECK(ui.page == PAGE_PANEL && sp_locate(PAGE_PANEL, ui.focus[PAGE_PANEL], 0) == 1);          /* EDIT twice: the second page */
    in = turn(2, 1); step(&in); CHECK(sp_locate(PAGE_PANEL, ui.focus[PAGE_PANEL], 0) == 2);       /* KNOB 3: a page on */
    in = turn(2, -1); step(&in); in = turn(2, -1); step(&in); CHECK(sp_locate(PAGE_PANEL, ui.focus[PAGE_PANEL], 0) == 0);
    in = turn(2, -1); step(&in); CHECK(sp_locate(PAGE_PANEL, ui.focus[PAGE_PANEL], 0) == NPANELPAGES - 1);   /* and round */
    ui.focus[PAGE_PANEL] = 0;
    in = turn(0, 3); step(&in); CHECK(ui.focus[PAGE_PANEL] == 3 && sp_locate(PAGE_PANEL, 3, 0) == 1 && sp_param(PAGE_PANEL, 3) == P_VM_LFO);   /* KNOB 1 crosses */
    {   /* every sound parameter is on a page, once */
        int seen[P_COUNT] = {0}, i, ok = 1;
        for (i = 0; i < sp_total(PAGE_PANEL); i++) seen[sp_param(PAGE_PANEL, i)]++;
        for (i = 0; i < P_ASSIGN; i++) ok = ok && seen[i] == 1;
        CHECK(ok && sp_total(PAGE_PANEL) == P_ASSIGN);
    }
    ui.focus[PAGE_PANEL] = sp_first(PAGE_PANEL, 0);
    in = turn(1, 1); ui.page = PAGE_PANEL; step(&in); CHECK(ju_patch[ui_layer()].v[P_LFO_RATE] > 0);     /* KNOB 2 sets the focused one */
    in = press(B_GLO); step(&in); in = press(B_GLO); step(&in);
    CHECK(ui.page == PAGE_PERF && sp_locate(PAGE_PERF, ui.focus[PAGE_PERF], 0) == 1);

    /* the effects series: at most five rows a page, a page never mixes sections, KNOB 3 and FX again jump a page */
    {
        fxrow_t rows[FX_MAXROWS];
        int starts[FX_MAXPAGES + 1], n, np, p, ok = 1;
        ju_patch[0].fx.mfxType = MFX_PHA2;
        n = fx_rows(&ju_patch[0].fx, rows); np = fx_paginate(rows, n, starts);
        for (p = 0; p < np; p++) {
            int k;
            ok = ok && starts[p + 1] - starts[p] <= FX_PER_PAGE && starts[p + 1] > starts[p];
            for (k = starts[p]; k < starts[p + 1]; k++) ok = ok && rows[k].group == rows[starts[p]].group;
        }
        CHECK(ok && starts[np] == n && np >= 6);
        in = press(B_FX); step(&in); in = press(B_FX); step(&in);
        CHECK(ui.page == PAGE_FX && ui.focus[PAGE_FX] == starts[1]);                       /* FX twice: the second page */
        in = turn(2, 1); step(&in); CHECK(ui.focus[PAGE_FX] == starts[2]);
        in = turn(2, -1); step(&in); in = turn(2, -1); step(&in); CHECK(ui.focus[PAGE_FX] == starts[0]);
        in = turn(2, -1); step(&in); CHECK(ui.focus[PAGE_FX] == starts[np - 1]);          /* and round */
        ui.focus[PAGE_FX] = 0; in = turn(0, 6); step(&in); CHECK(ui.focus[PAGE_FX] == 6 && fx_page_of(starts, np, 6) == 1);   /* KNOB 1 crosses */
        ju_fx_load_mfx_defaults(&ju_patch[0].fx, MFX_CHO1);
    }

    /* the SEQ page, a piano roll: KNOB 1 step, 2 pitch, 3 length of the note, 4 pattern; SCL deletes the note */
    {
        ju_global[G_PANEL] = 0; ju_seq_cur[0] = 0; ju_pat[0][0].len = 8;
        for (int i = 0; i < 64; i++) { ju_pat[0][0].type[i] = SQ_REST; ju_pat[0][0].note[i] = 60; }
        in = press(B_SEQ); step(&in); CHECK(ui.page == PAGE_SEQ && ui.seq_sub == 0);
        in = turn(0, 3); step(&in); CHECK(ju_seq_cursor[0] == 3);
        in = turn(0, -5); step(&in); CHECK(ju_seq_cursor[0] == 6);                                      /* wraps within the length (8) */
        ju_seq_cursor[0] = 2;
        in = turn(1, 1); step(&in); CHECK(ju_pat[0][0].note[2] == 61 && ju_pat[0][0].type[2] == SQ_NOTE);    /* a pitch turns a rest into a note */
        in = turn(1, -3); step(&in); CHECK(ju_pat[0][0].note[2] == 58);
        in = turn(2, 2); step(&in);                                                                      /* the length: two more steps */
        CHECK(ju_pat[0][0].type[3] == SQ_TIE && ju_pat[0][0].type[4] == SQ_TIE && ju_pat[0][0].type[5] == SQ_REST);
        CHECK(seq_chain(&ju_pat[0][0], 2) == 3);
        in = turn(2, -1); step(&in); CHECK(ju_pat[0][0].type[3] == SQ_TIE && ju_pat[0][0].type[4] == SQ_REST);   /* shorter: the tie goes */
        ju_seq_cursor[0] = 3;                                                                            /* on the tie: the same note */
        in = turn(1, 2); step(&in); CHECK(ju_pat[0][0].note[2] == 60 && ju_pat[0][0].type[3] == SQ_TIE);  /* pitch edits the head */
        in = turn(2, 1); step(&in); CHECK(seq_chain(&ju_pat[0][0], 2) == 3);                              /* length edits the head too */
        ju_pat[0][0].type[5] = SQ_NOTE; ju_pat[0][0].note[5] = 70;
        in = turn(2, 5); step(&in); CHECK(seq_chain(&ju_pat[0][0], 2) == 3 && ju_pat[0][0].type[5] == SQ_NOTE);   /* stops at the next note */
        in = turn(2, -9); step(&in); CHECK(ju_pat[0][0].type[2] == SQ_REST && ju_pat[0][0].type[3] == SQ_REST && ju_pat[0][0].type[4] == SQ_REST);   /* 0 deletes */
        CHECK(ju_pat[0][0].type[5] == SQ_NOTE);                                                          /* the next note is untouched */
        ju_seq_cursor[0] = 6; in = turn(2, 2); step(&in);                                               /* length on a rest: a new note */
        CHECK(ju_pat[0][0].type[6] == SQ_NOTE && ju_pat[0][0].type[7] == SQ_TIE);
        ju_seq_cursor[0] = 7; in = press(B_SCL); step(&in); blocks(1);                                   /* SCL on the tie: this step goes, the note stays one step shorter */
        CHECK(ju_pat[0][0].type[6] == SQ_NOTE && ju_pat[0][0].type[7] == SQ_REST && ui.page == PAGE_SEQ && ju_seq_cursor[0] == 7);
        in = press(B_EDIT); step(&in); blocks(1);                                                        /* EDIT on the SEQ page: undo, it does not leave the page */
        CHECK(ju_pat[0][0].type[7] == SQ_TIE && ui.page == PAGE_SEQ);
        ju_seq_cursor[0] = 6; in = press(B_SCL); step(&in); blocks(1);                                   /* SCL on a note: the note and its tie */
        CHECK(ju_pat[0][0].type[6] == SQ_REST && ju_pat[0][0].type[7] == SQ_REST);
        CHECK(ju_seq_dirty[0]);
        {   /* the roll follows the note under the cursor */
            ju_pat[0][0].note[1] = 100; ju_pat[0][0].type[1] = SQ_NOTE; ju_seq_cursor[0] = 1; in = turn(0, 0); in.knob[1] = 0; step(&in);
            CHECK(ui.seq_lo + ROLL_ROWS - 1 >= 100 && ui.seq_lo <= 100);
            ju_pat[0][0].note[1] = 20; in = turn(1, 1); step(&in);
            CHECK(ui.seq_lo <= 21 && ui.seq_lo + ROLL_ROWS - 1 >= 21);
            ju_pat[0][0].type[1] = SQ_REST;
        }
        in = turn(3, 1); step(&in); CHECK(ju_seq_cur[0] == 1 && ju_seq_cursor[0] == 0);
        in = turn(3, -1); step(&in); in = turn(3, -1); step(&in); CHECK(ju_seq_cur[0] == 15);          /* the patterns wrap */
        ju_seq_cur[0] = 0;
        in = press(B_SEQ); step(&in); CHECK(ui.seq_sub == 1);
        ju_pat[0][0].len = 8;
        in = turn(0, 4); step(&in); CHECK(ju_pat[0][0].len > 8);
        ju_pat[0][0].len = 8;
        in = turn(0, -100); step(&in); CHECK(ju_pat[0][0].len == 1);
        in = turn(0, 200); step(&in); CHECK(ju_pat[0][0].len == 64);
        ju_pat[0][0].gate = 3; in = turn(1, 5); step(&in); CHECK(ju_pat[0][0].gate == 6);
        in = turn(1, -9); step(&in); CHECK(ju_pat[0][0].gate == 1);
        { int t0 = ju_global[G_TEMPO]; in = turn(2, 1); step(&in); CHECK(ju_global[G_TEMPO] > t0); }
        ju_pat[0][0].type[5] = SQ_NOTE;
        in = press(B_OCTDN); step(&in); CHECK(ju_pat[0][0].type[5] == SQ_NOTE);                      /* once: asks */
        in = press(B_OCTDN); step(&in); CHECK(ju_pat[0][0].type[5] == SQ_REST);                      /* twice: cleared */
        /* SEQ again: the song page; the knobs edit rows (KNOB 1 the row, 2 upper, 3 lower, 4 repeats) */
        in = press(B_SEQ); step(&in); CHECK(ui.seq_sub == 2);
        ju_song.n = 0; ui.song_cur = 0; ju_global[G_SONG] = 0; ju_seq_cur[0] = 3; ju_seq_cur[1] = 5; ju_song_dirty = 0;
        in = turn(1, 1); step(&in);                                                                     /* a turn on the empty row makes it a row */
        CHECK(ju_song.n == 1 && ju_song.row[0].pat[0] == 4 && ju_song.row[0].pat[1] == 5 && ju_song.row[0].rep == 1 && ju_song_dirty);
        in = turn(1, -9); step(&in); CHECK(ju_song.row[0].pat[0] == JU_SONG_NONE);                       /* --: none, below the first pattern */
        in = turn(1, 3); step(&in); CHECK(ju_song.row[0].pat[0] == 2);                                   /* -1 + 3 */
        in = turn(2, 20); step(&in); CHECK(ju_song.row[0].pat[1] == 15);
        in = turn(3, 3); step(&in); CHECK(ju_song.row[0].rep == 4);
        in = turn(3, 99); step(&in); CHECK(ju_song.row[0].rep == 16);
        in = turn(3, -99); step(&in); CHECK(ju_song.row[0].rep == 1);
        in = turn(0, 1); step(&in); CHECK(ui.song_cur == 1 && ju_song.n == 1);                          /* the empty row after the last */
        in = turn(0, 5); step(&in); CHECK(ui.song_cur == 1);                                             /* no further than that */
        in = turn(3, 2); step(&in); CHECK(ju_song.n == 2 && ju_song.row[1].rep == 3);
        in = turn(0, -1); step(&in); CHECK(ui.song_cur == 0 && ju_song_sel == 0);
        in = press(B_OCTUP); step(&in); CHECK(ju_song.n == 3 && ju_song.row[0].pat[1] == ju_song.row[1].pat[1] && ju_song.row[2].rep == 3);   /* a copy before the row */
        ju_song.row[1].rep = 9;
        ui.song_cur = 1; in = press(B_SCL); step(&in); CHECK(ju_song.n == 2 && ju_song.row[1].rep == 3);  /* delete the row */
        in = turn(0, 1); step(&in); CHECK(ju_song_sel == 1);                                            /* PLAY starts from the cursor's row */
        in = press(B_OCTDN); step(&in); CHECK(ju_global[G_SONG] == 1);                                   /* song mode on */
        in = press(B_OCTDN); step(&in); CHECK(ju_global[G_SONG] == 0);
        for (int i = 0; i < JU_SONG_ROWS + 4; i++) { ui.song_cur = (uint8_t)ju_song.n; in = turn(3, 1); step(&in); }
        CHECK(ju_song.n == JU_SONG_ROWS);                                                                 /* a song has 32 rows at most */
        in = press(B_OCTUP); step(&in); CHECK(ju_song.n == JU_SONG_ROWS);
        ju_song.n = 0;
        {   /* saved two seconds on, when stopped and silent */
            ju_song_dirty = 1; ju_seq_run = 0; stored_song = 0; ju_seq_dirty[0] = ju_seq_dirty[1] = 0; ui.seq_save_at = 0;
            memset(&in, 0, sizeof in); T += 100; ui_update(&in, T); T += 2500; ui_update(&in, T);
            CHECK(stored_song == 1 && !ju_song_dirty);
        }
        in = press(B_SEQ); step(&in); CHECK(ui.seq_sub == 0);
        ju_seq_cur[0] = ju_seq_cur[1] = 0;
        /* the other part has its own pattern */
        ju_global[G_PANEL] = 1; ju_seq_cursor[1] = 0; ju_pat[1][0].len = 16;
        in = turn(1, 2); step(&in); CHECK(ju_pat[1][0].type[0] == SQ_NOTE && ju_pat[0][0].type[0] == SQ_REST);
        ju_global[G_PANEL] = 0;
        /* a changed pattern is saved two seconds on, when stopped and silent */
        ju_seq_dirty[0] = 1; ju_seq_run = 0; stored_seq[0] = 0; ju_seq_dirty[1] = 0;
        memset(&in, 0, sizeof in); T += 100; ui_update(&in, T); T += 2500; ui_update(&in, T);
        CHECK(stored_seq[0] == 1 && !ju_seq_dirty[0]);
        ju_seq_dirty[0] = 1; ju_seq_run = 1; stored_seq[0] = 0;
        T += 100; ui_update(&in, T); T += 2500; ui_update(&in, T); T += 2500; ui_update(&in, T);
        CHECK(stored_seq[0] == 0);                                                                       /* playing: not now */
        ju_seq_run = 0; ju_seq_dirty[0] = 0;
        in = press(B_HOME); step(&in);
    }

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
    /* PLAY: a tap starts and stops the sequencer; held 0.6 s it is all notes off */
    ju_panic_req = 0; ju_seq_req = 0; ju_seq_run = 0;
    in = press(B_PLAY); step(&in); in = release(B_PLAY); step(&in);
    CHECK(ju_seq_req == 1 && ju_panic_req == 0);
    ju_seq_req = 0; ju_seq_run = 1;
    in = press(B_PLAY); step(&in); in = release(B_PLAY); step(&in);
    CHECK(ju_seq_req == 2);
    ju_seq_req = 0; ju_seq_run = 0;
    in = press(B_PLAY); step(&in); T += 700; { ju_in_t h; memset(&h, 0, sizeof h); h.held = BTN(B_PLAY); step(&h); }
    CHECK(ju_panic_req == 1 && ju_seq_req == 2);
    in = release(B_PLAY); ju_seq_req = 0; step(&in); CHECK(ju_seq_req == 0);                                  /* let go after the long hold: no start */
    /* REC: a tap arms recording; used with a knob (the dial restore) it does not */
    ju_seq_rec = 0;
    in = press(B_REC); step(&in); in = release(B_REC); step(&in); CHECK(ju_seq_rec == 1);
    in = press(B_REC); step(&in); in = release(B_REC); step(&in); CHECK(ju_seq_rec == 0);
    ui_group_to(0);
    in = press(B_REC); step(&in); { ju_in_t h; memset(&h, 0, sizeof h); h.held = BTN(B_REC); h.knob[0] = 1; step(&h); }
    in = release(B_REC); step(&in); CHECK(ju_seq_rec == 0);

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
