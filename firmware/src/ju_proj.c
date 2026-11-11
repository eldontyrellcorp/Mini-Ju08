/* SPDX-License-Identifier: GPL-3.0-only */
/* A project: the two sounds (with their effects), the 32 patterns, the song and the musical settings, saved in one of 8 numbered slots.
 * This file is the data side, plain C with no flash: packing the working state into the two records a slot holds, unpacking and
 * validating what comes back (a flash that was damaged or written by other firmware cannot break the engine), and a signature of
 * the working state, to tell whether it differs from what was last saved. The flash I/O is in ju_store.c.
 *
 * Included after ju_dsp.c, ju_fxdef.c and ju_pinfo.c (it uses their tables). */
#define JU_NPROJ 8
#define JU_PROJ_MAGIC 0x334A504Au                     /* "JPJ3" as bytes */
/* the settings a project keeps (the others, the volume, the clock source, the metronome and the like, belong to the instrument) */
static const uint8_t ju_proj_g[] = {G_TEMPO, G_KEYMODE, G_SPLIT, G_BAL_U, G_BAL_L, G_ARP_ON, G_ARP_MODE, G_ARP_RANGE, G_ARP_RATE,
                                    G_PORTA_MODE, G_PORTA_TIME, G_CH_U, G_CH_L, G_SONG, G_SONGLOOP, G_SWING};
#define JU_PROJ_NG ((int)sizeof ju_proj_g)
typedef struct { uint8_t valid; char n0[12], n1[12]; uint8_t tempo; } ju_projinfo_t;    /* what the project list shows of a slot */
ju_projinfo_t ju_projinfo[JU_NPROJ];
uint8_t ju_proj_pidx[2];                              /* out of ju_proj_unpack: the library patches the loaded sounds came from */

/* byte streams: the writer stops at its capacity and remembers it, the reader returns 0 past its end and remembers it */
typedef struct { uint8_t *p; uint32_t n, cap; int err; } ju_wr_t;
typedef struct { const uint8_t *p; uint32_t n, pos; int err; } ju_rd_t;
static void wr8(ju_wr_t *w, uint8_t v) { if (w->n < w->cap) w->p[w->n++] = v; else w->err = 1; }
static void wrn(ju_wr_t *w, const void *src, uint32_t n) { const uint8_t *b = src; while (n--) wr8(w, *b++); }
static uint8_t rd8(ju_rd_t *r) { if (r->pos < r->n) return r->p[r->pos++]; r->err = 1; return 0; }
static void rdn(ju_rd_t *r, void *dst, uint32_t n) { uint8_t *b = dst; while (n--) *b++ = rd8(r); }

/* a sound made safe: every selector inside its range, every effect value inside the range of its parameter, the name ended */
void ju_patch_sanitize(ju_patch_t *p)
{
    int i, np;
    ju_fxpatch_t *f = &p->fx;
    p->name[11] = 0;
    for (i = 0; i < P_COUNT; i++) {
        int steps = ju_psteps(i);
        if (steps && p->v[i] >= steps) p->v[i] = (uint8_t)(steps - 1);
    }
    if (f->mfxType >= MFX_COUNT) f->mfxType = MFX_CHO1;
    f->mfxOn = f->mfxOn != 0; f->dlyOn = f->dlyOn != 0; f->revOn = f->revOn != 0;
    np = ju_mfx_nparams(f->mfxType);
    for (i = 0; i < JU_MFX_MAXP; i++) {
        if (i < np) { const ju_fxparam_t *d = ju_mfx_param(f->mfxType, i); f->m[i] = f->m[i] < d->lo ? d->lo : f->m[i] > d->hi ? d->hi : f->m[i]; }
        else f->m[i] = 0;
    }
    for (i = 0; i < DL_COUNT; i++) { const ju_fxparam_t *d = ju_delay_param(i); f->d[i] = f->d[i] < d->lo ? d->lo : f->d[i] > d->hi ? d->hi : f->d[i]; }
    for (i = 0; i < RV_COUNT; i++) { const ju_fxparam_t *d = ju_reverb_param(i); f->r[i] = f->r[i] < d->lo ? d->lo : f->r[i] > d->hi ? d->hi : f->r[i]; }
}
/* a pattern as bytes, only as long as it is: len, gate | lane << 3, then per step a kind byte (bits 0-1 the type, 2 flags, 3 chord, 4 lane value,
 * 5 a note) followed by what it announces. An empty step is one byte, so a pattern of sixteen steps with a melody is about 40. */
static void ju_pat_write(ju_wr_t *w, const ju_pat_t *t)
{
    int i, len = t->len >= 1 && t->len <= JU_SEQ_STEPS ? t->len : 16;
    wr8(w, (uint8_t)len);
    wr8(w, (uint8_t)((t->gate >= 1 && t->gate <= 6 ? t->gate : 3) | ((t->lane <= JU_LANE_N ? t->lane : 0) << 3)));
    for (i = 0; i < len; i++) {
        int ty = t->type[i] <= SQ_TIE ? t->type[i] : SQ_REST, on = ty != SQ_REST;
        int fl = on && t->flags[i], ch = on && (t->chord[i][0] || t->chord[i][1]), lv = t->lv[i] >= 1 && t->lv[i] <= 128;
        wr8(w, (uint8_t)(ty | (fl ? 4 : 0) | (ch ? 8 : 0) | (lv ? 16 : 0) | (on ? 32 : 0)));
        if (on) wr8(w, t->note[i] <= 127 ? t->note[i] : 60);
        if (fl) wr8(w, t->flags[i] & 0x3Fu);
        if (ch) { wr8(w, t->chord[i][0]); wr8(w, t->chord[i][1] & 15u); }
        if (lv) wr8(w, t->lv[i]);
    }
}
static void ju_pat_read(ju_rd_t *r, ju_pat_t *t)                          /* a whole pattern, made safe; r->err if the bytes end early */
{
    int i, len = rd8(r), g = rd8(r), lane = g >> 3 & 15;
    for (i = 0; i < JU_SEQ_STEPS; i++) { t->note[i] = 60; t->type[i] = SQ_REST; t->flags[i] = 0; t->chord[i][0] = t->chord[i][1] = 0; t->lv[i] = 0; }
    t->len = (uint8_t)(len >= 1 && len <= JU_SEQ_STEPS ? len : 16);
    t->gate = (uint8_t)((g & 7) >= 1 && (g & 7) <= 6 ? (g & 7) : 3);
    t->lane = (uint8_t)(lane <= JU_LANE_N ? lane : 0);
    t->pad = 0;
    for (i = 0; i < (len >= 1 && len <= JU_SEQ_STEPS ? len : 0) && !r->err; i++) {
        int k = rd8(r);
        t->type[i] = (uint8_t)((k & 3) <= SQ_TIE ? (k & 3) : SQ_REST);
        if (k & 32) { int n = rd8(r); t->note[i] = (uint8_t)(n <= 127 ? n : 60); }
        if (k & 4) t->flags[i] = (uint8_t)(rd8(r) & 0x3Fu);
        if (k & 8) { t->chord[i][0] = rd8(r); t->chord[i][1] = (uint8_t)(rd8(r) & 15u); }
        if (k & 16) { int v = rd8(r); t->lv[i] = (uint8_t)(v >= 1 && v <= 128 ? v : 0); }
    }
    if (len < 1 || len > JU_SEQ_STEPS) r->err = 1;
}
/* the sequencer's 32 patterns as one run of bytes (the working copy's flash object, and a part of every project) */
int ju_pats_pack(uint8_t *buf, uint32_t cap)                              /* the length, -2 if it does not fit */
{
    ju_wr_t w = {buf, 0, cap, 0};
    int p, k;
    for (p = 0; p < 2; p++) for (k = 0; k < JU_SEQ_PATS; k++) ju_pat_write(&w, &ju_pat[p][k]);
    return w.err ? -2 : (int)w.n;
}
int ju_pats_unpack(const uint8_t *buf, uint32_t len)                      /* 0, or -1 with nothing changed */
{
    static ju_pat_t tmp;
    ju_rd_t r = {buf, len, 0, 0};
    int p, k, pass;
    for (pass = 0; pass < 2; pass++) {                                     /* the first pass only checks, so a bad run changes nothing */
        r.pos = 0;
        for (p = 0; p < 2; p++)
            for (k = 0; k < JU_SEQ_PATS; k++) {
                ju_pat_read(&r, &tmp);
                if (r.err) return -1;
                if (pass) ju_pat[p][k] = tmp;
            }
    }
    ju_seq_dirty[0] = ju_seq_dirty[1] = 1;
    return 0;
}
static void ju_song_sanitize(ju_song_t *s)
{
    int i;
    if (s->n > JU_SONG_ROWS) s->n = JU_SONG_ROWS;
    s->pad[0] = s->pad[1] = s->pad[2] = 0;
    for (i = 0; i < JU_SONG_ROWS; i++) {
        ju_srow_t *r = &s->row[i];
        if (r->pat[0] >= JU_SEQ_PATS) r->pat[0] = JU_SONG_NONE;
        if (r->pat[1] >= JU_SEQ_PATS) r->pat[1] = JU_SONG_NONE;
        if (r->rep < 1 || r->rep > 16) r->rep = 1;
        r->pad = 0;
    }
}

/* a project as bytes: magic, the settings, the selected patterns, the two sounds, the song, the 32 patterns */
#define JU_PROJ_HEAD (4 + 16 + 2 + 2)
void ju_proj_summarize(const uint8_t *buf, uint32_t len, ju_projinfo_t *in)     /* for the project list: the sounds' names and the tempo */
{
    int i;
    in->valid = 0;
    if (len < JU_PROJ_HEAD + 2 * sizeof(ju_patch_t)) return;
    in->valid = 1;
    for (i = 0; i < 12; i++) { in->n0[i] = (char)buf[JU_PROJ_HEAD + i]; in->n1[i] = (char)buf[JU_PROJ_HEAD + sizeof(ju_patch_t) + i]; }
    in->n0[11] = in->n1[11] = 0;
    in->tempo = buf[4];
}
int ju_proj_magic_ok(const uint8_t *buf, uint32_t len) { return len >= 4 && (buf[0] | buf[1] << 8 | buf[2] << 16 | (uint32_t)buf[3] << 24) == JU_PROJ_MAGIC; }

int ju_proj_pack(uint8_t *buf, uint32_t cap)                              /* the length, -2 if the project does not fit */
{
    ju_wr_t w = {buf, 0, cap, 0};
    uint8_t g[16];
    int i, p, k;
    wr8(&w, JU_PROJ_MAGIC & 255u); wr8(&w, (JU_PROJ_MAGIC >> 8) & 255u); wr8(&w, (JU_PROJ_MAGIC >> 16) & 255u); wr8(&w, JU_PROJ_MAGIC >> 24);
    for (i = 0; i < 16; i++) g[i] = i < JU_PROJ_NG ? ju_global[ju_proj_g[i]] : 0;
    wrn(&w, g, 16);
    wr8(&w, ju_seq_cur[0]); wr8(&w, ju_seq_cur[1]); wr8(&w, ju_proj_pidx[0]); wr8(&w, ju_proj_pidx[1]);
    wrn(&w, &ju_patch[0], sizeof(ju_patch_t)); wrn(&w, &ju_patch[1], sizeof(ju_patch_t));
    wr8(&w, ju_song.n);
    for (i = 0; i < ju_song.n && i < JU_SONG_ROWS; i++) { wr8(&w, ju_song.row[i].pat[0]); wr8(&w, ju_song.row[i].pat[1]); wr8(&w, ju_song.row[i].rep); }
    for (p = 0; p < 2; p++) for (k = 0; k < JU_SEQ_PATS; k++) ju_pat_write(&w, &ju_pat[p][k]);
    return w.err ? -2 : (int)w.n;
}

/* the bytes of a project into the working state; 0, or -1 if they are not a whole project (nothing is touched then). The sounds go to the
 * engine through its staging area (it takes them over at its next block); the sequencer should be stopped. */
int ju_proj_unpack(const uint8_t *buf, uint32_t len)
{
    static ju_pat_t tmp;
    static ju_patch_t pt[2];
    ju_rd_t r = {buf, len, 0, 0};
    uint8_t g[16], sc[2], pi[2], rows[JU_SONG_ROWS][3], n = 0;
    int i, p, k, pass;
    if (!ju_proj_magic_ok(buf, len)) return -1;
    for (pass = 0; pass < 2; pass++) {                                     /* the first pass only checks the bytes, the second applies them */
        r.pos = 4;
        rdn(&r, g, 16); sc[0] = rd8(&r); sc[1] = rd8(&r); pi[0] = rd8(&r); pi[1] = rd8(&r);
        rdn(&r, &pt[0], sizeof pt[0]); rdn(&r, &pt[1], sizeof pt[1]);
        n = rd8(&r);
        if (n > JU_SONG_ROWS) return -1;
        for (i = 0; i < n; i++) { rows[i][0] = rd8(&r); rows[i][1] = rd8(&r); rows[i][2] = rd8(&r); }
        for (p = 0; p < 2; p++)
            for (k = 0; k < JU_SEQ_PATS; k++) {
                ju_pat_read(&r, &tmp);
                if (r.err) return -1;
                if (pass) ju_pat[p][k] = tmp;
            }
        if (r.err) return -1;
    }
    ju_song.n = n;
    for (i = 0; i < JU_SONG_ROWS; i++) {
        ju_srow_t *w = &ju_song.row[i];
        w->pat[0] = i < n ? rows[i][0] : JU_SONG_NONE; w->pat[1] = i < n ? rows[i][1] : JU_SONG_NONE; w->rep = i < n ? rows[i][2] : 1;
    }
    ju_song_sanitize(&ju_song);
    for (i = 0; i < JU_PROJ_NG; i++) {
        int id = P_COUNT + ju_proj_g[i], steps = ju_psteps(id);
        uint8_t v = g[i];
        if (steps && v >= steps) v = (uint8_t)(steps - 1);
        if (ju_proj_g[i] == G_SPLIT && v > 127) v = 127;
        if ((ju_proj_g[i] == G_CH_U || ju_proj_g[i] == G_CH_L) && v > 16) v = 0;
        ju_global[ju_proj_g[i]] = v;
    }
    ju_seq_cur[0] = sc[0] < JU_SEQ_PATS ? sc[0] : 0;
    ju_seq_cur[1] = sc[1] < JU_SEQ_PATS ? sc[1] : 0;
    ju_seq_cursor[0] = ju_seq_cursor[1] = 0;
    ju_proj_pidx[0] = pi[0] < 64 ? pi[0] : 0; ju_proj_pidx[1] = pi[1] < 64 ? pi[1] : 0;
    ju_patch_stage[0] = pt[0]; ju_patch_stage[1] = pt[1];
    ju_patch_sanitize(&ju_patch_stage[0]); ju_patch_sanitize(&ju_patch_stage[1]);
    ju_patch_stage_req = 1;
    ju_seq_dirty[0] = ju_seq_dirty[1] = 1; ju_song_dirty = 1;      /* the working copy is saved again */
    return 0;
}

/* a signature of what a project holds (FNV-1a over the patterns, the song, the sounds and the settings) */
static uint32_t ju_fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}
uint32_t ju_proj_sig(void)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < JU_PROJ_NG; i++) h = ju_fnv(h, &ju_global[ju_proj_g[i]], 1);
    for (i = 0; i < 2; i++) {                         /* field by field: the byte of padding before fx is not defined */
        h = ju_fnv(h, ju_patch[i].name, sizeof ju_patch[i].name);
        h = ju_fnv(h, ju_patch[i].v, sizeof ju_patch[i].v);
        h = ju_fnv(h, &ju_patch[i].fx, sizeof ju_patch[i].fx);
    }
    h = ju_fnv(h, ju_pat, sizeof ju_pat);
    h = ju_fnv(h, &ju_song, sizeof ju_song);
    h = ju_fnv(h, ju_seq_cur, 2);
    return h;
}
