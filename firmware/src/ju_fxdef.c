/* SPDX-License-Identifier: GPL-3.0-only */
/* Effect parameter model: names, ranges, defaults and display of every CHORUS & MFX type, the delay and the reverb,
 * after jupiter8_esp32/src/dsp/fx_params.cpp (itself after Roland's JUPITER-08 manual). Plain C, integers only. */
#include "ju_params.h"

static const char *const kNotes[] = {"64t","1/64","32t","1/32","16t","1/32.","1/16","1/8t","1/16.","1/8","1/4t","1/8.",
                                     "1/4","1/2t","1/4.","1/2","1t","1/2.","1","2t","1.","2"};
static const int32_t kBeats[22] = {2731, 4096, 5461, 8192, 10923, 12288, 16384, 21845, 24576, 32768, 43691, 49152,
                                   65536, 87381, 98304, 131072, 174763, 196608, 262144, 349525, 393216, 524288};
int32_t ju_note_beats_q16(int i) { return kBeats[i < 0 ? 0 : (i > 21 ? 21 : i)]; }

static const char *const kOff[] = {"OFF", "ON"};
static const char *const kDamp[] = {"200 Hz","250 Hz","315 Hz","400 Hz","500 Hz","630 Hz","800 Hz","1000 Hz","1250 Hz","1600 Hz",
                                    "2000 Hz","2500 Hz","3150 Hz","4000 Hz","5000 Hz","6300 Hz","8000 Hz","BYPASS"};
static const char *const kC1Mode[] = {"I", "II", "I+II", "JX 1", "JX 2"};
static const char *const kC3Mode[] = {"1", "2", "3", "4", "1+4", "2+4", "3+4"};
static const char *const kFbMode[] = {"NORMAL", "CROSS"};
static const char *const kPost[] = {"OFF", "LPF", "HPF"};
static const char *const kStages[] = {"4", "8", "12", "16", "20", "24"};
static const char *const kFType[] = {"LPF", "BPF", "HPF", "NOTCH"};
static const char *const kFSlope[] = {"GENTLE", "STEEP", "EXTREME"};
static const char *const kFWave[] = {"TRI", "SQR", "SIN", "SAW UP", "SAW DOWN"};
static const char *const kDlMode[] = {"2 TAP", "PING-PONG"};
static const char *const kRvMode[] = {"STANDARD", "SHIMMER"};
static const char *const kRvType[] = {"ROOM 1", "ROOM 2", "HALL 1", "HALL 2", "PLATE"};

#define NE(n, lo, hi, d) {n, lo, hi, d, FXK_NUM, 0}
#define KD(n, lo, hi, d, kind) {n, lo, hi, d, kind, 0}
#define LB(n, arr, d) {n, 0, (int16_t)(sizeof(arr) / sizeof(arr[0]) - 1), d, FXK_LABELS, arr}
#define SW(n, d) {n, 0, 1, d, FXK_ONOFF, kOff}
#define LO KD("EQ LOW", -15, 15, 0, FXK_DB)
#define HI KD("EQ HIGH", -15, 15, 0, FXK_DB)
#define BAL(d) KD("BALANCE", 0, 100, d, FXK_PCT)
#define LEV NE("LEVEL", 0, 127, 100)
#define FB(d) KD("FEEDBACK", -98, 98, d, FXK_PCT)
#define NOTE(d) LB("NOTE", kNotes, d)

static const ju_fxparam_t kC1[] = {LB("MODE", kC1Mode, 0), NE("NOISE", 0, 127, 15), BAL(50), LEV};
static const ju_fxparam_t kC2[] = {NE("INTENSITY", 0, 127, 100), LO, HI, LEV};
static const ju_fxparam_t kC3[] = {LB("MODE", kC3Mode, 1), LO, HI, LEV};
static const ju_fxparam_t kD1[] = {SW("SYNC", 0), KD("TIME", 1, 1300, 400, FXK_MS), NOTE(12), FB(35), BAL(30), LO, HI, LEV};
static const ju_fxparam_t kD2[] = {SW("SYNC", 0), KD("TIME", 1, 2600, 600, FXK_MS), NOTE(12), FB(35), LB("HIGH DAMP", kDamp, 13),
    KD("PAN 1", -64, 63, -64, FXK_PAN), KD("PAN 2", -64, 63, 63, FXK_PAN), NE("LEVEL 1", 0, 127, 127), NE("LEVEL 2", 0, 127, 127), LO, HI, BAL(30), LEV};
static const ju_fxparam_t kD3[] = {SW("L SYNC", 0), KD("L TIME", 1, 1300, 300, FXK_MS), NOTE(12), SW("R SYNC", 0), KD("R TIME", 1, 1300, 450, FXK_MS), NOTE(12),
    LB("FB MODE", kFbMode, 1), FB(35), LB("HIGH DAMP", kDamp, 14), SW("MOD SYNC", 0), KD("MOD RATE", 5, 1000, 50, FXK_HZ100), NOTE(16),
    NE("MOD DEPTH", 0, 127, 40), KD("MOD PHASE", 0, 180, 180, FXK_DEG), LO, HI, BAL(30), LEV};
static const ju_fxparam_t kRD[] = {SW("SYNC", 0), KD("TIME", 1, 1300, 500, FXK_MS), NOTE(12), FB(30), BAL(40), LO, HI, LEV};
static const ju_fxparam_t kOD[] = {NE("DISTORTION", 0, 127, 60), NE("TONE", 0, 127, 70), LEV};
static const ju_fxparam_t kFZ[] = {NE("DRIVE", 0, 127, 90), NE("TONE", 0, 100, 55), LEV};
static const ju_fxparam_t kFT[] = {KD("ODD", 0, 400, 100, FXK_PCT), KD("EVEN", 0, 400, 100, FXK_PCT), LEV};
static const ju_fxparam_t kBC[] = {NE("RATE", 0, 127, 40), NE("BITS", 0, 20, 8), NE("FILTER", 0, 127, 30), LO, HI, LEV};
static const ju_fxparam_t kLF[] = {NE("COMPRESSOR", 1, 6, 3), NE("LOFI", 1, 9, 4), LB("POST FILTER", kPost, 0), NE("POST FREQ", 1, 16, 10), LO, HI, BAL(100), LEV};
static const ju_fxparam_t kP1[] = {NE("SPEED", 0, 100, 40), NE("DEPTH", 0, 127, 100), LO, HI, LEV};
static const ju_fxparam_t kP2[] = {LB("STAGES", kStages, 2), NE("MANUAL", 0, 127, 64), SW("SYNC", 0), KD("RATE", 5, 1000, 30, FXK_HZ100), NOTE(19),
    NE("DEPTH", 0, 127, 90), NE("RESONANCE", 0, 127, 70), NE("MIX", 0, 127, 100), KD("PAN", -64, 63, 0, FXK_PAN), LO, HI, LEV};
static const ju_fxparam_t kSF[] = {LB("TYPE", kFType, 0), LB("SLOPE", kFSlope, 1), NE("CUTOFF", 0, 127, 70), NE("RESONANCE", 0, 100, 40), KD("GAIN", 0, 12, 0, FXK_DB),
    SW("MOD", 1), LB("WAVE", kFWave, 0), SW("SYNC", 0), KD("RATE", 5, 1000, 50, FXK_HZ100), NOTE(12), NE("DEPTH", 0, 127, 70), NE("ATTACK", 0, 127, 60), LEV};
static const ju_fxparam_t kPS[] = {KD("COARSE", -24, 12, 7, FXK_SEMI), KD("FINE", -100, 100, 0, FXK_CENTS), SW("SYNC", 0), KD("TIME", 1, 1300, 20, FXK_MS), NOTE(9), FB(0), LO, HI, BAL(40), LEV};
static const ju_fxparam_t kPV[] = {KD("1 COARSE", -24, 12, -12, FXK_SEMI), KD("1 FINE", -100, 100, 0, FXK_CENTS), SW("1 SYNC", 0), KD("1 TIME", 1, 1300, 20, FXK_MS), NOTE(9), FB(0), KD("1 PAN", -64, 63, -40, FXK_PAN), NE("1 LEVEL", 0, 127, 110),
    KD("2 COARSE", -24, 12, 12, FXK_SEMI), KD("2 FINE", -100, 100, 0, FXK_CENTS), SW("2 SYNC", 0), KD("2 TIME", 1, 1300, 30, FXK_MS), NOTE(9), FB(0), KD("2 PAN", -64, 63, 40, FXK_PAN), NE("2 LEVEL", 0, 127, 80), LO, HI, BAL(40), LEV};

typedef struct { const char *name; const ju_fxparam_t *p; int n; } ju_fxtab_t;
#define TB(nm, a) {nm, a, (int)(sizeof(a) / sizeof(a[0]))}
static const ju_fxtab_t kTabs[MFX_COUNT] = {
    TB("JUNO-106 Chorus", kC1), TB("CE-1", kC2), TB("SDD-320", kC3), TB("Time Ctrl Delay", kD1), TB("2Tap Pan Delay", kD2),
    TB("Modulation Delay", kD3), TB("Reverse Delay", kRD), TB("T-Scream", kOD), TB("Fuzz", kFZ), TB("Tone Fattener", kFT),
    TB("Bit Crusher", kBC), TB("LOFI Compress", kLF), TB("Script 90", kP1), TB("Multi Stage Phaser", kP2), TB("Super Filter", kSF),
    TB("Pitch Shifter", kPS), TB("2Voice Pitch Shift", kPV),
};

static const ju_fxparam_t kDelay[DL_COUNT] = {
    NE("SEND", 0, 127, 60), LB("MODE", kDlMode, DLM_PING), SW("SYNC", 0), KD("TIME", 1, 1300, 300, FXK_MS), NOTE(9), FB(45), LB("HIGH DAMP", kDamp, 14),
    KD("PAN 1", -64, 63, -64, FXK_PAN), KD("PAN 2", -64, 63, 63, FXK_PAN), NE("LEVEL 1", 0, 127, 110), NE("LEVEL 2", 0, 127, 110), NE("OUTPUT", 0, 127, 100), NE("TO REVERB", 0, 127, 0),
};
static const ju_fxparam_t kReverb[RV_COUNT] = {
    NE("SEND", 0, 127, 50), LB("MODE", kRvMode, RVM_STD),
    LB("TYPE", kRvType, RVT_HALL1), NE("PRE-DELAY", 0, 100, 15), NE("TIME", 1, 100, 50), NE("LEVEL", 0, 127, 100),
    NE("LOW DAMP", -50, 50, 0), NE("HIGH DAMP", -50, 50, 10), KD("TIME", 1, 100, 40, FXK_SEC10), NE("DENSITY", 1, 10, 8), KD("PRE-DELAY", 0, 100, 20, FXK_MS),
    NE("MOD RATE", 0, 100, 30), NE("MOD DEPTH", 0, 100, 25), KD("PITCH 1", -24, 24, 12, FXK_SEMI), KD("PITCH 2", -24, 24, 7, FXK_SEMI),
    KD("FB 1", -98, 98, 35, FXK_PCT), KD("FB 2", -98, 98, 0, FXK_PCT), NE("LEVEL 1", 0, 100, 60), NE("LEVEL 2", 0, 100, 20), NE("LEVEL", 0, 127, 100),
};

const char *ju_mfx_name(int t) { return kTabs[t].name; }
int ju_mfx_nparams(int t) { return kTabs[t].n; }
const ju_fxparam_t *ju_mfx_param(int t, int i) { return &kTabs[t].p[i]; }
const ju_fxparam_t *ju_delay_param(int i) { return &kDelay[i]; }
const ju_fxparam_t *ju_reverb_param(int i) { return &kReverb[i]; }

/* ---- integer formatting (no stdio, no float) */
static char *ju_put_str(char *o, char *end, const char *s)
{
    while (*s && o < end) *o++ = *s++;
    return o;
}
static char *ju_put_int(char *o, char *end, int v, int plus)
{
    char t[12];
    int n = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    if (v < 0 && o < end) *o++ = '-';
    else if (plus && o < end) *o++ = '+';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n && o < end) *o++ = t[--n];
    return o;
}
static char *ju_put_dec(char *o, char *end, int v, int div)             /* v / div with the digits of div (10 or 100) */
{
    int w = v < 0 ? -v : v;
    if (v < 0 && o < end) *o++ = '-';
    o = ju_put_int(o, end, w / div, 0);
    if (o < end) *o++ = '.';
    if (div == 100 && o < end) *o++ = (char)('0' + (w / 10) % 10);
    if (o < end) *o++ = (char)('0' + w % 10);
    return o;
}

void ju_fx_format(const ju_fxparam_t *d, int v, char *out, int n)
{
    char *o = out, *end = out + n - 1;
    switch (d->kind) {
    case FXK_ONOFF:
    case FXK_LABELS: o = ju_put_str(o, end, d->labels[v < d->lo ? d->lo : (v > d->hi ? d->hi : v)]); break;
    case FXK_MS: o = ju_put_str(ju_put_int(o, end, v, 0), end, " ms"); break;
    case FXK_PCT: o = ju_put_str(ju_put_int(o, end, v, 0), end, " %"); break;
    case FXK_DB: o = ju_put_str(ju_put_int(o, end, v, 1), end, " dB"); break;
    case FXK_HZ100: o = ju_put_str(ju_put_dec(o, end, v, 100), end, " Hz"); break;
    case FXK_PAN:
        if (v == 0) o = ju_put_str(o, end, "C");
        else { if (o < end) *o++ = v < 0 ? 'L' : 'R'; o = ju_put_int(o, end, v < 0 ? -v : v, 0); }
        break;
    case FXK_CENTS: o = ju_put_str(ju_put_int(o, end, v, 1), end, " cent"); break;
    case FXK_SEMI: o = ju_put_str(ju_put_int(o, end, v, 1), end, " st"); break;
    case FXK_SEC10: o = ju_put_str(ju_put_dec(o, end, v, 10), end, " s"); break;
    case FXK_DEG: o = ju_put_str(ju_put_int(o, end, v, 0), end, " deg"); break;
    default: o = ju_put_int(o, end, v, 0); break;
    }
    *o = 0;
}

void ju_fx_load_mfx_defaults(ju_fxpatch_t *f, int type)
{
    int i;
    f->mfxType = (uint8_t)type;
    for (i = 0; i < JU_MFX_MAXP; i++) f->m[i] = 0;
    for (i = 0; i < kTabs[type].n; i++) f->m[i] = kTabs[type].p[i].def;
}

void ju_fx_defaults(ju_fxpatch_t *f)
{
    int i;
    unsigned char *b = (unsigned char *)f;
    for (i = 0; i < (int)sizeof *f; i++) b[i] = 0;
    ju_fx_load_mfx_defaults(f, MFX_CHO1);
    for (i = 0; i < DL_COUNT; i++) f->d[i] = kDelay[i].def;
    for (i = 0; i < RV_COUNT; i++) f->r[i] = kReverb[i].def;
}
