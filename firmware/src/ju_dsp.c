/* SPDX-License-Identifier: GPL-3.0-only */
/* Jupiter-8 voice engine in fixed point: a port of jupiter8_esp32/src/dsp/engine.cpp (floats) for the FM-1.
 *
 * Plain C, integers only (the pi32v2 toolchain has no FPU: every float operation is a library call, but a
 * 32 x 32 -> 64 multiply is two instructions). Shared by the firmware and the host tests (host/).
 *
 * Formats:  audio and filter state Q24 (1.0 = 1 << 24, +-128 of headroom); envelopes Q30; phases and VCO
 * increments full turn = 2^32; octaves (pitch, cutoff, modulation depth) Q16; slow controls Q16.
 * Control rate: every JU_BLOCK = 32 samples at 44.1 kHz, as the original; the per-block values of a voice are
 * ramped sample by sample.
 *
 * What differs from the float engine (host/ju_test.cpp measures it against it):
 *   - the triangle has no BLAMP (its corners alias a little at the top of the keyboard)
 *   - one 4-pole ladder step per sample (J8_FILTER_OS = 1 there as well)
 *   - the pulse is built as two sawtooths, so it has no DC to take out
 *   - the arpeggiator, the effects and stereo layers are not ported yet (later milestones) */
#include "ju_params.h"
#include "ju_tables.h"

#define JU_NV 8
#define JU_BLOCK 32
#define JU_ONE24 (1 << 24)
#define JU_ONE30 (1 << 30)
#define JU_DRV(d) ((((d) >> 3) * 9) >> 1)                  /* x 0.5625: the OTA drive before the tanh */
#define JU_MAXINC 1932735283u                       /* 0.45 * 2^32 */

typedef struct {
    int8_t layer, note;
    uint8_t gate, active, fresh, st1, st2;
    uint32_t age;
    uint32_t ph1, ph2;
    int32_t nxt2;                                   /* VCO-2's output, one sample late (hard sync corrections) */
    int32_t env1, env2;                             /* Q30 */
    int32_t pitch;                                  /* note * 65536 (glides) */
    int32_t lfoGain;                                /* Q16 */
    int32_t z[4], hpz;
    int32_t gPrev, gdPrev, invPrev, kPrev, compPrev, hpPrev, vcaPrev;
    uint32_t inc1Prev, inc2Prev, pwPrev;
    int32_t o2Last;
    int32_t drift1, drift2, driftCut;               /* cents, Q16 */
    int32_t tolP1, tolP2, tolCut;                   /* cents, Q16 */
    int32_t invTolEnv;                              /* Q16 */
    uint32_t rng;
} ju_voice_t;

typedef struct {
    int32_t lfo;                                    /* Q24 */
    uint32_t lfoPhase, modLfoPhase;
    int32_t lfoHold, modDepth;
    int32_t bendVco1, bendVco2, bendVcf, modVcoOct, modVcfOct;
    int32_t cutS, hpfS, mixS, lvlS, xmS, pwS;       /* Q16 (cutS, hpfS in parameter units << 16) */
    int32_t resS;                                   /* Q24 */
    uint8_t first;
} ju_layer_t;

typedef struct { int8_t note; uint8_t down; } ju_held_t;

ju_patch_t ju_patch[2];
#ifndef JU_POOL
#define JU_POOL                                     /* the firmware puts the big buffers in its pool section */
#endif
ju_patch_t ju_bank[64] JU_POOL;                             /* the 64 patches the panel loads from (the firmware: RAM copy of the flash banks) */
volatile uint8_t ju_panic_req;                      /* all notes off at the next block (the PLAY button) */
ju_patch_t ju_patch_stage[2];                        /* the two sounds of a project the panel loaded, taken over at the next block */
volatile uint8_t ju_patch_stage_req;
volatile uint8_t ju_load_req[2];                    /* a patch to load into a part at the next block: index + 1 (the UI and MIDI ask, the audio applies) */
uint8_t ju_global[G_COUNT];
static ju_voice_t ju_v[JU_NV];
static ju_layer_t ju_ctl[2];
static uint32_t ju_lfo_rng = 0x9E3779B9u;
static ju_held_t ju_held[2][16];
static int ju_nheld[2];
static uint8_t ju_sustain[2];
static int ju_bend14[2];
static uint8_t ju_modwheel[2];
static uint8_t ju_last_keymode = 255, ju_last_hold[2], ju_last_assign[2] = {255, 255};
static uint32_t ju_age_counter;
static int ju_next_voice[2];
static int32_t ju_fx_tail;                          /* blocks the effects keep running after the last one is switched off */
static uint8_t ju_fx_run;                           /* this block goes through the effects */
uint8_t ju_stereo_layers;                           /* 1: in Dual / Split, Lower left and Upper right */
static int8_t ju_arp_keys[16];                      /* the arpeggiator's held keys, sorted low to high */
static uint8_t ju_arp_down[16];
static int ju_arp_n, ju_arp_pos, ju_arp_dir = 1;
static int8_t ju_arp_sounding = -1;
static uint8_t ju_arp_gate, ju_last_arp_on;
static uint32_t ju_arp_phase;

/* ------------------------------------------------------------------ maths */
static inline int32_t ju_mulq(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }
static inline int32_t ju_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline uint32_t ju_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static inline int32_t ju_rand_bip24(uint32_t *s) { return (int32_t)ju_rand(s) >> 7; }       /* +-1.0, Q24 */

/* tanh(x), Q24 in and out, table with linear interpolation */
static inline int32_t ju_sat(int32_t x)
{
    uint32_t p, i;
    int32_t a, b;
    if (x >= (4 << 24)) return JU_ONE24;
    if (x <= -(4 << 24)) return -JU_ONE24;
    p = (uint32_t)(x + (4 << 24));
    i = p >> 18;
    a = JU_SAT[i]; b = JU_SAT[i + 1];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)(p & 0x3FFFFu)) >> 18);
}

/* 2^x for x in octaves Q16 -> Q16 (saturates) */
static inline uint32_t ju_exp2_q16(int32_t x)
{
    int32_t n = x >> 16;
    uint32_t fr = (uint32_t)x & 0xFFFFu, i = fr >> 8, f = fr & 255u;
    uint32_t a = JU_EXP2P[i], b = JU_EXP2P[i + 1], v = a + (((b - a) * f) >> 8);
    if (n >= 15) return 0x7FFFFFFFu;
    if (n >= 0) return v << n;
    return n <= -31 ? 0u : v >> -n;
}

/* 2^-m for m = octaves Q24 (>= 0) -> Q30: the per-block coefficient of an exponential segment */
static inline int32_t ju_exp2n_q30(uint32_t m)
{
    uint32_t n = m >> 24, fr = m & 0xFFFFFFu, i = fr >> 16;
    int32_t a = (int32_t)JU_EXP2N[i], b = (int32_t)JU_EXP2N[i + 1];
    int32_t v = a + (int32_t)(((int64_t)(b - a) * (int32_t)(fr & 0xFFFFu)) >> 16);
    return n >= 31 ? 0 : v >> n;
}

/* phase increment of 440 Hz * 2^o (o octaves Q16) */
static inline uint32_t ju_inc(int32_t o)
{
    int32_t n = o >> 16;
    uint32_t fr = (uint32_t)o & 0xFFFFu, i = fr >> 8, f = fr & 255u;
    uint32_t a = JU_INC[i], b = JU_INC[i + 1], v = a + (((b - a) * f) >> 8);
    if (n > 5) return JU_MAXINC;
    if (n >= 0) { uint64_t w = (uint64_t)v << n; return w > JU_MAXINC ? JU_MAXINC : (uint32_t)w; }
    v = n <= -31 ? 0u : v >> -n;
    return v < 1u ? 1u : v;
}

/* g = T / (1 + T), T = tan(pi fc / fs), for log2(fc) in Q16 (a one-pole's coefficient, Q30) */
static inline int32_t ju_g_of(int32_t lg)
{
    int32_t x = lg - (JU_G_L0 << 16);
    uint32_t i, f, a, b;
    if (x < 0) x = 0;
    if (x > (JU_G_N - 2) * 2048) x = (JU_G_N - 2) * 2048;
    i = (uint32_t)x >> 11; f = (uint32_t)x & 2047u;
    a = JU_G[i]; b = JU_G[i + 1];
    return (int32_t)(a + (uint32_t)(((int64_t)((int32_t)b - (int32_t)a) * (int32_t)f) >> 11));
}

static inline int32_t ju_sine_turn(uint32_t ph)       /* cos(2 pi ph / 2^32), Q24: the original's sinTurn(0) is +1 */
{
    uint32_t i;
    ph += 0x40000000u;
    i = ph >> 24;
    int32_t a = JU_SINE[i], b = JU_SINE[i + 1];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)((ph >> 8) & 0xFFFFu)) >> 16);
}

#define FX_POOL JU_POOL
#include "ju_fx.c"                              /* the effects share the engine's maths */

/* ------------------------------------------------------------- oscillators */
/* polyBLEP residual of a unit step around a wrap of the phase (Q24, -1..0 after the wrap, 0..1 before it) */
static inline int32_t ju_blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d) return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                         /* t, Q15 (above 1.0 only for a VCO below ~1 Hz) */
        if (x > 32767) x = 32767;
        return (int32_t)((x + x - ((x * x) >> 15) - 32768)) * 512;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);
        if (x < -32767) x = -32767;
        return (int32_t)(((x * x) >> 15) + x + x + 32768) * 512;
    }
    return 0;
}

static inline int32_t ju_saw(uint32_t ph, uint32_t inc)  { return (int32_t)(ph >> 7) - JU_ONE24 - ju_blep(ph, inc); }
static inline int32_t ju_saw_plain(uint32_t ph)          { return (int32_t)(ph >> 7) - JU_ONE24; }
static inline int32_t ju_tri(uint32_t ph)                /* +1 at 0, -1 at the half turn */
{
    int32_t p = (int32_t)(ph >> 8);
    return p < (1 << 23) ? JU_ONE24 - 4 * p : 4 * p - 3 * JU_ONE24;
}
static inline int32_t ju_sine_shaped(uint32_t ph)        /* the original's analog-style sine: t (1.5 - 0.5 t^2) */
{
    int32_t t = ju_tri(ph), t2 = ju_mulq(t, t, 24);
    return (int32_t)(((int64_t)t * ((3 << 24) - t2)) >> 25);
}
enum { JW_TRI, JW_SAW, JW_PULSE, JW_SINE, JW_NOISE };
static inline int32_t ju_wave(int w, uint32_t ph, uint32_t inc, uint32_t pw, int blep)
{
    switch (w) {
    case JW_TRI:  return ju_tri(ph);
    case JW_SAW:  return blep ? ju_saw(ph, inc) : ju_saw_plain(ph);
    case JW_PULSE:
        return blep ? ju_saw(ph - pw, inc) - ju_saw(ph, inc) : ju_saw_plain(ph - pw) - ju_saw_plain(ph);   /* high for the first pw of the cycle */
    default:      return ju_sine_shaped(ph);
    }
}

/* ---------------------------------------------------------------- envelope */
/* RC-style exponential segments, one step per block (see engine.cpp stepEnv). f: Q16 rate factor
 * (> 1 shortens the times: key follow and the voice's tolerance) */
static inline int32_t ju_env_coef(uint32_t l, uint32_t f)
{
    return ju_exp2n_q30((uint32_t)(((uint64_t)l * f) >> 16));
}
static int32_t ju_env_step(int32_t e, uint8_t *st, const uint8_t *P, int a, int d, int s, int r, uint32_t f)
{
    const int32_t sus = (int32_t)P[s] * 4210752;
    switch (*st) {
    case 0: {
        const int32_t top = 0x50000000;                /* 1.25: the RC charges towards it and stops at 1.0 */
        e = top + ju_mulq(e - top, ju_env_coef(JU_ATT[P[a]], f), 30);
        if (e >= JU_ONE30) { e = JU_ONE30; *st = 1; }
        break;
    }
    case 1:
        e = sus + ju_mulq(e - sus, ju_env_coef(JU_DEC[P[d]], f), 30);
        if (e - sus < 536871) *st = 2;                 /* 0.0005 */
        break;
    case 2:
        e += ju_mulq(sus - e, 3277, 16);                /* 0.05 */
        break;
    default:
        e = ju_mulq(e, ju_env_coef(JU_DEC[P[r]], f), 30);
        break;
    }
    return e;
}

/* ------------------------------------------------------------ key assigner */
static int ju_layer_first(int l) { return (ju_global[G_KEYMODE] == KM_WHOLE || l == 0) ? 0 : JU_NV / 2; }
static int ju_layer_count(int l) { (void)l; return ju_global[G_KEYMODE] == KM_WHOLE ? JU_NV : JU_NV / 2; }
static int ju_layer_for_key(int note) { return note < ju_global[G_SPLIT] ? 1 : 0; }

static void ju_refresh_layers(void)
{
    int l, i;
    for (l = 0; l < 2; l++) {
        if (ju_global[G_KEYMODE] == KM_WHOLE && l == 1) continue;
        for (i = 0; i < ju_layer_count(l); i++) ju_v[ju_layer_first(l) + i].layer = (int8_t)l;
    }
}

static void ju_arp_clear(void);
static void ju_release_voice(ju_voice_t *v) { v->gate = 0; v->st1 = 3; v->st2 = 3; }

static void ju_seq_release(int p);
static void ju_seq_forget(void);
static void ju_all_notes_off(void)
{
    int i;
    for (i = 0; i < JU_NV; i++) { ju_v[i].gate = 0; ju_v[i].st1 = 3; ju_v[i].st2 = 3; }
    ju_nheld[0] = ju_nheld[1] = 0;
    ju_arp_clear();
    ju_seq_forget();
}

static void ju_trigger(ju_voice_t *v, int note, int glide)
{
    int l = v->layer;
    uint8_t pm = ju_global[G_PORTA_MODE];
    int porta = ju_global[G_PORTA_TIME] > 0 && (pm == PM_ON || (pm == PM_UPPER && l == 0));
    (void)glide;
    v->pitch = porta ? v->pitch : note << 16;           /* polyphonic portamento: each voice glides from its last pitch */
    v->note = (int8_t)note;
    v->gate = 1; v->st1 = 0; v->st2 = 0;
    v->age = ++ju_age_counter;
    v->lfoGain = 0;
    if (!v->active) {
        v->active = 1;
        v->env1 = v->env2 = 0;
        v->z[0] = v->z[1] = v->z[2] = v->z[3] = 0;
        v->hpz = 0; v->vcaPrev = 0; v->nxt2 = 0;
        v->fresh = 1;
    }
}

static void ju_solo_update(int l, int note, int retrig)
{
    ju_voice_t *v = &ju_v[ju_layer_first(l)];
    int i;
    for (i = 1; i < ju_layer_count(l); i++) {
        ju_voice_t *o = &ju_v[ju_layer_first(l) + i];
        if (o->gate) ju_release_voice(o);
    }
    if (retrig || !v->gate) ju_trigger(v, note, 1);
    else v->note = (int8_t)note;                        /* released back onto a held key: the pitch moves, no new envelope */
}

static void ju_unison_update(int l, int retrig)
{
    int nv = ju_layer_count(l), first = ju_layer_first(l), nh = ju_nheld[l], n, i;
    if (nh == 0) return;
    n = nh < nv ? nh : nv;
    for (i = 0; i < nv; i++) {
        ju_voice_t *v = &ju_v[first + i];
        int key = ju_held[l][nh - n + (i % n)].note;
        if (!v->gate || (retrig && v->note != key) || (retrig && n == 1)) ju_trigger(v, key, 1);
        else v->note = (int8_t)key;
    }
}

static void ju_layer_key_down(int l, int note)
{
    int nh = ju_nheld[l], i, j, first, nv;
    ju_held_t *h = ju_held[l];
    ju_voice_t *pick = 0;
    uint8_t as;
    for (i = 0; i < nh; i++)
        if (h[i].note == note) {
            for (j = i; j + 1 < nh; j++) h[j] = h[j + 1];
            nh--;
            break;
        }
    if (nh >= 16) { for (j = 0; j + 1 < nh; j++) h[j] = h[j + 1]; nh--; }
    h[nh].note = (int8_t)note; h[nh].down = 1;
    ju_nheld[l] = ++nh;

    as = ju_patch[l].v[P_ASSIGN];
    if (as == AS_SOLO) { ju_solo_update(l, note, 1); return; }
    if (as == AS_UNISON) { ju_unison_update(l, 1); return; }

    first = ju_layer_first(l); nv = ju_layer_count(l);
    for (i = 0; i < nv; i++)                            /* the same key sounding already: re-use its voice */
        if (ju_v[first + i].note == note && ju_v[first + i].gate) { pick = &ju_v[first + i]; break; }
    if (!pick && as == AS_POLY2)                        /* POLY-2: a key returns to the voice it used last, when free */
        for (i = 0; i < nv; i++)
            if (ju_v[first + i].note == note && !ju_v[first + i].gate) { pick = &ju_v[first + i]; break; }
    if (!pick) {                                        /* rotate through the voices so release tails ring on */
        int start = ju_next_voice[l];
        for (i = 0; i < nv; i++) {
            ju_voice_t *v = &ju_v[first + (start + i) % nv];
            if (!v->gate) { pick = v; ju_next_voice[l] = (start + i + 1) % nv; break; }
        }
    }
    if (!pick) {                                        /* all held: take the oldest */
        pick = &ju_v[first];
        for (i = 1; i < nv; i++)
            if (ju_v[first + i].age < pick->age) pick = &ju_v[first + i];
    }
    ju_trigger(pick, note, 0);
}

static void ju_layer_release(int l, int note)
{
    uint8_t as = ju_patch[l].v[P_ASSIGN];
    int nh = ju_nheld[l], first, nv, i;
    if (as == AS_SOLO) {
        if (nh > 0) { ju_solo_update(l, ju_held[l][nh - 1].note, 0); return; }
    } else if (as == AS_UNISON) {
        if (nh > 0) { ju_unison_update(l, 0); return; }
    }
    first = ju_layer_first(l); nv = ju_layer_count(l);
    for (i = 0; i < nv; i++) {
        ju_voice_t *v = &ju_v[first + i];
        if (v->gate && (as == AS_UNISON || as == AS_SOLO || v->note == note)) ju_release_voice(v);
    }
}

static void ju_layer_key_up(int l, int note)
{
    int nh = ju_nheld[l], i, j;
    ju_held_t *h = ju_held[l];
    int holding = ju_sustain[l] || ju_global[l == 0 ? G_HOLD_U : G_HOLD_L];
    for (i = 0; i < nh; i++)
        if (h[i].note == note) {
            if (holding) { h[i].down = 0; return; }
            for (j = i; j + 1 < nh; j++) h[j] = h[j + 1];
            ju_nheld[l] = nh - 1;
            break;
        }
    ju_layer_release(l, note);
}

static void ju_release_unheld(int l)
{
    int i, j;
    for (i = ju_nheld[l] - 1; i >= 0; i--)
        if (!ju_held[l][i].down) {
            int note = ju_held[l][i].note;
            for (j = i; j + 1 < ju_nheld[l]; j++) ju_held[l][j] = ju_held[l][j + 1];
            ju_nheld[l]--;
            ju_layer_release(l, note);
        }
}

static void ju_part_off(int l)
{
    int i, first = ju_layer_first(l), nv = ju_layer_count(l);
    for (i = 0; i < nv; i++)
        if (ju_v[first + i].gate) ju_release_voice(&ju_v[first + i]);
    ju_nheld[l] = 0;
    if (l == 0) ju_arp_clear();
}

/* ------------------------------------------------------------ arpeggiator */
static int ju_arp_enabled(void) { return ju_global[G_ARP_ON] != 0; }
static int ju_arp_owns_key(int note)
{
    if (!ju_arp_enabled()) return 0;
    if (ju_global[G_KEYMODE] == KM_SPLIT) return note < ju_global[G_SPLIT];     /* the arpeggio plays the lower zone */
    return 1;
}
static void ju_arp_add(int note)
{
    int i;
    for (i = 0; i < ju_arp_n; i++) if (ju_arp_keys[i] == note) { ju_arp_down[i] = 1; return; }
    if (ju_arp_n >= 16) return;
    i = ju_arp_n++;
    while (i > 0 && ju_arp_keys[i - 1] > note) { ju_arp_keys[i] = ju_arp_keys[i - 1]; ju_arp_down[i] = ju_arp_down[i - 1]; i--; }
    ju_arp_keys[i] = (int8_t)note;
    ju_arp_down[i] = 1;
}
static void ju_arp_remove(int note)
{
    int i, j;
    int holding = ju_sustain[0] || ju_global[G_HOLD_U] || ju_global[G_HOLD_L];
    for (i = 0; i < ju_arp_n; i++) {
        if (ju_arp_keys[i] != note) continue;
        if (holding) { ju_arp_down[i] = 0; return; }
        for (j = i; j + 1 < ju_arp_n; j++) { ju_arp_keys[j] = ju_arp_keys[j + 1]; ju_arp_down[j] = ju_arp_down[j + 1]; }
        ju_arp_n--;
        return;
    }
}
static int ju_arp_next(void)
{
    int span = ju_arp_n * (ju_global[G_ARP_RANGE] + 1), idx;
    if (span <= 0) return -1;
    switch (ju_global[G_ARP_MODE]) {
    case ARP_UP:   ju_arp_pos = (ju_arp_pos + 1) % span; idx = ju_arp_pos; break;
    case ARP_DOWN: ju_arp_pos = (ju_arp_pos + span - 1) % span; idx = ju_arp_pos; break;
    case ARP_UPDOWN:
        if (span == 1) ju_arp_pos = 0;
        else {
            ju_arp_pos += ju_arp_dir;
            if (ju_arp_pos >= span) { ju_arp_pos = span - 2; ju_arp_dir = -1; }
            else if (ju_arp_pos < 0) { ju_arp_pos = 1; ju_arp_dir = 1; }
        }
        idx = ju_arp_pos;
        break;
    default: idx = (int)(ju_rand(&ju_lfo_rng) % (uint32_t)span); break;
    }
    if (idx >= span) idx = span - 1;
    return ju_arp_keys[idx % ju_arp_n] + 12 * (idx / ju_arp_n);
}
static void ju_arp_play(int note, int on)
{
    int km = ju_global[G_KEYMODE], a = 0, b = 0, l, i, j;
    if (km == KM_DUAL) { a = 0; b = 1; } else if (km == KM_SPLIT) { a = 1; b = 1; }
    for (l = a; l <= b; l++) {
        if (on) { ju_layer_key_down(l, note); continue; }
        for (i = 0; i < ju_nheld[l]; i++)
            if (ju_held[l][i].note == note) {
                for (j = i; j + 1 < ju_nheld[l]; j++) ju_held[l][j] = ju_held[l][j + 1];
                ju_nheld[l]--;
                break;
            }
        ju_layer_release(l, note);
    }
}
static void ju_arp_clear(void) { ju_arp_n = 0; ju_arp_sounding = -1; ju_arp_gate = 0; }
static void ju_arp_tick(void)
{
    uint32_t old;
    int wrapped;
    if (!ju_arp_enabled()) {
        if (ju_last_arp_on) { if (ju_arp_sounding >= 0) ju_arp_play(ju_arp_sounding, 0); ju_arp_clear(); }
        ju_last_arp_on = 0;
        return;
    }
    ju_last_arp_on = 1;
    old = ju_arp_phase;
    ju_arp_phase += JU_ARP_INC[ju_global[G_ARP_RATE]];
    wrapped = ju_arp_phase < old;
    if (ju_arp_gate && (wrapped || ju_arp_phase >= 0x80000000u)) {
        if (ju_arp_sounding >= 0) ju_arp_play(ju_arp_sounding, 0);
        ju_arp_sounding = -1;
        ju_arp_gate = 0;
    }
    if (wrapped) {
        int n = ju_arp_next();
        if (n >= 0 && n < 128) { ju_arp_play(n, 1); ju_arp_sounding = (int8_t)n; ju_arp_gate = 1; }
    }
}

/* ----------------------------------------------------------------- sequencer */
/* One pattern per part (16 of them to choose from), up to 64 steps of a sixteenth note, one note per step, a rest or a tie
 * (the note sounds on through the step). The clock runs at 24 ticks to a quarter note, from the tempo (G_TEMPO, 40 + value BPM)
 * or from MIDI clock; a step is 6 ticks. The notes go through the part's voice assigner like key presses, so the patch's assign
 * mode and portamento apply. While it plays, a key transposes the pattern (the key is the new root, C4 = as written), unless
 * REC is armed: then the keys sound and are recorded. The pattern's gate is how many of a step's 6 ticks a note lasts. */
ju_pat_t ju_pat[2][JU_SEQ_PATS] JU_POOL;
uint8_t ju_seq_cur[2];                               /* the selected pattern of each part */
volatile uint8_t ju_seq_run, ju_seq_rec, ju_seq_req; /* playing; REC armed; a request from the panel: 1 start, 2 stop */
volatile uint8_t ju_seq_dirty[2];                    /* a pattern changed (the panel saves it) */
uint8_t ju_seq_pos[2];                               /* the step playing */
uint8_t ju_seq_cursor[2];                            /* the step the panel edits and step recording writes to */
static int8_t ju_seq_trans[2];
static int16_t ju_seq_snd[2] = {-1, -1};             /* the note the sequencer holds (after transposition), -1 none */
static uint8_t ju_seq_gate_left[2];                  /* ticks until the note ends; 255: held on into a tie */
static uint8_t ju_seq_tick6;                         /* tick within the step */
static uint32_t ju_seq_acc;                          /* internal clock, samples x 256 */
static volatile uint8_t ju_seq_ext_ticks;            /* MIDI clock pulses waiting */
static int16_t ju_seq_rec_key[2] = {-1, -1};         /* the key being recorded (real time), -1 none */
static int16_t ju_seq_rec_step[2] = {-1, -1};

static void ju_seq_forget(void) { ju_seq_snd[0] = ju_seq_snd[1] = -1; }      /* every voice was released: nothing to release later */
ju_song_t ju_song;
volatile uint8_t ju_song_dirty;
volatile uint8_t ju_song_row;                            /* the row playing (while a song plays) */
uint8_t ju_song_sel;                                     /* the row PLAY starts from (the panel's cursor) */
static uint8_t ju_song_act;                              /* a song is playing (it was started in song mode) */
static int8_t ju_seq_pidx[2] = {-1, -1};                 /* the pattern each part plays in the song's row, -1 none */
static uint32_t ju_song_steps, ju_song_row_steps;        /* steps played in the row, and the row's length */
static ju_pat_t ju_seq_empty;                            /* what a part with no pattern in the row plays: nothing */
static int ju_seq_idx(int p) { return ju_song_act ? ju_seq_pidx[p] : (int)(ju_seq_cur[p] % JU_SEQ_PATS); }
static ju_pat_t *ju_seq_pat(int p) { int i = ju_seq_idx(p); return i < 0 ? &ju_seq_empty : &ju_pat[p][i]; }
int ju_seq_playing_pat(int p) { return ju_seq_idx(p); }  /* the pattern a part plays now (-1 none) */
int ju_song_playing(void) { return ju_song_act && ju_seq_run; }
int ju_seq_active(int p)                             /* the part has a pattern with a note in it */
{
    const ju_pat_t *t = ju_seq_pat(p);
    int i;
    for (i = 0; i < t->len && i < JU_SEQ_STEPS; i++) if (t->type[i] == SQ_NOTE) return 1;
    return 0;
}
void ju_seq_defaults(void)
{
    int p, k, i;
    for (p = 0; p < 2; p++)
        for (k = 0; k < JU_SEQ_PATS; k++) {
            ju_pat_t *t = &ju_pat[p][k];
            for (i = 0; i < JU_SEQ_STEPS; i++) { t->note[i] = 60; t->type[i] = SQ_REST; }
            t->len = 16; t->gate = 3; t->pad[0] = t->pad[1] = 0;
        }
    for (p = 0; p < 2; p++) { ju_seq_cur[p] = 0; ju_seq_cursor[p] = 0; ju_seq_dirty[p] = 0; }
    for (i = 0; i < JU_SEQ_STEPS; i++) { ju_seq_empty.note[i] = 60; ju_seq_empty.type[i] = SQ_REST; }
    ju_seq_empty.len = 16; ju_seq_empty.gate = 3;
    ju_song.n = 0; ju_song_dirty = 0; ju_song_sel = 0; ju_song_act = 0; ju_song_row = 0;
    for (i = 0; i < JU_SONG_ROWS; i++) { ju_song.row[i].pat[0] = ju_song.row[i].pat[1] = JU_SONG_NONE; ju_song.row[i].rep = 1; }
}
static void ju_seq_release(int p)
{
    if (ju_seq_snd[p] >= 0) ju_layer_key_up(p, ju_seq_snd[p]);
    ju_seq_snd[p] = -1;
    ju_seq_gate_left[p] = 0;
}
/* ---- the metronome's click: a short decaying sine, higher on the first beat of a bar. It is mixed after the USB tap, so it is heard in
 * the headphones and speaker and never in the USB audio. */
static struct { int32_t pos, len; uint32_t ph, inc; } ju_click = {1, 0, 0, 0};
volatile uint32_t ju_click_count;                       /* clicks triggered (the host tests count them) */
uint8_t ju_click_accent;                                /* the last one was a first beat */
void ju_click_trig(int accent)
{
    ju_click.pos = 0;
    ju_click.len = 1100;                                /* 25 ms */
    ju_click.ph = 0;
    ju_click.inc = accent ? (uint32_t)(1500.0 * 4294967296.0 / 44100.0) : (uint32_t)(1000.0 * 4294967296.0 / 44100.0);
    ju_click_accent = (uint8_t)(accent != 0);
    ju_click_count++;
}
void ju_click_render(int32_t *out, uint32_t n)          /* interleaved stereo, Q15 */
{
    uint32_t i;
    if (ju_click.pos >= ju_click.len) return;
    for (i = 0; i < n && ju_click.pos < ju_click.len; i++, ju_click.pos++) {
        int32_t env = ((ju_click.len - ju_click.pos) * 65536) / ju_click.len;                      /* 1 -> 0, Q16 (a 32-bit divide) */
        int32_t v = ju_mulq(ju_mulq(ju_sine_turn(ju_click.ph) >> 9, env, 16), (int32_t)ju_global[G_CLICKVOL] * 128, 15);
        int32_t l = out[2 * i] + v, r = out[2 * i + 1] + v;
        out[2 * i] = l > 32767 ? 32767 : l < -32767 ? -32767 : l;
        out[2 * i + 1] = r > 32767 ? 32767 : r < -32767 ? -32767 : r;
        ju_click.ph += ju_click.inc;
    }
}

/* ---- undo: the last JU_UNDO_N pattern changes of recording (a note written, a tie added) and of erasing steps, one step at a time.
 * An entry remembers what the step was; entries pushed together (an erase of a step and its ties) are undone together. */
#define JU_UNDO_N 32
typedef struct { uint8_t part, pat, step, note, type, grp; } ju_undo_t;
static ju_undo_t ju_undo[JU_UNDO_N];
static uint8_t ju_undo_head, ju_undo_cnt;
static void ju_undo_push(int part, int step, int grp)    /* before the step changes */
{
    const ju_pat_t *t = ju_seq_pat(part);
    ju_undo_t *u = &ju_undo[ju_undo_head];
    if (ju_seq_idx(part) < 0) return;
    u->part = (uint8_t)part; u->pat = (uint8_t)ju_seq_idx(part); u->step = (uint8_t)step;
    u->note = t->note[step]; u->type = t->type[step]; u->grp = (uint8_t)grp;
    ju_undo_head = (uint8_t)((ju_undo_head + 1) % JU_UNDO_N);
    if (ju_undo_cnt < JU_UNDO_N) ju_undo_cnt++;
}
int ju_seq_undo_count(void) { return ju_undo_cnt; }
static void ju_seq_undo(void)                            /* the last change, with the ones that went with it */
{
    int more = 1;
    while (more && ju_undo_cnt) {
        ju_undo_head = (uint8_t)((ju_undo_head + JU_UNDO_N - 1) % JU_UNDO_N);
        {
            const ju_undo_t *u = &ju_undo[ju_undo_head];
            ju_pat_t *t = &ju_pat[u->part][u->pat];
            t->note[u->step] = u->note; t->type[u->step] = u->type;
            ju_seq_dirty[u->part] = 1;
            ju_seq_cursor[u->part] = u->step;
            more = u->grp;
        }
        ju_undo_cnt--;
    }
}
static void ju_seq_erase(int part, int step)             /* the step and the ties that hang on it become rests */
{
    ju_pat_t *t = ju_seq_pat(part);
    int k = step, first = 1;
    if (ju_seq_idx(part) < 0 || step < 0 || step >= t->len) return;
    do {
        if (t->type[k] != SQ_REST) { ju_undo_push(part, k, !first); first = 0; t->type[k] = SQ_REST; }
        k++;
    } while (k < t->len && t->type[k] == SQ_TIE);
    ju_seq_dirty[part] = 1;
}
/* a request from the panel (handled at the next block, in the audio's context, where the undo history is kept) */
volatile uint8_t ju_seq_op, ju_seq_op_part, ju_seq_op_step;      /* ju_seq_op: 1 undo, 2 erase a step (set it last) */

static uint32_t ju_seq_cnt, ju_seq_cnt_total;            /* count-in: pulses left, and in all */
static uint32_t ju_seq_pp;                               /* pulses since the start */
int ju_seq_tick_phase(void) { return ju_seq_tick6; }                  /* ticks of the step run so far (0: the step's last tick has just run) */
int ju_seq_count_beats_left(void) { return (int)((ju_seq_cnt + 23u) / 24u); }

static void ju_song_begin_row(int row)                    /* the row's patterns, its length in steps, and both parts back at their step 0 */
{
    const ju_srow_t *r = &ju_song.row[row];
    int p, maxlen = 0;
    ju_song_row = (uint8_t)row;
    for (p = 0; p < 2; p++) {
        ju_seq_pidx[p] = r->pat[p] < JU_SEQ_PATS ? (int8_t)r->pat[p] : -1;
        if (ju_seq_pidx[p] >= 0 && ju_seq_pat(p)->len > maxlen) maxlen = ju_seq_pat(p)->len;
    }
    ju_song_row_steps = (uint32_t)(r->rep < 1 ? 1 : r->rep > 16 ? 16 : r->rep) * (uint32_t)(maxlen ? maxlen : 16);
    ju_song_steps = 0;
    for (p = 0; p < 2; p++) {
        const ju_pat_t *t = ju_seq_pat(p);
        ju_seq_release(p);
        ju_seq_pos[p] = (uint8_t)(t->len ? t->len - 1 : 0);
    }
}
static void ju_seq_start_state(void)
{
    int p;
    ju_song_act = (uint8_t)(ju_global[G_SONG] && ju_song.n > 0);
    if (ju_song_act) ju_song_begin_row(ju_song_sel < ju_song.n ? ju_song_sel : 0);
    for (p = 0; p < 2; p++) {
        const ju_pat_t *t = ju_seq_pat(p);
        ju_seq_release(p);
        ju_seq_pos[p] = (uint8_t)(t->len ? t->len - 1 : 0);          /* the first step to play is 0 */
        ju_seq_rec_key[p] = ju_seq_rec_step[p] = -1;
    }
    ju_seq_tick6 = 0;
    ju_seq_pp = 0;
    ju_seq_run = 1;
}
static void ju_seq_start(void)
{
    ju_seq_cnt = 0;
    ju_seq_ext_ticks = 0;
    ju_seq_acc = 0xFFFFFFFFu;                                         /* the first tick comes at once (internal clock) */
    ju_seq_start_state();
}
static void ju_seq_countin(void)                                      /* PLAY with REC armed: the metronome counts, then it starts */
{
    ju_seq_cnt_total = ju_seq_cnt = (uint32_t)ju_global[G_COUNTIN] * 24u;
    ju_seq_ext_ticks = 0;
    ju_seq_acc = 0xFFFFFFFFu;
}
static void ju_seq_stop(void)
{
    int p;
    ju_seq_run = 0;
    ju_seq_cnt = 0;
    ju_song_act = 0;
    for (p = 0; p < 2; p++) { ju_seq_release(p); ju_seq_rec_key[p] = ju_seq_rec_step[p] = -1; }
}
static void ju_seq_step(int p)
{
    ju_pat_t *t = ju_seq_pat(p);
    int pos, type, nexttie, n, rp = ju_global[G_PANEL] & 1;
    if (!t->len) return;
    pos = (ju_seq_pos[p] + 1) % t->len;
    ju_seq_pos[p] = (uint8_t)pos;
    type = t->type[pos];
    if (ju_seq_rec && p == rp && ju_seq_rec_key[p] >= 0) {            /* real-time recording: a key still down ties on; its own step is played live */
        if (pos == ju_seq_rec_step[p]) type = -1;                     /* nothing for the sequencer to play */
        else { if (t->type[pos] != SQ_TIE) ju_undo_push(p, pos, 0); t->type[pos] = SQ_TIE; ju_seq_dirty[p] = 1; type = SQ_TIE; }
    }
    if (type < 0) return;
    nexttie = t->len > 1 && t->type[(pos + 1) % t->len] == SQ_TIE;
    if (type == SQ_NOTE) {
        if (ju_seq_snd[p] >= 0) ju_seq_release(p);
        n = (int)t->note[pos] + (ju_global[G_SEQKEYS] ? ju_seq_trans[p] : 0);
        n = n < 0 ? 0 : n > 127 ? 127 : n;
        ju_layer_key_down(p, n);
        ju_seq_snd[p] = (int16_t)n;
        ju_seq_gate_left[p] = nexttie ? 255 : (uint8_t)(t->gate < 1 ? 1 : t->gate > 6 ? 6 : t->gate);
    } else if (type == SQ_TIE) {
        if (ju_seq_snd[p] >= 0) ju_seq_gate_left[p] = nexttie ? 255 : (uint8_t)(t->gate < 1 ? 1 : t->gate > 6 ? 6 : t->gate);
    } else if (ju_seq_snd[p] >= 0) {
        ju_seq_release(p);
    }
}
static void ju_seq_tick(void)
{
    int p;
    for (p = 0; p < 2; p++)
        if (ju_seq_snd[p] >= 0 && ju_seq_gate_left[p] != 255 && ju_seq_gate_left[p] && --ju_seq_gate_left[p] == 0) ju_seq_release(p);
    if (ju_seq_tick6 == 0) {
        if (ju_song_act) {                                                  /* the song: a row ends after its repeats of the longer pattern */
            if (ju_song_steps >= ju_song_row_steps) {
                int next = ju_song_row + 1;
                if (next >= ju_song.n) {
                    if (ju_global[G_SONGLOOP] && ju_song.n > 0) next = 0;
                    else { ju_seq_stop(); return; }                          /* the end of the song */
                }
                ju_song_begin_row(next);
            }
            ju_song_steps++;
        }
        for (p = 0; p < 2; p++) ju_seq_step(p);
    }
    if (++ju_seq_tick6 >= 6) ju_seq_tick6 = 0;
}
/* one clock pulse (24 to a quarter note): the count-in, or a tick of the sequencer, with the metronome's click on the beat */
static void ju_seq_pulse(void)
{
    if (!ju_seq_run && !ju_seq_cnt) return;                          /* (stopped inside this block: the rest of its pulses are ignored) */
    if (ju_seq_cnt) {
        uint32_t done = ju_seq_cnt_total - ju_seq_cnt;
        if (done % 24u == 0) ju_click_trig((done / 24u) % 4u == 0);
        if (--ju_seq_cnt == 0) ju_seq_start_state();                  /* the next pulse is step 0, on the beat */
        return;
    }
    if (ju_global[G_METRO] == 2 || (ju_global[G_METRO] == 1 && ju_seq_rec))
        if (ju_seq_pp % 24u == 0) ju_click_trig((ju_seq_pp / 24u) % 4u == 0);
    ju_seq_pp++;
    ju_seq_tick();
}
static void ju_seq_block(void)                                       /* once per control block */
{
    if (ju_seq_req) {
        uint8_t r = ju_seq_req;
        ju_seq_req = 0;
        if (r == 1) { if (ju_seq_rec && ju_global[G_COUNTIN] && !ju_seq_run && !ju_seq_cnt) ju_seq_countin(); else ju_seq_start(); }
        else ju_seq_stop();
    }
    if (ju_seq_op) {
        uint8_t o = ju_seq_op;
        ju_seq_op = 0;
        if (o == 1) ju_seq_undo(); else if (o == 2) ju_seq_erase(ju_seq_op_part & 1, ju_seq_op_step);
    }
    if (!ju_seq_run && !ju_seq_cnt) return;
    if (ju_global[G_SYNC]) {
        while (ju_seq_ext_ticks) { ju_seq_ext_ticks--; ju_seq_pulse(); }
    } else {
        uint32_t period = 28224000u / (uint32_t)(40 + ju_global[G_TEMPO]);       /* 44100 x 60 / (24 x bpm), x 256 */
        if (ju_seq_acc == 0xFFFFFFFFu) ju_seq_acc = period;
        else ju_seq_acc += JU_BLOCK * 256u;
        while (ju_seq_acc >= period) { ju_seq_acc -= period; ju_seq_pulse(); }
    }
}
/* MIDI realtime (0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop): only with the MIDI clock selected */
void ju_seq_midi(uint8_t st)
{
    if (!ju_global[G_SYNC]) return;
    if (st == 0xF8u) { if ((ju_seq_run || ju_seq_cnt) && ju_seq_ext_ticks < 8) ju_seq_ext_ticks++; }
    else if (st == 0xFAu) ju_seq_start();
    else if (st == 0xFBu) { ju_seq_run = 1; }
    else if (st == 0xFCu) { ju_seq_stop(); }
}
/* a note in: recorded (REC armed, on the part being edited), or a transposition (playing, an active pattern); else 0 */
static int ju_seq_filter(int mask, int note, int on)
{
    int p, rp = ju_global[G_PANEL] & 1;
    for (p = 0; p < 2; p++) {
        ju_pat_t *t;
        if (!(mask & (1 << p))) continue;
        t = ju_seq_pat(p);
        if (ju_seq_idx(p) < 0) continue;                              /* no pattern in this row of the song: the keys play */
        if (ju_seq_rec && p == rp && t->len && !ju_seq_cnt) {        /* the key sounds and is written */
            if (on) {
                int s;
                if (!ju_seq_run) { s = ju_seq_cursor[p] % t->len; ju_seq_cursor[p] = (uint8_t)((s + 1) % t->len); }
                else {                                               /* real time: to the nearest point of the grid (G_QUANT) */
                    int g = 1 << (ju_global[G_QUANT] & 3), unit = 6 * g;
                    int ticks = ju_seq_pos[p] * 6 + (ju_seq_tick6 ? ju_seq_tick6 : 6);          /* ticks since the pattern began */
                    s = (((ticks + unit / 2) / unit) * g) % t->len;
                    ju_seq_rec_key[p] = (int16_t)note; ju_seq_rec_step[p] = (int16_t)s;
                }
                ju_undo_push(p, s, 0);
                t->note[s] = (uint8_t)note; t->type[s] = SQ_NOTE;
                ju_seq_dirty[p] = 1;
            } else if (ju_seq_rec_key[p] == note) ju_seq_rec_key[p] = -1;
            continue;
        }
        if (ju_seq_run && ju_global[G_SEQKEYS] && ju_seq_active(p)) {   /* TRAN: the key is the new root and does not sound (PLAY: it sounds, over the pattern) */
            if (on) ju_seq_trans[p] = (int8_t)(note - 60);
            mask &= ~(1 << p);
        }
    }
    return mask;
}

/* which parts listen to a MIDI channel (0-15; 0xFF = not from MIDI): bit 0 = part 1 (Upper), bit 1 = part 2 */
int ju_part_mask(uint8_t ch)
{
    int u = ju_global[G_CH_U] == 0 || ju_global[G_CH_U] == ch + 1;
    int lo = ju_global[G_CH_L] == 0 || ju_global[G_CH_L] == ch + 1;
    if (ju_global[G_KEYMODE] == KM_2PART) {
        if (ch == 0xFF) return 1;
        return (u ? 1 : 0) | (lo ? 2 : 0);
    }
    return (ch == 0xFF || u) ? 3 : 0;
}

void ju_note_on_m(int mask, uint8_t note)
{
    mask = ju_seq_filter(mask, note, 1);
    if (!mask) return;
    if (ju_global[G_KEYMODE] != KM_2PART) {
        if (ju_arp_owns_key(note)) { ju_arp_add(note); return; }
        switch (ju_global[G_KEYMODE]) {
        case KM_WHOLE: ju_layer_key_down(0, note); break;
        case KM_DUAL: ju_layer_key_down(0, note); ju_layer_key_down(1, note); break;
        default: ju_layer_key_down(ju_layer_for_key(note), note); break;
        }
        return;
    }
    if (mask & 1) { if (ju_arp_enabled()) ju_arp_add(note); else ju_layer_key_down(0, note); }    /* the arpeggio is part 1's */
    if (mask & 2) ju_layer_key_down(1, note);
}

void ju_note_on(uint8_t ch, uint8_t note) { ju_note_on_m(ju_part_mask(ch), note); }

void ju_note_off_m(int mask, uint8_t note)
{
    mask = ju_seq_filter(mask, note, 0);
    if (!mask) return;
    if (ju_global[G_KEYMODE] != KM_2PART) {
        if (ju_arp_owns_key(note)) { ju_arp_remove(note); return; }
        switch (ju_global[G_KEYMODE]) {
        case KM_WHOLE: ju_layer_key_up(0, note); break;
        case KM_DUAL: ju_layer_key_up(0, note); ju_layer_key_up(1, note); break;
        default: ju_layer_key_up(ju_layer_for_key(note), note); break;
        }
        return;
    }
    if (mask & 1) { if (ju_arp_enabled()) ju_arp_remove(note); else ju_layer_key_up(0, note); }
    if (mask & 2) ju_layer_key_up(1, note);
}

void ju_note_off(uint8_t ch, uint8_t note) { ju_note_off_m(ju_part_mask(ch), note); }

void ju_bend(uint8_t ch, int v14)                 /* -8192..8191 */
{
    int l, mask = ju_part_mask(ch);
    for (l = 0; l < 2; l++) if (mask & (1 << l)) ju_bend14[l] = v14;
}
void ju_modwheel_set(uint8_t ch, uint8_t v)
{
    int l, mask = ju_part_mask(ch);
    for (l = 0; l < 2; l++) if (mask & (1 << l)) ju_modwheel[l] = v;
}
void ju_sustain_set(uint8_t ch, uint8_t v)
{
    int l, mask = ju_part_mask(ch);
    for (l = 0; l < 2; l++) {
        int was;
        if (!(mask & (1 << l))) continue;
        was = ju_sustain[l];
        ju_sustain[l] = v >= 64;
        if (was && !ju_sustain[l]) {
            int i;
            if (!ju_global[l == 0 ? G_HOLD_U : G_HOLD_L]) ju_release_unheld(l);
            if (l == 0) for (i = ju_arp_n - 1; i >= 0; i--) if (!ju_arp_down[i]) ju_arp_remove(ju_arp_keys[i]);
        }
    }
}
void ju_all_off(uint8_t ch)
{
    int l, mask = ju_part_mask(ch);
    if (ju_global[G_KEYMODE] != KM_2PART || ch == 0xFF) { ju_all_notes_off(); return; }
    for (l = 0; l < 2; l++) if (mask & (1 << l)) ju_part_off(l);
}

/* -------------------------------------------------------------- patch init */
void ju_patch_init(ju_patch_t *p, const char *name)
{
    uint8_t *v = p->v;
    int i;
    for (i = 0; i < (int)sizeof(*p); i++) ((uint8_t *)p)[i] = 0;
    for (i = 0; i < 11 && name[i]; i++) p->name[i] = name[i];
    ju_fx_defaults(&p->fx);
    v[P_LFO_RATE] = 150; v[P_LFO_WAVE] = LFO_SINE;
    v[P_VM_DEST] = DEST_BOTH; v[P_VM_PW] = 0; v[P_VM_PWMODE] = PW_MAN;
    v[P_V1_RANGE] = 1; v[P_V1_WAVE] = V1_SAW;
    v[P_V2_RANGE] = 1; v[P_V2_TUNE] = 128; v[P_V2_WAVE] = V2_SAW;
    v[P_MIX] = 128;
    v[P_VCF_CUT] = 200; v[P_VCF_SLOPE] = 1; v[P_VCF_ENVSEL] = 1; v[P_VCF_KEY] = 128;
    v[P_VCA_LEVEL] = 220;
    v[P_E1_A] = 0; v[P_E1_D] = 120; v[P_E1_S] = 128; v[P_E1_R] = 120;
    v[P_E2_A] = 0; v[P_E2_D] = 120; v[P_E2_S] = 255; v[P_E2_R] = 110;
    v[P_ASSIGN] = AS_POLY1;
}

void ju_global_defaults(uint8_t *g)
{
    int i;
    for (i = 0; i < G_COUNT; i++) g[i] = 0;
    g[G_VOLUME] = 200; g[G_BAL_U] = 255; g[G_BAL_L] = 255; g[G_TUNE] = 128;
    g[G_KEYMODE] = KM_2PART; g[G_PANEL] = 0; g[G_SPLIT] = 60;
    g[G_CH_U] = 1; g[G_CH_L] = 2;
    g[G_ARP_RATE] = 120; g[G_ARP_MODE] = ARP_UP;
    g[G_PORTA_TIME] = 100; g[G_PORTA_MODE] = PM_OFF;
    g[G_BEND_VCO] = 90; g[G_BEND_V1] = 1; g[G_BEND_V2] = 1; g[G_BEND_VCF] = 0;
    g[G_MOD_VCO] = 70; g[G_MOD_VCOSW] = 1; g[G_MOD_VCF] = 0;
    g[G_VINTAGE] = 128;
    g[G_TEMPO] = 80;
    g[G_CLICKVOL] = 170;
}

#define JU_C(x) ((int32_t)((x) * 65536.0))
void ju_init(void)
{
    static const int32_t kP1[8] = {JU_C(0.6), JU_C(-1.1), JU_C(0.3), JU_C(1.4), JU_C(-0.5), JU_C(0.9), JU_C(-1.3), JU_C(0.2)};
    static const int32_t kP2[8] = {JU_C(-0.8), JU_C(0.5), JU_C(1.2), JU_C(-0.2), JU_C(1.0), JU_C(-1.4), JU_C(0.4), JU_C(-0.6)};
    static const int32_t kC[8] = {JU_C(4), JU_C(-7), JU_C(2), JU_C(9), JU_C(-5), JU_C(6), JU_C(-9), JU_C(1)};   /* cents */
    static const int32_t kE[8] = {JU_C(1.0 / 1.01), JU_C(1.0 / 0.98), JU_C(1.0 / 1.02), JU_C(1.0 / 0.99),
                                  JU_C(1.0 / 1.03), JU_C(1.0 / 0.97), JU_C(1.0), JU_C(1.0 / 1.02)};
    int i;
    uint8_t *b = (uint8_t *)ju_v;
    for (i = 0; i < (int)sizeof ju_v; i++) b[i] = 0;
    b = (uint8_t *)ju_ctl;
    for (i = 0; i < (int)sizeof ju_ctl; i++) b[i] = 0;
    ju_global_defaults(ju_global);
    ju_patch_init(&ju_patch[0], "INIT");
    ju_patch_init(&ju_patch[1], "INIT");
    ju_nheld[0] = ju_nheld[1] = 0;
    ju_sustain[0] = ju_sustain[1] = 0;
    ju_bend14[0] = ju_bend14[1] = 0;
    ju_modwheel[0] = ju_modwheel[1] = 0;
    ju_last_keymode = 255; ju_last_assign[0] = ju_last_assign[1] = 255;
    ju_last_hold[0] = ju_last_hold[1] = 0;
    ju_next_voice[0] = ju_next_voice[1] = 0;
    ju_age_counter = 0;
    ju_arp_clear(); ju_arp_pos = 0; ju_arp_dir = 1; ju_arp_phase = 0; ju_last_arp_on = 0; ju_stereo_layers = 0;
    ju_lfo_rng = 0x9E3779B9u;
    ju_ctl[0].first = ju_ctl[1].first = 1;
    for (i = 0; i < JU_NV; i++) {
        ju_voice_t *v = &ju_v[i];
        v->tolP1 = kP1[i & 7]; v->tolP2 = kP2[i & 7]; v->tolCut = kC[i & 7]; v->invTolEnv = kE[i & 7];
        v->rng = 0x9E3779B1u * (uint32_t)(i + 1) + 77u;
        v->ph1 = (uint32_t)i * (uint32_t)(0.137 * 4294967296.0);
        v->ph2 = (uint32_t)i * (uint32_t)(0.291 * 4294967296.0);
        v->st1 = v->st2 = 3;
        v->note = -1;
        v->pitch = 60 << 16;
        v->lfoGain = 65536;
    }
    ju_refresh_layers();
    ju_fx_init();
    ju_fx_tail = 0;
    ju_seq_defaults();
    ju_seq_run = ju_seq_rec = ju_seq_req = 0;
    ju_seq_snd[0] = ju_seq_snd[1] = -1;
    ju_seq_rec_key[0] = ju_seq_rec_key[1] = ju_seq_rec_step[0] = ju_seq_rec_step[1] = -1;
    ju_seq_trans[0] = ju_seq_trans[1] = 0;
    ju_seq_tick6 = 0;
    ju_seq_cnt = ju_seq_cnt_total = ju_seq_pp = 0;
    ju_seq_op = 0;
    ju_undo_head = ju_undo_cnt = 0;
    ju_click.pos = ju_click.len = 0;
    ju_click_count = 0;
}

/* ------------------------------------------------------------- per block */
#define JU_SMOOTH(x, t) ((x) += (((t) - (x)) * 5) >> 6)       /* 0.08: about 8 ms */
static void ju_update_layer(int l)
{
    ju_layer_t *c = &ju_ctl[l];
    const uint8_t *v = ju_patch[l].v;
    uint32_t prev = c->lfoPhase;
    int32_t wheel, step, bend, bo, ml, tg, lvl;

    /* main LFO */
    c->lfoPhase += JU_LFO_INC[v[P_LFO_RATE]];
    switch (v[P_LFO_WAVE]) {
    case LFO_SINE: c->lfo = ju_sine_turn(c->lfoPhase); break;
    case LFO_SAW: c->lfo = JU_ONE24 - (int32_t)(c->lfoPhase >> 7); break;
    case LFO_SQUARE: c->lfo = c->lfoPhase < 0x80000000u ? JU_ONE24 : -JU_ONE24; break;
    default:
        if (c->lfoPhase < prev) c->lfoHold = ju_rand_bip24(&ju_lfo_rng);
        c->lfo = c->lfoHold;
        break;
    }

    /* bender section */
    c->modLfoPhase += (uint32_t)(6.0 * JU_BLOCK / 44100.0 * 4294967296.0);
    wheel = ((int32_t)ju_modwheel[l] << 16) / 127;
    step = (int32_t)JU_RISE[ju_global[G_MOD_RISE]];
    if (c->modDepth < wheel) c->modDepth = wheel < c->modDepth + step ? wheel : c->modDepth + step;
    else c->modDepth = wheel;
    bend = ju_bend14[l];
    bo = (bend * 8 * (int32_t)ju_global[G_BEND_VCO]) / 255;                  /* full sensitivity = +-1 octave, Q16 */
    c->bendVco1 = ju_global[G_BEND_V1] ? bo : 0;
    c->bendVco2 = ju_global[G_BEND_V2] ? bo : 0;
    c->bendVcf = ju_global[G_BEND_VCFSW] ? (bend * 32 * (int32_t)ju_global[G_BEND_VCF]) / 255 : 0;
    ml = ju_mulq(ju_sine_turn(c->modLfoPhase), c->modDepth, 16);              /* Q24 */
    c->modVcoOct = ju_global[G_MOD_VCOSW] ? ((ml >> 9) * (int32_t)ju_global[G_MOD_VCO]) / 255 : 0;
    c->modVcfOct = ju_global[G_MOD_VCFSW] ? ((ml >> 7) * (int32_t)ju_global[G_MOD_VCF]) / 255 : 0;

    /* parameter smoothing */
    tg = (int32_t)JU_RES[v[P_VCF_RES]];
    lvl = (int32_t)v[P_VCA_LEVEL] * 257;
    lvl = ju_mulq(lvl, lvl, 16);
    if (c->first) {
        c->cutS = (int32_t)v[P_VCF_CUT] << 16; c->resS = tg; c->hpfS = (int32_t)v[P_HPF] << 16;
        c->mixS = (int32_t)v[P_MIX] * 257; c->lvlS = lvl; c->xmS = JU_XMOD[v[P_V1_XMOD]];
        c->pwS = (int32_t)v[P_VM_PW] * 257;
        c->first = 0;
    } else {
        int32_t t;
        t = (int32_t)v[P_VCF_CUT] << 16; JU_SMOOTH(c->cutS, t);
        JU_SMOOTH(c->resS, tg);
        t = (int32_t)v[P_HPF] << 16; JU_SMOOTH(c->hpfS, t);
        t = (int32_t)v[P_MIX] * 257; JU_SMOOTH(c->mixS, t);
        JU_SMOOTH(c->lvlS, lvl);
        t = JU_XMOD[v[P_V1_XMOD]]; JU_SMOOTH(c->xmS, t);
        t = (int32_t)v[P_VM_PW] * 257; JU_SMOOTH(c->pwS, t);
    }
}

void ju_prepare_block(void)
{
    int l, km = ju_global[G_KEYMODE];
    if (ju_panic_req) { ju_panic_req = 0; ju_all_notes_off(); }
    if (ju_patch_stage_req) { ju_patch[0] = ju_patch_stage[0]; ju_patch[1] = ju_patch_stage[1]; ju_patch_stage_req = 0; }
    for (l = 0; l < 2; l++)
        if (ju_load_req[l]) {
            uint8_t i = (uint8_t)(ju_load_req[l] - 1u);
            ju_load_req[l] = 0;
            if (i < 64) ju_patch[l] = ju_bank[i];
        }
    if (km != ju_last_keymode) {
        ju_all_notes_off();
        ju_last_keymode = (uint8_t)km;
        ju_refresh_layers();
    }
    for (l = 0; l < 2; l++) {
        uint8_t h = ju_global[l == 0 ? G_HOLD_U : G_HOLD_L];
        if (ju_last_hold[l] && !h && !ju_sustain[l]) {
            int i;
            ju_release_unheld(l);
            for (i = ju_arp_n - 1; i >= 0; i--) if (!ju_arp_down[i]) ju_arp_remove(ju_arp_keys[i]);
        }
        ju_last_hold[l] = h;
        if (ju_patch[l].v[P_ASSIGN] != ju_last_assign[l]) {
            if (ju_last_assign[l] != 255 && (km != KM_WHOLE || l == 0)) {     /* the allocator starts clean */
                int i, first = ju_layer_first(l), nv = ju_layer_count(l);
                for (i = 0; i < nv; i++)
                    if (ju_v[first + i].gate) ju_release_voice(&ju_v[first + i]);
                ju_nheld[l] = 0;
            }
            ju_last_assign[l] = ju_patch[l].v[P_ASSIGN];
        }
    }
    ju_arp_tick();
    ju_seq_block();
    ju_update_layer(0);
    ju_update_layer(1);
    ju_fx_control(&ju_patch[0].fx, 40 + ju_global[G_TEMPO]);
}

/* ------------------------------------------------------------- one voice */
static void ju_render_voice(ju_voice_t *v, int32_t *out)
{
    const int l = v->layer;
    const uint8_t *P = ju_patch[l].v;
    const ju_layer_t *c = &ju_ctl[l];
    const int note = v->note < 0 ? 60 : v->note;
    const int32_t vintage = ju_global[G_VINTAGE];                    /* /128 */
    uint32_t f1 = v->invTolEnv, f2 = v->invTolEnv;
    int32_t e1, lfo, delayStep, base, tune, o1, o2, lfoO, envO, m1, m2, cutO, lg, gNew, kNew, g2, g4, den, invNew, compNew;
    int32_t hpNew, vcaNew, trem, pwv;
    uint32_t inc1, inc2, pwNew;
    int porta, dest, n;

    /* key follow on the envelope times: longer below C4, shorter above (2^(keyRel / 2)) */
    if (P[P_E1_KEY] || P[P_E2_KEY]) {
        uint32_t kt = ju_exp2_q16((JU_NOTE_OCT[note] + 49152) >> 1);   /* ((note - 60) / 12) / 2: (n-69)/12 + 0.75 */
        if (P[P_E1_KEY]) f1 = (uint32_t)(((uint64_t)f1 * kt) >> 16);
        if (P[P_E2_KEY]) f2 = (uint32_t)(((uint64_t)f2 * kt) >> 16);
    }
    v->env1 = ju_env_step(v->env1, &v->st1, P, P_E1_A, P_E1_D, P_E1_S, P_E1_R, f1);
    v->env2 = ju_env_step(v->env2, &v->st2, P, P_E2_A, P_E2_D, P_E2_S, P_E2_R, f2);
    if (!v->gate && v->st2 == 3 && v->env2 < 53687) {              /* 0.00005 */
        v->active = 0; v->env1 = v->env2 = 0;
        return;
    }
    e1 = P[P_E1_POL] ? -v->env1 : v->env1;

    /* LFO delay: the amplitude ramps up after key-on */
    delayStep = (int32_t)JU_LFO_DLY[P[P_LFO_DELAY]];
    v->lfoGain += delayStep;
    if (v->lfoGain > 65536) v->lfoGain = 65536;
    lfo = ju_mulq(c->lfo, v->lfoGain, 16);

    /* portamento: RC glide */
    porta = ju_global[G_PORTA_TIME] > 0 && (ju_global[G_PORTA_MODE] == PM_ON || (ju_global[G_PORTA_MODE] == PM_UPPER && l == 0));
    if (porta) {
        int32_t a = ju_exp2n_q30(JU_PORTA[ju_global[G_PORTA_TIME]]);
        v->pitch = (note << 16) + ju_mulq(v->pitch - (note << 16), a, 30);
    } else v->pitch = note << 16;

    /* slow pitch drift: a random walk with a leak, in cents (Q16) */
    v->drift1 += (ju_rand_bip24(&v->rng) >> 8) * 1966 / 65536 - ju_mulq(v->drift1, 39, 16);       /* 0.03 */
    v->drift2 += (ju_rand_bip24(&v->rng) >> 8) * 1966 / 65536 - ju_mulq(v->drift2, 39, 16);
    v->driftCut += (ju_rand_bip24(&v->rng) >> 8) * 3277 / 65536 - ju_mulq(v->driftCut, 39, 16);    /* 0.05 */

    /* pitch, in octaves relative to A4 (Q16) */
    tune = ((int32_t)ju_global[G_TUNE] - 128) * 43 >> 1;
    base = (int32_t)(((int64_t)(v->pitch - (69 << 16)) * 87381) >> 20) + tune;
    dest = P[P_VM_DEST];
    lfoO = ju_mulq(lfo, JU_VCO_LFO[P[P_VM_LFO]], 24) + c->modVcoOct;
    envO = ju_mulq(e1, JU_VCO_ENV[P[P_VM_ENV]], 30);
    m1 = dest == DEST_VCO2 ? 0 : lfoO + envO;
    m2 = dest == DEST_VCO1 ? 0 : lfoO + envO;
#define JU_CENTS_OCT(cq) ((int32_t)(((int64_t)(cq) * 3495) >> 22))
    o1 = base + (((int32_t)P[P_V1_RANGE] - 1) * 65536) + m1 + c->bendVco1 +
         JU_CENTS_OCT(ju_mulq(v->tolP1 + v->drift1, vintage, 7));
    if (P[P_V2_LOW])                                                  /* LOW FREQ: fixed rate, ignores the key */
        o2 = (int32_t)(-10.7814 * 65536.0) + (2 << 16) * (int32_t)P[P_V2_RANGE] + JU_V2TUNE[P[P_V2_TUNE]] * 4 + m2;
    else
        o2 = base + (((int32_t)P[P_V2_RANGE] - 1) * 65536) + m2 + c->bendVco2 + JU_V2TUNE[P[P_V2_TUNE]] +
             JU_CENTS_OCT(ju_mulq(v->tolP2 + v->drift2, vintage, 7));
    inc1 = ju_inc(o1);
    inc2 = ju_inc(o2);

    /* VCF cutoff: log2(fc) = 4 + 10.2877 cut / 255 + modulation (Q16 octaves) */
    {
        int32_t env4f = P[P_VCF_ENVSEL] ? v->env2 : e1;
        int32_t keyAmt = ((int32_t)P[P_VCF_KEY] * 19738) >> 6;      /* 1.2 * key / 255, Q16 */
        lg = (4 << 16) + (int32_t)(((int64_t)c->cutS * 2644) >> 16);
        cutO = ju_mulq(env4f, JU_VCF_ENV[P[P_VCF_ENV]], 30) + ju_mulq(lfo, JU_VCF_LFO[P[P_VCF_LFO]], 24) +
               c->modVcfOct + c->bendVcf +
               ju_mulq(JU_NOTE_OCT[note] + 49152, keyAmt, 16) +
               JU_CENTS_OCT(ju_mulq(v->tolCut + v->driftCut, vintage, 7));
        lg += cutO;
    }
    gNew = ju_g_of(lg);                                                  /* Q30 */
    kNew = c->resS;                                                      /* Q24 */
    g2 = ju_mulq(gNew, gNew, 30);
    g4 = ju_mulq(g2, g2, 30);
    den = JU_ONE24 + ju_mulq(kNew, g4, 30);                              /* Q24 */
    invNew = (int32_t)(((1u << 31) / (uint32_t)(den >> 9)) << 14);       /* 1 / (1 + k g^4), Q30 */
    compNew = JU_ONE24 + ju_mulq(kNew, 358, 10);                           /* 1 + 0.35 k */

    /* HPF: one pole, no resonance. 6 Hz at 0, then 16 Hz * 2^(7.6439 hpf / 255) */
    if (c->hpfS < (1 << 15)) hpNew = ju_g_of((int32_t)(2.585 * 65536.0));
    else hpNew = ju_g_of((4 << 16) + (int32_t)(((int64_t)c->hpfS * 1965) >> 16));

    /* VCA */
    {
        static const int32_t kTrem[4] = {0, JU_C(0.3), JU_C(0.6), JU_C(1.0)};
        trem = 65536 - ju_mulq(kTrem[P[P_VCA_LFO] & 3], (32768 - (lfo >> 9)), 16);
        vcaNew = ju_mulq(v->env2 >> 6, c->lvlS, 16);                     /* Q24 */
        vcaNew = ju_mulq(vcaNew, trem, 16);
    }

    /* pulse width: the high part, a fraction of the cycle */
    pwv = c->pwS;                                                        /* Q16 0..1 */
    switch (P[P_VM_PWMODE]) {
    case PW_MAN: pwNew = (uint32_t)(((int64_t)(32768 - ju_mulq(pwv, 29491, 16)) << 16)); break;           /* 0.5 - 0.45 pw */
    case PW_LFO: pwNew = (uint32_t)((int64_t)(32768 - ju_mulq(ju_mulq(pwv, 29491, 16), 32768 + (lfo >> 9), 16)) << 16); break;
    default: pwNew = (uint32_t)((int64_t)(32768 - ju_mulq(ju_mulq(pwv, 29491, 16), ju_clamp(v->env1 >> 14, 0, 65536), 16)) << 16); break;
    }

    if (v->fresh) {                                                      /* a voice starting from silence: no ramp from old values */
        v->fresh = 0;
        v->inc1Prev = inc1; v->inc2Prev = inc2; v->gPrev = gNew; v->invPrev = invNew;
        v->kPrev = kNew; v->compPrev = compNew; v->hpPrev = hpNew; v->pwPrev = pwNew;
    }

    {
        const int w1 = (const int[4]){JW_TRI, JW_SAW, JW_PULSE, JW_PULSE}[P[P_V1_WAVE] & 3];
        const int w2 = (const int[4]){JW_SINE, JW_SAW, JW_PULSE, JW_NOISE}[P[P_V2_WAVE] & 3];
        const int sq1 = (P[P_V1_WAVE] & 3) == V1_SQUARE;
        const int sync = P[P_V2_SYNC] != 0 && w2 != JW_NOISE;
        const int32_t xm = c->xmS;
        const int xmOn = xm > 33;                                        /* 0.0005 */
        const int32_t mix2 = c->mixS, mix1 = 65536 - mix2;
        const int tap2 = P[P_VCF_SLOPE] == 0;
        int32_t dI1 = ((int32_t)inc1 - (int32_t)v->inc1Prev) >> 5, dI2 = ((int32_t)inc2 - (int32_t)v->inc2Prev) >> 5;
        int32_t dG = (gNew - v->gPrev) >> 5, dInv = (invNew - v->invPrev) >> 5, dK = (kNew - v->kPrev) >> 5;
        int32_t dC = (compNew - v->compPrev) >> 5, dH = (hpNew - v->hpPrev) >> 5, dV = (vcaNew - v->vcaPrev) >> 5;
        uint32_t dP = (uint32_t)((int32_t)(pwNew - v->pwPrev) >> 5);      /* unsigned: 50 % is 2^31 */
        int32_t i1 = (int32_t)v->inc1Prev, i2 = (int32_t)v->inc2Prev, G = v->gPrev, inv = v->invPrev, K = v->kPrev;
        int32_t comp = v->compPrev, hpG = v->hpPrev, vca = v->vcaPrev;
        uint32_t pw = v->pwPrev;
        int32_t o2Last = v->o2Last, z1 = v->z[0], z2 = v->z[1], z3 = v->z[2], z4 = v->z[3], hpz = v->hpz;
        int32_t Gd, dGd;
        uint32_t ph1 = v->ph1, ph2 = v->ph2;
        int32_t nxt2 = v->nxt2;
        /* G / drv with drv = 0.5625 (9 / 16): the OTA stage's input is scaled before the tanh and the output back */
        Gd = (int32_t)(((int64_t)G * 58254) >> 16);                      /* G x 8/9: Q30 / 0.5625 as Q29 */
        dGd = ((int32_t)(((int64_t)gNew * 58254) >> 16) - Gd) >> 5;

        for (n = 0; n < JU_BLOCK; n++) {
            int32_t s1, s2, x, hv, hl, S, u, y, y2 = 0, y4 = 0, vv;
            uint32_t incm = (uint32_t)i1, old;
            int wrapped;
            i1 += dI1; i2 += dI2; G += dG; Gd += dGd; inv += dInv; K += dK; comp += dC;
            hpG += dH; vca += dV; pw += dP;

            /* VCO-1, with cross mod from VCO-2's last output */
            incm = (uint32_t)i1;
            if (xmOn) {
                uint64_t m = ((uint64_t)incm * ju_exp2_q16(ju_mulq(xm, o2Last, 24))) >> 16;
                incm = m > JU_MAXINC ? JU_MAXINC : (uint32_t)m;
            }
            old = ph1; ph1 += incm; wrapped = ph1 < old;
            s1 = ju_wave(w1, ph1, incm, sq1 ? 0x80000000u : (uint32_t)pw, 1);

            /* VCO-2, hard-synced to VCO-1; its output is one sample late so the sync step can be corrected */
            if (w2 == JW_NOISE) {
                s2 = ju_mulq(ju_rand_bip24(&v->rng), 179, 8);            /* 0.7 */
                ph2 += (uint32_t)i2;
            } else if (sync && wrapped) {
                uint32_t p = ph1, d = incm, sq15, ph2pre;
                int32_t before, after, dd, thisb, nextb;
                if (d == 0) d = 1;
                while (!(d & 0x80000000u)) { d <<= 1; p <<= 1; }
                sq15 = (p >> 1) / (d >> 16);                             /* time since the wrap, Q15 of a sample */
                if (sq15 > 32768u) sq15 = 32768u;
                ph2pre = ph2 + (uint32_t)(((uint64_t)(uint32_t)i2 * (32768u - sq15)) >> 15);
                before = ju_wave(w2, ph2pre, (uint32_t)i2, (uint32_t)pw, 0);
                after = ju_wave(w2, 0, (uint32_t)i2, (uint32_t)pw, 0);
                dd = after - before;
                thisb = (int32_t)(((int64_t)sq15 * sq15) >> 16);         /* 0.5 t^2, Q15 */
                nextb = -(int32_t)((((int64_t)(32768 - sq15) * (32768 - sq15))) >> 16);
                s2 = nxt2 + (int32_t)(((int64_t)dd * thisb) >> 15);
                ph2 = (uint32_t)(((uint64_t)(uint32_t)i2 * sq15) >> 15);
                nxt2 = ju_wave(w2, ph2, (uint32_t)i2, (uint32_t)pw, 0) + (int32_t)(((int64_t)dd * nextb) >> 15);
                goto s2done;
            } else {
                ph2 += (uint32_t)i2;
            }
            if (w2 != JW_NOISE) {
                s2 = nxt2;
                nxt2 = ju_wave(w2, ph2, (uint32_t)i2, (uint32_t)pw, 1);
            }
        s2done:
            o2Last = s2;

            /* mixer into the HPF (one-pole TPT high-pass); the pulses carry no DC */
            x = (int32_t)(((int64_t)s1 * mix1 + (int64_t)s2 * mix2) >> 16);
            hv = ju_mulq(x - hpz, hpG, 30);
            hl = hv + hpz;
            hpz = hl + hv;
            x = (x - hl) >> 1;

            /* VCF: four OTA integrators in series, zero-delay feedback from stage 4 */
            S = ju_mulq((1 << 30) - G, z4 + ju_mulq(G, z3 + ju_mulq(G, z2 + ju_mulq(G, z1, 30), 30), 30), 30);
            u = ju_mulq((int32_t)(((int64_t)x * comp - (int64_t)K * S) >> 24), inv, 30);
            vv = ju_mulq(Gd, ju_sat(JU_DRV(u - z1)), 29); y = vv + z1; z1 = y + vv;
            vv = ju_mulq(Gd, ju_sat(JU_DRV(y - z2)), 29); y = vv + z2; z2 = y + vv; y2 = y;
            vv = ju_mulq(Gd, ju_sat(JU_DRV(y - z3)), 29); y = vv + z3; z3 = y + vv;
            vv = ju_mulq(Gd, ju_sat(JU_DRV(y - z4)), 29); y = vv + z4; z4 = y + vv; y4 = y;
            out[n] += (int32_t)(((int64_t)(tap2 ? y2 : y4) * vca) >> 23);     /* x 2: the makeup gain */
        }
        v->ph1 = ph1; v->ph2 = ph2; v->nxt2 = nxt2;
        v->o2Last = o2Last;
        v->z[0] = z1; v->z[1] = z2; v->z[2] = z3; v->z[3] = z4; v->hpz = hpz;
    }
    v->inc1Prev = inc1; v->inc2Prev = inc2; v->gPrev = gNew; v->invPrev = invNew;
    v->kPrev = kNew; v->compPrev = compNew; v->hpPrev = hpNew; v->vcaPrev = vcaNew; v->pwPrev = pwNew;
}

/* Render the active voices and ADD into the buffers (Q24, JU_BLOCK long) */
void ju_render(int32_t *upper, int32_t *lower)
{
    int i;
    for (i = 0; i < JU_NV; i++) {
        ju_voice_t *v = &ju_v[i];
        if (!v->active) continue;
        ju_render_voice(v, v->layer == 0 ? upper : lower);
    }
}

/* Master level, balance and a soft clip: Q24 buffers -> interleaved stereo Q15 (L, R). Mono as the original. */
void ju_finish(const int32_t *upper, const int32_t *lower, int32_t *out)
{
    const int32_t vol = (int32_t)ju_global[G_VOLUME] * 257;                 /* Q16 */
    const int32_t gu = ju_mulq(ju_mulq(vol, (int32_t)ju_global[G_BAL_U] * 257, 16), 39322, 16);   /* x 0.6 */
    const int32_t gl = ju_mulq(ju_mulq(vol, (int32_t)ju_global[G_BAL_L] * 257, 16), 39322, 16);
    const ju_fxpatch_t *f = &ju_patch[0].fx;
    int i;
    const int anyFx = f->mfxOn || f->dlyOn || f->revOn || ju_fx_tail > 0;
    if (f->mfxOn || f->dlyOn || f->revOn) ju_fx_tail = (int32_t)(45.0 * 44100.0 / JU_BLOCK);   /* the effects finish ringing out and clear themselves */
    else if (ju_fx_tail > 0) ju_fx_tail--;
    ju_fx_run = (uint8_t)anyFx;
    if (anyFx) {                                       /* the mix is mono into the effects, which make the stereo */
        int32_t mono[JU_BLOCK], L[JU_BLOCK], R[JU_BLOCK];
        for (i = 0; i < JU_BLOCK; i++) mono[i] = ju_mulq(upper[i], gu, 16) + ju_mulq(lower[i], gl, 16);
        ju_fx_reverb_block();
        ju_fx_process(mono, L, R);
        for (i = 0; i < JU_BLOCK; i++) { out[2 * i] = ju_sat(L[i]) >> 9; out[2 * i + 1] = ju_sat(R[i]) >> 9; }
        return;
    }
    {
        const int split = ju_stereo_layers && ju_global[G_KEYMODE] != KM_WHOLE;
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t uu = ju_mulq(upper[i], gu, 16), dd = ju_mulq(lower[i], gl, 16);
            if (split) {                                /* Lower left, Upper right */
                out[2 * i] = ju_sat(dd) >> 9;
                out[2 * i + 1] = ju_sat(uu) >> 9;
            } else {
                int32_t m = ju_sat(uu + dd) >> 9;
                out[2 * i] = m;
                out[2 * i + 1] = m;
            }
        }
    }
}

int ju_voice_active(int i) { return ju_v[i].active; }
int ju_voice_note(int i) { return ju_v[i].note; }
void ju_set_phase_seed(void)                         /* the host test: the float engine's starting phases */
{
    int i;
    for (i = 0; i < JU_NV; i++) {
        ju_v[i].ph1 = (uint32_t)i * (uint32_t)(0.137 * 4294967296.0);
        ju_v[i].ph2 = (uint32_t)i * (uint32_t)(0.291 * 4294967296.0);
        ju_v[i].rng = 74565u * (uint32_t)(i + 1) + 77u;       /* the float engine's noise seed: the same noise */
    }
}

/* ENV-2 level of a voice, Q30 (0 when it is not sounding): the panel's voice LEDs */
int32_t ju_voice_level(int i) { return ju_v[i].active ? ju_v[i].env2 : 0; }
int ju_voice_gate(int i) { return ju_v[i].gate; }

/* Overload: fade one voice out. The quietest released voice goes first, then the oldest held one. */
void ju_shed_voice(void)
{
    int i, best = -1;
    for (i = 0; i < JU_NV; i++)
        if (ju_v[i].active && !ju_v[i].gate && (best < 0 || ju_v[i].env2 < ju_v[best].env2)) best = i;
    if (best < 0)
        for (i = 0; i < JU_NV; i++)
            if (ju_v[i].active && ju_v[i].gate && (best < 0 || ju_v[i].age < ju_v[best].age)) best = i;
    if (best >= 0) { ju_v[best].gate = 0; ju_v[best].st1 = ju_v[best].st2 = 3; ju_v[best].env2 >>= 2; }
}
