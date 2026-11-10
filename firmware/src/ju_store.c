/* SPDX-License-Identifier: GPL-3.0-only */
/* The 64 patches and the panel's state in flash, on storage.c's A/B objects (a torn write leaves the old copy):
 * the patches in three objects of 23 (OBJ_PROJECT0..2), the current patches and the globals in OBJ_SETTINGS. */
#ifndef offsetof
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
#define JU_PPO 23u                                   /* patches per object */
#define JU_BANK_MAGIC 0x4A553031u                    /* "JU01" */
#define JU_SET_MAGIC 0x4A555332u                     /* "JUS2": the global list last, any length (settings saved with fewer globals load) */
#define JU_SEQ_MAGIC 0x4A535131u                     /* "JSQ1" */
typedef struct { uint32_t magic; ju_patch_t p[JU_PPO]; } ju_bankobj_t;
typedef struct { uint32_t magic; uint8_t cur[2]; uint8_t octave; uint8_t pad; uint8_t seqcur[2]; uint8_t pad2[2]; uint8_t global[G_COUNT]; } ju_setobj_t;
typedef struct { uint32_t magic; ju_pat_t p[JU_SEQ_PATS]; } ju_seqobj_t;         /* one part's 16 patterns */
_Static_assert(sizeof(ju_bankobj_t) <= ST_PAYLOAD_MAX, "a bank object fits a storage sector");
_Static_assert(sizeof(ju_setobj_t) <= ST_PAYLOAD_MAX, "the settings object fits a storage sector");
_Static_assert(sizeof(ju_seqobj_t) <= ST_PAYLOAD_MAX, "a part's patterns fit a storage sector");
_Static_assert(4 + sizeof(ju_song_t) <= ST_PAYLOAD_MAX, "the song fits a storage sector");
static ju_seqobj_t ju_seqbuf __attribute__((section(".pool")));
static ju_bankobj_t ju_bankbuf __attribute__((section(".pool")));

static int ju_store_patch(int idx)                   /* the object holding patch idx, from the RAM bank */
{
#if FELUCCA_FLASH
    uint32_t obj = (uint32_t)idx / JU_PPO, i;
    if (!flash_ok || idx < 0 || idx >= 64) return -1;
    ju_bankbuf.magic = JU_BANK_MAGIC;
    for (i = 0; i < JU_PPO; i++) {
        uint32_t k = obj * JU_PPO + i;
        if (k < 64u) ju_bankbuf.p[i] = ju_bank[k];
    }
    return st_save(OBJ_PROJECT0 + obj, &ju_bankbuf, sizeof ju_bankbuf);
#else
    (void)idx;
    return -1;
#endif
}

static void ju_store_current(void)
{
#if FELUCCA_FLASH
    static ju_setobj_t so __attribute__((section(".pool")));
    if (!flash_ok) return;
    so.magic = JU_SET_MAGIC;
    so.cur[0] = (uint8_t)ui.patch_idx[0];
    so.cur[1] = (uint8_t)ui.patch_idx[1];
    so.octave = (uint8_t)(ju_octave + 3);
    memcpy(so.global, ju_global, G_COUNT);
    so.seqcur[0] = ju_seq_cur[0]; so.seqcur[1] = ju_seq_cur[1];
    (void)st_save(OBJ_SETTINGS, &so, sizeof so);
#endif
}

/* boot: the factory patches, then whatever flash holds on top of them */
static void ju_store_boot(void)
{
    uint32_t i;
    memcpy(ju_bank, JU_PRESETS, sizeof JU_PRESETS);
#if FELUCCA_FLASH
    if (!flash_ok) return;
    for (i = 0; i < 3u; i++) {
        int n = st_load(OBJ_PROJECT0 + i, &ju_bankbuf, sizeof ju_bankbuf);
        if (n == (int)sizeof ju_bankbuf && ju_bankbuf.magic == JU_BANK_MAGIC) {
            uint32_t k;
            for (k = 0; k < JU_PPO && i * JU_PPO + k < 64u; k++) ju_bank[i * JU_PPO + k] = ju_bankbuf.p[k];
        }
    }
    {
        ju_setobj_t so;
        int n = st_load(OBJ_SETTINGS, &so, sizeof so);
        int head = (int)offsetof(ju_setobj_t, global);
        if (n > head && so.magic == JU_SET_MAGIC) {
            memcpy(ju_global, so.global, (unsigned)(n - head > G_COUNT ? G_COUNT : n - head));
            if (so.cur[0] < 64) ui.patch_idx[0] = so.cur[0];
            if (so.cur[1] < 64) ui.patch_idx[1] = so.cur[1];
            if (so.octave <= 6) ju_octave = (int8_t)so.octave - 3;
            if (so.seqcur[0] < JU_SEQ_PATS) ju_seq_cur[0] = so.seqcur[0];
            if (so.seqcur[1] < JU_SEQ_PATS) ju_seq_cur[1] = so.seqcur[1];
        }
    }
#endif
}

/* a part's 16 patterns (OBJ_UPRESET0 + part: 0xDC000 and 0xDE000, inside Felucca's store area) */
static int ju_store_seq(int part)
{
#if FELUCCA_FLASH
    if (!flash_ok || part < 0 || part > 1) return -1;
    ju_seqbuf.magic = JU_SEQ_MAGIC;
    memcpy(ju_seqbuf.p, ju_pat[part], sizeof ju_seqbuf.p);
    return st_save(OBJ_UPRESET0 + (uint32_t)part, &ju_seqbuf, sizeof ju_seqbuf);
#else
    (void)part;
    return -1;
#endif
}
static void ju_store_seq_boot(void)
{
#if FELUCCA_FLASH
    int part;
    if (!flash_ok) return;
    for (part = 0; part < 2; part++) {
        int n = st_load(OBJ_UPRESET0 + (uint32_t)part, &ju_seqbuf, sizeof ju_seqbuf), k, i;
        if (n != (int)sizeof ju_seqbuf || ju_seqbuf.magic != JU_SEQ_MAGIC) continue;
        for (k = 0; k < JU_SEQ_PATS; k++) {                                   /* sanitised: a damaged pattern cannot break the engine */
            ju_pat_t *t = &ju_pat[part][k];
            *t = ju_seqbuf.p[k];
            if (t->len < 1 || t->len > JU_SEQ_STEPS) t->len = 16;
            if (t->gate < 1 || t->gate > 6) t->gate = 3;
            for (i = 0; i < JU_SEQ_STEPS; i++) {
                if (t->note[i] > 127) t->note[i] = 60;
                if (t->type[i] > SQ_TIE) t->type[i] = SQ_REST;
            }
        }
    }
#endif
}

/* the song (OBJ_PROJECT0 + 3, the fourth of the sectors the patch bank leaves free) */
#define JU_SONG_MAGIC 0x4A534F31u                    /* "JSO1" */
typedef struct { uint32_t magic; ju_song_t s; } ju_songobj_t;
static ju_songobj_t ju_songbuf __attribute__((section(".pool")));
static int ju_store_song(void)
{
#if FELUCCA_FLASH
    if (!flash_ok) return -1;
    ju_songbuf.magic = JU_SONG_MAGIC;
    ju_songbuf.s = ju_song;
    return st_save(OBJ_PROJECT0 + 3u, &ju_songbuf, sizeof ju_songbuf);
#else
    return -1;
#endif
}
static void ju_store_song_boot(void)
{
#if FELUCCA_FLASH
    int i;
    if (!flash_ok) return;
    if (st_load(OBJ_PROJECT0 + 3u, &ju_songbuf, sizeof ju_songbuf) != (int)sizeof ju_songbuf || ju_songbuf.magic != JU_SONG_MAGIC) return;
    ju_song = ju_songbuf.s;                                         /* sanitised: a damaged song cannot break the engine */
    if (ju_song.n > JU_SONG_ROWS) ju_song.n = JU_SONG_ROWS;
    for (i = 0; i < JU_SONG_ROWS; i++) {
        ju_srow_t *r = &ju_song.row[i];
        if (r->pat[0] >= JU_SEQ_PATS) r->pat[0] = JU_SONG_NONE;
        if (r->pat[1] >= JU_SEQ_PATS) r->pat[1] = JU_SONG_NONE;
        if (r->rep < 1 || r->rep > 16) r->rep = 1;
    }
#endif
}
