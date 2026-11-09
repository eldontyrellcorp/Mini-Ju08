/* SPDX-License-Identifier: GPL-3.0-only */
/* The 64 patches and the panel's state in flash, on storage.c's A/B objects (a torn write leaves the old copy):
 * the patches in three objects of 23 (OBJ_PROJECT0..2), the current patches and the globals in OBJ_SETTINGS. */
#define JU_PPO 23u                                   /* patches per object */
#define JU_BANK_MAGIC 0x4A553031u                    /* "JU01" */
#define JU_SET_MAGIC 0x4A555330u                     /* "JUS0" */
typedef struct { uint32_t magic; ju_patch_t p[JU_PPO]; } ju_bankobj_t;
typedef struct { uint32_t magic; uint8_t cur[2]; uint8_t octave; uint8_t pad; uint8_t global[G_COUNT]; } ju_setobj_t;
_Static_assert(sizeof(ju_bankobj_t) <= ST_PAYLOAD_MAX, "a bank object fits a storage sector");
_Static_assert(sizeof(ju_setobj_t) <= ST_PAYLOAD_MAX, "the settings object fits a storage sector");
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
        if (n == (int)sizeof so && so.magic == JU_SET_MAGIC) {
            memcpy(ju_global, so.global, G_COUNT);
            if (so.cur[0] < 64) ui.patch_idx[0] = so.cur[0];
            if (so.cur[1] < 64) ui.patch_idx[1] = so.cur[1];
            if (so.octave <= 6) ju_octave = (int8_t)so.octave - 3;
        }
    }
#endif
}
