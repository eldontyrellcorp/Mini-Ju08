/* SPDX-License-Identifier: GPL-3.0-only */
/* The Jupiter-8 panel UI, after jupiter8_esp32/src/ui/ui.cpp, for the FM-1: a 240 x 240 screen, KNOB 1-4 as the four
 * Q-Link dials, and the page buttons. Pages: Q-LINK (four dials, eight groups), PANEL (every sound control),
 * PERFORMANCE (assign, keys, arpeggio, portamento, bender, master, MIDI), EFFECTS, PATCHES.
 *
 *   KNOB 1-4        the four dials of the Q-Link page; on the others KNOB 1 chooses, KNOB 2 sets, KNOB 3 jumps
 *                   a section, KNOB 4 steps the patch
 *   SELECT          the Q-Link group (and back to the Q-Link page)      PRESETS  the patch
 *   ALGORITHM       Upper / Lower part                                  MASTER   volume (the pot)
 *   HOME            Q-Link page (again: next group)    EDIT PANEL    GLO PERFORMANCE    FX EFFECTS    SAVE PATCHES
 *   ENV             ENV-1 / ENV-2      LFO  LFO group      SEQ  VCO group      SCL  PLAY group
 *   ARP             arpeggio on / off
 *   SEQ             the sequencer (again: its setup)    PLAY  start / stop (held: all notes off)
 *   REC             tapped: arm recording (step entry, or real time while playing); held + a knob: that dial back to the saved value
 *   OCT- / OCT+     octave of the keys (on the PATCHES page: back / load or write) */

/* panel palette: black panel, cream print, the Jupiter's band of bright button colours */
#define K_BG CV_RGB(10, 10, 12)
#define K_PANEL CV_RGB(22, 22, 25)
#define K_LINE CV_RGB(70, 70, 76)
#define K_TEXT CV_RGB(235, 230, 215)
#define K_DIM CV_RGB(135, 132, 124)
#define K_RED CV_RGB(226, 44, 38)
#define K_ORANGE CV_RGB(240, 124, 28)
#define K_YELLOW CV_RGB(244, 196, 48)
#define K_CREAM CV_RGB(232, 224, 196)
#define K_BLUE CV_RGB(50, 128, 208)
#define K_LEDON CV_RGB(255, 44, 30)
#define K_LEDOFF CV_RGB(52, 12, 10)
#define K_FOCUS CV_RGB(255, 196, 40)
#define K_FOCBG CV_RGB(52, 44, 18)

enum { PAGE_QLINK, PAGE_PANEL, PAGE_PERF, PAGE_FX, PAGE_PATCH, PAGE_SEQ, PAGE_COUNT };
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
#define BTN(b) (1u << (b))

/* what the panel hands the UI each frame */
typedef struct {
    int knob[4];                     /* KNOB 1-4: detents since the last frame */
    int sel, algo, preset;           /* SELECT, ALGORITHM, PRESETS */
    uint32_t press;                  /* buttons pressed since the last frame (bit per B_*) */
    uint32_t held;                   /* buttons down */
    uint32_t released;               /* buttons let go since the last frame */
} ju_in_t;

static const int16_t kCos[33] = {-181, -206, -226, -241, -251, -256, -255, -248, -237, -220, -198, -172, -142, -109, -74, -38, 0, 38, 74, 109, 142, 172, 198, 220, 237, 248, 255, 256, 251, 241, 226, 206, 181};
static const int16_t kSin[33] = {181, 152, 121, 86, 50, 13, -25, -62, -98, -132, -162, -190, -213, -231, -245, -253, -256, -253, -245, -231, -213, -190, -162, -132, -98, -62, -25, 13, 50, 86, 121, 152, 181};

/* The EDIT and GLO (performance) pages are series of pages of 2-6 controls, big enough to read: KNOB 1 walks through the
 * controls (crossing into the next page), KNOB 3 jumps a page, EDIT / GLO again go to the next page. The focus is the
 * position in the flat list of all the pages' controls. */
typedef struct { const char *title; uint16_t color; int8_t n; int16_t ids[8]; } spage_t;
#define PG(x) (P_COUNT + (x))
static const spage_t kPanelPages[] = {
    {"LFO", K_RED, 3, {P_LFO_RATE, P_LFO_DELAY, P_LFO_WAVE}},
    {"VCO MOD", K_ORANGE, 5, {P_VM_LFO, P_VM_ENV, P_VM_DEST, P_VM_PW, P_VM_PWMODE}},
    {"VCO-1", K_YELLOW, 3, {P_V1_XMOD, P_V1_RANGE, P_V1_WAVE}},
    {"VCO-2", K_CREAM, 5, {P_V2_SYNC, P_V2_RANGE, P_V2_LOW, P_V2_TUNE, P_V2_WAVE}},
    {"MIX HPF VCA", K_BLUE, 4, {P_MIX, P_HPF, P_VCA_LEVEL, P_VCA_LFO}},
    {"VCF", K_RED, 4, {P_VCF_CUT, P_VCF_RES, P_VCF_ENV, P_VCF_KEY}},
    {"VCF 2", K_RED, 3, {P_VCF_SLOPE, P_VCF_ENVSEL, P_VCF_LFO}},
    {"ENV-1", K_YELLOW, 6, {P_E1_A, P_E1_D, P_E1_S, P_E1_R, P_E1_KEY, P_E1_POL}},
    {"ENV-2", K_CREAM, 5, {P_E2_A, P_E2_D, P_E2_S, P_E2_R, P_E2_KEY}},
};
static const spage_t kPerfPages[] = {
    {"ASSIGN", K_RED, 4, {P_ASSIGN, PG(G_HOLD_U), PG(G_HOLD_L), PG(G_VINTAGE)}},
    {"KEY MODE", K_ORANGE, 3, {PG(G_KEYMODE), PG(G_PANEL), PG(G_SPLIT)}},
    {"ARPEGGIO", K_YELLOW, 4, {PG(G_ARP_ON), PG(G_ARP_MODE), PG(G_ARP_RANGE), PG(G_ARP_RATE)}},
    {"PORTAMENTO", K_CREAM, 2, {PG(G_PORTA_MODE), PG(G_PORTA_TIME)}},
    {"BENDER", K_BLUE, 5, {PG(G_BEND_VCO), PG(G_BEND_V1), PG(G_BEND_V2), PG(G_BEND_VCF), PG(G_BEND_VCFSW)}},
    {"LFO MOD", K_RED, 5, {PG(G_MOD_RISE), PG(G_MOD_VCO), PG(G_MOD_VCOSW), PG(G_MOD_VCF), PG(G_MOD_VCFSW)}},
    {"MASTER", K_ORANGE, 4, {PG(G_VOLUME), PG(G_BAL_U), PG(G_BAL_L), PG(G_TUNE)}},
    {"MIDI", K_YELLOW, 2, {PG(G_CH_U), PG(G_CH_L)}},
    {"SEQ CLOCK", K_ORANGE, 2, {PG(G_SYNC), PG(G_TEMPO)}},
};
#define NPANELPAGES ((int)(sizeof kPanelPages / sizeof kPanelPages[0]))
#define NPERFPAGES ((int)(sizeof kPerfPages / sizeof kPerfPages[0]))
static const spage_t *sp_tab(int page, int *np)
{
    if (page == PAGE_PERF) { *np = NPERFPAGES; return kPerfPages; }
    *np = NPANELPAGES;
    return kPanelPages;
}
static int sp_total(int page)
{
    int np, i, n = 0;
    const spage_t *t = sp_tab(page, &np);
    for (i = 0; i < np; i++) n += t[i].n;
    return n;
}
static int sp_locate(int page, int flat, int *k)               /* the page holding the flat position, and the control in it */
{
    int np, i;
    const spage_t *t = sp_tab(page, &np);
    for (i = 0; i < np; i++) {
        if (flat < t[i].n) { if (k) *k = flat; return i; }
        flat -= t[i].n;
    }
    if (k) *k = 0;
    return 0;
}
static int sp_first(int page, int pg)                          /* the flat position of a page's first control */
{
    int np, i, n = 0;
    const spage_t *t = sp_tab(page, &np);
    for (i = 0; i < pg; i++) n += t[i].n;
    return n;
}
static int sp_param(int page, int flat)
{
    int k, pg = sp_locate(page, flat, &k), np;
    return sp_tab(page, &np)[pg].ids[k];
}
/* a parameter's place (host tools and tests aim the focus with it) */
/* ---- the effects page: one scrolling list built from whichever effect types are selected */
typedef struct { uint8_t group, kind; int8_t idx; } fxrow_t;     /* kind 0 switch, 1 MFX type, 2 value, 3 tempo */
#define FX_MAXROWS 64
static int fx_rows(const ju_fxpatch_t *f, fxrow_t *r)
{
    int n = 0, i;
    r[n].group = 0; r[n].kind = 0; r[n++].idx = 0;
    r[n].group = 0; r[n].kind = 1; r[n++].idx = 0;
    for (i = 0; i < ju_mfx_nparams(f->mfxType); i++) { r[n].group = 0; r[n].kind = 2; r[n++].idx = (int8_t)i; }
    r[n].group = 1; r[n].kind = 0; r[n++].idx = 1;
    for (i = 0; i < DL_COUNT; i++) { r[n].group = 1; r[n].kind = 2; r[n++].idx = (int8_t)i; }
    r[n].group = 2; r[n].kind = 0; r[n++].idx = 2;
    r[n].group = 2; r[n].kind = 2; r[n++].idx = RV_SEND;
    r[n].group = 2; r[n].kind = 2; r[n++].idx = RV_MODE;
    if (f->r[RV_MODE] == RVM_SHIMMER) for (i = RS_LDAMP; i < RV_COUNT; i++) { r[n].group = 2; r[n].kind = 2; r[n++].idx = (int8_t)i; }
    else for (i = RV_TYPE; i <= RV_LEV; i++) { r[n].group = 2; r[n].kind = 2; r[n++].idx = (int8_t)i; }
    r[n].group = 3; r[n].kind = 3; r[n++].idx = 0;
    return n;
}
static const ju_fxparam_t *fx_row_def(const ju_fxpatch_t *f, const fxrow_t *r)
{
    if (r->group == 0) return ju_mfx_param(f->mfxType, r->idx);
    return r->group == 1 ? ju_delay_param(r->idx) : ju_reverb_param(r->idx);
}
static int16_t *fx_row_val(ju_fxpatch_t *f, const fxrow_t *r)
{
    return r->group == 0 ? &f->m[r->idx] : r->group == 1 ? &f->d[r->idx] : &f->r[r->idx];
}
/* The effects list in pages of up to FX_PER_PAGE rows at double size; a page never mixes the sections (CHORUS & MFX, delay,
 * reverb, tempo). starts[] gets the first row of every page; returns the number of pages. */
#define FX_PER_PAGE 5
#define FX_MAXPAGES 24
static int fx_paginate(const fxrow_t *rows, int n, int *starts)
{
    int i, np = 0, cnt = 0;
    for (i = 0; i < n; i++) {
        if (i == 0 || cnt == FX_PER_PAGE || rows[i].group != rows[i - 1].group) {
            if (np < FX_MAXPAGES - 1) starts[np++] = i;
            cnt = 0;
        }
        cnt++;
    }
    starts[np] = n;
    return np;
}
static int fx_page_of(const int *starts, int np, int row)
{
    int p;
    for (p = np - 1; p > 0; p--) if (row >= starts[p]) break;
    return p;
}

static const uint16_t kFxCols[4] = {K_RED, K_YELLOW, K_BLUE, K_ORANGE};
static const char *const kFxGroups[4] = {"CHORUS+MFX", "DELAY", "REVERB", "TEMPO"};
static const char *const kFxTags[4] = {"MFX", "DLY", "REV", "BPM"};

/* ---- the Q-Link groups */
enum { DK_PATCH, DK_GLOBAL, DK_REV_SEND, DK_DLY_SEND, DK_DLY_TIME, DK_DLY_FB, DK_BALANCE, DK_PORTA };
typedef struct { uint8_t kind, id; const char *name, *longName; } dial_t;
typedef struct { const char *name; uint16_t color; dial_t d[4]; } qgroup_t;
#define PD(id, n, l) {DK_PATCH, (uint8_t)(id), n, l}
#define GD(id, n, l) {DK_GLOBAL, (uint8_t)(id), n, l}
static const qgroup_t kQ[] = {
    {"FILTER", K_RED, {PD(P_VCF_CUT, "CUTOFF", "VCF CUTOFF"), PD(P_VCF_RES, "RESO", "VCF RESONANCE"), PD(P_VCF_ENV, "ENV", "VCF ENV DEPTH"), PD(P_VCF_KEY, "KEY", "VCF KEY FOLLOW")}},
    {"FILTER 2", K_RED, {PD(P_HPF, "HPF", "HIGH-PASS CUTOFF"), PD(P_VCF_LFO, "LFO", "VCF LFO DEPTH"), PD(P_VCF_ENVSEL, "ENV SEL", "VCF ENV SELECT"), PD(P_VCF_SLOPE, "SLOPE", "VCF SLOPE dB/OCT")}},
    {"ENV-1", K_YELLOW, {PD(P_E1_A, "ATTACK", "ENV-1 ATTACK"), PD(P_E1_D, "DECAY", "ENV-1 DECAY"), PD(P_E1_S, "SUSTAIN", "ENV-1 SUSTAIN"), PD(P_E1_R, "RELEASE", "ENV-1 RELEASE")}},
    {"ENV-2", K_CREAM, {PD(P_E2_A, "ATTACK", "ENV-2 ATTACK"), PD(P_E2_D, "DECAY", "ENV-2 DECAY"), PD(P_E2_S, "SUSTAIN", "ENV-2 SUSTAIN"), PD(P_E2_R, "RELEASE", "ENV-2 RELEASE")}},
    {"VCO", K_ORANGE, {PD(P_MIX, "MIX", "SOURCE MIX V1-V2"), PD(P_V2_TUNE, "FINE", "VCO-2 FINE TUNE"), PD(P_V1_XMOD, "XMOD", "CROSS MOD"), PD(P_VM_PW, "PW", "PULSE WIDTH MOD")}},
    {"LFO", K_RED, {PD(P_LFO_RATE, "RATE", "LFO RATE"), PD(P_LFO_DELAY, "DELAY", "LFO DELAY"), PD(P_VM_LFO, "VCO", "VCO LFO DEPTH"), PD(P_VCF_LFO, "VCF", "VCF LFO DEPTH")}},
    {"EFFECTS", K_BLUE, {{DK_REV_SEND, 0, "REVERB", "REVERB SEND"}, {DK_DLY_SEND, 0, "DELAY", "DELAY SEND"}, {DK_DLY_TIME, 0, "TIME", "DELAY TIME"}, {DK_DLY_FB, 0, "FDBACK", "DELAY FEEDBACK"}}},
    {"PLAY", K_ORANGE, {PD(P_VCA_LEVEL, "LEVEL", "VCA LEVEL"), {DK_PORTA, 0, "PORTA", "PORTAMENTO TIME"}, GD(G_ARP_RATE, "ARP", "ARP RATE"), {DK_BALANCE, 0, "BALANCE", "PART BALANCE"}}},
};
#define NQ ((int)(sizeof kQ / sizeof kQ[0]))
#define OVERVIEW_MS 1000u               /* how long the group overview stays after a group change */

typedef struct {
    int page, qgroup;
    int focus[PAGE_COUNT];
    int patch_idx[2];
    uint8_t write_mode, dirty;
    uint8_t saved_global[G_COUNT];
    int last_dial;
    uint32_t last_dial_at, now, overview_at, save_at;
    uint32_t last_turn[4];
    int turning_enc;
    uint8_t overview;
    uint8_t cpu;                        /* percent, for the header */
    uint8_t seq_sub;                    /* the SEQ page: 0 the steps, 1 the setup */
    uint8_t rec_used, panic_done;       /* REC was used as a modifier; PLAY held long enough for all notes off */
    uint32_t play_down_at, seq_save_at, clear_at;
} ui_t;
static ui_t ui = {PAGE_QLINK, 0, {0, 0, 0, 0, 0}, {8, 0}, 0, 0, {0}, -1, 0, 0, 0, 0, {0}, 1, 0, 0};

static void ui_focus_on(int page, int id)
{
    int np, i, k;
    const spage_t *t = sp_tab(page, &np);
    for (i = 0; i < np; i++)
        for (k = 0; k < t[i].n; k++)
            if (t[i].ids[k] == id) { ui.focus[page] = sp_first(page, i) + k; return; }
}

static int cv_text_w_1(const char *x) { return cv_text_w(x, 1); }
static void ju_store_current(void);                       /* ju_store.c */
static int ju_store_seq(int part);
static int ju_store_patch(int idx);

static int ui_layer(void) { return ju_global[G_PANEL] & 1; }
static int ui_focus_count(void)
{
    fxrow_t r[FX_MAXROWS];
    switch (ui.page) {
    case PAGE_QLINK: return 8;
    case PAGE_PANEL: case PAGE_PERF: return sp_total(ui.page);
    case PAGE_FX: return fx_rows(&ju_patch[0].fx, r);
    default: return 65;                                   /* 64 patches plus WRITE */
    }
}
static int ui_focus_param(void)
{
    switch (ui.page) {
    case PAGE_PANEL: case PAGE_PERF: return sp_param(ui.page, ui.focus[ui.page]);
    default: return -1;
    }
}
static uint8_t ui_get(int id) { return id < P_COUNT ? ju_patch[ui_layer()].v[id] : ju_global[id - P_COUNT]; }
static void ui_set(int id, uint8_t v)
{
    if (id < P_COUNT) ju_patch[ui_layer()].v[id] = v;
    else ju_global[id - P_COUNT] = v;
    ui.dirty = 1;
}

/* each knob keeps its own speed, so turning two at once does not disturb either */
static int ui_accel(int enc, uint32_t now, int range)
{
    uint32_t dt = now - ui.last_turn[enc & 3];
    int mul;
    ui.last_turn[enc & 3] = now;
    if (range < 30) return 1;
    mul = dt < 15 ? 8 : dt < 40 ? 4 : dt < 90 ? 2 : 1;
    if (range > 500) mul *= mul;
    return mul;
}

static void ui_adjust(int id, int delta, uint32_t now)
{
    int v, steps;
    if (id < 0 || delta == 0) return;
    steps = ju_psteps(id);
    v = ui_get(id);
    if (steps) {
        v += delta > 0 ? 1 : -1;
        if (v < 0) v = 0;
        if (v >= steps) v = steps - 1;
    } else {
        v += delta * ui_accel(ui.turning_enc, now, 255);
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
    ui_set(id, (uint8_t)v);
    if (id >= P_COUNT) ui.save_at = now + 2000u;
}

static void ui_adjust_fx(int row_idx, int delta, uint32_t now)
{
    ju_fxpatch_t *f = &ju_patch[0].fx;
    fxrow_t rows[FX_MAXROWS];
    const fxrow_t *r;
    int n = fx_rows(f, rows), dir = delta > 0 ? 1 : -1;
    if (delta == 0 || row_idx >= n) return;
    r = &rows[row_idx];
    switch (r->kind) {
    case 0: {
        uint8_t *sw = r->idx == 0 ? &f->mfxOn : r->idx == 1 ? &f->dlyOn : &f->revOn;
        *sw = dir > 0;
        break;
    }
    case 1: {
        int t = (int)f->mfxType + dir;
        if (t < 0) t = MFX_COUNT - 1;
        if (t >= MFX_COUNT) t = 0;
        ju_fx_load_mfx_defaults(f, t);
        break;
    }
    case 3:
        ui_adjust(P_COUNT + G_TEMPO, delta, now);
        return;
    default: {
        const ju_fxparam_t *d = fx_row_def(f, r);
        int16_t *v = fx_row_val(f, r);
        int nv = (int)*v + delta * ui_accel(ui.turning_enc, now, (int)d->hi - (int)d->lo);
        *v = (int16_t)(nv < d->lo ? d->lo : (nv > d->hi ? d->hi : nv));
        break;
    }
    }
    ui.dirty = 1;
    n = fx_rows(f, rows);
    if (ui.focus[PAGE_FX] >= n) ui.focus[PAGE_FX] = n - 1;
}

static void ui_move_focus(int d)
{
    int n = ui_focus_count(), f = ui.focus[ui.page] + d;
    while (f < 0) f += n;
    ui.focus[ui.page] = f % n;
}

static void ui_jump_section(int d)
{
    if (ui.page == PAGE_PANEL || ui.page == PAGE_PERF) {      /* a page forward or back */
        int np, pg;
        sp_tab(ui.page, &np);
        pg = sp_locate(ui.page, ui.focus[ui.page], 0);
        pg = (pg + (d > 0 ? 1 : np - 1)) % np;
        ui.focus[ui.page] = sp_first(ui.page, pg);
    } else if (ui.page == PAGE_FX) {
        fxrow_t rows[FX_MAXROWS];
        int starts[FX_MAXPAGES + 1];
        int n = fx_rows(&ju_patch[0].fx, rows), np = fx_paginate(rows, n, starts), f = ui.focus[PAGE_FX], pg;
        pg = fx_page_of(starts, np, f < n ? f : 0);
        pg = (pg + (d > 0 ? 1 : np - 1)) % np;
        ui.focus[PAGE_FX] = starts[pg];
    } else {
        ui_move_focus(d > 0 ? 8 : -8);
    }
}

/* a patch into a part: the audio applies it at its next block */
static void ui_load_patch(int layer, int index)
{
    if (index < 0 || index >= 64) return;
    ui.patch_idx[layer] = index;
    ju_load_req[layer] = (uint8_t)(index + 1);
}

static void ui_activate(void)                       /* PATCHES: load the patch under the cursor, or write to it */
{
    int f = ui.focus[PAGE_PATCH], l = ui_layer();
    if (f == 64) { ui.write_mode = !ui.write_mode; return; }
    if (ui.write_mode) {
        ju_bank[f] = ju_patch[l];
        ui.patch_idx[l] = f;
        (void)ju_store_patch(f);
        ui.write_mode = 0;
    } else {
        ui_load_patch(l, f);
        ju_store_current();
    }
}

/* ---- Q-Link dials */
static int balance_of(const uint8_t *g) { return g[G_BAL_L] < 255 ? 255 - g[G_BAL_L] : -(255 - g[G_BAL_U]); }   /* -255: part 2 only, +255: part 1 only */

static void q_turn(int di, int delta, uint32_t now)
{
    const dial_t *d = &kQ[ui.qgroup].d[di];
    ju_fxpatch_t *f = &ju_patch[0].fx;
    ui.last_dial = di;
    ui.last_dial_at = now;
    ui.turning_enc = di;
    switch (d->kind) {
    case DK_PATCH: ui_adjust(d->id, delta, now); break;
    case DK_GLOBAL: ui_adjust(P_COUNT + d->id, delta, now); break;
    case DK_REV_SEND:
    case DK_DLY_SEND: {                              /* a send of zero is the section switched off */
        int rev = d->kind == DK_REV_SEND;
        uint8_t *on = rev ? &f->revOn : &f->dlyOn;
        int16_t *send = rev ? &f->r[RV_SEND] : &f->d[DL_SEND];
        int v = *on ? *send : 0;
        v += delta * ui_accel(di, now, 127);
        v = v < 0 ? 0 : (v > 127 ? 127 : v);
        *send = (int16_t)v;
        *on = v > 0;
        break;
    }
    case DK_DLY_TIME:                                /* note values when synced, milliseconds otherwise */
        if (f->d[DL_SYNC]) {
            int v = f->d[DL_NOTE] + (delta > 0 ? 1 : -1);
            f->d[DL_NOTE] = (int16_t)(v < 0 ? 0 : (v > 21 ? 21 : v));
        } else {
            int v = f->d[DL_TIME] + delta * ui_accel(di, now, 1299);
            f->d[DL_TIME] = (int16_t)(v < 1 ? 1 : (v > 1300 ? 1300 : v));
        }
        break;
    case DK_DLY_FB: {
        int v = f->d[DL_FB] + delta * ui_accel(di, now, 196);
        f->d[DL_FB] = (int16_t)(v < -98 ? -98 : (v > 98 ? 98 : v));
        break;
    }
    case DK_BALANCE: {
        int v = balance_of(ju_global) + delta * ui_accel(di, now, 255);
        v = v < -255 ? -255 : (v > 255 ? 255 : v);
        ju_global[G_BAL_U] = (uint8_t)(v < 0 ? 255 + v : 255);
        ju_global[G_BAL_L] = (uint8_t)(v > 0 ? 255 - v : 255);
        ui.save_at = now + 2000u;
        break;
    }
    case DK_PORTA:
        ui_adjust(P_COUNT + G_PORTA_TIME, delta, now);
        if (ju_global[G_PORTA_TIME] == 0) ju_global[G_PORTA_MODE] = PM_OFF;       /* zero is off, turning it up is all it takes */
        else if (ju_global[G_PORTA_MODE] == PM_OFF) ju_global[G_PORTA_MODE] = PM_ON;
        break;
    }
}

static void q_restore(int di, uint32_t now)           /* the dial back to the saved patch's value */
{
    const dial_t *d = &kQ[ui.qgroup].d[di];
    ju_fxpatch_t *f = &ju_patch[0].fx;
    const ju_fxpatch_t *sf = &ju_bank[ui.patch_idx[0]].fx;
    ui.last_dial = di;
    ui.last_dial_at = now;
    switch (d->kind) {
    case DK_PATCH: ju_patch[ui_layer()].v[d->id] = ju_bank[ui.patch_idx[ui_layer()]].v[d->id]; break;
    case DK_GLOBAL: ju_global[d->id] = ui.saved_global[d->id]; break;
    case DK_REV_SEND: f->r[RV_SEND] = sf->r[RV_SEND]; f->revOn = sf->revOn; break;
    case DK_DLY_SEND: f->d[DL_SEND] = sf->d[DL_SEND]; f->dlyOn = sf->dlyOn; break;
    case DK_DLY_TIME: f->d[DL_TIME] = sf->d[DL_TIME]; f->d[DL_NOTE] = sf->d[DL_NOTE]; break;
    case DK_DLY_FB: f->d[DL_FB] = sf->d[DL_FB]; break;
    case DK_BALANCE: ju_global[G_BAL_U] = ui.saved_global[G_BAL_U]; ju_global[G_BAL_L] = ui.saved_global[G_BAL_L]; break;
    case DK_PORTA: ju_global[G_PORTA_TIME] = ui.saved_global[G_PORTA_TIME]; ju_global[G_PORTA_MODE] = ui.saved_global[G_PORTA_MODE]; break;
    }
}

static int q_edited(int di)
{
    const dial_t *d = &kQ[ui.qgroup].d[di];
    const ju_fxpatch_t *f = &ju_patch[0].fx, *sf = &ju_bank[ui.patch_idx[0]].fx;
    switch (d->kind) {
    case DK_PATCH: return ju_patch[ui_layer()].v[d->id] != ju_bank[ui.patch_idx[ui_layer()]].v[d->id];
    case DK_GLOBAL: return ju_global[d->id] != ui.saved_global[d->id];
    case DK_REV_SEND: return f->revOn != sf->revOn || (f->revOn && f->r[RV_SEND] != sf->r[RV_SEND]);
    case DK_DLY_SEND: return f->dlyOn != sf->dlyOn || (f->dlyOn && f->d[DL_SEND] != sf->d[DL_SEND]);
    case DK_DLY_TIME: return f->d[DL_TIME] != sf->d[DL_TIME] || f->d[DL_NOTE] != sf->d[DL_NOTE];
    case DK_DLY_FB: return f->d[DL_FB] != sf->d[DL_FB];
    case DK_BALANCE: return ju_global[G_BAL_U] != ui.saved_global[G_BAL_U] || ju_global[G_BAL_L] != ui.saved_global[G_BAL_L];
    case DK_PORTA: return ju_global[G_PORTA_TIME] != ui.saved_global[G_PORTA_TIME];
    }
    return 0;
}

static int q_norm(int di)                              /* the dial's position, 0..1000 */
{
    const dial_t *d = &kQ[ui.qgroup].d[di];
    const ju_fxpatch_t *f = &ju_patch[0].fx;
    switch (d->kind) {
    case DK_PATCH: {
        int steps = ju_psteps(d->id), v = ju_patch[ui_layer()].v[d->id];
        return steps ? v * 1000 / (steps - 1) : v * 1000 / 255;
    }
    case DK_GLOBAL: return ju_global[d->id] * 1000 / 255;
    case DK_REV_SEND: return f->revOn ? f->r[RV_SEND] * 1000 / 127 : 0;
    case DK_DLY_SEND: return f->dlyOn ? f->d[DL_SEND] * 1000 / 127 : 0;
    case DK_DLY_TIME: return f->d[DL_SYNC] ? f->d[DL_NOTE] * 1000 / 21 : f->d[DL_TIME] * 1000 / 1300;
    case DK_DLY_FB: return (f->d[DL_FB] + 98) * 1000 / 196;
    case DK_BALANCE: return (balance_of(ju_global) + 255) * 1000 / 510;
    case DK_PORTA: return ju_global[G_PORTA_TIME] * 1000 / 255;
    }
    return 0;
}

static void ui_strcat(char *d, int n, const char *s)
{
    int i = (int)str_len(d);
    while (*s && i < n - 1) d[i++] = *s++;
    d[i] = 0;
}
static void ui_num(char *d, int n, int v) { char t[12]; fmt_int(t, v); ui_strcat(d, n, t); }

static void seq_note_name(char *o, int n)                 /* "C#4" */
{
    static const char *const nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    o[0] = 0;
    ui_strcat(o, 8, nm[n % 12]);
    ui_num(o, 8, n / 12 - 1);
}


static void q_format(int di, char *out, int n)
{
    const dial_t *d = &kQ[ui.qgroup].d[di];
    const ju_fxpatch_t *f = &ju_patch[0].fx;
    out[0] = 0;
    switch (d->kind) {
    case DK_PATCH: ju_format_param(d->id, ju_patch[ui_layer()].v[d->id], out, n); break;
    case DK_GLOBAL: ju_format_param(P_COUNT + d->id, ju_global[d->id], out, n); break;
    case DK_REV_SEND: if (f->revOn) ui_num(out, n, f->r[RV_SEND]); else ui_strcat(out, n, "OFF"); break;
    case DK_DLY_SEND: if (f->dlyOn) ui_num(out, n, f->d[DL_SEND]); else ui_strcat(out, n, "OFF"); break;
    case DK_DLY_TIME:
        if (f->d[DL_SYNC]) ju_fx_format(ju_delay_param(DL_NOTE), f->d[DL_NOTE], out, n);
        else { ui_num(out, n, f->d[DL_TIME]); ui_strcat(out, n, " ms"); }
        break;
    case DK_DLY_FB: ui_num(out, n, f->d[DL_FB]); ui_strcat(out, n, " %"); break;
    case DK_BALANCE: {
        int b = balance_of(ju_global) * 100 / 255;
        if (b == 0) ui_strcat(out, n, "CENTRE");
        else { ui_strcat(out, n, b > 0 ? "P1 " : "P2 "); ui_num(out, n, 100 - (b > 0 ? b : -b)); ui_strcat(out, n, "%"); }
        break;
    }
    case DK_PORTA:
        if (ju_global[G_PORTA_TIME] == 0 || ju_global[G_PORTA_MODE] == PM_OFF) ui_strcat(out, n, "OFF");
        else ju_format_param(P_COUNT + G_PORTA_TIME, ju_global[G_PORTA_TIME], out, n);
        break;
    }
}

static void ui_group_to(int g)                         /* the Q-Link page on a group, with the overview up for a moment */
{
    ui.qgroup = ((g % NQ) + NQ) % NQ;
    ui.page = PAGE_QLINK;
    ui.last_dial = -1;
    ui.write_mode = 0;
    ui.overview = 1;
    ui.overview_at = ui.now;
}

/* ---- the sequencer page: KNOB 1 step, 2 note, 3 step type, 4 pattern (setup: 1 length, 2 gate, 3 tempo, 4 pattern) */
static ju_pat_t *ui_pat(void) { return &ju_pat[ui_layer()][ju_seq_cur[ui_layer()] % JU_SEQ_PATS]; }
static void ui_seq_clear(void)
{
    ju_pat_t *t = ui_pat();
    int i;
    for (i = 0; i < JU_SEQ_STEPS; i++) { t->type[i] = SQ_REST; t->note[i] = 60; }
    ju_seq_cursor[ui_layer()] = 0;
    ju_seq_dirty[ui_layer()] = 1;
}
static void ui_seq_input(const ju_in_t *in, uint32_t now)
{
    int p = ui_layer(), n;
    ju_pat_t *t = ui_pat();
    uint8_t *cur = &ju_seq_cursor[p];
    ui.turning_enc = 1;
    if (!t->len) t->len = 16;
    if (*cur >= t->len) *cur = (uint8_t)(t->len - 1);
    if (!ui.seq_sub) {
        if (in->knob[0]) *cur = (uint8_t)(((int)*cur + in->knob[0] % t->len + t->len) % t->len);
        if (in->knob[1]) {                                  /* the note; turning a rest makes it a note */
            n = (int)t->note[*cur] + in->knob[1];
            t->note[*cur] = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
            if (t->type[*cur] == SQ_REST) t->type[*cur] = SQ_NOTE;
            ju_seq_dirty[p] = 1;
        }
        if (in->knob[2]) { t->type[*cur] = (uint8_t)(((int)t->type[*cur] + (in->knob[2] > 0 ? 1 : 2)) % 3); ju_seq_dirty[p] = 1; }
        if (in->press & BTN(B_SCL)) { t->type[*cur] = SQ_REST; *cur = (uint8_t)((*cur + 1) % t->len); ju_seq_dirty[p] = 1; }
    } else {
        if (in->knob[0]) {
            n = (int)t->len + in->knob[0] * ui_accel(0, now, 63);
            t->len = (uint8_t)(n < 1 ? 1 : n > JU_SEQ_STEPS ? JU_SEQ_STEPS : n);
            if (*cur >= t->len) *cur = (uint8_t)(t->len - 1);
            ju_seq_dirty[p] = 1;
        }
        if (in->knob[1]) { n = (int)t->gate + in->knob[1]; t->gate = (uint8_t)(n < 1 ? 1 : n > 6 ? 6 : n); ju_seq_dirty[p] = 1; }
        if (in->knob[2]) { ui.turning_enc = 2; ui_adjust(P_COUNT + G_TEMPO, in->knob[2], now); }
    }
    if (in->knob[3]) {                                      /* the pattern (both pages) */
        ju_seq_cur[p] = (uint8_t)(((int)ju_seq_cur[p] + (in->knob[3] > 0 ? 1 : JU_SEQ_PATS - 1)) % JU_SEQ_PATS);
        ju_seq_cursor[p] = 0;
        ui.save_at = now + 2000u;                           /* the selection is saved with the settings */
    }
}

/* one UI frame of input */
static void ui_update(const ju_in_t *in, uint32_t now)
{
    int i, l = ui_layer();
    ui.now = now;

    if (in->press & BTN(B_HOME)) { if (ui.page == PAGE_QLINK) ui_group_to(ui.qgroup + 1); else { ui.page = PAGE_QLINK; ui.write_mode = 0; } }
    if (in->press & BTN(B_EDIT)) { if (ui.page == PAGE_PANEL) ui_jump_section(1); ui.page = PAGE_PANEL; ui.write_mode = 0; }
    if (in->press & BTN(B_GLO)) { if (ui.page == PAGE_PERF) ui_jump_section(1); ui.page = PAGE_PERF; ui.write_mode = 0; }
    if (in->press & BTN(B_FX)) { if (ui.page == PAGE_FX) ui_jump_section(1); ui.page = PAGE_FX; ui.write_mode = 0; }
    if (in->press & BTN(B_SAVE)) { ui.page = PAGE_PATCH; ui.write_mode = 0; }
    if (in->press & BTN(B_ENV)) ui_group_to(ui.page == PAGE_QLINK && ui.qgroup == 2 ? 3 : 2);
    if (in->press & BTN(B_LFO)) ui_group_to(5);
    if (in->press & BTN(B_SEQ)) { if (ui.page == PAGE_SEQ) ui.seq_sub ^= 1; else { ui.page = PAGE_SEQ; ui.seq_sub = 0; } ui.write_mode = 0; }
    if ((in->press & BTN(B_SCL)) && ui.page != PAGE_SEQ) ui_group_to(7);          /* (on the SEQ page SCL clears a step) */
    if (in->press & BTN(B_ARP)) { ju_global[G_ARP_ON] ^= 1; ui.save_at = now + 2000u; }
    /* PLAY: a tap starts / stops the sequencer, held for 0.6 s it is all notes off */
    if (in->press & BTN(B_PLAY)) { ui.play_down_at = now ? now : 1u; ui.panic_done = 0; }
    if ((in->held & BTN(B_PLAY)) && ui.play_down_at && !ui.panic_done && now - ui.play_down_at >= 600u) {
        ju_panic_req = 1; ju_seq_req = 2; ui.panic_done = 1;
    }
    if ((in->released & BTN(B_PLAY)) && ui.play_down_at) {
        if (!ui.panic_done) ju_seq_req = ju_seq_run ? 2 : 1;
        ui.play_down_at = 0;
    }
    /* REC: tapped, it arms recording; used with a knob (the dial restore) it does not */
    if (in->press & BTN(B_REC)) ui.rec_used = 0;
    if ((in->released & BTN(B_REC)) && !ui.rec_used) ju_seq_rec ^= 1;
    if (ui.page == PAGE_SEQ && ui.seq_sub) {
        if (in->press & BTN(B_OCTDN)) {                  /* OCT- twice within two seconds: the pattern is cleared */
            if (ui.clear_at && now - ui.clear_at < 2000u) { ui_seq_clear(); ui.clear_at = 0; } else ui.clear_at = now ? now : 1u;
        }
        if (ui.clear_at && now - ui.clear_at >= 2000u) ui.clear_at = 0;
    } else if (ui.page == PAGE_PATCH) {
        if (in->press & BTN(B_OCTUP)) ui_activate();
        if (in->press & BTN(B_OCTDN)) ui.write_mode = 0;
    } else {
        if (in->press & BTN(B_OCTUP)) { if (ju_octave < 3) ju_octave++; }
        if (in->press & BTN(B_OCTDN)) { if (ju_octave > -3) ju_octave--; }
        if ((in->held & BTN(B_OCTUP)) && (in->held & BTN(B_OCTDN))) ju_octave = 0;
    }

    if (in->sel) ui_group_to(ui.qgroup + (in->sel > 0 ? 1 : -1));
    if (in->algo) ju_global[G_PANEL] ^= 1;
    if (in->preset) ui_load_patch(l, (ui.patch_idx[l] + (in->preset > 0 ? 1 : 63)) % 64);

    if (ui.page == PAGE_QLINK) {
        for (i = 0; i < 4; i++) {
            if (!in->knob[i]) continue;
            ui.overview = 0;                           /* back to the dials; the move still counts */
            if (in->held & BTN(B_REC)) { q_restore(i, now); ui.rec_used = 1; }
            else q_turn(i, in->knob[i], now);
        }
    } else if (ui.page == PAGE_SEQ) {
        ui_seq_input(in, now);
    } else {
        ui.turning_enc = 1;
        if (in->knob[0]) ui_move_focus(in->knob[0]);
        if (ui.page == PAGE_PATCH) {
            if (in->knob[1]) ui_move_focus(in->knob[1] > 0 ? 8 : -8);
        } else if (in->knob[1]) {
            if (ui.page == PAGE_FX) ui_adjust_fx(ui.focus[PAGE_FX], in->knob[1], now);
            else ui_adjust(ui_focus_param(), in->knob[1], now);
        }
        if (in->knob[2]) ui_jump_section(in->knob[2]);
        if (in->knob[3]) ui_load_patch(l, (ui.patch_idx[l] + (in->knob[3] > 0 ? 1 : 63)) % 64);
    }
    if (ui.overview && now - ui.overview_at >= OVERVIEW_MS) ui.overview = 0;

    if ((ju_seq_dirty[0] || ju_seq_dirty[1]) && !ui.seq_save_at) ui.seq_save_at = now + 2000u;
    if (ui.seq_save_at && (int32_t)(now - ui.seq_save_at) > 0 && !ju_seq_run) {
        int silent = 1, part;
        for (i = 0; i < JU_NV; i++) silent = silent && ju_voice_level(i) == 0;
        if (silent) {                                    /* a flash write stalls the audio: only when nothing sounds and it is not playing */
            ui.seq_save_at = 0;
            for (part = 0; part < 2; part++) if (ju_seq_dirty[part]) { ju_seq_dirty[part] = 0; (void)ju_store_seq(part); }
            ju_store_current();
        }
    }
    /* a flash write stalls the audio for a moment: the globals save themselves only when nothing is sounding */
    if (ui.save_at && (int32_t)(now - ui.save_at) > 0) {
        int silent = 1;
        for (i = 0; i < JU_NV; i++) silent = silent && ju_voice_level(i) == 0;
        if (silent) {
            ui.save_at = 0;
            memcpy(ui.saved_global, ju_global, G_COUNT);
            ju_store_current();
        }
    }
}

/* ------------------------------------------------------------------ drawing */
static void d_header(void)
{
    int lay = ui_layer(), pi = ui.patch_idx[lay], i;
    char buf[16];
    cv_rect(0, 0, CV_W, 22, K_BG);
    cv_text(4, 4, "JUPITER", K_TEXT, 2);
    cv_text(88, 4, "-8", K_RED, 2);
    /* patch LEDs: bank and number, 11-88 */
    cv_rect(118, 1, 53, 21, CV_RGB(24, 4, 4));
    cv_seg7(122, 2, lay ? 'L' : 'U', K_LEDON, CV_RGB(36, 8, 6));
    cv_seg7(141, 2, (char)('1' + pi / 8), K_LEDON, CV_RGB(36, 8, 6));
    cv_seg7(156, 2, (char)('1' + pi % 8), K_LEDON, CV_RGB(36, 8, 6));
    /* voice LEDs, brightness from ENV-2 */
    for (i = 0; i < JU_NV; i++) {
        int32_t lv = ju_voice_level(i) >> 22;            /* 0..256 */
        int r = 40 + (lv > 255 ? 215 : lv * 215 / 255);
        cv_rect(174 + i * 8, 2, 6, 5, cv_rgb((uint32_t)r, (uint32_t)(r / 6), (uint32_t)(r / 8)));
    }
    buf[0] = 0;
    ui_strcat(buf, sizeof buf, "CPU ");
    ui_num(buf, sizeof buf, ui.cpu);
    cv_text(174, 12, buf, ui.cpu > 85 ? K_RED : K_DIM, 1);
    if (ju_seq_rec) cv_text(214, 12, "REC", K_RED, 1);
    else if (ju_seq_run) cv_text(214, 12, "PLAY", K_FOCUS, 1);
    else if (ju_global[G_ARP_ON]) cv_text(214, 12, "ARP", K_YELLOW, 1);
    cv_rect(0, 22, CV_W, 1, K_LINE);
}

/* One control, large: a slider with a long track, or a selector with its values stacked, or a window for a long list. Names
 * and values at twice the normal size when they fit. (x, y, w, h) is the control's area. */
static void d_control(int id, int x, int y, int w, int h, int foc)
{
    int steps = ju_psteps(id), v = ui_get(id), nameS = 2;
    const char *nm = ju_pshort(id);
    const int labelH = 18;
    int top = y + 4, bot = y + h - labelH - 2;
    uint16_t ink = foc ? K_FOCUS : K_TEXT;
    if (foc) cv_rect(x + 1, y, w - 2, h, K_FOCBG);
    if (cv_text_w(nm, 2) > w - 2) nameS = 1;
    cv_text(x + (w - cv_text_w(nm, nameS)) / 2, y + h - (nameS == 2 ? 15 : 11), nm, ink, nameS);
    if (steps == 0) {                                   /* a slider: a track with its scale and a pale cap */
        int cx = x + w / 2, H = bot - top, t, fy, cw = w - 14 > 32 ? 32 : w - 14;
        cv_rect(cx - 1, top, 3, H, K_LINE);
        for (t = 0; t <= 10; t++) cv_rect(cx - 6, top + (H - 1) * t / 10, t % 5 ? 5 : 11, 1, CV_RGB(60, 60, 66));
        fy = bot - 8 - (H - 8) * v / 255;
        cv_rect(cx - cw / 2, fy, cw, 9, foc ? K_FOCUS : K_CREAM);
        cv_rect(cx - cw / 2, fy + 4, cw, 1, K_BG);
    } else if (steps > 6) {                             /* a MIDI channel: the current one in a window */
        const char *lab = ju_plabel(id, v < steps ? v : 0);
        int boxY = top + (bot - top) / 2 - 14, s2 = cv_text_w(lab, 2) <= w - 6 ? 2 : 1;
        cv_frame(x + 3, boxY, w - 6, 28, foc ? K_FOCUS : K_LINE);
        cv_text(x + (w - cv_text_w(lab, s2)) / 2, boxY + (s2 == 2 ? 7 : 10), lab, foc ? K_FOCUS : K_LEDON, s2);
    } else {                                            /* a selector: the values stacked, an LED beside the active one */
        int n = steps, rowH = (bot - top) / n, yy = top, i, s2 = 2;
        if (rowH > 30) rowH = 30;
        if (rowH < 15) s2 = 1;
        for (i = 0; i < n; i++) {                       /* the last value on top, as a switch reads (the first at the bottom) */
            int val = n - 1 - i, on = v == val;
            const char *lb = ju_plabel(id, val);
            int ls = (s2 == 2 && cv_text_w(lb, 2) + 12 <= w) ? 2 : 1;
            cv_rect(x + 3, yy + rowH / 2 - 3, 4, 7, on ? K_LEDON : K_LEDOFF);
            cv_text(x + 10, yy + (rowH - 7 * ls) / 2, lb, on ? ink : K_DIM, ls);
            yy += rowH;
        }
    }
}

/* a page of the EDIT or GLO series: a title bar with a dot for each page, and its controls across the width */
static void d_spage(int page)
{
    int np, k, pg = sp_locate(page, ui.focus[page], &k), i;
    const spage_t *t = sp_tab(page, &np), *p;
    p = &t[pg];
    cv_rect(0, 24, CV_W, 20, p->color);
    cv_text(6, 27, p->title, K_BG, 2);
    for (i = 0; i < np; i++) {                          /* where this page is among the others */
        int x = CV_W - 6 - (np - i) * 10;
        cv_rect(x, 30, 8, 8, i == pg ? K_BG : p->color);
        cv_frame(x, 30, 8, 8, K_BG);
    }
    if (!cv_sees(46, 152)) return;
    for (i = 0; i < p->n; i++) {
        int x0 = i * CV_W / p->n, x1 = (i + 1) * CV_W / p->n;
        d_control(p->ids[i], x0, 46, x1 - x0, 152, i == k);
    }
}
static void d_panel(void) { d_spage(PAGE_PANEL); }
static void d_perf(void) { d_spage(PAGE_PERF); }

static void d_patches(void)
{
    static const uint16_t cols[8] = {K_RED, K_RED, K_ORANGE, K_ORANGE, K_YELLOW, K_YELLOW, K_CREAM, K_BLUE};
    const int ox = 12, oy = 31, cw = 26, ch = 14;
    int i, cur = ui.patch_idx[ui_layer()], f = ui.focus[PAGE_PATCH];
    const ju_patch_t *hp = &ju_bank[f < 64 ? f : cur];
    for (i = 1; i <= 8; i++) {
        char b[2];
        b[0] = (char)('0' + i); b[1] = 0;
        cv_text(ox + 11 + (i - 1) * (cw + 1), 25, b, K_DIM, 1);
    }
    for (i = 0; i < 64; i++) {
        int bank = i / 8, num = i % 8, x = ox + num * (cw + 1), y = oy + bank * (ch + 1);
        char t[3];
        if (!cv_sees(y - 1, ch + 2)) continue;
        cv_rect(x, y, cw, ch, i == cur ? cols[num] : K_PANEL);
        cv_rect(x, y + ch - 2, cw, 2, cols[num]);
        t[0] = (char)('1' + bank); t[1] = (char)('1' + num); t[2] = 0;
        cv_text(x + 7, y + 3, t, i == cur ? K_BG : K_TEXT, 1);
        if (i == f) { cv_frame(x - 1, y - 1, cw + 2, ch + 2, K_FOCUS); cv_frame(x, y, cw, ch, K_FOCUS); }
    }
    cv_text(6, 158, hp->name, K_TEXT, 2);
    cv_text(6, 178, ui.write_mode ? "PICK A SLOT" : "OCT+ LOADS", ui.write_mode ? K_RED : K_DIM, 1);
    cv_text(6, 186, ui.write_mode ? "OCT+ WRITES  OCT- CANCELS" : "KNOB 1/2 MOVE", ui.write_mode ? K_RED : K_DIM, 1);
    cv_rect(160, 168, 70, 24, f == 64 ? K_FOCUS : (ui.write_mode ? K_RED : K_PANEL));
    cv_frame(160, 168, 70, 24, K_RED);
    cv_text(180, 177, "WRITE", f == 64 ? K_BG : K_TEXT, 1);
}

static void d_fx(void)
{
    ju_fxpatch_t *f = &ju_patch[0].fx;
    fxrow_t rows[FX_MAXROWS];
    int starts[FX_MAXPAGES + 1];
    int n = fx_rows(f, rows), np = fx_paginate(rows, n, starts), fo = ui.focus[PAGE_FX], pg, k, i, g;
    const uint8_t on[4] = {f->mfxOn, f->dlyOn, f->revOn, 1};
    static const char *const title[4] = {"CHORUS", "DELAY", "REVERB", "TEMPO"};
    char val[24];
    if (fo >= n) fo = n - 1;
    pg = fx_page_of(starts, np, fo);
    g = rows[starts[pg]].group;
    cv_rect(0, 24, CV_W, 20, on[g] ? kFxCols[g] : K_PANEL);      /* the bar is lit while that effect is on */
    cv_text(6, 27, title[g], on[g] ? K_BG : K_DIM, 2);
    for (i = 0; i < np; i++) {                           /* a dot a page; the bar's own colour marks the others of this section */
        int x = CV_W - 4 - (np - i) * 9;
        cv_rect(x, 30, 7, 8, i == pg ? K_BG : (rows[starts[i]].group == g ? K_DIM : K_LINE));
        if (i == pg) cv_frame(x, 30, 7, 8, K_TEXT);
    }
    if (!cv_sees(46, 152)) return;
    for (k = starts[pg]; k < starts[pg + 1]; k++) {
        const fxrow_t *r = &rows[k];
        int y = 46 + (k - starts[pg]) * 30, foc = k == fo, cur = 0, lo = 0, hi = 1, nw, vw, vs = 2, ns;
        const char *name = "";
        cv_rect(0, y, CV_W, 29, foc ? K_FOCBG : K_PANEL);
        cv_rect(0, y, 3, 29, kFxCols[r->group]);
        val[0] = 0;
        switch (r->kind) {
        case 0: name = "SWITCH"; cur = on[r->idx]; ui_strcat(val, sizeof val, cur ? "ON" : "OFF"); break;
        case 1: name = "TYPE"; cur = f->mfxType; hi = MFX_COUNT - 1; ui_strcat(val, sizeof val, ju_mfx_name(f->mfxType)); break;
        case 3: name = "BPM"; cur = ju_global[G_TEMPO]; hi = 255; ui_num(val, sizeof val, 40 + cur); break;
        default: {
            const ju_fxparam_t *d = fx_row_def(f, r);
            name = d->name; cur = *fx_row_val(f, r); lo = d->lo; hi = d->hi;
            ju_fx_format(d, cur, val, sizeof val);
            break;
        }
        }
        nw = cv_text_w(name, 2);
        vw = cv_text_w(val, 2);
        ns = 2;
        if (8 + nw + 10 + vw > CV_W - 6) {                 /* too wide: the label small first, then the value small */
            ns = 1;
            if (8 + cv_text_w(name, 1) + 10 + vw > CV_W - 6) vs = 1;
        }
        cv_text(8, y + (ns == 2 ? 7 : 11), name, foc ? K_FOCUS : K_TEXT, ns);
        cv_text(CV_W - 6 - cv_text_w(val, vs), y + (vs == 2 ? 7 : 11), val, foc ? K_FOCUS : K_TEXT, vs);
        if (r->kind == 2 && hi > lo) cv_rect(8, y + 25, (cur - lo) * (CV_W - 16) / (hi - lo), 2, kFxCols[r->group]);
    }
}

/* all groups, one row each, with a square around the group the knobs control */
static void d_overview(void)
{
    const int y0 = 24, rowH = 19, nameW = 64, colW = (CV_W - nameW - 4) / 4;
    int r, i, y;
    for (r = 0; r < NQ; r++) {
        const qgroup_t *g = &kQ[r];
        int yy = y0 + r * rowH, ty = yy + 6, on = r == ui.qgroup;
        if (on) cv_rect(2, yy, CV_W - 4, rowH, K_PANEL);
        cv_rect(5, yy + 4, 3, rowH - 8, g->color);
        cv_text(11, ty, g->name, on ? K_TEXT : K_DIM, 1);
        for (i = 0; i < 4; i++) cv_text(nameW + i * colW + 2, ty, g->d[i].name, on ? K_TEXT : K_DIM, 1);
    }
    y = y0 + ui.qgroup * rowH;
    cv_frame(2, y, CV_W - 4, rowH, K_FOCUS);
    cv_frame(3, y + 1, CV_W - 6, rowH - 2, K_FOCUS);
    cv_rect(0, 176, CV_W, 1, K_LINE);
}

static void d_qlink(void)
{
    const qgroup_t *g = &kQ[ui.qgroup];
    int i, k;
    char val[24];
    int fresh = ui.now - ui.last_dial_at < 1200u;
    if (ui.overview) { d_overview(); return; }
    cv_rect(0, 24, CV_W, 26, K_PANEL);                  /* group bar: its name, and one lit tab per group */
    cv_rect(0, 24, 4, 26, g->color);
    cv_text(10, 31, g->name, K_TEXT, 2);
    for (i = 0; i < NQ; i++) {
        int x = CV_W - 8 - (NQ - i) * 15, on = i == ui.qgroup;
        cv_rect(x, 31, 12, 12, on ? kQ[i].color : K_BG);
        cv_frame(x, 31, 12, 12, on ? kQ[i].color : K_LINE);
    }
    for (i = 0; i < 4; i++) {                           /* four dials, in the order of the knobs below the screen */
        const int w = CV_W / 4, x0 = i * w, cx = x0 + w / 2, cy = 102, rad = 24;
        const int active = fresh && ui.last_dial == i;
        const dial_t *d = &g->d[i];
        int nw = cv_text_w(d->name, 1), lit = (q_norm(i) * 32 + 500) / 1000, vw, kk;
        if (active) cv_rect(x0 + 1, 52, w - 2, 124, K_FOCBG);
        cv_text(cx - nw / 2, 56, d->name, active ? K_FOCUS : K_TEXT, 1);
        for (k = 0; k < 33; k++) {                      /* 270 degrees of dots, 7 o'clock to 5 o'clock */
            int px = cx + (kCos[k] * rad) / 256, py = cy + (kSin[k] * rad) / 256, on = k <= lit, sz = on ? 3 : 2;
            cv_rect(px - sz / 2, py - sz / 2, sz, sz, on ? (active ? K_FOCUS : g->color) : K_LINE);
        }
        kk = lit > 32 ? 32 : lit;                       /* the pointer */
        for (k = 7; k < 19; k++) cv_rect(cx + (kCos[kk] * k) / 256 - 1, cy + (kSin[kk] * k) / 256 - 1, 2, 2, K_TEXT);
        q_format(i, val, sizeof val);
        vw = cv_text_w(val, 1);
        if (cv_text_w(val, 2) <= w - 2) cv_text(cx - cv_text_w(val, 2) / 2, 144, val, active ? K_FOCUS : K_TEXT, 2);
        else cv_text(cx - vw / 2, 148, val, active ? K_FOCUS : K_TEXT, 1);
        if (q_edited(i)) cv_rect(cx - 2, 162, 5, 3, K_LEDON);   /* a small mark: differs from the saved patch */
    }
    cv_rect(0, 176, CV_W, 1, K_LINE);
}

static void d_readout(void)
{
    const int y = 200;
    int id = ui_focus_param(), lay = ui_layer();
    char val[24], line[40];
    if (ui.page == PAGE_QLINK) {
        const int yy = 178;
        cv_rect(0, yy, CV_W, CV_H - yy, K_BG);
        line[0] = 0;
        ui_strcat(line, sizeof line, "PART ");
        ui_num(line, sizeof line, lay + 1);
        ui_strcat(line, sizeof line, "  CH ");
        ui_strcat(line, sizeof line, ju_plabel(P_COUNT + G_CH_U, ju_global[lay ? G_CH_L : G_CH_U] % 17));
        cv_text(6, yy + 4, line, K_DIM, 1);
        cv_text(6, yy + 14, ju_bank[ui.patch_idx[lay]].name, K_TEXT, 2);
        if (ui.now - ui.last_dial_at < 1200u && ui.last_dial >= 0) {
            const char *ln = kQ[ui.qgroup].d[ui.last_dial].longName;
            q_format(ui.last_dial, val, sizeof val);
            cv_text(CV_W - cv_text_w_1(ln) - 6, yy + 4, ln, K_DIM, 1);
            cv_text(CV_W - cv_text_w(val, 2) - 6, yy + 22, val, K_FOCUS, 2);
        }
        cv_text(6, yy + 40, "SELECT GROUP  ALGO PART  PRESETS PATCH", K_DIM, 1);
        cv_text(6, yy + 48, "REC + KNOB: SAVED VALUE", K_DIM, 1);
        return;
    }
    cv_rect(0, y, CV_W, CV_H - y, K_BG);
    cv_rect(0, y, CV_W, 1, K_LINE);
    if (ui.page == PAGE_FX) {
        ju_fxpatch_t *f = &ju_patch[0].fx;
        fxrow_t rows[FX_MAXROWS];
        int n = fx_rows(f, rows), fo = ui.focus[PAGE_FX], vw;
        const fxrow_t *r;
        const char *pname;
        char title[32];
        if (fo >= n) fo = n - 1;
        r = &rows[fo];
        pname = r->kind == 0 ? "SWITCH" : r->kind == 1 ? "EFFECT TYPE" : r->kind == 3 ? "BPM" : fx_row_def(f, r)->name;
        title[0] = 0;
        ui_strcat(title, sizeof title, kFxGroups[r->group]);
        ui_strcat(title, sizeof title, " ");
        ui_strcat(title, sizeof title, pname);
        val[0] = 0;
        if (r->kind == 2) ju_fx_format(fx_row_def(f, r), *fx_row_val(f, r), val, sizeof val);
        else if (r->kind == 1) ui_strcat(val, sizeof val, ju_mfx_name(f->mfxType));
        else if (r->kind == 3) { ui_num(val, sizeof val, 40 + ju_global[G_TEMPO]); ui_strcat(val, sizeof val, " BPM"); }
        else ui_strcat(val, sizeof val, (r->idx == 0 ? f->mfxOn : r->idx == 1 ? f->dlyOn : f->revOn) ? "ON" : "OFF");
        cv_text(6, y + 5, title, K_DIM, 1);
        vw = cv_text_w(val, 2);
        if (vw > CV_W - 12) cv_text(CV_W - cv_text_w(val, 1) - 6, y + 18, val, K_FOCUS, 1);
        else cv_text(CV_W - vw - 6, y + 16, val, K_FOCUS, 2);
        cv_text(6, y + 30, "SAVED WITH THE PATCH", K_DIM, 1);
        return;
    }
    if (ui.page == PAGE_SEQ) {
        const ju_pat_t *t = &ju_pat[lay][ju_seq_cur[lay] % JU_SEQ_PATS];
        int len = t->len ? t->len : 16, cur = ju_seq_cursor[lay] % len;
        char b[24], v[12];
        if (ui.seq_sub) {
            cv_text(6, y + 5, ui.clear_at ? "PRESS OCT- AGAIN TO CLEAR THE PATTERN" : "THE FOUR KNOBS SET THE FOUR ROWS", ui.clear_at ? K_RED : K_DIM, 1);
            cv_text(6, y + 18, "OCT- TWICE CLEARS THE PATTERN", K_DIM, 1);
            cv_text(6, y + 30, "SEQ AGAIN: THE STEPS", K_DIM, 1);
            return;
        }
        b[0] = 0;
        ui_strcat(b, sizeof b, "STEP ");
        ui_num(b, sizeof b, cur + 1);
        cv_text(6, y + 5, b, K_DIM, 1);
        if (t->type[cur] == SQ_NOTE) seq_note_name(v, t->note[cur]);
        else { v[0] = 0; ui_strcat(v, sizeof v, t->type[cur] == SQ_TIE ? "TIE" : "REST"); }
        cv_text(CV_W - cv_text_w(v, 2) - 6, y + 14, v, K_FOCUS, 2);
        cv_text(6, y + 18, ju_seq_rec ? (ju_seq_run ? "REC: PLAY KEYS" : "REC: KEYS ENTER STEPS") : "REC ARMS RECORDING", ju_seq_rec ? K_RED : K_DIM, 1);
        cv_text(6, y + 30, "KNOB 1 STEP 2 NOTE 3 TYPE 4 PATTERN", K_DIM, 1);
        return;
    }
    if (ui.page == PAGE_PATCH) {
        const ju_patch_t *p = &ju_patch[lay];
        cv_text(6, y + 6, p->name, K_TEXT, 2);
        line[0] = 0;
        ui_strcat(line, sizeof line, lay ? "PART 2 LOWER" : "PART 1 UPPER");
        ui_strcat(line, sizeof line, "  MIDI CH ");
        ui_strcat(line, sizeof line, ju_plabel(P_COUNT + G_CH_U, ju_global[lay ? G_CH_L : G_CH_U] % 17));
        cv_text(6, y + 26, line, K_DIM, 1);
        return;
    }
    if (id < 0) return;
    ju_format_param(id, ui_get(id), val, sizeof val);
    cv_text(6, y + 5, ju_plong(id), K_DIM, 1);
    cv_text(CV_W - cv_text_w(val, 2) - 6, y + 14, val, K_FOCUS, 2);
    cv_text(6, y + 20, ju_bank[ui.patch_idx[lay]].name, K_DIM, 1);
    cv_text(6, y + 30, "KNOB 1 MOVE  KNOB 2 VALUE  HOME DIALS", K_DIM, 1);
}

/* ---- the sequencer: the steps of a part's pattern, 16 at a time (4 x 4 at double size), and a setup page */
static void d_seq(void)
{
    int p = ui_layer();
    const ju_pat_t *t = &ju_pat[p][ju_seq_cur[p] % JU_SEQ_PATS];
    int len = t->len ? t->len : 16, cur = ju_seq_cursor[p] % len, base = (cur / 16) * 16, i;
    char b[24], b2[12];
    uint16_t col = p ? K_BLUE : K_ORANGE;
    cv_rect(0, 24, CV_W, 20, col);
    b[0] = 0;
    ui_strcat(b, sizeof b, ui.seq_sub ? "SETUP " : "SEQ ");
    ui_strcat(b, sizeof b, p ? "L" : "U");
    ui_strcat(b, sizeof b, " P");
    if (ju_seq_cur[p] < 9) ui_strcat(b, sizeof b, "0");
    ui_num(b, sizeof b, ju_seq_cur[p] + 1);
    cv_text(6, 27, b, K_BG, 2);
    if (!ui.seq_sub) {
        b[0] = 0;
        ui_num(b, sizeof b, base + 1); ui_strcat(b, sizeof b, "-"); ui_num(b, sizeof b, base + 16 > len ? len : base + 16);
        ui_strcat(b, sizeof b, "/"); ui_num(b, sizeof b, len);
        cv_text(CV_W - 6 - cv_text_w(b, 1), 31, b, K_BG, 1);
    }
    if (!cv_sees(46, 152)) return;
    if (ui.seq_sub) {                                      /* the setup: LENGTH, GATE, TEMPO, PATTERN, one a knob */
        static const char *const nm[4] = {"LENGTH", "GATE", "TEMPO", "PATTERN"};
        for (i = 0; i < 4; i++) {
            int y = 46 + i * 30;
            b[0] = 0;
            switch (i) {
            case 0: ui_num(b, sizeof b, len); ui_strcat(b, sizeof b, " STEPS"); break;
            case 1: ui_num(b, sizeof b, t->gate * 100 / 6); ui_strcat(b, sizeof b, " %"); break;
            case 2: ui_num(b, sizeof b, 40 + ju_global[G_TEMPO]); ui_strcat(b, sizeof b, ju_global[G_SYNC] ? " MIDI" : " BPM"); break;
            default: ui_num(b, sizeof b, ju_seq_cur[p] + 1); ui_strcat(b, sizeof b, " OF 16"); break;
            }
            cv_rect(0, y, CV_W, 29, K_PANEL);
            cv_rect(0, y, 3, 29, col);
            cv_text(8, y + 4, "KNOB", K_DIM, 1);
            b2[0] = 0; ui_num(b2, sizeof b2, i + 1);
            cv_text(8 + 30, y + 4, b2, K_DIM, 1);
            cv_text(8, y + 13, nm[i], K_TEXT, 2);
            cv_text(CV_W - 6 - cv_text_w(b, 2), y + 8, b, K_FOCUS, 2);
        }
        return;
    }
    for (i = 0; i < 16; i++) {                             /* the steps */
        int idx = base + i, x = (i % 4) * 60, y = 46 + (i / 4) * 38;
        int on = idx < len, isCur = on && idx == cur, playing = on && ju_seq_run && ju_seq_pos[p] == idx;
        uint16_t ink = isCur ? K_FOCUS : K_TEXT;
        cv_rect(x + 1, y + 1, 58, 36, isCur ? K_FOCBG : (on ? K_PANEL : K_BG));
        if (playing) cv_frame(x + 1, y + 1, 58, 36, K_LEDON);
        if (!on) continue;
        b2[0] = 0; ui_num(b2, sizeof b2, idx + 1);
        cv_text(x + 4, y + 4, b2, isCur ? K_FOCUS : K_DIM, 1);
        if (t->type[idx] == SQ_NOTE) {
            seq_note_name(b, t->note[idx]);
            cv_text(x + (60 - cv_text_w(b, 2)) / 2, y + 16, b, ink, 2);
        } else if (t->type[idx] == SQ_TIE) {
            cv_text(x + (60 - cv_text_w("TIE", 2)) / 2, y + 16, "TIE", isCur ? K_FOCUS : K_YELLOW, 2);
        } else {
            cv_rect(x + 22, y + 22, 16, 2, isCur ? K_FOCUS : K_LINE);
        }
    }
}

static void ui_draw(void)
{
    d_header();
    switch (ui.page) {
    case PAGE_QLINK: d_qlink(); break;
    case PAGE_PANEL: d_panel(); break;
    case PAGE_PERF: d_perf(); break;
    case PAGE_FX: d_fx(); break;
    case PAGE_SEQ: d_seq(); break;
    default: d_patches(); break;
    }
    d_readout();
}
