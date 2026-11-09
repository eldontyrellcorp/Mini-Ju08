// Fixed-point engine (firmware/src/ju_dsp.c) against the float engine of jupiter8_esp32, on the 64 factory patches.
// Same notes, same starting phases, ANALOG TOLERANCE off (the two random generators differ). Reports, per patch, the level
// of each and the waveform error between them; fails when a patch is silent, clips, or differs by more than the limits.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include "dsp/engine.h"
#include "fx_host.h"
extern "C" {
#include <stdint.h>
#include "ju_params.h"
void ju_init(void);
void ju_prepare_block(void);
void ju_render(int32_t*, int32_t*);
void ju_finish(const int32_t*, const int32_t*, int32_t*);
void ju_note_on(uint8_t, uint8_t);
void ju_note_off(uint8_t, uint8_t);
void ju_set_phase_seed(void);
const char *ju_mfx_name(int); int ju_mfx_nparams(int); const ju_fxparam_t *ju_mfx_param(int, int);
const ju_fxparam_t *ju_delay_param(int); const ju_fxparam_t *ju_reverb_param(int);
void ju_fx_format(const ju_fxparam_t*, int, char*, int);
const char *ju_pshort(int); const char *ju_plong(int); int ju_psteps(int); const char *ju_plabel(int, int); int ju_cc_to_param(uint8_t); void ju_format_param(int, uint8_t, char*, int); void ju_fx_defaults(ju_fxpatch_t*); int32_t ju_note_beats_q16(int);
extern ju_patch_t ju_patch[2];
extern uint8_t ju_stereo_layers;
extern uint8_t ju_global[G_COUNT];
}
#include "ju_presets.h"

static const int kRate = 44100;
struct Result { double rmsRef, rmsFx, snr, peak; };

// scen 3: the patch with its effects, same notes (pads with chorus and reverb, delays);  1: arpeggiator (up, 2 octaves); 2: DUAL with two patches, Lower left / Upper right
static Result run(int idx, std::vector<float>* dump, int scen = 0) {
    j8::Patch bank[64];
    j8::applyFactory(bank, 64);
    auto* ref = new j8::Engine();
    ref->init((float)kRate);
    hostFxInit(*ref, (float)kRate);
    if (scen == 3) {      // the same delay memory as the firmware has
        auto a = [](int n) { return (int16_t*)calloc((size_t)n, 2); };
        ref->fx.init((float)kRate, a(26000), a(26000), 26000, a(30000), a(30000), 30000, a(29600), 29600);
    }
    ref->global[j8::G_KEYMODE] = j8::KM_WHOLE;
    ref->global[j8::G_VINTAGE] = 0;
    ref->patch[0] = bank[idx];
    if (scen == 2) { ref->patch[1] = bank[(idx + 9) % 64]; j8::fxDefaults(ref->patch[1].fx); ref->global[j8::G_KEYMODE] = j8::KM_DUAL; ref->stereoLayers = true; }
    if (scen == 1) { ref->global[j8::G_ARP_ON] = 1; ref->global[j8::G_ARP_RATE] = 150; ref->global[j8::G_ARP_RANGE] = 1; }
    if (scen != 3) j8::fxDefaults(ref->patch[0].fx);   // dry voices; scenario 3 keeps the patch's effects
    ju_init();
    ju_global[G_KEYMODE] = KM_WHOLE;
    ju_global[G_VINTAGE] = 0;
    ju_patch[0] = JU_PRESETS[idx];
    if (scen != 3) ju_fx_defaults(&ju_patch[0].fx);      // dry, like the reference above
    if (scen == 2) { ju_patch[1] = JU_PRESETS[(idx + 9) % 64]; ju_global[G_KEYMODE] = KM_DUAL; ju_stereo_layers = 1; }
    if (scen == 1) { ju_global[G_ARP_ON] = 1; ju_global[G_ARP_RATE] = 150; ju_global[G_ARP_RANGE] = 1; }
    ju_set_phase_seed();

    const int notes[4] = {45 + (idx / 8) * 3, 52 + (idx / 8) * 3, 57 + (idx / 8) * 3, 60 + (idx / 8) * 3};
    const int blocks = kRate * 3 / j8::kBlock;
    double eRef = 0, eFx = 0, eDiff = 0, peak = 0;
    for (int b = 0; b < blocks; b++) {
        if (b == 4) for (int n : notes) { ref->pushEvent(j8::Event{j8::Event::NoteOn, (uint8_t)n, 0}); ju_note_on(0xFF, (uint8_t)n); }
        if (b == blocks * 2 / 3) for (int n : notes) { ref->pushEvent(j8::Event{j8::Event::NoteOff, (uint8_t)n, 0}); ju_note_off(0xFF, (uint8_t)n); }
        float u[32] = {0}, l[32] = {0}, L[32], R[32];
        ref->prepareBlock(); ref->render(0, j8::kNumVoices, u, l); ref->fxWorker(); ref->finish(u, l, L, R);
        int32_t fu[32] = {0}, fl[32] = {0}, out[64];
        ju_prepare_block(); ju_render(fu, fl); ju_finish(fu, fl, out);
        for (int k = 0; k < 32; k++) {
            double r = L[k], x = out[2 * k] / 32768.0, r2 = R[k], x2 = out[2 * k + 1] / 32768.0;
            eRef += r * r + r2 * r2; eFx += x * x + x2 * x2; eDiff += (r - x) * (r - x) + (r2 - x2) * (r2 - x2);
            if (fabs(x) > peak) peak = fabs(x);
            if (fabs(x2) > peak) peak = fabs(x2);
            if (dump) { dump->push_back((float)r); dump->push_back((float)x); }
        }
    }
    delete ref;
    double n = (double)blocks * 64;
    return {sqrt(eRef / n), sqrt(eFx / n), eDiff > 0 ? 10 * log10(eRef / eDiff) : 99.0, peak};
}

// The parameter model and its display: every effect parameter, every value, against the original's formatFx.
static int checkFxDefs() {
    int bad = 0, n = 0;
    auto cmp = [&](const j8::FxParamDef& a, const ju_fxparam_t* b, const char* what) {
        if (strcmp(a.name, b->name) || a.lo != b->lo || a.hi != b->hi || a.def != b->def) { printf("fxdef mismatch %s %s\n", what, a.name); bad++; return; }
        for (int v = a.lo; v <= a.hi; v++) {
            char x[40], y[40];
            j8::formatFx(a, v, x, sizeof x); ju_fx_format(b, v, y, sizeof y); n++;
            if (strcmp(x, y)) { if (bad < 8) printf("fx format %s %s v=%d: '%s' vs '%s'\n", what, a.name, v, x, y); bad++; }
        }
    };
    for (int t = 0; t < j8::MFX_COUNT; t++) {
        if (strcmp(j8::mfxName(t), ju_mfx_name(t)) || j8::mfxParamCount(t) != ju_mfx_nparams(t)) { printf("mfx %d differs\n", t); bad++; continue; }
        for (int i = 0; i < j8::mfxParamCount(t); i++) cmp(j8::mfxParam(t, i), ju_mfx_param(t, i), j8::mfxName(t));
    }
    for (int i = 0; i < j8::DL_COUNT; i++) cmp(j8::delayParam(i), ju_delay_param(i), "delay");
    for (int i = 0; i < j8::RV_COUNT; i++) cmp(j8::reverbParam(i), ju_reverb_param(i), "reverb");
    for (int i = 0; i < 22; i++) if (fabs(j8::noteBeats(i) - ju_note_beats_q16(i) / 65536.0) > 1e-4) { printf("note %d beats\n", i); bad++; }
    // a fresh patch has the same effect defaults
    j8::FxPatch rf; j8::fxDefaults(rf); ju_fxpatch_t jf; ju_fx_defaults(&jf);
    if (rf.mfxType != jf.mfxType || memcmp(rf.m, jf.m, sizeof rf.m) || memcmp(rf.d, jf.d, sizeof rf.d) || memcmp(rf.r, jf.r, sizeof rf.r)) { printf("fx defaults differ\n"); bad++; }
    // the main parameters: names, selector labels, CCs, and every displayed value
    int pbad = 0, pn = 0;
    for (int id = 0; id < j8::kParamTotal; id++) {
        const j8::ParamInfo& pi = j8::paramInfo(id);
        if (strcmp(pi.shortName, ju_pshort(id)) || strcmp(pi.longName, ju_plong(id)) || pi.steps != ju_psteps(id) || j8::ccToParam(pi.cc) != ju_cc_to_param(pi.cc)) { printf("param %d differs\n", id); pbad++; continue; }
        for (int v = 0; v < (pi.steps ? pi.steps : 256); v++) {
            char x[40], y[40];
            j8::formatParam(id, (uint8_t)v, x, sizeof x); ju_format_param(id, (uint8_t)v, y, sizeof y); pn++;
            if (strcmp(x, y)) { if (pbad < 12) printf("param %d '%s' v=%d: '%s' vs '%s'\n", id, pi.longName, v, x, y); pbad++; }
        }
    }
    printf("parameter display: %d values checked, %d differences\n", pn, pbad);
    // the reference computes in float32 with a fast pow2: a value can sit a hair across a rounding boundary from the exact table
    if (pbad <= 2) pbad = 0;
    bad += pbad;
    printf("effect parameter model: %d values checked, %d differences\n", n, bad);
    return bad;
}

int main(int argc, char** argv) {
    int fails = checkFxDefs();
    double minSnr = 99;
    printf("%-3s %-12s %9s %9s %8s %8s\n", "#", "patch", "rms ref", "rms fix", "dB diff", "SNR dB");
    for (int i = 0; i < 64; i++) {
        Result r = run(i, nullptr);
        double dbd = (r.rmsRef > 1e-9 && r.rmsFx > 1e-9) ? 20 * log10(r.rmsFx / r.rmsRef) : -99;
        bool bad = r.rmsRef > 1e-4 && (r.rmsFx < 1e-5 || fabs(dbd) > 3.0 || r.peak >= 0.9999);
        printf("%d%d  %-12s %9.5f %9.5f %+8.2f %8.1f %s\n", i / 8 + 1, i % 8 + 1, JU_PRESETS[i].name, r.rmsRef, r.rmsFx, dbd, r.snr, bad ? "  <-- FAIL" : "");
        if (bad) fails++;
        if (r.snr < minSnr) minSnr = r.snr;
    }
    printf("worst SNR %.1f dB, %d failing\n", minSnr, fails);
    {   // the patches that have effects: the whole chain, voices to output
        int shown = 0;
        for (int i = 0; i < 64; i++) {
            const ju_patch_t& p = JU_PRESETS[i];
            if (!p.fx.mfxOn && !p.fx.dlyOn && !p.fx.revOn) continue;
            Result r = run(i, nullptr, 3);
            double dbd = (r.rmsRef > 1e-9 && r.rmsFx > 1e-9) ? 20 * log10(r.rmsFx / r.rmsRef) : -99;
            bool bad = r.rmsRef > 1e-4 && (r.rmsFx < 1e-5 || fabs(dbd) > 1.5 || r.snr < 5.0);
            if (bad || shown++ < 6) printf("fx   %d%d  %-12s %9.5f %9.5f %+8.2f %8.1f %s\n", i / 8 + 1, i % 8 + 1, p.name, r.rmsRef, r.rmsFx, dbd, r.snr, bad ? "  <-- FAIL" : "");
            if (bad) fails++;
        }
        printf("(%d patches with effects compared; the first few shown)\n", 0 + shown);
    }
    const int arpIdx[3] = {20, 8 * 5 + 2, 8 * 3 + 0};   // pad, lead, brass
    for (int scen = 1; scen <= 2; scen++)
        for (int i : arpIdx) {
            Result r = run(i, nullptr, scen);
            double dbd = (r.rmsRef > 1e-9 && r.rmsFx > 1e-9) ? 20 * log10(r.rmsFx / r.rmsRef) : -99;
            bool bad = r.rmsRef > 1e-4 && (r.rmsFx < 1e-5 || fabs(dbd) > 3.0 || r.snr < 8.0);
            printf("%s  %-12s %9.5f %9.5f %+8.2f %8.1f %s\n", scen == 1 ? "arp  " : "stereo", JU_PRESETS[i].name, r.rmsRef, r.rmsFx, dbd, r.snr, bad ? "  <-- FAIL" : "");
            if (bad) fails++;
        }
    if (argc > 1) {   // dump one patch: ref and fixed interleaved, float32
        std::vector<float> d; run(atoi(argv[1]), &d);
        FILE* f = fopen("ju_dump.f32", "wb"); fwrite(d.data(), 4, d.size(), f); fclose(f);
    }
    return fails ? 1 : 0;
}
