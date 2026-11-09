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
 *   ARP             arpeggio on / off  PLAY  all notes off  REC held + a knob: that dial back to the saved value
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

enum { PAGE_QLINK, PAGE_PANEL, PAGE_PERF, PAGE_FX, PAGE_PATCH, PAGE_COUNT };
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
#define BTN(b) (1u << (b))

/* what the panel hands the UI each frame */
typedef struct {
    int knob[4];                     /* KNOB 1-4: detents since the last frame */
    int sel, algo, preset;           /* SELECT, ALGORITHM, PRESETS */
    uint32_t press;                  /* buttons pressed since the last frame (bit per B_*) */
    uint32_t held;                   /* buttons down */
} ju_in_t;

static const int16_t kCos[33] = {-181, -206, -226, -241, -251, -256, -255, -248, -237, -220, -198, -172, -142, -109, -74, -38, 0, 38, 74, 109, 142, 172, 198, 220, 237, 248, 255, 256, 251, 241, 226, 206, 181};
static const int16_t kSin[33] = {181, 152, 121, 86, 50, 13, -25, -62, -98, -132, -162, -190, -213, -231, -245, -253, -256, -253, -245, -231, -213, -190, -162, -132, -98, -62, -25, 13, 50, 86, 121, 152, 181};

typedef struct { const char *name; uint16_t color; uint8_t first, count, row; } section_t;
static const section_t kSections[] = {      /* the panel's left-to-right order, three rows */
    {"LFO", K_RED, P_LFO_RATE, 3, 0},
    {"VCO MOD", K_ORANGE, P_VM_LFO, 5, 0},
    {"VCO-1", K_YELLOW, P_V1_XMOD, 3, 0},
    {"VCO-2", K_CREAM, P_V2_SYNC, 5, 1},
    {"MIX", K_BLUE, P_MIX, 1, 1},
    {"HPF", K_BLUE, P_HPF, 1, 1},
    {"VCA", K_ORANGE, P_VCA_LEVEL, 2, 1},
    {"VCF", K_RED, P_VCF_CUT, 7, 2},
    {"ENV-1", K_YELLOW, P_E1_A, 6, 2},
    {"ENV-2", K_CREAM, P_E2_A, 5, 2},
};
#define NSECTIONS ((int)(sizeof kSections / sizeof kSections[0]))
#define PANEL_FOCUS P_ASSIGN                /* the focusable parameters of the panel page: all before ASSIGN */

#define PG(x) (P_COUNT + (x))
typedef struct { const char *name; uint16_t color; int8_t n, row; int ids[6]; } perfgroup_t;
static const perfgroup_t kPerf[] = {
    {"ASSIGN", K_RED, 4, 0, {P_ASSIGN, PG(G_HOLD_U), PG(G_HOLD_L), PG(G_VINTAGE)}},
    {"KEY", K_ORANGE, 3, 0, {PG(G_KEYMODE), PG(G_PANEL), PG(G_SPLIT)}},
    {"ARPEGGIO", K_YELLOW, 4, 1, {PG(G_ARP_ON), PG(G_ARP_MODE), PG(G_ARP_RANGE), PG(G_ARP_RATE)}},
    {"PORTA", K_CREAM, 2, 1, {PG(G_PORTA_MODE), PG(G_PORTA_TIME)}},
    {"BENDER", K_BLUE, 5, 2, {PG(G_BEND_VCO), PG(G_BEND_V1), PG(G_BEND_V2), PG(G_BEND_VCF), PG(G_BEND_VCFSW)}},
    {"LFO MOD", K_RED, 5, 2, {PG(G_MOD_RISE), PG(G_MOD_VCO), PG(G_MOD_VCOSW), PG(G_MOD_VCF), PG(G_MOD_VCFSW)}},
    {"MASTER", K_ORANGE, 4, 3, {PG(G_VOLUME), PG(G_BAL_U), PG(G_BAL_L), PG(G_TUNE)}},
    {"MIDI", K_YELLOW, 2, 3, {PG(G_CH_U), PG(G_CH_L)}},
};
#define NPERF ((int)(sizeof kPerf / sizeof kPerf[0]))
static int perf_total(void) { int n = 0, i; for (i = 0; i < NPERF; i++) n += kPerf[i].n; return n; }
static int perf_param_at(int idx)
{
    int i;
    for (i = 0; i < NPERF; i++) {
        if (idx < kPerf[i].n) return kPerf[i].ids[idx];
        idx -= kPerf[i].n;
    }
    return P_ASSIGN;
}

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
    {"EFFECTS", K_BLUE, {{DK_REV_SEND, 0, "REVERB", "REVERB SEND"}, {DK_DLY_SEND, 0, "DELAY", "DELAY SEND"}, {DK_DLY_TIME, 0, "TIME", "DELAY TIME"}, {DK_DLY_FB, 0, "FEEDBACK", "DELAY FEEDBACK"}}},
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
} ui_t;
static ui_t ui = {PAGE_QLINK, 0, {0, P_VCF_CUT, 0, 0, 0}, {8, 0}, 0, 0, {0}, -1, 0, 0, 0, 0, {0}, 1, 0, 0};

static void ju_store_current(void);                       /* ju_store.c */
static int ju_store_patch(int idx);

static int ui_layer(void) { return ju_global[G_PANEL] & 1; }
static int ui_focus_count(void)
{
    fxrow_t r[FX_MAXROWS];
    switch (ui.page) {
    case PAGE_QLINK: return 8;
    case PAGE_PANEL: return PANEL_FOCUS;
    case PAGE_PERF: return perf_total();
    case PAGE_FX: return fx_rows(&ju_patch[0].fx, r);
    default: return 65;                                   /* 64 patches plus WRITE */
    }
}
static int ui_focus_param(void)
{
    switch (ui.page) {
    case PAGE_PANEL: return ui.focus[PAGE_PANEL];
    case PAGE_PERF: return perf_param_at(ui.focus[PAGE_PERF]);
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
    if (ui.page == PAGE_PANEL) {
        int s = 0, i;
        for (i = 0; i < NSECTIONS; i++) if (ui.focus[PAGE_PANEL] >= kSections[i].first) s = i;
        s = (s + (d > 0 ? 1 : NSECTIONS - 1)) % NSECTIONS;
        ui.focus[PAGE_PANEL] = kSections[s].first;
    } else if (ui.page == PAGE_PERF) {
        int idx = 0, g = 0, f = ui.focus[PAGE_PERF], i;
        for (i = 0; i < NPERF; i++) { if (f >= idx) g = i; idx += kPerf[i].n; }
        g = (g + (d > 0 ? 1 : NPERF - 1)) % NPERF;
        idx = 0;
        for (i = 0; i < g; i++) idx += kPerf[i].n;
        ui.focus[PAGE_PERF] = idx;
    } else if (ui.page == PAGE_FX) {
        fxrow_t rows[FX_MAXROWS];
        int n = fx_rows(&ju_patch[0].fx, rows), f = ui.focus[PAGE_FX], g, ng, i;
        g = rows[f < n ? f : 0].group;
        ng = (g + (d > 0 ? 1 : 3)) % 4;
        for (i = 0; i < n; i++) if (rows[i].group == ng) { ui.focus[PAGE_FX] = i; break; }
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

/* one UI frame of input */
static void ui_update(const ju_in_t *in, uint32_t now)
{
    int i, l = ui_layer();
    ui.now = now;

    if (in->press & BTN(B_HOME)) { if (ui.page == PAGE_QLINK) ui_group_to(ui.qgroup + 1); else { ui.page = PAGE_QLINK; ui.write_mode = 0; } }
    if (in->press & BTN(B_EDIT)) { ui.page = PAGE_PANEL; ui.write_mode = 0; }
    if (in->press & BTN(B_GLO)) { ui.page = PAGE_PERF; ui.write_mode = 0; }
    if (in->press & BTN(B_FX)) { ui.page = PAGE_FX; ui.write_mode = 0; }
    if (in->press & BTN(B_SAVE)) { ui.page = PAGE_PATCH; ui.write_mode = 0; }
    if (in->press & BTN(B_ENV)) ui_group_to(ui.page == PAGE_QLINK && ui.qgroup == 2 ? 3 : 2);
    if (in->press & BTN(B_LFO)) ui_group_to(5);
    if (in->press & BTN(B_SEQ)) ui_group_to(4);
    if (in->press & BTN(B_SCL)) ui_group_to(7);
    if (in->press & BTN(B_ARP)) { ju_global[G_ARP_ON] ^= 1; ui.save_at = now + 2000u; }
    if (in->press & BTN(B_PLAY)) ju_panic_req = 1;
    if (ui.page == PAGE_PATCH) {
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
            if (in->held & BTN(B_REC)) q_restore(i, now);
            else q_turn(i, in->knob[i], now);
        }
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
    const char *km = ju_plabel(P_COUNT + G_KEYMODE, ju_global[G_KEYMODE] % 4);
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
        cv_rect(174 + i * 8, 4, 6, 5, cv_rgb((uint32_t)r, (uint32_t)(r / 6), (uint32_t)(r / 8)));
    }
    buf[0] = 0;
    ui_strcat(buf, sizeof buf, "CPU ");
    ui_num(buf, sizeof buf, ui.cpu);
    cv_tiny(174, 13, buf, ui.cpu > 85 ? K_RED : K_DIM);
    cv_tiny(200, 13, km, K_DIM);
    if (ju_global[G_ARP_ON]) cv_tiny(222, 13, "ARP", K_YELLOW);
    cv_rect(0, 22, CV_W, 1, K_LINE);
}

static void d_control(int id, int x, int y, int w, int h, int foc)
{
    int steps = ju_psteps(id), v = ui_get(id);
    const char *nm = ju_pshort(id);
    const int labelH = 7;
    int top = y + (steps >= 4 && steps <= 6 ? 1 : 2), bot = y + h - labelH - 1;
    if (foc) cv_rect(x, y, w, h, K_FOCBG);
    if (cv_tiny_w(nm) + 1 <= w) cv_tiny(x + (w - cv_tiny_w(nm) + 1) / 2, y + h - labelH + 1, nm, foc ? K_FOCUS : K_DIM);
    else cv_tiny_fit(x, y + h - labelH + 1, nm, foc ? K_FOCUS : K_DIM, w);
    if (steps == 0) {                                   /* slider: track with a tick scale and a pale fader cap */
        int cx = x + w / 2, H = bot - top, t, fy, cw = w > 13 ? 9 : 7;
        cv_rect(cx, top, 1, H, K_LINE);
        for (t = 0; t <= 5; t++) cv_rect(cx - 2, top + (H - 1) * t / 5, 5, 1, CV_RGB(46, 46, 50));
        fy = bot - 3 - (H - 3) * v / 255;
        cv_rect(cx - cw / 2, fy, cw, 4, foc ? K_FOCUS : K_CREAM);
        cv_rect(cx - cw / 2, fy + 1, cw, 1, K_BG);
    } else if (steps > 6) {                             /* too many values to stack (a MIDI channel): the current one in a window */
        const char *lab = ju_plabel(id, v < steps ? v : 0);
        int boxY = y + (h - labelH) / 2 - 8;
        cv_frame(x + 1, boxY, w - 2, 15, foc ? K_FOCUS : K_LINE);
        if (cv_text_w(lab, 1) <= w - 4) cv_text(x + (w - cv_text_w(lab, 1)) / 2, boxY + 4, lab, foc ? K_FOCUS : K_LEDON, 1);
        else cv_tiny(x + (w - cv_tiny_w(lab)) / 2, boxY + 5, lab, foc ? K_FOCUS : K_LEDON);
    } else {                                            /* selector: stacked labels, an LED beside the active one */
        int n = steps, rowH = (bot - top) / (n > 4 ? n : 4), yy = top, i;
        if (rowH < 7) rowH = n >= 4 && (bot - top) < 7 * n ? 6 : 7;
        for (i = n - 1; i >= 0; i--) {                  /* the first value at the bottom, as switches read */
            int on = v == i;
            const char *lb = ju_plabel(id, i);
            cv_rect(x + 1, yy + 1, 2, 3, on ? K_LEDON : K_LEDOFF);
            if (cv_tiny_w(lb) + 4 <= w) cv_tiny(x + 4, yy, lb, on ? (foc ? K_FOCUS : K_TEXT) : K_DIM);
            else cv_tiny_fit(x + 3, yy, lb, on ? (foc ? K_FOCUS : K_TEXT) : K_DIM, w - 3);
            yy += rowH;
        }
    }
}

static void d_panel(void)
{
    static const int rowY[3] = {24, 83, 142};
    const int rowH = 57, gap = 2, selW = 16;
    int r, s, i;
    for (r = 0; r < 3; r++) {
        int nSlider = 0, nSel = 0, nSec = 0, avail, slW, rem, x = gap, sIdx = 0;
        for (s = 0; s < NSECTIONS; s++) if (kSections[s].row == r) {
            nSec++;
            for (i = 0; i < kSections[s].count; i++) { if (ju_psteps(kSections[s].first + i)) nSel++; else nSlider++; }
        }
        if (!cv_sees(rowY[r], rowH)) continue;
        avail = CV_W - gap * (nSec + 1) - selW * nSel;
        slW = avail / nSlider; rem = avail % nSlider;
        for (s = 0; s < NSECTIONS; s++) if (kSections[s].row == r) {
            const section_t *se = &kSections[s];
            int w = 0, cx, tw;
            int ws[8];
            for (i = 0; i < se->count; i++) {
                if (ju_psteps(se->first + i)) ws[i] = selW;
                else ws[i] = slW + (sIdx++ < rem ? 1 : 0);
                w += ws[i];
            }
            cv_rect(x, rowY[r], w, rowH, K_PANEL);
            cv_rect(x, rowY[r], w, 9, se->color);
            tw = cv_tiny_w(se->name);
            cv_tiny(x + (w - tw) / 2, rowY[r] + 2, se->name, K_BG);
            cx = x;
            for (i = 0; i < se->count; i++) {
                d_control(se->first + i, cx, rowY[r] + 10, ws[i], rowH - 11, ui.focus[PAGE_PANEL] == se->first + i);
                cx += ws[i];
            }
            x += w + gap;
        }
    }
}

static void d_perf(void)
{
    const int rowH = 43;
    int r, g, k, focusId = ui_focus_param();
    for (r = 0; r < 4; r++) {
        int total = 0, x = 2, y = 24 + r * 44;
        if (!cv_sees(y, rowH)) continue;
        for (g = 0; g < NPERF; g++) if (kPerf[g].row == r) total += kPerf[g].n;
        for (g = 0; g < NPERF; g++) if (kPerf[g].row == r) {
            const perfgroup_t *pg = &kPerf[g];
            int each = (CV_W - 10) * pg->n / total, wi = each - 2, cw = wi / pg->n;
            cv_rect(x, y, wi, rowH, K_PANEL);
            cv_rect(x, y, wi, 9, pg->color);
            cv_tiny(x + (wi - cv_tiny_w(pg->name)) / 2, y + 2, pg->name, K_BG);
            for (k = 0; k < pg->n; k++) d_control(pg->ids[k], x + k * cw, y + 10, cw, rowH - 11, pg->ids[k] == focusId);
            x += each;
        }
    }
}

static void d_patches(void)
{
    static const uint16_t cols[8] = {K_RED, K_RED, K_ORANGE, K_ORANGE, K_YELLOW, K_YELLOW, K_CREAM, K_BLUE};
    const int ox = 12, oy = 31, cw = 26, ch = 14;
    int i, cur = ui.patch_idx[ui_layer()], f = ui.focus[PAGE_PATCH];
    const ju_patch_t *hp = &ju_bank[f < 64 ? f : cur];
    for (i = 1; i <= 8; i++) {
        char b[2];
        b[0] = (char)('0' + i); b[1] = 0;
        cv_tiny(ox + 11 + (i - 1) * (cw + 1), 25, b, K_DIM);
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
    cv_tiny(6, 178, ui.write_mode ? "PICK A SLOT" : "OCT+ LOADS", ui.write_mode ? K_RED : K_DIM);
    cv_tiny(6, 186, ui.write_mode ? "OCT+ WRITES  OCT- CANCELS" : "KNOB 1/2 MOVE", ui.write_mode ? K_RED : K_DIM);
    cv_rect(160, 168, 70, 24, f == 64 ? K_FOCUS : (ui.write_mode ? K_RED : K_PANEL));
    cv_frame(160, 168, 70, 24, K_RED);
    cv_text(180, 177, "WRITE", f == 64 ? K_BG : K_TEXT, 1);
}

static void d_fx(void)
{
    ju_fxpatch_t *f = &ju_patch[0].fx;
    fxrow_t rows[FX_MAXROWS];
    int n = fx_rows(f, rows), i, k, fo = ui.focus[PAGE_FX], first;
    const uint8_t on[3] = {f->mfxOn, f->dlyOn, f->revOn};
    static const char *const tn[3] = {"CHORUS", "DELAY", "REVERB"};
    const int top = 39, rowH = 12, vis = (199 - top) / rowH;
    char val[24];
    for (i = 0; i < 3; i++) {                           /* three lit tiles, like the panel's effect buttons */
        int x = 2 + i * 79;
        cv_rect(x, 25, 77, 12, on[i] ? kFxCols[i] : K_PANEL);
        cv_text(x + 4, 28, tn[i], on[i] ? K_BG : K_DIM, 1);
    }
    if (fo >= n) fo = n - 1;
    first = fo - vis / 2;
    if (first > n - vis) first = n - vis;
    if (first < 0) first = 0;
    for (k = 0; k < vis && first + k < n; k++) {
        const fxrow_t *r = &rows[first + k];
        int y = top + k * rowH, foc = first + k == fo, cur = 0, lo = 0, hi = 1, vw, nw;
        const char *name = "";
        if (!cv_sees(y, rowH)) continue;
        cv_rect(0, y, CV_W, rowH - 1, foc ? K_FOCBG : K_PANEL);
        cv_rect(0, y, 3, rowH - 1, kFxCols[r->group]);
        cv_tiny(6, y + 3, kFxTags[r->group], K_DIM);
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
        nw = cv_text_w(name, 1);
        vw = cv_text_w(val, 1);
        cv_text(26, y + 2, name, foc ? K_FOCUS : K_TEXT, 1);
        if (26 + nw + 8 + vw <= CV_W - 4) cv_text(CV_W - vw - 4, y + 2, val, foc ? K_FOCUS : K_TEXT, 1);
        else cv_tiny(CV_W - cv_tiny_w(val) - 4, y + 3, val, foc ? K_FOCUS : K_TEXT);
        if (r->kind == 2 && hi > lo) cv_rect(CV_W - 64, y + rowH - 2, (cur - lo) * 60 / (hi - lo), 1, kFxCols[r->group]);
    }
    if (n > vis) {                                      /* scroll position */
        int h = (199 - top) * vis / n, y = top + (199 - top - h) * first / (n - vis);
        cv_rect(CV_W - 2, y, 2, h, K_LINE);
    }
}

/* all groups, one row each, with a square around the group the knobs control */
static void d_overview(void)
{
    const int y0 = 24, rowH = 19, nameW = 66, colW = (CV_W - nameW - 4) / 4;
    int r, i, y;
    for (r = 0; r < NQ; r++) {
        const qgroup_t *g = &kQ[r];
        int yy = y0 + r * rowH, ty = yy + 6, on = r == ui.qgroup;
        if (on) cv_rect(2, yy, CV_W - 4, rowH, K_PANEL);
        cv_rect(5, yy + 4, 3, rowH - 8, g->color);
        cv_text(11, ty, g->name, on ? K_TEXT : K_DIM, 1);
        for (i = 0; i < 4; i++) cv_tiny(nameW + i * colW + 2, ty + 1, g->d[i].name, on ? K_TEXT : K_DIM);
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
        if (vw > w - 4) cv_tiny(cx - cv_tiny_w(val) / 2, 150, val, active ? K_FOCUS : K_TEXT);
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
        cv_tiny(6, yy + 4, line, K_DIM);
        cv_text(6, yy + 14, ju_bank[ui.patch_idx[lay]].name, K_TEXT, 2);
        if (ui.now - ui.last_dial_at < 1200u && ui.last_dial >= 0) {
            const char *ln = kQ[ui.qgroup].d[ui.last_dial].longName;
            q_format(ui.last_dial, val, sizeof val);
            cv_tiny(CV_W - cv_tiny_w(ln) - 6, yy + 4, ln, K_DIM);
            cv_text(CV_W - cv_text_w(val, 2) - 6, yy + 22, val, K_FOCUS, 2);
        }
        cv_tiny(6, yy + 40, "SELECT GROUP  ALGO PART  PRESETS PATCH", K_DIM);
        cv_tiny(6, yy + 48, "REC + KNOB: SAVED VALUE", K_DIM);
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
        cv_tiny(6, y + 30, "SAVED WITH THE PATCH", K_DIM);
        return;
    }
    if (ui.page == PAGE_PATCH) {
        const ju_patch_t *p = &ju_patch[lay];
        cv_text(6, y + 6, p->name, K_TEXT, 2);
        line[0] = 0;
        ui_strcat(line, sizeof line, lay ? "PART 2 LOWER" : "PART 1 UPPER");
        ui_strcat(line, sizeof line, "  MIDI CH ");
        ui_strcat(line, sizeof line, ju_plabel(P_COUNT + G_CH_U, ju_global[lay ? G_CH_L : G_CH_U] % 17));
        cv_tiny(6, y + 26, line, K_DIM);
        return;
    }
    if (id < 0) return;
    ju_format_param(id, ui_get(id), val, sizeof val);
    cv_text(6, y + 5, ju_plong(id), K_DIM, 1);
    cv_text(CV_W - cv_text_w(val, 2) - 6, y + 14, val, K_FOCUS, 2);
    cv_tiny(6, y + 20, ju_bank[ui.patch_idx[lay]].name, K_DIM);
    cv_tiny(6, y + 30, "KNOB 1 MOVE  KNOB 2 VALUE  HOME DIALS", K_DIM);
}

static void ui_draw(void)
{
    d_header();
    switch (ui.page) {
    case PAGE_QLINK: d_qlink(); break;
    case PAGE_PANEL: d_panel(); break;
    case PAGE_PERF: d_perf(); break;
    case PAGE_FX: d_fx(); break;
    default: d_patches(); break;
    }
    d_readout();
}
