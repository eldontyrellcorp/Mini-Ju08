/* Random patches, notes, bender, mod wheel and globals through the fixed-point engine, built with -fsanitize=undefined
 * (signed overflow, bad shifts): fails on any report, on a stuck voice after every key is up, or on an output that is not
 * quiet again after the release. No float reference needed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ju_params.h"
void ju_init(void); void ju_prepare_block(void); void ju_render(int32_t *, int32_t *); void ju_finish(const int32_t *, const int32_t *, int32_t *);
void ju_note_on(uint8_t, uint8_t); void ju_note_off(uint8_t, uint8_t); void ju_bend(uint8_t, int); void ju_modwheel_set(uint8_t, uint8_t);
void ju_sustain_set(uint8_t, uint8_t); void ju_all_off(uint8_t); int ju_voice_active(int);
const char *ju_mfx_name(int); int ju_mfx_nparams(int); const ju_fxparam_t *ju_mfx_param(int, int); const ju_fxparam_t *ju_delay_param(int); const ju_fxparam_t *ju_reverb_param(int);
void ju_fx_load_mfx_defaults(ju_fxpatch_t *, int);
extern ju_patch_t ju_patch[2]; extern uint8_t ju_global[G_COUNT];

static uint32_t rs = 12345;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static const uint8_t kMax[P_COUNT] = {255,255,3,255,255,2,255,2,255,3,3,1,3,1,255,3,255,255,255,255,1,255,1,255,255,255,3,255,255,255,255,1,1,255,255,255,255,1,3};

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 400, bad = 0, it;
    for (it = 0; it < iters; it++) {
        int b, k, quiet = 0, hot = 0, short_rel;
        ju_init();
        for (k = 0; k < P_COUNT; k++) {
            ju_patch[0].v[k] = (uint8_t)(rnd() % (kMax[k] + 1u));
            ju_patch[1].v[k] = (uint8_t)(rnd() % (kMax[k] + 1u));
            if (rnd() % 3 == 0) ju_patch[0].v[k] = (uint8_t)(rnd() & 1 ? kMax[k] : 0);   /* the extremes, often */
        }
        {   /* effects: any type, any value in its range, often the extremes; sections on or off */
            ju_fxpatch_t *fx = &ju_patch[0].fx;
            int t = (int)(rnd() % MFX_COUNT), n = ju_mfx_nparams(t), i;
            ju_fx_load_mfx_defaults(fx, t);
            for (i = 0; i < n; i++) {
                const ju_fxparam_t *d = ju_mfx_param(t, i);
                uint32_t r = rnd() % 4;
                fx->m[i] = (int16_t)(r == 0 ? d->lo : r == 1 ? d->hi : d->lo + (int)(rnd() % (uint32_t)(d->hi - d->lo + 1)));
            }
            for (i = 0; i < DL_COUNT; i++) { const ju_fxparam_t *d = ju_delay_param(i); fx->d[i] = (int16_t)(d->lo + (int)(rnd() % (uint32_t)(d->hi - d->lo + 1))); }
            for (i = 0; i < RV_COUNT; i++) { const ju_fxparam_t *d = ju_reverb_param(i); fx->r[i] = (int16_t)(d->lo + (int)(rnd() % (uint32_t)(d->hi - d->lo + 1))); }
            fx->mfxOn = (uint8_t)(rnd() % 4 != 0); fx->dlyOn = (uint8_t)(rnd() % 3 != 0); fx->revOn = (uint8_t)(rnd() % 3 != 0);
            ju_global[G_TEMPO] = (uint8_t)rnd();
        }
        ju_global[G_KEYMODE] = (uint8_t)(rnd() % 4);
        ju_global[G_VINTAGE] = (uint8_t)rnd();
        ju_global[G_PORTA_MODE] = (uint8_t)(rnd() % 3);
        ju_global[G_PORTA_TIME] = (uint8_t)rnd();
        ju_global[G_BEND_VCO] = (uint8_t)rnd(); ju_global[G_BEND_VCF] = (uint8_t)rnd(); ju_global[G_BEND_VCFSW] = (uint8_t)(rnd() & 1);
        ju_global[G_MOD_VCO] = (uint8_t)rnd(); ju_global[G_MOD_VCF] = (uint8_t)rnd(); ju_global[G_MOD_VCFSW] = (uint8_t)(rnd() & 1);
        ju_global[G_MOD_RISE] = (uint8_t)rnd();
        ju_global[G_TUNE] = (uint8_t)rnd(); ju_global[G_SPLIT] = (uint8_t)(30 + rnd() % 60);
        ju_global[G_CH_U] = 1; ju_global[G_CH_L] = (uint8_t)(rnd() % 3);
        /* a release over about 0.5 s, or key follow stretching it up to 4x, may still ring: only judge the short ones */
        short_rel = !ju_patch[0].fx.mfxOn && !ju_patch[0].fx.dlyOn && !ju_patch[0].fx.revOn && ju_patch[0].v[P_E2_R] < 150 && ju_patch[1].v[P_E2_R] < 150 && !ju_patch[0].v[P_E2_KEY] && !ju_patch[1].v[P_E2_KEY];
        for (b = 0; b < 44100 * 6 / 32; b++) {
            int32_t u[32], l[32], out[64], n;
            memset(u, 0, sizeof u); memset(l, 0, sizeof l);
            if (b % 40 == 0 && b < 3500) {
                int note = 12 + (int)(rnd() % 100), ch = (int)(rnd() % 2);
                if (rnd() & 1) ju_note_on((uint8_t)ch, (uint8_t)note); else ju_note_off((uint8_t)ch, (uint8_t)note);
            }
            if (b % 97 == 0) { ju_bend((uint8_t)(rnd() % 2), (int)(rnd() % 16384) - 8192); ju_modwheel_set((uint8_t)(rnd() % 2), (uint8_t)(rnd() % 128)); }
            if (b == 3600) { ju_all_off(0xFF); ju_sustain_set(0xFF, 0); }
            ju_prepare_block(); ju_render(u, l); ju_finish(u, l, out);
            for (n = 0; n < 32; n++) {
                int32_t a = out[2 * n] < 0 ? -out[2 * n] : out[2 * n];
                if (a > 32768) { hot++; }
                if (short_rel && b > 44100 * 5 / 32 && a > 40) quiet++;   /* 2.4 s after the last gate: Q15 40 = -58 dBFS */
            }
        }
        for (k = 0; k < 8; k++) if (short_rel && ju_voice_active(k)) quiet += 1000;      /* a voice that never ends */
        if (hot || quiet) { printf("iteration %d: hot=%d not-quiet=%d (assign %d, keymode %d)\n", it, hot, quiet, ju_patch[0].v[P_ASSIGN], ju_global[G_KEYMODE]); bad++; }
    }
    printf("%d iterations, %d with problems\n", iters, bad);
    return bad ? 1 : 0;
}
