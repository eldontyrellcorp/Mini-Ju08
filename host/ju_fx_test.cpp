// The fixed-point effects (firmware/src/ju_fx.c, inside ju_dsp.c) against the float Fx of jupiter8_esp32: every CHORUS & MFX
// type, the delay (2-tap and ping-pong) and the reverb (all types and shimmer), the same input block by block. Reports the
// level of each and the waveform error.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "dsp/engine.h"
extern "C" {
#include <stdint.h>
#include "ju_params.h"
void ju_fx_init(void); void ju_fx_control(const ju_fxpatch_t*, int); void ju_fx_reverb_block(void); void ju_fx_process(const int32_t*, int32_t*, int32_t*);
void ju_fx_defaults(ju_fxpatch_t*); void ju_fx_load_mfx_defaults(ju_fxpatch_t*, int);
}
static const int kFs = 44100, kLenM = 26000, kLenD = 30000, kLenR = 29600;

struct Res { double rmsRef, rmsFx, snr; };
// scen: 0 chord + noise bursts, 1 impulse
static Res run(j8::FxPatch& rp, int bpm, double seconds, int scen, int measure = 200) {   // measure: blocks compared after the fade-in
    j8::Fx* ref = new j8::Fx();      // a fresh one: init() does not reset the read heads, the fade or the type
    auto a = [](int n) { return (int16_t*)calloc((size_t)n, 2); };
    ref->init((float)kFs, a(kLenM), a(kLenM), kLenM, a(kLenD), a(kLenD), kLenD, a(kLenR), kLenR);
    ju_fx_init();
    ju_fxpatch_t jp;
    jp.mfxType = rp.mfxType; jp.mfxOn = rp.mfxOn; jp.dlyOn = rp.dlyOn; jp.revOn = rp.revOn;
    memcpy(jp.m, rp.m, sizeof jp.m); memcpy(jp.d, rp.d, sizeof jp.d); memcpy(jp.r, rp.r, sizeof jp.r);
    const int blocks = (int)(seconds * kFs / 32);
    double eR = 0, eF = 0, eD = 0;
    uint32_t rs = 12345;
    int64_t n = 0;
    for (int b = 0; b < blocks; b++) {
        float in[32], L[32], R[32];
        int32_t qi[32], ql[32], qr[32];
        for (int i = 0; i < 32; i++, n++) {
            float x;
            if (scen == 1) x = n == 4000 ? 0.4f : 0.f;
            else {
                rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
                float env = (n % 22050) < 4000 ? 1.f : 0.3f;
                x = env * (0.25f * sinf(6.2831853f * 330.f * n / kFs) + 0.12f * sinf(6.2831853f * 1760.5f * n / kFs) + 0.05f * ((int32_t)rs / 2147483648.f));
            }
            in[i] = x; qi[i] = (int32_t)lrintf(x * 16777216.f);
        }
        ref->control(rp, (float)bpm); ref->reverbBlock(); ref->process(in, L, R);
        ju_fx_control(&jp, bpm); ju_fx_reverb_block(); ju_fx_process(qi, ql, qr);
        if (getenv("FXDUMP") && b >= 150 && b < 152) for (int i = 0; i < 32; i++) printf("  n=%d ref %+.5f fix %+.5f   (in %+.5f)\n", b * 32 + i, L[i], ql[i] / 16777216.0, in[i]);
        if (getenv("FXTRACE") && b >= 100 && b < 700 && b % 20 == 0) {
            double e1 = 0, e2 = 0, e3 = 0;
            for (int i = 0; i < 32; i++) { double r1 = L[i], f1 = ql[i] / 16777216.0; e1 += r1 * r1; e2 += f1 * f1; e3 += (r1 - f1) * (r1 - f1); }
            printf("  block %d: ref %.4f fix %.4f err %.4f\n", b, sqrt(e1 / 32), sqrt(e2 / 32), sqrt(e3 / 32));
        }
        if (b < 100) continue;                      // fades in
        if (b >= 100 + measure) continue;           // a long run drifts apart in phase from tiny rounding differences (pitch shifters, sample-and-hold): compare the start
        for (int i = 0; i < 32; i++) {
            double r1 = L[i], f1 = ql[i] / 16777216.0, r2 = R[i], f2 = qr[i] / 16777216.0;
            eR += r1 * r1 + r2 * r2; eF += f1 * f1 + f2 * f2; eD += (r1 - f1) * (r1 - f1) + (r2 - f2) * (r2 - f2);
        }
    }
    double cnt = (double)(measure < blocks - 100 ? measure : blocks - 100) * 64;
    return {sqrt(eR / cnt), sqrt(eF / cnt), eD > 0 ? 10 * log10(eR / eD) : 99};
}

int main(int argc, char** argv) {
    j8::initTables();
    if (argc > 1) {      // ./ju_fx_test <mfx type>: one effect, with its samples dumped (FXDUMP=1)
        j8::FxPatch p; j8::fxDefaults(p); j8::fxLoadMfxDefaults(p, atoi(argv[1])); p.mfxOn = 1;
        Res r = run(p, 120, 3.0, 0); printf("%s: ref %.5f fix %.5f snr %.1f\n", j8::mfxName(atoi(argv[1])), r.rmsRef, r.rmsFx, r.snr); return 0;
    }
    int fails = 0;
    double minSnr = 99;
    printf("%-22s %9s %9s %8s %8s\n", "effect", "rms ref", "rms fix", "dB diff", "SNR dB");
    auto report = [&](const char* name, Res r, double minOk, double dbOk) {
        double db = (r.rmsRef > 1e-9 && r.rmsFx > 1e-9) ? 20 * log10(r.rmsFx / r.rmsRef) : (r.rmsRef < 1e-9 && r.rmsFx < 1e-9 ? 0 : -99);
        bool bad = r.snr < minOk || fabs(db) > dbOk;
        printf("%-22s %9.5f %9.5f %+8.2f %8.1f %s\n", name, r.rmsRef, r.rmsFx, db, r.snr, bad ? " <-- FAIL" : "");
        if (bad) fails++;
        if (r.snr < minSnr) minSnr = r.snr;
    };
    for (int t = 0; t < j8::MFX_COUNT; t++) {
        j8::FxPatch p; j8::fxDefaults(p); j8::fxLoadMfxDefaults(p, t); p.mfxOn = 1;
        if (t == j8::MFX_DLY2) p.m[j8::D2_TIME] = 300;      // the default 600 ms is longer than this firmware's MFX memory: the reference does not clamp the first tap
        report(j8::mfxName(t), run(p, 120, 3.0, 0), 15.0, 1.0);
    }
    { j8::FxPatch p; j8::fxDefaults(p); p.dlyOn = 1; p.d[j8::DL_MODE] = j8::DLM_PING; report("delay ping-pong", run(p, 120, 3.0, 1, 600), 12.0, 1.0); }
    { j8::FxPatch p; j8::fxDefaults(p); p.dlyOn = 1; p.d[j8::DL_MODE] = j8::DLM_2TAP; p.d[j8::DL_SYNC] = 1; report("delay 2-tap synced", run(p, 90, 3.0, 1, 600), 15.0, 1.0); }
    const char* tn[5] = {"reverb room 1", "reverb room 2", "reverb hall 1", "reverb hall 2", "reverb plate"};
    for (int t = 0; t < 5; t++) { j8::FxPatch p; j8::fxDefaults(p); p.revOn = 1; p.r[j8::RV_TYPE] = t; report(tn[t], run(p, 120, 3.0, 0, 800), 12.0, 1.0); }
    { j8::FxPatch p; j8::fxDefaults(p); p.revOn = 1; p.r[j8::RV_MODE] = j8::RVM_SHIMMER; report("reverb shimmer", run(p, 120, 3.0, 0, 800), 10.0, 1.5); }
    { j8::FxPatch p; j8::fxDefaults(p); j8::fxLoadMfxDefaults(p, j8::MFX_CHO1); p.mfxOn = p.dlyOn = p.revOn = 1; report("all three", run(p, 120, 3.0, 0, 800), 12.0, 1.0); }
    printf("worst SNR %.1f dB, %d failing\n", minSnr, fails);
    return fails ? 1 : 0;
}
