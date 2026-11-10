/* SPDX-License-Identifier: GPL-3.0-only */
/* A project: the two sounds (with their effects), the 32 patterns, the song and the musical settings, saved in one of 8 numbered slots.
 * This file is the data side, plain C with no flash: packing the working state into the two records a slot holds, unpacking and
 * validating what comes back (a flash that was damaged or written by other firmware cannot break the engine), and a signature of
 * the working state, to tell whether it differs from what was last saved. The flash I/O is in ju_store.c.
 *
 * Included after ju_dsp.c, ju_fxdef.c and ju_pinfo.c (it uses their tables). */
#define JU_NPROJ 8
#define JU_PROJ_MAGIC1 0x4A504A31u                    /* "JPJ1" */
#define JU_PROJ_MAGIC2 0x4A504A32u                    /* "JPJ2" */
/* the settings a project keeps (the others, the volume, the clock source, the metronome and the like, belong to the instrument) */
static const uint8_t ju_proj_g[] = {G_TEMPO, G_KEYMODE, G_SPLIT, G_BAL_U, G_BAL_L, G_ARP_ON, G_ARP_MODE, G_ARP_RANGE, G_ARP_RATE,
                                    G_PORTA_MODE, G_PORTA_TIME, G_CH_U, G_CH_L, G_SONG, G_SONGLOOP};
#define JU_PROJ_NG ((int)sizeof ju_proj_g)
typedef struct {                                      /* record 1: everything but the lower part's patterns */
    uint32_t magic;
    uint8_t g[16];
    uint8_t seqcur[2], pidx[2];                       /* the selected patterns, the library patches the sounds came from */
    ju_patch_t patch[2];
    ju_song_t song;
    ju_pat_t pat[JU_SEQ_PATS];                        /* the upper part's */
} ju_proj1_t;
typedef struct { uint32_t magic; ju_pat_t pat[JU_SEQ_PATS]; } ju_proj2_t;                /* record 2: the lower part's patterns */
typedef struct { uint8_t valid; char n0[12], n1[12]; uint8_t tempo; } ju_projinfo_t;    /* what the project list shows of a slot */
ju_projinfo_t ju_projinfo[JU_NPROJ];
uint8_t ju_proj_pidx[2];                              /* out of ju_proj_unpack: the library patches the loaded sounds came from */

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
static void ju_pat_sanitize(ju_pat_t *t)
{
    int i;
    if (t->len < 1 || t->len > JU_SEQ_STEPS) t->len = 16;
    if (t->gate < 1 || t->gate > 6) t->gate = 3;
    t->pad[0] = t->pad[1] = 0;
    for (i = 0; i < JU_SEQ_STEPS; i++) {
        if (t->note[i] > 127) t->note[i] = 60;
        if (t->type[i] > SQ_TIE) t->type[i] = SQ_REST;
    }
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

void ju_proj_summarize(const ju_proj1_t *a, ju_projinfo_t *in)
{
    int i;
    in->valid = 1;
    for (i = 0; i < 12; i++) { in->n0[i] = a->patch[0].name[i]; in->n1[i] = a->patch[1].name[i]; }
    in->n0[11] = in->n1[11] = 0;
    in->tempo = a->g[0];
}

/* the working state into the two records of a slot */
void ju_proj_pack(ju_proj1_t *a, ju_proj2_t *b)
{
    int i;
    a->magic = JU_PROJ_MAGIC1; b->magic = JU_PROJ_MAGIC2;
    for (i = 0; i < 16; i++) a->g[i] = i < JU_PROJ_NG ? ju_global[ju_proj_g[i]] : 0;
    a->seqcur[0] = ju_seq_cur[0]; a->seqcur[1] = ju_seq_cur[1];
    a->pidx[0] = ju_proj_pidx[0]; a->pidx[1] = ju_proj_pidx[1];
    a->patch[0] = ju_patch[0]; a->patch[1] = ju_patch[1];
    a->song = ju_song;
    for (i = 0; i < JU_SEQ_PATS; i++) { a->pat[i] = ju_pat[0][i]; b->pat[i] = ju_pat[1][i]; }
}

/* the records of a slot into the working state; 0 if they are a project, -1 if not (nothing is touched then). The sounds go to the
 * engine through its staging area (it takes them over at its next block); the sequencer should be stopped. */
int ju_proj_unpack(const ju_proj1_t *a, const ju_proj2_t *b)
{
    int i;
    if (a->magic != JU_PROJ_MAGIC1 || b->magic != JU_PROJ_MAGIC2) return -1;
    for (i = 0; i < JU_SEQ_PATS; i++) {
        ju_pat[0][i] = a->pat[i]; ju_pat[1][i] = b->pat[i];
        ju_pat_sanitize(&ju_pat[0][i]); ju_pat_sanitize(&ju_pat[1][i]);
    }
    ju_song = a->song;
    ju_song_sanitize(&ju_song);
    for (i = 0; i < JU_PROJ_NG; i++) {
        int id = P_COUNT + ju_proj_g[i], steps = ju_psteps(id);
        uint8_t v = a->g[i];
        if (steps && v >= steps) v = (uint8_t)(steps - 1);
        if (ju_proj_g[i] == G_SPLIT && v > 127) v = 127;
        if ((ju_proj_g[i] == G_CH_U || ju_proj_g[i] == G_CH_L) && v > 16) v = 0;
        ju_global[ju_proj_g[i]] = v;
    }
    ju_seq_cur[0] = a->seqcur[0] < JU_SEQ_PATS ? a->seqcur[0] : 0;
    ju_seq_cur[1] = a->seqcur[1] < JU_SEQ_PATS ? a->seqcur[1] : 0;
    ju_seq_cursor[0] = ju_seq_cursor[1] = 0;
    ju_proj_pidx[0] = a->pidx[0] < 64 ? a->pidx[0] : 0; ju_proj_pidx[1] = a->pidx[1] < 64 ? a->pidx[1] : 0;
    ju_patch_stage[0] = a->patch[0]; ju_patch_stage[1] = a->patch[1];
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
