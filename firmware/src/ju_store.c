/* SPDX-License-Identifier: GPL-3.0-only */
/* The 64 patches and the panel's state in flash, on storage.c's A/B objects (a torn write leaves the old copy):
 * the patches in three objects of 23 (OBJ_PROJECT0..2), the current patches and the globals in OBJ_SETTINGS. */
#ifndef offsetof
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
#define JU_PPO 23u                                   /* patches per object */
#define JU_BANK_MAGIC 0x4A553031u                    /* "JU01" */
#define JU_SET_MAGIC 0x4A555333u                     /* "JUS3": the global list last, any length (settings saved with fewer globals load) */
#define JU_SEQ_MAGIC 0x4A535131u                     /* "JSQ1": the first firmware's pattern objects (read once, to move them) */
typedef struct { uint32_t magic; ju_patch_t p[JU_PPO]; } ju_bankobj_t;
/* the working copy: the two sounds as they are now (edits included), the open project and the signature it had when saved or loaded */
typedef struct { uint32_t magic; uint8_t cur[2]; uint8_t octave; uint8_t proj; uint8_t seqcur[2]; uint8_t pad2[2]; uint32_t projsig;
                 ju_patch_t patch[2]; uint8_t global[G_COUNT]; } ju_setobj_t;
uint8_t ju_work_valid;                                /* the boot restored the working sounds */
typedef struct { uint8_t note[64], type[64], len, gate, pad[2]; } ju_patold_t;      /* a pattern of that firmware */
typedef struct { uint32_t magic; ju_patold_t p[JU_SEQ_PATS]; } ju_seqobj_t;
_Static_assert(sizeof(ju_bankobj_t) <= ST_PAYLOAD_MAX, "a bank object fits a storage sector");
_Static_assert(sizeof(ju_setobj_t) <= ST_PAYLOAD_MAX, "the settings object fits a storage sector");
_Static_assert(sizeof(ju_seqobj_t) <= ST_PAYLOAD_MAX, "the old pattern object fits a storage sector");
_Static_assert(4 + sizeof(ju_song_t) <= ST_PAYLOAD_MAX, "the song fits a storage sector");
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
    so.proj = (uint8_t)ui.proj_cur; so.pad2[0] = so.pad2[1] = 0; so.projsig = ui.proj_sig;
    so.patch[0] = ju_patch[0]; so.patch[1] = ju_patch[1];
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
            for (k = 0; k < JU_PPO && i * JU_PPO + k < 64u; k++) { ju_bank[i * JU_PPO + k] = ju_bankbuf.p[k]; ju_patch_sanitize(&ju_bank[i * JU_PPO + k]); }
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
            ui.proj_cur = so.proj < JU_NPROJ ? (int8_t)so.proj : -1;
            ui.proj_sig = so.projsig;
            ju_patch[0] = so.patch[0]; ju_patch[1] = so.patch[1];
            ju_patch_sanitize(&ju_patch[0]); ju_patch_sanitize(&ju_patch[1]);
            ju_work_valid = 1;
        }
    }
#endif
}

/* ---- blobs: a run of bytes over several storage objects (the 32 patterns, a project). Every object holds a header (magic, the whole length, the
 * CRC of the whole) and the next slice; the first object is written last. A power cut inside a save leaves slices of two versions, which the
 * CRC refuses: the loader then reports nothing stored, it never returns a mixture. */
#define JU_BLOB_MAGIC 0x314C424Au                    /* "JBL1" */
typedef struct { uint32_t magic, total, crc; } ju_blobhdr_t;
#define JU_BLOB_SLICE (ST_PAYLOAD_MAX - (uint32_t)sizeof(ju_blobhdr_t))
#define JU_BLOB_N 3                                  /* objects per blob */
#define JU_BLOB_MAX (JU_BLOB_N * JU_BLOB_SLICE)
static uint8_t ju_blob[JU_BLOB_MAX] __attribute__((section(".pool"), aligned(4)));
static uint8_t ju_chunk[ST_PAYLOAD_MAX] __attribute__((section(".pool"), aligned(4)));
#if FELUCCA_FLASH
static int ju_blob_save(uint32_t first, const uint32_t *objs, uint32_t len)      /* objs: the JU_BLOB_N objects; 0, -2 too big */
{
    ju_blobhdr_t h;
    int i, n = (int)((len + JU_BLOB_SLICE - 1) / JU_BLOB_SLICE), rc;
    (void)first;
    if (len > JU_BLOB_MAX) return -2;
    if (n < 1) n = 1;
    h.magic = JU_BLOB_MAGIC; h.total = len; h.crc = st_crc32(ju_blob, len);
    for (i = n - 1; i >= 0; i--) {
        uint32_t off = (uint32_t)i * JU_BLOB_SLICE, k = len > off ? len - off : 0, j;
        if (k > JU_BLOB_SLICE) k = JU_BLOB_SLICE;
        memcpy(ju_chunk, &h, sizeof h);
        for (j = 0; j < k; j++) ju_chunk[sizeof h + j] = ju_blob[off + j];
        rc = st_save(objs[i], ju_chunk, (uint32_t)sizeof h + k);
        if (rc) return rc;
    }
    return 0;
}
static int ju_blob_load(const uint32_t *objs, uint32_t *len)                      /* into ju_blob; 0 or -1 */
{
    ju_blobhdr_t h;
    uint32_t i, n;
    int r = st_load(objs[0], ju_chunk, sizeof ju_chunk);
    if (r < (int)sizeof h) return -1;
    memcpy(&h, ju_chunk, sizeof h);
    if (h.magic != JU_BLOB_MAGIC || h.total > JU_BLOB_MAX) return -1;
    n = (h.total + JU_BLOB_SLICE - 1) / JU_BLOB_SLICE;
    if (n < 1) n = 1;
    for (i = 0; i < n; i++) {
        uint32_t off = i * JU_BLOB_SLICE, k = h.total - off > JU_BLOB_SLICE ? JU_BLOB_SLICE : h.total - off, j;
        ju_blobhdr_t g;
        if (i) r = st_load(objs[i], ju_chunk, sizeof ju_chunk);
        if (r < (int)(sizeof g + k)) return -1;
        memcpy(&g, ju_chunk, sizeof g);
        if (g.magic != h.magic || g.total != h.total || g.crc != h.crc) return -1;
        for (j = 0; j < k; j++) ju_blob[off + j] = ju_chunk[sizeof g + j];
    }
    if (st_crc32(ju_blob, h.total) != h.crc) return -1;
    *len = h.total;
    return 0;
}
#endif

/* the sequencer's 32 patterns (the working copy): OBJ_UPRESET0, OBJ_UPRESET0 + 1, OBJ_PROJX0 */
#if FELUCCA_FLASH
static const uint32_t ju_pats_objs[JU_BLOB_N] = {OBJ_UPRESET0, OBJ_UPRESET0 + 1, OBJ_PROJX0};
#endif
static int ju_store_seq(int part)                    /* (both parts are written together) */
{
#if FELUCCA_FLASH
    int n;
    (void)part;
    if (!flash_ok) return -1;
    n = ju_pats_pack(ju_blob, JU_BLOB_MAX);
    if (n < 0) return n;
    return ju_blob_save(0, ju_pats_objs, (uint32_t)n);
#else
    (void)part;
    return -1;
#endif
}
static void ju_store_seq_boot(void)
{
#if FELUCCA_FLASH
    uint32_t n = 0;
    int part;
    if (!flash_ok) return;
    if (ju_blob_load(ju_pats_objs, &n) == 0 && ju_pats_unpack(ju_blob, n) == 0) {
        ju_seq_dirty[0] = ju_seq_dirty[1] = 0;
        return;
    }
    for (part = 0; part < 2; part++) {                                       /* the first firmware's objects: moved, and saved in the new form soon */
        static ju_seqobj_t old;
        int r = st_load(OBJ_UPRESET0 + (uint32_t)part, &old, sizeof old), k, i;
        if (r != (int)sizeof old || old.magic != JU_SEQ_MAGIC) continue;
        for (k = 0; k < JU_SEQ_PATS; k++) {
            ju_pat_t *t = &ju_pat[part][k];
            const ju_patold_t *o = &old.p[k];
            for (i = 0; i < JU_SEQ_STEPS; i++) {
                t->note[i] = o->note[i] <= 127 ? o->note[i] : 60;
                t->type[i] = o->type[i] <= SQ_TIE ? o->type[i] : SQ_REST;
                t->flags[i] = 0; t->chord[i][0] = t->chord[i][1] = 0; t->lv[i] = 0;
            }
            t->len = o->len >= 1 && o->len <= JU_SEQ_STEPS ? o->len : 16;
            t->gate = o->gate >= 1 && o->gate <= 6 ? o->gate : 3;
            t->lane = 0; t->pad = 0;
        }
        ju_seq_dirty[part] = 1;
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

/* ---- projects: 8 slots of JU_BLOB_N objects each (OBJ_JUPROJ0 + 3 * slot ...). Saved and loaded only when the panel asks. */
#if FELUCCA_FLASH
static void ju_proj_objs(int slot, uint32_t *o) { int k; for (k = 0; k < JU_BLOB_N; k++) o[k] = OBJ_JUPROJ0 + 3u * (uint32_t)slot + (uint32_t)k; }
#endif
static int ju_proj_flash_save(int slot)               /* 0, -2: it does not fit, other: flash failed */
{
#if FELUCCA_FLASH
    uint32_t o[JU_BLOB_N];
    int n, r;
    if (!flash_ok || slot < 0 || slot >= JU_NPROJ) return -1;
    n = ju_proj_pack(ju_blob, JU_BLOB_MAX);
    if (n < 0) return n;
    ju_proj_objs(slot, o);
    r = ju_blob_save(0, o, (uint32_t)n);
    if (r == 0) ju_proj_summarize(ju_blob, (uint32_t)n, &ju_projinfo[slot]);
    return r;
#else
    (void)slot;
    return -1;
#endif
}
static int ju_proj_flash_load(int slot)               /* 0 ok, -1 empty or damaged (nothing changed) */
{
#if FELUCCA_FLASH
    uint32_t o[JU_BLOB_N], n = 0;
    if (!flash_ok || slot < 0 || slot >= JU_NPROJ) return -1;
    ju_proj_objs(slot, o);
    if (ju_blob_load(o, &n)) return -1;
    return ju_proj_unpack(ju_blob, n);
#else
    (void)slot;
    return -1;
#endif
}
static int ju_proj_flash_clear(int slot)              /* a slot made empty: its first object gets an invalid header */
{
#if FELUCCA_FLASH
    uint32_t o[JU_BLOB_N];
    int i;
    if (!flash_ok || slot < 0 || slot >= JU_NPROJ) return -1;
    for (i = 0; i < (int)sizeof(ju_blobhdr_t); i++) ju_chunk[i] = 0;
    ju_proj_objs(slot, o);
    ju_projinfo[slot].valid = 0;
    return st_save(o[0], ju_chunk, (uint32_t)sizeof(ju_blobhdr_t));
#else
    (void)slot;
    return -1;
#endif
}
static void ju_proj_boot(void)                        /* fills the list the project page shows */
{
#if FELUCCA_FLASH
    int s;
    if (!flash_ok) return;
    for (s = 0; s < JU_NPROJ; s++) {
        uint32_t o[JU_BLOB_N];
        int n;
        ju_projinfo[s].valid = 0;
        ju_proj_objs(s, o);
        n = st_load(o[0], ju_chunk, sizeof ju_chunk);
        if (n > (int)sizeof(ju_blobhdr_t) && ju_proj_magic_ok(ju_chunk + sizeof(ju_blobhdr_t), (uint32_t)n - (uint32_t)sizeof(ju_blobhdr_t)) &&
            ((ju_blobhdr_t *)ju_chunk)->magic == JU_BLOB_MAGIC)
            ju_proj_summarize(ju_chunk + sizeof(ju_blobhdr_t), (uint32_t)n - (uint32_t)sizeof(ju_blobhdr_t), &ju_projinfo[s]);
    }
#endif
}
