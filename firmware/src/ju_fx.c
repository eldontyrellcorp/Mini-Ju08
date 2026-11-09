/* SPDX-License-Identifier: GPL-3.0-only */
/* Effects: CHORUS & MFX (17 types) in series, then a delay and a reverb send. A port of jupiter8_esp32/src/dsp/fx.cpp
 * to integers (the FM-1 has no float): the algorithms, constants and parameter laws are the original's, which are the
 * author's own models of what Roland's manual describes ("estimate" marks the ones with no source).
 *
 * Signals are Q24 (1.0 = 1 << 24) in a block of JU_BLOCK samples; delay memory is 16-bit. One-pole coefficients are Q30,
 * gains Q16, LFOs are 32-bit phase accumulators, delay times are samples in Q8 (reads) or Q16 (gliding times).
 * The memory is smaller than the ESP32's (no PSRAM here): MFX 0.59 s a channel, delay 0.68 s, so the longest settings
 * of the Roland ranges (up to 2.6 s) are cut to what fits, as Line::read did on the board. The reverb runs at half the
 * sample rate, as the original (its line lengths are the original's, tuned at 24 kHz: here the room is 8 % larger). */

#ifndef FX_MFX_LEN
#define FX_MFX_LEN 26000
#define FX_DLY_LEN 30000
#define FX_REV_LEN 29600                        /* HALL 2 and shimmer need 29,560 samples of lines (the ESP32 code allocates 29,000: an overflow there) */
#endif
#ifndef FX_POOL
#define FX_POOL
#endif
static int16_t fx_mfx_mem[2][FX_MFX_LEN] FX_POOL;
static int16_t fx_dly_mem[2][FX_DLY_LEN] FX_POOL;
static int16_t fx_rev_mem[FX_REV_LEN] FX_POOL;

#define FXM(a, b, sh) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> (sh)))
#define FXONE JU_ONE24
#define FXHZ(h) ((uint32_t)((h) * 4294967296.0 / 44100.0))      /* phase increment per sample, from a constant in Hz */
#define FXMS(m) ((int32_t)((m) * 44.1 * 256.0 + 0.5))           /* a time in ms as samples, Q8 */
#define FXQ16(x) ((int32_t)((x) * 65536.0 + 0.5))
#define FXQ24(x) ((int32_t)((x) * 16777216.0 + 0.5))

/* ---- primitives */
static inline int32_t fx_clip(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* one-pole smoothing coefficient 1 - exp(-2 pi f / fs), Q30, f in Hz */
static int32_t fx_a(uint32_t hz)
{
    uint32_t m;
    if (hz > 20000u) hz = 20000u;
    m = hz * 3448u;                                           /* 9.0647203 / 44100 * 2^24 per Hz (exponent of 2) */
    return (1 << 30) - ju_exp2n_q30(m);
}
/* 10^(db/20): 2^(db * 0.1660964) as Q16 */
static int32_t fx_db(int db) { return (int32_t)ju_exp2_q16(db * 10885); }

typedef struct { int16_t *b; int32_t n, w; } fx_line_t;
static inline void fx_push(fx_line_t *l, int32_t x)
{
    x >>= 9;
    l->b[l->w] = (int16_t)(x > 32767 ? 32767 : x < -32767 ? -32767 : x);
    if (++l->w == l->n) l->w = 0;
}
static inline int32_t fx_at(const fx_line_t *l, int32_t d)    /* d samples behind the newest, Q15 */
{
    int32_t i = l->w - 1 - d;
    if (i < 0) i += l->n;
    return l->b[i];
}
static inline int32_t fx_read(const fx_line_t *l, int32_t d_q8)    /* linear interpolation, Q24 out */
{
    int32_t di, f, a, b, mx = (l->n - 3) << 8;
    if (d_q8 < 0) d_q8 = 0;
    if (d_q8 > mx) d_q8 = mx;
    di = d_q8 >> 8; f = d_q8 & 255;
    a = fx_at(l, di); b = fx_at(l, di + 1);
    return (a * 256 + (b - a) * f) * 2;
}

typedef struct { int32_t zl, zh; } fx_tone_t;                 /* low and high shelves from one-pole splits */
static inline int32_t fx_tone(fx_tone_t *t, int32_t x, int32_t lo, int32_t hi, int32_t aL, int32_t aH)
{
    t->zl += FXM(x - t->zl, aL, 30);
    t->zh += FXM(x - t->zh, aH, 30);
    return x + FXM(t->zl, lo - 65536, 16) + FXM(x - t->zh, hi - 65536, 16);
}

static inline int32_t fx_tri(uint32_t ph)                     /* triFrom: -1 at 0, +1 at the half turn, Q24 */
{
    int32_t p = (int32_t)(ph >> 8);
    return p < (1 << 23) ? 4 * p - FXONE : 3 * FXONE - 4 * p;
}
static inline int32_t fx_tri16(uint32_t ph)                   /* 1 - 2 |p - 0.5|: 0 at the ends, 1 in the middle, Q16 */
{
    int32_t p = (int32_t)(ph >> 16);
    return p < 32768 ? p * 2 : 131072 - p * 2;
}
/* cos(2 pi ph), Q24: the original's sinTurn */
static inline int32_t fx_cos(uint32_t ph) { return ju_sine_turn(ph); }
/* pan law: gl = sinTurn(pos / 4), gr = sinTurn((1 - pos) / 4), pos = (pan + 64) / 127 */
static void fx_pan(int pan, int32_t *gl, int32_t *gr)
{
    uint32_t pos = (uint32_t)(((pan + 64) * 65536) / 127);   /* Q16, 0 left .. 1 right */
    *gl = fx_cos(pos << 14);
    *gr = fx_cos((65536u - pos) << 14);
}
static int32_t fx_damp_a(int idx)                              /* feedback high cut; the last value is a bypass */
{
    static const uint16_t t[17] = {200,250,315,400,500,630,800,1000,1250,1600,2000,2500,3150,4000,5000,6300,8000};
    if (idx >= 17) return 1 << 30;
    return fx_a(t[idx < 0 ? 0 : idx]);
}

/* ---- state */
typedef struct { int32_t s1, s2; } fx_svf_t;
typedef struct { uint32_t phase; } fx_head_t;
typedef struct { int32_t off, len, w; } fx_rl_t;
static struct {
    ju_fxpatch_t p;
    int32_t bpm;
    int16_t mrun[JU_MFX_MAXP];
    fx_line_t mL, mR, dL, dR;
    uint32_t rng;
    int curType, clearPos;
    int32_t mfxMix;                                           /* Q16, click-free on/off and type change */
    fx_tone_t toneL, toneR;
    int32_t lp1, lp2, hp1;
    uint32_t lfo1, lfo2;
    int32_t env, hold, holdCnt;
    int32_t timeL, timeR;                                     /* Q16 samples */
    int32_t dampL, dampR;
    int32_t apState[24], apCoef, phFb, modSmooth;
    fx_svf_t svf[4];
    fx_head_t headA, headB;
    uint32_t rPhase;
    /* delay */
    int32_t dlySend, dlyTime, dlyDampL;                       /* send Q16, time Q16 samples */
    int32_t dlyTail, dlyClear;
    int32_t send[JU_BLOCK];
    /* reverb */
    int32_t revMix, revTail;
    int16_t *rv;
    int32_t rvIn[JU_BLOCK], rvOutL[JU_BLOCK], rvOutR[JU_BLOCK];
    int32_t rvLastL, rvLastR;
    fx_rl_t rvPre, rvDiff[4], rvTank[8], rvPs1, rvPs2;
    uint32_t rvLfo;
    int32_t rvDamp1, rvDamp2, rvBw, rvLowA, rvLowB, rvTail1, rvTail2, rvPs1Out, rvPs2Out;
    uint32_t rvPh1, rvPh2;
    int32_t rvTap[12];
    int rvBuilt;
    int32_t rvScale;                                          /* the size, x 100 */
} fx;

static inline int32_t fx_rng(void) { return ju_rand_bip24(&fx.rng); }

static void fx_reset_mfx(void)
{
    int i;
    fx.toneL.zl = fx.toneL.zh = fx.toneR.zl = fx.toneR.zh = 0;
    fx.lp1 = fx.lp2 = fx.hp1 = 0;
    fx.lfo1 = fx.lfo2 = 0;
    fx.env = fx.hold = fx.holdCnt = 0;
    fx.timeL = fx.timeR = 0;
    fx.dampL = fx.dampR = 0;
    for (i = 0; i < 24; i++) fx.apState[i] = 0;
    fx.phFb = 0; fx.modSmooth = 0;
    for (i = 0; i < 4; i++) fx.svf[i].s1 = fx.svf[i].s2 = 0;
    fx.headA.phase = fx.headB.phase = 0;
    fx.rPhase = 0;
    fx.clearPos = 0;                                          /* the memory is wiped a little per block while the effect is muted */
}

static void fx_build_reverb(int32_t size100);

void ju_fx_init(void)
{
    unsigned char *b = (unsigned char *)&fx;
    unsigned i;
    for (i = 0; i < sizeof fx; i++) b[i] = 0;
    for (i = 0; i < FX_MFX_LEN; i++) fx_mfx_mem[0][i] = fx_mfx_mem[1][i] = 0;
    for (i = 0; i < FX_DLY_LEN; i++) fx_dly_mem[0][i] = fx_dly_mem[1][i] = 0;
    for (i = 0; i < FX_REV_LEN; i++) fx_rev_mem[i] = 0;
    fx.mL.b = fx_mfx_mem[0]; fx.mR.b = fx_mfx_mem[1]; fx.mL.n = fx.mR.n = FX_MFX_LEN;
    fx.dL.b = fx_dly_mem[0]; fx.dR.b = fx_dly_mem[1]; fx.dL.n = fx.dR.n = FX_DLY_LEN;
    fx.rv = fx_rev_mem;
    fx.rng = 0x9E3779B9u;
    fx.curType = -1; fx.clearPos = -1; fx.dlyClear = -1;
    fx.rvBuilt = -1;
    ju_fx_defaults(&fx.p);
    fx.bpm = 120;
    fx_build_reverb(100);
}

/* a time as samples, Q8: synced to the tempo (a note value) or in ms */
static int32_t fx_spb_q8;                                    /* samples per beat, Q8 */
static int32_t fx_time_q8(int sync, int ms, int note)
{
    if (sync) return (int32_t)(((int64_t)ju_note_beats_q16(note) * fx_spb_q8) >> 16);
    return ms * FXMS(1.0);
}
/* an LFO rate in mHz: from the tempo (a note value) or Hz x 100 */
static uint32_t fx_rate_mhz(int sync, int hz100, int note)
{
    if (sync) return (uint32_t)((uint32_t)fx.bpm * 1092267u / (uint32_t)ju_note_beats_q16(note));
    return (uint32_t)hz100 * 10u;
}
static inline uint32_t fx_inc_mhz(uint32_t mhz) { return mhz * 97392u / 1000u; }        /* per sample */

void ju_fx_control(const ju_fxpatch_t *p, int bpm)
{
    fx.p = *p;
    fx.bpm = bpm < 40 ? 40 : bpm;
    fx_spb_q8 = (int32_t)(677376000 / fx.bpm);               /* 60 * 44100 * 256 / bpm */
    if (fx.p.mfxType >= MFX_COUNT) fx.p.mfxType = MFX_CHO1;
    if (fx.curType < 0) { fx.curType = fx.p.mfxType; fx_reset_mfx(); }
    if (fx.p.mfxType == fx.curType) {                         /* the running effect keeps its own parameters until it has faded out */
        int i, np = ju_mfx_nparams(fx.curType);
        for (i = 0; i < np; i++) {
            const ju_fxparam_t *d = ju_mfx_param(fx.curType, i);
            fx.mrun[i] = fx.p.m[i] < d->lo ? d->lo : (fx.p.m[i] > d->hi ? d->hi : fx.p.m[i]);
        }
    }
    if (fx.revTail > 0 && !fx.p.revOn) fx.revTail--;
    if (fx.p.revOn) fx.revTail = (int32_t)(40.0 * 44100.0 / JU_BLOCK);
}

/* ---------------------------------------------------------------- CHORUS & MFX */
static const int32_t kSlew30 = (int32_t)(12.0 / 44100.0 * 1073741824.0);   /* a delay time glides: 12 / fs */
#define PCT(v) ((v) * 655)                                      /* v / 100 as Q16 */
#define MIXD(in, w, bal) (FXM((in), 65536 - (bal), 16) + FXM((w), (bal), 16))   /* dry * (1 - bal) + wet * bal */

static const uint32_t kC1Rate[5] = {FXHZ(0.513), FXHZ(0.863), FXHZ(9.75), FXHZ(0.6), FXHZ(0.95)};
static const int32_t kC1Min[5] = {FXMS(1.66), FXMS(1.66), FXMS(3.30), FXMS(2.0), FXMS(2.0)};
static const int32_t kC1Max[5] = {FXMS(5.35), FXMS(5.35), FXMS(3.70), FXMS(6.0), FXMS(7.5)};
static const uint32_t kC3Rate[7] = {FXHZ(0.25), FXHZ(0.25), FXHZ(0.25), FXHZ(0.5), FXHZ(0.5), FXHZ(0.5), FXHZ(0.5)};
static const int32_t kC3Dep[7] = {FXMS(0.20), FXMS(0.35), FXMS(0.55), FXMS(0.75), FXMS(0.95), FXMS(1.1), FXMS(1.3)};
static const int32_t kC3Cross[7] = {FXQ16(0.15), FXQ16(0.25), FXQ16(0.35), FXQ16(0.45), FXQ16(0.5), FXQ16(0.55), FXQ16(0.6)};
static const int32_t kLfPeriod[9] = {FXQ16(1), FXQ16(1.5), FXQ16(2), FXQ16(2.5), FXQ16(3), FXQ16(4), FXQ16(5), FXQ16(6.5), FXQ16(8)};
static const uint8_t kLfBits[9] = {14, 12, 11, 10, 9, 8, 7, 6, 5};

static int32_t fx_hold_quant(int32_t x, int bits)                /* clip and round to 2^bits levels */
{
    int sh = 24 - bits;
    int32_t h;
    x = fx_clip(x, -FXONE, FXONE);
    if (sh <= 0) return x;
    h = 1 << (sh - 1);
    return ((x >= 0 ? x + h : x - h) >> sh) * (1 << sh);
}

/* a pitch shifter head's phase step per sample, in turns of 2^32: (1 - ratio) / window; k = 2^32 / (window x 65536) x 1024 */
static uint32_t fx_ps_inc(int32_t ratio_q16, int32_t k1024)
{
    return (uint32_t)(int32_t)(((int64_t)(65536 - ratio_q16) * k1024) >> 10);
}

/* the pitch shifter's two crossfading read heads */
static int32_t fx_head(fx_head_t *hd, const fx_line_t *l, int32_t base_q8, int32_t win_q8, uint32_t inc)
{
    uint32_t p2;
    int32_t g1, g2, d1, d2;
    hd->phase += inc;
    p2 = hd->phase + 0x80000000u;
    g1 = fx_tri16(hd->phase); g2 = fx_tri16(p2);
    d1 = base_q8 + FXM(hd->phase >> 16, win_q8, 16);
    d2 = base_q8 + FXM(p2 >> 16, win_q8, 16);
    return FXM(fx_read(l, d1), g1, 16) + FXM(fx_read(l, d2), g2, 16);
}

static void fx_run_mfx(const int32_t *in, int32_t *L, int32_t *R)
{
    const int16_t *m = fx.mrun;
    int i;
    switch (fx.curType) {
    case MFX_CHO1: {                                   /* rates and sweep ranges are commonly quoted Juno measurements; not verified */
        int mode = m[C1_MODE];
        uint32_t inc = kC1Rate[mode];
        int32_t c = (kC1Min[mode] + kC1Max[mode]) / 2, dep = (kC1Max[mode] - kC1Min[mode]) / 2;
        int32_t bal = PCT(m[C1_BAL]), noise = m[C1_NOISE] * 119, lev = PCT(m[C1_LEV]), a = fx_a(9000);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t t, n, d, wl, wr, mix;
            fx.lfo1 += inc;
            t = fx_tri(fx.lfo1);
            fx.lp1 += FXM(in[i] - fx.lp1, a, 30);
            fx_push(&fx.mL, fx.lp1);
            n = FXM(fx_rng(), noise, 24);
            d = FXM(dep, t, 24);
            wl = fx_read(&fx.mL, c + d); wr = fx_read(&fx.mL, c - d);
            fx.lp2 += FXM(wl - fx.lp2, a, 30); fx.hp1 += FXM(wr - fx.hp1, a, 30);
            mix = MIXD(in[i], fx.lp2 + n, bal);          /* dry and wet add incoherently: the mix is lifted */
            L[i] = FXM(FXM(mix, lev, 16), FXQ16(1.35), 16);
            mix = MIXD(in[i], fx.hp1 - n, bal);
            R[i] = FXM(FXM(mix, lev, 16), FXQ16(1.35), 16);
        }
        break;
    }
    case MFX_CHO2: {                                   /* one BBD, wet added on one side and subtracted on the other (estimate: 0.8 Hz, 2.5-7 ms) */
        uint32_t inc = FXHZ(0.8);
        int32_t c = FXMS(4.75), dep = FXMS(2.25);
        int32_t wet = m[C2_INT] * 516, lo = fx_db(m[C2_LO]), hi = fx_db(m[C2_HI]), lev = PCT(m[C2_LEV]);
        int32_t a = fx_a(5000), aL = fx_a(200), aH = fx_a(4000);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t w;
            fx.lfo1 += inc;
            fx.lp1 += FXM(in[i] - fx.lp1, a, 30);
            fx_push(&fx.mL, fx.lp1);
            w = fx_read(&fx.mL, c + FXM(dep, fx_cos(fx.lfo1), 24));
            w = FXM(FXM(ju_sat(FXM(w, FXQ16(1.3), 16)), wet, 16), FXQ16(0.8), 16);
            L[i] = FXM(fx_tone(&fx.toneL, in[i] + w, lo, hi, aL, aH), lev, 16);
            R[i] = FXM(fx_tone(&fx.toneR, in[i] - w, lo, hi, aL, aH), lev, 16);
        }
        break;
    }
    case MFX_CHO3: {                                   /* two BBDs swept in opposite directions and cross-mixed: the mode values are estimates */
        int mode = m[C3_MODE];
        uint32_t inc = kC3Rate[mode];
        int32_t dep = kC3Dep[mode], c = FXMS(3.5), x = kC3Cross[mode];
        int32_t lo = fx_db(m[C3_LO]), hi = fx_db(m[C3_HI]), lev = PCT(m[C3_LEV]);
        int32_t a = fx_a(11000), aL = fx_a(200), aH = fx_a(4000);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t t, d, w1, w2, l, r;
            fx.lfo1 += inc;
            t = fx_tri(fx.lfo1);
            fx.lp1 += FXM(in[i] - fx.lp1, a, 30);
            fx_push(&fx.mL, fx.lp1);
            d = FXM(dep, t, 24);
            w1 = fx_read(&fx.mL, c + d); w2 = fx_read(&fx.mL, c - d);
            l = in[i] + FXM(w1, FXQ16(0.6), 16) - FXM(w2, x, 16);
            r = in[i] + FXM(w2, FXQ16(0.6), 16) - FXM(w1, x, 16);
            L[i] = FXM(fx_tone(&fx.toneL, FXM(l, FXQ16(0.8), 16), lo, hi, aL, aH), lev, 16);
            R[i] = FXM(fx_tone(&fx.toneR, FXM(r, FXQ16(0.8), 16), lo, hi, aL, aH), lev, 16);
        }
        break;
    }
    case MFX_DLY1: {
        int32_t target = fx_clip(fx_time_q8(m[D1_SYNC], m[D1_TIME], m[D1_NOTE]), 0, (fx.mL.n - 3) << 8) << 8;
        int32_t fb = PCT(m[D1_FB]), bal = PCT(m[D1_BAL]), lev = PCT(m[D1_LEV]);
        int32_t lo = fx_db(m[D1_LO]), hi = fx_db(m[D1_HI]), aL = fx_a(200), aH = fx_a(4000);
        if (fx.timeL <= 0) fx.timeL = target;
        for (i = 0; i < JU_BLOCK; i++) {               /* the read point glides, so time changes bend the pitch like tape */
            int32_t y, w, o;
            fx.timeL += FXM(target - fx.timeL, kSlew30, 30);
            y = fx_read(&fx.mL, fx.timeL >> 8);
            fx_push(&fx.mL, in[i] + ju_sat(FXM(y, fb, 16)));
            w = fx_tone(&fx.toneL, y, lo, hi, aL, aH);
            o = FXM(MIXD(in[i], w, bal), lev, 16);
            L[i] = R[i] = o;
        }
        break;
    }
    case MFX_DLY2: {                                   /* tap 2 at the set time, tap 1 halfway (halfway is an estimate) */
        int32_t t2 = fx_clip(fx_time_q8(m[D2_SYNC], m[D2_TIME], m[D2_NOTE]), 0, (fx.mL.n - 3) << 8) << 8;
        int32_t fb = PCT(m[D2_FB]), bal = PCT(m[D2_BAL]), lev = PCT(m[D2_LEV]), a = fx_damp_a(m[D2_DAMP]);
        int32_t g1l, g1r, g2l, g2r, v1 = m[D2_LV1] * 516, v2 = m[D2_LV2] * 516;
        int32_t lo = fx_db(m[D2_LO]), hi = fx_db(m[D2_HI]), aL = fx_a(200), aH = fx_a(4000);
        fx_pan(m[D2_PAN1], &g1l, &g1r);
        fx_pan(m[D2_PAN2], &g2l, &g2r);
        if (fx.timeL <= 0) fx.timeL = t2;
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t y1, yraw, y2, wl, wr;
            fx.timeL += FXM(t2 - fx.timeL, kSlew30, 30);
            y1 = FXM(fx_read(&fx.mL, fx.timeL >> 9), v1, 16);
            yraw = fx_read(&fx.mL, fx.timeL >> 8);
            y2 = FXM(yraw, v2, 16);
            fx.dampL += FXM(yraw - fx.dampL, a, 30);
            fx_push(&fx.mL, in[i] + ju_sat(FXM(fx.dampL, fb, 16)));
            wl = fx_tone(&fx.toneL, FXM(y1, g1l, 24) + FXM(y2, g2l, 24), lo, hi, aL, aH);
            wr = fx_tone(&fx.toneR, FXM(y1, g1r, 24) + FXM(y2, g2r, 24), lo, hi, aL, aH);
            L[i] = FXM(MIXD(in[i], wl, bal), lev, 16);
            R[i] = FXM(MIXD(in[i], wr, bal), lev, 16);
        }
        break;
    }
    case MFX_DLY3: {
        int32_t tl = fx_clip(fx_time_q8(m[D3_LSYNC], m[D3_LTIME], m[D3_LNOTE]), 0, (fx.mL.n - 3) << 8) << 8;
        int32_t tr = fx_clip(fx_time_q8(m[D3_RSYNC], m[D3_RTIME], m[D3_RNOTE]), 0, (fx.mL.n - 3) << 8) << 8;
        uint32_t inc = fx_inc_mhz(fx_rate_mhz(m[D3_RSYNC2], m[D3_RATE], m[D3_RNOTE2]));
        int32_t dep = m[D3_DEPTH] * 533;                 /* depth / 127 * 6 ms: an estimate */
        uint32_t ph = (uint32_t)m[D3_PHASE] * 11930465u;
        int32_t fb = PCT(m[D3_FB]), bal = PCT(m[D3_BAL]), lev = PCT(m[D3_LEV]), a = fx_damp_a(m[D3_DAMP]);
        int cross = m[D3_FBMODE] != 0;
        int32_t lo = fx_db(m[D3_LO]), hi = fx_db(m[D3_HI]), aL = fx_a(200), aH = fx_a(4000);
        if (fx.timeL <= 0) { fx.timeL = tl; fx.timeR = tr; }
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t yl, yr, wl, wr;
            uint32_t p2;
            fx.timeL += FXM(tl - fx.timeL, kSlew30, 30); fx.timeR += FXM(tr - fx.timeR, kSlew30, 30);
            fx.lfo1 += inc;
            p2 = fx.lfo1 + ph;
            yl = fx_read(&fx.mL, (fx.timeL >> 8) + FXM(dep, FXONE + fx_cos(fx.lfo1), 24));
            yr = fx_read(&fx.mR, (fx.timeR >> 8) + FXM(dep, FXONE + fx_cos(p2), 24));
            fx.dampL += FXM(yl - fx.dampL, a, 30); fx.dampR += FXM(yr - fx.dampR, a, 30);
            fx_push(&fx.mL, in[i] + ju_sat(FXM(cross ? fx.dampR : fx.dampL, fb, 16)));
            fx_push(&fx.mR, in[i] + ju_sat(FXM(cross ? fx.dampL : fx.dampR, fb, 16)));
            wl = fx_tone(&fx.toneL, yl, lo, hi, aL, aH); wr = fx_tone(&fx.toneR, yr, lo, hi, aL, aH);
            L[i] = FXM(MIXD(in[i], wl, bal), lev, 16);
            R[i] = FXM(MIXD(in[i], wr, bal), lev, 16);
        }
        break;
    }
    case MFX_RDLY: {                                   /* two reversed heads, each playing a slice backward with a triangular fade */
        int32_t T = fx_time_q8(m[RD_SYNC], m[RD_TIME], m[RD_NOTE]);
        uint32_t inc;
        int32_t fb = PCT(m[RD_FB]), bal = PCT(m[RD_BAL]), lev = PCT(m[RD_LEV]);
        int32_t lo = fx_db(m[RD_LO]), hi = fx_db(m[RD_HI]), aL = fx_a(200), aH = fx_a(4000);
        T = fx_clip(T, 64 << 8, (int32_t)((int64_t)fx.mL.n * 48 / 100) << 8);
        inc = 0xFFFFFFFFu / (uint32_t)(T >> 8);
        for (i = 0; i < JU_BLOCK; i++) {
            uint32_t p2;
            int32_t g1, g2, y, w, o;
            fx.rPhase += inc;
            p2 = fx.rPhase + 0x80000000u;
            g1 = fx_tri16(fx.rPhase); g2 = fx_tri16(p2);
            y = FXM(fx_read(&fx.mL, FXM(fx.rPhase >> 16, T, 15)), g1, 16) + FXM(fx_read(&fx.mL, FXM(p2 >> 16, T, 15)), g2, 16);
            fx_push(&fx.mL, in[i] + ju_sat(FXM(y, fb, 16)));
            w = fx_tone(&fx.toneL, y, lo, hi, aL, aH);
            o = FXM(MIXD(in[i], w, bal), lev, 16);
            L[i] = R[i] = o;
        }
        break;
    }
    case MFX_OD: {
        int32_t drive = 65536 + m[OD_DIST] * 16384;      /* 1 + dist / 4 */
        int32_t aH = fx_a(720), aT = fx_a(800 + 7000 * m[OD_TONE] / 127);
        int32_t lev = FXM(PCT(m[OD_LEV]), 39322 - m[OD_DIST] * 155, 16);       /* x (0.6 - 0.3 dist / 127) */
        for (i = 0; i < JU_BLOCK; i++) {
            int64_t xw;
            int32_t x, y, o;
            fx.hp1 += FXM(in[i] - fx.hp1, aH, 30);
            xw = ((int64_t)(in[i] - fx.hp1) * drive >> 16) + in[i];
            x = (int32_t)(xw > (1 << 30) ? (1 << 30) : xw < -(1 << 30) ? -(1 << 30) : xw);
            y = ju_sat(x + FXQ24(0.1)) - FXQ24(0.0997);  /* slight asymmetry: even harmonics */
            fx.lp1 += FXM(y - fx.lp1, aT, 30);
            o = FXM(fx.lp1, lev, 16);
            L[i] = R[i] = o;
        }
        break;
    }
    case MFX_FUZZ: {
        int32_t drive = 131072 + m[FZ_DRIVE] * 52429;    /* 2 + drive * 0.8 */
        int32_t aT = fx_a(500 + 80 * m[FZ_TONE]);
        int32_t lev = FXM(PCT(m[FZ_LEV]), FXQ16(0.3), 16);
        for (i = 0; i < JU_BLOCK; i++) {
            int64_t xw = (int64_t)in[i] * drive >> 16;
            int32_t x = (int32_t)(xw > (1 << 30) ? (1 << 30) : xw < -(1 << 30) ? -(1 << 30) : xw), y, s, o;
            y = FXM(fx_clip(x + FXQ24(0.3), -FXQ24(0.6), FXONE), FXQ16(1.25), 16) - FXQ24(0.25);   /* hard, lopsided clip */
            s = ju_sat((int32_t)(xw / 2 > (1 << 30) ? (1 << 30) : xw / 2 < -(1 << 30) ? -(1 << 30) : xw / 2));
            y += FXM(s < 0 ? -s : s, FXQ16(0.25), 16);   /* rectified octave content */
            fx.lp1 += FXM(y - fx.lp1, aT, 30);
            fx.hp1 += FXM(fx.lp1 - fx.hp1, 2147484, 30);  /* 0.002 */
            o = FXM(fx.lp1 - fx.hp1, lev, 16);
            L[i] = R[i] = o;
        }
        break;
    }
    case MFX_FATTEN: {
        int32_t odd = FXM(PCT(m[FT_ODD]), FXQ16(0.25), 16), even = FXM(PCT(m[FT_EVEN]), FXQ16(0.3), 16), lev = PCT(m[FT_LEV]);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t x = fx_clip(in[i] * 3, -FXONE, FXONE), x2 = FXM(x, x, 24), y;
            int32_t shape = FXM(x, FXM(x2, 3 * FXONE - 2 * x2, 24), 24);       /* x^3 (3 - 2 x^2) / 1 */
            y = in[i] + FXM(shape, odd, 16) + FXM(x2 - fx.env, even, 16);
            fx.env += FXM(x2 - fx.env, 1073742, 30);      /* 0.001: keeps the even-harmonic DC out */
            y = ju_sat(y);
            L[i] = R[i] = FXM(y, lev, 16);
        }
        break;
    }
    case MFX_BITC: {                                   /* RATE 0 = full rate, 127 = about 700 Hz. BITS 20 = clean. Both directions are the original's reading */
        int32_t period = (int32_t)ju_exp2_q16(m[BC_RATE] * 3148);
        int bits = m[BC_BIT];
        int32_t aF = fx_a((uint32_t)(((uint64_t)16000u * ju_exp2_q16(-(m[BC_FILT] * 2576))) >> 16));    /* 16 kHz x 2^(-filt / 127 x 5) */
        int32_t lo = fx_db(m[BC_LO]), hi = fx_db(m[BC_HI]), lev = PCT(m[BC_LEV]), aL = fx_a(200), aH = fx_a(4000);
        for (i = 0; i < JU_BLOCK; i++) {
            fx.holdCnt += 65536;
            if (fx.holdCnt >= period) { fx.holdCnt -= period; fx.hold = fx_hold_quant(in[i], bits); }
            fx.lp1 += FXM(fx.hold - fx.lp1, aF, 30);
            L[i] = R[i] = FXM(fx_tone(&fx.toneL, fx.lp1, lo, hi, aL, aH), lev, 16);
        }
        break;
    }
    case MFX_LOFI: {                                   /* compressor, then degrade, then post filter, in the manual's order; the tables are estimates */
        int li = fx_clip(m[LF_LOFI], 1, 9) - 1;
        int32_t period = kLfPeriod[li];
        int bits = kLfBits[li];
        int32_t ratio = 65536 / m[LF_COMP];
        int32_t bal = PCT(m[LF_BAL]), lev = PCT(m[LF_LEV]);
        int32_t lo = fx_db(m[LF_LO]), hi = fx_db(m[LF_HI]), aL = fx_a(200), aH = fx_a(4000);
        int32_t aF = fx_a((uint32_t)(((uint64_t)150u * ju_exp2_q16((m[LF_COFF] - 1) * 27525)) >> 16));
        int post = m[LF_FILT];
        int32_t aAtk = fx_a(100), aRel = fx_a(6);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t x = in[i], y, w, o;
            if (m[LF_COMP] > 1) {
                int32_t a = x < 0 ? -x : x, over, den, g;
                fx.env += FXM(a - fx.env, a > fx.env ? aAtk : aRel, 30);
                over = fx.env > FXQ24(0.2) ? (int32_t)(((int64_t)fx.env * 5) >> 8) : 65536;     /* env / 0.2, Q16 */
                den = 65536 + FXM(over - 65536, 65536 - ratio, 16);
                g = (int32_t)(0xFFFFFFFFu / (uint32_t)den);                                      /* 1 / den, Q16 */
                x = FXM(x, FXM(g, 65536 + FXM(65536 - ratio, 98304, 16), 16), 16);              /* x g (1 + (1 - ratio) 1.5) */
            }
            fx.holdCnt += 65536;
            if (fx.holdCnt >= period) { fx.holdCnt -= period; fx.hold = fx_hold_quant(x, bits); }
            y = fx.hold;
            if (post) {
                fx.lp1 += FXM(y - fx.lp1, aF, 30);
                y = post == 1 ? fx.lp1 : y - fx.lp1;
            }
            w = fx_tone(&fx.toneL, y, lo, hi, aL, aH);
            o = FXM(MIXD(in[i], w, bal), lev, 16);
            L[i] = R[i] = o;
        }
        break;
    }
    case MFX_PHA1:
    case MFX_PHA2: {
        int multi = fx.curType == MFX_PHA2;
        int stages = multi ? (m[P2_STAGES] + 1) * 4 : 4;
        uint32_t mhz;
        int32_t depth, centerLg, reso, mix, lev, lo, hi, gl = FXONE, gr = FXONE, lg, G, target, dA, aL, aH, wetG, dryG;
        if (multi) {
            mhz = fx_rate_mhz(m[P2_SYNC], m[P2_RATE], m[P2_NOTE]);
            depth = m[P2_DEPTH] * 1290;                  /* octaves either way: an estimate */
            centerLg = 435412 + m[P2_MANUAL] * 3045;     /* log2(100 Hz * 2^(manual / 127 * 5.9)) */
            reso = m[P2_RESO] * 464;
            mix = m[P2_MIX] * 516;
            lo = fx_db(m[P2_LO]); hi = fx_db(m[P2_HI]);
            lev = PCT(m[P2_LEV]);
            if (m[P2_PAN] != 0) {
                fx_pan(m[P2_PAN], &gl, &gr);
                gl = FXM(gl, FXQ16(1.414), 16); gr = FXM(gr, FXQ16(1.414), 16);
            }
        } else {
            mhz = (uint32_t)(((uint64_t)50u * ju_exp2_q16(m[P1_SPEED] * 4830)) >> 16);   /* 0.05 to 8 Hz: an estimate */
            depth = m[P1_DEPTH] * 929;
            centerLg = 619395;                           /* log2(700) */
            reso = FXQ16(0.15);
            mix = 65536;
            lo = fx_db(m[P1_LO]); hi = fx_db(m[P1_HI]);
            lev = PCT(m[P1_LEV]);
        }
        fx.lfo1 += fx_inc_mhz(mhz) * JU_BLOCK;
        lg = centerLg + FXM(depth, fx_cos(fx.lfo1), 24);
        lg = fx_clip(lg, 321578, 935560);                /* 30 Hz .. 0.45 fs */
        G = ju_g_of(lg);
        target = 2 * (G >> 0) - (1 << 30);               /* (t - 1) / (t + 1) = 2 g - 1 with g = t / (1 + t) */
        dA = (target - fx.apCoef) / JU_BLOCK;
        aL = fx_a(200); aH = fx_a(4000);
        wetG = FXM(mix, FXQ16(0.7), 16); dryG = FXQ16(0.7);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t x, o;
            int s;
            fx.apCoef += dA;
            x = in[i] + FXM(fx.phFb, reso, 16);
            for (s = 0; s < stages; s++) {
                int32_t y = FXM(fx.apCoef, x, 30) + fx.apState[s];
                fx.apState[s] = x - FXM(fx.apCoef, y, 30);
                x = y;
            }
            fx.phFb = ju_sat(x);
            o = FXM(fx_tone(&fx.toneL, FXM(in[i], dryG, 16) + FXM(x, wetG, 16), lo, hi, aL, aH), lev, 16);
            L[i] = FXM(o, gl, 24); R[i] = FXM(o, gr, 24);
        }
        break;
    }
    case MFX_FILT: {
        uint32_t mhz = fx_rate_mhz(m[SF_SYNC], m[SF_RATE], m[SF_NOTE]);
        int32_t w, slew, oct, lg, G, t, q, kLast, kFlat = FXQ16(1.414), gain, a1[4], a2[4], a3[4], kk[4];
        int stages, type = m[SF_TYPE], s;
        static const int kSt[3] = {1, 2, 4};              /* 12, 24, 48 dB/oct: an estimate */
        fx.lfo1 += fx_inc_mhz(mhz) * JU_BLOCK;
        switch (m[SF_WAVE]) {
        case 0: w = fx_tri(fx.lfo1); break;
        case 1: w = fx.lfo1 < 0x80000000u ? FXONE : -FXONE; break;
        case 2: w = fx_cos(fx.lfo1); break;
        case 3: w = 2 * (int32_t)(fx.lfo1 >> 8) - FXONE; break;
        default: w = FXONE - 2 * (int32_t)(fx.lfo1 >> 8); break;
        }
        /* ATTACK slows the jumps of square and saw: a higher value is faster (the original's reading) */
        slew = (m[SF_WAVE] == 0 || m[SF_WAVE] == 2) ? 65536 : 655 + m[SF_ATK] * 511;
        fx.modSmooth += FXM(w - fx.modSmooth, slew, 16);
        oct = m[SF_MOD] ? FXM(fx.modSmooth, m[SF_DEPTH] * 2064, 24) : 0;
        lg = 283242 + m[SF_CUT] * 5145 + oct;           /* 20 Hz * 2^(cut / 127 * 9.97 + oct) */
        lg = fx_clip(lg, 283242, 935560);
        G = ju_g_of(lg);
        {   /* t = tan(pi fc / fs) = G / (1 - G), Q16 */
            uint32_t g16 = (uint32_t)G >> 14;
            t = (int32_t)((g16 * 65536u) / (65536u - g16 + 1u));
        }
        q = 39322 + m[SF_RES] * 7864;                    /* 0.6 + res * 0.12 */
        kLast = (int32_t)(0xFFFFFFFFu / (uint32_t)q);
        stages = kSt[m[SF_SLOPE] % 3];
        gain = FXM(fx_db(m[SF_GAIN]), PCT(m[SF_LEV]), 16);
        for (s = 0; s < stages; s++) {
            int32_t k = s == stages - 1 ? kLast : kFlat;
            uint32_t den = 65536u + (uint32_t)FXM(t, t + k, 16);
            kk[s] = k;
            a1[s] = (int32_t)(0xFFFFFFFFu / den);
            a2[s] = FXM(t, a1[s], 16);
            a3[s] = FXM(t, a2[s], 16);
        }
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t x = in[i], o;
            for (s = 0; s < stages; s++) {
                fx_svf_t *f = &fx.svf[s];
                int32_t k = kk[s], v3 = x - f->s2;
                int32_t v1 = FXM(a1[s], f->s1, 16) + FXM(a2[s], v3, 16);
                int32_t v2 = f->s2 + FXM(a2[s], f->s1, 16) + FXM(a3[s], v3, 16);
                f->s1 = 2 * v1 - f->s1;
                f->s2 = 2 * v2 - f->s2;
                switch (type) {
                case 0: x = v2; break;
                case 1: x = FXM(v1, k, 16); break;
                case 2: x = x - FXM(v1, k, 16) - v2; break;
                default: x = x - FXM(v1, k, 16); break;
                }
            }
            o = ju_sat(FXM(x, gain, 16));
            L[i] = R[i] = o;
        }
        break;
    }
    case MFX_PCH1: {
        int32_t semis100 = m[PS_CRS] * 100 + m[PS_FINE];
        int32_t ratio = (int32_t)ju_exp2_q16((semis100 * 16384 / 300));
        const int32_t win = 2205 << 8;                   /* 50 ms grains: an estimate */
        uint32_t inc = fx_ps_inc(ratio, 30436);
        int32_t base = fx_time_q8(m[PS_SYNC], m[PS_TIME], m[PS_NOTE]);
        int32_t fb = PCT(m[PS_FB]), bal = PCT(m[PS_BAL]), lev = PCT(m[PS_LEV]);
        int32_t lo = fx_db(m[PS_LO]), hi = fx_db(m[PS_HI]), aL = fx_a(200), aH = fx_a(4000);
        base = fx_clip(base, 0, (fx.mL.n - 3 - 2205) << 8);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t w, o;
            fx_push(&fx.mL, in[i] + ju_sat(FXM(fx.lp1, fb, 16)));
            fx.lp1 = fx_head(&fx.headA, &fx.mL, base, win, inc);
            w = fx_tone(&fx.toneL, fx.lp1, lo, hi, aL, aH);
            o = FXM(MIXD(in[i], w, bal), lev, 16);
            L[i] = R[i] = o;
        }
        break;
    }
    case MFX_PCH2: {
        const int32_t win = 2205 << 8;
        uint32_t i1 = fx_ps_inc((int32_t)ju_exp2_q16(((m[V2_1CRS] * 100 + m[V2_1FIN]) * 16384 / 300)), 30436);
        uint32_t i2 = fx_ps_inc((int32_t)ju_exp2_q16(((m[V2_2CRS] * 100 + m[V2_2FIN]) * 16384 / 300)), 30436);
        int32_t b1 = fx_clip(fx_time_q8(m[V2_1SYNC], m[V2_1TIME], m[V2_1NOTE]), 0, (fx.mL.n - 3 - 2205) << 8);
        int32_t b2 = fx_clip(fx_time_q8(m[V2_2SYNC], m[V2_2TIME], m[V2_2NOTE]), 0, (fx.mL.n - 3 - 2205) << 8);
        int32_t f1 = PCT(m[V2_1FB]), f2 = PCT(m[V2_2FB]), l1 = m[V2_1LEV] * 516, l2 = m[V2_2LEV] * 516;
        int32_t g1l, g1r, g2l, g2r, bal = PCT(m[V2_BAL]), lev = PCT(m[V2_LEV]);
        int32_t lo = fx_db(m[V2_LO]), hi = fx_db(m[V2_HI]), aL = fx_a(200), aH = fx_a(4000);
        fx_pan(m[V2_1PAN], &g1l, &g1r);
        fx_pan(m[V2_2PAN], &g2l, &g2r);
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t a, b, wl, wr;
            fx_push(&fx.mL, in[i] + ju_sat(FXM(fx.lp1, f1, 16)));
            fx_push(&fx.mR, in[i] + ju_sat(FXM(fx.lp2, f2, 16)));
            fx.lp1 = fx_head(&fx.headA, &fx.mL, b1, win, i1);
            fx.lp2 = fx_head(&fx.headB, &fx.mR, b2, win, i2);
            a = FXM(fx.lp1, l1, 16); b = FXM(fx.lp2, l2, 16);
            wl = fx_tone(&fx.toneL, FXM(a, g1l, 24) + FXM(b, g2l, 24), lo, hi, aL, aH);
            wr = fx_tone(&fx.toneR, FXM(a, g1r, 24) + FXM(b, g2r, 24), lo, hi, aL, aH);
            L[i] = FXM(MIXD(in[i], wl, bal), lev, 16);
            R[i] = FXM(MIXD(in[i], wr, bal), lev, 16);
        }
        break;
    }
    default:
        for (i = 0; i < JU_BLOCK; i++) { L[i] = in[i]; R[i] = in[i]; }
        break;
    }
}

/* ---------------------------------------------------------------- reverb */
/* A plate-style tank (a figure eight of allpasses and delays, after Dattorro 1997) at half the audio rate. Room, hall
 * and plate differ by size, damping and input diffusion; all the values are the original's estimates. */
typedef struct { int32_t size100, k, damp, diff; } fx_rvtype_t;       /* size x 100; 1.6308 size / rtMax (Q16); damp, diff (Q16) */
static const fx_rvtype_t kRvTypes[5] = {
    {40, FXQ16(1.6308 * 0.40 / 1.5), FXQ16(0.45), FXQ16(0.60)},       /* ROOM 1 */
    {55, FXQ16(1.6308 * 0.55 / 2.5), FXQ16(0.35), FXQ16(0.65)},       /* ROOM 2 */
    {85, FXQ16(1.6308 * 0.85 / 5.0), FXQ16(0.30), FXQ16(0.72)},       /* HALL 1 */
    {115, FXQ16(1.6308 * 1.15 / 9.0), FXQ16(0.22), FXQ16(0.75)},      /* HALL 2 */
    {75, FXQ16(1.6308 * 0.75 / 6.0), FXQ16(0.08), FXQ16(0.75)},       /* PLATE */
};
static const int16_t kRvDiff[4] = {115, 86, 305, 223};                /* at 24 kHz */
static const int16_t kRvTank[8] = {542, 3590, 1450, 2998, 732, 3398, 2141, 2549};
static const uint16_t kRvTapFrac[12] = {FXQ16(0.09), FXQ16(0.93), FXQ16(0.61), FXQ16(0.60), FXQ16(0.60), FXQ16(0.06),
                                        FXQ16(0.12), FXQ16(0.92), FXQ16(0.41), FXQ16(0.67), FXQ16(0.66), FXQ16(0.09)};

static void fx_build_reverb(int32_t size100)
{
    int32_t off = 0;
    int i, k;
    fx_rl_t *tl[12];
    const uint8_t tankOf[12] = {5, 5, 6, 7, 1, 2, 1, 1, 2, 3, 5, 6};
    if (!fx.rv) return;
#define RV_ADD(l, n) do { (l).off = off; (l).len = (n) < 4 ? 4 : (n); (l).w = 0; off += (l).len; } while (0)
    RV_ADD(fx.rvPre, 2500);                                 /* 100 ms at 24 kHz */
    RV_ADD(fx.rvPs1, 3000);
    RV_ADD(fx.rvPs2, 3000);
    for (i = 0; i < 4; i++) RV_ADD(fx.rvDiff[i], kRvDiff[i] + 1);
    for (i = 0; i < 8; i++) RV_ADD(fx.rvTank[i], kRvTank[i] * size100 / 100 + 40);
    fx.rvScale = size100;
    if (off > FX_REV_LEN) { fx.rv = 0; return; }             /* no memory: the reverb stays silent */
    for (i = 0; i < FX_REV_LEN; i++) fx.rv[i] = 0;
    fx.rvTail1 = fx.rvTail2 = fx.rvDamp1 = fx.rvDamp2 = fx.rvBw = fx.rvLowA = fx.rvLowB = 0;
    for (k = 0; k < 12; k++) {
        tl[k] = &fx.rvTank[tankOf[k]];
        fx.rvTap[k] = (int32_t)(((int64_t)kRvTapFrac[k] * (tl[k]->len - 40)) >> 16) + 1;
    }
}
static inline int32_t rv_rd(const fx_rl_t *l, int32_t d)
{
    int32_t i = l->w - d;
    if (i < 0) i += l->len;
    return (int32_t)fx.rv[l->off + i] * 512;
}
static inline void rv_wr(fx_rl_t *l, int32_t x)
{
    x >>= 9;
    fx.rv[l->off + l->w] = (int16_t)(x > 32767 ? 32767 : x < -32767 ? -32767 : x);
    if (++l->w == l->len) l->w = 0;
}
/* one of the modulated allpass loops: the delay swings by mo (Q8 samples) */
static int32_t rv_mod_allpass(fx_rl_t *l, int32_t a, int32_t mo)
{
    int32_t dl = ((l->len - 20) << 8) + mo, di = dl >> 8, f = dl & 255;
    int32_t z = (int32_t)(((int64_t)rv_rd(l, di) * (256 - f) + (int64_t)rv_rd(l, di + 1) * f) >> 8);
    int32_t v = a + FXM(z, FXQ16(0.7), 16);
    rv_wr(l, v);
    return z - FXM(v, FXQ16(0.7), 16);
}
static int32_t rv_allpass(fx_rl_t *l, int32_t a, int32_t g)       /* g Q16 */
{
    int32_t z = rv_rd(l, l->len - 1), v = a - FXM(g, z, 16);
    rv_wr(l, v);
    return z + FXM(g, v, 16);
}

void ju_fx_reverb_block(void)
{
    const int16_t *r = fx.p.r;
    int shim = r[RV_MODE] == RVM_SHIMMER, n;
    const fx_rvtype_t *ty = &kRvTypes[(r[RV_TYPE] >= 0 && r[RV_TYPE] < 5) ? r[RV_TYPE] : 0];
    int32_t wantSize = shim ? 115 : ty->size100;
    int32_t e, decay, damp, lowDamp = 0, diff, predelay, outLev, modDepth, bw = (int32_t)(0.85 * 1073741824.0);
    uint32_t lfoInc;
    int32_t psWin = 1102, fb1 = 0, fb2 = 0, lv1 = 0, lv2 = 0;
    uint32_t psInc1 = 0, psInc2 = 0;
    int i;
    if (fx.revTail == 0 || !fx.rv) {
        for (i = 0; i < JU_BLOCK; i++) fx.rvOutL[i] = fx.rvOutR[i] = 0;
        return;
    }
    if (fx.rvBuilt != wantSize) { fx.rvBuilt = wantSize; fx_build_reverb(wantSize); }     /* a new size means new line lengths: the tail clears */
    if (!fx.rv) return;
    if (shim) {
        e = (int32_t)(1229130 / (r[RS_TIME] < 1 ? 1 : r[RS_TIME]));                       /* 1.6308 x 1.15 / (time x 0.1 s), Q16 */
        damp = fx_clip(FXQ16(0.25) + r[RS_HDAMP] * 786, 0, FXQ16(0.9));
        lowDamp = fx_clip(r[RS_LDAMP] * 786 + FXQ16(0.1), 0, FXQ16(0.8));
        diff = FXQ16(0.35) + 2621 * r[RS_DENS];
        predelay = (r[RS_PDLY] * 22050 + 500) / 1000;
        outLev = PCT(r[RS_LEV]);
        lfoInc = (uint32_t)(((100u + (uint32_t)r[RS_MRATE] * 50u) * 194783u) / 1000u);      /* 0.1 + rate x 0.05 Hz, per half-rate sample */
        modDepth = r[RS_MDEPTH] * 64;                                                        /* x 0.25 samples, Q8 */
    } else {                                           /* TIME 1-100 is an exponential law up to the type's maximum (estimate) */
        e = (int32_t)(((int64_t)ty->k * ju_exp2_q16((100 - r[RV_TIME]) * 3277)) >> 16);
        damp = ty->damp;
        diff = ty->diff;
        predelay = (r[RV_PDLY] * 22050 + 500) / 1000;
        outLev = PCT(r[RV_LEV]);
        lfoInc = (uint32_t)(800u * 194783u / 1000u);
        modDepth = 6 << 8;
    }
    {   /* rt never below 0.05 s: the exponent has a ceiling */
        int32_t emax = (int32_t)((int64_t)FXQ16(1.6308) * fx.rvScale / 100 * 20);
        if (e > emax) e = emax;
    }
    {   /* the loop gain aims longer than the set time: damping shortens the broadband decay (kRtFit, 2.2, is in k) */
        int64_t m = (int64_t)e * fx.rvScale / 100 * 0 + e;
        decay = ju_exp2n_q30((uint32_t)m << 8);
        if (decay > (int32_t)(0.9995 * 1073741824.0)) decay = (int32_t)(0.9995 * 1073741824.0);
    }
    if (shim) {
        psInc1 = fx_ps_inc((int32_t)ju_exp2_q16((r[RS_PCH1] * 65536) / 12), 60871);
        psInc2 = fx_ps_inc((int32_t)ju_exp2_q16((r[RS_PCH2] * 65536) / 12), 60871);
        fb1 = PCT(r[RS_FB1]); fb2 = PCT(r[RS_FB2]); lv1 = PCT(r[RS_LV1]); lv2 = PCT(r[RS_LV2]);
    }
    {
        int32_t damp30 = (int32_t)(((int64_t)(65536 - damp) << 14));     /* 1 - damp as a Q30 coefficient */
        int32_t d1[4];
        d1[0] = d1[1] = diff; d1[2] = d1[3] = FXM(diff, FXQ16(0.85), 16);
        for (n = 0, i = 0; n < JU_BLOCK / 2; n++, i += 2) {
            int32_t x = (fx.rvIn[i] + fx.rvIn[i + 1]) >> 1, in, y, mo, a, b, tailA, tailB, oL, oR, k;
            if (shim) x += ju_sat(FXM(fx.rvPs1Out, fb1, 16));
            rv_wr(&fx.rvPre, x);
            in = rv_rd(&fx.rvPre, predelay + 1);
            fx.rvBw += FXM(in - fx.rvBw, bw, 30);
            y = fx.rvBw;
            for (k = 0; k < 4; k++) y = rv_allpass(&fx.rvDiff[k], y, d1[k]);     /* input diffusion */
            fx.rvLfo += lfoInc;
            mo = FXM(fx_cos(fx.rvLfo), modDepth, 24);

            a = y + FXM(fx.rvTail2, decay, 30);                                    /* left half */
            a = rv_mod_allpass(&fx.rvTank[0], a, mo);
            rv_wr(&fx.rvTank[1], a);
            a = rv_rd(&fx.rvTank[1], fx.rvTank[1].len - 1);
            fx.rvDamp1 += FXM(a - fx.rvDamp1, damp30, 30);
            a = fx.rvDamp1;
            if (lowDamp > 0) { fx.rvLowA += FXM(a - fx.rvLowA, 10737418, 30); a -= FXM(fx.rvLowA, lowDamp, 16); }
            a = FXM(a, decay, 30);
            a = rv_allpass(&fx.rvTank[2], a, FXQ16(0.5));
            rv_wr(&fx.rvTank[3], a);
            tailA = rv_rd(&fx.rvTank[3], fx.rvTank[3].len - 1);

            b = y + FXM(fx.rvTail1, decay, 30);                                    /* right half */
            b = rv_mod_allpass(&fx.rvTank[4], b, -mo);
            rv_wr(&fx.rvTank[5], b);
            b = rv_rd(&fx.rvTank[5], fx.rvTank[5].len - 1);
            fx.rvDamp2 += FXM(b - fx.rvDamp2, damp30, 30);
            b = fx.rvDamp2;
            if (lowDamp > 0) { fx.rvLowB += FXM(b - fx.rvLowB, 10737418, 30); b -= FXM(fx.rvLowB, lowDamp, 16); }
            b = FXM(b, decay, 30);
            b = rv_allpass(&fx.rvTank[6], b, FXQ16(0.5));
            rv_wr(&fx.rvTank[7], b);
            tailB = rv_rd(&fx.rvTank[7], fx.rvTank[7].len - 1);

            fx.rvTail1 = ju_sat(tailA);
            fx.rvTail2 = ju_sat(tailB);

            /* stereo outputs: taps at fractions of each tank line (Dattorro's table, scaled) */
#define TAP(k, t) rv_rd(&fx.rvTank[t], fx.rvTap[k])
            oL = FXM(TAP(0, 5) + TAP(1, 5) - TAP(2, 6) + TAP(3, 7) - TAP(4, 1) - TAP(5, 2), FXQ16(0.6), 16);
            oR = FXM(TAP(6, 1) + TAP(7, 1) - TAP(8, 2) + TAP(9, 3) - TAP(10, 5) - TAP(11, 6), FXQ16(0.6), 16);
            if (shim) {
                int32_t mono = (oL + oR) >> 1, add, o1, o2, g1, g2, ph, p2;
                rv_wr(&fx.rvPs1, mono);
                rv_wr(&fx.rvPs2, mono + ju_sat(FXM(fx.rvPs2Out, fb2, 16)));
                fx.rvPh1 += psInc1;
                ph = (int32_t)(fx.rvPh1 >> 16); p2 = (int32_t)((fx.rvPh1 + 0x80000000u) >> 16);
                g1 = fx_tri16(fx.rvPh1); g2 = fx_tri16(fx.rvPh1 + 0x80000000u);
                o1 = FXM(rv_rd(&fx.rvPs1, FXM(ph, psWin, 16) + 2), g1, 16) + FXM(rv_rd(&fx.rvPs1, FXM(p2, psWin, 16) + 2), g2, 16);
                fx.rvPs1Out = o1;
                fx.rvPh2 += psInc2;
                ph = (int32_t)(fx.rvPh2 >> 16); p2 = (int32_t)((fx.rvPh2 + 0x80000000u) >> 16);
                g1 = fx_tri16(fx.rvPh2); g2 = fx_tri16(fx.rvPh2 + 0x80000000u);
                o2 = FXM(rv_rd(&fx.rvPs2, FXM(ph, psWin, 16) + 2), g1, 16) + FXM(rv_rd(&fx.rvPs2, FXM(p2, psWin, 16) + 2), g2, 16);
                fx.rvPs2Out = o2;
                add = FXM(o1, lv1, 16) + FXM(o2, lv2, 16);
                oL += add; oR += add;
            }
            oL = FXM(oL, outLev, 16); oR = FXM(oR, outLev, 16);
            fx.rvOutL[i] = (fx.rvLastL + oL) >> 1; fx.rvOutL[i + 1] = oL;          /* back up to the full rate */
            fx.rvOutR[i] = (fx.rvLastR + oR) >> 1; fx.rvOutR[i + 1] = oR;
            fx.rvLastL = oL; fx.rvLastR = oR;
        }
    }
}

/* ---------------------------------------------------------------- top level */
void ju_fx_process(const int32_t *in, int32_t *L, int32_t *R)
{
    int typeChange = fx.p.mfxType != fx.curType, i;
    int wanted = fx.p.mfxOn && !typeChange && fx.clearPos < 0;
    int32_t pre[JU_BLOCK], dlyRv[JU_BLOCK], target, g, dg;
    const int16_t *d = fx.p.d;
    int32_t rvFromDelay = 0;
    int dlyRun;

    /* CHORUS & MFX: fades out before a type change or switch-off, then wipes its memory a little per block so old audio
     * never replays, then fades back in if wanted */
    if (fx.clearPos >= 0) {
        int32_t n = fx.mL.n - fx.clearPos, k;
        if (n > 2048) n = 2048;
        for (k = 0; k < n; k++) { fx.mL.b[fx.clearPos + k] = 0; fx.mR.b[fx.clearPos + k] = 0; }
        fx.clearPos += n;
        if (fx.clearPos >= fx.mL.n) fx.clearPos = -1;
    }
    if (fx.mfxMix > 0 || wanted) {
        int32_t gg = fx.mfxMix, next = fx_clip(fx.mfxMix + (wanted ? 3277 : -3277), 0, 65536), dgg = (next - gg) / JU_BLOCK;
        fx_run_mfx(in, L, R);
        for (i = 0; i < JU_BLOCK; i++) {
            gg += dgg;
            L[i] = in[i] + FXM(L[i] - in[i], gg, 16);
            R[i] = in[i] + FXM(R[i] - in[i], gg, 16);
        }
        fx.mfxMix = next;
        if (fx.mfxMix == 0 && !wanted) {
            if (typeChange) fx.curType = fx.p.mfxType;
            fx_reset_mfx();
        }
    } else {
        if (typeChange) { fx.curType = fx.p.mfxType; fx_reset_mfx(); }
        for (i = 0; i < JU_BLOCK; i++) { L[i] = in[i]; R[i] = in[i]; }
    }
    for (i = 0; i < JU_BLOCK; i++) pre[i] = (L[i] + R[i]) >> 1;

    /* delay: switching off stops the send; the echoes ring out, then the memory is wiped */
    if (fx.p.dlyOn) {
        if (fx.dlyClear < 0) fx.dlyTail = (int32_t)(30.0 * 44100.0 / JU_BLOCK);
    } else if (fx.dlyTail > 0) {
        if (--fx.dlyTail == 0) fx.dlyClear = 0;
    }
    if (fx.dlyClear >= 0) {
        int32_t n = fx.dL.n - fx.dlyClear, k;
        if (n > 2048) n = 2048;
        for (k = 0; k < n; k++) { fx.dL.b[fx.dlyClear + k] = 0; fx.dR.b[fx.dlyClear + k] = 0; }
        fx.dlyClear += n;
        if (fx.dlyClear >= fx.dL.n) { fx.dlyClear = -1; fx.dlyDampL = 0; fx.dlyTime = 0; }
    }
    dlyRun = fx.dlyTail > 0 && fx.dlyClear < 0;
    if (dlyRun) {
        int32_t T = fx_clip(fx_time_q8(d[DL_SYNC], d[DL_TIME], d[DL_NOTE]), 0, (fx.dL.n - 3) << 8) << 8;
        int32_t fb = PCT(d[DL_FB]), a = fx_damp_a(d[DL_DAMP]), g1l, g1r, g2l, g2r;
        int32_t v1 = d[DL_LV1] * 516, v2 = d[DL_LV2] * 516, lev = PCT(d[DL_LEV]);
        int ping = d[DL_MODE] == DLM_PING;
        int32_t sg, dsg;
        target = fx.p.dlyOn ? d[DL_SEND] * 516 : 0;
        fx_pan(d[DL_PAN1], &g1l, &g1r);
        fx_pan(d[DL_PAN2], &g2l, &g2r);
        rvFromDelay = d[DL_RSEND] * 516;
        if (fx.dlyTime <= 0) fx.dlyTime = T;
        sg = fx.dlySend; dsg = (target - sg) / JU_BLOCK;
        for (i = 0; i < JU_BLOCK; i++) {
            int32_t x, t1, t2, w1, w2, wl, wr;
            sg += dsg;
            fx.dlyTime += FXM(T - fx.dlyTime, kSlew30, 30);
            x = FXM(pre[i], sg, 16);
            if (ping) {                                /* one bounce per TIME: the input enters left, each echo crosses to the other side */
                t1 = fx_read(&fx.dL, fx.dlyTime >> 8);
                t2 = fx_read(&fx.dR, fx.dlyTime >> 8);
                fx.dlyDampL += FXM(t2 - fx.dlyDampL, a, 30);
                fx_push(&fx.dL, x + ju_sat(FXM(fx.dlyDampL, fb, 16)));
                fx_push(&fx.dR, t1);
            } else {                                   /* second tap at TIME, the first halfway (an estimate) */
                t1 = fx_read(&fx.dL, fx.dlyTime >> 9);
                t2 = fx_read(&fx.dL, fx.dlyTime >> 8);
                fx.dlyDampL += FXM(t2 - fx.dlyDampL, a, 30);
                fx_push(&fx.dL, x + ju_sat(FXM(fx.dlyDampL, fb, 16)));
            }
            w1 = FXM(t1, v1, 16); w2 = FXM(t2, v2, 16);
            wl = FXM(FXM(w1, g1l, 24) + FXM(w2, g2l, 24), lev, 16);
            wr = FXM(FXM(w1, g1r, 24) + FXM(w2, g2r, 24), lev, 16);
            L[i] += wl; R[i] += wr;
            dlyRv[i] = (wl + wr) >> 1;
        }
        fx.dlySend = target;
    }

    /* reverb: the send for the next block and this block's return. Switching off stops the send only. */
    target = fx.p.revOn ? fx.p.r[RV_SEND] * 516 : 0;
    g = fx.revMix; dg = (target - g) / JU_BLOCK;
    for (i = 0; i < JU_BLOCK; i++) {
        int32_t s = pre[i];
        g += dg;
        if (dlyRun) s += FXM(dlyRv[i], rvFromDelay, 16);
        fx.rvIn[i] = FXM(FXM(s, g, 16), 32768, 16);
        L[i] += fx.rvOutL[i];
        R[i] += fx.rvOutR[i];
    }
    fx.revMix = target;
}

int ju_fx_active(void) { return fx.p.mfxOn || fx.p.dlyOn || fx.p.revOn; }
